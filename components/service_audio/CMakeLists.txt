idf_component_register(
    SRCS "service_audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_config bsp_io_expander codec_board freertos log
)
