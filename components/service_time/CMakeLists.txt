idf_component_register(
    SRCS "service_time.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_wifi log lwip service_network
)
