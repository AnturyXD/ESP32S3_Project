#include "app_config.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "APP_CFG";

void app_config_log_summary(void)
{
    const bool wifi_ssid_configured = strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 && strlen(WIFI_SSID) > 0;
    const bool cloud_url_configured = strlen(CLOUD_SERVER_BASE_URL) > 0;
    const bool device_secret_configured = strlen(DEVICE_SHARED_SECRET) > 0;

    ESP_LOGI(TAG, "Config loaded: provider=%s backend=%d", AI_PROVIDER, AI_USE_BACKEND_SERVER);
    ESP_LOGI(TAG, "Wi-Fi SSID configured: %s", wifi_ssid_configured ? "yes" : "no");
    ESP_LOGI(TAG, "Cloud URL configured: %s", cloud_url_configured ? "yes" : "no");
    ESP_LOGI(TAG, "Device ID: %s", DEVICE_ID);
    ESP_LOGI(TAG, "Device token configured: %s", device_secret_configured ? "yes" : "no");
    ESP_LOGI(TAG, "Audio config: %d Hz, %d bit, %d ch", AUDIO_SAMPLE_RATE, AUDIO_BITS, AUDIO_CHANNELS);
}
