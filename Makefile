PROGPORT := /dev/tty.usbmodem1444301
CONSPORT := /dev/cu.usbserial-144440
XC8 := /Applications/microchip/xc8/v2.40/bin/xc8
PIC := 18F47Q43
#PP3_DIR := $(PJ_DIR)/../a-p-prog/sw
PP3_OPTS := -c $(PROGPORT) -s 1700 -v 2 -r 30 -t $(PIC)

PJ_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
FATFS_DIR := $(PJ_DIR)/../FatFs
DISKIO_DIR := $(PJ_DIR)/diskio
SRC_DIR := $(PJ_DIR)/src
BUILD_DIR := $(PJ_DIR)/build
CPM2_DIR := $(PJ_DIR)/cpm2

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

$(BUILD_DIR)/supermez80.hex: $(SRCS) $(FATFS_SRCS) $(DISK_SRCS) $(HDRS)
	cd $(BUILD_DIR) && \
        $(XC8) --chip=$(PIC) $(INCS) $(SRCS) $(FATFS_SRCS) $(DISK_SRCS)

$(BUILD_DIR)/%.inc: $(SRC_DIR)/%.z80
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
        sjasmplus --lst=%*.lst --raw=%*.bin $< && \
        cat %*.bin | xxd -i > $@

$(BUILD_DIR)/%.bin: $(CPM2_DIR)/%.asm $(BUILD_DIR)
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
        sjasmplus --raw=%*.bin %<

$(BUILD_DIR)/drivea.dsk: $(CPM2_DIR)/boot.bin $(CPM2_DIR)/bios.bin
	cd $(BUILD_DIR); \
	dd if=$(CPM2_DIR)/z80pack-cpm2-1.dsk of=drivea.dsk bs=128; \
	dd if=boot.bin of=drivea.dsk bs=128 seek=0  count=1 conv=notrunc; \
	dd if=bios.bin of=drivea.dsk bs=128 seek=45 count=6 conv=notrunc

upload: $(BUILD_DIR)/supermez80.hex
	if [ .$(PP3_DIR) != . ]; then \
            echo using $(PP3_DIR)/pp3; \
            cd $(PP3_DIR); \
            ./pp3 $(PP3_OPTS) $(BUILD_DIR)/supermez80.hex; \
        else \
            echo using `which pp3`; \
            pp3 $(PP3_OPTS) $(BUILD_DIR)/supermez80.hex; \
        fi

test::
	PORT=$(CONSPORT) test/test.sh

clean::
	rm -rf $(BUILD_DIR)
