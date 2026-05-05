idf_component_register(
    SRCS "bsp_board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd espressif__esp_lcd_axs15231b freertos log
)
