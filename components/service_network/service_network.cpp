#include "service_network.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI";

namespace {

constexpr size_t kIpBufLen = 16;
constexpr size_t kEventBufLen = 64;
constexpr uint32_t kReconnectDelayMs = 1500;

static network_state_t s_state = NETWORK_STATE_IDLE;
static char s_ip[kIpBufLen] = "0.0.0.0";
static char s_last_event[kEventBufLen] = "IDLE";
static bool s_inited = false;
static esp_netif_t *s_sta_netif = nullptr;
static TimerHandle_t s_reconnect_timer = nullptr;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_is_started = false;
static bool s_is_connecting = false;
static bool s_is_connected = false;
static bool s_got_ip = false;
static bool s_manual_disconnect = false;
static uint32_t s_reconnect_count = 0;
static uint32_t s_revision = 0;

static void network_try_connect(const char *trigger);

static void network_set_state(network_state_t state)
{
    network_state_t old_state = NETWORK_STATE_IDLE;
    portENTER_CRITICAL(&s_lock);
    old_state = s_state;
    s_state = state;
    portEXIT_CRITICAL(&s_lock);

    if (old_state != state) {
        portENTER_CRITICAL(&s_lock);
        ++s_revision;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "state change: %s -> %s", service_network_get_state_str(old_state), service_network_get_state_str(state));
    }
}

static void network_set_ip(const char *ip)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = strncmp(s_ip, ip ? ip : "0.0.0.0", sizeof(s_ip)) != 0;
    strlcpy(s_ip, ip ? ip : "0.0.0.0", sizeof(s_ip));
    if (changed) {
        ++s_revision;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void network_set_last_event(const char *evt)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = strncmp(s_last_event, evt ? evt : "UNKNOWN", sizeof(s_last_event)) != 0;
    strlcpy(s_last_event, evt ? evt : "UNKNOWN", sizeof(s_last_event));
    if (changed) {
        ++s_revision;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void network_clear_ip(void)
{
    network_set_ip("0.0.0.0");
}

static void network_schedule_reconnect(uint32_t delay_ms, const char *reason)
{
    bool should_schedule = false;
    portENTER_CRITICAL(&s_lock);
    should_schedule = s_is_started && !s_manual_disconnect && !s_is_connected && !s_got_ip;
    portEXIT_CRITICAL(&s_lock);

    if (!should_schedule) {
        ESP_LOGI(TAG, "skip reconnect schedule, reason=%s", reason ? reason : "unknown");
        return;
    }

    if (s_reconnect_timer == nullptr) {
        s_reconnect_timer =
            xTimerCreate("wifi_rc", pdMS_TO_TICKS(kReconnectDelayMs), pdFALSE, nullptr, [](TimerHandle_t timer) {
                (void)timer;
                network_try_connect("reconnect_timer");
            });
        if (s_reconnect_timer == nullptr) {
            network_set_state(NETWORK_STATE_ERROR);
            network_set_last_event("RECONNECT_TIMER_FAIL");
            ESP_LOGE(TAG, "failed to create reconnect timer");
            return;
        }
    }

    if (xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(delay_ms), 0) != pdPASS) {
        network_set_state(NETWORK_STATE_ERROR);
        network_set_last_event("RECONNECT_SCHEDULE_FAIL");
        ESP_LOGE(TAG, "failed to schedule reconnect timer");
        return;
    }
    ESP_LOGW(TAG, "reconnect scheduled in %lu ms, reason=%s", static_cast<unsigned long>(delay_ms), reason ? reason : "unknown");
}

static void network_try_connect(const char *trigger)
{
    bool can_connect = false;
    uint32_t reconnect_count = 0;

    portENTER_CRITICAL(&s_lock);
    if (s_is_started && !s_is_connecting && !s_is_connected && !s_got_ip) {
        s_is_connecting = true;
        can_connect = true;
        reconnect_count = s_reconnect_count;
    }
    portEXIT_CRITICAL(&s_lock);

    if (!can_connect) {
        ESP_LOGI(TAG, "skip esp_wifi_connect, trigger=%s", trigger ? trigger : "unknown");
        return;
    }

    network_set_state(NETWORK_STATE_CONNECTING);
    network_set_last_event("CONNECTING");
    ESP_LOGI(TAG,
             "esp_wifi_connect trigger=%s reconnect_count=%lu",
             trigger ? trigger : "unknown",
             static_cast<unsigned long>(reconnect_count));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_is_connecting = false;
        portEXIT_CRITICAL(&s_lock);
        network_set_state(NETWORK_STATE_ERROR);
        network_set_last_event("CONNECT_FAIL");
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
}

static void network_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        portENTER_CRITICAL(&s_lock);
        s_is_started = true;
        s_is_connecting = false;
        s_is_connected = false;
        s_got_ip = false;
        s_manual_disconnect = false;
        portEXIT_CRITICAL(&s_lock);
        network_set_last_event("STA_START");
        network_set_state(NETWORK_STATE_CONNECTING);
        ESP_LOGI(TAG, "event: WIFI_EVENT_STA_START");
        network_try_connect("sta_start");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        portENTER_CRITICAL(&s_lock);
        s_is_connecting = false;
        s_is_connected = true;
        s_got_ip = false;
        portEXIT_CRITICAL(&s_lock);
        network_set_state(NETWORK_STATE_CONNECTED);
        network_set_last_event("STA_CONNECTED");
        ESP_LOGI(TAG, "event: WIFI_EVENT_STA_CONNECTED");
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disc = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        int reason = disc ? disc->reason : -1;
        char evt[kEventBufLen] = {0};
        snprintf(evt, sizeof(evt), "STA_DISCONNECTED(%d)", reason);

        bool manual_disconnect = false;
        uint32_t reconnect_count = 0;
        portENTER_CRITICAL(&s_lock);
        s_is_connecting = false;
        s_is_connected = false;
        s_got_ip = false;
        manual_disconnect = s_manual_disconnect;
        if (!manual_disconnect) {
            ++s_reconnect_count;
        }
        reconnect_count = s_reconnect_count;
        portEXIT_CRITICAL(&s_lock);

        network_set_state(NETWORK_STATE_DISCONNECTED);
        network_set_last_event(evt);
        network_clear_ip();
        ESP_LOGW(TAG, "event: WIFI_EVENT_STA_DISCONNECTED, reason=%d reconnect_count=%lu", reason, static_cast<unsigned long>(reconnect_count));

        if (!manual_disconnect) {
            network_schedule_reconnect(kReconnectDelayMs, "sta_disconnected");
        }
        break;
    }
    default:
        break;
    }
}

static void network_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *got_ip = static_cast<ip_event_got_ip_t *>(event_data);
    if (got_ip == nullptr) {
        network_set_state(NETWORK_STATE_ERROR);
        network_set_last_event("GOT_IP_NULL");
        ESP_LOGE(TAG, "event: IP_EVENT_STA_GOT_IP with null data");
        return;
    }

    char ip_buf[kIpBufLen] = {0};
    snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&got_ip->ip_info.ip));
    portENTER_CRITICAL(&s_lock);
    s_is_connecting = false;
    s_is_connected = true;
    s_got_ip = true;
    s_reconnect_count = 0;
    portEXIT_CRITICAL(&s_lock);
    network_set_ip(ip_buf);
    network_set_state(NETWORK_STATE_GOT_IP);
    network_set_last_event("STA_GOT_IP");
    ESP_LOGI(TAG, "event: IP_EVENT_STA_GOT_IP, ip=%s", ip_buf);
}

} // namespace

esp_err_t service_network_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "service_network already initialized");
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "esp_event_loop_create_default failed");
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == nullptr) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &network_wifi_event_handler, nullptr),
                        TAG,
                        "register WIFI_EVENT_STA_START failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &network_wifi_event_handler, nullptr),
                        TAG,
                        "register WIFI_EVENT_STA_CONNECTED failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &network_wifi_event_handler, nullptr),
                        TAG,
                        "register WIFI_EVENT_STA_DISCONNECTED failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_ip_event_handler, nullptr),
                        TAG,
                        "register IP_EVENT_STA_GOT_IP failed");

    wifi_config_t wifi_config = {};
    strlcpy(reinterpret_cast<char *>(wifi_config.sta.ssid), WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy(reinterpret_cast<char *>(wifi_config.sta.password), WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "esp_wifi_set_storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    network_set_state(NETWORK_STATE_CONNECTING);
    network_set_last_event("WIFI_STARTING");
    network_clear_ip();
    s_inited = true;
    ESP_LOGI(TAG, "Wi-Fi STA init done, SSID=%s", WIFI_SSID);
    return ESP_OK;
}

network_state_t service_network_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    network_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_network_get_state_str(network_state_t state)
{
    switch (state) {
    case NETWORK_STATE_IDLE:
        return "Idle";
    case NETWORK_STATE_CONNECTING:
        return "Connecting";
    case NETWORK_STATE_CONNECTED:
        return "Connected";
    case NETWORK_STATE_GOT_IP:
        return "Online";
    case NETWORK_STATE_DISCONNECTED:
        return "Disconnected";
    case NETWORK_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

void service_network_get_ip(char *ip_buf, size_t ip_buf_size)
{
    if (ip_buf == nullptr || ip_buf_size == 0) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(ip_buf, s_ip, ip_buf_size);
    portEXIT_CRITICAL(&s_lock);
}

const char *service_network_get_ssid(void)
{
    return WIFI_SSID;
}

const char *service_network_get_state_string(void)
{
    return service_network_get_state_str(service_network_get_state());
}

const char *service_network_get_ip_string(void)
{
    static char ip_buf[kIpBufLen] = {0};
    service_network_get_ip(ip_buf, sizeof(ip_buf));
    return ip_buf;
}

bool service_network_is_connected(void)
{
    network_state_t state = service_network_get_state();
    return state == NETWORK_STATE_CONNECTED || state == NETWORK_STATE_GOT_IP;
}

uint32_t service_network_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}

void service_network_get_last_event(char *event_buf, size_t event_buf_size)
{
    if (event_buf == nullptr || event_buf_size == 0) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(event_buf, s_last_event, event_buf_size);
    portEXIT_CRITICAL(&s_lock);
}
