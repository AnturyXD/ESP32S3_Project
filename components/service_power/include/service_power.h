#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_STATE_INIT = 0,
    POWER_STATE_HOLD_ON,
    POWER_STATE_RUNNING,
    POWER_STATE_PWR_PRESSED,
    POWER_STATE_SHUTDOWN_PENDING,
    POWER_STATE_POWER_OFF,
    POWER_STATE_ERROR,
} power_state_t;

esp_err_t service_power_init(void);
esp_err_t service_power_start_task(void);
bool service_power_is_pwr_pressed(void);
esp_err_t service_power_set_hold_enabled(bool enable);
esp_err_t service_power_request_shutdown(void);
power_state_t service_power_get_state(void);
const char *service_power_get_last_event(void);

#ifdef __cplusplus
}
#endif
