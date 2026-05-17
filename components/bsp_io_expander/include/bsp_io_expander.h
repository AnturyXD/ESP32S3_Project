#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_io_expander_init(void);
esp_err_t bsp_io_expander_set_direction(uint8_t pin, bool output);
esp_err_t bsp_io_expander_set_output(uint8_t pin, bool level);
esp_err_t bsp_io_expander_get_output_level(uint8_t pin, bool *level);
uint8_t bsp_io_expander_get_output_shadow(void);
uint8_t bsp_io_expander_get_direction_shadow(void);

#ifdef __cplusplus
}
#endif

