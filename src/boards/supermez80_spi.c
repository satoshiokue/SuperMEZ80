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
#include <stdio.h>
#include <SDCard.h>
#include <mcp23s08.h>
#include <picregister.h>

#define Z80_DATA        C
#define Z80_ADDR_H      D
#define Z80_ADDR_L      B

#define Z80_IOREQ       A0
#define Z80_MEMRQ       A1
#define SRAM_WE         A2
#define Z80_CLK         A3
#define SRAM_OE         A4
#define Z80_RD          A5
// RA6 is used as UART TXD
// RA7 is used as UART RXD

// RD0~5 are used as address high A8~13
#ifdef Z80_USE_M1_FOR_SRAM_OE
#define Z80_M1          D5
#endif
#define Z80_RFSH        D6
#define Z80_WAIT        D7

#define Z80_BUSRQ       E0
#define Z80_RESET       E1
#define SPI_SS          E2
// RE3 is occupied by PIC MCLR

#define GPIO_CS0        0
#if defined(Z80_USE_M1_FOR_SRAM_OE)
#define GPIO_A13        1
#else
#define GPIO_CS1        1
#endif
#define GPIO_BANK1      2
#define GPIO_BANK2      3
#define GPIO_NMI        4
#define GPIO_A14        5
#define GPIO_A15        6
#define GPIO_BANK0      7

#define SPI_SD_PICO     C0
#define SPI_SD_CLK      C1
#define SPI_SD_POCI     C2
#define SPI_SD_SS       SPI_SS
#define SPI_SD_GPIO_SS  GPIO_CS0

#if defined(GPIO_A13)
#define HIGH_ADDR_MASK  0xffffe000
#define LOW_ADDR_MASK   0x00001fff
#elif defined(GPIO_A14)
#define HIGH_ADDR_MASK  0xffffc000
#define LOW_ADDR_MASK   0x00003fff
#elif defined(GPIO_A15)
#define HIGH_ADDR_MASK  0xffff8000
#define LOW_ADDR_MASK   0x00007fff
#else
#define HIGH_ADDR_MASK  0xffff0000
#define LOW_ADDR_MASK   0x0000ffff
#endif

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

    // /WE output pin
    LAT(SRAM_WE) = 1;
    TRIS(SRAM_WE) = 0;          // Set as output
    PPS(SRAM_WE) = 0x00;        // unbind with CLC

    // /OE output pin
    LAT(SRAM_OE) = 1;
    TRIS(SRAM_OE) = 0;          // Set as output
    PPS(SRAM_OE) = 0x00;        // unbind with CLC

    emuz80_common_wait_for_programmer();

    //
    // Initialize SPI I/O expander MCP23S08
    //
    if (mcp23s08_probe(MCP23S08_ctx, SPI_CLOCK_2MHZ, 0 /* address */) == 0) {
        printf("SuperMEZ80+SPI with GPIO expander\n\r");
    }
    mcp23s08_write(MCP23S08_ctx, GPIO_CS0, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS0, MCP23S08_PINMODE_OUTPUT);
    #ifdef GPIO_CS1
    mcp23s08_write(MCP23S08_ctx, GPIO_CS1, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS1, MCP23S08_PINMODE_OUTPUT);
    #endif
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_NMI, MCP23S08_PINMODE_OUTPUT);

    //
    // Initialize memory bank
    //
    set_bank_pins(0x00000);
    #ifdef GPIO_BANK0
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK0, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK1
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK1, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK2
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK2, MCP23S08_PINMODE_OUTPUT);
    #endif

    //
    // Initialize SD Card
    //
    static int retry;  // I don't know why, but someone destroys automatic variables,
                       // so I made them static variables to get around it.
    for (retry = 0; 1; retry++) {
        if (20 <= retry) {
            printf("No SD Card?\n\r");
            while(1);
        }
        if (SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_2MHZ, /* timeout */ 100) == SDCARD_SUCCESS)
            break;
        __delay_ms(200);
    }
}

static void supermez80_spi_release_addrbus(void)
{
    int pending = mcp23s08_set_pending(MCP23S08_ctx, 1);
    #ifdef GPIO_A13
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A13, MCP23S08_PINMODE_INPUT);
    #endif
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A14, MCP23S08_PINMODE_INPUT);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A15, MCP23S08_PINMODE_INPUT);

    // higher address lines must always be driven by MCP23S08
    set_bank_pins((uint32_t)mmu_bank << 16);

    #ifdef GPIO_LED
    mcp23s08_write(MCP23S08_ctx, GPIO_LED, turn_on_io_len ? 0 : 1);
    #endif
    mcp23s08_set_pending(MCP23S08_ctx, pending);
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
        supermez80_spi_release_addrbus();

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
    CLC3IE = 0;          // NOTE: CLC3 interrupt is not enabled. This will be handled by polling.

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

static void supermez80_spi_set_bank_pins(uint32_t addr)
{
    uint32_t mask = 0;
    uint32_t val = 0;

    #ifdef GPIO_BANK0
    mask |= (1 << GPIO_BANK0);
    if ((addr >> 16) & 1) {
        val |= (1 << GPIO_BANK0);
    }
    #endif
    #ifdef GPIO_BANK1
    mask |= (1 << GPIO_BANK1);
    if (!((addr >> 17) & 1)) {  // invert A17 to activate CE2 of TC551001
        val |= (1 << GPIO_BANK1);
    }
    #endif
    #ifdef GPIO_BANK2
    mask |= (1 << GPIO_BANK2);
    if ((addr >> 18) & 1) {
        val |= (1 << GPIO_BANK2);
    }
    #endif
    mcp23s08_masked_write(MCP23S08_ctx, mask, val);
}

static void supermez80_spi_setup_addrbus(uint32_t addr)
{
    static int no_mcp23s08_warn = 1;

    if (no_mcp23s08_warn && (addr & HIGH_ADDR_MASK) != 0) {
        no_mcp23s08_warn = 0;
        if (!mcp23s08_is_alive(MCP23S08_ctx)) {
            printf("WARNING: no GPIO expander to control higher address\n\r");
        }
    }
    int pending = mcp23s08_set_pending(MCP23S08_ctx, 1);
    #ifdef GPIO_LED
    mcp23s08_write(MCP23S08_ctx, GPIO_LED, turn_on_io_len ? 0 : 1);
    #endif
    #ifdef GPIO_A13
    mcp23s08_write(MCP23S08_ctx, GPIO_A13, ((addr >> 13) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A13, MCP23S08_PINMODE_OUTPUT);
    #endif
    mcp23s08_write(MCP23S08_ctx, GPIO_A14, ((addr >> 14) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A14, MCP23S08_PINMODE_OUTPUT);
    mcp23s08_write(MCP23S08_ctx, GPIO_A15, ((addr >> 15) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A15, MCP23S08_PINMODE_OUTPUT);

    set_bank_pins(addr);
    mcp23s08_set_pending(MCP23S08_ctx, pending);
}

static __bit supermez80_spi_io_event(void)
{
    return CLC3IF;
}

static void supermez80_spi_wait_io_event(void)
{
    while (!CLC3IF && !invoke_monitor);
}

static void supermez80_spi_clear_io_event(void)
{
    CLC3IF = 0;
}

void board_init()
{
    emuz80_common_init();

    board_sys_init_hook = supermez80_spi_sys_init;
    board_bus_master_hook = supermez80_spi_bus_master;
    board_start_z80_hook = supermez80_spi_start_z80;
    board_set_bank_pins_hook = supermez80_spi_set_bank_pins;
    board_setup_addrbus_hook = supermez80_spi_setup_addrbus;
    board_io_event_hook = supermez80_spi_io_event;
    board_wait_io_event_hook = supermez80_spi_wait_io_event;
    board_clear_io_event_hook = supermez80_spi_clear_io_event;

    board_set_nmi_pin_hook   = supermez80_spi_set_nmi_pin;
    board_set_wait_pin_hook  = supermez80_spi_set_wait_pin;
}

#define SPI_PREFIX SPI_SD
#define SPI_HW_INST SPI1
#define SPI_USE_MCP23S08
#include <pic18f47q43_spi.c>
#include <SDCard.c>
