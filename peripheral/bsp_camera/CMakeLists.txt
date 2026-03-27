idf_component_register(
    SRCS "bsp_camera.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_cam esp_driver_isp esp_mm
)
