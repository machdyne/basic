#ifndef _BOARD_H
#define _BOARD_H

// LS10A: CH32V003F4U6, 8KB SPI F-RAM (targets/ls1x/module.c, fram.c)

#define ZW_MODULE       "LS10A"

#define ZW_GPIOA_PORT   GPIOC
#define ZW_GPIOA        2
#define ZW_GPIOB_PORT   GPIOC
#define ZW_GPIOB        1
#define ZW_GPIOC_PORT   GPIOD
#define ZW_GPIOC        6
#define ZW_GPIOD_PORT   GPIOD
#define ZW_GPIOD        5

#define ZW_GPIOE_PORT   GPIOC
#define ZW_GPIOE        0
#define ZW_GPIOF_PORT   GPIOC
#define ZW_GPIOF        3  
#define ZW_GPIOG_PORT   GPIOC
#define ZW_GPIOG        4
#define ZW_LED_PORT     GPIOD
#define ZW_LED          4
#define ZW_LED_ACTIVE_LOW 1     // (from the schematic)

#define SPI_SS_PORT     GPIOD
#define SPI_SS          3

#define FRAM_SIZE       8192    // bytes of F-RAM
#define ZW_STORAGE_SIZE FRAM_SIZE

#endif
