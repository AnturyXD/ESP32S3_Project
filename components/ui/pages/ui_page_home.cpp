#include "ui_pages.h"

#include <stddef.h>

#include "esp_log.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_HOME";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;
static constexpr uint32_t kTextSecondary = 0xC8D7EA;

namespace {

typedef struct {
    ui_page_nav_cb_t nav_cb;
    app_page_id_t target_page;
} home_card_ctx_t;

static home_card_ctx_t s_card_ctx[5];

static void home_card_click_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    home_card_ctx_t *ctx = static_cast<home_card_ctx_t *>(lv_event_get_user_data(event));
    if (ctx == nullptr || ctx->nav_cb == nullptr) {
        ESP_LOGE(TAG, "home card callback context invalid");
        return;
    }

    ESP_LOGI(TAG, "home card clicked: target_page=%d", static_cast<int>(ctx->target_page));
    ctx->nav_cb(ctx->target_page);
}

} // namespace

void ui_page_home_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb)
{
    ui_page_layout_t layout = ui_page_create_layout(screen, "Home", APP_PAGE_HOME, nav_cb, lv_color_hex(0x101820));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create home layout");
        return;
    }

    lv_obj_t *time_label = lv_label_create(layout.content);
    lv_label_set_text(time_label, "--:--");
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_text_color(time_label, lv_color_hex(kTextPrimary), 0);

    lv_obj_t *wifi_label = lv_label_create(layout.content);
    lv_label_set_text(wifi_label, "WiFi: Offline");
    lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 2, 24);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(kTextSecondary), 0);

    lv_obj_t *state_label = lv_label_create(layout.content);
    lv_label_set_text(state_label, "System: Ready");
    lv_obj_align(state_label, LV_ALIGN_TOP_LEFT, 2, 46);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0xAEEFCC), 0);

    lv_obj_t *apps = lv_obj_create(layout.content);
    lv_obj_set_size(apps, LV_PCT(100), LV_PCT(80));
    lv_obj_align(apps, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(apps, 8, 0);
    lv_obj_set_style_pad_row(apps, 8, 0);
    lv_obj_set_style_bg_color(apps, lv_color_hex(0x1C2E3C), 0);
    lv_obj_set_style_border_width(apps, 0, 0);
    lv_obj_set_style_radius(apps, 8, 0);
    lv_obj_set_flex_flow(apps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(apps, LV_SCROLLBAR_MODE_OFF);

    static const struct {
        const char *name;
        app_page_id_t page_id;
    } entries[] = {
        {"AI Voice", APP_PAGE_AI_VOICE},
        {"Settings", APP_PAGE_SETTINGS},
        {"Debug", APP_PAGE_DEBUG},
        {"About", APP_PAGE_ABOUT},
        {"Placeholder", APP_PAGE_PLACEHOLDER},
    };

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        s_card_ctx[i].nav_cb = nav_cb;
        s_card_ctx[i].target_page = entries[i].page_id;

        lv_obj_t *btn = lv_btn_create(apps);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 42);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x274155), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(kTextPrimary), 0);
        lv_obj_add_event_cb(btn, home_card_click_cb, LV_EVENT_CLICKED, &s_card_ctx[i]);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, entries[i].name);
        lv_obj_center(label);
    }
}
