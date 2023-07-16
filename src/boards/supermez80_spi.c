/*
 * UART, disk I/O and monitor firmware for SuperMEZ80
 *
 * Based on main.c by Tetsuya Suzuki and emuz80_z80ram.c by Satoshi Okue
 * Modified by @hanyazou https://twitter.com/hanyazou
 */
/*!
 * PIC18F47Q43/PIC18F47Q83/PIC18F47Q84 ROM image uploader and UART emulation firmware
 * This single source file contains all code
 *
 * Target: EMUZ80 with Z80+RAM
 * Compiler: MPLAB XC8 v2.40
 *
 * Modified by Satoshi Okue https://twitter.com/S_Okue
 * Version 0.1 2022/11/15
 */

/*
    PIC18F47Q43 ROM RAM and UART emulation firmware
    This single source file contains all code

    Target: EMUZ80 - The computer with only Z80 and PIC18F47Q43
    Compiler: MPLAB XC8 v2.36
    Written by Tetsuya Suzuki
*/

#define BOARD_DEPENDENT_SOURCE

#include <supermez80.h>
#include <mcp23s08.h>

#define Z80_IOREQ   A0
#define Z80_MEMRQ   A1
#define SRAM_WE     A2
#define Z80_CLK     A3
#define SRAM_OE     A4
#define Z80_RD      A5
// RA6 is used as UART TXD
// RA7 is used as UART RXD

// RD0~5 are used as address high A8~13
#ifdef Z80_USE_M1_FOR_SRAM_OE
#define Z80_M1      D5
#endif
#define Z80_RFSH    D6
#define Z80_WAIT    D7

#define Z80_BUSRQ   E0
#define Z80_RESET   E1
#define SPI_SS      E2
// RE3 is occupied by PIC MCLR

#define SPI_SDCARD_PICO_PPS  RC0PPS
#define SPI_SDCARD_PICO_TRIS TRISC0
#define SPI_SDCARD_CLK_PIN   ((2 << 3) | 1)  // RC1
#define SPI_SDCARD_CLK_PPS   RC1PPS
#define SPI_SDCARD_CLK_TRIS  TRISC1
#define SPI_SDCARD_POCI_PIN  ((2 << 3) | 2)  // RC2
#define SPI_SDCARD_POCI_TRIS TRISC2
#define SPI_SDCARD_CS        LATE2  // chip select
#define SPI_SDCARD_CS_TRIS   TRISE2
#define SPI_SDCARD_CS_ANSEL  ANSELE2
#define SPI_SDCARD_CS_PORT   0

#include "emuz80_common.c"

static void supermez80_spi_sys_init()
{
    emuz80_common_sys_init();

    // Address bus A15-A8 pin (A14:/RFSH, A15:/WAIT)
    LAT(Z80_ADDR_H) = 0x00;
    #ifdef Z80_USE_M1_FOR_SRAM_OE
    TRIS(Z80_ADDR_H) = 0x60;    // Set as output except 6:/RFSH and 5:/M1
    #else
    TRIS(Z80_ADDR_H) = 0x40;    // Set as output except 6:/RFSH
    #endif

    // SPI /CS output pin
    LAT(SPI_SS) = 1;            // deactive
    TRIS(SPI_SS) = 0;           // Set as output

    // Z80 clock
#ifdef Z80_CLK_HZ
    PPS(Z80_CLK) = 0x3f;        // asign NCO1
    TRIS(Z80_CLK) = 0;          // NCO output pin
    NCO1INC = Z80_CLK_HZ * 2 / 61;
    NCO1CLK = 0x00;             // Clock source Fosc
    NCO1PFM = 0;                // FDC mode
    NCO1OUT = 1;                // NCO output enable
    NCO1EN = 1;                 // NCO enable
#else
    // Disable clock output for Z80 (Use external clock for Z80)
    PPS(Z80_CLK) = 0;           // select LATxy
    TRIS(Z80_CLK) = 1;          // set as input
    NCO1OUT = 0;                // NCO output disable
    NCO1EN = 0;                 // NCO disable
#endif

    // /WE output pin
    LAT(SRAM_WE) = 1;
    TRIS(SRAM_WE) = 0;          // Set as output
    PPS(SRAM_WE) = 0x00;        // unbind with CLC

    // /OE output pin
    LAT(SRAM_OE) = 1;
    TRIS(SRAM_OE) = 0;          // Set as output
    PPS(SRAM_OE) = 0x00;        // unbind with CLC
}

static void supermez80_spi_bus_master(int enable)
{
    if (enable) {
        PPS(SRAM_OE) = 0x00;        // unbind CLC and /OE
        PPS(SRAM_WE) = 0x00;        // unbind CLC and /WE
        LAT(SRAM_OE) = 1;           // deactivate /OE
        LAT(SRAM_WE) = 1;           // deactivate /WE

        // Set address bus as output
        #ifdef Z80_USE_M1_FOR_SRAM_OE
        TRIS(Z80_ADDR_H) = 0x60;    // A15-A8 pin except 6:/RFSH and 5:/M1
        #else
        TRIS(Z80_ADDR_H) = 0x40;    // A15-A8 pin except 6:/RFSH
        #endif
        TRIS(Z80_ADDR_L) = 0x00;    // A7-A0
    } else {
        // Set address bus as input
        dma_release_addrbus();

        TRIS(Z80_ADDR_H) = 0x7f;    // A15-A8 pin except 7:/WAIT
        TRIS(Z80_ADDR_L) = 0xff;    // A7-A0 pin
        TRIS(Z80_DATA) = 0xff;      // D7-D0 pin

        PPS(SRAM_OE) = 0x01;        // CLC1 -> /OE
        PPS(SRAM_WE) = 0x02;        // CLC2 -> /WE
    }
}

static void supermez80_spi_start_z80(void)
{
    emuz80_common_start_z80();

    //========== CLC pin assign ===========
    // 0,1,4,5 = Port A, C
    // 2,3,6,7 = Port B, D
    CLCIN0PPS = 0x01;           // RA1 <- /MREQ
    CLCIN1PPS = 0x00;           // RA0 <- /IORQ
    CLCIN2PPS = 0x1e;           // RD6 <- /RFSH
    #ifdef Z80_USE_M1_FOR_SRAM_OE
    CLCIN3PPS = 0x1d;           // RD5 <- /M1
    #endif
    CLCIN4PPS = 0x05;           // RA5 <- /RD

    // 1,2,5,6 = Port A, C
    // 3,4,7,8 = Port B, D
    PPS(SRAM_OE) = 0x01;        // CLC1 -> /OE
    PPS(SRAM_WE) = 0x02;        // CLC2 -> /WE
    PPS(Z80_WAIT) = 0x03;       // CLC3 -> /WAIT

    //========== CLC1 /OE ==========
    CLCSELECT = 0;       // CLC1 select

    #ifdef Z80_USE_M1_FOR_SRAM_OE
    CLCnSEL0 = 0;        // CLCIN0PPS <- /MREQ
    CLCnSEL1 = 4;        // CLCIN4PPS <- /RD
    CLCnSEL2 = 3;        // CLCIN3PPS <- /M1
    CLCnSEL3 = 127;      // NC

    CLCnGLS0 = 0x01;     // /MREQ inverted
    CLCnGLS1 = 0x04;     // /RD inverted
    CLCnGLS2 = 0x10;     // /M1 inverted
    CLCnGLS3 = 0x40;     // 1(0 inverted) for AND gate

    CLCnPOL = 0x80;      // inverted the CLC1 output
    CLCnCON = 0x80;      // AND-OR
    #else
    CLCnSEL0 = 0;        // CLCIN0PPS <- /MREQ
    CLCnSEL1 = 2;        // CLCIN2PPS <- /RFSH
    CLCnSEL2 = 4;        // CLCIN4PPS <- /RD
    CLCnSEL3 = 127;      // NC

    CLCnGLS0 = 0x01;     // /MREQ inverted
    CLCnGLS1 = 0x08;     // /RFSH noninverted
    CLCnGLS2 = 0x10;     // RD inverted
    CLCnGLS3 = 0x40;     // 1(0 inverted) for AND gate

    CLCnPOL = 0x80;      // inverted the CLC1 output
    CLCnCON = 0x82;      // 4 input AND
    #endif

    //========== CLC2 /WE ==========
    CLCSELECT = 1;       // CLC2 select

    CLCnSEL0 = 0;        // CLCIN0PPS <- /MREQ
    CLCnSEL1 = 2;        // CLCIN2PPS <- /RFSH
    CLCnSEL2 = 4;        // CLCIN4PPS <- /RD
    CLCnSEL3 = 127;      // NC

    CLCnGLS0 = 0x01;     // /MREQ inverted
    CLCnGLS1 = 0x08;     // /RFSH noninverted
    CLCnGLS2 = 0x20;     // /RD noninverted
    CLCnGLS3 = 0x40;     // 1(0 inverted) for AND gate

    CLCnPOL = 0x80;      // inverted the CLC2 output
    CLCnCON = 0x82;      // 4 input AND

    //========== CLC3 /WAIT ==========
    CLCSELECT = 2;       // CLC3 select

    CLCnSEL0 = 1;        // D-FF CLK <- /IORQ
    CLCnSEL1 = 127;      // D-FF D NC
    CLCnSEL2 = 127;      // D-FF S NC
    CLCnSEL3 = 127;      // D-FF R NC

    CLCnGLS0 = 0x1;      // LCG1D1N
    CLCnGLS1 = 0x0;      // Connect none
    CLCnGLS2 = 0x0;      // Connect none
    CLCnGLS3 = 0x0;      // Connect none

    CLCnPOL = 0x82;      // inverted the CLC3 output
    CLCnCON = 0x8c;      // Select D-FF, falling edge inturrupt

    CLCDATA = 0x0;       // Clear all CLC outs
    CLC3IF = 0;          // Clear the CLC interrupt flag

    // Unlock IVT
    IVTLOCK = 0x55;
    IVTLOCK = 0xAA;
    IVTLOCKbits.IVTLOCKED = 0x00;

    // Default IVT base address
    IVTBASE = 0x000008;

    // Lock IVT
    IVTLOCK = 0x55;
    IVTLOCK = 0xAA;
    IVTLOCKbits.IVTLOCKED = 0x01;

    // Z80 start
    LAT(Z80_BUSRQ) = 1;  // /BUSREQ=1
    LAT(Z80_RESET) = 1;  // Release reset
}

static void supermez80_spi_set_nmi_pin(uint8_t v) {
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, v);
}

static void supermez80_spi_set_wait_pin(uint8_t v)
{
    if (v == 1) {
        // Release wait (D-FF reset)
        G3POL = 1;
        G3POL = 0;
    } else {
        // not implemented
    }
}

void supermez80_spi_init()
{
    emuz80_common_init();

    board_sys_init_hook = supermez80_spi_sys_init;
    board_bus_master_hook = supermez80_spi_bus_master;
    board_start_z80_hook = supermez80_spi_start_z80;

    board_set_nmi_pin_hook   = supermez80_spi_set_nmi_pin;
    board_set_wait_pin_hook  = supermez80_spi_set_wait_pin;
}

#define SPI_PREFIX SPI_SDCARD
#define SPI_USE_MCP23S08
#include <pic18f47q43_spi.c>
#include <SDCard.c>
