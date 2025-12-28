cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(werkzeug)

add_executable(werkzeug
        werkzeug.c
        ../../../basic.c
        )

target_sources(werkzeug PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/werkzeug.c
        )

pico_sdk_init()

target_link_libraries(werkzeug PRIVATE pico_stdlib hardware_resets hardware_irq hardware_adc hardware_i2c)

# enable usb output, disable uart output
pico_enable_stdio_usb(werkzeug 1)
pico_enable_stdio_uart(werkzeug 0)

pico_add_extra_outputs(werkzeug)
