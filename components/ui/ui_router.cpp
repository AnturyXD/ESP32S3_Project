#include "ui_router.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "service_audio.h"
#include "service_ai.h"
#include "service_cloud.h"
#include "service_network.h"
#include "service_storage.h"
#include "service_time.h"
#include "ui_pages.h"

static const char *TAG = "UI_ROUTER";

namespace {

constexpr size_t kRouteQueueLen = 8;
constexpr int64_t kStatusRefreshUs = 1000 * 1000;

static QueueHandle_t s_route_queue = nullptr;
static app_page_id_t s_last_page = APP_PAGE_HOME;
static app_page_id_t s_active_page = APP_PAGE_HOME;
static int64_t s_last_status_refresh_us = 0;
static ui_page_status_views_t s_views = {};
static uint32_t s_missing_label_warn_count = 0;
static uint32_t s_last_net_revision = 0;
static uint32_t s_last_time_revision = 0;
static uint32_t s_last_audio_revision = 0;
static uint32_t s_last_ai_revision = 0;
static uint32_t s_last_cloud_revision = 0;
static uint32_t s_last_storage_revision = 0;
static lv_obj_t *s_bound_screen = nullptr;

static void ui_router_clear_views(void)
{
    memset(&s_views, 0, sizeof(s_views));
}

static bool ui_router_set_label_text(lv_obj_t *label, const char *text)
{
    if (label == nullptr || text == nullptr) {
        if (label == nullptr && (s_missing_label_warn_count++ % 10 == 0)) {
            ESP_LOGW(TAG, "status label missing, skip update");
        }
        return false;
    }
    const char *current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0) {
        return false;
    }
    lv_label_set_text(label, text);
    lv_obj_invalidate(label);
    return true;
}

static bool ui_router_is_ascii_text(const char *text)
{
    if (text == nullptr) {
        return true;
    }
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p != '\0'; ++p) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            continue;
        }
        if (*p < 0x20 || *p > 0x7E) {
            return false;
        }
    }
    return true;
}

static const char *ui_router_screen_text_or_fallback(const char *text)
{
    /*
     * 当前产品策略是不在屏幕上显示中文原文。LVGL 未接入中文字体时直接显示 UTF-8
     * 会出现乱码或方块，因此 UI 只显示 ASCII；中文/非 ASCII 原文保留在串口和服务器日志。
     */
    if (text == nullptr || text[0] == '\0') {
        return "--";
    }
    return ui_router_is_ascii_text(text) ? text : "Non-ASCII text received";
}

static void ui_router_screen_delete_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }

    lv_obj_t *screen = lv_event_get_target(event);
    if (screen == s_bound_screen) {
        ESP_LOGI(TAG, "active screen deleted, clear view bindings");
        ui_router_clear_views();
        s_bound_screen = nullptr;
    }
}

static void ui_router_nav_request(app_page_id_t page_id)
{
    esp_err_t err = app_shell_navigate(page_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "navigate failed: %s", app_shell_page_name(page_id));
    }
}

static void ui_router_show_page_locked(app_page_id_t page_id)
{
    lv_obj_t *old_screen = lv_scr_act();
    if (old_screen == nullptr) {
        ESP_LOGE(TAG, "active screen is null");
        return;
    }

    lv_obj_t *new_screen = lv_obj_create(nullptr);
    if (new_screen == nullptr) {
        ESP_LOGE(TAG, "new screen create failed, page=%d", static_cast<int>(page_id));
        return;
    }

    ESP_LOGI(TAG, "create new page: page_id=%d new_screen=%p", static_cast<int>(page_id), static_cast<void *>(new_screen));
    ui_router_clear_views();

    switch (page_id) {
    case APP_PAGE_HOME:
        ui_page_home_create(new_screen, ui_router_nav_request, &s_views);
        break;
    case APP_PAGE_AI_VOICE:
        ui_page_ai_voice_create(new_screen, ui_router_nav_request, &s_views);
        break;
    case APP_PAGE_SETTINGS:
        ui_page_settings_create(new_screen, ui_router_nav_request, &s_views);
        break;
    case APP_PAGE_DEBUG:
        ui_page_debug_create(new_screen, ui_router_nav_request, &s_views);
        break;
    case APP_PAGE_ABOUT:
        ui_page_about_create(new_screen, ui_router_nav_request, &s_views);
        break;
    case APP_PAGE_PLACEHOLDER:
        ui_page_placeholder_create(new_screen, ui_router_nav_request, &s_views);
        break;
    default:
        ESP_LOGW(TAG, "unknown page id, fallback home: %d", static_cast<int>(page_id));
        page_id = APP_PAGE_HOME;
        ui_page_home_create(new_screen, ui_router_nav_request, &s_views);
        break;
    }

    s_active_page = page_id;
    s_bound_screen = new_screen;
    lv_obj_add_event_cb(new_screen, ui_router_screen_delete_cb, LV_EVENT_DELETE, nullptr);
    lv_scr_load(new_screen);
    ESP_LOGI(TAG, "load new page: page_id=%d screen=%p", static_cast<int>(page_id), static_cast<void *>(new_screen));

    if (old_screen != new_screen) {
        lv_obj_del_async(old_screen);
        ESP_LOGI(TAG, "delete old screen async: old_screen=%p", static_cast<void *>(old_screen));
    }

    lv_obj_invalidate(new_screen);
    ESP_LOGI(TAG, "page shown: %s", app_shell_page_name(page_id));
}

static void ui_router_refresh_status_internal_locked(bool force)
{
    const int64_t now_us = esp_timer_get_time();
    const uint32_t net_revision = service_network_get_revision();
    const uint32_t time_revision = service_time_get_revision();
    const uint32_t audio_revision = service_audio_get_revision();
    const uint32_t ai_revision = service_ai_get_revision();
    const uint32_t cloud_revision = service_cloud_get_revision();
    const uint32_t storage_revision = service_storage_get_revision();
    const bool state_changed =
        (net_revision != s_last_net_revision) || (time_revision != s_last_time_revision) ||
        (audio_revision != s_last_audio_revision) || (ai_revision != s_last_ai_revision) ||
        (cloud_revision != s_last_cloud_revision) ||
        (storage_revision != s_last_storage_revision) || force;
    if (!state_changed && (now_us - s_last_status_refresh_us < kStatusRefreshUs)) {
        return;
    }
    s_last_status_refresh_us = now_us;
    s_last_net_revision = net_revision;
    s_last_time_revision = time_revision;
    s_last_audio_revision = audio_revision;
    s_last_ai_revision = ai_revision;
    s_last_cloud_revision = cloud_revision;
    s_last_storage_revision = storage_revision;

    char ip[16] = {0};
    char last_event[64] = {0};
    char line[128] = {0};

    const char *net_state_str = service_network_is_connected() ? "Online" : service_network_get_state_string();
    service_network_get_ip(ip, sizeof(ip));
    service_network_get_last_event(last_event, sizeof(last_event));

    const char *time_state_str = service_time_get_state_string();
    const char *time_str = service_time_get_time_string();
    const char *audio_state_str = service_audio_get_state_string();
    const char *audio_event_str = service_audio_get_last_event();
    const uint32_t audio_pcm_bytes = service_audio_get_last_pcm_bytes();
    const uint16_t audio_peak = service_audio_get_peak_level();
    const bool audio_recording = service_audio_is_recording();
    const bool audio_playing = service_audio_is_playing();
    const char *storage_state_str = service_storage_get_state_string();
    const char *storage_event_str = service_storage_get_last_event();
    const char *storage_error_str = service_storage_get_last_error();
    const bool storage_mounted = service_storage_is_mounted();
    const uint64_t storage_total_mb = service_storage_get_total_bytes() / (1024ULL * 1024ULL);
    const uint64_t storage_free_mb = service_storage_get_free_bytes() / (1024ULL * 1024ULL);
    const char *cloud_state_str = service_cloud_get_state_string();
    const bool cloud_registered = service_cloud_is_registered();
    const char *cloud_heartbeat_str = service_cloud_get_last_heartbeat();
    const int cloud_http_status = service_cloud_get_last_http_status();
    const char *cloud_error_str = service_cloud_get_last_error();
    const char *ai_state_str = service_ai_get_asr_state_string();
    const bool ai_recording = service_ai_is_asr_recording();
    const float ai_sent_seconds = service_ai_get_asr_sent_seconds();
    const char *ai_partial_text = service_ai_get_partial_text();
    const char *ai_final_text = service_ai_get_final_text();
    const char *ai_error_str = service_ai_get_last_error();
    bool has_label_change = false;

    if (s_views.home_time_label != nullptr || s_views.home_wifi_label != nullptr) {
        has_label_change |= ui_router_set_label_text(s_views.home_time_label, time_str);
        snprintf(line, sizeof(line), "WiFi: %s", net_state_str);
        has_label_change |= ui_router_set_label_text(s_views.home_wifi_label, line);
    }

    if (s_views.ai_audio_state_label != nullptr || s_views.ai_pcm_bytes_label != nullptr || s_views.ai_peak_label != nullptr) {
        snprintf(line, sizeof(line), "Audio State: %s", audio_state_str);
        has_label_change |= ui_router_set_label_text(s_views.ai_audio_state_label, line);

        snprintf(line, sizeof(line), "Last PCM Bytes: %lu", static_cast<unsigned long>(audio_pcm_bytes));
        has_label_change |= ui_router_set_label_text(s_views.ai_pcm_bytes_label, line);

        snprintf(line, sizeof(line), "Peak Level: %u", static_cast<unsigned>(audio_peak));
        has_label_change |= ui_router_set_label_text(s_views.ai_peak_label, line);
    }

    if (s_views.ai_asr_state_label != nullptr || s_views.ai_asr_partial_label != nullptr ||
        s_views.ai_asr_final_label != nullptr) {
        snprintf(line, sizeof(line), "ASR State: %s", ai_state_str);
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_state_label, line);

        snprintf(line, sizeof(line), "Server: %s", cloud_state_str);
        has_label_change |= ui_router_set_label_text(s_views.ai_server_state_label, line);

        snprintf(line, sizeof(line), "Recording: %s", ai_recording ? "Yes" : "No");
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_recording_label, line);

        snprintf(line, sizeof(line), "Sent: %.1fs", static_cast<double>(ai_sent_seconds));
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_sent_label, line);

        snprintf(line, sizeof(line), "Partial: %s", ui_router_screen_text_or_fallback(ai_partial_text));
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_partial_label, line);

        snprintf(line, sizeof(line), "Final: %s", ui_router_screen_text_or_fallback(ai_final_text));
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_final_label, line);

        snprintf(line, sizeof(line), "ASR Error: %s", ai_error_str);
        has_label_change |= ui_router_set_label_text(s_views.ai_asr_error_label, line);
    }

    if (s_active_page == APP_PAGE_SETTINGS) {
        snprintf(line, sizeof(line), "SSID: %s", service_network_get_ssid());
        has_label_change |= ui_router_set_label_text(s_views.settings_ssid_label, line);

        snprintf(line, sizeof(line), "WiFi: %s", net_state_str);
        has_label_change |= ui_router_set_label_text(s_views.settings_wifi_label, line);

        snprintf(line, sizeof(line), "IP: %s", ip);
        has_label_change |= ui_router_set_label_text(s_views.settings_ip_label, line);

        snprintf(line, sizeof(line), "Time: %s", time_state_str);
        has_label_change |= ui_router_set_label_text(s_views.settings_time_state_label, line);
    } else if (s_active_page == APP_PAGE_DEBUG) {
        snprintf(line, sizeof(line), "Network: %s", net_state_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_network_label, line);

        snprintf(line, sizeof(line), "IP: %s", ip);
        has_label_change |= ui_router_set_label_text(s_views.debug_ip_label, line);

        snprintf(line, sizeof(line), "Last Event: %s", last_event);
        has_label_change |= ui_router_set_label_text(s_views.debug_last_event_label, line);

        snprintf(line, sizeof(line), "Audio State: %s", audio_state_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_state_label, line);

        snprintf(line, sizeof(line), "Last Audio Event: %s", audio_event_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_event_label, line);

        snprintf(line, sizeof(line), "Last PCM Bytes: %lu", static_cast<unsigned long>(audio_pcm_bytes));
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_pcm_label, line);

        snprintf(line, sizeof(line), "Peak Level: %u", static_cast<unsigned>(audio_peak));
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_peak_label, line);

        snprintf(line, sizeof(line), "Recording: %s", audio_recording ? "Yes" : "No");
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_rec_label, line);

        snprintf(line, sizeof(line), "Playing: %s", audio_playing ? "Yes" : "No");
        has_label_change |= ui_router_set_label_text(s_views.debug_audio_play_label, line);

        snprintf(line, sizeof(line), "Storage State: %s", storage_state_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_state_label, line);

        snprintf(line, sizeof(line), "SD Mounted: %s", storage_mounted ? "Yes" : "No");
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_mounted_label, line);

        snprintf(line, sizeof(line), "Total: %llu MB", static_cast<unsigned long long>(storage_total_mb));
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_total_label, line);

        snprintf(line, sizeof(line), "Free: %llu MB", static_cast<unsigned long long>(storage_free_mb));
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_free_label, line);

        snprintf(line, sizeof(line), "Storage Event: %s", storage_event_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_event_label, line);

        snprintf(line, sizeof(line), "Storage Error: %s", storage_error_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_storage_error_label, line);

        snprintf(line, sizeof(line), "Cloud State: %s", cloud_state_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_cloud_state_label, line);

        snprintf(line, sizeof(line), "Registered: %s", cloud_registered ? "Yes" : "No");
        has_label_change |= ui_router_set_label_text(s_views.debug_cloud_registered_label, line);

        snprintf(line, sizeof(line), "Last Heartbeat: %s", cloud_heartbeat_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_cloud_heartbeat_label, line);

        snprintf(line, sizeof(line), "Last HTTP: %d", cloud_http_status);
        has_label_change |= ui_router_set_label_text(s_views.debug_cloud_http_label, line);

        snprintf(line, sizeof(line), "Cloud Error: %s", cloud_error_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_cloud_error_label, line);

        snprintf(line, sizeof(line), "AI State: %s", ai_state_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_ai_state_label, line);

        snprintf(line, sizeof(line), "ASR Sent: %.1fs", static_cast<double>(ai_sent_seconds));
        has_label_change |= ui_router_set_label_text(s_views.debug_ai_sent_label, line);

        snprintf(line, sizeof(line), "ASR Error: %s", ai_error_str);
        has_label_change |= ui_router_set_label_text(s_views.debug_ai_error_label, line);
    }

    (void)has_label_change;
}

static esp_err_t ui_router_route_handler(app_page_id_t page_id, void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGI(TAG, "route request queued: current_page=%d target_page=%d", static_cast<int>(s_last_page), static_cast<int>(page_id));
    if (s_route_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_route_queue, &page_id, 0) != pdTRUE) {
        ESP_LOGW(TAG, "route queue full, drop page: %s", app_shell_page_name(page_id));
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

} // namespace

esp_err_t ui_router_init(void)
{
    if (s_route_queue != nullptr) {
        return ESP_OK;
    }

    s_route_queue = xQueueCreate(kRouteQueueLen, sizeof(app_page_id_t));
    if (s_route_queue == nullptr) {
        ESP_LOGE(TAG, "failed to create route queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(app_shell_set_route_handler(ui_router_route_handler, nullptr), TAG, "set route handler failed");
    ESP_LOGI(TAG, "router initialized");
    return ESP_OK;
}

void ui_router_show_boot_placeholder_locked(void)
{
    lv_obj_t *old_screen = lv_scr_act();
    if (old_screen == nullptr) {
        ESP_LOGE(TAG, "boot placeholder failed: active screen null");
        return;
    }

    lv_obj_t *screen = lv_obj_create(nullptr);
    if (screen == nullptr) {
        ESP_LOGE(TAG, "boot placeholder failed: new screen null");
        return;
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "UI Ready\nWaiting App Shell...");
    lv_obj_center(label);

    lv_scr_load(screen);
    if (old_screen != screen) {
        lv_obj_del_async(old_screen);
    }
    lv_obj_invalidate(screen);
}

bool ui_router_drain_pending_locked(void)
{
    if (s_route_queue == nullptr) {
        return false;
    }

    app_page_id_t requested_page = APP_PAGE_HOME;
    bool has_request = false;
    while (xQueueReceive(s_route_queue, &requested_page, 0) == pdTRUE) {
        has_request = true;
    }

    if (has_request) {
        ESP_LOGI(TAG, "route dispatch: current_page=%d target_page=%d", static_cast<int>(s_last_page), static_cast<int>(requested_page));
        ui_router_show_page_locked(requested_page);
        ui_router_refresh_status_internal_locked(true);
        s_last_page = requested_page;
    }
    return has_request;
}

void ui_router_refresh_status_locked(void)
{
    ui_router_refresh_status_internal_locked(true);

    lv_obj_t *active = lv_scr_act();
    if (active != nullptr) {
        lv_obj_invalidate(active);
        lv_refr_now(nullptr);
    }
}
