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
        views->debug_audio_state_label = nullptr;
        views->debug_audio_event_label = nullptr;
        views->debug_audio_pcm_label = nullptr;
        views->debug_audio_peak_label = nullptr;
        views->debug_audio_rec_label = nullptr;
        views->debug_audio_play_label = nullptr;
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

    lv_obj_t *audio_state = lv_label_create(layout.content);
    lv_label_set_text(audio_state, "Audio State: --");
    lv_obj_align(audio_state, LV_ALIGN_TOP_LEFT, 2, 88);
    lv_obj_set_style_text_color(audio_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_audio_state_label = audio_state;
    }

    lv_obj_t *audio_evt = lv_label_create(layout.content);
    lv_label_set_text(audio_evt, "Last Audio Event: --");
    lv_obj_align(audio_evt, LV_ALIGN_TOP_LEFT, 2, 114);
    lv_obj_set_style_text_color(audio_evt, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_audio_event_label = audio_evt;
    }

    lv_obj_t *audio_pcm = lv_label_create(layout.content);
    lv_label_set_text(audio_pcm, "Last PCM Bytes: 0");
    lv_obj_align(audio_pcm, LV_ALIGN_TOP_LEFT, 2, 140);
    lv_obj_set_style_text_color(audio_pcm, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_audio_pcm_label = audio_pcm;
    }

    lv_obj_t *audio_peak = lv_label_create(layout.content);
    lv_label_set_text(audio_peak, "Peak Level: 0");
    lv_obj_align(audio_peak, LV_ALIGN_TOP_LEFT, 2, 166);
    lv_obj_set_style_text_color(audio_peak, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_audio_peak_label = audio_peak;
    }

    lv_obj_t *audio_rec = lv_label_create(layout.content);
    lv_label_set_text(audio_rec, "Recording: No");
    lv_obj_align(audio_rec, LV_ALIGN_TOP_LEFT, 2, 192);
    lv_obj_set_style_text_color(audio_rec, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_audio_rec_label = audio_rec;
    }

    lv_obj_t *audio_play = lv_label_create(layout.content);
    lv_label_set_text(audio_play, "Playing: No");
    lv_obj_align(audio_play, LV_ALIGN_TOP_LEFT, 2, 218);
    lv_obj_set_style_text_color(audio_play, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_audio_play_label = audio_play;
    }
}
