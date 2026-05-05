#include "service_log.h"

#include "esp_log.h"

static const char *TAG = "LOG";

esp_err_t service_log_init(void)
{
    ESP_LOGI(TAG, "service_log initialized");
    return ESP_OK;
}
