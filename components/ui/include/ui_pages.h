#pragma once

#include "app_shell.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_page_nav_cb_t)(app_page_id_t page_id);

typedef struct {
    lv_obj_t *home_time_label;
    lv_obj_t *home_wifi_label;

    lv_obj_t *ai_audio_state_label;
    lv_obj_t *ai_pcm_bytes_label;
    lv_obj_t *ai_peak_label;

    lv_obj_t *settings_ssid_label;
    lv_obj_t *settings_wifi_label;
    lv_obj_t *settings_ip_label;
    lv_obj_t *settings_time_state_label;

    lv_obj_t *debug_network_label;
    lv_obj_t *debug_ip_label;
    lv_obj_t *debug_last_event_label;
    lv_obj_t *debug_audio_state_label;
    lv_obj_t *debug_audio_event_label;
    lv_obj_t *debug_audio_pcm_label;
    lv_obj_t *debug_audio_peak_label;
    lv_obj_t *debug_audio_rec_label;
    lv_obj_t *debug_audio_play_label;
} ui_page_status_views_t;

void ui_page_home_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);
void ui_page_ai_voice_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);
void ui_page_settings_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);
void ui_page_debug_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);
void ui_page_about_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);
void ui_page_placeholder_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb, ui_page_status_views_t *views);

#ifdef __cplusplus
}
#endif
