idf_component_register(
    SRCS "service_power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_io_expander driver esp_wifi freertos log
)
