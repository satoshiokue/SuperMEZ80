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
#include <utils.h>

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
unsigned int io_output_chars = 0;
static int io_stat_ = IO_STAT_INVALID;

// hardware control
static uint8_t hw_ctrl_lock = HW_CTRL_LOCKED;

// console input/output buffers
static char key_input_buffer[80];
static unsigned int key_input = 0;
static unsigned int key_input_buffer_head = 0;
static char con_output_buffer[80];
static unsigned int con_output = 0;
static unsigned int con_output_buffer_head = 0;

static uint8_t disk_buf[SECTOR_SIZE];

void io_init(void) {
    io_stat_ = IO_STAT_NOT_STARTED;
}

int io_stat(void) {
    return io_stat_;
}

// UART3 Transmit
void putch(char c) {
    while(!U3TXIF);             // Wait or Tx interrupt flag set
    U3TXB = c;                  // Write data
}

// UART3 Recive
int getch(void) {
    while(!U3RXIF);             // Wait for Rx interrupt flag set
    return U3RXB;               // Read data
}

// Shall be called with disabling the interrupt
void __con_flush_buffer(void) {
    while (0 < con_output && U3TXIF) {
        U3TXB = con_output_buffer[con_output_buffer_head];
        con_output_buffer_head = ((con_output_buffer_head + 1) % sizeof(con_output_buffer));
        con_output--;
    }
}

void con_flush_buffer(void) {
    while (0 < con_output)
        __delay_ms(50);
}

void putch_buffered(char c) {
    GIE = 0;                    // Disable interrupt
    if (sizeof(con_output_buffer) <= con_output) {
        while(!U3TXIF);
        __con_flush_buffer();
    }
    con_output_buffer[((con_output_buffer_head + con_output) % sizeof(con_output_buffer))] = c;
    con_output++;
    U3TXIE = 1;                 // Enable Tx interrupt
    GIE = 1;                    // Enable interrupt
}

char getch_buffered(void) {
    char res;
    while (1) {
        GIE = 0;                // Disable interrupt
        if (0 < key_input) {
            res = key_input_buffer[key_input_buffer_head];
            key_input_buffer_head = ((key_input_buffer_head + 1) % sizeof(key_input_buffer));
            key_input--;
            U3RXIE = 1;         // Enable Rx interrupt
            GIE = 1;            // Enable interrupt
            return res;
        }
        if (invoke_monitor) {
            GIE = 1;            // Enable interrupt
            return 0;           // This input is dummy to escape Z80 from  IO read instruction
                                // and might be a garbage. Sorry.
        }
        GIE = 1;                // Enable interrupt
        __delay_us(1000);
    }

    // not reached
    return res;
}

void ungetch(char c) {
    GIE = 0;                    // Disable interrupt
    if (key_input < sizeof(key_input_buffer)) {
        key_input_buffer[(key_input_buffer_head + key_input) % sizeof(key_input_buffer)] = c;
        key_input++;
    }
    if ((sizeof(key_input_buffer) * 4 / 5) <= key_input) {
        // Disable key input interrupt if key buffer is full
        U3RXIE = 0;
    }
    GIE = 1;                    // Enable interrupt
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

int cpm_disk_read(unsigned int drive, uint32_t lba, void *buf, unsigned int sectors)
{
    unsigned int n;
    int result = 0;
    int track, sector;

    if (num_drives <= drive || drives[drive].filep == NULL) {
        printf("invalid drive %d\n\r", drive);
        return -1;
    }

    FIL *filep = drives[drive].filep;
    FRESULT fres;
    if ((fres = f_lseek(filep, lba * SECTOR_SIZE)) != FR_OK) {
        printf("f_lseek(): ERROR %d\n\r", fres);
        return -1;
    }
    while (result < sectors) {
        if ((fres = f_read(filep, buf, SECTOR_SIZE, &n)) != FR_OK || n != SECTOR_SIZE) {
            printf("f_read(): ERROR res=%d, n=%d\n\r", fres, n);
            return result;
        }
        buf = (void*)((uint8_t*) + SECTOR_SIZE);
        lba++;
        result++;
    }
    return result;
}

int cpm_trsect_to_lba(unsigned int drive, unsigned int track, unsigned int sector, uint32_t *lba)
{
    if (lba)
        *lba = 0xdeadbeefUL;  // fail safe
    if (num_drives <= drive || drives[drive].filep == NULL) {
        printf("invalid drive %d\n\r", drive);
        return -1;
    }
    if (lba)
        *lba = track * drives[drive].sectors + sector - 1;
    return 0;
}

int cpm_trsect_from_lba(unsigned int drive, unsigned int *track, unsigned int *sector,
                        uint32_t lba)
{
    if (track)
        *track = 0xdead;  // fail safe
    if (sector)
        *sector = 0xbeef;  // fail safe
    if (num_drives <= drive || drives[drive].filep == NULL) {
        printf("invalid drive %d\n\r", drive);
        return -1;
    }
    if (track)
        *track =  (unsigned int)(lba / drives[drive].sectors);
    if (sector)
        *sector = (unsigned int)(lba % drives[drive].sectors + 1);
    return 0;
}

// Called at UART3 receiving some data
void __interrupt(irq(default),base(8)) Default_ISR(){
    // Read UART input if Rx interrupt flag is set
    if (U3RXIF) {
        uint8_t c = U3RXB;
        if (c != 0x00) {        // Save input key to the buffer if the input is not a break key
            ungetch(c);
        } else {
            invoke_monitor = 1;
        }
    }

    // Write UART output if some chars exists in the buffer and Tx interrupt flag is set
    __con_flush_buffer();

    // Disable Tx interrupt if the buffer is empty
    if (con_output == 0)
        U3TXIE = 0;
}

void try_to_invoke_monitor(void) {
        if (!invoke_monitor)
            return;

        // Break key was received.
        // Attempts to become a bassmaster.
        #ifdef CPM_MON_DEBUG
        printf("\n\rAttempts to become a bassmaster ...\n\r");
        #endif

        if (board_io_event()) { // Check /IORQ
            #ifdef CPM_MON_DEBUG
            printf("/IORQ is active\n\r");
            #endif
            // Let I/O handler to handle the break key if /IORQ is detected if /IORQ is detected
            return;
        }

        set_busrq_pin(0);       // set /BUSREQ to active
        __delay_us(20);         // Wait a while for Z80 to release the bus
        if (board_io_event()) { // Check /IORQ again
            // Withdraw /BUSREQ and let I/O handler to handle the break key if /IORQ is detected
            set_busrq_pin(1);
            #ifdef CPM_MON_DEBUG
            printf("Withdraw /BUSREQ because /IORQ is active\n\r");
            #endif
            return;
        }

        invoke_monitor = 0;
        bus_master(1);
        mon_setup();            // Hook NMI handler and assert /NMI
        bus_master(0);
        set_busrq_pin(1);       // Clear /BUSREQ so that the Z80 can handle NMI
}

void io_handle() {
    static uint8_t disk_drive = 0;
    static uint8_t disk_track = 0;
    static uint16_t disk_sector = 0;
    static uint8_t disk_op = 0;
    static uint8_t disk_dmal = 0;
    static uint8_t disk_dmah = 0;
    static uint8_t disk_stat = DISK_ST_ERROR;
    static uint8_t *disk_datap = NULL;
    uint8_t c;

    try_to_invoke_monitor();

    if (!board_io_event())        // Nothing to do and just return if no IO access is occurring
        return;

    int do_bus_master = 0;
    uint8_t io_addr = addr_l_pins();
    uint8_t io_data = data_pins();

    if (rd_pin()) {
        io_stat_ = IO_STAT_WRITE_WAITING;
        goto io_write;
    }

    io_stat_ = IO_STAT_READ_WAITING;

    // Z80 IO read cycle
    set_data_dir(0x00);           // Set as output
    switch (io_addr) {
    case UART_CREG:
        if (key_input) {
            set_data_pins(0xff);  // input available
        } else {
            set_data_pins(0x00);  // no input available
        }
        break;
    case UART_DREG:
        con_flush_buffer();
        c = getch_buffered();
        set_data_pins(c);         // Out the character
        break;
    case DISK_REG_DATA:
        if (disk_datap && (disk_datap - disk_buf) < SECTOR_SIZE) {
            set_data_pins(*disk_datap++);
        } else
        if (DEBUG_DISK) {
            printf("DISK: OP=%02x D/T/S=%d/%3d/%3d            ADDR=%01x%02x%02x (RD IGNORED)\n\r",
                   disk_op, disk_drive, disk_track, disk_sector, mmu_bank, disk_dmah, disk_dmal);
        }
        break;
    case DISK_REG_FDCST:
        set_data_pins(disk_stat);
        break;
    case HW_CTRL:
        set_data_pins(hw_ctrl_read());
        break;
    default:
        #ifdef CPM_IO_DEBUG
        printf("WARNING: unknown I/O read %d (%02XH)\n\r", io_addr, io_addr);
        invoke_monitor = 1;
        #endif
        set_data_pins(0xff);    // Invalid data
        break;
    }

    // Assert /NMI for invoking the monitor before releasing /WAIT.
    // You can use SPI bus because Z80 is in I/O read instruction and does not drive D0~7 here.
    if (invoke_monitor) {
        mon_assert_nmi();
    }

    // Let Z80 read the data
    set_busrq_pin(0);           // /BUSREQ is active
    set_wait_pin(1);            // Release wait
    while(!ioreq_pin());        // wait for /IORQ to be cleared
    set_data_dir(0xff);         // Set as input

    if (invoke_monitor) {
        goto enter_bus_master;
    } else {
        goto withdraw_busreq;
    }

 io_write:
    // Z80 IO write cycle
    switch (io_addr) {
    case UART_DREG:
        putch_buffered(io_data);
        io_output_chars++;
        break;
    case DISK_REG_DATA:
        if (disk_datap && (disk_datap - disk_buf) < SECTOR_SIZE) {
            *disk_datap++ = io_data;
            if (DISK_OP_WRITE == DISK_OP_WRITE && (disk_datap - disk_buf) == SECTOR_SIZE) {
                do_bus_master = 1;
            }
        } else
        if (DEBUG_DISK) {
            printf("DISK: OP=%02x D/T/S=%d/%3d/%3d            ADDR=%01x%02x%02x (WR IGNORED)\n\r",
                   disk_op, disk_drive, disk_track, disk_sector, mmu_bank, disk_dmah, disk_dmal);
        }
        break;
    case DISK_REG_DRIVE:
        disk_drive = io_data;
        break;
    case DISK_REG_TRACK:
        disk_track = io_data;
        break;
    case DISK_REG_SECTOR:
        disk_sector = (disk_sector & 0xff00) | io_data;
        break;
    case DISK_REG_SECTORH:
        disk_sector = (disk_sector & 0x00ff) | ((uint16_t)io_data << 8);
        break;
    case DISK_REG_FDCOP:
        disk_op = io_data;
        if (disk_op == DISK_OP_WRITE) {
            disk_datap = disk_buf;
        } else {
            do_bus_master = 1;
        }
        if ((DEBUG_DISK_READ  && (disk_op == DISK_OP_DMA_READ  || disk_op == DISK_OP_READ )) ||
            (DEBUG_DISK_WRITE && (disk_op == DISK_OP_DMA_WRITE || disk_op == DISK_OP_WRITE))) {
            if (DEBUG_DISK_VERBOSE && !(debug.disk_mask & (1 << disk_drive))) {
                printf("DISK: OP=%02x D/T/S=%d/%3d/%3d            ADDR=%01x%02x%02x ... \n\r",
                       disk_op, disk_drive, disk_track, disk_sector,
                       mmu_bank, disk_dmah, disk_dmal);
            }
        }
        break;
    case DISK_REG_DMAL:
        disk_dmal = io_data;
        break;
    case DISK_REG_DMAH:
        disk_dmah = io_data;
        break;
    case MMU_INIT:
    case MMU_BANK_SEL:
        do_bus_master = 1;
        break;
    case HW_CTRL:
        hw_ctrl_write(io_data);
        break;
    case MON_PREPARE:
    case MON_ENTER:
    case MON_CLEANUP:
        do_bus_master = 1;
        break;
    default:
        #ifdef CPM_IO_DEBUG
        printf("WARNING: unknown I/O write %d, %d (%02XH, %02XH)\n\r", io_addr, io_data, io_addr,
               io_data);
        invoke_monitor = 1;
        #endif
        break;
    }

    //
    // Assert /BUSREQ and release /WAIT
    //
    set_busrq_pin(0);           // /BUSREQ is active
    set_wait_pin(1);            // Release wait
    while(!ioreq_pin());        // wait for /IORQ to be cleared

    io_stat_ = IO_STAT_STOPPED;

    if (!do_bus_master && !invoke_monitor) {
        goto withdraw_busreq;
    }

 enter_bus_master:
    //
    // Do something as the bus master
    //
    bus_master(1);

    if (!do_bus_master) {
        goto exit_bus_master;
    }

    switch (io_addr) {
    case MMU_INIT:
        mmu_bank_config(io_data);
        goto exit_bus_master;
    case MMU_BANK_SEL:
        mmu_bank_select(io_data);
        goto exit_bus_master;
    case MON_PREPARE:
        mon_prepare();
        io_stat_ = IO_STAT_INTERRUPTED;
        goto exit_bus_master;
    case MON_ENTER:
        io_stat_ = IO_STAT_INTERRUPTED;
        mon_enter();
        while (!mon_step_execution && mon_prompt() != MON_CMD_EXIT);
        mon_leave();
        goto exit_bus_master;
    case MON_CLEANUP:
        io_stat_ = IO_STAT_INTERRUPTED;
        mon_cleanup();
        io_stat_ = IO_STAT_STOPPED;
        goto exit_bus_master;
    }

    //
    // Do disk I/O
    //

    // turn on the LED
    turn_on_io_led = 1;

    uint32_t sector = 0;
    if (num_drives <= disk_drive || drives[disk_drive].filep == NULL) {
        disk_stat = DISK_ST_ERROR;
        goto disk_io_done;
    }

    sector = disk_track * drives[disk_drive].sectors + disk_sector - 1;
    FIL *filep = drives[disk_drive].filep;
    unsigned int n;
    FRESULT fres;
    if ((fres = f_lseek(filep, sector * SECTOR_SIZE)) != FR_OK) {
        printf("f_lseek(): ERROR %d\n\r", fres);
        disk_stat = DISK_ST_ERROR;
        goto disk_io_done;
    }
    if (disk_op == DISK_OP_DMA_READ || disk_op == DISK_OP_READ) {
        //
        // DISK read
        //

        // read from the DISK
        if ((fres = f_read(filep, disk_buf, SECTOR_SIZE, &n)) != FR_OK || n != SECTOR_SIZE) {
            printf("f_read(): ERROR res=%d, n=%d\n\r", fres, n);
            disk_stat = DISK_ST_ERROR;
            goto disk_io_done;
        }

        if (DEBUG_DISK_READ && DEBUG_DISK_VERBOSE && !(debug.disk_mask & (1 << disk_drive))) {
            util_hexdump_sum("buf: ", disk_buf, SECTOR_SIZE);
        }

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

        if (DEBUG_DISK_WRITE && DEBUG_DISK_VERBOSE && !(debug.disk_mask & (1 << disk_drive))) {
            util_hexdump_sum("buf: ", disk_buf, SECTOR_SIZE);
        }

        // write buffer to the DISK
        if ((fres = f_write(filep, disk_buf, SECTOR_SIZE, &n)) != FR_OK || n != SECTOR_SIZE) {
            printf("f_write(): ERROR res=%d, n=%d\n\r", fres, n);
            disk_stat = DISK_ST_ERROR;
            goto disk_io_done;
        }
        if ((fres = f_sync(filep)) != FR_OK) {
            printf("f_sync(): ERROR %d\n\r", fres);
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
    if (((DEBUG_DISK_READ  && (disk_op == DISK_OP_DMA_READ  || disk_op == DISK_OP_READ )) ||
         (DEBUG_DISK_WRITE && (disk_op == DISK_OP_DMA_WRITE || disk_op == DISK_OP_WRITE))) &&
        !(debug.disk_mask & (1 << disk_drive))) {
        printf("DISK: OP=%02x D/T/S=%d/%3d/%3d x%3d=%5ld ADDR=%01x%02x%02x ... ST=%02x\n\r",
               disk_op, disk_drive, disk_track, disk_sector, drives[disk_drive].sectors, sector,
               mmu_bank, disk_dmah, disk_dmal, disk_stat);
    }

    turn_on_io_led = 0;

 exit_bus_master:
    if (invoke_monitor) {
        invoke_monitor = 0;
        mon_setup();
    }

    io_stat_ = IO_STAT_RESUMING;
    bus_master(0);

 withdraw_busreq:
    board_clear_io_event(); // Clear interrupt flag
    set_busrq_pin(1);       // /BUSREQ is deactive

    io_stat_ = IO_STAT_RUNNING;
}
