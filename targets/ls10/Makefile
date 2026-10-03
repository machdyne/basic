all : flash

TARGET:=ls10

TARGET_MCU?=CH32V003

# Options that reduce code size: the CH32V003 has 16KB of flash.
EXTRA_CFLAGS:=-msave-restore -fno-inline-small-functions -finline-limit=0 -fno-jump-tables -fno-caller-saves

# files are the module filesystem: basic.h maps hw_f* to fs_*
EXTRA_CFLAGS+=-DHW_FILES_FS
ADDITIONAL_C_FILES:=../../basic.c fram.c ../../fs/fs.c ../../sechs/sechs.c
include ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
