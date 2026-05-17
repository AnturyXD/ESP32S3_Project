#include "bsp_io_expander.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "IOX";

namespace {

constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cScl = GPIO_NUM_48;
constexpr gpio_num_t kI2cSda = GPIO_NUM_47;
constexpr uint8_t kPinMax = 7;

static bool s_inited = false;
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static esp_io_expander_handle_t s_expander = nullptr;
static SemaphoreHandle_t s_mutex = nullptr;
static uint8_t s_direction_shadow = 0xFF; // 1=input, 0=output
static uint8_t s_output_shadow = 0xFF;    // power-up default from driver reset()

static inline uint8_t pin_to_mask(uint8_t pin)
{
    return static_cast<uint8_t>(1U << pin);
}

static esp_err_t lock_take(TickType_t ticks_to_wait)
{
    if (s_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xSemaphoreTake(s_mutex, ticks_to_wait) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void lock_give(void)
{
    if (s_mutex != nullptr) {
        xSemaphoreGive(s_mutex);
    }
}

static esp_err_t ensure_i2c_bus(void)
{
    if (s_i2c_bus != nullptr) {
        return ESP_OK;
    }

    esp_err_t err = i2c_master_get_bus_handle(kI2cPort, &s_i2c_bus);
    if (err == ESP_OK && s_i2c_bus != nullptr) {
        ESP_LOGI(TAG, "reuse I2C bus %d", static_cast<int>(kI2cPort));
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = kI2cPort;
    bus_cfg.scl_io_num = kI2cScl;
    bus_cfg.sda_io_num = kI2cSda;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(kI2cPort, &s_i2c_bus);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "init I2C bus failed");
    ESP_LOGI(TAG, "init I2C bus %d", static_cast<int>(kI2cPort));
    return ESP_OK;
}

static esp_err_t validate_pin(uint8_t pin)
{
    if (pin > kPinMax) {
        ESP_LOGE(TAG, "invalid pin=%u", static_cast<unsigned>(pin));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

} // namespace

esp_err_t bsp_io_expander_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_mutex != nullptr, ESP_ERR_NO_MEM, TAG, "create mutex failed");
    }

    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), TAG, "ensure I2C bus failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(
                            s_i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_expander),
                        TAG,
                        "create TCA9554 failed");

    // Driver reset() sets direction/output to 0xFF. Keep shadows aligned.
    s_direction_shadow = 0xFF;
    s_output_shadow = 0xFF;
    s_inited = true;
    ESP_LOGI(TAG,
             "IOX init done dir_shadow=0x%02X out_shadow=0x%02X",
             static_cast<unsigned>(s_direction_shadow),
             static_cast<unsigned>(s_output_shadow));
    return ESP_OK;
}

esp_err_t bsp_io_expander_set_direction(uint8_t pin, bool output)
{
    ESP_RETURN_ON_ERROR(validate_pin(pin), TAG, "set_direction invalid pin");
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "IOX not ready");
    ESP_RETURN_ON_ERROR(lock_take(pdMS_TO_TICKS(100)), TAG, "IOX mutex timeout");

    uint8_t before = s_direction_shadow;
    uint8_t mask = pin_to_mask(pin);
    if (output) {
        s_direction_shadow = static_cast<uint8_t>(s_direction_shadow & ~mask);
    } else {
        s_direction_shadow = static_cast<uint8_t>(s_direction_shadow | mask);
    }
    esp_err_t err = esp_io_expander_set_dir(s_expander, mask, output ? IO_EXPANDER_OUTPUT : IO_EXPANDER_INPUT);
    if (err != ESP_OK) {
        s_direction_shadow = before;
        lock_give();
        return err;
    }

    uint8_t after = s_direction_shadow;
    lock_give();
    ESP_LOGI(TAG,
             "direction_shadow before=0x%02X after=0x%02X pin=%u output=%d",
             static_cast<unsigned>(before),
             static_cast<unsigned>(after),
             static_cast<unsigned>(pin),
             output ? 1 : 0);
    return ESP_OK;
}

esp_err_t bsp_io_expander_set_output(uint8_t pin, bool level)
{
    ESP_RETURN_ON_ERROR(validate_pin(pin), TAG, "set_output invalid pin");
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "IOX not ready");
    ESP_RETURN_ON_ERROR(lock_take(pdMS_TO_TICKS(100)), TAG, "IOX mutex timeout");

    uint8_t before = s_output_shadow;
    uint8_t mask = pin_to_mask(pin);
    if (level) {
        s_output_shadow = static_cast<uint8_t>(s_output_shadow | mask);
    } else {
        s_output_shadow = static_cast<uint8_t>(s_output_shadow & ~mask);
    }

    esp_err_t err = esp_io_expander_set_level(s_expander, mask, level ? 1 : 0);
    if (err != ESP_OK) {
        s_output_shadow = before;
        lock_give();
        return err;
    }

    uint8_t after = s_output_shadow;
    lock_give();
    ESP_LOGI(TAG,
             "output_shadow before=0x%02X after=0x%02X pin=%u level=%d",
             static_cast<unsigned>(before),
             static_cast<unsigned>(after),
             static_cast<unsigned>(pin),
             level ? 1 : 0);
    return ESP_OK;
}

esp_err_t bsp_io_expander_get_output_level(uint8_t pin, bool *level)
{
    ESP_RETURN_ON_ERROR(validate_pin(pin), TAG, "get_output invalid pin");
    ESP_RETURN_ON_FALSE(level != nullptr, ESP_ERR_INVALID_ARG, TAG, "level is null");
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "IOX not ready");
    ESP_RETURN_ON_ERROR(lock_take(pdMS_TO_TICKS(100)), TAG, "IOX mutex timeout");
    *level = (s_output_shadow & pin_to_mask(pin)) != 0;
    lock_give();
    return ESP_OK;
}

uint8_t bsp_io_expander_get_output_shadow(void)
{
    if (!s_inited || s_mutex == nullptr) {
        return s_output_shadow;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint8_t value = s_output_shadow;
        xSemaphoreGive(s_mutex);
        return value;
    }
    return s_output_shadow;
}

uint8_t bsp_io_expander_get_direction_shadow(void)
{
    if (!s_inited || s_mutex == nullptr) {
        return s_direction_shadow;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint8_t value = s_direction_shadow;
        xSemaphoreGive(s_mutex);
        return value;
    }
    return s_direction_shadow;
}

