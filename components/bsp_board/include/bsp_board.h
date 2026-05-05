#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_board_init(void);
esp_lcd_panel_handle_t bsp_board_get_lcd_panel(void);
esp_lcd_panel_io_handle_t bsp_board_get_lcd_io(void);
bool bsp_board_touch_read(uint16_t *x, uint16_t *y);
uint16_t bsp_board_get_hres(void);
uint16_t bsp_board_get_vres(void);
size_t bsp_board_get_lvgl_dma_buffer_size(void);

#ifdef __cplusplus
}
#endif
