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

#include <supermez80.h>
#include <mcp23s08.h>
#include "emuz80_common.h"

#define Z80_IOREQ	RA0
#define Z80_MEMRQ	RA1
// RA2 is assigned to WE of SRAM
// RA3 is assigned to CLK which controlled by NCO
// RA4 is assigned to OE of SRAM
#define Z80_RD		RA5
// RA6 is used as UART TXD
// RA7 is used as UART RXD

#define Z80_BUSRQ	RE0
#define Z80_RESET	RE1
// RE2 is assigned to SS of SPI
// RE3 is occupied by PIC MCLR

static __bit supermez80_spi_ioreq_pin(void) { return Z80_IOREQ; }
static __bit supermez80_spi_memrq_pin(void) { return Z80_MEMRQ; }
static __bit supermez80_spi_rd_pin(void) { return Z80_RD; }
static void supermez80_spi_set_busrq_pin(uint8_t v) { Z80_BUSRQ = (__bit)(v & 0x01); }
static void supermez80_spi_set_reset_pin(uint8_t v) { Z80_RESET = (__bit)(v & 0x01); }

static void supermez80_spi_set_nmi_pin(uint8_t v) {
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, v);
}

static void supermez80_spi_set_int_pin(uint8_t v) {
    // we does not have INT pin
    // Z80_INT = v;
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

static void supermez80_spi_sys_init()
{
    emuz80_common_sys_init();

    // RESET (RE1) output pin
    LATE1 = 0;          // Reset
    TRISE1 = 0;         // Set as output

    // /BUSREQ (RE0) output pin
    LATE0 = 0;          // BUS request
    TRISE0 = 0;         // Set as output

    // Address bus A15-A8 pin (A14:/RFSH, A15:/WAIT)
    LATD = 0x00;
    #ifdef Z80_USE_M1_FOR_SRAM_OE
    TRISD = 0x60;       // Set as output except 6:/RFSH and 5:/M1
    #else
    TRISD = 0x40;       // Set as output except 6:/RFSH
    #endif

    // SPI /CS (RE2) output pin
    LATE2 = 1;          // deactive
    TRISE2 = 0;         // Set as output

    // Address bus A7-A0 pin
    LATB = 0x00;
    TRISB = 0x00;       // Set as output

    // Data bus D7-D0 pin
    LATC = 0x00;
    TRISC = 0x00;       // Set as output

    // Z80 clock(RA3)
#ifdef Z80_CLK
    RA3PPS = 0x3f;      // RA3 asign NCO1
    TRISA3 = 0;         // NCO output pin
    NCO1INC = Z80_CLK * 2 / 61;
    // NCO1INC = 524288;   // 15.99MHz
    NCO1CLK = 0x00;     // Clock source Fosc
    NCO1PFM = 0;        // FDC mode
    NCO1OUT = 1;        // NCO output enable
    NCO1EN = 1;         // NCO enable
#else
    // Disable clock output for Z80 (Use external clock for Z80)
    RA3PPS = 0;         // select LATxy
    TRISA3 = 1;         // set as input
    NCO1OUT = 0;        // NCO output disable
    NCO1EN = 0;         // NCO disable
#endif

    // /WE (RA2) output pin
    LATA2 = 1;          //
    TRISA2 = 0;         // Set as output
    RA2PPS = 0x00;      // LATA2 -> RA2

    // /OE (RA4) output pin
    LATA4 = 1;
    TRISA4 = 0;         // Set as output
    RA4PPS = 0x00;      // unbind with CLC1
}

static void supermez80_spi_bus_master(int enable)
{
    if (enable) {
        RA4PPS = 0x00;      // unbind CLC1 and /OE (RA4)
        RA2PPS = 0x00;      // unbind CLC2 and /WE (RA2)
        LATA4 = 1;          // deactivate /OE
        LATA2 = 1;          // deactivate /WE

        // Set address bus as output
        #ifdef Z80_USE_M1_FOR_SRAM_OE
        TRISD = 0x60;       // A15-A8 pin except 6:/RFSH and 5:/M1
        #else
        TRISD = 0x40;       // A15-A8 pin except 6:/RFSH
        #endif
        TRISB = 0x00;       // A7-A0
    } else {
        // Set address bus as input
        dma_release_addrbus();

        TRISD = 0x7f;       // A15-A8 pin except 7:/WAIT
        TRISB = 0xff;       // A7-A0 pin
        TRISC = 0xff;       // D7-D0 pin

        RA4PPS = 0x01;      // CLC1 -> RA4 -> /OE
        RA2PPS = 0x02;      // CLC2 -> RA2 -> /WE
    }
}

static void supermez80_spi_start_z80(void)
{
    // Address bus A15-A8 pin (A14:/RFSH, A15:/WAIT)
    ANSELD = 0x00;      // Disable analog function
    WPUD = 0xff;        // Week pull up
    TRISD = 0xff;       // Set as input

    // Address bus A7-A0 pin
    ANSELB = 0x00;      // Disable analog function
    WPUB = 0xff;        // Week pull up
    TRISB = 0xff;       // Set as input

    // Data bus D7-D0 input pin
    ANSELC = 0x00;      // Disable analog function
    WPUC = 0xff;        // Week pull up
    TRISC = 0xff;       // Set as input

    // /IORQ (RA0) input pin
    ANSELA0 = 0;        // Disable analog function
    WPUA0 = 1;          // Week pull up
    TRISA0 = 1;         // Set as input

    // /MREQ (RA1) input pin
    ANSELA1 = 0;        // Disable analog function
    WPUA1 = 1;          // Week pull up
    TRISA1 = 1;         // Set as input

    // /RD (RA5) input pin
    ANSELA5 = 0;        // Disable analog function
    WPUA5 = 1;          // Week pull up
    TRISA5 = 1;         // Set as input

    #ifdef Z80_USE_M1_FOR_SRAM_OE
    // /M1 (RD5) input pin
    ANSELD5 = 0;        // Disable analog function
    WPUD5 = 1;          // Week pull up
    TRISD5 = 1;         // Set as input
    #endif

    // /RFSH (RD6) input pin
    ANSELD6 = 0;        // Disable analog function
    WPUD6 = 1;          // Week pull up
    TRISD6 = 1;         // Set as input

    // /WAIT (RD7) output pin
    ANSELD7 = 0;        // Disable analog function
    LATD7 = 1;          // WAIT
    TRISD7 = 0;         // Set as output


    //========== CLC pin assign ===========
    // 0,1,4,5 = Port A, C
    // 2,3,6,7 = Port B, D
    CLCIN0PPS = 0x01;   // RA1 <- /MREQ
    CLCIN1PPS = 0x00;   // RA0 <- /IORQ
    CLCIN2PPS = 0x1e;   // RD6 <- /RFSH
    #ifdef Z80_USE_M1_FOR_SRAM_OE
    CLCIN3PPS = 0x1d;   // RD5 <- /M1
    #endif
    CLCIN4PPS = 0x05;   // RA5 <- /RD

    // 1,2,5,6 = Port A, C
    // 3,4,7,8 = Port B, D
    RA4PPS = 0x01;       // CLC1 -> RA4 -> /OE
    RA2PPS = 0x02;       // CLC2 -> RA2 -> /WE
    RD7PPS = 0x03;       // CLC3 -> RD7 -> /WAIT

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
    LATE0 = 1;           // /BUSREQ=1
    LATE1 = 1;           // Release reset
}

void supermez80_spi_init()
{
    board_sys_init_hook = supermez80_spi_sys_init;
    board_bus_master_hook = supermez80_spi_bus_master;
    board_start_z80_hook = supermez80_spi_start_z80;

    board_ioreq_pin_hook     = supermez80_spi_ioreq_pin;
    board_memrq_pin_hook     = supermez80_spi_memrq_pin;
    board_rd_pin_hook        = supermez80_spi_rd_pin;
    board_set_busrq_pin_hook = supermez80_spi_set_busrq_pin;
    board_set_reset_pin_hook = supermez80_spi_set_reset_pin;
    board_set_nmi_pin_hook   = supermez80_spi_set_nmi_pin;
    board_set_int_pin_hook   = supermez80_spi_set_int_pin;
    board_set_wait_pin_hook  = supermez80_spi_set_wait_pin;
}
