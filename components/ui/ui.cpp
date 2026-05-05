#include "ui.h"

#include <algorithm>
#include <assert.h>
#include <string.h>

#include "bsp_board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_router.h"

static const char *TAG = "UI";

namespace {

constexpr uint32_t kLvglTickMs = 5;
constexpr uint32_t kUiTaskMinDelayMs = 5;
constexpr uint32_t kUiTaskMaxDelayMs = 500;
constexpr uint32_t kUiTaskStack = 6144;
constexpr UBaseType_t kUiTaskPrio = 4;
static SemaphoreHandle_t s_lvgl_mux = nullptr;
static esp_timer_handle_t s_lvgl_tick_timer = nullptr;
static TaskHandle_t s_ui_task = nullptr;

static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;
static lv_color_t *s_lvgl_draw_buf = nullptr;
static uint8_t *s_lvgl_dma_buf = nullptr;
static bool s_inited = false;

} // namespace

static bool ui_lvgl_lock(int timeout_ms)
{
    if (s_lvgl_mux == nullptr) {
        return false;
    }
    TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, timeout_ticks) == pdTRUE;
}

static void ui_lvgl_unlock(void)
{
    if (s_lvgl_mux != nullptr) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

static void ui_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(kLvglTickMs);
}

static void ui_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x = 0;
    uint16_t y = 0;
    if (bsp_board_touch_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void ui_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = static_cast<esp_lcd_panel_handle_t>(drv->user_data);
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    size_t line_bytes = static_cast<size_t>(width) * sizeof(lv_color_t);
    size_t dma_buf_bytes = bsp_board_get_lvgl_dma_buffer_size();
    int y_cursor = area->y1;
    int rows_left = height;
    const uint8_t *src = reinterpret_cast<const uint8_t *>(color_map);

    while (rows_left > 0) {
        int rows = static_cast<int>(std::max<size_t>(1, dma_buf_bytes / line_bytes));
        rows = std::min(rows, rows_left);
        size_t copy_bytes = static_cast<size_t>(rows) * line_bytes;
        memcpy(s_lvgl_dma_buf, src, copy_bytes);
        esp_lcd_panel_draw_bitmap(panel, area->x1, y_cursor, area->x2 + 1, y_cursor + rows, s_lvgl_dma_buf);
        src += copy_bytes;
        y_cursor += rows;
        rows_left -= rows;
    }

    lv_disp_flush_ready(drv);
}

static void ui_task(void *arg)
{
    (void)arg;
    uint32_t task_delay_ms = kUiTaskMaxDelayMs;

    for (;;) {
        if (ui_lvgl_lock(-1)) {
            ui_router_drain_pending_locked();
            task_delay_ms = lv_timer_handler();
            ui_lvgl_unlock();
        }

        task_delay_ms = std::clamp(task_delay_ms, kUiTaskMinDelayMs, kUiTaskMaxDelayMs);
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

esp_err_t ui_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "ui already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "LVGL init start");
    lv_init();

    const uint16_t hres = bsp_board_get_hres();
    const uint16_t vres = bsp_board_get_vres();
    const size_t dma_buf_size = bsp_board_get_lvgl_dma_buffer_size();

    s_lvgl_dma_buf = static_cast<uint8_t *>(heap_caps_malloc(dma_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (s_lvgl_dma_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to alloc LVGL DMA buffer");
        return ESP_ERR_NO_MEM;
    }

    s_lvgl_draw_buf = static_cast<lv_color_t *>(heap_caps_malloc(dma_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_lvgl_draw_buf == nullptr) {
        s_lvgl_draw_buf = static_cast<lv_color_t *>(heap_caps_malloc(dma_buf_size, MALLOC_CAP_8BIT));
    }
    if (s_lvgl_draw_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to alloc LVGL draw buffer");
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_disp_buf, s_lvgl_draw_buf, nullptr, dma_buf_size / sizeof(lv_color_t));

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = hres;
    s_disp_drv.ver_res = vres;
    s_disp_drv.flush_cb = ui_lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    s_disp_drv.user_data = bsp_board_get_lcd_panel();
    lv_disp_drv_register(&s_disp_drv);
    ESP_LOGI(TAG, "LVGL display driver registered");

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = ui_touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);
    ESP_LOGI(TAG, "Touch input driver registered");

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &ui_lvgl_tick_cb;
    tick_args.name = "lvgl_tick";
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, kLvglTickMs * 1000));

    s_lvgl_mux = xSemaphoreCreateMutex();
    assert(s_lvgl_mux != nullptr);

    ESP_RETURN_ON_ERROR(ui_router_init(), TAG, "router init failed");
    xTaskCreatePinnedToCore(ui_task, "ui_task", kUiTaskStack, nullptr, kUiTaskPrio, &s_ui_task, 0);
    ESP_LOGI(TAG, "UI task created");

    if (ui_lvgl_lock(-1)) {
        ui_router_show_boot_placeholder_locked();
        ui_lvgl_unlock();
    }

    ESP_LOGI(TAG, "LVGL initialized");
    s_inited = true;
    return ESP_OK;
}
