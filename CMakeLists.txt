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

# Workaround: GCC 14.2 false positive -Wstringop-overflow in ESP-DL's dl_tensor_base.cpp
# This is a known issue with esp-dl v3.3.0 + GCC 14.x inlining heuristics
idf_component_get_property(espdl espressif__esp-dl COMPONENT_LIB)
target_compile_options(${espdl} PRIVATE -Wno-stringop-overflow)
