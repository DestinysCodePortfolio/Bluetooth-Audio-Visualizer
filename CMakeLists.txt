# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
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
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == Pico VS Code Extension setup ==
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
# ================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(bt_visualizer C CXX ASM)

pico_sdk_init()

set(LVGL_DIR ${CMAKE_SOURCE_DIR}/lib/lvgl)

# Build LVGL as a static library
file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")

add_library(lvgl STATIC ${LVGL_SOURCES})

target_include_directories(lvgl PUBLIC
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    ${CMAKE_SOURCE_DIR}/src
)

# FatFS SD card library
add_subdirectory(lib/pico_fatfs)

# Main executable
add_executable(bt_visualizer
    src/main.cpp
    src/audio_buffer.cpp
    src/audio_fallback.cpp
    src/sd_wav_fallback.cpp
    src/spi_display.cpp
    src/cs122_app.cpp
    src/lvgl_oscilloscope.cpp
)

pico_set_program_name(bt_visualizer "BTVisualizer")
pico_set_program_version(bt_visualizer "0.1")

pico_enable_stdio_uart(bt_visualizer 0)
pico_enable_stdio_usb(bt_visualizer 1)

target_include_directories(bt_visualizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LVGL_DIR}
    ${LVGL_DIR}/src

    # FatFS headers
    ${CMAKE_SOURCE_DIR}/lib/pico_fatfs
    ${CMAKE_SOURCE_DIR}/lib/pico_fatfs/fatfs
    ${CMAKE_SOURCE_DIR}/lib/pico_fatfs/fatfs/source
)

target_link_libraries(bt_visualizer
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_flash_bank
    pico_btstack_sbc_decoder
    hardware_spi
    lvgl
    pico_fatfs
    m
)

pico_add_extra_outputs(bt_visualizer)