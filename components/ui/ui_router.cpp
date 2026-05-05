#include "ui_router.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "ui_pages.h"

static const char *TAG = "UI_ROUTER";

namespace {

constexpr size_t kRouteQueueLen = 8;

static QueueHandle_t s_route_queue = nullptr;
static app_page_id_t s_last_page = APP_PAGE_HOME;

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

    switch (page_id) {
    case APP_PAGE_HOME:
        ui_page_home_create(new_screen, ui_router_nav_request);
        break;
    case APP_PAGE_AI_VOICE:
        ui_page_ai_voice_create(new_screen, ui_router_nav_request);
        break;
    case APP_PAGE_SETTINGS:
        ui_page_settings_create(new_screen, ui_router_nav_request);
        break;
    case APP_PAGE_DEBUG:
        ui_page_debug_create(new_screen, ui_router_nav_request);
        break;
    case APP_PAGE_ABOUT:
        ui_page_about_create(new_screen, ui_router_nav_request);
        break;
    case APP_PAGE_PLACEHOLDER:
        ui_page_placeholder_create(new_screen, ui_router_nav_request);
        break;
    default:
        ESP_LOGW(TAG, "unknown page id, fallback home: %d", static_cast<int>(page_id));
        ui_page_home_create(new_screen, ui_router_nav_request);
        break;
    }

    lv_scr_load(new_screen);
    ESP_LOGI(TAG, "load new page: page_id=%d screen=%p", static_cast<int>(page_id), static_cast<void *>(new_screen));

    if (old_screen != new_screen) {
        lv_obj_del_async(old_screen);
        ESP_LOGI(TAG, "delete old screen async: old_screen=%p", static_cast<void *>(old_screen));
    }

    lv_obj_invalidate(new_screen);
    ESP_LOGI(TAG, "page shown: %s", app_shell_page_name(page_id));
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
        s_last_page = requested_page;
    }
    return has_request;
}
