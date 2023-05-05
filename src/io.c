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

#include <supermez80.h>
#include <stdio.h>
#include <ff.h>

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
const int num_drives = (sizeof(drives)/sizeof(*drives));

// hardware control
static uint8_t hw_ctrl_lock = HW_CTRL_LOCKED;

// key input buffer
static char key_input_buffer[80];
static unsigned int key_input = 0;
static unsigned int key_input_buffer_head = 0;

static uint8_t disk_buf[SECTOR_SIZE];

// UART3 Transmit
void putch(char c) {
    while(!U3TXIF);             // Wait or Tx interrupt flag set
    U3TXB = c;                  // Write data
}

// UART3 Recive
char getch(void) {
    char res;
    while (1) {
        if (0 < key_input) {
            res = key_input_buffer[key_input_buffer_head];
            key_input_buffer_head = ((key_input_buffer_head + 1) % sizeof(key_input_buffer));
            key_input--;
            U3RXIE = 1;         // Receiver interrupt enable
            break;
        }
        if (U3RXIF) {           // Wait for Rx interrupt flag set
            res = U3RXB;        // Read data
            break;
        }
    }
    return res;
}

void ungetch(char c) {
    if ((sizeof(key_input_buffer) * 4 / 5) <= key_input) {
        // Disable key input interrupt if key buffer is full
        U3RXIE = 0;
        return;
    }
    key_input_buffer[(key_input_buffer_head + key_input) % sizeof(key_input_buffer)] = c;
    key_input++;
}

uint8_t hw_ctrl_read(void)
{
    return hw_ctrl_lock;
}

void hw_ctrl_write(uint8_t val)
{
    if (hw_ctrl_lock != HW_CTRL_UNLOCKED && val != HW_CTRL_MAGIC)
        return;
    if (val == HW_CTRL_MAGIC) {
        hw_ctrl_lock = HW_CTRL_UNLOCKED;
        return;
    }
    if (val & HW_CTRL_RESET) {
        printf("\n\rReset by IO port %02XH\n\r", HW_CTRL);
        RESET();
    }
    if (val & HW_CTRL_HALT) {
        printf("\n\rHALT by IO port %02XH\n\r", HW_CTRL);
        while (1);
    }
}

// Called at UART3 receiving some data
void __interrupt(irq(default),base(8)) Default_ISR(){
    GIE = 0;                    // Disable interrupt

    // Read UART input if Rx interrupt flag is set
    if (U3RXIF) {
        uint8_t c = U3RXB;
        if (c != 0x00) {        // Save input key to the buffer if the input is not a break key
            ungetch(c);
            goto enable_interupt_return;
        }

        // Break key was received.
        // Attempts to become a bassmaster.
        #ifdef CPM_MON_DEBUG
        printf("\n\rAttempts to become a bassmaster ...\n\r");
        #endif

        if (CLC3IF) {           // Check /IORQ
            #ifdef CPM_MON_DEBUG
            printf("/IORQ is active\n\r");
            #endif
            // Let I/O handler to handle the break key if /IORQ is detected if /IORQ is detected
            invoke_monitor = 1;
            goto enable_interupt_return;
        }

        LATE0 = 0;              // set /BUSREQ to active
        __delay_us(20);         // Wait a while for Z80 to release the bus
        if (CLC3IF) {           // Check /IORQ again
            // Withdraw /BUSREQ and let I/O handler to handle the break key if /IORQ is detected
            // if /IORQ is detected
            LATE0 = 1;
            invoke_monitor = 1;
            #ifdef CPM_MON_DEBUG
            printf("Withdraw /BUSREQ because /IORQ is active\n\r");
            #endif
            goto enable_interupt_return;
        }

        bus_master(1);
        mon_setup();            // Hook NMI handler and assert /NMI
        bus_master(0);
        LATE0 = 1;              // Clear /BUSREQ so that the Z80 can handle NMI
    }

 enable_interupt_return:
    GIE = 1;                    // Enable interrupt
}

// Called at WAIT falling edge(Immediately after Z80 IORQ falling)
void __interrupt(irq(CLC3),base(8)) CLC_ISR() {
    static uint8_t disk_drive = 0;
    static uint8_t disk_track = 0;
    static uint16_t disk_sector = 0;
    static uint8_t disk_op = 0;
    static uint8_t disk_dmal = 0;
    static uint8_t disk_dmah = 0;
    static uint8_t disk_stat = DISK_ST_ERROR;
    static uint8_t *disk_datap = NULL;
    uint8_t c;
    union address_bus_u ab;

    GIE = 0;                    // Disable interrupt

    ab.l = PORTB;               // Read address low

    // Z80 IO read cycle
    if(!RA5) {
    TRISC = 0x00;               // Set as output
    switch (ab.l) {
    case UART_CREG:
        if (key_input) {
            LATC = 0xff;        // input available
        } else {
            LATC = 0x00;        // no input available
        }
        break;
    case UART_DREG:
        c = getch();
        if (c == 0x00) {
            invoke_monitor = 1;
        }
        LATC = c;               // Out the character
        break;
    case DISK_REG_DATA:
        if (disk_datap && (disk_datap - disk_buf) < SECTOR_SIZE) {
            LATC = *disk_datap++;
        } else {
            #ifdef CPM_DISK_DEBUG
            printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x (READ IGNORED)\n\r", disk_op,
               disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal);
            #endif
        }
        break;
    case DISK_REG_FDCST:
        LATC = disk_stat;
        break;
    case HW_CTRL:
        LATC = hw_ctrl_read();
        break;
    default:
        #ifdef CPM_IO_DEBUG
        printf("WARNING: unknown I/O read %d (%02XH)\n\r", ab.l, ab.l);
        invoke_monitor = 1;
        #endif
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

    GIE = 1;                    // Enable interrupt
    return;
    }

    // Z80 IO write cycle
    int do_bus_master = 0;
    int led_on = 0;
    int io_addr = ab.l;
    int io_data = PORTC;
    switch (ab.l) {
    case UART_DREG:
        while(!U3TXIF);
        U3TXB = PORTC;      // Write into    U3TXB
        break;
    case DISK_REG_DATA:
        if (disk_datap && (disk_datap - disk_buf) < SECTOR_SIZE) {
            *disk_datap++ = PORTC;
            if (DISK_OP_WRITE == DISK_OP_WRITE && (disk_datap - disk_buf) == SECTOR_SIZE) {
                do_bus_master = 1;
            }
        } else {
            #ifdef CPM_DISK_DEBUG
            printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x DATA=%02x (IGNORED)\n\r", disk_op,
               disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal, PORTC);
            #endif
        }
        break;
    case DISK_REG_DRIVE:
        disk_drive = PORTC;
        break;
    case DISK_REG_TRACK:
        disk_track = PORTC;
        break;
    case DISK_REG_SECTOR:
        disk_sector = (disk_sector & 0xff00) | PORTC;
        break;
    case DISK_REG_SECTORH:
        disk_sector = (disk_sector & 0x00ff) | (PORTC << 8);
        break;
    case DISK_REG_FDCOP:
        disk_op = PORTC;
        if (disk_op == DISK_OP_WRITE) {
            disk_datap = disk_buf;
        } else {
            do_bus_master = 1;
        }
        #ifdef CPM_DISK_DEBUG_VERBOSE
        printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x ...\n\r", disk_op,
               disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal);
        #endif
        break;
    case DISK_REG_DMAL:
        disk_dmal = PORTC;
        break;
    case DISK_REG_DMAH:
        disk_dmah = PORTC;
        break;
    case MMU_INIT:
        mmu_bank_config(PORTC);
        break;
    case MMU_BANK_SEL:
        do_bus_master = 1;
        break;
    case HW_CTRL:
        hw_ctrl_write(PORTC);
        break;
    case MON_ENTER:
    case MON_RESTORE:
    case MON_BREAK:
        do_bus_master = 1;
        break;
    default:
        #ifdef CPM_IO_DEBUG
        printf("WARNING: unknown I/O write %d, %d (%02XH, %02XH)\n\r", ab.l, PORTC, ab.l, PORTC);
        invoke_monitor = 1;
        #endif
        break;
    }
    if (!do_bus_master && !invoke_monitor) {
        // Release wait (D-FF reset)
        G3POL = 1;
        G3POL = 0;
        CLC3IF = 0;         // Clear interrupt flag
        GIE = 1;            // Enable interrupt
        return;
    }

    //
    // Do something as the bus master
    //
    LATE0 = 0;          // /BUSREQ is active
    G3POL = 1;          // Release wait (D-FF reset)
    G3POL = 0;

    bus_master(1);

    if (!do_bus_master) {
        goto io_exit;
    }

    switch (io_addr) {
    case MMU_BANK_SEL:
        mmu_bank_select(io_data);
        goto io_exit;
    case MON_ENTER:
    case MON_BREAK:
        mon_enter(io_addr == MON_ENTER /* NMI or not*/);
        while (!mon_step_execution && !mon_prompt());
        mon_leave();
        goto io_exit;
    case MON_RESTORE:
        mon_restore();
        goto io_exit;
    }

    //
    // Do disk I/O
    //

    // turn on the LED
    led_on = 1;
    #ifdef GPIO_LED
    mcp23s08_write(MCP23S08_ctx, GPIO_LED, 0);
    #endif

    if (num_drives <= disk_drive || drives[disk_drive].filep == NULL) {
        disk_stat = DISK_ST_ERROR;
        goto disk_io_done;
    }

    uint32_t sector = disk_track * drives[disk_drive].sectors + disk_sector - 1;
    FIL *filep = drives[disk_drive].filep;
    unsigned int n;
    if (f_lseek(filep, sector * SECTOR_SIZE) != FR_OK) {
        printf("f_lseek(): ERROR\n\r");
        disk_stat = DISK_ST_ERROR;
        goto disk_io_done;
    }
    if (disk_op == DISK_OP_DMA_READ || disk_op == DISK_OP_READ) {
        //
        // DISK read
        //

        // read from the DISK
        if (f_read(filep, disk_buf, SECTOR_SIZE, &n) != FR_OK) {
            printf("f_read(): ERROR\n\r");
            disk_stat = DISK_ST_ERROR;
            goto disk_io_done;
        }

        #ifdef CPM_DISK_DEBUG_VERBOSE
        util_hexdump_sum("buf: ", disk_buf, SECTOR_SIZE);
        #endif

        if (disk_op == DISK_OP_DMA_READ) {
            //
            // DMA read
            //
            // transfer read data to SRAM
            uint32_t addr = ((uint32_t)mmu_bank << 16) | ((uint16_t)disk_dmah << 8) | disk_dmal;
            dma_write_to_sram(addr, disk_buf, SECTOR_SIZE);
            disk_datap = NULL;

            #ifdef CPM_MEM_DEBUG
            // read back the SRAM
            uint32_t addr = ((uint32_t)mmu_bank << 16) | ((uint16_t)disk_dmah << 8) | disk_dmal;
            printf("f_read(): SRAM address: %04x\n\r", addr);
            dma_read_from_sram(addr, disk_buf, SECTOR_SIZE);
            util_hexdump_sum("RAM: ", disk_buf, SECTOR_SIZE);
            #endif  // CPM_MEM_DEBUG
        } else {
            //
            // non DMA read
            //

            // just set the read pointer to the heat of the buffer
            disk_datap = disk_buf;
        }

        disk_stat = DISK_ST_SUCCESS;

    } else
    if (disk_op == DISK_OP_DMA_WRITE || disk_op == DISK_OP_WRITE) {
        //
        // DISK write
        //
        if (disk_op == DISK_OP_DMA_WRITE) {
            //
            // DMA write
            //
            // transfer write data from SRAM to the buffer
            uint32_t addr = ((uint32_t)mmu_bank << 16) | ((uint16_t)disk_dmah << 8) | disk_dmal;
            dma_read_from_sram(addr, disk_buf, SECTOR_SIZE);
        } else {
            //
            // non DMA write
            //

            // writing data 128 bytes are in the buffer already
        }

        // write buffer to the DISK
        if (f_write(filep, disk_buf, SECTOR_SIZE, &n) != FR_OK) {
            printf("f_write(): ERROR\n\r");
            disk_stat = DISK_ST_ERROR;
            goto disk_io_done;
        }

        disk_stat = DISK_ST_SUCCESS;
        disk_datap = NULL;

    } else {
        disk_stat = DISK_ST_ERROR;
        disk_datap = NULL;
    }

 disk_io_done:
    #ifdef CPM_DISK_DEBUG
    printf("DISK: OP=%02x D/T/S=%d/%d/%d ADDR=%02x%02x ... ST=%02x\n\r", disk_op,
           disk_drive, disk_track, disk_sector, disk_dmah, disk_dmal, disk_stat);
    #endif

    #ifdef GPIO_LED
    if (led_on)  // turn off the LED
        mcp23s08_write(MCP23S08_ctx, GPIO_LED, 1);
    #endif

 io_exit:
    if (invoke_monitor) {
        invoke_monitor = 0;
        mon_setup();
    }

    bus_master(0);
    LATE0 = 1;              // /BUSREQ is deactive

    CLC3IF = 0;             // Clear interrupt flag
    GIE = 1;                // Enable interrupt
}
