#include "app_config.h"
#include "app_shell.h"
#include "bsp_board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "service_ai.h"
#include "service_audio.h"
#include "service_cloud.h"
#include "service_log.h"
#include "service_network.h"
#include "service_power.h"
#include "service_storage.h"
#include "service_time.h"
#include "ui.h"

static const char *TAG = "APP";

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(service_log_init());
    ESP_LOGI(TAG, "System boot start");

    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM initialized, free=%u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized");
    }

    app_config_log_summary();

    ESP_ERROR_CHECK(service_power_init());
    ESP_ERROR_CHECK(service_power_start_task());
    ESP_ERROR_CHECK(bsp_board_init());
    ESP_ERROR_CHECK(ui_init());
    ESP_ERROR_CHECK(app_shell_init());
    ESP_ERROR_CHECK(service_network_init());
    ESP_ERROR_CHECK(service_time_init());
    ESP_ERROR_CHECK(service_audio_init());
    ESP_ERROR_CHECK(service_storage_init());
    ESP_ERROR_CHECK(service_cloud_init());
    ESP_ERROR_CHECK(service_ai_init());

    ESP_LOGI(TAG, "System boot done (V0.1 skeleton)");
}
