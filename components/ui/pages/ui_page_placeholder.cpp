#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_PH";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;

void ui_page_placeholder_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views)
{
    (void)views;
    ui_page_layout_t layout = ui_page_create_layout(screen, "Placeholder", APP_PAGE_PLACEHOLDER, nav_cb, lv_color_hex(0x141414));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create placeholder layout");
        return;
    }

    lv_obj_t *msg = lv_label_create(layout.content);
    lv_label_set_text(msg, "Reserved for future app extension.");
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 2, 18);
    lv_obj_set_style_text_color(msg, lv_color_hex(kTextPrimary), 0);
}
