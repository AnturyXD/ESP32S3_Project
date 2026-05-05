#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_DBG";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;
static constexpr uint32_t kTextSecondary = 0xC8D7EA;

void ui_page_debug_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views)
{
    if (views != nullptr) {
        views->debug_network_label = nullptr;
        views->debug_ip_label = nullptr;
        views->debug_last_event_label = nullptr;
    }

    ui_page_layout_t layout = ui_page_create_layout(screen, "Debug", APP_PAGE_DEBUG, nav_cb, lv_color_hex(0x0E141B));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create debug layout");
        return;
    }

    lv_obj_t *net = lv_label_create(layout.content);
    lv_label_set_text(net, "Network: --");
    lv_obj_align(net, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_text_color(net, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_network_label = net;
    }

    lv_obj_t *ip = lv_label_create(layout.content);
    lv_label_set_text(ip, "IP: 0.0.0.0");
    lv_obj_align(ip, LV_ALIGN_TOP_LEFT, 2, 30);
    lv_obj_set_style_text_color(ip, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_ip_label = ip;
    }

    lv_obj_t *evt = lv_label_create(layout.content);
    lv_label_set_text(evt, "Last Event: --");
    lv_obj_align(evt, LV_ALIGN_TOP_LEFT, 2, 58);
    lv_obj_set_style_text_color(evt, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_last_event_label = evt;
    }
}
