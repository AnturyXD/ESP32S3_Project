idf_component_register(
    SRCS "service_cloud.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_config esp_http_client esp_timer freertos log service_audio service_network service_power service_storage service_time
)
