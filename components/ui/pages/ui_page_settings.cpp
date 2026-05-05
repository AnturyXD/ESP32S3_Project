#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_SET";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;

void ui_page_settings_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views)
{
    if (views != nullptr) {
        views->settings_ssid_label = nullptr;
        views->settings_wifi_label = nullptr;
        views->settings_ip_label = nullptr;
        views->settings_time_state_label = nullptr;
    }

    ui_page_layout_t layout = ui_page_create_layout(screen, "Settings", APP_PAGE_SETTINGS, nav_cb, lv_color_hex(0x11181F));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create settings layout");
        return;
    }

    lv_obj_t *ssid = lv_label_create(layout.content);
    lv_label_set_text(ssid, "SSID: --");
    lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, 2, 16);
    lv_obj_set_style_text_color(ssid, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->settings_ssid_label = ssid;
    }

    lv_obj_t *wifi = lv_label_create(layout.content);
    lv_label_set_text(wifi, "WiFi: --");
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 2, 42);
    lv_obj_set_style_text_color(wifi, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->settings_wifi_label = wifi;
    }

    lv_obj_t *ip = lv_label_create(layout.content);
    lv_label_set_text(ip, "IP: 0.0.0.0");
    lv_obj_align(ip, LV_ALIGN_TOP_LEFT, 2, 68);
    lv_obj_set_style_text_color(ip, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->settings_ip_label = ip;
    }

    lv_obj_t *time_state = lv_label_create(layout.content);
    lv_label_set_text(time_state, "Time: Not Synced");
    lv_obj_align(time_state, LV_ALIGN_TOP_LEFT, 2, 94);
    lv_obj_set_style_text_color(time_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->settings_time_state_label = time_state;
    }

    lv_obj_t *model = lv_label_create(layout.content);
    lv_label_set_text(model, "Model: Not configured");
    lv_obj_align(model, LV_ALIGN_TOP_LEFT, 2, 120);
    lv_obj_set_style_text_color(model, lv_color_hex(kTextPrimary), 0);
}
