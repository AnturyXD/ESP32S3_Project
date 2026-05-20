#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief V0.6 ASR 状态机。
 *
 * service_ai 协调“录音 -> 自建服务器 ASR -> ASR final -> 服务器 Chat”链路。
 * 它不操作 LVGL，不保存火山密钥，也不接 TTS。UI 页面只能通过本头文件
 * 的只读接口读取状态，并通过 start/stop 触发用户主动的 ASR 测试。
 */
typedef enum {
    AI_ASR_STATE_IDLE = 0,
    AI_ASR_STATE_CONNECTING,
    AI_ASR_STATE_RECORDING,
    AI_ASR_STATE_WAITING_FINAL,
    AI_ASR_STATE_DONE,
    AI_ASR_STATE_ERROR,
} ai_asr_state_t;

/**
 * @brief V0.7 LLM 文本对话状态。
 *
 * service_ai 负责在 ASR final 后协调 service_cloud 发送 Chat 请求。
 * ESP32 不保存火山方舟 API Key，UI 只能读取状态和回复接收结果。
 */
typedef enum {
    AI_LLM_STATE_IDLE = 0,
    AI_LLM_STATE_REQUESTING,
    AI_LLM_STATE_DONE,
    AI_LLM_STATE_ERROR,
    AI_LLM_STATE_CONFIG_MISSING,
} ai_llm_state_t;

/**
 * @brief V0.8 TTS 中文语音播放状态。
 *
 * ESP32 只请求自建服务器 TTS 接口并播放服务器返回的 PCM/WAV PCM；
 * 火山 TTS Key 只保存在服务器本地 .env。
 */
typedef enum {
    AI_TTS_STATE_IDLE = 0,
    AI_TTS_STATE_REQUESTING,
    AI_TTS_STATE_DOWNLOADING,
    AI_TTS_STATE_PLAYING,
    AI_TTS_STATE_DONE,
    AI_TTS_STATE_ERROR,
    AI_TTS_STATE_CONFIG_MISSING,
    AI_TTS_STATE_UNSUPPORTED_FORMAT,
} ai_tts_state_t;

esp_err_t service_ai_init(void);
esp_err_t service_ai_start_asr(void);
esp_err_t service_ai_stop_asr(void);
ai_asr_state_t service_ai_get_asr_state(void);
const char *service_ai_get_asr_state_string(void);
const char *service_ai_get_partial_text(void);
const char *service_ai_get_final_text(void);
const char *service_ai_get_last_error(void);
ai_llm_state_t service_ai_get_llm_state(void);
const char *service_ai_get_llm_state_string(void);
const char *service_ai_get_reply_text(void);
const char *service_ai_get_reply_status(void);
const char *service_ai_get_llm_error(void);
ai_tts_state_t service_ai_get_tts_state(void);
const char *service_ai_get_tts_state_string(void);
const char *service_ai_get_tts_status(void);
const char *service_ai_get_tts_error(void);
bool service_ai_is_tts_speaking(void);
size_t service_ai_get_tts_audio_bytes(void);
float service_ai_get_asr_sent_seconds(void);
bool service_ai_is_asr_recording(void);
uint32_t service_ai_get_revision(void);

#ifdef __cplusplus
}
#endif
