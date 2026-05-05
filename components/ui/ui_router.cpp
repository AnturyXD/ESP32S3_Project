#include "ui_router.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "service_network.h"
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
    const bool state_changed = (net_revision != s_last_net_revision) || (time_revision != s_last_time_revision) || force;
    if (!state_changed && (now_us - s_last_status_refresh_us < kStatusRefreshUs)) {
        return;
    }
    s_last_status_refresh_us = now_us;
    s_last_net_revision = net_revision;
    s_last_time_revision = time_revision;

    char ip[16] = {0};
    char last_event[64] = {0};
    char line[96] = {0};

    const char *net_state_str = service_network_is_connected() ? "Online" : service_network_get_state_string();
    service_network_get_ip(ip, sizeof(ip));
    service_network_get_last_event(last_event, sizeof(last_event));

    const char *time_state_str = service_time_get_state_string();
    const char *time_str = service_time_get_time_string();
    bool has_label_change = false;

    if (s_views.home_time_label != nullptr || s_views.home_wifi_label != nullptr) {
        has_label_change |= ui_router_set_label_text(s_views.home_time_label, time_str);
        snprintf(line, sizeof(line), "WiFi: %s", net_state_str);
        has_label_change |= ui_router_set_label_text(s_views.home_wifi_label, line);
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
