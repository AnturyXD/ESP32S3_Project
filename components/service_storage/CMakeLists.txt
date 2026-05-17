idf_component_register(
    SRCS "service_storage.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer fatfs freertos log sdmmc
)

