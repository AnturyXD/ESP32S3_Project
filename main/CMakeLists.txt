idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        app_config
        app_shell
        bsp_board
        esp_psram
        service_ai
        service_audio
        service_log
        service_network
        service_time
        ui
)
