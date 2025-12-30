all : flash

TARGET:=ls10

TARGET_MCU?=CH32V003
ADDITIONAL_C_FILES:=../../basic.c fram.c ../../fs/fs.c
include ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
