idf_component_register(
    SRCS "bsp_io_expander.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver espressif__esp_io_expander_tca9554 freertos log
)

