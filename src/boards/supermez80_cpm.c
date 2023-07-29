/*
 * Copyright (c) 2023 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define BOARD_DEPENDENT_SOURCE

#include <supermez80.h>
#include <stdio.h>
#include <SDCard.h>
#include <mcp23s08.h>
#include <picregister.h>

#define SPI_PREFIX      SPI_SD
#define SPI_HW_INST     SPI2

#define Z80_IOREQ       A0
#define Z80_MEMRQ       A1
#define Z80_BUSRQ       A2
#define Z80_CLK         A3
#define Z80_WAIT        A4
#define Z80_RD          A5
// RA6 is used as UART TXD
// RA7 is used as UART RXD

#define Z80_ADDR_L      B
#define Z80_DATA        C

#define SPI_SD_SS       D0
#define SPI_SD_PICO     D1
#define SPI_SD_CLK      D2
#define SPI_SD_POCI     D3
#define BANK0           D4
#define BANK1           D5
#define Z80_A14         D6
#define Z80_A15         D7

#define Z80_WR          E0
#define Z80_RESET       E1
#define Z80_NMI         E2

#define SRAM_CE         Z80_MEMRQ
#define SRAM_OE         Z80_RD
#define SRAM_WE         Z80_WR

#define HIGH_ADDR_MASK  0xffffff00
#define LOW_ADDR_MASK   0x000000ff

#include "emuz80_common.c"

static void supermez80_cpm_sys_init()
{
    emuz80_common_sys_init();

    // Address bus
    LAT(Z80_A14) = 0;
    LAT(Z80_A15) = 0;
    TRIS(Z80_A14) = 0;
    TRIS(Z80_A15) = 0;

    // /CE
    LAT(SRAM_CE) = 1;
    TRIS(SRAM_CE) = 0;          // Set as output

    // /WE output pin
    LAT(SRAM_WE) = 1;
    TRIS(SRAM_WE) = 0;          // Set as output
    PPS(SRAM_WE) = 0x00;        // unbind with CLC

    // /OE output pin
    LAT(SRAM_OE) = 1;
    TRIS(SRAM_OE) = 0;          // Set as output
    PPS(SRAM_OE) = 0x00;        // unbind with CLC

    LAT(Z80_A14) = 0;
    TRIS(Z80_A14) = 0;

    LAT(Z80_A15) = 0;
    TRIS(Z80_A15) = 0;

    LAT(BANK0) = 0;
    TRIS(BANK0) = 0;

    LAT(BANK1) = 0;
    TRIS(BANK1) = 0;

    emuz80_common_wait_for_programmer();

    // Initialize memory bank
    set_bank_pins(0x00000);

    //
    // Initialize SD Card
    //
    static int retry;
    for (retry = 0; 1; retry++) {
        if (20 <= retry) {
            printf("No SD Card?\n\r");
            while(1);
        }
        if (SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_2MHZ+9, /* timeout */ 102) == SDCARD_SUCCESS)
            break;
        __delay_ms(200);
    }
}

static void supermez80_cpm_bus_master(int enable)
{
    if (enable) {
        // Set address bus as output
        TRIS(Z80_ADDR_L) = 0x00;    // A7-A0
        LAT(Z80_A14) = 0;
        TRIS(Z80_A14) = 0;
        LAT(Z80_A15) = 0;
        TRIS(Z80_A15) = 0;

        // Set /MEMRQ, /RD and /WR as output
        LAT(Z80_MEMRQ) = 1;         // deactivate /MEMRQ
        LAT(Z80_RD) = 1;            // deactivate /RD
        LAT(Z80_WR) = 1;            // deactivate /WR
        TRIS(Z80_MEMRQ) = 0;        // output
        TRIS(Z80_RD) = 0;           // output
        TRIS(Z80_WR) = 0;           // output
    } else {
        // restore bank pins
        set_bank_pins((uint32_t)mmu_bank << 16);

        // Set address bus as input
        TRIS(Z80_ADDR_L) = 0xff;    // A7-A0
        TRIS(Z80_A14) = 1;
        TRIS(Z80_A15) = 1;
        TRIS(Z80_DATA) = 0xff;      // D7-D0 pin

        // Set /MEMRQ, /RD and /WR as input
        TRIS(Z80_MEMRQ) = 1;        // input
        TRIS(Z80_RD) = 1;           // input
        TRIS(Z80_WR) = 1;           // input
    }
}

static void supermez80_cpm_start_z80(void)
{
    emuz80_common_start_z80();

    // CLC input pin assign
    CLCIN0PPS = PPS_IN(Z80_IOREQ);	// Port A or C can be specified
    CLCIN1PPS = PPS_IN(Z80_MEMRQ);  // Port A or C can be specified
    CLCIN2PPS = PPS_IN(Z80_A14);    // Port B or D can be specified
    CLCIN3PPS = PPS_IN(Z80_A15);    // Port B or D can be specified

    //
    // CLC1: /IOREQ -> /WAIT
    //
    CLCSELECT = 0;                  // Select CLC1
    CLCnCON = 0x00;                 // Disable CLC

    // input data selection
    CLCnSEL0 = 0;                   // /IORQ is connected to input 0
    CLCnSEL1 = 127;                 // NC
    CLCnSEL2 = 127;                 // NC
    CLCnSEL3 = 127;                 // NC

    // data gating
    CLCnGLS0 = 0x1;                 // Input 0 is inverted and gated into g1
    CLCnGLS1 = 0x0;                 // NC
    CLCnGLS2 = 0x0;                 // NC
    CLCnGLS3 = 0x0;                 // NC

    // select gate output polarities
    CLCnPOL = 0x82;                 // Inverted the CLC output
    CLCnCON = 0x8c;                 // Enable, 1-Input D FF with S and R, falling edge inturrupt

    CLCDATA = 0x0;                  // Clear all CLC outs
    CLC1IF = 0;                     // Clear the CLC interrupt flag
    CLC1IE = 0;                     // Interrupt is not enabled. This will be handled by polling.

    PPS(Z80_WAIT) = 0x01;           // CLC1 -> /WAIT

    // Z80 start
    LAT(Z80_BUSRQ) = 1;  // /BUSREQ=1
    LAT(Z80_RESET) = 1;  // Release reset
}

static void supermez80_cpm_set_wait_pin(uint8_t v)
{
    if (v == 1) {
        // Release wait (D-FF reset)
        G3POL = 1;
        G3POL = 0;
    } else {
        // not implemented
    }
}

static void supermez80_cpm_set_bank_pins(uint32_t addr)
{
    LAT(BANK0) = (addr >> 16) & 1;
    LAT(BANK1) = ~((addr >> 17) & 1);  // invert A17 to activate CE2 of TC551001
}

static void supermez80_cpm_setup_addrbus(uint32_t addr)
{
    LAT(Z80_A14) = 0;
    LAT(Z80_A15) = 0;
}

static uint16_t supermez80_cpm_low_addr_mask(void)
{
    return LOW_ADDR_MASK;
}

static __bit supermez80_cpm_io_event(void)
{
    return CLC1IF;
}

static void supermez80_cpm_wait_io_event(void)
{
    while (!CLC1IF && !invoke_monitor);
}

static void supermez80_cpm_clear_io_event(void)
{
    CLC1IF = 0;
}

void board_init()
{
    emuz80_common_init();

    board_sys_init_hook = supermez80_cpm_sys_init;
    board_bus_master_hook = supermez80_cpm_bus_master;
    board_start_z80_hook = supermez80_cpm_start_z80;
    board_set_bank_pins_hook = supermez80_cpm_set_bank_pins;
    board_setup_addrbus_hook = supermez80_cpm_setup_addrbus;
    board_low_addr_mask_hook = supermez80_cpm_low_addr_mask;
    board_io_event_hook = supermez80_cpm_io_event;
    board_wait_io_event_hook = supermez80_cpm_wait_io_event;
    board_clear_io_event_hook = supermez80_cpm_clear_io_event;

    board_set_wait_pin_hook  = supermez80_cpm_set_wait_pin;
}

#include <pic18f47q43_spi.c>
#include <SDCard.c>