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
#include <fatdisk_debug.h>

static const unsigned char trampoline[] = {
// trampoline code at zero page
#include "trampoline.inc"
};

#define ZERO_PAGE 0x0000
#define ZERO_PAGE_SIZE 256
#define NMI_VECTOR 0x0066
#define NMI_VECTOR_SIZE 3
#define RST_VECTOR 0x0008
#define RST_VECTOR_SIZE 6

// this structure must equals to work area in the trampoline code. see trampoline.z80
struct trampoline_work_s {
    uint16_t iy;
    uint16_t ix;
    uint16_t hl;
    uint16_t de;
    uint16_t bc;
    uint16_t af;
    uint16_t fake_ret_addr;
    uint16_t pc;
    uint16_t sp;
    uint16_t cleanup_code_location;
    uint8_t nmi;
    uint8_t rfu;
};

static const uint16_t trampoline_work =
    sizeof(trampoline) - sizeof(struct trampoline_work_s);
static unsigned char zero_page_saved[ZERO_PAGE_SIZE];
static int trampoline_installed = MMU_INVALID_BANK;
static int trampoline_destroyed = 0;
static unsigned char nmi_vector_saved[NMI_VECTOR_SIZE];
static int nmi_vector_installed = MMU_INVALID_BANK;

// Saved Z80 Context
struct z80_context_s {
    struct trampoline_work_s w;
    uint8_t saved_prog[2];
};

int invoke_monitor = 0;
unsigned int mon_step_execution = 0;
static struct z80_context_s z80_context;
static uint32_t mon_cur_addr = 0;
static unsigned int mon_cur_drive = 0;
static uint32_t mon_cur_lba = 0;
static uint32_t mon_bp_addr;
static uint8_t mon_bp_installed = 0;
static uint8_t mon_bp_saved_inst;
static const char *mon_prompt_str = "MON>";

uint16_t read_mcu_mem_w(void *addr)
{
    uint8_t *p = (uint8_t *)addr;
    return (((uint16_t)p[1] << 8) + p[0]);
}

void write_mcu_mem_w(void *addr, uint16_t val)
{
    uint8_t *p = (uint8_t *)addr;
    *p++ = ((val >> 0) & 0xff);
    *p++ = ((val >> 8) & 0xff);
}

void mon_show_registers(void)
{
    printf("PC: %04X  ", z80_context.w.pc);
    printf("SP: %04X  ", z80_context.w.sp);
    printf("AF: %04X  ", z80_context.w.af);
    printf("BC: %04X  ", z80_context.w.bc);
    printf("DE: %04X  ", z80_context.w.de);
    printf("HL: %04X  ", z80_context.w.hl);
    printf("IX: %04X  ", z80_context.w.ix);
    printf("IY: %04X\n\r", z80_context.w.iy);
    printf("MMU: bank %02X  ", mmu_bank);
    printf("STATUS: %c%c%c%c%c%c%c%c",
           (z80_context.w.af & 0x80) ? 'S' : '-',
           (z80_context.w.af & 0x40) ? 'Z' : '-',
           (z80_context.w.af & 0x20) ? 'X' : '-',
           (z80_context.w.af & 0x10) ? 'H' : '-',
           (z80_context.w.af & 0x08) ? 'X' : '-',
           (z80_context.w.af & 0x04) ? 'P' : '-',
           (z80_context.w.af & 0x02) ? 'N' : '-',
           (z80_context.w.af & 0x01) ? 'C' : '-');
    printf("\n\r");
}

static void uninstall_nmi_vector(void)
{
    if (nmi_vector_installed == MMU_INVALID_BANK)
        return;
    __write_to_sram(bank_phys_addr(nmi_vector_installed, NMI_VECTOR), nmi_vector_saved,
                    NMI_VECTOR_SIZE);
    nmi_vector_installed = MMU_INVALID_BANK;
}

static void install_nmi_vector(int bank)
{
    if (nmi_vector_installed != MMU_INVALID_BANK) {
        uninstall_nmi_vector();
    }
    __read_from_sram(bank_phys_addr(bank, NMI_VECTOR), nmi_vector_saved, NMI_VECTOR_SIZE);
    __write_to_sram(bank_phys_addr(bank, NMI_VECTOR), &trampoline[NMI_VECTOR], NMI_VECTOR_SIZE);
    nmi_vector_installed = bank;
}

static void uninstall_trampoline(void)
{
    if (trampoline_installed == MMU_INVALID_BANK)
        return;
    __write_to_sram(bank_phys_addr(trampoline_installed, ZERO_PAGE), zero_page_saved,
                    ZERO_PAGE_SIZE);
    trampoline_installed = MMU_INVALID_BANK;
}

static void install_trampoline(int bank)
{
    if (trampoline_installed != MMU_INVALID_BANK) {
        uninstall_trampoline();
    }
    __read_from_sram(bank_phys_addr(bank, ZERO_PAGE), zero_page_saved, sizeof(zero_page_saved));
    __write_to_sram(bank_phys_addr(bank, ZERO_PAGE), trampoline, sizeof(trampoline));
    trampoline_installed = bank;
    trampoline_destroyed = 0;
}

static void reinstall_trampoline(void)
{
    if (trampoline_installed == mmu_bank && !trampoline_destroyed) {
        return;
    }
    if (trampoline_installed != mmu_bank) {
        install_trampoline(mmu_bank);
    } else {
        __write_to_sram(bank_phys_addr(mmu_bank, ZERO_PAGE), trampoline, sizeof(trampoline));
    }
    __write_to_sram(bank_phys_addr(trampoline_installed, trampoline_work), &z80_context.w,
                    sizeof(z80_context.w));
    trampoline_destroyed = 0;
}

void mon_use_zeropage(int bank)
{
    if (trampoline_installed != bank) {
        install_trampoline(bank);
    }
    trampoline_destroyed = 1;
}

static void install_rst_vector(int bank)
{
    __write_to_sram(bank_phys_addr(bank, RST_VECTOR), &trampoline[RST_VECTOR], RST_VECTOR_SIZE);
}

static void bank_select_callback(int from, int to)
{
    static uint16_t installed = 0;

    if (mon_bp_installed && !(installed & (1 << to))) {
        install_rst_vector(to);
        installed |= (1 << to);
    }
}

static struct {
    uint8_t fatdisk;
    uint8_t fatdisk_read;
    uint8_t fatdisk_write;
    uint8_t fatdisk_verbose;
    uint8_t sdcard;
    uint8_t sdcard_read;
    uint8_t sdcard_write;
    uint8_t sdcard_verbose;
    uint32_t z80_clock_NCO1INC;
} dbg_set = { 0 };

static void read_debug_settings(void)
{
    int v;

    // fatdisk
    v = fatdisk_debug(0);
    fatdisk_debug(v);
    dbg_set.fatdisk = (v & FATDISK_DEBUG) ? 1 : 0;
    dbg_set.fatdisk_read = (v & FATDISK_DEBUG_READ) ? 1 : 0;
    dbg_set.fatdisk_write = (v & FATDISK_DEBUG_WRITE) ? 1 : 0;
    dbg_set.fatdisk_verbose = (v & FATDISK_DEBUG_VERBOSE) ? 1 : 0;

    // SDCard
    v = SDCard_debug(0);
    SDCard_debug(v);
    dbg_set.sdcard = (v & SDCARD_DEBUG) ? 1 : 0;
    dbg_set.sdcard_read = (v & SDCARD_DEBUG_READ) ? 1 : 0;
    dbg_set.sdcard_write = (v & SDCARD_DEBUG_WRITE) ? 1 : 0;
    dbg_set.sdcard_verbose = (v & SDCARD_DEBUG_VERBOSE) ? 1 : 0;

    dbg_set.z80_clock_NCO1INC = NCO1INC;
}

static void write_debug_settings(void)
{
    int v;

    // fatdisk
    v = 0;
    v |= (dbg_set.fatdisk ? FATDISK_DEBUG : 0);
    v |= (dbg_set.fatdisk_read ? FATDISK_DEBUG_READ : 0);
    v |= (dbg_set.fatdisk_write ? FATDISK_DEBUG_WRITE : 0);
    v |= (dbg_set.fatdisk_verbose ? FATDISK_DEBUG_VERBOSE : 0);
    fatdisk_debug(v);

    // SDCard
    v = 0;
    v |= (dbg_set.sdcard ? SDCARD_DEBUG : 0);
    v |= (dbg_set.sdcard_read ? SDCARD_DEBUG_READ : 0);
    v |= (dbg_set.sdcard_write ? SDCARD_DEBUG_WRITE : 0);
    v |= (dbg_set.sdcard_verbose ? SDCARD_DEBUG_VERBOSE : 0);
    SDCard_debug(v);

    // XXX Fix me. Why this required twice? But it is necessary.
    NCO1INC = (__uint24)dbg_set.z80_clock_NCO1INC;
    NCO1INC = (__uint24)dbg_set.z80_clock_NCO1INC;
}

void mon_init(void)
{
    mmu_bank_select_callback = bank_select_callback;
}

void mon_assert_nmi(void)
{
    set_nmi_pin(0);
}

void mon_setup(void)
{
    install_nmi_vector(mmu_bank);
    mon_assert_nmi();
}

void mon_prepare()
{
    install_trampoline(mmu_bank);
}

void mon_enter()
{
    uint16_t stack_addr;

    #ifdef CPM_MON_DEBUG
    printf("Enter monitor\n\r");
    #endif

    __read_from_sram(phys_addr(trampoline_work), &z80_context.w, sizeof(z80_context.w));

    if (z80_context.w.pc < 0x0100) {
        // Assume that the target is executing the bootloader in early boot stage
        z80_context.w.cleanup_code_location = 0x0200;
    } else {
        z80_context.w.cleanup_code_location = 0x0000;
    }

    mon_cur_addr = z80_context.w.pc;

    if (mon_step_execution) {
        mon_step_execution--;
        mon_cur_addr = z80_context.w.pc;

        mon_show_registers();
        __read_from_sram(phys_addr(mon_cur_addr), tmp_buf[0], 64);
        disas_ops(disas_z80, phys_addr(mon_cur_addr), tmp_buf[0], 64, 1, NULL);
    }

    if (!z80_context.w.nmi && mon_bp_installed && z80_context.w.pc == (mon_bp_addr & 0xffff) + 1) {
        printf("Break at %04X\n\r", (uint16_t)(mon_bp_addr & 0xffff));
        __write_to_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
        z80_context.w.pc--;
        mon_bp_installed = 0;
        mon_cur_addr = mon_bp_addr;
    }
}

void edit_line(char *line, unsigned int maxlen, unsigned int start, unsigned int pos)
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

        int c = getch_buffered();
        refresh = 1;
        switch (c) {
        case 0x00:
            invoke_monitor = 0;
            continue;
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
            for (unsigned int i = pos; i <= maxlen - 1; i++) {
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
            for (unsigned int i = pos; i < maxlen; i++) {
                printf(" ");
            }
            continue;
        }
        if (32 <= c && c <= 126) {
            if (line[pos] == '\0' && pos < maxlen - 1) {
                refresh = 0;
                line[pos + 1] = '\0';
            } else {
                for (unsigned int i = maxlen - 2; pos <= i; i--) {
                    line[i + 1] = line[i];
                }
            }
            line[pos++] = (char)c;
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
int mon_cmd_write(int argc, char *args[]);

#define MON_MAX_ARGS 4
#define MON_STR_ARG(n) (1 << (n))
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
    { "set",            2, mon_cmd_set,              MON_STR_ARG(0) },
    { "step",           1, mon_cmd_step              },
    { "status",         0, mon_cmd_status            },
    { "write",          2, mon_cmd_write,            MON_STR_ARG(1) },
    { "help",           0, mon_cmd_help              },
};
#define MON_INVALID_CMD_INDEX UTIL_ARRAYSIZEOF(mon_cmds)

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

int mon_chartoval(char **strp, uint8_t *valp)
{
    uint8_t res;
    uint8_t val;
    char *str;
    static char *curptr;

    if (strp != NULL) {
        str = *strp;
    } else {
        str = curptr;
    }

    if (str == NULL || str[0] == '\0') {
        res = 0;  // no more value
        goto exit;
    }

    if (str[0] == '\'') {
        res = 0;  // no more value
        str++;
        goto exit;
    }

    if (str[0] != '\\') {
        val = str[0] & 0xff;
        str++;
        res = 1;
        goto exit;
    }

    if (str[1] == '\0') {
        res = 0;
        str++;
        goto exit;
    }
    switch (str[1]) {
    case '\\': val = '\\'; break;
    case '\'': val = '\''; break;
    case '\"': val = '\"'; break;
    case 't':  val = '\t'; break;
    case 'n':  val = '\n'; break;
    case 'r':  val = '\r'; break;
    default:   val = str[1]; break;
    }

    str += 2;
    res = 1;

 exit:
    if (valp)
        *valp = val;
    if (strp)
        *strp = str;
    curptr = str;

    return res;
}

int mon_get_str(char **linep)
{
    int result = 0;

    if (**linep == '\'') {
        char *tmpp = ++(*linep);
        while (mon_chartoval(linep, NULL)) {
            // nothing to do
        }
        result =  *linep - tmpp;
        mon_remove_trailing_space(linep);

        return result;
    }

    while (1) {
        char c = **linep;
        if (c == ' ' || c == ',' || c == '=' || c == '\0')
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

    if ((*linep)[0] == '0' && (*linep)[1] == 'x')
        (*linep) += 2;
    while (1) {
        char c = **linep;
        if ('a' <= c && c <= 'f')
            c -= ('a' - 'A');
        if ((c < '0' || '9' < c) && (c < 'A' || 'F' < c))
            break;
        result++;
        (*linep)++;
    }
    if (**linep == 'h' || **linep == 'H')
        (*linep)++;
    mon_remove_space(linep);

    return result;
}

uint32_t mon_strtoval(char *str)
{
    static char *null_str = { 0 };
    mon_chartoval(&null_str, NULL);

    if (str[0] == '\0')
        return 0;
    if (str[0] == '0' && str[1] == 'x')
        return strtoul(&str[2], NULL, 16);;
    if (str[strlen(str) - 1] == 'h' || str[strlen(str) - 1] == 'H')
        return strtoul(str, NULL, 16);;
    if (str[0] == '\'') {
        uint8_t res;
        char *tmpp;
        tmpp = &str[1];
        if (mon_chartoval(&tmpp, &res))
            return res;
        else
            return '\'';  // error
    }

    return strtoul(str, NULL, 10);;
}

int mon_parse(char *line, unsigned int *command, char *args[MON_MAX_ARGS])
{
    int nmatches = 0;
    unsigned int match_idx;
    static unsigned int last_command_idx = MON_INVALID_CMD_INDEX;
    unsigned int cmd_idx;
    int i;

    for (i = 0; i < MON_MAX_ARGS; i++)
        args[i] = NULL;

    mon_remove_space(&line);
    if (*line == '\0' && last_command_idx != MON_INVALID_CMD_INDEX) {
        *command = last_command_idx;
        printf("\r%s%s\n\r", mon_prompt_str, mon_cmds[last_command_idx].name);
        return 0;
    }
    last_command_idx = MON_INVALID_CMD_INDEX;

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
        if (mon_cmds[match_idx].flags & MON_STR_ARG(i)) {
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
        if (*line == ',' || *line == '=') {
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
        addr = mon_strtoval(args[0]);
    if (args[1] != NULL && *args[1] != '\0')
        len = (unsigned int)mon_strtoval(args[1]);

    if (addr & 0xf) {
        len += (addr & 0xf);
        addr &= ~0xfUL;
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
        addr = mon_strtoval(args[0]);
    if (args[1] != NULL && *args[1] != '\0')
        len = (unsigned int)mon_strtoval(args[1]);

    unsigned int leftovers = 0;
    while (leftovers < len) {
        unsigned int n = UTIL_MIN(len, sizeof(tmp_buf[0])) - leftovers;
        dma_read_from_sram(addr, &tmp_buf[0][leftovers], n);
        n += leftovers;
        unsigned int done = disas_ops(disas_z80, addr, tmp_buf[0], n, n, NULL);
        leftovers = n - done;
        len -= done;
        addr += done;
        #ifdef CPM_MON_DEBUG
        printf("addr=%lx, done=%d, len=%d, n=%d, leftover=%d\n\r", addr, done, len, n, leftovers);
        #endif
        for (unsigned int i = 0; i < leftovers; i++)
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
    unsigned int drive = mon_cur_drive;
    static unsigned int track = 0;
    static unsigned int sector = 0;
    unsigned int len = 1;
    uint8_t update_lba = 0;
    uint32_t lba = mon_cur_lba;

    if (argc == 0) {
        // use current driver and lba if no argument specified
    } else
    if (argc == 1 && *args[0] != '\0') {
        // use the first argument as lba if only one argument specified
        lba = mon_strtoval(args[0]);
    } else {
        if (*args[0] != '\0')
            drive = (unsigned int)mon_strtoval(args[0]);
        if (*args[1] != '\0') {
            update_lba = 1;
            track = (unsigned int)mon_strtoval(args[1]);
        }
        if (argc == 2) {
            update_lba = 0;
            if (*args[1] != '\0') {
                lba = track;
            }
        }
        if (2 < argc && *args[2] != '\0') {
            update_lba = 1;
            sector = (unsigned int)mon_strtoval(args[2]);
        }
        if (3 < argc && *args[3] != '\0')
            len = (unsigned int)mon_strtoval(args[3]);
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
    unsigned int i;
    static uint32_t lba = 0;
    unsigned int count = 1;

    if (0 < argc && *args[0] != '\0') {
        lba = mon_strtoval(args[0]);
    }
    if (1 < argc && *args[1] != '\0') {
        count = (unsigned int)mon_strtoval(args[1]);
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
        mon_step_execution = (unsigned int)mon_strtoval(args[0]);
    } else {
        mon_step_execution = 1;
    }
    return MON_CMD_OK;
}

int mon_cmd_status(int argc, char *args[])
{
    uint16_t sp = z80_context.w.sp;
    uint16_t pc = z80_context.w.pc;

    mon_show_registers();

    printf("\n\r");
    printf("stack:\n\r");
    dma_read_from_sram(phys_addr(sp & ~0xfU), tmp_buf[0], 64);
    util_addrdump("", phys_addr(sp & ~0xfU), tmp_buf[0], 64);

    printf("\n\r");
    printf("program:\n\r");
    dma_read_from_sram(phys_addr(pc & ~0xfU), tmp_buf[0], 64);
    util_addrdump("", phys_addr(pc & ~0xfU), tmp_buf[0], 64);

    printf("\n\r");
    disas_ops(disas_z80, phys_addr(pc), &tmp_buf[0][pc & 0xf], 16, 16, NULL);
    return MON_CMD_OK;
}

int mon_cmd_breakpoint(int argc, char *args[])
{
    uint8_t rst08[] = { 0xcf };

    if (args[0] != NULL && *args[0] != '\0') {
        // break point address specified
        if (mon_bp_installed) {
            // clear previous break point if it exist
            dma_write_to_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
            mon_bp_installed = 0;
        }

        mon_bp_addr = mon_strtoval(args[0]);  // new break point address
        printf("Set breakpoint at %04lX\n\r", mon_bp_addr);

        // save and replace the instruction at the break point with RST instruction
        dma_read_from_sram(mon_bp_addr, &mon_bp_saved_inst, 1);
        dma_write_to_sram(mon_bp_addr, rst08, 1);
        mon_bp_installed = 1;
        install_rst_vector(mmu_bank);
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
    return 0;
}

int mon_cmd_set(int argc, char *args[])
{
    unsigned int i;
    void *ptr;
    #define VA_I8 0
    #define VA_I16 (1 << 0)
    #define VA_I32 (1 << 1)
    static const struct {
        const char *name;
        void *ptr;
        uint8_t attr;
    } variables[] = {
        #ifdef ENABLE_DISK_DEBUG
        { "debug_disk",            &debug.disk,              VA_I8  },
        { "debug_disk_read",       &debug.disk_read,         VA_I8  },
        { "debug_disk_write",      &debug.disk_write,        VA_I8  },
        { "debug_disk_verbose",    &debug.disk_verbose,      VA_I8  },
        { "debug_disk_mask",       &debug.disk_mask,         VA_I16 },
        #endif
        { "debug_fatdisk",         &dbg_set.fatdisk,         VA_I8  },
        { "debug_fatdisk_read",    &dbg_set.fatdisk_read,    VA_I8  },
        { "debug_fatdisk_write",   &dbg_set.fatdisk_write,   VA_I8  },
        { "debug_fatdisk_verbose", &dbg_set.fatdisk_verbose, VA_I8  },
        { "debug_sdcard",          &dbg_set.sdcard,          VA_I8  },
        { "debug_sdcard_read",     &dbg_set.sdcard_read,     VA_I8  },
        { "debug_sdcard_write",    &dbg_set.sdcard_write,    VA_I8  },
        { "debug_sdcard_verbose",  &dbg_set.sdcard_verbose,  VA_I8  },
        { "z80_clock_NCO1INC",     &dbg_set.z80_clock_NCO1INC,VA_I32 },
    };

    read_debug_settings();

    // show all settings if no arguments specified
    if (args[0] == NULL || *args[0] == '\0') {
        for (i = 0; i < UTIL_ARRAYSIZEOF(variables); i++) {
            ptr = variables[i].ptr;
            if (variables[i].attr & VA_I32)
                printf("%s=%ld (%lXh)\n\r", variables[i].name, *(uint32_t*)ptr, *(uint32_t*)ptr);
            else
            if (variables[i].attr & VA_I16)
                printf("%s=%d (%Xh)\n\r", variables[i].name, *(uint16_t*)ptr, *(uint16_t*)ptr);
            else
                printf("%s=%d (%Xh)\n\r", variables[i].name, *(uint8_t*)ptr, *(uint8_t*)ptr);
        }
        return MON_CMD_OK;
    }

    // search entry of variable
    for (i = 0; i < UTIL_ARRAYSIZEOF(variables); i++) {
        if (util_stricmp(variables[i].name, args[0]) == 0)
            break;
    }

    // error if no entry is found
    if (UTIL_ARRAYSIZEOF(variables) <= i) {
        printf("Unknown variable '%s'\n\r", args[0]);
        return MON_CMD_OK;
    }

    // set value to the variable if second argument is specified
    if (args[1] != NULL && *args[1] != '\0') {
        if (variables[i].attr & VA_I32)
            *(uint32_t*)variables[i].ptr = (uint32_t)mon_strtoval(args[1]);
        else
        if (variables[i].attr & VA_I16)
            *(uint16_t*)variables[i].ptr = (uint16_t)mon_strtoval(args[1]);
        else
            *(uint8_t*)variables[i].ptr = (uint8_t)mon_strtoval(args[1]);
        write_debug_settings();
    }

    // show name and value of the variable
    ptr = variables[i].ptr;
    if (variables[i].attr & VA_I32)
        printf("%s=%ld (%lXh)\n\r", variables[i].name, *(uint32_t*)ptr, *(uint32_t*)ptr);
    else
    if (variables[i].attr & VA_I16)
        printf("%s=%d (%Xh)\n\r", variables[i].name, *(uint16_t*)ptr, *(uint16_t*)ptr);
    else
        printf("%s=%d (%Xh)\n\r", variables[i].name, *(uint8_t*)ptr, *(uint8_t*)ptr);

    return MON_CMD_OK;
}

int mon_cmd_write(int argc, char *args[])
{
    uint32_t addr = mon_cur_addr;
    uint8_t val;

    if (args[0] != NULL && *args[0] != '\0')
        addr = mon_strtoval(args[0]);
    if (args[1] == NULL || *args[1] == '\0') {
        printf("write [addr],val\n\r");
        return MON_CMD_OK;
    }

    val = (uint8_t)mon_strtoval(args[1]);
    dma_write_to_sram(addr, &val, 1);
    addr++;
    while (mon_chartoval(NULL, &val)) {
        dma_write_to_sram(addr, &val, 1);
        addr++;
    }
    mon_cur_addr = addr;

    return MON_CMD_OK;
}

int mon_prompt(void)
{
    char line[48];
    int argc;
    char *args[MON_MAX_ARGS];
    const unsigned int prompt_len = strlen(mon_prompt_str);
    char* input = &line[prompt_len];

    sprintf(line, "%s", mon_prompt_str);
    edit_line(line, sizeof(line), prompt_len, prompt_len);

    printf("\n\r");
    #ifdef CPM_MON_DEBUG
    util_hexdump("edit_line: ", line, sizeof(line));
    printf("command: %s\n\r", input);
    #endif

    unsigned int command;
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

    reinstall_trampoline();

    // restore bank pins
    set_bank_pins((uint32_t)mmu_bank << 16);

    set_nmi_pin(1);
}

void mon_cleanup(void)
{
    // printf("\n\rCleanup monitor\n\r");

    uninstall_trampoline();
    uninstall_nmi_vector();

    if (z80_context.w.cleanup_code_location == 0x0000) {
        // Use modified RST 08h vector to return to original NMI return addess
        install_rst_vector(mmu_bank);
        static const char return_instruction[] = { 0xc9 };  // Z80 RET instruction
        __write_to_sram(phys_addr(0x000b), return_instruction, sizeof(return_instruction));
    }

    if (mon_step_execution) {
        invoke_monitor = 1;
    }
}
