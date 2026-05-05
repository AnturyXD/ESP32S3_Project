#include "ui_pages.h"

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_AI";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;

void ui_page_ai_voice_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb)
{
    ui_page_layout_t layout = ui_page_create_layout(screen, "AI Voice", APP_PAGE_AI_VOICE, nav_cb, lv_color_hex(0x0F1923));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create AI Voice layout");
        return;
    }

    lv_obj_t *desc = lv_label_create(layout.content);
    lv_label_set_text(desc, "AI Voice Assistant");
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_text_color(desc, lv_color_hex(kTextPrimary), 0);

    lv_obj_t *start_btn = lv_btn_create(layout.content);
    lv_obj_set_size(start_btn, 120, 46);
    lv_obj_align(start_btn, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x2A4A64), 0);
    lv_obj_set_style_text_color(start_btn, lv_color_hex(kTextPrimary), 0);
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_center(start_label);
}
