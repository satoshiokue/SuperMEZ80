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

#define SPI_PICO LATC0  // controller data out
#define SPI_CLK	 LATC1  // clock
#define SPI_POCI ((PORTC & 0x04) ? 1 : 0)  // controller data in
#define SPI_CS   LATE2  // chip select

static struct SPI {
    uint8_t clk_phase;
    uint8_t clk_polarity;
    uint8_t bit_order;
    uint8_t trisc;
    uint8_t clock_delay;
    void (*send)(void *buf, int count);
    void (*receive)(void *buf, int count);
} ctx_ = { 0 };
#define ctx (&ctx_)

void SPI_begin(void)
{
    ANSELE2 = 0;  // Disable analog function
    SPI_CS = 1;  // Inactive
    TRISE2 = 0;  // Set as output
}

void SPI_configure(uint16_t clock_delay, uint8_t bit_order, uint8_t data_mode)
{
    ctx->clk_phase = (data_mode & 1);  // clock phase (CPHA)
    ctx->clk_polarity = (data_mode & 2);  // clock polarity (CPOL)
    ctx->bit_order = bit_order;
    ctx->clock_delay = clock_delay;

    if (data_mode == SPI_MODE0 && bit_order == SPI_MSBFIRST && clock_delay == 0) {
        ctx->send = SPI_send_mode0_msbfirst_nowait;
        ctx->receive = SPI_receive_mode0_msbfirst_nowait;
    } else {
        ctx->send = NULL;
        ctx->receive = NULL;
    }
}

void SPI_begin_transaction(void)
{
    ctx->trisc = TRISC;  // save direction settings
    TRISC0 = 0;  // set PICO as output
    TRISC1 = 0;  // set clock as output
    TRISC2 = 1;  // set POCI as input

    //TRISC = TRISC & 0xfc;  // XXX
    //TRISC = TRISC | 0x04;  // XXX

    SPI_CLK = ctx->clk_polarity ? 1 : 0;  // set clock in idle state
    SPI_PICO = 1;  // set PICO as high (SPI standard does not require this but SD card does)
    SPI_CS = 0;  // start transaction
}

// reverse bit order of given byte
uint8_t SPI_reverse_bit_order(uint8_t val)
{
    return
        ((val & 0x01) << 7) |
        ((val & 0x02) << 5) |
        ((val & 0x04) << 3) |
        ((val & 0x08) << 1) |
        ((val & 0x10) >> 1) |
        ((val & 0x20) >> 3) |
        ((val & 0x40) >> 5) |
        ((val & 0x80) >> 7);
}

#define SPI_clock_delay() do { \
        for (volatile uint_fast8_t i = ctx->clock_delay; i != 0; i--) ;  \
    } while(0)

uint8_t SPI_transfer_byte(uint8_t output)
{
    uint8_t input = 0;
    if (ctx->bit_order == SPI_MSBFIRST) {
        output = SPI_reverse_bit_order(output);
    }

    int clk = ctx->clk_polarity ? 1 : 0;  // clock is in idle state

    for (int i = 0; i < 8; i++) {
        // write output data
        SPI_PICO = (output & 1);
        output >>= 1;

        if (ctx->clk_phase) {
            // read input data at second edge if clock phase (CPHA) is 1
            SPI_CLK = (clk ^= 1);
            SPI_clock_delay();
        }

        SPI_CLK = (clk ^= 1);

        // read intput data
        input = (input << 1) | SPI_POCI;
        SPI_clock_delay();

        if (!ctx->clk_phase) {
            // read input data at first edge if clock phase (CPHA) is 0
            SPI_CLK = (clk ^= 1);
            SPI_clock_delay();
        }
    }

    if (ctx->bit_order != SPI_MSBFIRST) {
        input = SPI_reverse_bit_order(input);
    }

    return input;
}

void SPI_transfer(void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    for (int i = 0; i < count; i++) {
        *p = SPI_transfer_byte(*p);
        p++;
    }
}

void SPI_send_mode0_msbfirst_nowait(void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    while (p < endp) {
        uint8_t output = *p++;

        // write output data bit 7
        SPI_PICO = (output & 0x80) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 6
        SPI_PICO = (output & 0x40) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 5
        SPI_PICO = (output & 0x20) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 4
        SPI_PICO = (output & 0x10) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 3
        SPI_PICO = (output & 0x08) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 2
        SPI_PICO = (output & 0x04) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 1
        SPI_PICO = (output & 0x02) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;

        // write output data bit 0
        SPI_PICO = (output & 0x01) ? 1 : 0;
        SPI_CLK = 1;
        asm("nop");
        SPI_CLK = 0;
    }
}

void SPI_receive_mode0_msbfirst_nowait(void *buf, int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    SPI_PICO = 1;

    while (p < endp) {
        uint8_t input = 0;

        // read input data bit 7
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 6
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 5
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 4
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 3
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 2
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 1
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        // read input data bit 0
        input <<= 1;
        SPI_CLK = 1;
        input |= SPI_POCI;
        SPI_CLK = 0;

        *p++ = input;
    }
}

void SPI_send(void *buf, int count)
{
    if (ctx->send) {
        (*ctx->send)(buf, count);
    } else {
        uint8_t *p = (uint8_t*)buf;
        for (int i = 0; i < count; i++) {
            SPI_transfer_byte(*p);
            p++;
        }
    }
}

void SPI_receive(void *buf, int count)
{
    if (ctx->receive) {
        (*ctx->receive)(buf, count);
    } else {
        uint8_t *p = (uint8_t*)buf;
        for (int i = 0; i < count; i++) {
            *p++ = SPI_transfer_byte(0xff);
        }
    }
}

void SPI_end_transaction(void)
{
    SPI_CS = 1;  // end transaction
    TRISC = ctx->trisc;  // restore direction settings
}

void SPI_dummy_clocks(int clocks)
{
    if (ctx->clock_delay == 0) {
        SPI_PICO = 1;
        clocks *= 8;

        while (clocks--) {
            SPI_CLK = 1;
            asm("nop");
            SPI_CLK = 0;
        }
        return;
    }

    uint8_t dummy = 0xff;
    for (int i = 0; i < clocks; i++) {
        SPI_send(&dummy, 1);
    }
}

uint8_t SPI_receive_byte(void)
{
    uint8_t dummy = 0xff;
    SPI_receive(&dummy, 1);
    return dummy;
}

