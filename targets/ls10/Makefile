all : flash

TARGET:=ls10

TARGET_MCU?=CH32V003

# Options that reduce code size: the CH32V003 has 16KB of flash.
EXTRA_CFLAGS:=-msave-restore -fno-inline-small-functions -finline-limit=0 -fno-jump-tables -fno-caller-saves

# files are the module filesystem: basic.h maps hw_f* to fs_*; board.h
# (this directory) describes the board to the shared module code
EXTRA_CFLAGS+=-DHW_FILES_FS -I.
ADDITIONAL_C_FILES:=../../basic.c fram.c ../../fs/fs.c ../../sechs/sechs.c
include ch32fun/ch32fun/ch32fun.mk

# LS10 fills its 16KB flash almost exactly; older compilers make larger
# code (GCC 12: 212 bytes too many). README: "LS10 and LS11".
GCC_MAJOR:=$(shell $(PREFIX)-gcc -dumpversion 2>/dev/null | cut -d. -f1)
ifneq ($(GCC_MAJOR),)
ifeq ($(shell [ "$(GCC_MAJOR)" -lt 13 ] 2>/dev/null && echo old),old)
$(warning LS10 needs GCC 13 or newer to fit its 16KB flash; this is GCC $(GCC_MAJOR) ($(PREFIX)-gcc). See README.md, "LS10 and LS11")
endif
endif

flash : cv_flash
clean : cv_clean
