#include "ui_pages.h"

#include "esp_err.h"
#include "esp_log.h"
#include "service_audio.h"
#include "ui_page_layout.h"

static const char *TAG = "UI_PAGE_AI";
static constexpr uint32_t kTextPrimary = 0xEAF2FF;
static constexpr uint32_t kBtnColor = 0x2A4A64;

namespace {

typedef enum {
    AUDIO_BTN_RECORD = 0,
    AUDIO_BTN_STOP_RECORD,
    AUDIO_BTN_PLAY_TONE,
    AUDIO_BTN_STOP_PLAY,
} audio_btn_action_t;

typedef struct {
    audio_btn_action_t action;
} audio_btn_ctx_t;

static audio_btn_ctx_t s_btn_ctx[4];

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
    case AUDIO_BTN_RECORD:
        err = service_audio_start_record_test();
        ESP_LOGI(TAG, "record test requested");
        break;
    case AUDIO_BTN_STOP_RECORD:
        err = service_audio_stop_record_test();
        ESP_LOGI(TAG, "stop record requested");
        break;
    case AUDIO_BTN_PLAY_TONE:
        err = service_audio_play_test_tone();
        ESP_LOGI(TAG, "play test tone requested");
        break;
    case AUDIO_BTN_STOP_PLAY:
        err = service_audio_stop_playback();
        ESP_LOGI(TAG, "stop playback requested");
        break;
    default:
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio action failed: %s", esp_err_to_name(err));
    }
}

static lv_obj_t *create_action_btn(lv_obj_t *parent, const char *text, lv_coord_t y, audio_btn_action_t action)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 144, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kBtnColor), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(kTextPrimary), 0);
    lv_obj_set_style_radius(btn, 8, 0);

    s_btn_ctx[action].action = action;
    lv_obj_add_event_cb(btn, audio_btn_click_cb, LV_EVENT_CLICKED, &s_btn_ctx[action]);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

} // namespace

void ui_page_ai_voice_create(lv_obj_t *screen, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views)
{
    if (views != nullptr) {
        views->ai_audio_state_label = nullptr;
        views->ai_pcm_bytes_label = nullptr;
        views->ai_peak_label = nullptr;
    }

    ui_page_layout_t layout = ui_page_create_layout(screen, "AI Voice", APP_PAGE_AI_VOICE, nav_cb, lv_color_hex(0x0F1923));
    if (layout.content == nullptr) {
        ESP_LOGE(TAG, "failed to create AI Voice layout");
        return;
    }

    lv_obj_t *title = lv_label_create(layout.content);
    lv_label_set_text(title, "Audio Test Panel");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_text_color(title, lv_color_hex(kTextPrimary), 0);

    lv_obj_t *audio_state = lv_label_create(layout.content);
    lv_label_set_text(audio_state, "Audio State: --");
    lv_obj_align(audio_state, LV_ALIGN_TOP_LEFT, 2, 24);
    lv_obj_set_style_text_color(audio_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->ai_audio_state_label = audio_state;
    }

    lv_obj_t *pcm_bytes = lv_label_create(layout.content);
    lv_label_set_text(pcm_bytes, "Last PCM Bytes: 0");
    lv_obj_align(pcm_bytes, LV_ALIGN_TOP_LEFT, 2, 46);
    lv_obj_set_style_text_color(pcm_bytes, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->ai_pcm_bytes_label = pcm_bytes;
    }

    lv_obj_t *peak = lv_label_create(layout.content);
    lv_label_set_text(peak, "Peak Level: 0");
    lv_obj_align(peak, LV_ALIGN_TOP_LEFT, 2, 68);
    lv_obj_set_style_text_color(peak, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->ai_peak_label = peak;
    }

    create_action_btn(layout.content, "Record Test", 100, AUDIO_BTN_RECORD);
    create_action_btn(layout.content, "Stop Record", 142, AUDIO_BTN_STOP_RECORD);
    create_action_btn(layout.content, "Play Test Tone", 184, AUDIO_BTN_PLAY_TONE);
    create_action_btn(layout.content, "Stop Playback", 226, AUDIO_BTN_STOP_PLAY);
}
