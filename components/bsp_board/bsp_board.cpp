#include "bsp_board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG_BSP = "BSP";
static const char *TAG_LCD = "LCD";
static const char *TAG_TOUCH = "TOUCH";

namespace {

constexpr spi_host_device_t kLcdHost = SPI3_HOST;

constexpr gpio_num_t kTouchScl = GPIO_NUM_18;
constexpr gpio_num_t kTouchSda = GPIO_NUM_17;

constexpr gpio_num_t kLcdCs = GPIO_NUM_9;
constexpr gpio_num_t kLcdPclk = GPIO_NUM_10;
constexpr gpio_num_t kLcdData0 = GPIO_NUM_11;
constexpr gpio_num_t kLcdData1 = GPIO_NUM_12;
constexpr gpio_num_t kLcdData2 = GPIO_NUM_13;
constexpr gpio_num_t kLcdData3 = GPIO_NUM_14;
constexpr gpio_num_t kLcdRst = GPIO_NUM_21;

constexpr int kLcdHRes = 172;
constexpr int kLcdVRes = 640;
constexpr size_t kLvglDmaBufSize = kLcdHRes * 64 * 2;

constexpr uint8_t kTouchAddr = 0x3b;
constexpr uint32_t kTouchI2cHz = 300000;

static i2c_master_bus_handle_t s_touch_bus = nullptr;
static i2c_master_dev_handle_t s_touch_dev = nullptr;
static esp_lcd_panel_io_handle_t s_lcd_io = nullptr;
static esp_lcd_panel_handle_t s_lcd_panel = nullptr;
static bool s_inited = false;

static const axs15231b_lcd_init_cmd_t kLcdInitCmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

} // namespace

static esp_err_t bsp_lcd_init(void)
{
    ESP_LOGI(TAG_LCD, "Initialize LCD reset pin");
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (1ULL << kLcdRst);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG_LCD, "gpio_config failed");

    ESP_LOGI(TAG_LCD, "Initialize QSPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num = kLcdData0;
    buscfg.data1_io_num = kLcdData1;
    buscfg.sclk_io_num = kLcdPclk;
    buscfg.data2_io_num = kLcdData2;
    buscfg.data3_io_num = kLcdData3;
    buscfg.max_transfer_sz = static_cast<int>(kLvglDmaBufSize);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(kLcdHost, &buscfg, SPI_DMA_CH_AUTO), TAG_LCD, "spi_bus_initialize failed");

    ESP_LOGI(TAG_LCD, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = kLcdCs;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(kLcdHost, &io_config, &s_lcd_io), TAG_LCD, "new panel io failed");

    ESP_LOGI(TAG_LCD, "Install AXS15231B panel driver");
    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = kLcdInitCmds;
    vendor_config.init_cmds_size = sizeof(kLcdInitCmds) / sizeof(kLcdInitCmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_axs15231b(s_lcd_io, &panel_config, &s_lcd_panel), TAG_LCD, "new panel failed");

    ESP_ERROR_CHECK(gpio_set_level(kLcdRst, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(kLcdRst, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(kLcdRst, 1));
    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_lcd_panel), TAG_LCD, "panel init failed");
    ESP_LOGI(TAG_LCD, "LCD initialized: %dx%d", kLcdHRes, kLcdVRes);
    return ESP_OK;
}

static esp_err_t bsp_touch_init(void)
{
    ESP_LOGI(TAG_TOUCH, "Initialize touch I2C bus");
    i2c_master_bus_config_t i2c_bus_config = {};
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = I2C_NUM_1;
    i2c_bus_config.scl_io_num = kTouchScl;
    i2c_bus_config.sda_io_num = kTouchSda;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_config, &s_touch_bus), TAG_TOUCH, "i2c_new_master_bus failed");

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kTouchAddr;
    dev_cfg.scl_speed_hz = kTouchI2cHz;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_touch_bus, &dev_cfg, &s_touch_dev), TAG_TOUCH, "add touch device failed");
    ESP_LOGI(TAG_TOUCH, "Touch initialized at addr=0x%02x", kTouchAddr);
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG_BSP, "bsp_board already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG_BSP, "bsp_board init start");
    ESP_RETURN_ON_ERROR(bsp_lcd_init(), TAG_BSP, "lcd init failed");
    ESP_RETURN_ON_ERROR(bsp_touch_init(), TAG_BSP, "touch init failed");
    s_inited = true;
    ESP_LOGI(TAG_BSP, "bsp_board init done");
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_board_get_lcd_panel(void)
{
    return s_lcd_panel;
}

esp_lcd_panel_io_handle_t bsp_board_get_lcd_io(void)
{
    return s_lcd_io;
}

bool bsp_board_touch_read(uint16_t *x, uint16_t *y)
{
    if (x == nullptr || y == nullptr || s_touch_dev == nullptr) {
        return false;
    }

    uint8_t read_touch_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_touch_dev, read_touch_cmd, sizeof(read_touch_cmd), buff, sizeof(buff), 50);
    if (err != ESP_OK) {
        return false;
    }

    if (buff[1] == 0 || buff[1] >= 5) {
        return false;
    }

    uint16_t point_x = static_cast<uint16_t>((buff[2] & 0x0f) << 8) | buff[3];
    uint16_t point_y = static_cast<uint16_t>((buff[4] & 0x0f) << 8) | buff[5];
    if (point_x > kLcdVRes) {
        point_x = kLcdVRes;
    }
    if (point_y > kLcdHRes) {
        point_y = kLcdHRes;
    }

    *x = point_y;
    *y = static_cast<uint16_t>(kLcdVRes - point_x);
    return true;
}

uint16_t bsp_board_get_hres(void)
{
    return kLcdHRes;
}

uint16_t bsp_board_get_vres(void)
{
    return kLcdVRes;
}

size_t bsp_board_get_lvgl_dma_buffer_size(void)
{
    return kLvglDmaBufSize;
}
