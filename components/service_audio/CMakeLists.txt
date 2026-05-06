idf_component_register(
    SRCS "service_audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_config codec_board driver espressif__esp_io_expander_tca9554 freertos log
)
