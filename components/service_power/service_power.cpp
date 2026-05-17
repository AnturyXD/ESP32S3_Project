#include "service_power.h"

#include <stdio.h>
#include <string.h>

#include "bsp_io_expander.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER";

namespace {

constexpr gpio_num_t kPwrGpio = GPIO_NUM_16;
constexpr int kPwrPressedLevel = 0;
constexpr uint32_t kPollMs = 20;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongPressMs = 3000;
constexpr uint32_t kShutdownDelayMs = 300;
constexpr uint8_t kSysEnPin = 6;
constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPrio = 4;

static bool s_inited = false;
static bool s_task_started = false;
static bool s_pwr_pressed = false;
static bool s_shutdown_requested = false;
static bool s_shutdown_done = false;
static bool s_hold_enabled = false;
static TaskHandle_t s_power_task = nullptr;
static power_state_t s_state = POWER_STATE_INIT;
static char s_last_event[64] = "INIT";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void power_set_state(power_state_t state)
{
    power_state_t old_state = POWER_STATE_INIT;
    portENTER_CRITICAL(&s_lock);
    old_state = s_state;
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
    if (old_state != state) {
        ESP_LOGI(TAG, "state change: %d -> %d", static_cast<int>(old_state), static_cast<int>(state));
    }
}

static void power_set_event(const char *event_text)
{
    if (event_text == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_last_event, event_text, sizeof(s_last_event));
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t power_init_input_gpio(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << kPwrGpio);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config failed");
    return ESP_OK;
}

static void power_execute_shutdown(const char *reason)
{
    if (s_shutdown_done) {
        return;
    }

    power_set_state(POWER_STATE_SHUTDOWN_PENDING);
    power_set_event(reason != nullptr ? reason : "SHUTDOWN_PENDING");
    ESP_LOGW(TAG, "long press detected, shutdown pending");

    esp_err_t wifi_err = esp_wifi_stop();
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi stopped before power off");
    } else if (wifi_err != ESP_ERR_WIFI_NOT_INIT && wifi_err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop returned: %s", esp_err_to_name(wifi_err));
    }

    vTaskDelay(pdMS_TO_TICKS(kShutdownDelayMs));
    if (service_power_set_hold_enabled(false) != ESP_OK) {
        power_set_state(POWER_STATE_ERROR);
        power_set_event("POWER_OFF_FAILED");
        return;
    }
    s_shutdown_done = true;
    power_set_state(POWER_STATE_POWER_OFF);
    power_set_event("POWER_OFF_TRIGGERED");
    ESP_LOGW(TAG, "SYS_EN set LOW, battery path should power off; USB VBUS may keep device alive");
}

static void power_task(void *arg)
{
    (void)arg;
    TickType_t stable_since = xTaskGetTickCount();
    TickType_t press_started = 0;
    bool raw_pressed = false;
    bool debounced_pressed = false;

    for (;;) {
        bool now_pressed = (gpio_get_level(kPwrGpio) == kPwrPressedLevel);
        if (now_pressed != raw_pressed) {
            raw_pressed = now_pressed;
            stable_since = xTaskGetTickCount();
        }

        TickType_t stable_ticks = xTaskGetTickCount() - stable_since;
        if (stable_ticks >= pdMS_TO_TICKS(kDebounceMs) && debounced_pressed != raw_pressed) {
            debounced_pressed = raw_pressed;
            portENTER_CRITICAL(&s_lock);
            s_pwr_pressed = debounced_pressed;
            portEXIT_CRITICAL(&s_lock);
            if (debounced_pressed) {
                press_started = xTaskGetTickCount();
                power_set_state(POWER_STATE_PWR_PRESSED);
                power_set_event("PWR_PRESSED");
                ESP_LOGI(TAG, "PWR pressed");
            } else {
                press_started = 0;
                if (!s_shutdown_done) {
                    power_set_state(POWER_STATE_RUNNING);
                }
                power_set_event("PWR_RELEASED");
            }
        }

        bool request_shutdown = false;
        portENTER_CRITICAL(&s_lock);
        request_shutdown = s_shutdown_requested;
        portEXIT_CRITICAL(&s_lock);

        if (!s_shutdown_done && (request_shutdown || (debounced_pressed && press_started > 0 &&
                                                      (xTaskGetTickCount() - press_started) >= pdMS_TO_TICKS(kLongPressMs)))) {
            power_execute_shutdown(request_shutdown ? "SW_REQUEST_SHUTDOWN" : "PWR_LONG_PRESS");
            portENTER_CRITICAL(&s_lock);
            s_shutdown_requested = false;
            portEXIT_CRITICAL(&s_lock);
        }

        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

} // namespace

esp_err_t service_power_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "service_power already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "POWER init");
    power_set_state(POWER_STATE_INIT);
    power_set_event("INIT");

    ESP_RETURN_ON_ERROR(power_init_input_gpio(), TAG, "init PWR input failed");
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "init IO expander failed");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(kSysEnPin, true), TAG, "set SYS_EN direction failed");
    power_set_state(POWER_STATE_HOLD_ON);
    ESP_RETURN_ON_ERROR(service_power_set_hold_enabled(true), TAG, "enable hold failed");

    power_set_state(POWER_STATE_RUNNING);
    power_set_event("RUNNING");
    s_inited = true;
    ESP_LOGI(TAG, "power hold enabled");
    return ESP_OK;
}

esp_err_t service_power_start_task(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task_started) {
        ESP_LOGW(TAG, "power task already started");
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(power_task, "power_task", kTaskStack, nullptr, kTaskPrio, &s_power_task);
    if (ok != pdPASS) {
        power_set_state(POWER_STATE_ERROR);
        power_set_event("TASK_CREATE_FAILED");
        return ESP_FAIL;
    }
    s_task_started = true;
    ESP_LOGI(TAG,
             "power task started (poll=%lums debounce=%lums long_press=%lums)",
             static_cast<unsigned long>(kPollMs),
             static_cast<unsigned long>(kDebounceMs),
             static_cast<unsigned long>(kLongPressMs));
    return ESP_OK;
}

bool service_power_is_pwr_pressed(void)
{
    portENTER_CRITICAL(&s_lock);
    bool pressed = s_pwr_pressed;
    portEXIT_CRITICAL(&s_lock);
    return pressed;
}

esp_err_t service_power_set_hold_enabled(bool enable)
{
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_output(kSysEnPin, enable), TAG, "set SYS_EN failed");
    portENTER_CRITICAL(&s_lock);
    s_hold_enabled = enable;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "service_power set EXIO6 SYS_EN %s", enable ? "HIGH" : "LOW");
    power_set_event(enable ? "SYS_EN_HIGH" : "SYS_EN_LOW");
    return ESP_OK;
}

esp_err_t service_power_request_shutdown(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    s_shutdown_requested = true;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "shutdown requested");
    return ESP_OK;
}

power_state_t service_power_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    power_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_power_get_last_event(void)
{
    static char event_buf[64] = {0};
    portENTER_CRITICAL(&s_lock);
    strlcpy(event_buf, s_last_event, sizeof(event_buf));
    portEXIT_CRITICAL(&s_lock);
    return event_buf;
}
