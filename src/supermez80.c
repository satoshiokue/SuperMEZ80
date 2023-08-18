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
#include <utils.h>

static FATFS fs;
static DIR fsdir;
static FILINFO fileinfo;
static FIL files[NUM_FILES];
static int num_files = 0;
uint8_t tmp_buf[2][TMP_BUF_SIZE];
debug_t debug = {
    0,  // disk
    0,  // disk_read
    0,  // disk_write
    0,  // disk_verbose
    0,  // disk_mask
};

// global variable which is handled by board dependent stuff
int turn_on_io_led = 0;

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
int disk_init(void);
int menu_select(void);
void start_z80(void);

// main routine
void main(void)
{
    sys_init();
    if (disk_init() < 0)
        while (1);
    io_init();
    mem_init();
    mon_init();

    U3RXIE = 1;          // Receiver interrupt enable
    GIE = 1;             // Global interrupt enable

    //
    // Transfer ROM image to the SRAM
    //
    dma_write_to_sram(0x00000, rom, sizeof(rom));

#if !defined(CPM_MMU_EXERCISE)
    if (menu_select() < 0)
        while (1);
#endif  // !CPM_MMU_EXERCISE

    //
    // Start Z80
    //
    if (NCO1EN) {
        printf("Use NCO %.2f MHz for Z80 clock\n\r", ((uint32_t)NCO1INC * 61 / 2) / 1000000.0);
    } else {
        printf("Use RA3 external clock for Z80\n\r");
    }
    printf("\n\r");
    start_z80();

    while(1) {
        // Wait for IO access
        board_wait_io_event();
        io_handle();
    }
}

void bus_master(int enable)
{
    board_bus_master(enable);
}

void sys_init()
{
    board_init();
    board_sys_init();
}

int disk_init(void)
{
    if (f_mount(&fs, "0://", 1) != FR_OK) {
        printf("Failed to mount SD Card.\n\r");
        return -2;
    }

    return 0;
}

int menu_select(void)
{
    int i;
    unsigned int drive;

    //
    // Select disk image folder
    //
    if (f_opendir(&fsdir, "/")  != FR_OK) {
        printf("Failed to open SD Card..\n\r");
        return -3;
    }
 restart:
    i = 0;
    int selection = -1;
    f_rewinddir(&fsdir);
    while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
        if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
            strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
            printf("%d: %s\n\r", i, fileinfo.fname);
            if (strcmp(fileinfo.fname, "CPMDISKS") == 0)
                selection = i;
            i++;
        }
    }
    printf("M: Monitor prompt\n\r");
    if (1 < i) {
        printf("Select: ");
        while (1) {
            uint8_t c = (uint8_t)getch_buffered();  // Wait for input char
            if ('0' <= c && c <= '9' && c - '0' < i) {
                selection = c - '0';
                break;
            }
            if (c == 'm' || c == 'M') {
                printf("M\n\r");
                while (mon_prompt() != MON_CMD_EXIT);
                goto restart;
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
    for (drive = 0; drive < num_drives && num_files < NUM_FILES; drive++) {
        char drive_letter = (char)('A' + drive);
        char * const buf = (char *)tmp_buf[0];
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
    board_start_z80();
}
