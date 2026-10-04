/*
 * SPI F-RAM interface
 * Copyright (c) 2024 Lone Dynamics Corporation. All rights reserved.
 *
 * Also the media driver for the filesystem (fs_media_read/fs_media_prog),
 * with burst transfers: one command and address, then any number of bytes.
 */

#include <stdio.h>
#include <stdint.h>

#include "ch32fun/ch32fun/ch32fun.h"

#define CH32V003_SPI_IMPLEMENTATION
#define CH32V003_SPI_NSS_SOFTWARE_ANY_MANUAL
// Four times the original 1 MHz setting (ch32fun derives the prescaler
// from this value). F-RAM parts are rated far higher; to be confirmed on
// hardware before going further.
#define CH32V003_SPI_SPEED_HZ 4000000
#define CH32V003_SPI_DIRECTION_2LINE_TXRX
#define CH32V003_SPI_CLK_MODE_POL0_PHA0

#include "ch32fun/extralibs/ch32v003_SPI.h"
#include "board.h"
#include "../../fs/fs.h"

#define FRAM_READ   0x03
#define FRAM_WRITE  0x02
#define FRAM_WREN   0x06

void storage_init(void);

static void cs_low(void) {
    (SPI_SS_PORT)->BSHR = (1 << (16 + SPI_SS));
}

static void cs_high(void) {
    (SPI_SS_PORT)->BSHR = (1 << SPI_SS);
}

static void command(uint8_t cmd, uint32_t addr) {
    SPI_transfer_8(cmd);
    SPI_transfer_8((addr >> 8) & 0xff);
    SPI_transfer_8(addr & 0xff);
}

void storage_init(void) {

    // set up SPI master interface for FRAM
    SPI_init();
    SPI_begin_8();

    (SPI_SS_PORT)->CFGLR &= ~(0xf << (4 * SPI_SS));
    (SPI_SS_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP) << (4 * SPI_SS);

    cs_high();

}

int fs_media_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    if (addr + len > FRAM_SIZE) return -1;
    cs_low();
    command(FRAM_READ, addr);
    while (len--) *buf++ = SPI_transfer_8(0x00);
    cs_high();
    return 0;
}

int fs_media_prog(uint32_t addr, const uint8_t *buf, uint16_t len) {
    if (addr + len > FRAM_SIZE) return -1;

    // write enable; cleared again by the F-RAM after each write
    cs_low();
    SPI_transfer_8(FRAM_WREN);
    cs_high();

    cs_low();
    command(FRAM_WRITE, addr);
    while (len--) SPI_transfer_8(*buf++);
    cs_high();
    return 0;
}
