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
#define CPM_IO_DEBUG
//#define CPM_MMU_DEBUG
//#define CPM_MMU_EXERCISE
//#define CPM_MON_DEBUG

#define Z80_CLK 6000000UL       // Z80 clock frequency(Max 16MHz)

#define SPI_CLOCK_100KHZ 10     // Determined by actual measurement
#define SPI_CLOCK_2MHZ   0      // Maximum speed w/o any wait (1~2 MHz)
#define NUM_FILES        6
#define SECTOR_SIZE      128
#define TMP_BUF_SIZE     256

#define MEM_CHECK_UNIT   TMP_BUF_SIZE * 16 // 2 KB
#define MAX_MEM_SIZE     0x00100000        // 1 MB
#define HIGH_ADDR_MASK   0xffffc000
#define LOW_ADDR_MASK    0x00003fff

#define GPIO_CS0    0
#define GPIO_CS1    1
#define GPIO_BANK1  2
#define GPIO_BANK2  3
#define GPIO_NMI    4
#define GPIO_A14    5
#define GPIO_A15    6
#define GPIO_BANK0  7

//
// Constant value definitions
//

#define UART_DREG 0x01          // Data REG
#define UART_CREG 0x00          // Control REG
#define DISK_REG_DATA    8      // fdc-port: data (non-DMA)
#define DISK_REG_DRIVE   10     // fdc-port: # of drive
#define DISK_REG_TRACK   11     // fdc-port: # of track
#define DISK_REG_SECTOR  12     // fdc-port: # of sector
#define DISK_REG_FDCOP   13     // fdc-port: command
#define DISK_OP_DMA_READ     0
#define DISK_OP_DMA_WRITE    1
#define DISK_OP_READ         2
#define DISK_OP_WRITE        3
#define DISK_REG_FDCST   14     // fdc-port: status
#define DISK_ST_SUCCESS      0x00
#define DISK_ST_ERROR        0x01
#define DISK_REG_DMAL    15     // dma-port: dma address low
#define DISK_REG_DMAH    16     // dma-port: dma address high
#define DISK_REG_SECTORH 17     // fdc-port: # of sector high

#define MMU_INIT         20     // MMU initialisation
#define MMU_BANK_SEL     21     // MMU bank select
#define MMU_SEG_SIZE     22     // MMU select segment size (in pages a 256 bytes)
#define MMU_WR_PROT      23     // MMU write protect/unprotect common memory segment

#define HW_CTRL          160    // hardware control
#define HW_CTRL_LOCKED       0xff
#define HW_CTRL_UNLOCKED     0x00
#define HW_CTRL_MAGIC        0xaa
#define HW_CTRL_RESET        (1 << 6)
#define HW_CTRL_HALT         (1 << 7)

#define MON_CLEANUP      170    // clean up monitor mode
#define MON_NMI_PREP     171    // NMI preparation
#define MON_NMI_ENTER    172    // NMI monitor
#define MON_RST08_PREP   173    // RST08 preparation
#define MON_RST08_ENTER  174    // RST08 monitor

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

void bus_master(int enable);

// io
extern char getch(void);
extern void ungetch(char c);
extern drive_t drives[];
extern const int num_drives;
extern unsigned int io_output_chars;
extern int cpm_disk_read(unsigned int drive, uint32_t lba, void *buf, unsigned int sectors);
extern int cpm_trsect_to_lba(unsigned int drive, unsigned int track, unsigned int sector,
                             uint32_t *lba);
extern int cpm_trsect_from_lba(unsigned int drive, unsigned int *track, unsigned int *sector,
                               uint32_t lba);

// monitor
extern int invoke_monitor;
extern unsigned int mon_step_execution;

void mon_init(void);
void mon_assert_nmi(void);
void mon_setup(void);
void mon_prepare(int nmi);
void mon_enter(int nmi);
int mon_prompt(void);
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
extern void dma_acquire_addrbus(uint32_t addr);
extern void dma_release_addrbus(void);
extern void dma_write_to_sram(uint32_t dest, const void *buf, unsigned int len);
extern void dma_read_from_sram(uint32_t src, void *buf, unsigned int len);
extern void mmu_bank_config(int nbanks);
extern void mmu_bank_select(int bank);

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
