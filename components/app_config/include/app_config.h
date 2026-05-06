#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
WARNING:
Storing API keys in firmware is only acceptable for local development and demo testing.
For production, use a backend server or temporary token mechanism.
Do not commit real API keys to Git.
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
#define AI_PROVIDER "doubao"

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

#define SERVER_BASE_URL "https://your-backend.example.com"
#define SERVER_DEVICE_ID "ESP32S3_DEVICE_PLACEHOLDER"

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_BITS 16
#define AUDIO_CHANNELS 1

#define LOG_LEVEL 3
#define UI_THEME "default"

void app_config_log_summary(void);

#ifdef __cplusplus
}
#endif
