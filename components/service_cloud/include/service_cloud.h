#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 云端连接服务状态。
 *
 * service_cloud 只负责设备与自建服务器的注册、心跳和后续配置拉取入口。
 * 它不操作 LVGL，不保存第三方 ASR/LLM/TTS 密钥，也不直接访问 UI 控件。
 */
typedef enum {
    CLOUD_STATE_UNINIT = 0,
    CLOUD_STATE_IDLE,
    CLOUD_STATE_REGISTERING,
    CLOUD_STATE_REGISTERED,
    CLOUD_STATE_HEARTBEAT_SENDING,
    CLOUD_STATE_ONLINE,
    CLOUD_STATE_ERROR,
    CLOUD_STATE_CONFIG_MISSING,
} cloud_state_t;

typedef struct {
    uint8_t *data;
    size_t bytes;
    char audio_format[16];
    int sample_rate;
    int bits;
    int channels;
} cloud_tts_audio_t;

esp_err_t service_cloud_init(void);
esp_err_t service_cloud_register_device(void);
esp_err_t service_cloud_send_heartbeat(void);
/**
 * @brief 向自建服务器发送单轮 Chat 请求。
 *
 * service_cloud 只负责 HTTP 通信和响应读取，不理解 AI 状态机，也不操作 UI。
 * reply_text 由调用方提供缓冲区接收；真实火山方舟 Key 只保存在服务器端。
 */
esp_err_t service_cloud_chat_request(const char *text, char *reply_text, size_t reply_text_size);
/**
 * @brief 请求服务器 TTS 合成并下载音频。
 *
 * 返回的 audio->data 由 service_cloud 在 PSRAM/heap 中分配，调用方播放完成后
 * 必须调用 service_cloud_free_tts_audio() 释放。ESP32 只访问自建服务器，不保存
 * 火山 TTS Key。
 */
esp_err_t service_cloud_tts_request(const char *text, cloud_tts_audio_t *audio);
void service_cloud_free_tts_audio(cloud_tts_audio_t *audio);
cloud_state_t service_cloud_get_state(void);
const char *service_cloud_get_state_string(void);
const char *service_cloud_get_last_event(void);
const char *service_cloud_get_last_error(void);
int service_cloud_get_last_http_status(void);
int service_cloud_get_last_chat_http_status(void);
const char *service_cloud_get_last_chat_error(void);
int service_cloud_get_last_tts_http_status(void);
const char *service_cloud_get_last_tts_error(void);
const char *service_cloud_get_last_tts_audio_format(void);
size_t service_cloud_get_last_tts_audio_bytes(void);
bool service_cloud_is_registered(void);
const char *service_cloud_get_last_heartbeat(void);
uint32_t service_cloud_get_revision(void);

#ifdef __cplusplus
}
#endif
