/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BLAUSTAHL_H_
#define BLAUSTAHL_H_

#include <stdint.h>

#define FRAM_SIZE 8192		// 8KB (Blaustahl)
//#define FRAM_SIZE 262144	// 256KB (Kaltstahl)

#define FRAM_METADATA 512	// reserved for encryption
#define FRAM_AVAILABLE (FRAM_SIZE - FRAM_METADATA)

#if FRAM_SIZE >= 262144
 #define FRAM_BIG
#endif

// PINS

#define BS_LED 			9

#define BS_FRAM_SPI		spi1
#define BS_FRAM_MOSI		11
#define BS_FRAM_MISO		12
#define BS_FRAM_SS		13
#define BS_FRAM_SCK		14

#endif
