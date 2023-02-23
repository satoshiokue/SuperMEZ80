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
#include <picconfig.h>

#include <xc.h>
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <SDCard.h>
#include <utils.h>

//#define CPM_DISK_DEBUG
//#define CPM_DISK_DEBUG_VERBOSE
//#define CPM_MEM_DEBUG

#define Z80_CLK 6000000UL       // Z80 clock frequency(Max 16MHz)

#define UART_DREG 0x01          // Data REG
#define UART_CREG 0x00          // Control REG
#define DISK_REG_DRIVE   10     // fdc-port: # of drive
#define DISK_REG_TRACK   11     // fdc-port: # of track
#define DISK_REG_SECTOR  12     // fdc-port: # of sector
#define DISK_REG_FDCOP   13     // fdc-port: command
#define DISK_REG_FDCST   14     // fdc-port: status
#define DISK_REG_DMAL    15     // dma-port: dma address low
#define DISK_REG_DMAH    16     // dma-port: dma address high

#define SPI_CLOCK_100KHZ 10     // Determined by actual measurement
#define SPI_CLOCK_2MHZ 0        // Maximum speed w/o any wait (1~2 MHz)
#define NUM_FILES 8
#define SECTOR_SIZE 128

// Z80 ROM equivalent, see end of this file
extern const unsigned char rom[];
static FATFS fs;
static FIL file;
static uint8_t buf[SECTOR_SIZE];

typedef struct {
    unsigned int sectors;
    FIL *filep;
} drive_t;
drive_t drives[] = {
    { 26 },
    { 26 },
    { 26 },
    { 26 },
    { 0 },
    { 0 },
    { 0 },
    { 0 },
    { 128 },
    { 128 },
    { 128 },
    { 128 },
    { 0 },
    { 0 },
    { 0 },
    { 16484 },
};
FIL files[NUM_FILES];
int num_files = 0;

#define NUM_DRIVES (sizeof(drives)/sizeof(*drives))

const unsigned char rom[] = {
// Initial program loader at 0x0000
//#include "ipl.inc"
// Modified boot sector at 0x0000
#include "boot.inc"
};

// Address Bus
union {
unsigned int w;                 // 16 bits Address
    struct {
        unsigned char l;        // Address low
        unsigned char h;        // Address high
    };
} ab;

// UART3 Transmit
void putch(char c) {
    while(!U3TXIF);             // Wait or Tx interrupt flag set
    U3TXB = c;                  // Write data
}

// Never called, logically
void __interrupt(irq(default),base(8)) Default_ISR(){}

// Called at WAIT falling edge(Immediately after Z80 IORQ falling)
void __interrupt(irq(CLC3),base(8)) CLC_ISR() {
    static uint8_t disk_drive = 0;
    static uint8_t disk_track = 0;
    static uint8_t disk_sector = 0;
    static uint8_t disk_op = 0;
    static uint8_t disk_dmal = 0;
    static uint8_t disk_dmah = 0;
    static uint8_t disk_stat = 0;

    ab.l = PORTB;               // Read address low

    // Z80 IO write cycle
    if(RA5) {
        int do_disk_io = 0;

        switch (ab.l) {
        case UART_DREG:
            while(!U3TXIF);
            U3TXB = PORTC;      // Write into    U3TXB
            break;
        case DISK_REG_DRIVE:
            disk_drive = PORTC;
            break;
        case DISK_REG_TRACK:
            disk_track = PORTC;
            break;
        case DISK_REG_SECTOR:
            disk_sector = PORTC;
            break;
        case DISK_REG_FDCOP:
            disk_op = PORTC;
            do_disk_io = 1;
            #ifdef CPM_DISK_DEBUG_VERBOSE
            printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x ...\n\r", disk_op,
                   disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal, PORTC);
            #endif
            break;
        case DISK_REG_DMAL:
            disk_dmal = PORTC;
            break;
        case DISK_REG_DMAH:
            disk_dmah = PORTC;
            break;
        }
        if (!do_disk_io) {
            // Release wait (D-FF reset)
            G3POL = 1;
            G3POL = 0;
            CLC3IF = 0;         // Clear interrupt flag
            return;
        }

        //
        // Do disk I/O
        //
        LATE0 = 0;          // /BUSREQ is active
        RA4PPS = 0x00;      // unbind CLC1 and /OE (RA4)
        RA2PPS = 0x00;      // unbind CLC2 and /WE (RA2)
        LATA4 = 1;          // deactivate /OE
        LATA2 = 1;          // deactivate /WE

        G3POL = 1;          // Release wait (D-FF reset)
        G3POL = 0;

        if (NUM_DRIVES <= disk_drive || drives[disk_drive].filep == NULL) {
            disk_stat = 0xff;   // error
            goto disk_io_done;
        }

        // Set address bus as output
        TRISD = 0x00;       // A15-A8 pin (A14:/RFSH, A15:/WAIT)
        TRISB = 0x00;       // A7-A0

        uint32_t sector = disk_track * drives[disk_drive].sectors + disk_sector - 1;
        FIL *filep = drives[disk_drive].filep;
        unsigned int n;
        if (f_lseek(filep, sector * SECTOR_SIZE) != FR_OK) {
            printf("f_lseek(): ERROR\n\r");
            disk_stat = 0xff;   // error
            goto disk_io_done;
        }
        if (disk_op == 0) {
            //
            // DISK read
            //

            // read from the DISK
            if (f_read(filep, buf, SECTOR_SIZE, &n) != FR_OK) {
                printf("f_read(): ERROR\n\r");
                disk_stat = 0xff;  // error
                goto disk_io_done;
            }

            // transfer read data to SRAM
            uint16_t addr = ((uint16_t)disk_dmah << 8) | disk_dmal;
            TRISC = 0x00;       // Set as output to write to the SRAM
            for(int i = 0; i < SECTOR_SIZE; i++) {
                ab.w = addr;
                LATD = ab.h;
                LATB = ab.l;
                addr++;
                LATA2 = 0;      // activate /WE
                LATC = buf[i];
                LATA2 = 1;      // deactivate /WE
            }

            #ifdef CPM_DISK_DEBUG_VERBOSE
            util_hexdump_sum("buf: ", buf, SECTOR_SIZE);
            #endif

            #ifdef CPM_MEM_DEBUG
            // read back the SRAM
            uint16_t addr = ((uint16_t)disk_dmah << 8) | disk_dmal;
            printf("f_read(): SRAM address: %04x\n\r", addr);
            TRISC = 0xff;       // Set as input to read from the SRAM
            for(int i = 0; i < SECTOR_SIZE; i++) {
                ab.w = addr;
                LATD = ab.h;
                LATB = ab.l;
                addr++;
                LATA4 = 0;      // activate /OE
                for (int j = 0; j < 50; j++)
                    asm("nop");
                buf[i] = PORTC;
                LATA4 = 1;      // deactivate /OE
            }
            util_hexdump_sum("RAM: ", buf, SECTOR_SIZE);
            #endif  // CPM_MEM_DEBUG
        } else {
            //
            // DISK write
            //

            // transfer write data from SRAM to the buffer
            uint16_t addr = ((uint16_t)disk_dmah << 8) | disk_dmal;
            TRISC = 0xff;       // Set as input to read from the SRAM
            for(int i = 0; i < SECTOR_SIZE; i++) {
                ab.w = addr;
                LATD = ab.h;
                LATB = ab.l;
                addr++;
                LATA4 = 0;      // activate /OE
                buf[i] = LATC;
                LATA4 = 1;      // deactivate /OE
            }

            // write buffer to the DISK
            if (f_write(filep, buf, SECTOR_SIZE, &n) != FR_OK || n != strlen(buf)) {
                printf("f_write(): ERROR\n\r");
                disk_stat = 0xff;  // error
                goto disk_io_done;
            }
        }

        disk_stat = 0x00;       // disk I/O succeeded

    disk_io_done:
        #ifdef CPM_DISK_DEBUG
        printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x ... ST=%02x\n\r", disk_op,
               disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal, disk_stat);
        #endif

        // Set address bus as input
        TRISD = 0x7f;           // A15-A8 pin (A14:/RFSH, A15:/WAIT)
        TRISB = 0xff;           // A7-A0 pin
        TRISC = 0xff;           // D7-D0 pin

        RA4PPS = 0x01;          // CLC1 -> RA4 -> /OE
        RA2PPS = 0x02;          // CLC2 -> RA2 -> /WE

        for (int j = 0; j < 50; j++)
            asm("nop");

        LATE0 = 1;              // /BUSREQ is deactive

        CLC3IF = 0;             // Clear interrupt flag
        return;
    }

    // Z80 IO read cycle
    TRISC = 0x00;               // Set as output
    switch (ab.l) {
    case UART_CREG:
        LATC = PIR9;            // Out PIR9
        break;
    case UART_DREG:
        while (!U3RXIF);        // Wait for Rx interrupt flag set
        LATC = U3RXB;           // Out U3RXB
        break;
    case DISK_REG_FDCST:
        LATC = disk_stat;
        break;
    default:
        LATC = 0xff;            // Invalid data
        break;
    }

    // Release wait (D-FF reset)
    G3POL = 1;
    G3POL = 0;

    // Post processing
#if 1
    while(!RA0);                // /IORQ <5.6MHz
#else
    while(!RD7);                // /WAIT >=5.6MHz
#endif
    TRISC = 0xff;               // Set as input
    CLC3IF = 0;                 // Clear interrupt flag
}

// main routine
void main(void) {

    unsigned int i;

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
    TRISD = 0x00;       // Set as output

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

    printf("\n\r");
    //
    // Initialize SD Card
    //
    SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_2MHZ, /* timeout */ 100);
    if (f_mount(&fs, "0://", 1) == FR_OK) {
        //
        // Open disk images
        //
        for (unsigned int drive = 0; drive < NUM_DRIVES && num_files < NUM_FILES; drive++) {
            char drive_letter = 'A' + drive;
            sprintf(buf, "CPMDISKS/DRIVE%c.DSK", drive_letter);
            if (f_open(&files[num_files], buf, FA_READ|FA_WRITE) == FR_OK) {
                printf("Image file DRIVE%c.DSK is assigned to drive %c\n\r",
                       drive_letter, drive_letter);
                drives[drive].filep = &files[num_files];
                num_files++;
            }
        }
    }
    if (drives[0].filep == NULL) {
        printf("No boot disk.\n\r");
        while (1);
    }

    //
    // Transfer ROM image to the SRAM
    //
    for(i = 0; i < sizeof(rom); i++) {
        ab.w = i;
        LATD = ab.h;
        LATB = ab.l;
        LATA2 = 0;      // /WE=0
        LATC = rom[i];
        LATA2 = 1;      // /WE=1
    }

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
    GIE = 1;             // Global interrupt enable
    LATE0 = 1;           // /BUSREQ=1
    LATE1 = 1;           // Release reset

    printf("\n\r");

    while(1);  // All things come to those who wait
}
