idf_component_register(
    SRCS
        "ui.cpp"
        "ui_router.cpp"
        "pages/ui_page_layout.cpp"
        "pages/ui_page_home.cpp"
        "pages/ui_page_ai_voice.cpp"
        "pages/ui_page_settings.cpp"
        "pages/ui_page_debug.cpp"
        "pages/ui_page_about.cpp"
        "pages/ui_page_placeholder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_shell bsp_board esp_timer freertos log lvgl service_audio service_network service_time
)
