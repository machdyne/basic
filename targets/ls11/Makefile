all : flash

TARGET:=ls11

TARGET_MCU?=CH32V005

# files are the module filesystem (basic.h maps hw_f* to fs_*); board.h
# (this directory) describes the board to the shared module code. The
# CH32V005 has room for programs of 4KB, the PIN extension (pins 5-7 are
# E, F, G) and programming mode (BOOT, Sechs PROGRAM).
EXTRA_CFLAGS+=-DHW_FILES_FS -DHW_PINS=7 -DMAX_PROG=4096 -DHW_PROG_MODE -DSECHS_PROGRAM -I.
ADDITIONAL_C_FILES:=../../basic.c eeprom.c ../../fs/fs.c ../../sechs/sechs.c

# one ch32fun checkout for both modules: targets/ls10/ch32fun
include ../ls10/ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
