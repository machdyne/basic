cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(blaustahl)

add_executable(blaustahl
        blaustahl.c
        fram.c
        ../../../fs/fs.c
        ../../../basic.c
        )

target_sources(blaustahl PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/blaustahl.c
        )


target_compile_definitions(blaustahl PUBLIC
   PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
   )

pico_sdk_init()

target_link_libraries(blaustahl PRIVATE pico_stdlib pico_stdio_usb hardware_resets hardware_irq hardware_timer hardware_adc hardware_spi)

# enable usb output, disable uart output
pico_enable_stdio_usb(blaustahl 1)
pico_enable_stdio_uart(blaustahl 0)

pico_add_extra_outputs(blaustahl)
