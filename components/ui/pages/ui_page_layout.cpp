#include "ui_page_layout.h"

#include "esp_log.h"

static const char *TAG = "UI_PAGE";

namespace {

constexpr int kHeaderMargin = 4;
constexpr int kHeaderHeight = 52;
constexpr int kBackBtnWidth = 48;
constexpr int kBackBtnHeight = 36;
constexpr int kContentTop = 58;
constexpr int kBottomMargin = 4;
constexpr uint32_t kTextPrimary = 0xEAF2FF;
constexpr uint32_t kTextSecondary = 0xC8D7EA;

typedef struct {
    ui_page_nav_cb_t nav_cb;
    app_page_id_t page_id;
} page_header_ctx_t;

static page_header_ctx_t s_header_ctx[APP_PAGE_MAX];

static void page_back_btn_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    page_header_ctx_t *ctx = static_cast<page_header_ctx_t *>(lv_event_get_user_data(event));
    if (ctx == nullptr || ctx->nav_cb == nullptr) {
        ESP_LOGE(TAG, "back button callback context invalid");
        return;
    }

    ESP_LOGI(TAG, "back button clicked: from_page=%d to_page=%d", static_cast<int>(ctx->page_id), static_cast<int>(APP_PAGE_HOME));
    ctx->nav_cb(APP_PAGE_HOME);
}

} // namespace

ui_page_layout_t ui_page_create_layout(lv_obj_t *screen,
                                       const char *title,
                                       app_page_id_t page_id,
                                       ui_page_nav_cb_t nav_cb,
                                       lv_color_t bg_color)
{
    ui_page_layout_t layout = {};
    if (screen == nullptr || title == nullptr || nav_cb == nullptr) {
        ESP_LOGE(TAG, "invalid layout args");
        return layout;
    }
    if (page_id < APP_PAGE_HOME || page_id >= APP_PAGE_MAX) {
        ESP_LOGE(TAG, "invalid page id for layout: %d", static_cast<int>(page_id));
        return layout;
    }

    int screen_w = lv_disp_get_hor_res(nullptr);
    int screen_h = lv_disp_get_ver_res(nullptr);
    if (screen_w <= 0 || screen_h <= 0) {
        ESP_LOGE(TAG, "invalid screen size w=%d h=%d", screen_w, screen_h);
        return layout;
    }

    lv_obj_set_style_bg_color(screen, bg_color, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kTextPrimary), 0);

    layout.header = lv_obj_create(screen);
    lv_obj_set_size(layout.header, screen_w - (kHeaderMargin * 2), kHeaderHeight);
    lv_obj_align(layout.header, LV_ALIGN_TOP_MID, 0, kHeaderMargin);
    lv_obj_set_style_radius(layout.header, 10, 0);
    lv_obj_set_style_pad_all(layout.header, 0, 0);
    lv_obj_set_style_border_width(layout.header, 0, 0);
    lv_obj_set_style_bg_color(layout.header, lv_color_hex(0x1D2A35), 0);

    page_header_ctx_t *ctx = &s_header_ctx[page_id];
    ctx->nav_cb = nav_cb;
    ctx->page_id = page_id;

    lv_obj_t *back_btn = lv_btn_create(layout.header);
    lv_obj_set_size(back_btn, kBackBtnWidth, kBackBtnHeight);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2A3E52), 0);
    lv_obj_set_style_text_color(back_btn, lv_color_hex(kTextPrimary), 0);
    lv_obj_add_event_cb(back_btn, page_back_btn_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_center(back_label);

    lv_obj_t *title_label = lv_label_create(layout.header);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 8, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(kTextPrimary), 0);

    int content_h = screen_h - kContentTop - kBottomMargin;
    if (content_h < 20) {
        content_h = 20;
    }

    layout.content = lv_obj_create(screen);
    lv_obj_set_size(layout.content, screen_w - (kHeaderMargin * 2), content_h);
    lv_obj_align(layout.content, LV_ALIGN_TOP_MID, 0, kContentTop);
    lv_obj_set_style_radius(layout.content, 10, 0);
    lv_obj_set_style_pad_all(layout.content, 8, 0);
    lv_obj_set_style_border_width(layout.content, 0, 0);
    lv_obj_set_style_bg_color(layout.content, lv_color_hex(0x16212B), 0);
    lv_obj_set_style_text_color(layout.content, lv_color_hex(kTextSecondary), 0);
    lv_obj_set_scrollbar_mode(layout.content, LV_SCROLLBAR_MODE_OFF);

    return layout;
}
