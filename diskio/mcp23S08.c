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

#include <xc.h>
#include <stdio.h>
#include "SPI.h"
#include "mcp23s08.h"

#include <picconfig.h>

// #define MCP23S08_DEBUG
#if defined(MCP23S08_DEBUG)
#define dprintf(args) do { printf args; } while(0)
#else
#define dprintf(args) do { } while(0)
#endif

static struct MCP23S08 {
    struct SPI *spi;
    uint16_t clock_delay;
    uint8_t addr;
    uint8_t dir;
    uint8_t olat;
} ctx_ = { 0 };
struct MCP23S08 *MCP23S08_ctx = &ctx_;

#define REG_IODIR    0x00
#define REG_IPOL     0x01
#define REG_GPINTEN  0x02
#define REG_DEFVAL   0x03
#define REG_INTCON   0x04
#define REG_IOCON    0x05
#define REG_GPPU     0x06
#define REG_INTF     0x07
#define REG_INTCAP   0x08
#define REG_GPIO     0x09
#define REG_OLAT     0x0a

static uint8_t mcp23S08_reg_read(struct MCP23S08 *ctx, uint8_t reg)
{
    struct SPI *spi = ctx->spi;
    uint8_t buf[3];
    buf[0] = 0x41 | (ctx->addr) << 1;  // read
    buf[1] = reg;
    spi->begin_transaction(spi);
    spi->begin_transaction(spi);
    spi->send(spi, buf, 2);
    buf[2] = spi->receive_byte(spi);
    spi->end_transaction(spi);
    return buf[2];
}

static void mcp23S08_reg_write(struct MCP23S08 *ctx, uint8_t reg, uint8_t val)
{
    struct SPI *spi = ctx->spi;
    uint8_t buf[3];
    buf[0] = 0x40 | (ctx->addr) << 1;  // read
    buf[1] = reg;
    buf[2] = val;
    spi->begin_transaction(spi);
    spi->send(spi, buf, 3);
    spi->end_transaction(spi);
}

void mcp23s08_init(struct MCP23S08 *ctx, struct SPI *spi, uint16_t clock_delay, uint8_t addr)
{
    ctx->spi = spi;
    ctx->clock_delay = clock_delay;
    ctx->addr = addr;
    ctx->dir = 0xff;  // all ports are setup as input
    ctx->olat = 0;
    dprintf(("\n\rMCP23S08: initialize ...\n\r"));

    spi->begin(spi);
    spi->configure(spi, clock_delay, SPI_MSBFIRST, SPI_MODE0);

    #ifdef MCP23S08_DEBUG
    uint8_t iodir, gpio;
    iodir = mcp23S08_reg_read(ctx, REG_IODIR);
    gpio = mcp23S08_reg_read(ctx, REG_GPIO);
    dprintf(("MCP23S08:   IODIR: %02x\n\r", iodir));
    dprintf(("MCP23S08:    GPIO: %02x\n\r", gpio));
    #endif
}

void mcp23s08_pinmode(struct MCP23S08 *ctx, int gpio, int mode)
{
    if (mode == MCP23S08_PINMODE_OUTPUT) {
        ctx->dir &= ~(1UL<<gpio);
    } else {
        ctx->dir |= (1UL<<gpio);
    }
    mcp23S08_reg_write(ctx, REG_IODIR, ctx->dir);
}

void mcp23s08_write(struct MCP23S08 *ctx, int gpio, int val)
{
    if (val) {
        ctx->olat |= (1UL<<gpio);
    } else {
        ctx->olat &= ~(1UL<<gpio);
    }
    mcp23S08_reg_write(ctx, REG_OLAT, ctx->olat);
}
