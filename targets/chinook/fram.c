/*
 * SPI F-RAM interface
 * Copyright (c) 2024 Lone Dynamics Corporation. All rights reserved.
 *
 */

#include <stdio.h>
#include <stdint.h>

#include "ch32fun/ch32fun/ch32fun.h"

#define CH32V003_SPI_IMPLEMENTATION
#define CH32V003_SPI_NSS_SOFTWARE_ANY_MANUAL
#define CH32V003_SPI_SPEED_HZ 1000000
#define CH32V003_SPI_DIRECTION_2LINE_TXRX
#define CH32V003_SPI_CLK_MODE_POL0_PHA0

#include "ch32fun/extralibs/ch32v003_SPI.h"
#include "ls10.h"

void fram_init(void);
uint8_t fram_read(int addr);
void fram_write(int addr, unsigned char d);
void fram_write_enable(void);

void fram_init(void) {

	// set up SPI master interface for FRAM
   SPI_init();
   SPI_begin_8();

   (SPI_SS_PORT)->CFGLR &= ~(0xf<<(4*SPI_SS));
   (SPI_SS_PORT)->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*SPI_SS);

	// set SS high
	(SPI_SS_PORT)->BSHR = (1<<SPI_SS);

}

uint8_t fram_read(int addr) {

	(SPI_SS_PORT)->BSHR = (1<<(16+SPI_SS));

	SPI_transfer_8(0x03);   // READ

	SPI_transfer_8((addr >> 8) & 0xff);
	SPI_transfer_8(addr & 0xff);

	uint8_t d = SPI_transfer_8(0x00);

	(SPI_SS_PORT)->BSHR = (1<<SPI_SS);

	return(d);

}

void fram_write_enable(void) {

	(SPI_SS_PORT)->BSHR = (1<<(16+SPI_SS));
   SPI_transfer_8(0x06);   // WREN
	(SPI_SS_PORT)->BSHR = (1<<SPI_SS);

}

void fram_write(int addr, unsigned char d) {

   fram_write_enable(); // auto-disabled after each write

	(SPI_SS_PORT)->BSHR = (1<<(16+SPI_SS));

   SPI_transfer_8(0x02);   // WRITE
   SPI_transfer_8((addr >> 8) & 0xff);
   SPI_transfer_8(addr & 0xff);
   SPI_transfer_8(d);

	(SPI_SS_PORT)->BSHR = (1<<SPI_SS);

}
