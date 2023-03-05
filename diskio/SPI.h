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

#define SPI0_PICO      LATC0  // controller data out
#define SPI0_PICO_TRIS TRISC0
#define SPI0_CLK       LATC1  // clock
#define SPI0_CLK_TRIS  TRISC1
#define SPI0_POCI      ((PORTC & 0x04) ? 1 : 0)  // controller data in
#define SPI0_POCI_TRIS TRISC2
#define SPI0_CS        LATE2  // chip select
#define SPI0_CS_TRIS   TRISE2
#define SPI0_CS_ANSEL  ANSELE2
#define SPI0_CS_PORT   0

#define SPI1_PICO      LATC3  // controller data out
#define SPI1_PICO_TRIS TRISC3
#define SPI1_CLK       LATC4  // clock
#define SPI1_CLK_TRIS  TRISC4
#define SPI1_POCI      ((PORTC & 0x20) ? 1 : 0)  // controller data in
#define SPI1_POCI_TRIS TRISC5
#define SPI1_CS        LATE2  // chip select
#define SPI1_CS_TRIS   TRISE2
#define SPI1_CS_ANSEL  ANSELE2

#define SPI_MSBFIRST 1
#define SPI_LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

#define __SPI_C(a, b) a ## _ ## b
#define SPI_C(a, b) __SPI_C(a, b)
#define SPI(a) SPI_C(SPI_PREFIX, a)

struct SPI {
    int dummy;
};

void SPI0_begin(struct SPI *ctx_);
void SPI0_configure(struct SPI *ctx_, uint16_t clock_delay, uint8_t bitOrder, uint8_t dataMode);
void SPI0_begin_transaction(struct SPI *ctx_);
uint8_t SPI0_transfer_byte(struct SPI *ctx_, uint8_t output);
void SPI0_transfer(struct SPI *ctx_, void *buf, int count);
void SPI0_send(struct SPI *ctx_, void *buf, int count);
void SPI0_receive(struct SPI *ctx_, void *buf, int count);
void SPI0_end_transaction(struct SPI *ctx_);
void SPI0_dummy_clocks(struct SPI *ctx_, int clocks);
uint8_t SPI0_receive_byte(struct SPI *ctx_);
void SPI0_select(struct SPI *ctx_, int select);
extern struct SPI *SPI0_ctx;

void SPI1_begin(struct SPI *ctx_);
void SPI1_configure(struct SPI *ctx_, uint16_t clock_delay, uint8_t bitOrder, uint8_t dataMode);
void SPI1_begin_transaction(struct SPI *ctx_);
uint8_t SPI1_transfer_byte(struct SPI *ctx_, uint8_t output);
void SPI1_transfer(struct SPI *ctx_, void *buf, int count);
void SPI1_send(struct SPI *ctx_, void *buf, int count);
void SPI1_receive(struct SPI *ctx_, void *buf, int count);
void SPI1_end_transaction(struct SPI *ctx_);
void SPI1_dummy_clocks(struct SPI *ctx_, int clocks);
uint8_t SPI1_receive_byte(struct SPI *ctx_);
void SPI1_select(struct SPI *ctx_, int select);
extern struct SPI *SPI1_ctx;

static inline void SPI_begin(void) {
    SPI0_begin(SPI0_ctx);
}
static inline void SPI_configure(uint16_t clock_delay, uint8_t bitOrder, uint8_t dataMode) {
    SPI0_configure(SPI0_ctx, clock_delay, bitOrder, dataMode);
}
static inline void SPI_begin_transaction(void) {
    SPI0_begin_transaction(SPI0_ctx);
}
static inline uint8_t SPI_transfer_byte(uint8_t output) {
    return SPI0_transfer_byte(SPI0_ctx, output);
}
static inline void SPI_transfer(void *buf, int count) {
    SPI0_transfer(SPI0_ctx, buf, count);
}
static inline void SPI_send(void *buf, int count) {
    SPI0_send(SPI0_ctx, buf, count);
}
static inline void SPI_receive(void *buf, int count) {
    SPI0_receive(SPI0_ctx, buf, count);
}
static inline void SPI_end_transaction(void) {
    SPI0_end_transaction(SPI0_ctx);
}
static inline void SPI_dummy_clocks(int clocks) {
    SPI0_dummy_clocks(SPI0_ctx, clocks);
}
static inline uint8_t SPI_receive_byte(void) {
    return SPI0_receive_byte(SPI0_ctx);
}

#endif  // __SPI_H__
