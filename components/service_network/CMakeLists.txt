idf_component_register(
    SRCS "service_network.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_config esp_event esp_netif esp_wifi log lwip nvs_flash
)
