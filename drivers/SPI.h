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

#ifndef __SPI_H__
#define __SPI_H__

#include <stdint.h>

#define SPI_MSBFIRST 1
#define SPI_LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

enum {
    SPI_CLOCK_100KHZ,
    SPI_CLOCK_2MHZ,
    SPI_CLOCK_4MHZ,
    SPI_CLOCK_5MHZ,
    SPI_CLOCK_6MHZ,
    SPI_CLOCK_8MHZ,
    SPI_CLOCK_10MHZ,
};

#define __SPI_C(a, b) a ## _ ## b
#define SPI_C(a, b) __SPI_C(a, b)
#define SPI(a) SPI_C(SPI_PREFIX, a)

struct SPI {
    int dummy;
};

void SPI(begin)(struct SPI *ctx_);
void SPI(configure)(struct SPI *ctx_, int clock_speed, uint8_t bitOrder, uint8_t dataMode);
void SPI(begin_transaction)(struct SPI *ctx_);
uint8_t SPI(transfer_byte)(struct SPI *ctx_, uint8_t output);
void SPI(transfer)(struct SPI *ctx_, void *buf, unsigned int count);
void SPI(send)(struct SPI *ctx_, const void *buf, unsigned int count);
void SPI(receive)(struct SPI *ctx_, void *buf, unsigned int count);
void SPI(end_transaction)(struct SPI *ctx_);
void SPI(dummy_clocks)(struct SPI *ctx_, unsigned int clocks);
uint8_t SPI(receive_byte)(struct SPI *ctx_);
void SPI(select)(struct SPI *ctx_, int select);
extern struct SPI *SPI(ctx);

#endif  // __SPI_H__
