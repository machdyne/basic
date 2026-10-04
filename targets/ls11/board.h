#ifndef _BOARD_H
#define _BOARD_H

// LS11A: CH32V005F6U6, I2C EEPROM (targets/ls1x/module.c, eeprom.c)

#define ZW_MODULE       "LS11A"

// Sechs pins: A and B are the I2C peripheral's alternative pins (remap 1);
// A (PD1) is also SWIO, used for programming in programming mode
#define ZW_GPIOA_PORT   GPIOD
#define ZW_GPIOA        1
#define ZW_GPIOB_PORT   GPIOD
#define ZW_GPIOB        0
#define ZW_GPIOC_PORT   GPIOD   // USART1 RX, ADC 6
#define ZW_GPIOC        6
#define ZW_GPIOD_PORT   GPIOD   // USART1 TX, ADC 5
#define ZW_GPIOD        5

// rear pins: BASIC pins 5-7 (the PIN extension), all analog;
// E/F are also USART2 TX/RX (remap 3; not used yet)
#define ZW_GPIOE_PORT   GPIOD   // ADC 3
#define ZW_GPIOE        2
#define ZW_GPIOF_PORT   GPIOD   // ADC 4
#define ZW_GPIOF        3
#define ZW_GPIOG_PORT   GPIOD   // ADC 7
#define ZW_GPIOG        4

#define ZW_LED_PORT     GPIOA
#define ZW_LED          1
#define ZW_LED_ACTIVE_LOW 1     // to be confirmed on hardware

// EEPROM: AT24C64 (8KB, 32-byte pages) at 0x50, bit-banged on PC4/PC5;
// WP on PC6 (the EEPROM pulls it down: writable when released)
#define EE_SDA_PORT     GPIOC
#define EE_SDA          4
#define EE_SCL_PORT     GPIOC
#define EE_SCL          5
#define EE_WP_PORT      GPIOC
#define EE_WP           6
#define EE_ADDR         0x50
#define EE_SIZE         8192    // AT24C32: 4096
#define EE_PAGE         32
#define ZW_STORAGE_SIZE EE_SIZE

#define ZW_I2C_REMAP    1       // the Sechs I2C slave on PD1/PD0
#define ZW_PROG_MODE    1       // programming mode: SWIO on A
#define ZW_ADC_12BIT    1       // 12-bit ADC (BASIC reads 0-1023)

// unused pins (PA2, PC0-PC3, PC7): inputs with pull-ups
// (4 configuration bits per pin; 0x8 = input with pull, BSHR = up).
// PA4 is bonded to PD7 (RESETN) inside the chip: never an output.
#define ZW_BOARD_INIT() do { \
    GPIOA->CFGLR = (GPIOA->CFGLR & ~0x00000F00u) | 0x00000800u; \
    GPIOA->BSHR = (1 << 2); \
    GPIOC->CFGLR = (GPIOC->CFGLR & ~0xF000FFFFu) | 0x80008888u; \
    GPIOC->BSHR = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 7); \
} while (0)

#endif
