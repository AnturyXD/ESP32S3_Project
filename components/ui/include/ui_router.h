#pragma once

#include <stdbool.h>

#include "app_shell.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_router_init(void);
void ui_router_show_boot_placeholder_locked(void);
bool ui_router_drain_pending_locked(void);

#ifdef __cplusplus
}
#endif
