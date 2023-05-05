/*
 * Copyright (c) 2023 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <supermez80.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp23s08.h>
#include <utils.h>
#include <disas.h>
#include <disas_z80.h>
#include <SDCard.h>

static const unsigned char nmimon[] = {
// NMI entry at 0x0066
#include "nmimon.inc"
};
#define NMI_ENTRY 0x0066
#define NMI_HOOK_SIZE (sizeof(nmimon) - NMI_ENTRY)
static const unsigned char *nmi_hook = &nmimon[NMI_ENTRY];
static const uint16_t nmi_hook_stack = sizeof(nmimon);
static unsigned char nmi_hook_saved[NMI_HOOK_SIZE];
static int nmi_hook_installed = MMU_INVALID_BANK;

static const unsigned char rstmon[] = {
// break point interrupt entry at 0x0008
#include "rstmon.inc"
};
#define RST_ENTRY 0x0008
#define RST_HOOK_SIZE (sizeof(rstmon) - RST_ENTRY)
static const unsigned char *rst_hook = &rstmon[RST_ENTRY];
static const uint16_t rst_hook_stack = sizeof(rstmon);
static unsigned char rst_hook_saved[RST_HOOK_SIZE];
static int rst_hook_installed = MMU_INVALID_BANK;

// Saved Z80 Context
struct z80_context_s {
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
};

int invoke_monitor = 0;
unsigned int mon_step_execution = 0;
static struct z80_context_s z80_context;
static uint32_t mon_cur_addr = 0;
static uint32_t mon_cur_drive = 0;
static uint32_t mon_cur_lba = 0;
static uint32_t mon_bp_addr;
static uint8_t mon_bp_installed = 0;
static uint8_t mon_bp_saved_inst;
static const char *mon_prompt_str = "MON>";

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

static void uninstall_nmi_hook(void)
{
    if (nmi_hook_installed == MMU_INVALID_BANK)
        return;
    dma_write_to_sram(bank_phys_addr(nmi_hook_installed,NMI_ENTRY), nmi_hook_saved, NMI_HOOK_SIZE);
    nmi_hook_installed = MMU_INVALID_BANK;
}

static void install_nmi_hook(int bank)
{
    if (nmi_hook_installed != MMU_INVALID_BANK) {
        uninstall_nmi_hook();
    }
    dma_read_from_sram(bank_phys_addr(bank, NMI_ENTRY), nmi_hook_saved, NMI_HOOK_SIZE);
    dma_write_to_sram(bank_phys_addr(bank, NMI_ENTRY), nmi_hook, NMI_HOOK_SIZE);
    nmi_hook_installed = bank;
}

static void uninstall_rst_hook(void)
{
    if (rst_hook_installed == MMU_INVALID_BANK)
        return;
    dma_write_to_sram(bank_phys_addr(rst_hook_installed,RST_ENTRY), rst_hook_saved, RST_HOOK_SIZE);
    rst_hook_installed = MMU_INVALID_BANK;
}

static void install_rst_hook(int bank)
{
    if (rst_hook_installed != MMU_INVALID_BANK)
        uninstall_rst_hook();
    dma_read_from_sram(bank_phys_addr(bank, RST_ENTRY), rst_hook_saved, RST_HOOK_SIZE);
    dma_write_to_sram(bank_phys_addr(bank, RST_ENTRY), rst_hook, RST_HOOK_SIZE);
    rst_hook_installed = bank;
}

static void bank_select_callback(int from, int to)
{
    if (nmi_hook_installed != MMU_INVALID_BANK)
        install_nmi_hook(to);
    if (rst_hook_installed != MMU_INVALID_BANK)
        install_rst_hook(to);
}

void mon_init(void)
{
    mmu_bank_select_callback = bank_select_callback;
}

void mon_setup(void)
{
    install_nmi_hook(mmu_bank);
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
        stack_addr = nmi_hook_stack;
    } else {
        stack_addr = rst_hook_stack;
    }
    z80_context.nmi = nmi;

    dma_read_from_sram(phys_addr(0x0000), tmp_buf[0], stack_addr);
    z80_context.sp = read_mcu_mem_w(&tmp_buf[0][stack_addr - 2]);
    z80_context.af = read_mcu_mem_w(&tmp_buf[0][stack_addr - 4]);
    z80_context.bc = read_mcu_mem_w(&tmp_buf[0][stack_addr - 6]);
    z80_context.de = read_mcu_mem_w(&tmp_buf[0][stack_addr - 8]);
    z80_context.hl = read_mcu_mem_w(&tmp_buf[0][stack_addr - 10]);
    z80_context.ix = read_mcu_mem_w(&tmp_buf[0][stack_addr - 12]);
    z80_context.iy = read_mcu_mem_w(&tmp_buf[0][stack_addr - 14]);

    uint16_t sp = z80_context.sp;
    dma_read_from_sram(phys_addr(sp), tmp_buf[0], 2);
    z80_context.pc = read_mcu_mem_w(tmp_buf[0]);
    mon_cur_addr = z80_context.pc;

    if (mon_step_execution) {
        mon_step_execution--;
        mon_cur_addr = z80_context.pc;

        mon_show_registers();
        dma_read_from_sram(phys_addr(mon_cur_addr), tmp_buf[0], 64);
        disas_ops(disas_z80, phys_addr(mon_cur_addr), tmp_buf[0], 1, 64, NULL);
    }

    if (!nmi && mon_bp_installed && z80_context.pc == (mon_bp_addr & 0xffff) + 1) {
        printf("Break at %04X\n\r", (uint16_t)(mon_bp_addr & 0xffff));
        dma_write_to_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
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

int mon_cmd_help(int argc, char *args[]);
int mon_cmd_dump(int argc, char *args[]);
int mon_cmd_disassemble(int argc, char *args[]);
int mon_cmd_diskread(int argc, char *args[]);
int mon_cmd_sdread(int argc, char *args[]);
int mon_cmd_step(int argc, char *args[]);
int mon_cmd_status(int argc, char *args[]);
int mon_cmd_breakpoint(int argc, char *args[]);
int mon_cmd_clearbreakpoint(int argc, char *args[]);
int mon_cmd_continue(int argc, char *args[]);
int mon_cmd_reset(int argc, char *args[]);
int mon_cmd_set(int argc, char *args[]);

#define MON_MAX_ARGS 4
#define MON_STR_ARG1 (1 << 0)
static const struct {
    const char *name;
    uint8_t nargs;
    int (*function)(int argc, char *args[]);
    unsigned int flags;
} mon_cmds[] = {
    { "breakpoint",     1, mon_cmd_breakpoint        },
    { "clearbreakpoint",0, mon_cmd_clearbreakpoint   },
    { "continue",       0, mon_cmd_continue          },
    { "disassemble",    2, mon_cmd_disassemble       },
    { "di",             2, mon_cmd_disassemble       },
    { "diskread",       4, mon_cmd_diskread          },
    { "dump",           2, mon_cmd_dump              },
    { "reset",          0, mon_cmd_reset             },
    { "sdread",         2, mon_cmd_sdread            },
    { "set",            2, mon_cmd_set,              MON_STR_ARG1   },
    { "step",           1, mon_cmd_step              },
    { "status",         0, mon_cmd_status            },
    { "help",           0, mon_cmd_help              },
};

void mon_remove_space(char **linep)
{
    while (**linep == ' ')  // Remove reading white space characters
        (*linep)++;
}

void mon_remove_trailing_space(char **linep)
{
    while (**linep == ' ')  // Remove trailing white space characters
        *((*linep)++) = '\0';
}

int mon_get_str(char **linep)
{
    int result = 0;

    while (1) {
        char c = **linep;
        if (c == ' ' || c == ',' || c == '\0')
            break;
        result++;
        (*linep)++;
    }
    mon_remove_trailing_space(linep);

    return result;
}

int mon_get_hexval(char **linep)
{
    int result = 0;

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

int mon_parse(char *line, int *command, char *args[MON_MAX_ARGS])
{
    int nmatches = 0;
    int match_idx;
    static int last_command_idx = -1;
    unsigned int cmd_idx;
    int i;

    for (i = 0; i < MON_MAX_ARGS; i++)
        args[i] = NULL;

    mon_remove_space(&line);
    if (*line == '\0' && 0 < last_command_idx) {
        *command = last_command_idx;
        printf("\r%s%s\n\r", mon_prompt_str, mon_cmds[last_command_idx].name);
        return 0;
    }
    last_command_idx = -1;

    // Search command in the command table
    for (cmd_idx = 0; cmd_idx < sizeof(mon_cmds)/sizeof(*mon_cmds); cmd_idx++) {
        for (i = 0; line[i] && line[i] != ' '; i++) {
            if (line[i] <= 'z' && line[i] != mon_cmds[cmd_idx].name[i] &&
                line[i] - ('A' - 'a') != mon_cmds[cmd_idx].name[i]) {
                break;
            }
        }
        if (line[i] == '\0' || line[i] == ' ') {
            match_idx = cmd_idx;
            if (mon_cmds[cmd_idx].name[i] == '\0') {
                nmatches = 1;
                break;
            }
            nmatches++;
        }
    }
    if (nmatches < 1){
        // Unknown command
        #ifdef CPM_MON_DEBUG
        printf("not match: %s\n\r", line);
        #endif
        printf("Unknown command: %s\n\r", line);
        return -1;
    }
    if (1 < nmatches){
        // Ambiguous command
        printf("Ambiguous command %s\n\r", line);
        return -2;
    }
    // Read command name
    while (*line != '\0' && *line != ' ')
        line++;
    mon_remove_space(&line);

    // Read command arguments
    for (i = 0; i < mon_cmds[match_idx].nargs; i++) {
        mon_remove_space(&line);
        args[i] = line;
        if (i == 0 && (mon_cmds[match_idx].flags & MON_STR_ARG1)) {
            if (mon_get_str(&line) == 0 && *line == '\0') {
                // reach end of the line without any argument
                args[i] = NULL;
                break;
            }
        } else
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
        printf("Invalid argument: '%s'\n\r", line);
        return -3;
    }

    *command = match_idx;
    last_command_idx = match_idx;

    return i;  // number of arguments
}

int mon_cmd_help(int argc, char *args[])
{
    for (unsigned int cmd_idx = 0; cmd_idx < sizeof(mon_cmds)/sizeof(*mon_cmds); cmd_idx++) {
        printf("%s\n\r", mon_cmds[cmd_idx].name);
    }
    return MON_CMD_OK;
}

int mon_cmd_dump(int argc, char *args[])
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
        dma_read_from_sram(addr, tmp_buf[0], n);
        util_addrdump("", addr, tmp_buf[0], n);
        len -= n;
        addr += n;
    }
    mon_cur_addr = addr;
    return MON_CMD_OK;
}

int mon_cmd_disassemble(int argc, char *args[])
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
        dma_read_from_sram(addr, &tmp_buf[0][leftovers], n);
        n += leftovers;
        int done = disas_ops(disas_z80, addr, tmp_buf[0], n, n, NULL);
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
    return MON_CMD_OK;
}

int mon_cmd_diskread(int argc, char *args[])
{
    int i;
    int drive = mon_cur_drive;
    static int track = 0;
    static int sector = 0;
    unsigned int len = 1;
    uint8_t update_lba = 0;
    uint32_t lba = mon_cur_lba;

    if (argc == 0) {
        // use current driver and lba if no argument specified
    } else
    if (argc == 1 && *args[0] != '\0') {
        // use the first argument as lba if only one argument specified
        lba = strtoul(args[0], NULL, 10);
    } else {
        if (*args[0] != '\0')
            drive = strtoul(args[0], NULL, 10);
        if (*args[1] != '\0') {
            update_lba = 1;
            track = strtoul(args[1], NULL, 10);
        }
        if (argc == 2) {
            update_lba = 0;
            if (*args[1] != '\0') {
                lba = track;
            }
        }
        if (2 < argc && *args[2] != '\0') {
            update_lba = 1;
            sector = strtoul(args[2], NULL, 10);
        }
        if (3 < argc && *args[3] != '\0')
            len = strtoul(args[3], NULL, 10);
    }
    mon_cur_drive = drive;

    if (update_lba && cpm_trsect_to_lba(drive, track, sector, &lba) != 0) {
        return MON_CMD_ERROR;
    }

    for (i = 0; i < len; i++) {
        if (cpm_trsect_from_lba(drive, &track, &sector, lba) ||
            cpm_disk_read(drive, lba, tmp_buf[0], 1) != 1) {
            printf("DISK:  read D/T/S=%d/%3d/%3d x%3d=%5ld: ERROR\n\r",
                   drive, track, sector, drives[drive].sectors, lba);
            return MON_CMD_ERROR;
        }
        printf("DISK:  read D/T/S=%d/%3d/%3d x%3d=%5ld: OK\n\r",
               drive, track, sector, drives[drive].sectors, lba);
        util_hexdump("", tmp_buf[0], SECTOR_SIZE);
        lba++;
        mon_cur_lba = lba;
    }

    return MON_CMD_OK;
}

int mon_cmd_sdread(int argc, char *args[])
{
    int i;
    static uint32_t lba = 0;
    int count = 1;

    if (0 < argc && *args[0] != '\0') {
        lba = strtoul(args[0], NULL, 10);
    }
    if (1 < argc && *args[1] != '\0') {
        count = strtoul(args[1], NULL, 10);
    }

    for (i = 0; i < count; i++) {
        if (SDCard_read512(lba, 0, tmp_buf[0], 512) != SDCARD_SUCCESS) {
            printf("SD Card:  read512: sector=%ld: ERROR\n\r", lba);
            return MON_CMD_ERROR;
        }
        printf("SD Card:  read512: sector=%ld: OK\n\r", lba);
        util_hexdump("", tmp_buf[0], 512);
        lba++;
    }

    return MON_CMD_OK;
}

int mon_cmd_step(int argc, char *args[])
{
    if (args[0] != NULL && *args[0] != '\0') {
        mon_step_execution = strtoul(args[0], NULL, 16);
    } else {
        mon_step_execution = 1;
    }
    return MON_CMD_OK;
}

int mon_cmd_status(int argc, char *args[])
{
    uint16_t sp = z80_context.sp;
    uint16_t pc = z80_context.pc;

    mon_show_registers();

    printf("\n\r");
    printf("stack:\n\r");
    dma_read_from_sram(phys_addr(sp & ~0xf), tmp_buf[0], 64);
    util_addrdump("", phys_addr(sp & ~0xf), tmp_buf[0], 64);

    printf("\n\r");
    printf("program:\n\r");
    dma_read_from_sram(phys_addr(pc & ~0xf), tmp_buf[0], 64);
    util_addrdump("", phys_addr(pc & ~0xf), tmp_buf[0], 64);

    printf("\n\r");
    disas_ops(disas_z80, phys_addr(pc), &tmp_buf[0][pc & 0xf], 16, 16, NULL);
    return MON_CMD_OK;
}

int mon_cmd_breakpoint(int argc, char *args[])
{
    uint8_t rst08[] = { 0xcf };
    char *p;

    if (args[0] != NULL && *args[0] != '\0') {
        // break point address specified
        if (mon_bp_installed) {
            // clear previous break point if it exist
            dma_write_to_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
            mon_bp_installed = 0;
            uninstall_rst_hook();
        }

        mon_bp_addr = strtoul(args[0], &p, 16);  // new break point address
        printf("Set breakpoint at %04lX\n\r", mon_bp_addr);

        // save and replace the instruction at the break point with RST instruction
        dma_read_from_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
        dma_write_to_sram(mon_bp_addr, rst08, 1);
        mon_bp_installed = 1;
        install_rst_hook(mmu_bank);
    } else {
        if (mon_bp_installed) {
            printf("Breakpoint is %04lX\n\r", mon_bp_addr);
        } else {
            printf("Breakpoint is not set\n\r");
        }
    }
    return MON_CMD_OK;
}

int mon_cmd_clearbreakpoint(int argc, char *args[])
{
    if (mon_bp_installed) {
        printf("Clear breakpoint at %04lX\n\r", mon_bp_addr);
        dma_write_to_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
        mon_bp_installed = 0;
        uninstall_rst_hook();
    } else {
        printf("Breakpoint is not set\n\r");
    }
    return MON_CMD_OK;
}

int mon_cmd_continue(int argc, char *args[])
{
    // "continue" means to exit the monitor and continue running the Z80
    return MON_CMD_EXIT;
}

int mon_cmd_reset(int argc, char *args[])
{
    RESET();
    // no return
}

int mon_cmd_set(int argc, char *args[])
{
    unsigned int i;
    static const struct {
        const char *name;
        uint8_t *ptr;
    } variables[] = {
        #ifdef ENABLE_DISK_DEBUG
        { "debug_disk",         &debug.disk         },
        { "debug_disk_read",    &debug.disk_read    },
        { "debug_disk_write",   &debug.disk_write   },
        { "debug_disk_verbose", &debug.disk_verbose },
        #endif
    };

    // show all settings if no arguments specified
    if (args[0] == NULL || *args[0] == '\0') {
        for (i = 0; i < UTIL_ARRAYSIZEOF(variables); i++) {
            printf("%s=%d\n\r", variables[i].name, *variables[i].ptr);
        }
        return MON_CMD_OK;
    }

    // search entry of variable
    for (i = 0; i < UTIL_ARRAYSIZEOF(variables); i++) {
        if (stricmp(variables[i].name, args[0]) == 0)
            break;
    }

    // error if no entry is found
    if (UTIL_ARRAYSIZEOF(variables) <= i) {
        printf("Unknown variable '%s'\n\r", args[0]);
        return MON_CMD_OK;
    }

    // set value to the variable if second argument is specified
    if (args[1] != NULL && *args[1] != '\0') {
        *variables[i].ptr = strtoul(args[1], NULL, 16);
    }

    // show name and value of the variable
    printf("%s=%d\n\r", variables[i].name, *variables[i].ptr);

    return MON_CMD_OK;
}

int mon_prompt(void)
{
    char line[32];
    int argc;
    char *args[MON_MAX_ARGS];
    const int prompt_len = strlen(mon_prompt_str);
    char* input = &line[prompt_len];

    sprintf(line, mon_prompt_str);
    edit_line(line, sizeof(line), prompt_len, prompt_len);

    printf("\n\r");
    #ifdef CPM_MON_DEBUG
    util_hexdump("edit_line: ", line, sizeof(line));
    printf("command: %s\n\r", input);
    #endif

    int command;
    argc = mon_parse(input, &command, args);
    if (argc < 0) {
        printf("type 'help' to see the list of available commands\n\r");
        return 0;
    }

    #ifdef CPM_MON_DEBUG
    printf("command: %s", mon_cmds[command].name);
    for (int i = 0; i < MON_MAX_ARGS; i++) {
        if (args[i])
            printf("  '%s'", args[i]);
        else
            printf("  null");
    }
    printf(" (argc=%d)\n\r", argc);
    #endif

    int result = mon_cmds[command].function(argc, args);
    if (result == MON_CMD_ERROR) {
        printf("Error\n\r");
    }
    return result;
}

void mon_leave(void)
{
    // printf("Leave monitor\n\r");

    uint16_t pc = z80_context.pc;
    uint16_t sp = z80_context.sp;
    const unsigned int size = sizeof(z80_context.saved_prog);

    // Rewind PC on the NMI stack by 2 bytes
    pc -= size;
    write_mcu_mem_w(tmp_buf[0], pc);
    dma_write_to_sram(phys_addr(sp), tmp_buf[0], 2);

    // Save original program
    dma_read_from_sram(phys_addr(pc), &z80_context.saved_prog, size);

    // Insert 'OUT (MON_RESTORE), A'
    memset(tmp_buf[0], 0, size);  // Fill with NOP
    tmp_buf[0][0] = 0xd3;
    tmp_buf[0][1] = MON_RESTORE;
    dma_write_to_sram(phys_addr(pc), tmp_buf[0], size);

    // Clear NMI
    mcp23s08_write(MCP23S08_ctx, GPIO_NMI, 1);
}

void mon_restore(void)
{
    // printf("\n\rCleanup monitor\n\r");

    // Restore original program
    const unsigned int size = sizeof(z80_context.saved_prog);
    uint16_t pc = z80_context.pc - size;
    dma_write_to_sram(phys_addr(pc), &z80_context.saved_prog, size);
    uninstall_nmi_hook();
    if (!mon_bp_installed) {
        uninstall_rst_hook();
    }

    if (mon_step_execution) {
        invoke_monitor = 1;
    }
}
