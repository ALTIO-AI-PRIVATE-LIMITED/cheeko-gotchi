idf_component_register(
    SRCS "cheeko_self_test.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer esp_lcd_jd9853 esp_lvgl_port lvgl nvs_flash
)
