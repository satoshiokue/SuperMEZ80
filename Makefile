PROGPORT := /dev/tty.usbmodem1444301
CONSPORT := /dev/cu.usbserial-144440
BOARD := SUPERMEZ80_SPI
#BOARD := SUPERMEZ80_CPM
DEFS += -DSUPERMEZ80_CPM_MMU
#DEFS += -DCPM_MMU_EXERCISE
#DEFS += -DNO_MEMORY_CHECK
DEFS += -DNO_MON_BREAKPOINT
DEFS += -DNO_MON_STEP

PIC := 18F47Q43
XC8 := /Applications/microchip/xc8/v2.40/bin/xc8
XC8_OPTS := --chip=$(PIC) --std=c99
#XC8 := /Applications/microchip/xc8/v2.40/bin/xc8-cc
#XC8_OPTS := -mcpu=$(PIC) -std=c99
#PP3_DIR := $(PJ_DIR)/../a-p-prog/sw
PP3_OPTS := -c $(PROGPORT) -s 1700 -v 2 -r 30 -t $(PIC)
TEST_REPEAT := 10

PJ_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
FATFS_DIR := $(PJ_DIR)/../FatFs
DRIVERS_DIR := $(PJ_DIR)/drivers
SRC_DIR := $(PJ_DIR)/src
BUILD_DIR := $(PJ_DIR)/build
CPM2_DIR := $(PJ_DIR)/cpm2

FATFS_SRCS := $(FATFS_DIR)/source/ff.c
DISK_SRCS := \
    $(DRIVERS_DIR)/diskio.c $(DRIVERS_DIR)/utils.c
SRCS := $(SRC_DIR)/supermez80.c $(SRC_DIR)/disas.c $(SRC_DIR)/disas_z80.c $(SRC_DIR)/memory.c \
    $(SRC_DIR)/monitor.c $(SRC_DIR)/io.c $(SRC_DIR)/board.c

ifeq ($(BOARD),SUPERMEZ80_SPI)
SRCS += $(SRC_DIR)/boards/supermez80_spi.c
SRCS += $(SRC_DIR)/boards/supermez80_spi_ioexp.c
endif
ifeq ($(BOARD),SUPERMEZ80_CPM)
SRCS += $(SRC_DIR)/boards/supermez80_cpm.c
endif

INCS :=-I$(SRC_DIR) -I$(DRIVERS_DIR) -I$(FATFS_DIR)/source -I$(BUILD_DIR)

HDRS := $(SRC_DIR)/supermez80.h $(SRC_DIR)/picconfig.h \
        $(DRIVERS_DIR)/SPI.c $(DRIVERS_DIR)/SPI.h $(DRIVERS_DIR)/SDCard.h \
        $(DRIVERS_DIR)/mcp23s08.h \
        $(SRC_DIR)/disas.h $(SRC_DIR)/disas_z80.h \
        $(BUILD_DIR)/ipl.inc $(BUILD_DIR)/trampoline.inc $(BUILD_DIR)/mmu_exercise.inc \
        $(BUILD_DIR)/dma_helper.inc \
        $(BUILD_DIR)/dummy.inc \
        $(DRIVERS_DIR)/pic18f47q43_spi.c \
        $(DRIVERS_DIR)/SDCard.c \
        $(DRIVERS_DIR)/mcp23s08.c \
        $(SRC_DIR)/boards/emuz80_common.c

all: $(BUILD_DIR)/supermez80.hex $(BUILD_DIR)/drivea.dsk

$(BUILD_DIR)/supermez80.hex: $(SRCS) $(FATFS_SRCS) $(DISK_SRCS) $(HDRS)
	cd $(BUILD_DIR) && \
        $(XC8) $(XC8_OPTS) $(DEFS) $(INCS) $(SRCS) $(FATFS_SRCS) $(DISK_SRCS)

$(BUILD_DIR)/%.inc: $(SRC_DIR)/%.z80
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
        sjasmplus --lst=$*.lst --raw=$*.bin $< && \
        cat $*.bin | xxd -i > $@

$(BUILD_DIR)/%.bin: $(CPM2_DIR)/%.asm $(BUILD_DIR)
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
        sjasmplus --raw=$*.bin $<

$(BUILD_DIR)/drivea.dsk: $(BUILD_DIR)/boot.bin $(BUILD_DIR)/bios.bin
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

test_repeat::
	for i in $$(seq $(TEST_REPEAT)); do \
          PORT=$(CONSPORT) test/test.sh || exit 1; \
        done

test_time::
	PORT=$(CONSPORT) test/measure_time.sh

clean::
	rm -rf $(BUILD_DIR)
