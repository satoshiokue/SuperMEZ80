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

#include <stdio.h>
#include "SPI.h"
#ifdef SPI_USE_MCP23S08
#include "mcp23s08.h"
#endif

#include <picconfig.h>

struct SPI_HW {
    struct SPI spi;
    uint8_t bus_acquired;
    uint8_t tris;
};
static struct SPI_HW pic18f47q43_spi_ctx = { 0 };
struct SPI *SPI(ctx) = (struct SPI *)&pic18f47q43_spi_ctx;

void SPI(select)(struct SPI *ctx_, int select);

static void acquire_bus(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    if (ctx->bus_acquired == 0) {
        PPS(SPI(CLK)) = PPS_OUT(SPIx(SCK));
                                    // Set as CLK output
        PPS(SPI(PICO)) = PPS_OUT(SPIx(SDO));
                                    // Set as data output
        ctx->tris = TRIS(Z80_DATA); // save direction settings
        TRIS(SPI(PICO)) = 0;        // set PICO as output
        TRIS(SPI(CLK)) = 0;         // set clock as output
        TRIS(SPI(POCI)) = 1;        // set POCI as input
    }
    ctx->bus_acquired++;
}

static void release_bus(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    if (--ctx->bus_acquired <= 0) {
        PPS(SPI(CLK)) = 0x00;       // Release CLK output
        PPS(SPI(PICO)) = 0x00;      // Release data output
        TRIS(Z80_DATA) = ctx->tris; // restore direction settings
    }
}

void SPI(begin)(struct SPI *ctx_)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;
    ctx->bus_acquired = 0;

    SPIx(CON0) = 0;
    SPIx(CON1) = 0;
    SPIx(SCKPPS) = PPS_IN(SPI(CLK));    // Assign CLK input pin (?)
    SPIx(SDIPPS) = PPS_IN(SPI(POCI));   // Assign data input pin
#ifdef SPI_USE_MCP23S08
    if (mcp23s08_is_alive(MCP23S08_ctx)) {
        mcp23s08_write(MCP23S08_ctx, SPI(GPIO_SS), 1);  // Inactive
    } else
#endif
    {
    TRIS(SPI(SS)) = 0;                  // Set as output
    }
    SPIx(CON0bits).EN = 1;              // Enable SPI
}

void SPI(configure)(struct SPI *ctx_, int clock_speed, uint8_t bit_order, uint8_t data_mode)
{
    struct SPI_HW *ctx = (struct SPI_HW *)ctx_;

    SPIx(CON0bits).MST = 1;     // Host mode
    SPIx(CON0bits).BMODE = 1;   // Byte transfer mode
    SPIx(TWIDTH) = 0;           // 8 bit
    SPIx(INTE) = 0;             // Interrupts are not used
    SPIx(CON1bits).FST = 0;     // Delay to first SCK will be at least 1⁄2 baud period
    SPIx(CON2bits).TXR = 1;     // Full duplex mode (TXR and RXR are both enabled)
    SPIx(CON2bits).RXR = 1;

    if (bit_order == SPI_MSBFIRST)
        SPIx(CON0bits).LSBF = 0;
    else
        SPIx(CON0bits).LSBF = 1;

    if (data_mode == SPI_MODE0) {
        SPIx(CON1bits).SMP = 0; // SDI input is sampled in the middle of data output time
        SPIx(CON1bits).CKE = 1; // Output data changes on transition from Active to Idle clock state
        SPIx(CON1bits).CKP = 0; // Idle state for SCK is low level
    } else {
        printf("%s: ERROR: mode %d is not supported\n\r", __func__, data_mode);
        while (1);
    }

    SPIx(CLK) = 0;      // FOSC (System Clock)
    switch (clock_speed) {
    case SPI_CLOCK_100KHZ:
        SPIx(CLK) = 2;      // MFINTOSC (500 kHz)
        SPIx(BAUD) = 2;     // 500 kHz / (2 * ( 2 + 1)) = 83 kHz
        break;
    case SPI_CLOCK_2MHZ:
        SPIx(BAUD) = 15;    // 64 MHz / (2 * (15 + 1)) = 2.0 MHz
        break;
    case SPI_CLOCK_4MHZ:
        SPIx(BAUD) = 7;     // 64 MHz / (2 * ( 7 + 1)) = 4.0 MHz
        break;
    case SPI_CLOCK_5MHZ:
        SPIx(BAUD) = 5;     // 64 MHz / (2 * ( 5 + 1)) = 5.3 MHz
        break;
    case SPI_CLOCK_6MHZ:
        SPIx(BAUD) = 4;     // 64 MHz / (2 * ( 4 + 1)) = 6.4 MHz
        break;
    case SPI_CLOCK_8MHZ:
        SPIx(BAUD) = 3;     // 64 MHz / (2 * ( 3 + 1)) = 8.0 MHz
        break;
    case SPI_CLOCK_10MHZ:
        SPIx(BAUD) = 2;     // 64 MHz / (2 * ( 2 + 1)) = 10.7 MHz
        break;
    default:
        printf("%s: ERROR: clock speed %d is not supported\n\r", __func__, clock_speed);
        break;
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
    SPIx(TCNTH) = 0;
    SPIx(TCNTL) = 1;
    SPIx(TXB) = output;
    while(!SPIx(RXIF));
    return SPIx(RXB);
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

    SPIx(TCNTH) = (count >> 8);
    SPIx(TCNTL) = (count & 0xff);

    SPIx(TXB) = *p++;
    for (int i = 1; i < count; i++) {
        SPIx(TXB) = *p++;
        while(!SPIx(RXIF));
        dummy = SPIx(RXB);
    }
    while(!SPIx(RXIF));
    dummy = SPIx(RXB);
}

void SPI(receive)(struct SPI *ctx_, void *buf, unsigned int count)
{
    uint8_t *p = (uint8_t*)buf;

    if (count == 0)
        return;

    SPIx(TCNTH) = (count >> 8);
    SPIx(TCNTL) = (count & 0xff);

    if ((count & 0x07) || 255 < count / 8) {
        SPIx(TXB) = 0xff;
        for (int i = 1; i < count; i++) {
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
        }
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
    } else {
        SPIx(TXB) = 0xff;
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        SPIx(TXB) = 0xff;
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
        uint8_t repeat = (uint8_t)(count / 8);
        for (uint8_t i = 1; i < repeat; i++) {
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
            SPIx(TXB) = 0xff;
            while(!SPIx(RXIF));
            *p++ = SPIx(RXB);
        }
        while(!SPIx(RXIF));
        *p++ = SPIx(RXB);
    }
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
        mcp23s08_write(MCP23S08_ctx, SPI(GPIO_SS), select ? 0 : 1);
    } else
#endif
    LAT(SPI(SS)) = select ? 0 : 1;
}
