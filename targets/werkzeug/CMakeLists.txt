cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(werkzeug C CXX ASM)

add_executable(werkzeug
        ${CMAKE_CURRENT_LIST_DIR}/werkzeug.c
        ${CMAKE_CURRENT_LIST_DIR}/usb.c
        ${CMAKE_CURRENT_LIST_DIR}/../../basic.c
        ${CMAKE_CURRENT_LIST_DIR}/../../fs/fs.c
        ${CMAKE_CURRENT_LIST_DIR}/swio.c
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/sechs/bridge.c
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/sechs/ch32prog.c
        )

pico_sdk_init()

pico_generate_pio_header(werkzeug ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)

# 24 pins (PIN extension); files in fs.c's NOR flash mode, 4KB blocks
target_compile_definitions(werkzeug PRIVATE
        HW_PINS=24 FS_NOR FS_MAX_BLOCKS=512 HW_FILES_FS)

# tusb_config.h: two USB serial ports (usb.c), not the SDK's USB stdio
target_include_directories(werkzeug PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(werkzeug PRIVATE pico_stdlib pico_unique_id
        hardware_flash hardware_sync hardware_adc hardware_pio hardware_uart
        tinyusb_device)

pico_enable_stdio_usb(werkzeug 0)
pico_enable_stdio_uart(werkzeug 0)

pico_add_extra_outputs(werkzeug)
