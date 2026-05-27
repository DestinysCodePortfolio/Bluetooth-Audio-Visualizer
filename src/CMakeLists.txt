add_executable(bt_visualizer
    main.c
    a2dp_sink.c
)

target_link_libraries(bt_visualizer
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    pico_btstack_sbc_decoder  
    hardware_pwm
    hardware_dma
    hardware_spi
)

# BTstack config header
target_include_directories(bt_visualizer PRIVATE ${CMAKE_CURRENT_LIST_DIR})

pico_btstack_make_gatt_header(bt_visualizer PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bt_visualizer.gatt")

pico_add_extra_outputs(bt_visualizer)