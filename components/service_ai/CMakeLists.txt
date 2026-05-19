idf_component_register(
    SRCS "service_ai.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_config esp_timer esp_websocket_client freertos log service_audio service_cloud service_network
)
