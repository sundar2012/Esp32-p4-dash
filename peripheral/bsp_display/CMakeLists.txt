idf_component_register(
    SRCS "bsp_display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_lcd_mipi_dsi bsp_i2c bsp_extra lvgl
)
