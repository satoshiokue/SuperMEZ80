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
#ifdef SPI_USE_MCP23S08
#include "mcp23s08.h"
#endif

#include <picconfig.h>

struct SPI_HW {
    struct SPI spi;
    uint8_t bus_acquired;
    uint8_t trisc;
    uint16_t clock_delay;
};
static struct SPI_HW ctx_ = { 0 };
struct SPI *SPI(ctx) = (struct SPI *)&ctx_;

static void acquire_bus(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    if (ctx->bus_acquired == 0) {
        SPI(CLK_PPS) = 0x31;    // Set as CLK output
        SPI(PICO_PPS) = 0x32;   // Set as data output
        ctx->trisc = TRISC;     // save direction settings
        SPI(PICO_TRIS) = 0;     // set PICO as output
        SPI(CLK_TRIS) = 0;      // set clock as output
        SPI(POCI_TRIS) = 1;     // set POCI as input
    }
    ctx->bus_acquired++;
}

static void release_bus(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    if (--ctx->bus_acquired <= 0) {
        SPI(CLK_PPS) = 0x00;    // Release CLK output
        SPI(PICO_PPS) = 0x00;   // Release data output
        TRISC = ctx->trisc;     // restore direction settings
    }
}

void SPI(begin)(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    ctx->bus_acquired = 0;

    SPI1CON0 = 0;
    SPI1CON1 = 0;

    SPI1SCKPPS = SPI(CLK_PIN);  // Assign CLK input pin (?)
    SPI1SDIPPS = SPI(POCI_PIN);  // Assign data input pin
#ifdef SPI_USE_MCP23S08
    if (mcp23s08_is_alive(MCP23S08_ctx)) {
        mcp23s08_write(MCP23S08_ctx, SPI(CS_PORT), 1);  // Inactive
    } else
#endif
    {
    SPI(CS_ANSEL) = 0;  // Disable analog function
    SPI(CS_TRIS) = 0;  // Set as output
    }
    SPI1CON0bits.EN = 1;  // Enable SPI
}

void SPI(configure)(struct SPI *ctx_, uint16_t clock_delay, uint8_t bit_order, uint8_t data_mode)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    ctx->clock_delay = clock_delay;

    SPI1CON0bits.MST = 1;      // Host mode
    SPI1CON0bits.BMODE = 1;    // Byte transfer mode
    SPI1TWIDTH = 0;            // 8 bit
    SPI1INTE = 0;              // Interrupts are not used
    SPI1CON1bits.FST = 0;      // Delay to first SCK will be at least 1⁄2 baud period
    SPI1CON2bits.TXR = 1;      // Full duplex mode (TXR and RXR are both enabled)
    SPI1CON2bits.RXR = 1;

    if (bit_order == SPI_MSBFIRST)
        SPI1CON0bits.LSBF = 0;
    else
        SPI1CON0bits.LSBF = 1;

    if (data_mode == SPI_MODE0) {
        SPI1CON1bits.SMP = 0;  // SDI input is sampled in the middle of data output time
        SPI1CON1bits.CKE = 1;  // Output data changes on transition from Active to Idle clock state
        SPI1CON1bits.CKP = 0;  // Idle state for SCK is low level
    } else {
        printf("%s: ERROR: mode %d is not supported\n\r", __FILE__, data_mode);
        while (1);
    }

    if (clock_delay == 0) {
        SPI1CLK = 0;   // FOSC (System Clock)
        //SPI1BAUD = 3;  // 64 MHz / 2 * ( 3 + 1) = 8.0 MHz
        //SPI1BAUD = 4;  // 64 MHz / 2 * ( 4 + 1) = 6.4 MHz
        SPI1BAUD = 5;  // 64 MHz / 2 * ( 5 + 1) = 5.3 MHz
        //SPI1BAUD = 7;  // 64 MHz / 2 * ( 5 + 1) = 4.0 MHz
    } else {
        SPI1CLK = 2;   // MFINTOSC (500 kHz)
        SPI1BAUD = 2;  // 500 kHz / 2 * ( 2 + 1) = 83 kHz
    }
}

void SPI(begin_transaction)(struct SPI *ctx_)
{
    acquire_bus(ctx_);
    SPI(select)(ctx_, 1);  // select the chip and start transaction
}

void SPI(end_transaction)(struct SPI *ctx_)
{
    SPI(select)(ctx_, 0);  // de-select the chip and end transaction
    release_bus(ctx_);
}

uint8_t SPI(transfer_byte)(struct SPI *ctx_, uint8_t output)
{
    SPI1TCNTH = 0;
    SPI1TCNTL = 1;
    SPI1TXB = output;
    while(!PIR3bits.SPI1RXIF);
    return SPI1RXB;
}

void SPI(transfer)(struct SPI *ctx_, void *buf, unsigned int count)
{
    uint8_t *p = (uint8_t*)buf;
    for (int i = 0; i < count; i++) {
        *p = SPI(transfer_byte)(ctx_, *p);
        p++;
    }
}

void SPI(send)(struct SPI *ctx_, const void *buf, unsigned int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t dummy;

    if (count == 0)
        return;

    SPI1TCNTH = (count >> 8);
    SPI1TCNTL = (count & 0xff);

    SPI1TXB = *p++;
    for (int i = 1; i < count; i++) {
        SPI1TXB = *p++;
        while(!PIR3bits.SPI1RXIF);
        dummy = SPI1RXB;
    }
    while(!PIR3bits.SPI1RXIF);
    dummy = SPI1RXB;
}

void SPI(receive)(struct SPI *ctx_, void *buf, unsigned int count)
{
    uint8_t *p = (uint8_t*)buf;

    if (count == 0)
        return;

    SPI1TCNTH = (count >> 8);
    SPI1TCNTL = (count & 0xff);

    SPI1TXB = 0xff;
    for (int i = 1; i < count; i++) {
        SPI1TXB = 0xff;
        while(!PIR3bits.SPI1RXIF);
        *p++ = SPI1RXB;
    }
    while(!PIR3bits.SPI1RXIF);
    *p++ = SPI1RXB;
}

void SPI(dummy_clocks)(struct SPI *ctx_, unsigned int clocks)
{
    uint8_t dummy = 0xff;
    acquire_bus(ctx_);
    for (int i = 0; i < clocks; i++) {
        SPI(send)(ctx_, &dummy, 1);
    }
    release_bus(ctx_);
}

uint8_t SPI(receive_byte)(struct SPI *ctx_)
{
    uint8_t dummy = 0xff;
    SPI(receive)(ctx_, &dummy, 1);
    return dummy;
}

void SPI(select)(struct SPI *ctx_, int select)
{
#ifdef SPI_USE_MCP23S08
    if (mcp23s08_is_alive(MCP23S08_ctx)) {
        mcp23s08_write(MCP23S08_ctx, SPI(CS_PORT), select ? 0 : 1);
    } else
#endif
    SPI(CS) = select ? 0 : 1;
}
