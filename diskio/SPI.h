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

#define SPI_MSBFIRST 1
#define SPI_LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

void SPI_begin(void);
void SPI_configure(uint16_t clock_delay, uint8_t bitOrder, uint8_t dataMode);
void SPI_begin_transaction(void);
uint8_t SPI_transfer_byte(uint8_t output);
void SPI_transfer(void *buf, int count);
void SPI_send(void *buf, int count);
void SPI_receive(void *buf, int count);
void SPI_end_transaction(void);

void SPI_send_mode0_msbfirst_nowait(void *buf, int count);
void SPI_receive_mode0_msbfirst_nowait(void *buf, int count);

void SPI_dummy_clocks(int clocks);
uint8_t SPI_receive_byte(void);
uint8_t SPI_reserve_bit_order(uint8_t val);

#endif  // __SPI_H__
