/*
 * UART, disk I/O and monitor firmware for SuperMEZ80-SPI
 *
 * Based on main.c by Tetsuya Suzuki and emuz80_z80ram.c by Satoshi Okue
 * Modified by @hanyazou https://twitter.com/hanyazou
 */
#ifndef __SUPERMEZ80_H__
#define __SUPERMEZ80_H__

#include <picconfig.h>
#include <xc.h>
#include <stdint.h>
#include <ff.h>

//
// Configlations
//

#define ENABLE_DISK_DEBUG
//#define CPM_MEM_DEBUG
//#define CPM_IO_DEBUG
//#define CPM_MMU_DEBUG
//#define CPM_MEMCPY_DEBUG
//#define CPM_MMU_EXERCISE
//#define CPM_MON_DEBUG

// Z80 clock frequency (select one or use external clock)
//#define Z80_CLK_HZ 499712UL         //  0.5 MHz (NCOxINC = 0x04000, 64MHz/64/2)
//#define Z80_CLK_HZ 999424UL         //  1.0 MHz (NCOxINC = 0x08000, 64MHz/32/2)
//#define Z80_CLK_HZ 1998848UL        //  2.0 MHz (NCOxINC = 0x10000, 64MHz/16/2)
//#define Z80_CLK_HZ 3997696UL        //  4.0 MHz (NCOxINC = 0x20000, 64MHz/8/2)
//#define Z80_CLK_HZ 4568778UL        //  4.6 MHz (NCOxINC = 0x24924, 64MHz/7/2)
//#define Z80_CLK_HZ 5330241UL        //  5.3 MHz (NCOxINC = 0x2AAAA, 64MHz/6/2)
#define Z80_CLK_HZ 6396277UL        //  6.4 MHz (NCOxINC = 0x33333, 64MHz/5/2)
//#define Z80_CLK_HZ 7995392UL        //  8.0 MHz (NCOxINC = 0x40000, 64MHz/4/2)
//#define Z80_CLK_HZ 10660482UL       // 10.7 MHz (NCOxINC = 0x55555, 64MHz/3/2)
//#define Z80_CLK_HZ 12792615UL       //  12.8 MHz (NCOxINC = 0x66666, 64MHz/5)
//#define Z80_CLK_HZ 15990784UL       // 16.0 MHz (NCOxINC = 0x80000, 64MHz/2/2)

//#define Z80_USE_M1_FOR_SRAM_OE

#define SPI_CLOCK_100KHZ 10     // Determined by actual measurement
#define SPI_CLOCK_2MHZ   0      // Maximum speed w/o any wait (1~2 MHz)
#define NUM_FILES        6
#define SECTOR_SIZE      128
#define TMP_BUF_SIZE     256

#define MEM_CHECK_UNIT   TMP_BUF_SIZE * 16 // 2 KB
#define MAX_MEM_SIZE     0x00100000        // 1 MB

//
// Constant value definitions
//

#define UART_DREG 0x01          // 01h Data REG
#define UART_CREG 0x00          // 00h Control REG
#define DISK_REG_DATA    8      // 08h fdc-port: data (non-DMA)
#define DISK_REG_DRIVE   10     // 0Ah fdc-port: # of drive
#define DISK_REG_TRACK   11     // 0Bh fdc-port: # of track
#define DISK_REG_SECTOR  12     // 0Ch fdc-port: # of sector
#define DISK_REG_FDCOP   13     // 0Dh fdc-port: command
#define DISK_OP_DMA_READ     0
#define DISK_OP_DMA_WRITE    1
#define DISK_OP_READ         2
#define DISK_OP_WRITE        3
#define DISK_REG_FDCST   14     // OEh fdc-port: status
#define DISK_ST_SUCCESS      0x00
#define DISK_ST_ERROR        0x01
#define DISK_REG_DMAL    15     // OFh dma-port: dma address low
#define DISK_REG_DMAH    16     // 10h dma-port: dma address high
#define DISK_REG_SECTORH 17     // 11h fdc-port: # of sector high

#define MMU_INIT         20     // 14h MMU initialisation
#define MMU_BANK_SEL     21     // 15h MMU bank select
#define MMU_SEG_SIZE     22     // 16h MMU select segment size (in pages a 256 bytes)
#define MMU_WR_PROT      23     // 17h MMU write protect/unprotect common memory segment

#define HW_CTRL          160    // A0h hardware control
#define HW_CTRL_LOCKED       0xff
#define HW_CTRL_UNLOCKED     0x00
#define HW_CTRL_MAGIC        0xaa
#define HW_CTRL_RESET        (1 << 6)
#define HW_CTRL_HALT         (1 << 7)

#define MON_CLEANUP      170    // AAh clean up monitor mode
#define MON_PREPARE      171    // ABh prepare monitor mode
#define MON_ENTER        172    // ACh clean up monitor mode
#define TGTINV_TRAP      173    // ADh return from target CPU invocation

#define MMU_INVALID_BANK 0xff

#define MON_CMD_OK   0
#define MON_CMD_EXIT 1
#define MON_CMD_ERROR -1

//
// Type definitions
//

// Address Bus
union address_bus_u {
    unsigned int w;             // 16 bits Address
    struct {
        unsigned char l;        // Address low
        unsigned char h;        // Address high
    };
};

typedef struct {
    unsigned int sectors;
    FIL *filep;
} drive_t;

typedef struct {
    uint8_t disk;
    uint8_t disk_read;
    uint8_t disk_write;
    uint8_t disk_verbose;
    uint16_t disk_mask;
} debug_t;

//
// Global variables and function prototypes
//

extern uint8_t tmp_buf[2][TMP_BUF_SIZE];
extern debug_t debug;
extern int turn_on_io_led;

void bus_master(int enable);

// io
enum {
    IO_STAT_INVALID       = 0,
    IO_STAT_NOT_STARTED   = 10,
    IO_STAT_RUNNING       = 20,
    IO_STAT_READ_WAITING  = 30,
    IO_STAT_WRITE_WAITING = 40,
    IO_STAT_STOPPED       = 50,
    IO_STAT_RESUMING      = 60,
    IO_STAT_INTERRUPTED   = 70,
    IO_STAT_PREPINVOKE    = 80,
    IO_STAT_MONITOR       = 90
};
extern void io_init(void);
extern int io_stat(void);
extern int getch(void);
extern char getch_buffered(void);
extern void ungetch(char c);
extern void putch_buffered(char c);
extern drive_t drives[];
extern const int num_drives;
extern unsigned int io_output_chars;
extern void io_handle(void);
extern int cpm_disk_read(unsigned int drive, uint32_t lba, void *buf, unsigned int sectors);
extern int cpm_trsect_to_lba(unsigned int drive, unsigned int track, unsigned int sector,
                             uint32_t *lba);
extern int cpm_trsect_from_lba(unsigned int drive, unsigned int *track, unsigned int *sector,
                               uint32_t lba);
extern void io_invoke_target_cpu_prepare(int *saved_status);
extern int io_invoke_target_cpu(const void *code, unsigned int len, const void *params,
                               unsigned int plen);
extern void io_invoke_target_cpu_teardown(int *saved_status);

// monitor
extern int invoke_monitor;
extern unsigned int mon_step_execution;

void mon_init(void);
void mon_assert_nmi(void);
void mon_setup(void);
void mon_prepare(void);
void mon_enter(void);
int mon_prompt(void);
void mon_destroy_trampoline(void);
void mon_leave(void);
void mon_cleanup(void);

// memory
extern int mmu_bank;
extern int mmu_num_banks;
extern uint32_t mmu_mem_size;
extern void (*mmu_bank_select_callback)(int from, int to);
extern void (*mmu_bank_config_callback)(void);

extern void mem_init(void);
#define bank_phys_addr(bank, addr) (((uint32_t)(bank) << 16) + (addr))
#define phys_addr(addr) bank_phys_addr(mmu_bank, (addr))
extern void set_bank_pins(uint32_t addr);
extern void dma_write_to_sram(uint32_t dest, const void *buf, unsigned int len);
extern void dma_read_from_sram(uint32_t src, void *buf, unsigned int len);
extern void __write_to_sram(uint32_t dest, const void *buf, unsigned int len);
extern void __read_from_sram(uint32_t src, const void *buf, unsigned int len);
extern void mmu_bank_config(int nbanks);
extern void mmu_bank_select(int bank);

// board
extern void board_init(void);
extern void (*board_sys_init_hook)(void);
#define board_sys_init() (*board_sys_init_hook)()
extern void (*board_bus_master_hook)(int enable);
#define board_bus_master(enable) (*board_bus_master_hook)(enable)
extern void (*board_start_z80_hook)(void);
#define board_start_z80() (*board_start_z80_hook)()
extern void (*board_set_bank_pins_hook)(uint32_t addr);
#define set_bank_pins(addr) (*board_set_bank_pins_hook)(addr)
extern void (*board_setup_addrbus_hook)(uint32_t addr);
#define board_setup_addrbus(addr) (*board_setup_addrbus_hook)(addr)
extern uint32_t (*board_high_addr_mask_hook)(void);
#define board_high_addr_mask(addr) (*board_high_addr_mask_hook)()
extern uint16_t (*board_low_addr_mask_hook)(void);
#define board_low_addr_mask(addr) (*board_low_addr_mask_hook)()
extern void (*board_write_to_sram_hook)(uint16_t addr, uint8_t *buf, unsigned int len);
#define board_write_to_sram(addr, buf, len) (*board_write_to_sram_hook)(addr, buf, len)
extern void (*board_read_from_sram_hook)(uint16_t addr, uint8_t *buf, unsigned int len);
#define board_read_from_sram(addr, buf, len) (*board_read_from_sram_hook)(addr, buf, len)
extern __bit (*board_io_event_hook)(void);
#define board_io_event() (*board_io_event_hook)()
extern void (*board_wait_io_event_hook)(void);
#define board_wait_io_event() (*board_wait_io_event_hook)()
extern void (*board_clear_io_event_hook)(void);
#define board_clear_io_event() (*board_clear_io_event_hook)()

// Address read and write
extern uint8_t (*board_addr_l_pins_hook)(void);
#define addr_l_pins() (*board_addr_l_pins_hook)()
extern void (*board_set_addr_l_pins_hook)(uint8_t);
#define set_addr_l_pins(v) (*board_set_addr_l_pins_hook)(v)

// Data read and write
extern uint8_t (*board_data_pins_hook)(void);
#define data_pins() (*board_data_pins_hook)()
extern void (*board_set_data_pins_hook)(uint8_t);
#define set_data_pins(v) (*board_set_data_pins_hook)(v)
extern void (*board_set_data_dir_hook)(uint8_t);
#define set_data_dir(v) (*board_set_data_dir_hook)(v)

// IOREQ read only
extern __bit (*board_ioreq_pin_hook)(void);
#define ioreq_pin() (*board_ioreq_pin_hook)()
// MEMRQ read only
extern __bit (*board_memrq_pin_hook)(void);
#define memrq_pin() (*board_memrq_pin_hook)()
// RD    read only
extern __bit (*board_rd_pin_hook)(void);
#define rd_pin() (*board_rd_pin_hook)()

// BUSRQ write olny
extern void (*board_set_busrq_pin_hook)(uint8_t);
#define set_busrq_pin(v) (*board_set_busrq_pin_hook)(v)
// RESET write olny
extern void (*board_set_reset_pin_hook)(uint8_t);
#define set_reset_pin(v) (*board_set_reset_pin_hook)(v)
// NMI   write olny
extern void (*board_set_nmi_pin_hook)(uint8_t);
#define set_nmi_pin(v) (*board_set_nmi_pin_hook)(v)
// INT   write olny
extern void (*board_set_int_pin_hook)(uint8_t);
#define set_int_pin(v) (*board_set_int_pin_hook)(v)
// WAIT  write olny
extern void (*board_set_wait_pin_hook)(uint8_t);
#define set_wait_pin(v) (*board_set_wait_pin_hook)(v)

#include "chk_borad_dpend.h"

//
// debug macros
//
#ifdef ENABLE_DISK_DEBUG
#define DEBUG_DISK (debug.disk || debug.disk_read || debug.disk_write || debug.disk_verbose)
#define DEBUG_DISK_READ (debug.disk_read)
#define DEBUG_DISK_WRITE (debug.disk_write)
#define DEBUG_DISK_VERBOSE (debug.disk_verbose)
#else
#define DEBUG_DISK 0
#define DEBUG_READ 0
#define DEBUG_WRITE 0
#define DEBUG_DISK_VERBOSE 0
#endif

#endif  // __SUPERMEZ80_H__
