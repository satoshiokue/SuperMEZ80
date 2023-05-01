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
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include <SDCard.h>
#include <SPI.h>
#include <mcp23s08.h>
#include <utils.h>
#include <disas.h>
#include <disas_z80.h>

//#define CPM_DISK_DEBUG
//#define CPM_DISK_DEBUG_VERBOSE
//#define CPM_MEM_DEBUG
#define CPM_IO_DEBUG
//#define CPM_MMU_DEBUG
//#define CPM_MON_DEBUG

#define Z80_CLK 6000000UL       // Z80 clock frequency(Max 16MHz)

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

#define MON_ENTER        170    // enter monitor mode
#define MON_RESTORE      171    // clean up monitor mode
#define MON_BREAK        172    // hit break point

#define SPI_CLOCK_100KHZ 10     // Determined by actual measurement
#define SPI_CLOCK_2MHZ   0      // Maximum speed w/o any wait (1~2 MHz)
#define NUM_FILES        8
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

// Z80 ROM equivalent, see end of this file
extern const unsigned char rom[];
static FATFS fs;
static DIR fsdir;
static FILINFO fileinfo;
static FIL file;
static uint8_t disk_buf[SECTOR_SIZE];
static uint8_t tmp_buf[2][TMP_BUF_SIZE];

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
#include "ipl.inc"
};

const unsigned char mon[] = {
// NMI monitor at 0x0000
#include "nmimon.inc"
};

const unsigned char mon_rstmon[] = {
// break point interrupt entry at 0x0008
#include "rstmon.inc"
};

// Address Bus
union {
unsigned int w;                 // 16 bits Address
    struct {
        unsigned char l;        // Address low
        unsigned char h;        // Address high
    };
} ab;

// Saved Z80 Context
struct z80_context {
    uint16_t pc;
    uint16_t sp;
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;
    uint16_t ix;
    uint16_t iy;
    uint8_t saved_prog[2];
    uint8_t nmi;
} z80_context;
uint32_t mon_cur_addr = 0;
int invoke_monitor = 0;
uint32_t mon_bp_addr;
uint8_t mon_bp_installed = 0;
uint8_t mon_bp_saved_inst;
unsigned int mon_step_execution = 0;
const char *mon_prompt_str = "MON>";

// MMU
int mmu_bank = 0;
uint32_t mmu_num_banks = 0;
uint32_t mmu_mem_size = 0;

// hardware control
uint8_t hw_ctrl_lock = HW_CTRL_LOCKED;

// key input buffer
char key_input_buffer[8];
unsigned int key_input = 0;
unsigned int key_input_buffer_head = 0;

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

void acquire_addrbus(uint32_t addr)
{
    static int no_mcp23s08_warn = 1;

    if (no_mcp23s08_warn && (addr & HIGH_ADDR_MASK) != 0) {
        no_mcp23s08_warn = 0;
        if (!mcp23s08_is_alive(MCP23S08_ctx)) {
            printf("WARNING: no GPIO expander to control higher address\n\r");
        }
    }
    mcp23s08_write(MCP23S08_ctx, GPIO_A14, ((addr >> 14) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A14, MCP23S08_PINMODE_OUTPUT);
    mcp23s08_write(MCP23S08_ctx, GPIO_A15, ((addr >> 15) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A15, MCP23S08_PINMODE_OUTPUT);
    #ifdef GPIO_BANK0
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK0, ((addr >> 16) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK0, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK1
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK1, ((addr >> 17) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK1, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK2
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK2, ((addr >> 18) & 1));
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK2, MCP23S08_PINMODE_OUTPUT);
    #endif
}

void release_addrbus(void)
{
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A14, MCP23S08_PINMODE_INPUT);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A15, MCP23S08_PINMODE_INPUT);

    // higher address lines must always be driven by MCP23S08
    #ifdef GPIO_BANK0
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK0, ((mmu_bank >> 0) & 1));
    #endif
    #ifdef GPIO_BANK1
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK1, ((mmu_bank >> 1) & 1));
    #endif
    #ifdef GPIO_BANK2
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK2, ((mmu_bank >> 2) & 1));
    #endif
}

void dma_write_to_sram(uint32_t dest, void *buf, int len)
{
    uint16_t addr = (dest & LOW_ADDR_MASK);
    uint16_t second_half = 0;
    int i;

    if ((uint32_t)LOW_ADDR_MASK + 1 < (uint32_t)addr + len)
        second_half = (uint16_t)(((uint32_t)addr + len) - ((uint32_t)LOW_ADDR_MASK + 1));

    acquire_addrbus(dest);
    TRISC = 0x00;       // Set as output to write to the SRAM
    for(i = 0; i < len - second_half; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA2 = 0;      // activate /WE
        LATC = ((uint8_t*)buf)[i];
        LATA2 = 1;      // deactivate /WE
    }

    if (0 < second_half)
        acquire_addrbus(dest + i);
    for( ; i < len; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA2 = 0;      // activate /WE
        LATC = ((uint8_t*)buf)[i];
        LATA2 = 1;      // deactivate /WE
    }

    release_addrbus();
}

void dma_read_from_sram(uint32_t src, void *buf, int len)
{
    uint16_t addr = (src & LOW_ADDR_MASK);
    uint16_t second_half = 0;
    int i;

    if ((uint32_t)LOW_ADDR_MASK + 1 < (uint32_t)addr + len)
        second_half = (uint16_t)(((uint32_t)addr + len) - ((uint32_t)LOW_ADDR_MASK + 1));

    acquire_addrbus(src);
    TRISC = 0xff;       // Set as input to read from the SRAM
    for(i = 0; i < len - second_half; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA4 = 0;      // activate /OE
        ((uint8_t*)buf)[i] = PORTC;
        LATA4 = 1;      // deactivate /OE
    }

    if (0 < second_half)
        acquire_addrbus(src + i);
    for( ; i < len; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA4 = 0;      // activate /OE
        ((uint8_t*)buf)[i] = PORTC;
        LATA4 = 1;      // deactivate /OE
    }

    release_addrbus();
}

void mmu_bank_config(int nbanks)
{
    #ifdef CPM_MMU_DEBUG
    printf("mmu_bank_config: %d\n\r", nbanks);
    #endif
    if (mmu_num_banks < nbanks)
        printf("WARNING: too many banks requested. (request is %d)\n\r", nbanks);
}

void mmu_bank_select(int bank)
{
    #ifdef CPM_MMU_DEBUG
    printf("mmu_bank_select: %d\n\r", bank);
    #endif
    if (mmu_bank == bank)
        return;
    if (mmu_num_banks <= bank) {
        printf("ERROR: bank %d is not available.\n\r", bank);
        while (1);
    }
    mmu_bank = bank;
    release_addrbus();  // set higher address pins
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

uint16_t read_mcu_mem_w(void *addr)
{
    uint8_t *p = (uint8_t *)addr;
    return ((p[1] << 8) + p[0]);
}

void write_mcu_mem_w(void *addr, uint16_t val)
{
    uint8_t *p = (uint8_t *)addr;
    *p++ = ((val >> 0) & 0xff);
    *p++ = ((val >> 8) & 0xff);
}

void mon_show_registers(void)
{
    printf("PC: %04X  ", z80_context.pc);
    printf("SP: %04X  ", z80_context.sp);
    printf("AF: %04X  ", z80_context.af);
    printf("BC: %04X  ", z80_context.bc);
    printf("DE: %04X  ", z80_context.de);
    printf("HL: %04X  ", z80_context.hl);
    printf("IX: %04X  ", z80_context.ix);
    printf("IY: %04X\n\r", z80_context.iy);
    printf("MMU: bank %02X  ", mmu_bank);
    printf("STATUS: %c%c%c%c%c%c%c%c",
           (z80_context.af & 0x80) ? 'S' : '-',
           (z80_context.af & 0x40) ? 'Z' : '-',
           (z80_context.af & 0x20) ? 'X' : '-',
           (z80_context.af & 0x10) ? 'H' : '-',
           (z80_context.af & 0x08) ? 'X' : '-',
           (z80_context.af & 0x04) ? 'P' : '-',
           (z80_context.af & 0x02) ? 'N' : '-',
           (z80_context.af & 0x01) ? 'C' : '-');
    printf("\n\r");
}

void mon_setup(void)
{
    dma_read_from_sram(((uint32_t)mmu_bank << 16), tmp_buf[1], sizeof(mon));
    dma_write_to_sram((uint32_t)mmu_bank << 16, mon, sizeof(mon));
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 0);
}

void mon_enter(int nmi)
{
    int stack_addr;
    printf("\n\r");
    #ifdef CPM_MON_DEBUG
    printf("Enter monitor\n\r");
    #endif
    if (nmi) {
        stack_addr = sizeof(mon);
    } else {
        stack_addr = sizeof(mon_rstmon);
    }
    z80_context.nmi = nmi;

    dma_read_from_sram((uint32_t)mmu_bank << 16, tmp_buf[0], stack_addr);
    z80_context.sp = read_mcu_mem_w(&tmp_buf[0][stack_addr - 2]);
    z80_context.af = read_mcu_mem_w(&tmp_buf[0][stack_addr - 4]);
    z80_context.bc = read_mcu_mem_w(&tmp_buf[0][stack_addr - 6]);
    z80_context.de = read_mcu_mem_w(&tmp_buf[0][stack_addr - 8]);
    z80_context.hl = read_mcu_mem_w(&tmp_buf[0][stack_addr - 10]);
    z80_context.ix = read_mcu_mem_w(&tmp_buf[0][stack_addr - 12]);
    z80_context.iy = read_mcu_mem_w(&tmp_buf[0][stack_addr - 14]);

    uint16_t sp = z80_context.sp;
    dma_read_from_sram(((uint32_t)mmu_bank << 16) + sp, tmp_buf[0], 2);
    z80_context.pc = read_mcu_mem_w(tmp_buf[0]);
    mon_cur_addr = z80_context.pc;

    if (mon_step_execution) {
        mon_step_execution--;
        mon_cur_addr = z80_context.pc;

        mon_show_registers();
        dma_read_from_sram(((uint32_t)mmu_bank << 16) + mon_cur_addr, tmp_buf[0], 64);
        disas_ops(disas_z80, ((uint32_t)mmu_bank << 16) + mon_cur_addr, tmp_buf[0], 1, 64, NULL);
    }

    if (!nmi && mon_bp_installed && z80_context.pc == mon_bp_addr + 1) {
        printf("Break at %04X\n\r", mon_bp_addr);
        dma_write_to_sram(((uint32_t)mmu_bank << 16) + mon_bp_addr, &mon_bp_saved_inst, 1);
        z80_context.pc--;
        mon_bp_installed = 0;
        mon_cur_addr = mon_bp_addr;
    }
}

void edit_line(char *line, int maxlen, int start, int pos)
{
    int refresh = 1;
    if (maxlen <= strlen(line))
        line[maxlen - 1] = '\0';
    while (1) {
        if (pos < start) {
            refresh = 1;
            pos = start;
        } else
        if (maxlen - 1 <= pos) {
            refresh = 1;
            pos = maxlen - 2;
            line[maxlen - 1] = '\0';
        } else
        if (strlen(line) < pos) {
            refresh = 1;
            pos = strlen(line);
        }

        if (refresh) {
            printf("\r%s \r", line);
            for (int i = 0; i < pos; i++)
                printf("%c", line[i]);
        } else {
            printf("%c", line[pos - 1]);
        }

        int c = getch();
        refresh = 1;
        switch (c) {
        case 0x01:
            pos = start;
            continue;
        case 0x02:
            pos--;
            continue;
        case 0x05:
            pos = strlen(line);
            continue;
        case 0x06:
            pos++;
            continue;
        case 0x08:
            if (pos <= start)
                continue;
            for (int i = pos; i <= maxlen - 1; i++) {
                line[i - 1] = line[i];
            }
            pos--;
            continue;
        case 0x0a:
        case 0x0d:
            return;
        case 0x11:
            line[pos] = '\0';
            printf("\r%s", line);
            for (int i = pos; i < maxlen; i++) {
                printf(" ");
            }
            continue;
        }
        if (32 <= c && c <= 126) {
            if (line[pos] == '\0' && pos < maxlen - 1) {
                refresh = 0;
                line[pos + 1] = '\0';
            } else {
                for (int i = maxlen - 2; pos <= i; i--) {
                    line[i + 1] = line[i];
                }
            }
            line[pos++] = c;
        } else {
            printf("<%d>\n\r", c);
        }
    }
}

#define MON_MAX_ARGS 2
static const struct {
    uint8_t command;
    const char *name;
    uint8_t nargs;
} mon_cmds[] = {
    { 'b', "breakpoint",    1 },
    { 'C', "clear",         0 },
    { 'c', "continue",      0 },
    { 'd', "disassemble",   2 },
    { 'x', "dump",          2 },
    { 'r', "reset",         0 },
    { 'S', "step",          1 },
    { 's', "status",        0 },
    { 'h', "help",          0 },
};

void mon_help(void)
{
    for (unsigned int cmd_idx = 0; cmd_idx < sizeof(mon_cmds)/sizeof(*mon_cmds); cmd_idx++) {
        printf("%s\n\r", mon_cmds[cmd_idx].name);
    }
}

void mon_remove_space(char **linep)
{
    while (**linep == ' ')  // Remove reading white space characters
        (*linep)++;
}

int mon_get_hexval(char **linep)
{
    int result = 0;
    mon_remove_space(linep);

    while (1) {
        char c = **linep;
        if ('a' <= c && c <= 'f')
            c -= ('a' - 'A');
        if ((c < '0' || '9' < c) && (c < 'A' || 'F' < c))
            break;
        result++;
        (*linep)++;
    }
    mon_remove_space(linep);

    return result;
}

int mon_parse(char *line, uint8_t *command, char *args[MON_MAX_ARGS])
{
    int nmatches = 0;
    int match_idx;
    static int last_command_idx = -1;
    unsigned int cmd_idx;

    for (int i = 0; i < MON_MAX_ARGS; i++)
        args[i] = NULL;

    mon_remove_space(&line);
    if (*line == '\0' && 0 < last_command_idx) {
        *command = mon_cmds[last_command_idx].command;
        printf("\r%s%s", mon_prompt_str, mon_cmds[last_command_idx].name);
        return 0;
    }
    last_command_idx = -1;

    // Search command in the command table
    for (cmd_idx = 0; cmd_idx < sizeof(mon_cmds)/sizeof(*mon_cmds); cmd_idx++) {
        int i;
        for (i = 0; line[i] && line[i] != ' '; i++) {
            if (line[i] <= 'z' && line[i] != mon_cmds[cmd_idx].name[i] &&
                line[i] - ('A' - 'a') != mon_cmds[cmd_idx].name[i]) {
                break;
            }
        }
        if (line[i] == '\0' || line[i] == ' ') {
            nmatches++;
            match_idx = cmd_idx;
        }
    }
    if (nmatches < 1){
        // Unknown command
        #ifdef CPM_MON_DEBUG
        printf("not match: %s\n\r", line);
        #endif
        return 1;
    }
    if (1 < nmatches){
        // Ambiguous command
        #ifdef CPM_MON_DEBUG
        printf("Ambiguous command %s\n\r", line);
        #endif
        return 2;
    }
    // Read command name
    while (*line != '\0' && *line != ' ')
        line++;
    mon_remove_space(&line);

    // Read command arguments
    for (int i = 0; i < mon_cmds[match_idx].nargs; i++) {
        args[i] = line;
        if (mon_get_hexval(&line) == 0 && *line == '\0') {
            // reach end of the line without any argument
            args[i] = NULL;
            break;
        }
        if (*line == ',') {
            // terminate arg[i] and go next argument
            (*line++) = '\0';
            continue;
        }
    }
    if (*line != '\0') {
        // Some garbage found
        #ifdef CPM_MON_DEBUG
        printf("Trailing garbage found: '%s'\n\r", line);
        #endif
        return 3;
    }

    *command = mon_cmds[match_idx].command;
    last_command_idx = match_idx;
    return 0;
}

void mon_dump(char *args[])
{
    uint32_t addr = mon_cur_addr;
    unsigned int len = 64;

    if (args[0] != NULL && *args[0] != '\0')
        addr = strtoul(args[0], NULL, 16);
    if (args[1] != NULL && *args[1] != '\0')
        len = strtoul(args[1], NULL, 16);

    if (addr & 0xf) {
        len += (addr & 0xf);
        addr &= ~0xf;
    }
    if (len & 0xf) {
        len += (16 - (len & 0xf));
    }

    while (0 < len) {
        unsigned int n = UTIL_MIN(len, sizeof(tmp_buf[0]));
        dma_read_from_sram(((uint32_t)mmu_bank << 16) + addr, tmp_buf[0], n);
        util_addrdump("", ((uint32_t)mmu_bank << 16) + addr, tmp_buf[0], n);
        len -= n;
        addr += n;
    }
    mon_cur_addr = addr;
}

void mon_disas(char *args[])
{
    uint32_t addr = mon_cur_addr;
    unsigned int len = 32;

    if (args[0] != NULL && *args[0] != '\0')
        addr = strtoul(args[0], NULL, 16);
    if (args[1] != NULL && *args[1] != '\0')
        len = strtoul(args[1], NULL, 16);

    int leftovers = 0;
    while (leftovers < len) {
        unsigned int n = UTIL_MIN(len, sizeof(tmp_buf[0])) - leftovers;
        dma_read_from_sram(((uint32_t)mmu_bank << 16) + addr, &tmp_buf[0][leftovers], n);
        n += leftovers;
        int done = disas_ops(disas_z80, ((uint32_t)mmu_bank << 16) + addr, tmp_buf[0], n, n, NULL);
        leftovers = n - done;
        len -= done;
        addr += done;
        #ifdef CPM_MON_DEBUG
        printf("addr=%lx, done=%d, len=%d, n=%d, leftover=%d\n\r", addr, done, len, n, leftovers);
        #endif
        for (int i = 0; i < leftovers; i++)
            tmp_buf[0][i] = tmp_buf[0][sizeof(tmp_buf[0]) - leftovers + i];
    }
    mon_cur_addr = addr;

    #ifdef CPM_MON_DEBUG
    printf("mon_cur_addr=%lx\n\r", mon_cur_addr);
    #endif
}

void mon_step(char *args[])
{
    if (args[0] != NULL && *args[0] != '\0') {
        mon_step_execution = strtoul(args[0], NULL, 16);
    } else {
        mon_step_execution = 1;
    }
}

void mon_status(void)
{
    uint16_t sp = z80_context.sp;
    uint16_t pc = z80_context.pc;

    mon_show_registers();

    printf("\n\r");
    printf("stack:\n\r");
    dma_read_from_sram(((uint32_t)mmu_bank << 16) + (sp & ~0xf), tmp_buf[0], 64);
    util_addrdump("", ((uint32_t)mmu_bank << 16) + (sp & ~0xf), tmp_buf[0], 64);

    printf("\n\r");
    printf("program:\n\r");
    dma_read_from_sram(((uint32_t)mmu_bank << 16) + (pc & ~0xf), tmp_buf[0], 64);
    util_addrdump("", ((uint32_t)mmu_bank << 16) + (pc & ~0xf), tmp_buf[0], 64);

    printf("\n\r");
    disas_ops(disas_z80, ((uint32_t)mmu_bank << 16) + pc, &tmp_buf[0][pc & 0xf], 16, 16, NULL);
}

void mon_breakpoint(char *args[])
{
    uint8_t rst08[] = { 0xcf };
    char *p;

    if (args[0] != NULL && *args[0] != '\0') {
        // break point address specified
        if (mon_bp_installed) {
            // clear previous break point if it exist
            dma_write_to_sram(((uint32_t)mmu_bank << 16) + mon_bp_addr, &mon_bp_saved_inst, 1);
            mon_bp_installed = 0;
        }

        mon_bp_addr = strtoul(args[0], &p, 16);  // new break point address
        printf("Set breakpoint at %04X\n\r", mon_bp_addr);

        // save and replace the instruction at the break point with RST instruction
        dma_read_from_sram(((uint32_t)mmu_bank << 16) + mon_bp_addr, &mon_bp_saved_inst, 1);
        dma_write_to_sram(((uint32_t)mmu_bank << 16) + mon_bp_addr, rst08, 1);
        memcpy(&tmp_buf[1][0x08], &mon_rstmon[0x08], sizeof(mon_rstmon) - 0x08);
        mon_bp_installed = 1;
    } else {
        if (mon_bp_installed) {
            printf("Breakpoint is %04X\n\r", mon_bp_addr);
        } else {
            printf("Breakpoint is not set\n\r");
        }
    }
}

void mon_clear_breakpoint()
{
    if (mon_bp_installed) {
        printf("Clear breakpoint at %04X\n\r", mon_bp_addr);
        dma_write_to_sram(((uint32_t)mmu_bank << 16) + mon_bp_addr, &mon_bp_saved_inst, 1);
        mon_bp_installed = 0;
    } else {
        printf("Breakpoint is not set\n\r");
    }
}

int mon_prompt(void)
{
    char line[32];
    char *args[MON_MAX_ARGS];
    const int prompt_len = strlen(mon_prompt_str);
    char* input = &line[prompt_len];

    sprintf(line, mon_prompt_str);
    edit_line(line, sizeof(line), prompt_len, prompt_len);

    #ifdef CPM_MON_DEBUG
    printf("\n\r");
    util_hexdump("edit_line: ", line, sizeof(line));
    printf("command: %s\n\r", input);
    #endif

    uint8_t command;
    uint16_t arg1;
    uint16_t arg2;

    if (mon_parse(input, &command, args)) {
        printf("\n\r");
        printf("unknown command: %s\n\r", input);
        mon_help();
        return 0;
    }
    printf("\n\r");

    #ifdef CPM_MON_DEBUG
    printf("command: %c", command);
    for (int i = 0; i < MON_MAX_ARGS; i++) {
        if (args[i])
            printf("  '%s'", args[i]);
        else
            printf("  null");
    }
    printf("\n\r");
    #endif

    switch (command) {
    case 'b':
        mon_breakpoint(args);
        break;
    case 'C':
        mon_clear_breakpoint();
        break;
    case 'c':
        return 1;
    case 'd':
        mon_disas(args);
        break;
    case 'r':
        RESET();
        // no return
    case 'S':
        mon_step(args);
        return 1;
    case 's':
        mon_status();
        break;
    case 'x':
        mon_dump(args);
        break;
    case 'h':
        mon_help();
        break;
    }

    return 0;
}

void mon_leave(void)
{
    // printf("Leave monitor\n\r");

    uint16_t pc = z80_context.pc;
    uint16_t sp = z80_context.sp;
    const unsigned int size = sizeof(z80_context.saved_prog);

    // Rewind PC on the NMI stack by 2 byes
    pc -= size;
    write_mcu_mem_w(tmp_buf[0], pc);
    dma_write_to_sram(((uint32_t)mmu_bank << 16) + sp, tmp_buf[0], 2);

    // Save original program
    dma_read_from_sram(((uint32_t)mmu_bank << 16) + pc, &z80_context.saved_prog, size);

    // Insert 'OUT (MON_RESTORE), A'
    memset(tmp_buf[0], 0, size);  // Fill with NOP
    tmp_buf[0][0] = 0xd3;
    tmp_buf[0][1] = MON_RESTORE;
    dma_write_to_sram(((uint32_t)mmu_bank << 16) + pc, tmp_buf[0], size);

    // Clear NMI
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 1);
}

void mon_restore(void)
{
    // printf("\n\rCleanup monitor\n\r");

    // Restore original program
    const unsigned int size = sizeof(z80_context.saved_prog);
    uint16_t pc = z80_context.pc - size;
    dma_write_to_sram(((uint32_t)mmu_bank << 16) + pc, &z80_context.saved_prog, size);
    dma_write_to_sram(((uint32_t)mmu_bank << 16), tmp_buf[1], sizeof(mon));

    if (mon_step_execution) {
        #ifdef CPM_MON_DEBUG
        printf("Single step execution ...\n\r");
        #endif

        // Stop the clock for Z80
        LATA3 = 1;          // CLK
        RA3PPS = 0x00;      // unbind NCO1 and CLK (RA3)

        bus_master(0);
        LATE0 = 1;          // Clear /BUSREQ so that the Z80 can run

        invoke_monitor = 1;
        for (int i = 0; i < 32; i++) {
            // 1 clock
            LATA3 = 1;
            __delay_us(1);
            LATA3 = 0;
            __delay_us(1);
            #ifdef CPM_MON_DEBUG
            if (!RA0 || !RA1) {
                printf("%2d: %04X %c /IORQ=%d  /MREQ=%d\n\r",
                       i, ((PORTD & 0x3f) << 8) | PORTB, !RA5 ? 'R' : 'W', RA0, RA1);
            } else {
                printf("%2d:      %c /IORQ=%d  /MREQ=%d\n\r",
                       i, ' ', RA0, RA1);
            }
            #endif
            if (!RA1 && !RA5) {
                // First memory read is M1 instruction fetch
                break;
            }
        }

        // Restart the clock for Z80
        LATE0 = 0;          // set /BUSREQ to active
        RA3PPS = 0x3f;      // RA3 asign NCO1
        bus_master(1);
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
        mmu_bank_select(PORTC);
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

    if (NUM_DRIVES <= disk_drive || drives[disk_drive].filep == NULL) {
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

    //
    // Give a chance to use PRC (RB6/A6) and PRD (RB7/A7) to PIC programer.
    // It must prevent Z80 from driving A6 and A7 while this period.
    //
    printf("\n\r");
    printf("wait for programmer ...\r");
    __delay_ms(200);
    printf("                       \r");

    printf("\n\r");

    //
    // Say Hello to SPI I/O expander MCP23S08
    //
    if (mcp23s08_probe(MCP23S08_ctx, SPI1_ctx, SPI_CLOCK_100KHZ, 0 /* address */) == 0) {
        printf("SuperMEZ80+SPI with GPIO expander\n\r");
    }
    mcp23s08_write(MCP23S08_ctx, GPIO_CS0, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS0, MCP23S08_PINMODE_OUTPUT);
    mcp23s08_write(MCP23S08_ctx, GPIO_CS1, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_CS1, MCP23S08_PINMODE_OUTPUT);
    #ifdef GPIO_LED
    mcp23s08_write(MCP23S08_ctx, GPIO_LED, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_LED, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_INT
    mcp23s08_write(MCP23S08_ctx, GPIO_INT, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_INT, MCP23S08_PINMODE_OUTPUT);
    #endif
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_NMI, MCP23S08_PINMODE_OUTPUT);
    #ifdef GPIO_BANK0
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK0, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK0, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK1
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK1, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK1, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK2
    mcp23s08_write(MCP23S08_ctx, GPIO_BANK2, 1);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK2, MCP23S08_PINMODE_OUTPUT);
    #endif

    // RAM check
    for (i = 0; i < TMP_BUF_SIZE; i += 2) {
        tmp_buf[0][i + 0] = 0xa5;
        tmp_buf[0][i + 1] = 0x5a;
    }
    uint32_t addr;
    for (addr = 0; addr < MAX_MEM_SIZE; addr += MEM_CHECK_UNIT) {
        printf("Memory 000000 - %06lXH\r", addr);
        tmp_buf[0][0] = (addr >>  0) & 0xff;
        tmp_buf[0][1] = (addr >>  8) & 0xff;
        tmp_buf[0][2] = (addr >> 16) & 0xff;
        dma_write_to_sram(addr, tmp_buf[0], TMP_BUF_SIZE);
        dma_read_from_sram(addr, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) != 0) {
            printf("\nMemory error at %06lXH\n\r", addr);
            util_hexdump_sum(" write: ", tmp_buf[0], TMP_BUF_SIZE);
            util_hexdump_sum("verify: ", tmp_buf[1], TMP_BUF_SIZE);
            break;
        }
        if (addr == 0)
            continue;
        dma_read_from_sram(0, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) == 0) {
            // the page at addr is the same as the first page
            break;
        }
    }
    mmu_mem_size = addr;
    #ifdef CPM_MMU_DEBUG
    for (addr = 0; addr < mmu_mem_size; addr += MEM_CHECK_UNIT) {
        printf("Memory 000000 - %06lXH\r", addr);
        if (addr == 0 || (addr & 0xc000))
            continue;
        tmp_buf[0][0] = (addr >>  0) & 0xff;
        tmp_buf[0][1] = (addr >>  8) & 0xff;
        tmp_buf[0][2] = (addr >> 16) & 0xff;
        //dma_write_to_sram(addr, tmp_buf[0], TMP_BUF_SIZE);
        dma_read_from_sram(addr, tmp_buf[1], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) != 0) {
            printf("\nMemory error at %06lXH\n\r", addr);
            util_hexdump_sum(" canon: ", tmp_buf[0], TMP_BUF_SIZE);
            util_hexdump_sum("  read: ", tmp_buf[1], TMP_BUF_SIZE);
            while (1);
        }
    }
    for (i = 0; i < TMP_BUF_SIZE; i++)
        tmp_buf[1][i] = TMP_BUF_SIZE - i;
    for (addr = 0x0c000; addr < 0x10000; addr += TMP_BUF_SIZE) {
        for (i = 0; i < TMP_BUF_SIZE; i++)
            tmp_buf[0][i] = i;
        dma_write_to_sram(0x0c000, tmp_buf[0], TMP_BUF_SIZE);
        dma_write_to_sram(0x1c000, tmp_buf[1], TMP_BUF_SIZE);
        dma_read_from_sram(0x0c000, tmp_buf[0], TMP_BUF_SIZE);
        if (memcmp(tmp_buf[0], tmp_buf[1], TMP_BUF_SIZE) != 0) {
            printf("\nMemory error at %06lXH\n\r", addr);
            util_hexdump_sum("expect: ", tmp_buf[1], TMP_BUF_SIZE);
            util_hexdump_sum("  read: ", tmp_buf[0], TMP_BUF_SIZE);
            while (1);
        }
    }
    #endif  // CPM_MMU_DEBUG

    mmu_num_banks = mmu_mem_size / 0x10000;
    printf("Memory 000000 - %06lXH %d KB OK\r\n", addr, (int)(mmu_mem_size / 1024));

    //
    // Initialize SD Card
    //
    for (int retry = 0; 1; retry++) {
        if (20 <= retry) {
            printf("No SD Card?\n\r");
            while (1);
        }
        if (SDCard_init(SPI_CLOCK_100KHZ, SPI_CLOCK_2MHZ, /* timeout */ 100) == SDCARD_SUCCESS)
            break;
        __delay_ms(200);
    }
    if (f_mount(&fs, "0://", 1) != FR_OK) {
        printf("Failed to mount SD Card.\n\r");
        while (1);
    }

    //
    // Select disk image folder
    //
    if (f_opendir(&fsdir, "/")  != FR_OK) {
        printf("Failed to open SD Card..\n\r");
        while (1);
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
    for (unsigned int drive = 0; drive < NUM_DRIVES && num_files < NUM_FILES; drive++) {
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
        while (1);
    }

    //
    // Transfer ROM image to the SRAM
    //
    acquire_addrbus(0x00000);
    TRISC = 0x00;       // Set as output to write to the SRAM
    for(i = 0; i < sizeof(rom); i++) {
        ab.w = i;
        LATD = ab.h;
        LATB = ab.l;
        LATA2 = 0;      // /WE=0
        LATC = rom[i];
        LATA2 = 1;      // /WE=1
    }
    release_addrbus();

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

    printf("\n\r");

    while(1);  // All things come to those who wait
}
