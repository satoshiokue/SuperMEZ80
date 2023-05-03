PJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
XC8 := /Applications/microchip/xc8/v2.40/bin/xc8
FATFS_DIR := $(PJ_DIR)/../FatFs
DISKIO_DIR := $(PJ_DIR)/diskio
SRC_DIR := $(PJ_DIR)/src
BUILD_DIR := $(PJ_DIR)/build
CPM2_DIR := $(PJ_DIR)/cpm2
PROGPORT := /dev/tty.usbmodem1444301
CONSPORT := /dev/cu.usbserial-144440
PIC := 18F47Q43

#PP3_DIR := $(PJ_DIR)/../a-p-prog/sw
PP3_OPTS := -c $(PROGPORT) -s 1700 -v 2 -r 30 -t $(PIC)

FATFS_SRCS := $(FATFS_DIR)/source/ff.c
DISK_SRCS := $(DISKIO_DIR)/SDCard.c $(DISKIO_DIR)/SPI0.c $(DISKIO_DIR)/SPI1.c \
    $(DISKIO_DIR)/mcp23s08.c \
    $(DISKIO_DIR)/diskio.c $(DISKIO_DIR)/utils.c
SRCS := $(SRC_DIR)/supermez80.c $(SRC_DIR)/disas.c $(SRC_DIR)/memory.c $(SRC_DIR)/monitor.c \
    $(SRC_DIR)/io.c

INCS :=-I$(SRC_DIR) -I$(DISKIO_DIR) -I$(FATFS_DIR)/source -I$(BUILD_DIR)

HDRS := $(SRC_DIR)/supermez80.h $(SRC_DIR)/picconfig.h \
        $(DISKIO_DIR)/SPI.c $(DISKIO_DIR)/SPI.h $(DISKIO_DIR)/SDCard.h $(DISKIO_DIR)/mcp23s08.h \
        $(SRC_DIR)/disas.h $(SRC_DIR)/disas_z80.h \
        $(BUILD_DIR)/ipl.inc $(BUILD_DIR)/nmimon.inc \
        $(BUILD_DIR)/rstmon.inc $(BUILD_DIR)/mmu_exercise.inc 

all: $(BUILD_DIR)/supermez80.hex $(CPM2_DIR)/drivea.dsk

$(BUILD_DIR)/supermez80.hex: $(SRCS) $(FATFS_SRCS) $(DISK_SRCS) $(HDRS) $(BUILD_DIR)
	cd $(BUILD_DIR); \
        $(XC8) --chip=$(PIC) $(INCS) $(SRCS) $(FATFS_SRCS) $(DISK_SRCS)

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

$(BUILD_DIR)/ipl.inc: $(SRC_DIR)/ipl.z80 $(BUILD_DIR)
	cd $(BUILD_DIR); \
        sjasmplus --lst=ipl.lst --raw=ipl.bin $(SRC_DIR)/ipl.z80 && \
        cat ipl.bin | xxd -i > ipl.inc

$(BUILD_DIR)/nmimon.inc: $(SRC_DIR)/nmimon.z80 $(BUILD_DIR)
	cd $(BUILD_DIR); \
        sjasmplus --lst=nmimon.lst --raw=nmimon.bin $(SRC_DIR)/nmimon.z80 && \
        cat nmimon.bin | xxd -i > nmimon.inc

$(BUILD_DIR)/rstmon.inc: $(SRC_DIR)/rstmon.z80 $(BUILD_DIR)
	cd $(BUILD_DIR); \
        sjasmplus --lst=rstmon.lst --raw=rstmon.bin $(SRC_DIR)/rstmon.z80 && \
        cat rstmon.bin | xxd -i > rstmon.inc

$(BUILD_DIR)/mmu_exercise.inc: $(SRC_DIR)/mmu_exercise.z80 $(BUILD_DIR)
	cd $(BUILD_DIR); \
        sjasmplus --lst=mmu_exercise.lst --raw=mmu_exercise.bin $(SRC_DIR)/mmu_exercise.z80 && \
        cat mmu_exercise.bin | xxd -i > mmu_exercise.inc

$(BUILD_DIR)/boot.bin: $(CPM2_DIR)/boot.asm $(BUILD_DIR)
	cd $(BUILD_DIR); \
        sjasmplus --raw=boot.bin $(SRC_DIR)/boot.asm

$(BUILD_DIR)/bios.bin: $(CPM2_DIR)/bios.asm $(BUILD_DIR)
	cd $(BUILD_DIR); \
	sjasmplus --raw=bios.bin $(SRC_DIR)/bios.asm

$(BUILD_DIR)/drivea.dsk: $(CPM2_DIR)/boot.bin $(CPM2_DIR)/bios.bin
	cd $(BUILD_DIR); \
	dd if=$(CPM2_DIR)/z80pack-cpm2-1.dsk of=drivea.dsk bs=128; \
	dd if=boot.bin of=drivea.dsk bs=128 seek=0  count=1 conv=notrunc; \
	dd if=bios.bin of=drivea.dsk bs=128 seek=45 count=6 conv=notrunc

upload: $(BUILD_DIR)/supermez80.hex
	if [ .$(PP3_DIR) != . ]; then \
            echo using $(PP3_DIR)/pp3; \
            cd $(PP3_DIR); \
            ./pp3 $(PP3_OPTS) $(BUILD_DIR)/supermez80.hex || \
            ./pp3 $(PP3_OPTS) $(BUILD_DIR)/supermez80.hex; \
        else \
            echo using `which pp3`; \
            pp3 $(PP3_OPTS) $(BUILD_DIR)/supermez80.hex; \
        fi

test::
	PORT=$(CONSPORT) test/test.sh

clean::
	rm -rf $(BUILD_DIR)
