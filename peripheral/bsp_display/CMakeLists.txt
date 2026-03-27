idf_component_register(
    SRCS "bsp_display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd bsp_i2c bsp_extra lvgl esp_timer
)
