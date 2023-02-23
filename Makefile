CWD := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
XC8 := /Applications/microchip/xc8/v2.40/bin/xc8
PP3_DIR := ../Arduino-PIC-Programmer
FATFS_DIR := ../FatFs
PORT := /dev/tty.usbmodem1444301
PIC := 18F47Q43

FATFS_SRCS := $(FATFS_DIR)/source/ff.c
DISK_SRCS := disk/SDCard.c disk/SPI.c disk/diskio.c disk/utils.c

INCS :=-I. -Idisk -I$(FATFS_DIR)/source

all: upload

emuz80_z80ram.hex: emuz80_z80ram.c $(FATFS_SRCS) $(DISK_SRCS) ipl.inc boot.inc
	$(XC8) --chip=$(PIC) $(INCS) emuz80_z80ram.c $(FATFS_SRCS) $(DISK_SRCS)

ipl.bin: ipl.z80
	sjasmplus --raw=ipl.bin ipl.z80

ipl.inc: ipl.bin
	cat ipl.bin | xxd -i > ipl.inc

boot.bin: boot.asm
	sjasmplus --raw=boot.bin boot.asm

boot.inc: boot.bin
	cat boot.bin | xxd -i > boot.inc

upload: emuz80_z80ram.hex
	cd $(PP3_DIR); \
        ./pp3 -c $(PORT) -s 1700 -v 2 -t $(PIC) $(CWD)/emuz80_z80ram.hex || \
        ./pp3 -c $(PORT) -s 1700 -v 2 -t $(PIC) $(CWD)/emuz80_z80ram.hex

clean::
	rm -f ipl.bin ipl.inc boot.bin boot.inc
	rm -f *.as
	rm -f *.p1 *.d *.hex *.pre *.lst *.cmf *.hxl *.sdb *.obj *.sym *.rlf *.elf
