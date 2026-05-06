idf_component_register(
    SRCS "service_power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver espressif__esp_io_expander_tca9554 esp_wifi freertos log
)
