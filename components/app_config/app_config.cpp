#include "app_config.h"

#include "esp_log.h"

static const char *TAG = "APP_CFG";

void app_config_log_summary(void)
{
    ESP_LOGI(TAG, "Config loaded: provider=%s backend=%d", AI_PROVIDER, AI_USE_BACKEND_SERVER);
    ESP_LOGI(TAG, "Wi-Fi SSID configured: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Audio config: %d Hz, %d bit, %d ch", AUDIO_SAMPLE_RATE, AUDIO_BITS, AUDIO_CHANNELS);
}
