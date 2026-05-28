cmake_minimum_required(VERSION 3.13)

set(CONFIG_LV_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    ON CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lv_conf.h CACHE STRING "" FORCE)

add_subdirectory(lvgl)
add_definitions(-DBUILD_LVGL=1)

add_executable(cs122_demo
    src/main.cpp
    src/spi_display.cpp
    src/cs122_app.cpp
    src/lvgl_oscilloscope.cpp
    src/lvgl_touch.cpp
    src/lvgl_clock.cpp
    src/lvgl_pie_chart.cpp
    src/lvgl_compass.cpp
    src/lvgl_arc_label.cpp
    src/lvgl_heart_rate.cpp
    src/lvgl_demo_widgets.cpp
)

target_include_directories(cs122_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/lvgl/demos
)

target_link_libraries(cs122_demo
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_adc
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_flash_bank
    pico_btstack_sbc_decoder
    lvgl::lvgl
    hardware_pwm

)

pico_enable_stdio_uart(cs122_demo 0)
pico_enable_stdio_usb(cs122_demo  1)
pico_add_extra_outputs(cs122_demo)