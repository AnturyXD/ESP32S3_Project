#include "ui_pages.h"

#include "esp_err.h"
#include "esp_log.h"
#include "service_ai.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_AI";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;
static constexpr uint32_t kTextSecondary = 0xC8D7EA;
static constexpr uint32_t kAsrBtnColor = 0x2E5B4B;

namespace {

typedef enum {
    AUDIO_BTN_START_ASR = 0,
    AUDIO_BTN_STOP_ASR,
} audio_btn_action_t;

typedef struct {
    audio_btn_action_t action;
} audio_btn_ctx_t;

static audio_btn_ctx_t s_btn_ctx[2];

static void audio_btn_click_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    audio_btn_ctx_t *ctx = static_cast<audio_btn_ctx_t *>(lv_event_get_user_data(event));
    if (ctx == nullptr) {
        ESP_LOGE(TAG, "audio button context missing");
        return;
    }

    esp_err_t err = ESP_OK;
    switch (ctx->action) {
    case AUDIO_BTN_START_ASR:
        err = service_ai_start_asr();
        ESP_LOGI(TAG, "start ASR requested");
        break;
    case AUDIO_BTN_STOP_ASR:
        err = service_ai_stop_asr();
        ESP_LOGI(TAG, "stop ASR requested");
        break;
    default:
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AI Voice action failed: %s", esp_err_to_name(err));
    }
}

static lv_obj_t *create_action_btn(lv_obj_t *parent, const char *text, lv_coord_t y, audio_btn_action_t action, uint32_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 144, 34);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(kTextPrimary), 0);
    lv_obj_set_style_radius(btn, 8, 0);

    s_btn_ctx[action].action = action;
    lv_obj_add_event_cb(btn, audio_btn_click_cb, LV_EVENT_CLICKED, &s_btn_ctx[action]);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_status_label(lv_obj_t *parent, const char *text, lv_coord_t y, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 148);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 2, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

} // namespace

void ui_page_ai_voice_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views)
{
    if (views != nullptr) {
        views->ai_audio_state_label = nullptr;
        views->ai_pcm_bytes_label = nullptr;
        views->ai_peak_label = nullptr;
        views->ai_asr_state_label = nullptr;
        views->ai_server_state_label = nullptr;
        views->ai_asr_recording_label = nullptr;
        views->ai_asr_sent_label = nullptr;
        views->ai_asr_partial_label = nullptr;
        views->ai_asr_final_label = nullptr;
        views->ai_asr_error_label = nullptr;
    }

    ui_page_layout_t layout = ui_page_create_layout(screen, "AI Voice", APP_PAGE_AI_VOICE, nav_cb, lv_color_hex(0x0F1923));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create AI Voice layout");
        return;
    }

    lv_obj_t *title = create_status_label(layout.content, "ASR Test Panel", 2, kTextPrimary);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);

    lv_obj_t *asr_state = create_status_label(layout.content, "ASR State: --", 24, kTextPrimary);
    lv_obj_t *server_state = create_status_label(layout.content, "Server: --", 46, kTextPrimary);
    lv_obj_t *asr_rec = create_status_label(layout.content, "Recording: No", 68, kTextPrimary);
    lv_obj_t *asr_sent = create_status_label(layout.content, "Sent: 0.0s", 90, kTextPrimary);
    lv_obj_t *partial = create_status_label(layout.content, "Partial: --", 112, kTextSecondary);
    lv_obj_t *final_text = create_status_label(layout.content, "Final: --", 158, kTextSecondary);
    lv_obj_t *asr_error = create_status_label(layout.content, "ASR Error: None", 204, kTextSecondary);

    if (views != nullptr) {
        views->ai_asr_state_label = asr_state;
        views->ai_server_state_label = server_state;
        views->ai_asr_recording_label = asr_rec;
        views->ai_asr_sent_label = asr_sent;
        views->ai_asr_partial_label = partial;
        views->ai_asr_final_label = final_text;
        views->ai_asr_error_label = asr_error;
    }

    create_action_btn(layout.content, "Start ASR", 250, AUDIO_BTN_START_ASR, kAsrBtnColor);
    create_action_btn(layout.content, "Stop ASR", 290, AUDIO_BTN_STOP_ASR, kAsrBtnColor);
}
