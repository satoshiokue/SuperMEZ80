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
#include <assert.h>

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

static void con_flush_buffer(void);

// UART3 Transmit
void putch(char c) {
    con_flush_buffer();
    while(!U3TXIF);             // Wait or Tx interrupt flag set
    U3TXB = c;                  // Write data
}

// UART3 Recive
int getch(void) {
    while(!U3RXIF);             // Wait for Rx interrupt flag set
    return U3RXB;               // Read data
}

// Shall be called with disabling the interrupt
static void __con_flush_buffer(void) {
    while (0 < con_output && U3TXIF) {
        U3TXB = con_output_buffer[con_output_buffer_head];
        con_output_buffer_head = ((con_output_buffer_head + 1) % sizeof(con_output_buffer));
        con_output--;
    }
}

static void con_flush_buffer(void) {
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

static uint8_t disk_stat = DISK_ST_ERROR;

void io_handle() {
    static uint8_t disk_drive = 0;
    static uint8_t disk_track = 0;
    static uint16_t disk_sector = 0;
    static uint8_t disk_op = 0;
    static uint8_t disk_dmal = 0;
    static uint8_t disk_dmah = 0;
    static uint8_t *disk_datap = NULL;
    static unsigned int prev_output_chars = 0;
    static unsigned int io_output_chars = 0;
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
        printf("WARNING: unknown I/O read %d (%02XH)\n\r", io_addr, io_addr);
        invoke_monitor = 1;
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
    board_clear_io_event();     // Clear interrupt flag
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
        printf("WARNING: unknown I/O write %d, %d (%02XH, %02XH)\n\r", io_addr, io_data, io_addr,
               io_data);
        invoke_monitor = 1;
        break;
    }

    //
    // Assert /BUSREQ and release /WAIT
    //
    set_busrq_pin(0);           // /BUSREQ is active
    board_clear_io_event();     // Clear interrupt flag
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
        // new line if some output from the target
        if (prev_output_chars != io_output_chars) {
            printf("\n\r");
            prev_output_chars = io_output_chars;
        }
        mon_enter();
        io_stat_ = IO_STAT_MONITOR;
        while (!mon_step_execution && mon_prompt() != MON_CMD_EXIT);
        mon_leave();
        io_stat_ = IO_STAT_INTERRUPTED;
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

        // Store disk I/O status here so that io_invoke_target_cpu() can return the status in it
        disk_stat = DISK_ST_SUCCESS;

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
    set_busrq_pin(1);       // /BUSREQ is deactive

    io_stat_ = IO_STAT_RUNNING;
}

int io_wait_write(uint8_t wait_io_addr, uint8_t *result_io_data)
{
    int done = 0;
    int result = -1;
    uint8_t io_addr;
    uint8_t io_data;

    #ifdef CPM_IO_DEBUG
    printf("%s: %3d      (%02XH     ) ...\n\r", __func__, wait_io_addr, wait_io_addr);
    #endif

    assert(io_stat() == IO_STAT_NOT_STARTED ||
           io_stat() == IO_STAT_STOPPED || io_stat() == IO_STAT_INTERRUPTED ||
           io_stat() == IO_STAT_PREPINVOKE || io_stat() == IO_STAT_MONITOR);

    bus_master(0);
    set_busrq_pin(1);       // /BUSREQ is deactive

    while (1) {
        // Wait for IO access
        board_wait_io_event();
        if (!board_io_event())
            continue;

        io_addr = addr_l_pins();
        io_data = data_pins();

        if (rd_pin() == 0 && io_addr == DISK_REG_FDCST) {
            //
            // This might be a dirty hack. But works well?
            //
            #ifdef CPM_IO_DEBUG
            printf("%s: %3d      (%02XH     ) ... disk_stat=%02Xh\n\r", __func__,
                   wait_io_addr, wait_io_addr, disk_stat);
            #endif
            // Let Z80 read the data
            set_busrq_pin(0);           // Activate /BUSRQ
            set_data_dir(0x00);         // Set as output
            set_data_pins(disk_stat);
            board_clear_io_event();     // Clear interrupt flag
            set_wait_pin(1);            // Release /WAIT
            while(!ioreq_pin());        // wait for /IORQ to be cleared
            set_data_dir(0xff);         // Set as input
            set_busrq_pin(1);           // Release /BUSRQ
            continue;
        }

        if (rd_pin() == 0 || (io_addr != UART_DREG && io_addr != wait_io_addr)) {
            // something wrong
            printf("%s: ERROR: I/O %5s %3d, %3d (%02XH, %02XH) while waiting for %d (%02XH)\n\r",
                   __func__, rd_pin() == 0 ? "read" : "write",
                   io_addr, io_data, io_addr, io_data, wait_io_addr, wait_io_addr);
            invoke_monitor = 1;
            break;
        }

        if (io_addr == wait_io_addr) {
            if (result_io_data)
                *result_io_data = io_data;
            result = 0;
            break;
        }

        // write to UART_DREG
        putch_buffered(io_data);
        board_clear_io_event(); // Clear interrupt flag
        set_wait_pin(1);        // Release wait
    }

    set_busrq_pin(0);           // /BUSREQ is active
    board_clear_io_event();     // Clear interrupt flag
    set_wait_pin(1);            // Release wait
    while(!ioreq_pin());        // wait for /IORQ to be cleared
    bus_master(1);

    #ifdef CPM_IO_DEBUG
    printf("%s: %3d, %3d (%02XH, %02XH) ... result=%d\n\r", __func__, io_addr, io_data,
           io_addr, io_data,result);
    #endif

    return result;
}

void io_invoke_target_cpu_prepare(int *saved_status)
{
    static const unsigned char dummy_rom[] = {
        // Dummy program, infinite HALT loop that do nothing
        #include "dummy.inc"
    };

    assert(io_stat() == IO_STAT_NOT_STARTED ||
           io_stat() == IO_STAT_STOPPED || io_stat() == IO_STAT_MONITOR);

    *saved_status = io_stat();
    if (io_stat() == IO_STAT_MONITOR) {
        return;
    }

    if (io_stat() == IO_STAT_NOT_STARTED) {
        // Start Z80 as DMA helper
        bus_master(1);
        __write_to_sram(bank_phys_addr(0, 0x00000), dummy_rom, sizeof(dummy_rom));
        board_start_z80();
        set_bank_pins(bank_phys_addr(0, 0x00000));
        io_wait_write(TGTINV_TRAP, NULL);
    }

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_setup()\n\r", __func__);
    #endif
    bus_master(1);
    mon_setup();            // Hook NMI handler and assert /NMI

    io_wait_write(MON_PREPARE, NULL);

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_prepare()\n\r", __func__);
    #endif
    mon_prepare();          // Install the trampoline code
    io_wait_write(MON_ENTER, NULL);
    io_stat_ = IO_STAT_PREPINVOKE;

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_enter()\n\r", __func__);
    #endif
    mon_enter();            // Now we can use the trampoline
    io_stat_ = IO_STAT_MONITOR;

    return;
}

int io_invoke_target_cpu(const param_block_t *inparams, unsigned int ninparams,
                         const param_block_t *outparams, unsigned int noutparams, int bank)
{
    int i;
    uint8_t result_data;

    assert(io_stat() == IO_STAT_MONITOR);
    mon_use_zeropage(bank);

    for (i = 0; i < ninparams; i++) {
        __write_to_sram(bank_phys_addr(bank, inparams[i].offs), inparams[i].addr,
                        inparams[i].len);
    }

    // Run the code
    io_wait_write(TGTINV_TRAP, &result_data);

    for (i = 0; i < noutparams; i++) {
        __read_from_sram(bank_phys_addr(bank, outparams[i].offs), outparams[i].addr,
                         outparams[i].len);
    }

    return (int)(signed char)result_data;
}

void io_invoke_target_cpu_teardown(int *saved_status)
{
    if (*saved_status == IO_STAT_MONITOR || *saved_status == IO_STAT_INVALID) {
        return;
    }

    assert(io_stat() == IO_STAT_MONITOR);

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_leave()\n\r", __func__);
    #endif
    mon_leave();
    io_stat_ = IO_STAT_INTERRUPTED;

    io_wait_write(MON_CLEANUP, NULL);

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_cleanup() ...\n\r", __func__);
    #endif
    mon_cleanup();

    #ifdef CPM_IO_DEBUG
    printf("%s: mon_cleanup() ... done\n\r", __func__);
    #endif
    io_stat_ = IO_STAT_STOPPED;

    if (*saved_status == IO_STAT_NOT_STARTED) {
        set_reset_pin(0);
        bus_master(1);
        io_stat_ = IO_STAT_NOT_STARTED;
    }

    *saved_status = IO_STAT_INVALID;  // fail safe
}
