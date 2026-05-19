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

/**
 * @brief PCM 采集回调。
 *
 * service_audio 内部负责把板载 Codec 的底层 stereo slot 数据转换为
 * 16kHz / 16bit / mono PCM。上层 ASR 只接收已经规整好的 mono PCM，
 * 不需要知道 ES7210/ES8311 或 I2S 槽位细节。
 *
 * 注意：回调运行在音频采集任务中，不能操作 LVGL，也不能做长时间阻塞。
 */
typedef esp_err_t (*service_audio_pcm_callback_t)(const int16_t *pcm,
                                                  size_t bytes,
                                                  void *user_ctx);

esp_err_t service_audio_init(void);
esp_err_t service_audio_start_record_test(void);
esp_err_t service_audio_stop_record_test(void);
esp_err_t service_audio_start_pcm_capture(service_audio_pcm_callback_t callback, void *user_ctx);
esp_err_t service_audio_stop_pcm_capture(void);
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
