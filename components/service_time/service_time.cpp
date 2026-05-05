#include "service_time.h"

#include "esp_log.h"

static const char *TAG = "TIME";

esp_err_t service_time_init(void)
{
    ESP_LOGI(TAG, "service_time initialized (skeleton)");
    return ESP_OK;
}
