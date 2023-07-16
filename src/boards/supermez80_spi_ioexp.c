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

#define BOARD_DEPENDENT_SOURCE

#include <supermez80.h>

#define SPI_IOEXP_PICO       LATC3  // controller data out
#define SPI_IOEXP_PICO_TRIS  TRISC3
#define SPI_IOEXP_CLK        LATC4  // clock
#define SPI_IOEXP_CLK_TRIS   TRISC4
#define SPI_IOEXP_POCI       ((uint8_t)((PORTC & 0x20) ? 1 : 0))  // controller data in
#define SPI_IOEXP_POCI_TRIS  TRISC5
#define SPI_IOEXP_CS         LATE2  // chip select
#define SPI_IOEXP_CS_TRIS    TRISE2
#define SPI_IOEXP_CS_ANSEL   ANSELE2

#define SPI_PREFIX SPI_IOEXP
#include <SPI.c>
#include <mcp23S08.c>
