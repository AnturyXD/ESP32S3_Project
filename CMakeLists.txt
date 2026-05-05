# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Keep V0.1 focused on the new modular skeleton components.
set(EXCLUDE_COMPONENTS
    SensorLib
    adc_bsp
    audio_bsp
    ble_scan_bsp
    button_bsp
    codec_board
    esp_wifi_bsp
    i2c_bsp
    i2c_equipment
    lcd_bl_pwm_bsp
    sdcard_bsp
    ui_bsp
    user_app
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(11_FactoryProgram)
