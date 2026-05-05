#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_ABT";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;

void ui_page_about_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb)
{
    ui_page_layout_t layout = ui_page_create_layout(screen, "About", APP_PAGE_ABOUT, nav_cb, lv_color_hex(0x171520));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create about layout");
        return;
    }

    lv_obj_t *info = lv_label_create(layout.content);
    lv_label_set_text(info, "ESP32-S3 AI Voice Terminal\nV0.3.1 UI Routing Fix");
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 2, 18);
    lv_obj_set_style_text_color(info, lv_color_hex(kTextPrimary), 0);
}
