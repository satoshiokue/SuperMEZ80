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

#define __C(a, b) a ## _ ## b
#define C(a, b) __C(a, b)
#define SPI(a) C(SPI_PREFIX, a)

static struct SPI_SW {
    struct SPI spi;
    uint8_t clk_phase;
    uint8_t clk_polarity;
    uint8_t bit_order;
    uint8_t trisc;
    uint8_t clock_delay;
    void (*send)(struct SPI *ctx_, void *buf, int count);
    void (*receive)(struct SPI *ctx_, void *buf, int count);
};

static void SPI(send_mode0_msbfirst_nowait)(struct SPI *ctx_, void *buf, int count);
static void SPI(receive_mode0_msbfirst_nowait)(struct SPI *ctx_, void *buf, int count);

void SPI(begin)(struct SPI *ctx_)
{
    SPI(CS_ANSEL) = 0;  // Disable analog function
    SPI(CS) = 1;  // Inactive
    SPI(CS_TRIS) = 0;  // Set as output
}

void SPI(configure)(struct SPI *ctxx2, uint16_t clock_delay, uint8_t bit_order, uint8_t data_mode)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctxx2;
    ctx->clk_phase = (data_mode & 1);  // clock phase (CPHA)
    ctx->clk_polarity = (data_mode & 2);  // clock polarity (CPOL)
    ctx->bit_order = bit_order;
    ctx->clock_delay = clock_delay;

    if (data_mode == SPI_MODE0 && bit_order == SPI_MSBFIRST && clock_delay == 0) {
        ctx->send = SPI(send_mode0_msbfirst_nowait);
        ctx->receive = SPI(receive_mode0_msbfirst_nowait);
    } else {
        ctx->send = NULL;
        ctx->receive = NULL;
    }
}

void SPI(begin_transaction)(struct SPI *ctx_)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    ctx->trisc = TRISC;  // save direction settings
    SPI(PICO_TRIS) = 0;  // set PICO as output
    SPI(CLK_TRIS) = 0;   // set clock as output
    SPI(POCI_TRIS) = 1;  // set POCI as input

    //TRISC = TRISC & 0xfc;  // XXX
    //TRISC = TRISC | 0x04;  // XXX

    SPI(CLK) = ctx->clk_polarity ? 1 : 0;  // set clock in idle state
    SPI(PICO) = 1;  // set PICO as high (SPI standard does not require this but SD card does)
    SPI(CS) = 0;  // start transaction
}

// reverse bit order of given byte
static uint8_t SPI_reverse_bit_order(uint8_t val)
{
    static uint8_t high[] = {
        0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e,
        0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f };
    static uint8_t low[] = {
        0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
        0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0 };
    return high[(val & 0xf0)>>4] | low[val & 0xf];
}

#define SPI_clock_delay() do { \
        for (volatile uint_fast8_t i = ctx->clock_delay; i != 0; i--) ;  \
    } while(0)

uint8_t SPI(transfer_byte)(struct SPI *ctx_, uint8_t output)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    uint8_t input = 0;
    if (ctx->bit_order == SPI_MSBFIRST) {
        output = SPI_reverse_bit_order(output);
    }

    int clk = ctx->clk_polarity ? 1 : 0;  // clock is in idle state

    for (int i = 0; i < 8; i++) {
        // write output data
        SPI(PICO) = (output & 1);
        output >>= 1;

        if (ctx->clk_phase) {
            // read input data at second edge if clock phase (CPHA) is 1
            SPI(CLK) = (clk ^= 1);
            SPI_clock_delay();
        }

        SPI(CLK) = (clk ^= 1);

        // read intput data
        input = (input << 1) | SPI(POCI);
        SPI_clock_delay();

        if (!ctx->clk_phase) {
            // read input data at first edge if clock phase (CPHA) is 0
            SPI(CLK) = (clk ^= 1);
            SPI_clock_delay();
        }
    }

    if (ctx->bit_order != SPI_MSBFIRST) {
        input = SPI_reverse_bit_order(input);
    }

    return input;
}

void SPI(transfer)(struct SPI *ctx_, void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    for (int i = 0; i < count; i++) {
        *p = SPI(transfer_byte)(ctx_, *p);
        p++;
    }
}

static void SPI(send_mode0_msbfirst_nowait)(struct SPI *ctx_, void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    while (p < endp) {
        uint8_t output = *p++;

        // write output data bit 7
        SPI(PICO) = (output & 0x80) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 6
        SPI(PICO) = (output & 0x40) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 5
        SPI(PICO) = (output & 0x20) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 4
        SPI(PICO) = (output & 0x10) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 3
        SPI(PICO) = (output & 0x08) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 2
        SPI(PICO) = (output & 0x04) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 1
        SPI(PICO) = (output & 0x02) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;

        // write output data bit 0
        SPI(PICO) = (output & 0x01) ? 1 : 0;
        SPI(CLK) = 1;
        asm("nop");
        SPI(CLK) = 0;
    }
}

static void SPI(receive_mode0_msbfirst_nowait)(struct SPI *ctx_, void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    SPI(PICO) = 1;

    while (p < endp) {
        uint8_t input = 0;

        // read input data bit 7
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 6
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 5
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 4
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 3
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 2
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 1
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        // read input data bit 0
        input <<= 1;
        SPI(CLK) = 1;
        input |= SPI(POCI);
        SPI(CLK) = 0;

        *p++ = input;
    }
}

void SPI(send)(struct SPI *ctx_, void *buf, int count)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    if (ctx->send) {
        (*ctx->send)(ctx_, buf, count);
    } else {
        uint8_t *p = (uint8_t*)buf;
        for (int i = 0; i < count; i++) {
            SPI(transfer_byte)(ctx_, *p);
            p++;
        }
    }
}

void SPI(receive)(struct SPI *ctx_, void *buf, int count)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    if (ctx->receive) {
        (*ctx->receive)(ctx_, buf, count);
    } else {
        uint8_t *p = (uint8_t*)buf;
        for (int i = 0; i < count; i++) {
            *p++ = SPI(transfer_byte)(ctx_, 0xff);
        }
    }
}

void SPI(end_transaction)(struct SPI *ctx_)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    SPI(CS) = 1;  // end transaction
    TRISC = ctx->trisc;  // restore direction settings
}

void SPI(dummy_clocks)(struct SPI *ctx_, int clocks)
{
    struct SPI_SW *ctx = (struct SPI_SW *)ctx_;
    if (ctx->clock_delay == 0) {
        SPI(PICO) = 1;
        clocks *= 8;

        while (clocks--) {
            SPI(CLK) = 1;
            asm("nop");
            SPI(CLK) = 0;
        }
        return;
    }

    uint8_t dummy = 0xff;
    for (int i = 0; i < clocks; i++) {
        SPI(send)(ctx_, &dummy, 1);
    }
}

uint8_t SPI(receive_byte)(struct SPI *ctx_)
{
    uint8_t dummy = 0xff;
    SPI(receive)(ctx_, &dummy, 1);
    return dummy;
}

static struct SPI_SW ctx_ = {
    {
        SPI(begin),
        SPI(configure),
        SPI(begin_transaction),
        SPI(transfer_byte),
        SPI(transfer),
        SPI(send),
        SPI(receive),
        SPI(end_transaction),
        SPI(dummy_clocks),
        SPI(receive_byte),
    },
};

struct SPI *SPI(ctx) = (struct SPI *)&ctx_;
