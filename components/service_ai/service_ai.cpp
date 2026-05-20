#include "service_ai.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "service_audio.h"
#include "service_cloud.h"
#include "service_network.h"

static const char *TAG = "AI";

namespace {

constexpr uint32_t kTaskStackBytes = 16384;
constexpr UBaseType_t kTaskPriority = 5;
constexpr int kWebsocketTimeoutMs = 8000;
constexpr uint32_t kAsrPacketMs = AI_ASR_PACKET_MS;
constexpr uint32_t kAsrMaxSeconds = AI_ASR_MAX_SECONDS;
constexpr uint32_t kFinalWaitMs = 3500;
constexpr size_t kAsrPacketBytes = (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS / 8) * kAsrPacketMs) / 1000;
constexpr EventBits_t kWsConnectedBit = BIT0;
constexpr EventBits_t kWsDisconnectedBit = BIT1;
constexpr EventBits_t kWsErrorBit = BIT2;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_asr_task = nullptr;
static EventGroupHandle_t s_ws_events = nullptr;
static esp_websocket_client_handle_t s_ws = nullptr;
static ai_asr_state_t s_state = AI_ASR_STATE_IDLE;
static ai_llm_state_t s_llm_state = AI_LLM_STATE_IDLE;
static ai_tts_state_t s_tts_state = AI_TTS_STATE_IDLE;
static bool s_stop_requested = false;
static bool s_recording = false;
static bool s_tts_speaking = false;
static uint32_t s_revision = 0;
static uint32_t s_sent_bytes = 0;
static size_t s_tts_audio_bytes = 0;
static char s_partial_text[256] = "";
static char s_final_text[512] = "";
static char s_reply_text[512] = "";
static char s_llm_error[160] = "None";
static char s_tts_error[160] = "None";
static char s_last_error[160] = "None";
static uint8_t s_packet_buffer[kAsrPacketBytes] = {0};
static size_t s_packet_len = 0;

static const char *asr_state_to_string(ai_asr_state_t state)
{
    switch (state) {
    case AI_ASR_STATE_IDLE:
        return "Idle";
    case AI_ASR_STATE_CONNECTING:
        return "Connecting";
    case AI_ASR_STATE_RECORDING:
        return "Recording";
    case AI_ASR_STATE_WAITING_FINAL:
        return "Waiting Final";
    case AI_ASR_STATE_DONE:
        return "Done";
    case AI_ASR_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static const char *llm_state_to_string(ai_llm_state_t state)
{
    switch (state) {
    case AI_LLM_STATE_IDLE:
        return "Idle";
    case AI_LLM_STATE_REQUESTING:
        return "Requesting";
    case AI_LLM_STATE_DONE:
        return "Done";
    case AI_LLM_STATE_ERROR:
        return "Error";
    case AI_LLM_STATE_CONFIG_MISSING:
        return "Config Missing";
    default:
        return "Unknown";
    }
}

static const char *tts_state_to_string(ai_tts_state_t state)
{
    switch (state) {
    case AI_TTS_STATE_IDLE:
        return "Idle";
    case AI_TTS_STATE_REQUESTING:
        return "Requesting";
    case AI_TTS_STATE_DOWNLOADING:
        return "Downloading";
    case AI_TTS_STATE_PLAYING:
        return "Playing";
    case AI_TTS_STATE_DONE:
        return "Done";
    case AI_TTS_STATE_ERROR:
        return "Error";
    case AI_TTS_STATE_CONFIG_MISSING:
        return "Config Missing";
    case AI_TTS_STATE_UNSUPPORTED_FORMAT:
        return "Unsupported Format";
    default:
        return "Unknown";
    }
}

static void ai_touch_revision_locked(void)
{
    ++s_revision;
}

static void ai_set_state(ai_asr_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = (s_state != state);
    s_state = state;
    if (changed) {
        ai_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "ASR state -> %s", asr_state_to_string(state));
    }
}

static void ai_set_error(const char *message)
{
    portENTER_CRITICAL(&s_lock);
    s_state = AI_ASR_STATE_ERROR;
    snprintf(s_last_error, sizeof(s_last_error), "%s", message ? message : "Unknown");
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "ASR error: %s", message ? message : "Unknown");
}

static void ai_set_llm_state(ai_llm_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = (s_llm_state != state);
    s_llm_state = state;
    if (changed) {
        ai_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "LLM state -> %s", llm_state_to_string(state));
    }
}

static void ai_set_llm_error(const char *message, ai_llm_state_t state = AI_LLM_STATE_ERROR)
{
    portENTER_CRITICAL(&s_lock);
    s_llm_state = state;
    snprintf(s_llm_error, sizeof(s_llm_error), "%s", message ? message : "Unknown");
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "LLM error: %s", message ? message : "Unknown");
}

static void ai_set_tts_state(ai_tts_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = (s_tts_state != state);
    s_tts_state = state;
    if (changed) {
        ai_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "TTS state -> %s", tts_state_to_string(state));
    }
}

static void ai_set_tts_error(const char *message, ai_tts_state_t state = AI_TTS_STATE_ERROR)
{
    portENTER_CRITICAL(&s_lock);
    s_tts_state = state;
    snprintf(s_tts_error, sizeof(s_tts_error), "%s", message ? message : "Unknown");
    s_tts_speaking = false;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "TTS error: %s", message ? message : "Unknown");
}

static bool ai_stop_requested(void)
{
    portENTER_CRITICAL(&s_lock);
    bool requested = s_stop_requested;
    portEXIT_CRITICAL(&s_lock);
    return requested;
}

static void ai_set_recording(bool recording)
{
    portENTER_CRITICAL(&s_lock);
    s_recording = recording;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
}

static void ai_reset_result_locked(void)
{
    s_partial_text[0] = '\0';
    s_final_text[0] = '\0';
    s_reply_text[0] = '\0';
    s_llm_state = AI_LLM_STATE_IDLE;
    s_tts_state = AI_TTS_STATE_IDLE;
    s_tts_speaking = false;
    s_tts_audio_bytes = 0;
    snprintf(s_llm_error, sizeof(s_llm_error), "None");
    snprintf(s_tts_error, sizeof(s_tts_error), "None");
    snprintf(s_last_error, sizeof(s_last_error), "None");
    s_sent_bytes = 0;
    s_packet_len = 0;
}

static void ai_json_escape(const char *input, char *output, size_t output_size)
{
    if (output_size == 0) {
        return;
    }
    size_t out = 0;
    const char *src = input ? input : "";
    while (*src != '\0' && out + 1 < output_size) {
        if ((*src == '"' || *src == '\\') && out + 2 < output_size) {
            output[out++] = '\\';
            output[out++] = *src++;
        } else {
            output[out++] = *src++;
        }
    }
    output[out] = '\0';
}

static bool ai_extract_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0) {
        return false;
    }
    char pattern[48] = {0};
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == nullptr) {
        return false;
    }
    pos = strchr(pos + strlen(pattern), ':');
    if (pos == nullptr) {
        return false;
    }
    pos = strchr(pos, '"');
    if (pos == nullptr) {
        return false;
    }
    ++pos;

    size_t out_index = 0;
    while (*pos != '\0' && *pos != '"' && out_index + 1 < out_size) {
        if (*pos == '\\' && pos[1] != '\0') {
            ++pos;
        }
        out[out_index++] = *pos++;
    }
    out[out_index] = '\0';
    return true;
}

static void ai_handle_server_text(const char *text, size_t len)
{
    if (text == nullptr || len == 0) {
        return;
    }

    char msg[384] = {0};
    const size_t copy_len = (len < sizeof(msg) - 1) ? len : sizeof(msg) - 1;
    memcpy(msg, text, copy_len);
    msg[copy_len] = '\0';

    char type[24] = {0};
    char value[256] = {0};
    (void)ai_extract_json_string(msg, "type", type, sizeof(type));

    if (strcmp(type, "partial") == 0) {
        if (ai_extract_json_string(msg, "text", value, sizeof(value))) {
            ESP_LOGI(TAG, "ASR partial text: %s", value);
            portENTER_CRITICAL(&s_lock);
            snprintf(s_partial_text, sizeof(s_partial_text), "%s", value);
            ai_touch_revision_locked();
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (strcmp(type, "final") == 0) {
        if (ai_extract_json_string(msg, "text", value, sizeof(value)) && value[0] != '\0') {
            ESP_LOGI(TAG, "ASR final text: %s", value);
            portENTER_CRITICAL(&s_lock);
            snprintf(s_final_text, sizeof(s_final_text), "%s", value);
            ai_touch_revision_locked();
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (strcmp(type, "error") == 0) {
        if (ai_extract_json_string(msg, "message", value, sizeof(value))) {
            ai_set_error(value);
        }
    } else if (strcmp(type, "started") == 0) {
        ESP_LOGI(TAG, "ASR server started session");
    }
}

static bool ai_is_stop_close_expected(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool expected = s_stop_requested || s_state == AI_ASR_STATE_WAITING_FINAL || s_state == AI_ASR_STATE_DONE;
    portEXIT_CRITICAL(&s_lock);
    return expected;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = static_cast<esp_websocket_event_data_t *>(event_data);
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(s_ws_events, kWsConnectedBit);
        ESP_LOGI(TAG, "ASR websocket connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        xEventGroupSetBits(s_ws_events, kWsDisconnectedBit);
        ESP_LOGI(TAG, "ASR websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data != nullptr && data->op_code == 0x1 && data->data_ptr != nullptr && data->data_len > 0) {
            ai_handle_server_text(data->data_ptr, static_cast<size_t>(data->data_len));
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        xEventGroupSetBits(s_ws_events, kWsErrorBit);
        /*
         * Stop ASR 后，服务端可能已经返回 final 并主动 FIN 关闭连接。
         * esp_websocket_client 会把这种 FIN 上报为 ERROR 事件，但它不是识别失败。
         * 只有录音阶段的异常断开才进入 ERROR，避免 UI 在正常停止后显示 WebSocket 错误。
         */
        if (ai_is_stop_close_expected()) {
            ESP_LOGI(TAG, "ASR websocket closed after stop, treat as normal finalizing");
        } else {
            ai_set_error("websocket error");
        }
        break;
    default:
        break;
    }
}

static bool ai_build_ws_url(char *url, size_t url_size)
{
    if (url == nullptr || url_size == 0 || strlen(CLOUD_SERVER_BASE_URL) == 0) {
        return false;
    }

    const char *base = CLOUD_SERVER_BASE_URL;
    if (strncmp(base, "http://", 7) == 0) {
        snprintf(url, url_size, "ws://%s%s", base + 7, CLOUD_ASR_WS_PATH);
    } else if (strncmp(base, "https://", 8) == 0) {
        snprintf(url, url_size, "wss://%s%s", base + 8, CLOUD_ASR_WS_PATH);
    } else if (strncmp(base, "ws://", 5) == 0 || strncmp(base, "wss://", 6) == 0) {
        snprintf(url, url_size, "%s%s", base, CLOUD_ASR_WS_PATH);
    } else {
        return false;
    }
    return true;
}

static esp_err_t ai_send_packet_locked(void)
{
    if (s_ws == nullptr || s_packet_len == 0) {
        return ESP_OK;
    }
    int sent = esp_websocket_client_send_bin(s_ws,
                                             reinterpret_cast<const char *>(s_packet_buffer),
                                             static_cast<int>(s_packet_len),
                                             pdMS_TO_TICKS(kWebsocketTimeoutMs));
    if (sent < 0) {
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_lock);
    s_sent_bytes += s_packet_len;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    s_packet_len = 0;
    return ESP_OK;
}

static esp_err_t ai_audio_pcm_callback(const int16_t *pcm, size_t bytes, void *user_ctx)
{
    (void)user_ctx;
    if (pcm == nullptr || bytes == 0) {
        return ESP_OK;
    }
    if (ai_stop_requested() || s_ws == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *src = reinterpret_cast<const uint8_t *>(pcm);
    size_t remaining = bytes;
    while (remaining > 0) {
        const size_t room = sizeof(s_packet_buffer) - s_packet_len;
        const size_t copy_len = (remaining < room) ? remaining : room;
        memcpy(s_packet_buffer + s_packet_len, src, copy_len);
        s_packet_len += copy_len;
        src += copy_len;
        remaining -= copy_len;

        if (s_packet_len >= sizeof(s_packet_buffer)) {
            esp_err_t err = ai_send_packet_locked();
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t ai_send_start_message(void)
{
    char device_id[80] = {0};
    char start_json[256] = {0};
    ai_json_escape(DEVICE_ID, device_id, sizeof(device_id));
    snprintf(start_json,
             sizeof(start_json),
             "{\"type\":\"start\",\"device_id\":\"%s\",\"sample_rate\":%d,\"bits\":%d,\"channels\":%d,\"format\":\"pcm\"}",
             device_id,
             AUDIO_SAMPLE_RATE,
             AUDIO_BITS,
             AUDIO_CHANNELS);
    int sent = esp_websocket_client_send_text(s_ws, start_json, strlen(start_json), pdMS_TO_TICKS(kWebsocketTimeoutMs));
    return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ai_wait_websocket_connected(void)
{
    EventBits_t bits = xEventGroupWaitBits(s_ws_events,
                                           kWsConnectedBit | kWsErrorBit | kWsDisconnectedBit,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(kWebsocketTimeoutMs));
    if ((bits & kWsConnectedBit) == 0) {
        return ESP_ERR_TIMEOUT;
    }

    /*
     * esp_websocket_client 的 CONNECTED 事件和内部 connected 标志之间存在很短的
     * 调度窗口。实机日志已经出现“send start failed 后才打印 connected”的时序，
     * 因此发送 start 前再轮询一次客户端真实连接状态，避免抢跑。
     */
    const int64_t start_us = esp_timer_get_time();
    while (!esp_websocket_client_is_connected(s_ws)) {
        if ((esp_timer_get_time() - start_us) > (kWebsocketTimeoutMs * 1000LL)) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

static void ai_send_stop_message(void)
{
    if (s_ws == nullptr) {
        return;
    }
    const char *stop_json = "{\"type\":\"stop\"}";
    (void)esp_websocket_client_send_text(s_ws, stop_json, strlen(stop_json), pdMS_TO_TICKS(kWebsocketTimeoutMs));
}

static void ai_wait_final_or_close(void)
{
    /*
     * Stop 后不无限等待 final。火山 ASR 经过服务器代理后，服务端可能先返回 final，
     * 也可能直接正常关闭 WebSocket。V0.6.1 规则是：用户主动 Stop 后，正常 close
     * 不算业务错误，等待窗口结束后统一收敛到 Done。
     */
    const int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) < (kFinalWaitMs * 1000LL)) {
        portENTER_CRITICAL(&s_lock);
        const bool has_final = (s_final_text[0] != '\0');
        const bool has_error = (s_state == AI_ASR_STATE_ERROR);
        portEXIT_CRITICAL(&s_lock);
        if (has_final || has_error) {
            break;
        }
        if ((xEventGroupGetBits(s_ws_events) & kWsDisconnectedBit) != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static bool ai_get_final_for_chat(char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    snprintf(out, out_size, "%s", s_final_text);
    portEXIT_CRITICAL(&s_lock);
    return out[0] != '\0' && strcmp(out, "No final text") != 0;
}

static bool ai_get_reply_for_tts(char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    snprintf(out, out_size, "%s", s_reply_text);
    portEXIT_CRITICAL(&s_lock);
    return out[0] != '\0';
}

static bool ai_try_parse_wav_pcm(const uint8_t *data,
                                 size_t bytes,
                                 const int16_t **pcm,
                                 size_t *pcm_bytes,
                                 int *sample_rate,
                                 int *bits,
                                 int *channels)
{
    if (data == nullptr || bytes < 44 || pcm == nullptr || pcm_bytes == nullptr || sample_rate == nullptr ||
        bits == nullptr || channels == nullptr) {
        return false;
    }
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t offset = 12;
    bool has_fmt = false;
    while (offset + 8 <= bytes) {
        const uint8_t *chunk = data + offset;
        uint32_t chunk_size = static_cast<uint32_t>(chunk[4]) |
                              (static_cast<uint32_t>(chunk[5]) << 8) |
                              (static_cast<uint32_t>(chunk[6]) << 16) |
                              (static_cast<uint32_t>(chunk[7]) << 24);
        offset += 8;
        if (offset + chunk_size > bytes) {
            return false;
        }
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            uint16_t audio_format = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            *channels = static_cast<uint16_t>(data[offset + 2]) | (static_cast<uint16_t>(data[offset + 3]) << 8);
            *sample_rate = static_cast<int>(data[offset + 4]) |
                           (static_cast<int>(data[offset + 5]) << 8) |
                           (static_cast<int>(data[offset + 6]) << 16) |
                           (static_cast<int>(data[offset + 7]) << 24);
            *bits = static_cast<uint16_t>(data[offset + 14]) | (static_cast<uint16_t>(data[offset + 15]) << 8);
            if (audio_format != 1) {
                return false;
            }
            has_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0 && has_fmt) {
            *pcm = reinterpret_cast<const int16_t *>(data + offset);
            *pcm_bytes = chunk_size;
            return true;
        }
        offset += (chunk_size + 1) & ~static_cast<size_t>(1);
    }
    return false;
}

static void ai_request_tts_if_ready(void)
{
    char reply_text[512] = {0};
    cloud_tts_audio_t audio = {};

    if (!ai_get_reply_for_tts(reply_text, sizeof(reply_text))) {
        ESP_LOGI(TAG, "TTS skipped: reply text empty");
        return;
    }

    /*
     * V0.8 只做一轮回复合成：LLM 成功后请求服务器 TTS，服务器返回
     * 16kHz/16bit/mono PCM 或 WAV PCM。该后台任务不是 UI task，因此可以
     * 执行 HTTP 下载和同步播放，但不能操作 LVGL。
     */
    ai_set_tts_state(AI_TTS_STATE_REQUESTING);
    ai_set_tts_state(AI_TTS_STATE_DOWNLOADING);
    esp_err_t err = service_cloud_tts_request(reply_text, &audio);
    if (err != ESP_OK) {
        const char *cloud_tts_error = service_cloud_get_last_tts_error();
        if (cloud_tts_error != nullptr && strstr(cloud_tts_error, "Config Missing") != nullptr) {
            ai_set_tts_error("TTS Config Missing", AI_TTS_STATE_CONFIG_MISSING);
        } else if (err == ESP_ERR_NOT_SUPPORTED) {
            ai_set_tts_error(cloud_tts_error ? cloud_tts_error : "TTS Unsupported Format", AI_TTS_STATE_UNSUPPORTED_FORMAT);
        } else {
            ai_set_tts_error(cloud_tts_error ? cloud_tts_error : "TTS request failed");
        }
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_tts_audio_bytes = audio.bytes;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    const int16_t *pcm = reinterpret_cast<const int16_t *>(audio.data);
    size_t pcm_bytes = audio.bytes;
    int sample_rate = audio.sample_rate;
    int bits = audio.bits;
    int channels = audio.channels;
    if (strcmp(audio.audio_format, "wav") == 0) {
        if (!ai_try_parse_wav_pcm(audio.data, audio.bytes, &pcm, &pcm_bytes, &sample_rate, &bits, &channels)) {
            service_cloud_free_tts_audio(&audio);
            ai_set_tts_error("WAV PCM parse failed", AI_TTS_STATE_UNSUPPORTED_FORMAT);
            return;
        }
    } else if (strcmp(audio.audio_format, "pcm") != 0) {
        service_cloud_free_tts_audio(&audio);
        ai_set_tts_error("TTS Unsupported Format", AI_TTS_STATE_UNSUPPORTED_FORMAT);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_tts_speaking = true;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ai_set_tts_state(AI_TTS_STATE_PLAYING);
    err = service_audio_play_pcm_buffer(pcm, pcm_bytes, sample_rate, bits, channels);
    service_cloud_free_tts_audio(&audio);

    portENTER_CRITICAL(&s_lock);
    s_tts_speaking = false;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    if (err != ESP_OK) {
        ai_set_tts_error("TTS playback failed");
        return;
    }
    portENTER_CRITICAL(&s_lock);
    snprintf(s_tts_error, sizeof(s_tts_error), "None");
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ai_set_tts_state(AI_TTS_STATE_DONE);
}

static void ai_request_llm_if_ready(void)
{
    char final_text[512] = {0};
    char reply_text[512] = {0};

    if (!ai_get_final_for_chat(final_text, sizeof(final_text))) {
        ESP_LOGI(TAG, "LLM skipped: final text empty");
        return;
    }

    /*
     * V0.7 采用 ASR final 后自动请求 Chat 的最小闭环。HTTP 请求由 service_cloud
     * 执行，本任务不是 UI task，因此不会阻塞 LVGL；真实 ARK_API_KEY 仍只在服务器端。
     */
    ai_set_llm_state(AI_LLM_STATE_REQUESTING);
    const esp_err_t err = service_cloud_chat_request(final_text, reply_text, sizeof(reply_text));
    if (err != ESP_OK) {
        const char *cloud_chat_error = service_cloud_get_last_chat_error();
        if (cloud_chat_error != nullptr && strstr(cloud_chat_error, "Config Missing") != nullptr) {
            ai_set_llm_error("LLM Config Missing", AI_LLM_STATE_CONFIG_MISSING);
        } else {
            ai_set_llm_error(cloud_chat_error ? cloud_chat_error : "LLM request failed");
        }
        return;
    }

    ESP_LOGI(TAG, "LLM reply text: %s", reply_text);
    portENTER_CRITICAL(&s_lock);
    snprintf(s_reply_text, sizeof(s_reply_text), "%s", reply_text);
    snprintf(s_llm_error, sizeof(s_llm_error), "None");
    s_llm_state = AI_LLM_STATE_DONE;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "LLM state -> %s", llm_state_to_string(AI_LLM_STATE_DONE));
    ai_request_tts_if_ready();
}

static void ai_asr_task(void *arg)
{
    (void)arg;
    char ws_url[224] = {0};
    char headers[160] = {0};

    if (!ai_build_ws_url(ws_url, sizeof(ws_url))) {
        ai_set_error("invalid cloud ASR websocket URL");
        s_asr_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    snprintf(headers, sizeof(headers), "X-Device-Token: %s\r\n", DEVICE_SHARED_SECRET);

    esp_websocket_client_config_t config = {};
    config.uri = ws_url;
    config.headers = headers;
    config.network_timeout_ms = kWebsocketTimeoutMs;
    config.reconnect_timeout_ms = 0;
    config.disable_auto_reconnect = true;
    config.task_stack = 4096;
    config.buffer_size = 1024;

    s_ws = esp_websocket_client_init(&config);
    if (s_ws == nullptr) {
        ai_set_error("websocket init failed");
        s_asr_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, websocket_event_handler, nullptr));

    ai_set_state(AI_ASR_STATE_CONNECTING);
    xEventGroupClearBits(s_ws_events, kWsConnectedBit | kWsDisconnectedBit | kWsErrorBit);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ai_set_error("websocket start failed");
        esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
        s_asr_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (ai_wait_websocket_connected() != ESP_OK) {
        ai_set_error("websocket connect timeout");
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
        s_asr_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (ai_send_start_message() != ESP_OK) {
        ai_set_error("send start failed");
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = nullptr;
        s_asr_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ai_set_state(AI_ASR_STATE_RECORDING);
    ai_set_recording(true);
    ESP_LOGI(TAG, "ASR recording started, max=%lus packet=%lums", static_cast<unsigned long>(kAsrMaxSeconds), static_cast<unsigned long>(kAsrPacketMs));
    if (service_audio_start_pcm_capture(ai_audio_pcm_callback, nullptr) != ESP_OK) {
        ai_set_recording(false);
        ai_set_error("audio capture start failed");
    } else {
        const int64_t start_us = esp_timer_get_time();
        while (!ai_stop_requested()) {
            const float elapsed = (esp_timer_get_time() - start_us) / 1000000.0f;
            if (elapsed >= static_cast<float>(kAsrMaxSeconds)) {
                ESP_LOGW(TAG, "ASR max duration reached, auto stop: %.1fs", static_cast<double>(elapsed));
                break;
            }
            if ((xEventGroupGetBits(s_ws_events) & (kWsErrorBit | kWsDisconnectedBit)) != 0) {
                ai_set_error("websocket disconnected");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        (void)service_audio_stop_pcm_capture();
        ai_set_recording(false);
    }

    (void)ai_send_packet_locked();
    ai_set_state(AI_ASR_STATE_WAITING_FINAL);
    ai_send_stop_message();
    ai_wait_final_or_close();

    portENTER_CRITICAL(&s_lock);
    const bool has_error = (s_state == AI_ASR_STATE_ERROR);
    if (!has_error && s_final_text[0] == '\0') {
        snprintf(s_final_text, sizeof(s_final_text), "No final text");
        ai_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
    if (!has_error) {
        ai_set_state(AI_ASR_STATE_DONE);
    }
    ESP_LOGI(TAG, "ASR session ended, sent_seconds=%.2f", static_cast<double>(service_ai_get_asr_sent_seconds()));

    /*
     * 服务端收到 stop 后通常会主动关闭连接。若连接已经断开，再调用 stop 会打印
     * “Client was not started”警告。这里仅在连接仍保持时主动 stop。
     */
    const EventBits_t final_ws_bits = xEventGroupGetBits(s_ws_events);
    if ((final_ws_bits & kWsDisconnectedBit) == 0 && esp_websocket_client_is_connected(s_ws)) {
        esp_websocket_client_stop(s_ws);
    }
    esp_websocket_client_destroy(s_ws);
    s_ws = nullptr;

    if (!has_error) {
        ai_request_llm_if_ready();
    }

    portENTER_CRITICAL(&s_lock);
    s_stop_requested = false;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    s_asr_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

esp_err_t service_ai_init(void)
{
    if (s_ws_events == nullptr) {
        s_ws_events = xEventGroupCreate();
        if (s_ws_events == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "service_ai initialized (V0.8 ASR + LLM + TTS bridge ready)");
    return ESP_OK;
}

esp_err_t service_ai_start_asr(void)
{
    if (s_asr_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(CLOUD_SERVER_BASE_URL) == 0 || strlen(DEVICE_ID) == 0) {
        ai_set_error("cloud config missing");
        return ESP_ERR_INVALID_STATE;
    }
    if (!service_network_is_connected()) {
        ai_set_error("network offline");
        return ESP_ERR_INVALID_STATE;
    }
    if (!service_cloud_is_registered()) {
        ai_set_error("cloud not registered");
        return ESP_ERR_INVALID_STATE;
    }
    if (service_audio_get_state() != AUDIO_STATE_READY) {
        ai_set_error("audio not ready");
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_lock);
    s_stop_requested = false;
    ai_reset_result_locked();
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);

    BaseType_t ok = xTaskCreate(ai_asr_task, "ai_asr", kTaskStackBytes, nullptr, kTaskPriority, &s_asr_task);
    if (ok != pdPASS) {
        s_asr_task = nullptr;
        ai_set_error("create ASR task failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t service_ai_stop_asr(void)
{
    if (s_asr_task == nullptr) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_lock);
    s_stop_requested = true;
    ai_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "ASR stop requested");
    return ESP_OK;
}

ai_asr_state_t service_ai_get_asr_state(void)
{
    portENTER_CRITICAL(&s_lock);
    ai_asr_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_ai_get_asr_state_string(void)
{
    return asr_state_to_string(service_ai_get_asr_state());
}

const char *service_ai_get_partial_text(void)
{
    static char text[256];
    portENTER_CRITICAL(&s_lock);
    snprintf(text, sizeof(text), "%s", s_partial_text[0] ? s_partial_text : "--");
    portEXIT_CRITICAL(&s_lock);
    return text;
}

const char *service_ai_get_final_text(void)
{
    static char text[512];
    portENTER_CRITICAL(&s_lock);
    snprintf(text, sizeof(text), "%s", s_final_text[0] ? s_final_text : "--");
    portEXIT_CRITICAL(&s_lock);
    return text;
}

const char *service_ai_get_last_error(void)
{
    static char error[160];
    portENTER_CRITICAL(&s_lock);
    snprintf(error, sizeof(error), "%s", s_last_error);
    portEXIT_CRITICAL(&s_lock);
    return error;
}

ai_llm_state_t service_ai_get_llm_state(void)
{
    portENTER_CRITICAL(&s_lock);
    ai_llm_state_t state = s_llm_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_ai_get_llm_state_string(void)
{
    return llm_state_to_string(service_ai_get_llm_state());
}

const char *service_ai_get_reply_text(void)
{
    static char text[512];
    portENTER_CRITICAL(&s_lock);
    snprintf(text, sizeof(text), "%s", s_reply_text[0] ? s_reply_text : "--");
    portEXIT_CRITICAL(&s_lock);
    return text;
}

const char *service_ai_get_reply_status(void)
{
    ai_llm_state_t state = service_ai_get_llm_state();
    switch (state) {
    case AI_LLM_STATE_DONE:
        return "Response received";
    case AI_LLM_STATE_REQUESTING:
        return "Requesting";
    case AI_LLM_STATE_CONFIG_MISSING:
        return "Config Missing";
    case AI_LLM_STATE_ERROR:
        return "Error";
    case AI_LLM_STATE_IDLE:
    default:
        return "--";
    }
}

const char *service_ai_get_llm_error(void)
{
    static char error[160];
    portENTER_CRITICAL(&s_lock);
    snprintf(error, sizeof(error), "%s", s_llm_error);
    portEXIT_CRITICAL(&s_lock);
    return error;
}

ai_tts_state_t service_ai_get_tts_state(void)
{
    portENTER_CRITICAL(&s_lock);
    ai_tts_state_t state = s_tts_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_ai_get_tts_state_string(void)
{
    return tts_state_to_string(service_ai_get_tts_state());
}

const char *service_ai_get_tts_status(void)
{
    ai_tts_state_t state = service_ai_get_tts_state();
    switch (state) {
    case AI_TTS_STATE_PLAYING:
        return "Speaking";
    case AI_TTS_STATE_DONE:
        return "Speech Done";
    case AI_TTS_STATE_REQUESTING:
    case AI_TTS_STATE_DOWNLOADING:
        return "Preparing Speech";
    case AI_TTS_STATE_CONFIG_MISSING:
        return "Config Missing";
    case AI_TTS_STATE_UNSUPPORTED_FORMAT:
        return "Unsupported Format";
    case AI_TTS_STATE_ERROR:
        return "Error";
    case AI_TTS_STATE_IDLE:
    default:
        return "--";
    }
}

const char *service_ai_get_tts_error(void)
{
    static char error[160];
    portENTER_CRITICAL(&s_lock);
    snprintf(error, sizeof(error), "%s", s_tts_error);
    portEXIT_CRITICAL(&s_lock);
    return error;
}

bool service_ai_is_tts_speaking(void)
{
    portENTER_CRITICAL(&s_lock);
    bool speaking = s_tts_speaking;
    portEXIT_CRITICAL(&s_lock);
    return speaking;
}

size_t service_ai_get_tts_audio_bytes(void)
{
    portENTER_CRITICAL(&s_lock);
    size_t bytes = s_tts_audio_bytes;
    portEXIT_CRITICAL(&s_lock);
    return bytes;
}

float service_ai_get_asr_sent_seconds(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t sent_bytes = s_sent_bytes;
    portEXIT_CRITICAL(&s_lock);
    const float bytes_per_second = AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS / 8.0f);
    return sent_bytes / bytes_per_second;
}

bool service_ai_is_asr_recording(void)
{
    portENTER_CRITICAL(&s_lock);
    bool recording = s_recording;
    portEXIT_CRITICAL(&s_lock);
    return recording;
}

uint32_t service_ai_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}
