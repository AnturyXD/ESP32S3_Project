#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_STATE_NOT_SYNCED = 0,
    TIME_STATE_SYNCING,
    TIME_STATE_SYNCED,
    TIME_STATE_ERROR,
} time_state_t;

esp_err_t service_time_init(void);
time_state_t service_time_get_state(void);
const char *service_time_get_state_str(time_state_t state);
const char *service_time_get_state_string(void);
void service_time_get_current_time(char *time_buf, size_t time_buf_size);
const char *service_time_get_time_string(void);
bool service_time_is_synced(void);
uint32_t service_time_get_revision(void);

#ifdef __cplusplus
}
#endif
