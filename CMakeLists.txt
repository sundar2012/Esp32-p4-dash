# CrowPanel ESP32-P4 Face Recognition Dashboard
# Top-level CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Add peripheral BSP components to the component search path
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/bsp_display
    ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/bsp_i2c
    ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/bsp_extra
    ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/bsp_camera
    ${CMAKE_CURRENT_SOURCE_DIR}/peripheral/bsp_uart
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(crowpanel-face-dashboard)
