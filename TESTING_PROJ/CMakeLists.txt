cmake_minimum_required(VERSION 3.13)
 
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
 
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
set(PICO_BOARD pico2_w CACHE STRING "Board type")
 
# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
 
project(TESTING_PROJ C CXX ASM)
 
# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()
 
# ── LVGL ─────────────────────────────────────────────────────────────────────
# Clone lvgl into your project folder:
#   git submodule add https://github.com/lvgl/lvgl.git lvgl
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(lvgl)
 
# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(TESTING_PROJ
    main.cpp                  # merged BT + display entry point
    spi_display.cpp           # SPI display driver
    cs122_app.cpp             # CS122_App base class
    lvgl_oscilloscope.cpp     # oscilloscope widget
    audio_buffer.cpp
    fallback_audio.cpp
)
 
pico_set_program_name(TESTING_PROJ "TESTING_PROJ")
pico_set_program_version(TESTING_PROJ "0.1")
pico_enable_stdio_uart(TESTING_PROJ 0)
pico_enable_stdio_usb(TESTING_PROJ 1)
 
# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(TESTING_PROJ PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}          # btstack_config.h, lv_conf.h, all headers
    ${CMAKE_CURRENT_LIST_DIR}/lvgl/src # lvgl internals (pulled in via add_subdirectory too)
)
 
# ── Libraries ─────────────────────────────────────────────────────────────────
target_link_libraries(TESTING_PROJ
    pico_stdlib
    pico_multicore            
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_classic
    pico_btstack_ble
    pico_btstack_flash_bank
    pico_btstack_sbc_decoder
    lvgl                          
)
 
pico_add_extra_outputs(TESTING_PROJ)
 