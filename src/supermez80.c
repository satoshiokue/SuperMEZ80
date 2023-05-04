/*
 * UART, disk I/O and monitor firmware for SuperMEZ80-SPI
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

#define INCLUDE_PIC_PRAGMA
#include <supermez80.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDCard.h>
#include <SPI.h>
#include <mcp23s08.h>
#include <utils.h>

static FATFS fs;
static DIR fsdir;
static FILINFO fileinfo;
static FIL files[NUM_FILES];
static int num_files = 0;
uint8_t tmp_buf[2][TMP_BUF_SIZE];

const unsigned char rom[] = {
// Initial program loader at 0x0000
#ifdef CPM_MMU_EXERCISE
#include "mmu_exercise.inc"
#else
#include "ipl.inc"
#endif
};

void bus_master(int enable);
void sys_init(void);
void ioexp_init(void);
int disk_init(void);
void start_z80(void);

// main routine
void main(void)
{
    sys_init();

    //
    // Give a chance to use PRC (RB6/A6) and PRD (RB7/A7) to PIC programer.
    // It must prevent Z80 from driving A6 and A7 while this period.
    //
    printf("\n\r");
    printf("wait for programmer ...\r");
    __delay_ms(200);
    printf("                       \r");

    printf("\n\r");

    ioexp_init();
    mem_init();
    mon_init();

#if !defined(CPM_MMU_EXERCISE)
    if (disk_init() < 0)
        while (1);
#endif  // !CPM_MMU_EXERCISE

    //
    // Transfer ROM image to the SRAM
    //
    dma_write_to_sram(0x00000, rom, sizeof(rom));

    //
    // Start Z80
    //
    printf("\n\r");
    start_z80();

    while(1);  // All things come to those who wait
}

void bus_master(int enable)
{
    if (enable) {
        RA4PPS = 0x00;      // unbind CLC1 and /OE (RA4)
        RA2PPS = 0x00;      // unbind CLC2 and /WE (RA2)
        LATA4 = 1;          // deactivate /OE
        LATA2 = 1;          // deactivate /WE

        // Set address bus as output (except /RFSH)
        TRISD = 0x40;       // A15-A8 pin (A14:/RFSH, A15:/WAIT)
        TRISB = 0x00;       // A7-A0
    } else {
        // Set address bus as input
        TRISD = 0x7f;       // A15-A8 pin (A14:/RFSH, A15:/WAIT)
        TRISB = 0xff;       // A7-A0 pin
        TRISC = 0xff;       // D7-D0 pin

        RA4PPS = 0x01;      // CLC1 -> RA4 -> /OE
        RA2PPS = 0x02;      // CLC2 -> RA2 -> /WE
    }
}

void sys_init()
{
    // System initialize
    OSCFRQ = 0x08;      // 64MHz internal OSC

    // RESET (RE1) output pin
    ANSELE1 = 0;        // Disable analog function
    LATE1 = 0;          // Reset
    TRISE1 = 0;         // Set as output

    // /BUSREQ (RE0) output pin
    ANSELE0 = 0;        // Disable analog function
    LATE0 = 0;          // BUS request
    TRISE0 = 0;         // Set as output

    // Address bus A15-A8 pin (A14:/RFSH, A15:/WAIT)
    ANSELD = 0x00;      // Disable analog function
    LATD = 0x00;
    TRISD = 0x40;       // Set as output except /RFSH

    // SPI /CS (RE2) output pin
    ANSELE2 = 0;        // Disable analog function
    LATE2 = 1;          // deactive
    TRISE2 = 0;         // Set as output

    // Address bus A7-A0 pin
    ANSELB = 0x00;      // Disable analog function
    LATB = 0x00;
    TRISB = 0x00;       // Set as output

    // Data bus D7-D0 pin
    ANSELC = 0x00;      // Disable analog function
    LATC = 0x00;
    TRISC = 0x00;       // Set as output

    // Z80 clock(RA3) by NCO FDC mode
    RA3PPS = 0x3f;      // RA3 asign NCO1
    ANSELA3 = 0;        // Disable analog function
    TRISA3 = 0;         // NCO output pin
    NCO1INC = Z80_CLK * 2 / 61;
    // NCO1INC = 524288;   // 15.99MHz
    NCO1CLK = 0x00;     // Clock source Fosc
    NCO1PFM = 0;        // FDC mode
    NCO1OUT = 1;        // NCO output enable
    NCO1EN = 1;         // NCO enable

    // /WE (RA2) output pin
    ANSELA2 = 0;        // Disable analog function
    LATA2 = 1;          //
    TRISA2 = 0;         // Set as output

    // /OE (RA4) output pin
    ANSELA4 = 0;        // Disable analog function
    LATA4 = 1;
    TRISA4 = 0;         // Set as output
    RA4PPS = 0x00;      // unbind with CLC1

    // UART3 initialize
    U3BRG = 416;        // 9600bps @ 64MHz
    U3RXEN = 1;         // Receiver enable
    U3TXEN = 1;         // Transmitter enable

    // UART3 Receiver
    ANSELA7 = 0;        // Disable analog function
    TRISA7 = 1;         // RX set as input
    U3RXPPS = 0x07;     // RA7->UART3:RX3;

    // UART3 Transmitter
    ANSELA6 = 0;        // Disable analog function
    LATA6 = 1;          // Default level
    TRISA6 = 0;         // TX set as output
    RA6PPS = 0x26;      // RA6->UART3:TX3;

    U3ON = 1;           // Serial port enable

    RA2PPS = 0x00;      // LATA2 -> RA2
}

void ioexp_init(void)
{
    //
    // Initialize SPI I/O expander MCP23S08
    //
    if (mcp23s08_probe(MCP23S08_ctx, SPI1_ctx, SPI_CLOCK_100KHZ, 0 /* address */) == 0) {
        printf("SuperMEZ80+SPI with GPIO expander\n\r");
    }
    mcp23s08_write(MCP23S08_ctx, GPIO_CS0, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS0, MCP23S08_PINMODE_OUTPUT);
    mcp23s08_write(MCP23S08_ctx, GPIO_CS1, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS1, MCP23S08_PINMODE_OUTPUT);
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_NMI, MCP23S08_PINMODE_OUTPUT);
}

int disk_init(void)
{
    unsigned int i;

    //
    // Initialize SD Card
    //
    for (int retry = 0; 1; retry++) {
        if (20 <= retry) {
            printf("No SD Card?\n\r");
            return -1;
        }
        if (SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_2MHZ, /* timeout */ 100) == SDCARD_SUCCESS)
            break;
        __delay_ms(200);
    }
    if (f_mount(&fs, "0://", 1) != FR_OK) {
        printf("Failed to mount SD Card.\n\r");
        return -2;
    }

    //
    // Select disk image folder
    //
    if (f_opendir(&fsdir, "/")  != FR_OK) {
        printf("Failed to open SD Card..\n\r");
        return -3;
    }
    i = 0;
    int selection = -1;
    while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
        if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
            strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
            printf("%d: %s\n\r", i, fileinfo.fname);
            if (strcmp(fileinfo.fname, "CPMDISKS") == 0)
                selection = i;
            i++;
        }
    }
    if (1 < i) {
        printf("Select: ");
        while (1) {
            char c = getch();       // Wait for input char
            if ('0' <= c && c <= '9' && c - '0' <= i) {
                selection = c - '0';
                break;
            }
            if ((c == 0x0d || c == 0x0a) && 0 <= selection)
                break;
        }
        printf("%d\n\r", selection);
        f_rewinddir(&fsdir);
        i = 0;
        while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
            if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
                strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
                if (selection == i)
                    break;
                i++;
            }
        }
        printf("%s is selected.\n\r", fileinfo.fname);
    } else {
        strcpy(fileinfo.fname, "CPMDISKS");
    }
    f_closedir(&fsdir);

    //
    // Open disk images
    //
    for (unsigned int drive = 0; drive < num_drives && num_files < NUM_FILES; drive++) {
        char drive_letter = 'A' + drive;
        char buf[22];
        sprintf(buf, "%s/DRIVE%c.DSK", fileinfo.fname, drive_letter);
        if (f_open(&files[num_files], buf, FA_READ|FA_WRITE) == FR_OK) {
            printf("Image file %s/DRIVE%c.DSK is assigned to drive %c\n\r",
                   fileinfo.fname, drive_letter, drive_letter);
            drives[drive].filep = &files[num_files];
            num_files++;
        }
    }
    if (drives[0].filep == NULL) {
        printf("No boot disk.\n\r");
        return -4;
    }

    return 0;
}

void start_z80(void)
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
    CLCIN4PPS = 0x05;   // RA5 <- /RD

    // 1,2,5,6 = Port A, C
    // 3,4,7,8 = Port B, D
    RA4PPS = 0x01;       // CLC1 -> RA4 -> /OE
    RA2PPS = 0x02;       // CLC2 -> RA2 -> /WE
    RD7PPS = 0x03;       // CLC3 -> RD7 -> /WAIT

    //========== CLC1 /OE ==========
    CLCSELECT = 0;       // CLC1 select

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

    // CLC VI enable
    CLC3IF = 0;          // Clear the CLC interrupt flag
    CLC3IE = 1;          // Enabling CLC3 interrupt

    // Z80 start
    U3RXIE = 1;          // Receiver interrupt enable
    GIE = 1;             // Global interrupt enable
    LATE0 = 1;           // /BUSREQ=1
    LATE1 = 1;           // Release reset
}
