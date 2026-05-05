#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_SET";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;

void ui_page_settings_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb)
{
    ui_page_layout_t layout = ui_page_create_layout(screen, "Settings", APP_PAGE_SETTINGS, nav_cb, lv_color_hex(0x11181F));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create settings layout");
        return;
    }

    lv_obj_t *wifi = lv_label_create(layout.content);
    lv_label_set_text(wifi, "WiFi: Not configured");
    lv_obj_align(wifi, LV_ALIGN_TOP_LEFT, 2, 20);
    lv_obj_set_style_text_color(wifi, lv_color_hex(kTextPrimary), 0);

    lv_obj_t *model = lv_label_create(layout.content);
    lv_label_set_text(model, "Model: Not configured");
    lv_obj_align(model, LV_ALIGN_TOP_LEFT, 2, 48);
    lv_obj_set_style_text_color(model, lv_color_hex(kTextPrimary), 0);
}
