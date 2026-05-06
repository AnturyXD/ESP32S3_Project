#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_STATE_UNINIT = 0,
    AUDIO_STATE_INIT,
    AUDIO_STATE_READY,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_STOPPING,
    AUDIO_STATE_ERROR,
} audio_state_t;

esp_err_t service_audio_init(void);
esp_err_t service_audio_start_record_test(void);
esp_err_t service_audio_stop_record_test(void);
esp_err_t service_audio_play_test_tone(void);
esp_err_t service_audio_stop_playback(void);
audio_state_t service_audio_get_state(void);
const char *service_audio_get_state_string(void);
const char *service_audio_get_last_event(void);
uint32_t service_audio_get_last_pcm_bytes(void);
uint16_t service_audio_get_peak_level(void);
bool service_audio_is_recording(void);
bool service_audio_is_playing(void);
uint32_t service_audio_get_revision(void);

#ifdef __cplusplus
}
#endif
