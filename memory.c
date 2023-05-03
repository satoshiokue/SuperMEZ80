/*
 * UART, disk I/O and monitor firmware for SuperMEZ80-SPI
 *
 * Based on main.c by Tetsuya Suzuki and emuz80_z80ram.c by Satoshi Okue
 * Modified by @hanyazou https://twitter.com/hanyazou
 */
#include <supermez80.h>
#include <stdio.h>
#include <string.h>
#include <mcp23s08.h>
#include <utils.h>

// MMU
int mmu_bank = 0;
uint32_t mmu_num_banks = 0;
uint32_t mmu_mem_size = 0;

void mem_init()
{
    unsigned int i;
    uint32_t addr;

    set_bank_pins(0x00000);
    #ifdef GPIO_BANK0
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK0, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK1
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK1, MCP23S08_PINMODE_OUTPUT);
    #endif
    #ifdef GPIO_BANK2
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_BANK2, MCP23S08_PINMODE_OUTPUT);
    #endif

#ifdef CPM_MMU_EXERCISE
    mmu_mem_size = 0x80000;
    mmu_num_banks = mmu_mem_size / 0x10000;
    memset(tmp_buf[0], 0, TMP_BUF_SIZE * 2);
    for (addr = 0; addr < mmu_mem_size; addr += TMP_BUF_SIZE * 2) {
        dma_write_to_sram(addr, tmp_buf[0], TMP_BUF_SIZE * 2);
    }
#else
    // RAM check
    for (i = 0; i < TMP_BUF_SIZE; i += 2) {
        tmp_buf[0][i + 0] = 0xa5;
        tmp_buf[0][i + 1] = 0x5a;
    }
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
#endif  // !CPM_MMU_EXERCISE
}

void set_bank_pins(uint32_t addr)
{
    uint32_t mask = 0;
    uint32_t val = 0;

    #ifdef GPIO_BANK0
    mask |= (1 << GPIO_BANK0);
    if ((addr >> 16) & 1) {
        val |= (1 << GPIO_BANK0);
    }
    #endif
    #ifdef GPIO_BANK1
    mask |= (1 << GPIO_BANK1);
    if ((addr >> 17) & 1) {
        val |= (1 << GPIO_BANK1);
    }
    #endif
    #ifdef GPIO_BANK2
    mask |= (1 << GPIO_BANK2);
    if ((addr >> 18) & 1) {
        val |= (1 << GPIO_BANK2);
    }
    #endif
    mcp23s08_masked_write(MCP23S08_ctx, mask, val);
}

void dma_acquire_addrbus(uint32_t addr)
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

    set_bank_pins(addr);
}

void dma_release_addrbus(void)
{
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A14, MCP23S08_PINMODE_INPUT);
    mcp23s08_pinmode(MCP23S08_ctx, GPIO_A15, MCP23S08_PINMODE_INPUT);

    // higher address lines must always be driven by MCP23S08
    set_bank_pins((uint32_t)mmu_bank << 16);
}

void dma_write_to_sram(uint32_t dest, void *buf, int len)
{
    uint16_t addr = (dest & LOW_ADDR_MASK);
    uint16_t second_half = 0;
    int i;
    union address_bus_u ab;

    if ((uint32_t)LOW_ADDR_MASK + 1 < (uint32_t)addr + len)
        second_half = (uint16_t)(((uint32_t)addr + len) - ((uint32_t)LOW_ADDR_MASK + 1));

    dma_acquire_addrbus(dest);
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
        dma_acquire_addrbus(dest + i);
    for( ; i < len; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA2 = 0;      // activate /WE
        LATC = ((uint8_t*)buf)[i];
        LATA2 = 1;      // deactivate /WE
    }

    dma_release_addrbus();
}

void dma_read_from_sram(uint32_t src, void *buf, int len)
{
    uint16_t addr = (src & LOW_ADDR_MASK);
    uint16_t second_half = 0;
    int i;
    union address_bus_u ab;

    if ((uint32_t)LOW_ADDR_MASK + 1 < (uint32_t)addr + len)
        second_half = (uint16_t)(((uint32_t)addr + len) - ((uint32_t)LOW_ADDR_MASK + 1));

    dma_acquire_addrbus(src);
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
        dma_acquire_addrbus(src + i);
    for( ; i < len; i++) {
        ab.w = addr;
        LATD = ab.h;
        LATB = ab.l;
        addr++;
        LATA4 = 0;      // activate /OE
        ((uint8_t*)buf)[i] = PORTC;
        LATA4 = 1;      // deactivate /OE
    }

    dma_release_addrbus();
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
        invoke_monitor = 1;
    }
    mmu_bank = bank;
    set_bank_pins((uint32_t)mmu_bank << 16);
}
