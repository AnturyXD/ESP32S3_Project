#include "service_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "service_network.h"

static const char *TAG = "TIME";

namespace {

static time_state_t s_time_state = TIME_STATE_NOT_SYNCED;
static bool s_inited = false;
static bool s_sntp_started = false;
static TimerHandle_t s_sync_watchdog = nullptr;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_revision = 0;
constexpr uint32_t kSyncTimeoutMs = 15000;

static void time_set_state(time_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = s_time_state != state;
    s_time_state = state;
    if (changed) {
        ++s_revision;
    }
    portEXIT_CRITICAL(&s_lock);
}

static bool time_is_valid_now(void)
{
    time_t now = 0;
    time(&now);
    return now > 1700000000; // Roughly after Nov 2023.
}

static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    time_set_state(TIME_STATE_SYNCED);
    if (s_sync_watchdog != nullptr) {
        xTimerStop(s_sync_watchdog, 0);
    }
    ESP_LOGI(TAG, "SNTP time synced");
}

static void time_sync_watchdog_cb(TimerHandle_t timer)
{
    (void)timer;
    if (time_is_valid_now()) {
        time_set_state(TIME_STATE_SYNCED);
        ESP_LOGI(TAG, "SNTP sync watchdog: time already valid");
        return;
    }

    time_set_state(TIME_STATE_ERROR);
    ESP_LOGW(TAG, "SNTP sync timeout");
}

static void time_start_sntp_sync(void)
{
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "ntp.ntsc.ac.cn");
        esp_sntp_setservername(2, "pool.ntp.org");
        esp_sntp_set_time_sync_notification_cb(time_sync_cb);
        esp_sntp_init();
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP started (aliyun/ntsc/pool)");
    } else {
        esp_sntp_restart();
        ESP_LOGI(TAG, "SNTP restarted");
    }

    if (s_sync_watchdog == nullptr) {
        s_sync_watchdog = xTimerCreate("time_sync", pdMS_TO_TICKS(kSyncTimeoutMs), pdFALSE, nullptr, time_sync_watchdog_cb);
    }
    if (s_sync_watchdog != nullptr) {
        xTimerChangePeriod(s_sync_watchdog, pdMS_TO_TICKS(kSyncTimeoutMs), 0);
    }

    time_set_state(TIME_STATE_SYNCING);
}

static void time_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        time_set_state(TIME_STATE_NOT_SYNCED);
        ESP_LOGW(TAG, "Wi-Fi disconnected, time marked not synced");
    }
}

static void time_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    if (time_is_valid_now()) {
        time_set_state(TIME_STATE_SYNCED);
        ESP_LOGI(TAG, "Time already valid, skip SNTP restart");
        return;
    }

    ESP_LOGI(TAG, "IP ready, start SNTP sync");
    time_start_sntp_sync();
}

} // namespace

esp_err_t service_time_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "service_time already initialized");
        return ESP_OK;
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &time_wifi_event_handler, nullptr),
                        TAG,
                        "register WIFI_EVENT_STA_DISCONNECTED failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &time_ip_event_handler, nullptr),
                        TAG,
                        "register IP_EVENT_STA_GOT_IP failed");

    time_set_state(time_is_valid_now() ? TIME_STATE_SYNCED : TIME_STATE_NOT_SYNCED);
    if (service_network_get_state() == NETWORK_STATE_GOT_IP && !time_is_valid_now()) {
        ESP_LOGI(TAG, "Network already has IP, start initial SNTP sync");
        time_start_sntp_sync();
    }
    s_inited = true;
    ESP_LOGI(TAG, "service_time initialized");
    return ESP_OK;
}

time_state_t service_time_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    time_state_t state = s_time_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_time_get_state_str(time_state_t state)
{
    switch (state) {
    case TIME_STATE_NOT_SYNCED:
        return "Not Synced";
    case TIME_STATE_SYNCING:
        return "Syncing";
    case TIME_STATE_SYNCED:
        return "Synced";
    case TIME_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

const char *service_time_get_state_string(void)
{
    return service_time_get_state_str(service_time_get_state());
}

void service_time_get_current_time(char *time_buf, size_t time_buf_size)
{
    if (time_buf == nullptr || time_buf_size == 0) {
        return;
    }

    if (!time_is_valid_now()) {
        strlcpy(time_buf, "--:--", time_buf_size);
        return;
    }

    time_t now = 0;
    struct tm timeinfo = {};
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_buf, time_buf_size, "%H:%M:%S", &timeinfo);
}

const char *service_time_get_time_string(void)
{
    static char time_buf[16] = {0};
    service_time_get_current_time(time_buf, sizeof(time_buf));
    return time_buf;
}

bool service_time_is_synced(void)
{
    return service_time_get_state() == TIME_STATE_SYNCED;
}

uint32_t service_time_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}
