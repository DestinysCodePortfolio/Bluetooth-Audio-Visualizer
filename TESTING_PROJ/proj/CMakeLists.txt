cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(proj C CXX ASM)

pico_sdk_init()

set(LVGL_DIR ${CMAKE_SOURCE_DIR}/icesugar-pro-framebuffer/sw/lvgl)
set(SHARED_INCLUDE ${CMAKE_SOURCE_DIR}/icesugar-pro-framebuffer/sw/include)

add_executable(proj
    main.cpp
    audio_buffer.cpp
    audio_fallback.cpp
    spi_display.cpp
    cs122_app.cpp
)

pico_set_program_name(proj "BTVisualizer")
pico_set_program_version(proj "0.1")

pico_enable_stdio_uart(proj 0)
pico_enable_stdio_usb(proj 1)

target_include_directories(proj PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}   # your local headers take priority
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    # only pull specific headers from shared include, not audio_buffer.h
    # lvgl_oscilloscope.h etc will be found via LVGL_DIR
)

target_link_libraries(proj
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_flash_bank
    pico_btstack_sbc_decoder
    hardware_spi
)

pico_add_extra_outputs(proj)
