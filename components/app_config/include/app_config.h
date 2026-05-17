#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 应用编译期配置。
 *
 * 本文件可以提交到仓库，只能保存占位值和非敏感默认值。
 * 本地真实 Wi-Fi、设备 token、服务器地址应写入同目录下的 app_secrets.h，
 * app_secrets.h 已加入 .gitignore，避免误提交真实密钥。
 */

#if defined(__has_include)
#if __has_include("app_secrets.h")
#include "app_secrets.h"
#endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#define AI_USE_BACKEND_SERVER 0
#define AI_PROVIDER "placeholder"

#ifndef DOUBAO_API_KEY
#define DOUBAO_API_KEY "YOUR_DOUBAO_API_KEY"
#endif
#ifndef DOUBAO_ASR_MODEL
#define DOUBAO_ASR_MODEL "YOUR_ASR_MODEL"
#endif
#ifndef DOUBAO_LLM_MODEL
#define DOUBAO_LLM_MODEL "YOUR_LLM_MODEL"
#endif
#ifndef DOUBAO_TTS_MODEL
#define DOUBAO_TTS_MODEL "YOUR_TTS_MODEL"
#endif
#ifndef DOUBAO_TTS_VOICE
#define DOUBAO_TTS_VOICE "YOUR_TTS_VOICE"
#endif

#ifndef CLOUD_SERVER_BASE_URL
#define CLOUD_SERVER_BASE_URL ""
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "ESP32S3_DEVICE_PLACEHOLDER"
#endif
#ifndef DEVICE_SHARED_SECRET
#define DEVICE_SHARED_SECRET ""
#endif
#ifndef APP_FIRMWARE_VERSION
#define APP_FIRMWARE_VERSION "V0.5.3"
#endif
#ifndef DEVICE_HARDWARE_NAME
#define DEVICE_HARDWARE_NAME "ESP32-S3-Touch-LCD-3.49"
#endif

/* 兼容早期占位宏，后续新代码统一使用 CLOUD_SERVER_BASE_URL / DEVICE_ID。 */
#define SERVER_BASE_URL CLOUD_SERVER_BASE_URL
#define SERVER_DEVICE_ID DEVICE_ID

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_BITS 16
#define AUDIO_CHANNELS 1

#define LOG_LEVEL 3
#define UI_THEME "default"

void app_config_log_summary(void);

#ifdef __cplusplus
}
#endif
