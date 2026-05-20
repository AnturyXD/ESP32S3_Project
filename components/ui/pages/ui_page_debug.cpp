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
        views->debug_storage_state_label = nullptr;
        views->debug_storage_mounted_label = nullptr;
        views->debug_storage_total_label = nullptr;
        views->debug_storage_free_label = nullptr;
        views->debug_storage_event_label = nullptr;
        views->debug_storage_error_label = nullptr;
        views->debug_cloud_state_label = nullptr;
        views->debug_cloud_registered_label = nullptr;
        views->debug_cloud_heartbeat_label = nullptr;
        views->debug_cloud_http_label = nullptr;
        views->debug_cloud_error_label = nullptr;
        views->debug_ai_state_label = nullptr;
        views->debug_llm_state_label = nullptr;
        views->debug_chat_http_label = nullptr;
        views->debug_chat_error_label = nullptr;
        views->debug_reply_received_label = nullptr;
        views->debug_tts_state_label = nullptr;
        views->debug_tts_http_label = nullptr;
        views->debug_tts_format_label = nullptr;
        views->debug_tts_bytes_label = nullptr;
        views->debug_tts_error_label = nullptr;
        views->debug_ai_sent_label = nullptr;
        views->debug_ai_error_label = nullptr;
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

    lv_obj_t *storage_state = lv_label_create(layout.content);
    lv_label_set_text(storage_state, "Storage State: --");
    lv_obj_align(storage_state, LV_ALIGN_TOP_LEFT, 2, 248);
    lv_obj_set_style_text_color(storage_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_storage_state_label = storage_state;
    }

    lv_obj_t *storage_mounted = lv_label_create(layout.content);
    lv_label_set_text(storage_mounted, "SD Mounted: No");
    lv_obj_align(storage_mounted, LV_ALIGN_TOP_LEFT, 2, 274);
    lv_obj_set_style_text_color(storage_mounted, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_storage_mounted_label = storage_mounted;
    }

    lv_obj_t *storage_total = lv_label_create(layout.content);
    lv_label_set_text(storage_total, "Total: 0 MB");
    lv_obj_align(storage_total, LV_ALIGN_TOP_LEFT, 2, 300);
    lv_obj_set_style_text_color(storage_total, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_storage_total_label = storage_total;
    }

    lv_obj_t *storage_free = lv_label_create(layout.content);
    lv_label_set_text(storage_free, "Free: 0 MB");
    lv_obj_align(storage_free, LV_ALIGN_TOP_LEFT, 2, 326);
    lv_obj_set_style_text_color(storage_free, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_storage_free_label = storage_free;
    }

    lv_obj_t *storage_event = lv_label_create(layout.content);
    lv_label_set_text(storage_event, "Storage Event: --");
    lv_obj_align(storage_event, LV_ALIGN_TOP_LEFT, 2, 352);
    lv_obj_set_style_text_color(storage_event, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_storage_event_label = storage_event;
    }

    lv_obj_t *storage_error = lv_label_create(layout.content);
    lv_label_set_text(storage_error, "Storage Error: None");
    lv_obj_align(storage_error, LV_ALIGN_TOP_LEFT, 2, 378);
    lv_obj_set_style_text_color(storage_error, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_storage_error_label = storage_error;
    }

    lv_obj_t *cloud_state = lv_label_create(layout.content);
    lv_label_set_text(cloud_state, "Cloud State: --");
    lv_obj_align(cloud_state, LV_ALIGN_TOP_LEFT, 2, 408);
    lv_obj_set_style_text_color(cloud_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_cloud_state_label = cloud_state;
    }

    lv_obj_t *cloud_registered = lv_label_create(layout.content);
    lv_label_set_text(cloud_registered, "Registered: No");
    lv_obj_align(cloud_registered, LV_ALIGN_TOP_LEFT, 2, 434);
    lv_obj_set_style_text_color(cloud_registered, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_cloud_registered_label = cloud_registered;
    }

    lv_obj_t *cloud_heartbeat = lv_label_create(layout.content);
    lv_label_set_text(cloud_heartbeat, "Last Heartbeat: --");
    lv_obj_align(cloud_heartbeat, LV_ALIGN_TOP_LEFT, 2, 460);
    lv_obj_set_style_text_color(cloud_heartbeat, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_cloud_heartbeat_label = cloud_heartbeat;
    }

    lv_obj_t *cloud_http = lv_label_create(layout.content);
    lv_label_set_text(cloud_http, "Last HTTP: 0");
    lv_obj_align(cloud_http, LV_ALIGN_TOP_LEFT, 2, 486);
    lv_obj_set_style_text_color(cloud_http, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_cloud_http_label = cloud_http;
    }

    lv_obj_t *cloud_error = lv_label_create(layout.content);
    lv_label_set_text(cloud_error, "Cloud Error: None");
    lv_obj_align(cloud_error, LV_ALIGN_TOP_LEFT, 2, 512);
    lv_obj_set_style_text_color(cloud_error, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_cloud_error_label = cloud_error;
    }

    lv_obj_t *ai_state = lv_label_create(layout.content);
    lv_label_set_text(ai_state, "AI State: --");
    lv_obj_align(ai_state, LV_ALIGN_TOP_LEFT, 2, 538);
    lv_obj_set_style_text_color(ai_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_ai_state_label = ai_state;
    }

    lv_obj_t *ai_sent = lv_label_create(layout.content);
    lv_label_set_text(ai_sent, "ASR Sent: 0.0s");
    lv_obj_align(ai_sent, LV_ALIGN_TOP_LEFT, 2, 564);
    lv_obj_set_style_text_color(ai_sent, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_ai_sent_label = ai_sent;
    }

    lv_obj_t *ai_error = lv_label_create(layout.content);
    lv_label_set_text(ai_error, "ASR Error: None");
    lv_obj_align(ai_error, LV_ALIGN_TOP_LEFT, 2, 590);
    lv_obj_set_style_text_color(ai_error, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_ai_error_label = ai_error;
    }

    lv_obj_t *llm_state = lv_label_create(layout.content);
    lv_label_set_text(llm_state, "LLM State: --");
    lv_obj_align(llm_state, LV_ALIGN_TOP_LEFT, 2, 616);
    lv_obj_set_style_text_color(llm_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_llm_state_label = llm_state;
    }

    lv_obj_t *chat_http = lv_label_create(layout.content);
    lv_label_set_text(chat_http, "Chat HTTP: 0");
    lv_obj_align(chat_http, LV_ALIGN_TOP_LEFT, 2, 642);
    lv_obj_set_style_text_color(chat_http, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_chat_http_label = chat_http;
    }

    lv_obj_t *chat_error = lv_label_create(layout.content);
    lv_label_set_text(chat_error, "Chat Error: None");
    lv_obj_align(chat_error, LV_ALIGN_TOP_LEFT, 2, 668);
    lv_obj_set_style_text_color(chat_error, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_chat_error_label = chat_error;
    }

    lv_obj_t *reply_received = lv_label_create(layout.content);
    lv_label_set_text(reply_received, "Reply Received: No");
    lv_obj_align(reply_received, LV_ALIGN_TOP_LEFT, 2, 694);
    lv_obj_set_style_text_color(reply_received, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_reply_received_label = reply_received;
    }

    lv_obj_t *tts_state = lv_label_create(layout.content);
    lv_label_set_text(tts_state, "TTS State: --");
    lv_obj_align(tts_state, LV_ALIGN_TOP_LEFT, 2, 720);
    lv_obj_set_style_text_color(tts_state, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_tts_state_label = tts_state;
    }

    lv_obj_t *tts_http = lv_label_create(layout.content);
    lv_label_set_text(tts_http, "TTS HTTP: 0");
    lv_obj_align(tts_http, LV_ALIGN_TOP_LEFT, 2, 746);
    lv_obj_set_style_text_color(tts_http, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_tts_http_label = tts_http;
    }

    lv_obj_t *tts_format = lv_label_create(layout.content);
    lv_label_set_text(tts_format, "TTS Format: --");
    lv_obj_align(tts_format, LV_ALIGN_TOP_LEFT, 2, 772);
    lv_obj_set_style_text_color(tts_format, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_tts_format_label = tts_format;
    }

    lv_obj_t *tts_bytes = lv_label_create(layout.content);
    lv_label_set_text(tts_bytes, "TTS Bytes: 0");
    lv_obj_align(tts_bytes, LV_ALIGN_TOP_LEFT, 2, 798);
    lv_obj_set_style_text_color(tts_bytes, lv_color_hex(kTextPrimary), 0);
    if (views != nullptr) {
        views->debug_tts_bytes_label = tts_bytes;
    }

    lv_obj_t *tts_error = lv_label_create(layout.content);
    lv_label_set_text(tts_error, "TTS Error: None");
    lv_obj_align(tts_error, LV_ALIGN_TOP_LEFT, 2, 824);
    lv_obj_set_style_text_color(tts_error, lv_color_hex(kTextSecondary), 0);
    if (views != nullptr) {
        views->debug_tts_error_label = tts_error;
    }
}
