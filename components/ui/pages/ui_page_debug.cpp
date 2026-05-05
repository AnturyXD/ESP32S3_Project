#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_DBG";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;
static constexpr uint32_t kTextSecondary = 0xC8D7EA;

void ui_page_debug_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb)
{
    ui_page_layout_t layout = ui_page_create_layout(screen, "Debug", APP_PAGE_DEBUG, nav_cb, lv_color_hex(0x0E141B));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create debug layout");
        return;
    }

    lv_obj_t *log_title = lv_label_create(layout.content);
    lv_label_set_text(log_title, "Recent Logs (Placeholder)");
    lv_obj_align(log_title, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_text_color(log_title, lv_color_hex(kTextPrimary), 0);

    lv_obj_t *log_body = lv_label_create(layout.content);
    lv_label_set_text(log_body, "No log cache connected in V0.3.1");
    lv_obj_align(log_body, LV_ALIGN_TOP_LEFT, 2, 30);
    lv_obj_set_style_text_color(log_body, lv_color_hex(kTextSecondary), 0);
}
