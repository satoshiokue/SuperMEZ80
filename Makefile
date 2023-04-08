PJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
XC8 := /Applications/microchip/xc8/v2.40/bin/xc8
FATFS_DIR := $(PJ_DIR)/../FatFs
DISKIO_DIR := $(PJ_DIR)/diskio
SRC_DIR := $(PJ_DIR)
CPM2_DIR := $(PJ_DIR)/cpm2
PORT := /dev/tty.usbmodem1444301
PIC := 18F47Q43

#PP3_DIR := $(PJ_DIR)/../a-p-prog/sw
PP3_OPTS := -c $(PORT) -s 1700 -v 2 -r 30 -t $(PIC)

FATFS_SRCS := $(FATFS_DIR)/source/ff.c
DISK_SRCS := $(DISKIO_DIR)/SDCard.c $(DISKIO_DIR)/SPI0.c $(DISKIO_DIR)/SPI1.c \
    $(DISKIO_DIR)/mcp23s08.c \
    $(DISKIO_DIR)/diskio.c $(DISKIO_DIR)/utils.c
SRCS := $(SRC_DIR)/emuz80_z80ram.c $(SRC_DIR)/disas.c

INCS :=-I$(SRC_DIR) -I$(DISKIO_DIR) -I$(FATFS_DIR)/source

HDRS := picconfig.h \
        $(DISKIO_DIR)/SPI.c $(DISKIO_DIR)/SPI.h $(DISKIO_DIR)/SDCard.h $(DISKIO_DIR)/mcp23s08.h \
        $(SRC_DIR)/disas.h $(SRC_DIR)/disas_z80.h 

all: emuz80_z80ram.hex $(CPM2_DIR)/drivea.dsk

emuz80_z80ram.hex: $(SRCS) $(FATFS_SRCS) $(DISK_SRCS) $(SRC_DIR)/ipl.inc $(HDRS)
	cd $(SRC_DIR); \
        $(XC8) --chip=$(PIC) $(INCS) $(SRCS) $(FATFS_SRCS) $(DISK_SRCS)

$(SRC_DIR)/ipl.inc: $(SRC_DIR)/ipl.z80
	cd $(SRC_DIR); \
        sjasmplus --raw=ipl.bin ipl.z80; \
        cat ipl.bin | xxd -i > ipl.inc

$(CPM2_DIR)/boot.bin: $(CPM2_DIR)/boot.asm
	cd $(CPM2_DIR); \
        sjasmplus --raw=boot.bin boot.asm

$(CPM2_DIR)/bios.bin: $(CPM2_DIR)/bios.asm
	cd $(CPM2_DIR); \
	sjasmplus --raw=bios.bin bios.asm

$(CPM2_DIR)/drivea.dsk: $(CPM2_DIR)/boot.bin $(CPM2_DIR)/bios.bin
	cd $(CPM2_DIR); \
	dd if=z80pack-cpm2-1.dsk of=drivea.dsk bs=128; \
	dd if=boot.bin of=drivea.dsk bs=128 seek=0  count=1 conv=notrunc; \
	dd if=bios.bin of=drivea.dsk bs=128 seek=45 count=6 conv=notrunc

upload: emuz80_z80ram.hex
	if [ .$(PP3_DIR) != . ]; then \
            echo using $(PP3_DIR)/pp3; \
            cd $(PP3_DIR); \
            ./pp3 $(PP3_OPTS) $(PJ_DIR)/emuz80_z80ram.hex || \
            ./pp3 $(PP3_OPTS) $(PJ_DIR)/emuz80_z80ram.hex; \
        else \
            echo using `which pp3`; \
            pp3 $(PP3_OPTS) $(PJ_DIR)/emuz80_z80ram.hex; \
        fi

clean::
	cd $(SRC_DIR); rm -f ipl.bin ipl.inc
	cd $(CPM2_DIR); rm -f boot.bin bios.bin
	cd $(SRC_DIR); rm -f *.as *.p1 *.d *.pre *.lst *.cmf *.hxl *.sdb *.obj *.sym *.rlf \
            *.elf
