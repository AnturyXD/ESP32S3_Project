#include "service_audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bsp_io_expander.h"
#include "codec_board.h"
#include "codec_init.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO";
static const char *TAG_IN = "AUDIO_IN";
static const char *TAG_OUT = "AUDIO_OUT";
static const char *TAG_CODEC = "CODEC";

namespace {

constexpr uint32_t kSampleRate = AUDIO_SAMPLE_RATE;
constexpr uint16_t kBitsPerSample = AUDIO_BITS;
// V0.5.1 固化对上层输出的音频格式：16kHz / 16bit / mono PCM。
constexpr uint8_t kRecordOutChannels = 1;
// ES8311 播放链路使用 stereo slot，输出保持双声道槽位。
constexpr uint8_t kPlaybackChannels = 2;
// 底层录音链路按 stereo slot 读取，再在 service_audio 内部转 mono 输出给上层 ASR。
constexpr uint8_t kCodecCaptureChannels = 2;
constexpr uint32_t kRecordFrameMs = 20;
constexpr uint32_t kRecordOutFrameBytes = (kSampleRate * (kBitsPerSample / 8) * kRecordOutChannels * kRecordFrameMs) / 1000;
constexpr uint32_t kCodecCaptureFrameBytes = (kSampleRate * (kBitsPerSample / 8) * kCodecCaptureChannels * kRecordFrameMs) / 1000;
constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPrio = 4;
constexpr float kToneFreqHz = 440.0f;
constexpr uint32_t kToneDurationMs = 2000;
constexpr int16_t kToneAmplitude = 11000;
constexpr float kPi = 3.1415926535f;
constexpr uint8_t kSysEnPin = 6;
constexpr uint8_t kSpeakerEnablePin = 7;

static bool s_inited = false;
static bool s_recording = false;
static bool s_playing = false;
static bool s_record_stop_req = false;
static bool s_play_stop_req = false;
static TaskHandle_t s_record_task = nullptr;
static TaskHandle_t s_play_task = nullptr;
static esp_codec_dev_handle_t s_playback = nullptr;
static esp_codec_dev_handle_t s_record = nullptr;
static service_audio_pcm_callback_t s_pcm_callback = nullptr;
static void *s_pcm_callback_ctx = nullptr;
static audio_state_t s_state = AUDIO_STATE_UNINIT;
static uint32_t s_last_pcm_bytes = 0;
static uint16_t s_peak_level = 0;
static char s_last_event[96] = "UNINIT";
static uint32_t s_revision = 0;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static_assert(AUDIO_SAMPLE_RATE == 16000, "service_audio expects 16kHz sample rate");
static_assert(AUDIO_BITS == 16, "service_audio expects 16-bit PCM");
static_assert(AUDIO_CHANNELS == 1, "service_audio public output format must be mono");

static const char *audio_state_to_string(audio_state_t state)
{
    switch (state) {
    case AUDIO_STATE_UNINIT:
        return "Uninit";
    case AUDIO_STATE_INIT:
        return "Init";
    case AUDIO_STATE_READY:
        return "Ready";
    case AUDIO_STATE_RECORDING:
        return "Recording";
    case AUDIO_STATE_PLAYING:
        return "Playing";
    case AUDIO_STATE_STOPPING:
        return "Stopping";
    case AUDIO_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static void audio_touch_revision_locked(void)
{
    ++s_revision;
}

static void audio_set_state(audio_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = (s_state != state);
    s_state = state;
    if (changed) {
        audio_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "state -> %s", audio_state_to_string(state));
    }
}

static void audio_set_event(const char *event_text)
{
    if (event_text == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_last_event, event_text, sizeof(s_last_event));
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
}

static void audio_set_pcm_stats(uint32_t pcm_bytes, uint16_t peak)
{
    portENTER_CRITICAL(&s_lock);
    bool changed = (s_last_pcm_bytes != pcm_bytes) || (s_peak_level != peak);
    s_last_pcm_bytes = pcm_bytes;
    s_peak_level = peak;
    if (changed) {
        audio_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);
}

static void audio_mark_error(const char *event_text, const char *log_text)
{
    audio_set_state(AUDIO_STATE_ERROR);
    audio_set_event(event_text);
    ESP_LOGE(TAG, "%s", log_text);
}

static uint16_t audio_calc_peak(const int16_t *samples, size_t sample_count)
{
    // Peak 统计基于 int16_t 有符号 PCM 的绝对幅值。
    uint16_t peak = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t v = samples[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = static_cast<uint16_t>(v);
        }
    }
    return peak;
}

static void audio_expand_mono_to_stereo(const int16_t *mono, int16_t *stereo, size_t samples)
{
    if (mono == nullptr || stereo == nullptr) {
        return;
    }
    for (size_t i = 0; i < samples; ++i) {
        // 上层 mono PCM 播放前复制到 L/R 双声道槽位。
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }
}

static void audio_downmix_stereo_to_mono(const int16_t *stereo, int16_t *mono, size_t samples)
{
    if (stereo == nullptr || mono == nullptr) {
        return;
    }
    for (size_t i = 0; i < samples; ++i) {
        int32_t l = stereo[i * 2];
        int32_t r = stereo[i * 2 + 1];
        mono[i] = static_cast<int16_t>((l + r) / 2);
    }
}

static esp_err_t audio_open_streams_once(void)
{
    esp_codec_dev_sample_info_t play_info = {};
    play_info.bits_per_sample = kBitsPerSample;
    play_info.channel = kPlaybackChannels;
    play_info.channel_mask = 0;
    play_info.sample_rate = kSampleRate;
    play_info.mclk_multiple = 0;

    if (esp_codec_dev_open(s_playback, &play_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t rec_info = {};
    rec_info.bits_per_sample = kBitsPerSample;
    rec_info.channel = kCodecCaptureChannels;
    rec_info.channel_mask = 0;
    rec_info.sample_rate = kSampleRate;
    rec_info.mclk_multiple = 0;
    if (esp_codec_dev_open(s_record, &rec_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t audio_enable_speaker_path(void)
{
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG_CODEC, "speaker path: IOX init failed");

    // EXIO7 连接扬声器通路使能，必须拉高后功放路径才会导通。
    // 该引脚与 EXIO6/SYS_EN 共用 TCA9554，所以只能通过 bsp_io_expander 共享层写入。
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(kSpeakerEnablePin, true),
                        TAG_CODEC,
                        "speaker path: set EXIO7 direction failed");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_output(kSpeakerEnablePin, true),
                        TAG_CODEC,
                        "speaker path: set EXIO7 high failed");
    ESP_LOGI(TAG_CODEC, "service_audio set EXIO7 speaker enable HIGH");

    bool sys_en_high = false;
    ESP_RETURN_ON_ERROR(bsp_io_expander_get_output_level(kSysEnPin, &sys_en_high),
                        TAG_CODEC,
                        "speaker path: read EXIO6 failed");
    if (!sys_en_high) {
        ESP_LOGE(TAG_CODEC, "EXIO6 SYS_EN was overwritten, force restore HIGH");
        ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(kSysEnPin, true), TAG_CODEC, "restore EXIO6 direction failed");
        ESP_RETURN_ON_ERROR(bsp_io_expander_set_output(kSysEnPin, true), TAG_CODEC, "restore EXIO6 high failed");
    }
    ESP_LOGI(TAG_CODEC, "service_audio after speaker enable: EXIO6 state still HIGH");
    return ESP_OK;
}

static void audio_record_task(void *arg)
{
    (void)arg;
    uint8_t *capture_buf = static_cast<uint8_t *>(heap_caps_malloc(kCodecCaptureFrameBytes, MALLOC_CAP_8BIT));
    int16_t *mono_buf = static_cast<int16_t *>(heap_caps_malloc(kRecordOutFrameBytes, MALLOC_CAP_8BIT));
    if (capture_buf == nullptr || mono_buf == nullptr) {
        if (capture_buf != nullptr) {
            heap_caps_free(capture_buf);
        }
        if (mono_buf != nullptr) {
            heap_caps_free(mono_buf);
        }
        audio_mark_error("REC_MEM_FAIL", "record buffer alloc failed");
        s_record_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    uint32_t loop_count = 0;
    ESP_LOGI(TAG_IN,
             "record test started, capture=%lu bytes output_mono=%lu bytes",
             static_cast<unsigned long>(kCodecCaptureFrameBytes),
             static_cast<unsigned long>(kRecordOutFrameBytes));
    audio_set_event("REC_STARTED");

    while (!s_record_stop_req) {
        int err = esp_codec_dev_read(s_record, capture_buf, static_cast<int>(kCodecCaptureFrameBytes));
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG_IN, "esp_codec_dev_read failed: %d", static_cast<int>(err));
            audio_mark_error("REC_READ_FAIL", "record read failed");
            break;
        }

        const size_t mono_samples = kRecordOutFrameBytes / sizeof(int16_t);
        if (kCodecCaptureChannels == 1) {
            memcpy(mono_buf, capture_buf, kRecordOutFrameBytes);
        } else {
            audio_downmix_stereo_to_mono(reinterpret_cast<const int16_t *>(capture_buf), mono_buf, mono_samples);
        }

        uint16_t peak = audio_calc_peak(mono_buf, mono_samples);
        audio_set_pcm_stats(kRecordOutFrameBytes, peak);
        service_audio_pcm_callback_t callback = nullptr;
        void *callback_ctx = nullptr;
        portENTER_CRITICAL(&s_lock);
        callback = s_pcm_callback;
        callback_ctx = s_pcm_callback_ctx;
        portEXIT_CRITICAL(&s_lock);
        if (callback != nullptr) {
            /*
             * ASR 只接收 service_audio 输出的 16kHz/16bit/mono PCM。
             * Stop ASR 时，上层会返回 ESP_ERR_INVALID_STATE 通知录音任务退出；
             * 这是受控停止路径，不应被当成音频硬件错误。
             */
            esp_err_t cb_err = callback(mono_buf, kRecordOutFrameBytes, callback_ctx);
            if (cb_err != ESP_OK) {
                if (cb_err == ESP_ERR_INVALID_STATE) {
                    ESP_LOGI(TAG_IN, "pcm callback stopped capture by request");
                } else {
                    ESP_LOGW(TAG_IN, "pcm callback stopped capture: %s", esp_err_to_name(cb_err));
                }
                audio_set_event("PCM_CB_STOP");
                break;
            }
        }
        ++loop_count;
        if ((loop_count % 25) == 0) {
            ESP_LOGI(TAG_IN,
                     "mono_pcm_bytes=%lu peak=%u",
                     static_cast<unsigned long>(kRecordOutFrameBytes),
                     static_cast<unsigned>(peak));
        }
    }

    heap_caps_free(capture_buf);
    heap_caps_free(mono_buf);
    portENTER_CRITICAL(&s_lock);
    s_recording = false;
    s_record_stop_req = false;
    s_pcm_callback = nullptr;
    s_pcm_callback_ctx = nullptr;
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);

    if (s_state != AUDIO_STATE_ERROR && !s_playing) {
        audio_set_state(AUDIO_STATE_READY);
    }
    audio_set_event("REC_STOPPED");
    ESP_LOGI(TAG_IN, "record test stopped");
    s_record_task = nullptr;
    vTaskDelete(nullptr);
}

static void audio_play_task(void *arg)
{
    (void)arg;
    const uint32_t total_samples = (kSampleRate * kToneDurationMs) / 1000;
    const uint32_t frame_samples = (kSampleRate * kRecordFrameMs) / 1000;
    uint32_t generated = 0;
    float phase = 0.0f;
    const float phase_step = (2.0f * kPi * kToneFreqHz) / static_cast<float>(kSampleRate);
    const size_t frame_bytes = frame_samples * kPlaybackChannels * sizeof(int16_t);

    int16_t *mono_buf = static_cast<int16_t *>(heap_caps_malloc(frame_samples * sizeof(int16_t), MALLOC_CAP_8BIT));
    int16_t *tone_buf = static_cast<int16_t *>(heap_caps_malloc(frame_bytes, MALLOC_CAP_8BIT));
    if (mono_buf == nullptr || tone_buf == nullptr) {
        if (mono_buf != nullptr) {
            heap_caps_free(mono_buf);
        }
        if (tone_buf != nullptr) {
            heap_caps_free(tone_buf);
        }
        audio_mark_error("PLAY_MEM_FAIL", "play buffer alloc failed");
        s_play_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG_OUT,
             "test tone started, freq=%.1fHz duration=%lums",
             static_cast<double>(kToneFreqHz),
             static_cast<unsigned long>(kToneDurationMs));
    audio_set_event("PLAY_STARTED");

    while (!s_play_stop_req && generated < total_samples) {
        uint32_t samples_now = frame_samples;
        if (samples_now > (total_samples - generated)) {
            samples_now = total_samples - generated;
        }

        for (uint32_t i = 0; i < samples_now; ++i) {
            mono_buf[i] = static_cast<int16_t>(kToneAmplitude * sinf(phase));
            phase += phase_step;
            if (phase > (2.0f * kPi)) {
                phase -= (2.0f * kPi);
            }
        }
        audio_expand_mono_to_stereo(mono_buf, tone_buf, samples_now);

        size_t bytes_now = samples_now * kPlaybackChannels * sizeof(int16_t);
        int err = esp_codec_dev_write(s_playback, tone_buf, static_cast<int>(bytes_now));
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG_OUT, "esp_codec_dev_write failed: %d", static_cast<int>(err));
            audio_mark_error("PLAY_WRITE_FAIL", "play write failed");
            break;
        }

        generated += samples_now;
        audio_set_pcm_stats(static_cast<uint32_t>(bytes_now), static_cast<uint16_t>(kToneAmplitude));
    }

    heap_caps_free(mono_buf);
    heap_caps_free(tone_buf);
    portENTER_CRITICAL(&s_lock);
    s_playing = false;
    s_play_stop_req = false;
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);

    if (s_state != AUDIO_STATE_ERROR && !s_recording) {
        audio_set_state(AUDIO_STATE_READY);
    }
    audio_set_event("PLAY_STOPPED");
    ESP_LOGI(TAG_OUT, "test tone stopped");
    s_play_task = nullptr;
    vTaskDelete(nullptr);
}

static esp_err_t audio_wait_task_exit(TaskHandle_t *task_handle, uint32_t timeout_ms)
{
    const uint32_t step_ms = 20;
    uint32_t waited = 0;
    while (*task_handle != nullptr && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    return (*task_handle == nullptr) ? ESP_OK : ESP_ERR_TIMEOUT;
}

} // namespace

esp_err_t service_audio_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "service_audio already initialized");
        return ESP_OK;
    }

    audio_set_state(AUDIO_STATE_INIT);
    audio_set_event("INIT_START");
    ESP_LOGI(TAG_CODEC, "init codec board for S3_LCD_3_49");

    set_codec_board_type("S3_LCD_3_49");
    codec_init_cfg_t codec_cfg = {
        .in_mode = CODEC_I2S_MODE_STD,
        .out_mode = CODEC_I2S_MODE_STD,
        .in_use_tdm = false,
        .reuse_dev = false,
    };
    if (init_codec(&codec_cfg) != 0) {
        audio_mark_error("CODEC_INIT_FAIL", "init_codec failed");
        return ESP_FAIL;
    }

    s_playback = get_playback_handle();
    s_record = get_record_handle();
    if (s_playback == nullptr || s_record == nullptr) {
        audio_mark_error("CODEC_HANDLE_NULL", "codec handle missing");
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(s_playback, 70.0f);
    esp_codec_dev_set_in_gain(s_record, 30.0f);
    if (audio_open_streams_once() != ESP_OK) {
        audio_mark_error("AUDIO_OPEN_FAIL", "open codec stream failed");
        return ESP_FAIL;
    }
    (void)audio_enable_speaker_path();

    s_inited = true;
    audio_set_state(AUDIO_STATE_READY);
    audio_set_event("READY");
    ESP_LOGI(TAG, "service_audio ready: output=%luHz %u-bit %u-ch mono PCM, playback_slot=%u-ch",
             static_cast<unsigned long>(kSampleRate),
             static_cast<unsigned>(kBitsPerSample),
             static_cast<unsigned>(kRecordOutChannels),
             static_cast<unsigned>(kPlaybackChannels));
    return ESP_OK;
}

esp_err_t service_audio_start_record_test(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recording) {
        return ESP_OK;
    }
    if (s_playing) {
        ESP_RETURN_ON_ERROR(service_audio_stop_playback(), TAG, "stop playback before record failed");
    }

    portENTER_CRITICAL(&s_lock);
    s_record_stop_req = false;
    s_recording = true;
    s_pcm_callback = nullptr;
    s_pcm_callback_ctx = nullptr;
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    audio_set_state(AUDIO_STATE_RECORDING);

    BaseType_t ok = xTaskCreate(audio_record_task, "audio_rec_test", kTaskStack, nullptr, kTaskPrio, &s_record_task);
    if (ok != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_recording = false;
        audio_touch_revision_locked();
        portEXIT_CRITICAL(&s_lock);
        audio_mark_error("REC_TASK_FAIL", "create record task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t service_audio_start_pcm_capture(service_audio_pcm_callback_t callback, void *user_ctx)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_recording) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        ESP_RETURN_ON_ERROR(service_audio_stop_playback(), TAG, "stop playback before pcm capture failed");
    }

    portENTER_CRITICAL(&s_lock);
    s_record_stop_req = false;
    s_recording = true;
    s_pcm_callback = callback;
    s_pcm_callback_ctx = user_ctx;
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    audio_set_state(AUDIO_STATE_RECORDING);
    audio_set_event("PCM_CAPTURE_START");

    BaseType_t ok = xTaskCreate(audio_record_task, "audio_pcm_cap", kTaskStack, nullptr, kTaskPrio, &s_record_task);
    if (ok != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_recording = false;
        s_pcm_callback = nullptr;
        s_pcm_callback_ctx = nullptr;
        audio_touch_revision_locked();
        portEXIT_CRITICAL(&s_lock);
        audio_mark_error("PCM_TASK_FAIL", "create pcm capture task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t service_audio_stop_record_test(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_recording) {
        return ESP_OK;
    }

    audio_set_state(AUDIO_STATE_STOPPING);
    portENTER_CRITICAL(&s_lock);
    s_record_stop_req = true;
    portEXIT_CRITICAL(&s_lock);
    audio_set_event("REC_STOP_REQ");
    return audio_wait_task_exit(&s_record_task, 800);
}

esp_err_t service_audio_stop_pcm_capture(void)
{
    return service_audio_stop_record_test();
}

esp_err_t service_audio_play_test_tone(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_OK;
    }
    if (s_recording) {
        ESP_RETURN_ON_ERROR(service_audio_stop_record_test(), TAG, "stop record before play failed");
    }

    portENTER_CRITICAL(&s_lock);
    s_play_stop_req = false;
    s_playing = true;
    audio_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    audio_set_state(AUDIO_STATE_PLAYING);

    BaseType_t ok = xTaskCreate(audio_play_task, "audio_play_test", kTaskStack, nullptr, kTaskPrio, &s_play_task);
    if (ok != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_playing = false;
        audio_touch_revision_locked();
        portEXIT_CRITICAL(&s_lock);
        audio_mark_error("PLAY_TASK_FAIL", "create play task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t service_audio_stop_playback(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_playing) {
        return ESP_OK;
    }

    audio_set_state(AUDIO_STATE_STOPPING);
    portENTER_CRITICAL(&s_lock);
    s_play_stop_req = true;
    portEXIT_CRITICAL(&s_lock);
    audio_set_event("PLAY_STOP_REQ");
    return audio_wait_task_exit(&s_play_task, 800);
}

audio_state_t service_audio_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    audio_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_audio_get_state_string(void)
{
    return audio_state_to_string(service_audio_get_state());
}

const char *service_audio_get_last_event(void)
{
    static char event_buf[96] = {0};
    portENTER_CRITICAL(&s_lock);
    strlcpy(event_buf, s_last_event, sizeof(event_buf));
    portEXIT_CRITICAL(&s_lock);
    return event_buf;
}

uint32_t service_audio_get_last_pcm_bytes(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t pcm_bytes = s_last_pcm_bytes;
    portEXIT_CRITICAL(&s_lock);
    return pcm_bytes;
}

uint16_t service_audio_get_peak_level(void)
{
    portENTER_CRITICAL(&s_lock);
    uint16_t peak = s_peak_level;
    portEXIT_CRITICAL(&s_lock);
    return peak;
}

bool service_audio_is_recording(void)
{
    portENTER_CRITICAL(&s_lock);
    bool recording = s_recording;
    portEXIT_CRITICAL(&s_lock);
    return recording;
}

bool service_audio_is_playing(void)
{
    portENTER_CRITICAL(&s_lock);
    bool playing = s_playing;
    portEXIT_CRITICAL(&s_lock);
    return playing;
}

uint32_t service_audio_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}
