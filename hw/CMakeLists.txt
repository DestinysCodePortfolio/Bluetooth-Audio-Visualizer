set(TARGET_NAME top)
set(DEVICE 25k)
set(PACKAGE CABGA256)
set(FREQ 65)

add_custom_target(hw ALL 
    DEPENDS
        ${TARGET_NAME}.bit
)

add_custom_command(OUTPUT ${TARGET_NAME}.json
    COMMAND yosys -ql ${TARGET_NAME}.log -p 'synth_ecp5 -json ${TARGET_NAME}.json' ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.v

    DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.v
    ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.lpf
    ${CMAKE_CURRENT_SOURCE_DIR}/async_fifo.v
    ${CMAKE_CURRENT_SOURCE_DIR}/lcd_fb.v
    ${CMAKE_CURRENT_SOURCE_DIR}/lcd_timing.v
    ${CMAKE_CURRENT_SOURCE_DIR}/pll_clocks.v
    ${CMAKE_CURRENT_SOURCE_DIR}/sdram_controller.v
    ${CMAKE_CURRENT_SOURCE_DIR}/spi_peripheral.v
    ${CMAKE_CURRENT_SOURCE_DIR}/display_controller.v

    COMMENT 
        "Synthesizing ${TARGET_NAME}"
)

add_custom_command(OUTPUT ${TARGET_NAME}_out.config
    COMMAND nextpnr-ecp5 --${DEVICE} --package ${PACKAGE} --freq ${FREQ} --speed 6 --textcfg ${TARGET_NAME}_out.config --json ${TARGET_NAME}.json --lpf ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.lpf 

    DEPENDS
        ${TARGET_NAME}.json

    COMMENT
        "Performing Place & Route"
)

add_custom_command(OUTPUT ${TARGET_NAME}.bit
    COMMAND ecppack --svf ${TARGET_NAME}.svf ${TARGET_NAME}_out.config ${TARGET_NAME}.bit 

    DEPENDS
        ${TARGET_NAME}_out.config

    COMMENT 
        "Packing binary"
)

