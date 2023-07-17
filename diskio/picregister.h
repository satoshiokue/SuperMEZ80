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

#ifndef __PICREGISTER_H__
#define __PICREGISTER_H__

#define PORT_CAT(x, y) PORT_CAT_(x, y)
#define PORT_CAT_(x, y) x ## y
#define PORT_CAT3(x, y, z) PORT_CAT3_(x, y, z)
#define PORT_CAT3_(x, y, z) x ## y ## z
#define TRIS(port) PORT_CAT(TRIS, port)
#define LAT(port) PORT_CAT(LAT, port)
#define R(port) PORT_CAT(R, port)
#define PPS(port) PORT_CAT3(R, port, PPS)
#define WPU(port) PORT_CAT(WPU, port)
#define PORT(port) PORT_CAT(PORT, port)
#define PPS_IN(port) PORT_CAT(PPS_IN, port)
#define PPS_OUT(port) PORT_CAT(PPS_OUT_, port)

#define SPIx(reg) PORT_CAT(SPI_HW_INST, reg)

#define PPS_INA 0
#define PPS_INA0 ((PPS_INA << 3) | 0)
#define PPS_INA1 ((PPS_INA << 3) | 1)
#define PPS_INA2 ((PPS_INA << 3) | 2)
#define PPS_INA3 ((PPS_INA << 3) | 3)
#define PPS_INA4 ((PPS_INA << 3) | 4)
#define PPS_INA5 ((PPS_INA << 3) | 5)
#define PPS_INA6 ((PPS_INA << 3) | 6)
#define PPS_INA7 ((PPS_INA << 3) | 7)

#define PPS_INB 1
#define PPS_INB0 ((PPS_INB << 3) | 0)
#define PPS_INB1 ((PPS_INB << 3) | 1)
#define PPS_INB2 ((PPS_INB << 3) | 2)
#define PPS_INB3 ((PPS_INB << 3) | 3)
#define PPS_INB4 ((PPS_INB << 3) | 4)
#define PPS_INB5 ((PPS_INB << 3) | 5)
#define PPS_INB6 ((PPS_INB << 3) | 6)
#define PPS_INB7 ((PPS_INB << 3) | 7)

#define PPS_INC 2
#define PPS_INC0 ((PPS_INC << 3) | 0)
#define PPS_INC1 ((PPS_INC << 3) | 1)
#define PPS_INC2 ((PPS_INC << 3) | 2)
#define PPS_INC3 ((PPS_INC << 3) | 3)
#define PPS_INC4 ((PPS_INC << 3) | 4)
#define PPS_INC5 ((PPS_INC << 3) | 5)
#define PPS_INC6 ((PPS_INC << 3) | 6)
#define PPS_INC7 ((PPS_INC << 3) | 7)

#define PPS_IND 3
#define PPS_IND0 ((PPS_IND << 3) | 0)
#define PPS_IND1 ((PPS_IND << 3) | 1)
#define PPS_IND2 ((PPS_IND << 3) | 2)
#define PPS_IND3 ((PPS_IND << 3) | 3)
#define PPS_IND4 ((PPS_IND << 3) | 4)
#define PPS_IND5 ((PPS_IND << 3) | 5)
#define PPS_IND6 ((PPS_IND << 3) | 6)
#define PPS_IND7 ((PPS_IND << 3) | 7)

// PPS output
#define PPS_OUT_ADGRB       0x45
#define PPS_OUT_ADGRA       0x44
#define PPS_OUT_DSM1        0x43
#define PPS_OUT_CLKR        0x42
#define PPS_OUT_NCO3        0x41
#define PPS_OUT_NCO2        0x40
#define PPS_OUT_NCO1        0x3f

#define PPS_OUT_TMR0        0x39
#define PPS_OUT_I2C1SDA     0x38
#define PPS_OUT_I2C1SCL     0x37
#define PPS_OUT_SPI2SS      0x36
#define PPS_OUT_SPI2SDO     0x35
#define PPS_OUT_SPI2SCK     0x34
#define PPS_OUT_SPI1SS      0x33
#define PPS_OUT_SPI1SDO     0x32
#define PPS_OUT_SPI1SCK     0x31
#define PPS_OUT_C2OUT       0x30
#define PPS_OUT_C1OUT       0x2f
#define PPS_OUT_UART5RTS    0x2e
#define PPS_OUT_UART5TXDE   0x2d
#define PPS_OUT_UART5TX     0x2c
#define PPS_OUT_UART4RTS    0x2b
#define PPS_OUT_UART4TXDE   0x2a
#define PPS_OUT_UART4TX     0x29
#define PPS_OUT_UART3RTS    0x28
#define PPS_OUT_UART3TXDE   0x27
#define PPS_OUT_UART3TX     0x26
#define PPS_OUT_UART2RTS    0x25
#define PPS_OUT_UART2TXDE   0x24
#define PPS_OUT_UART2TX     0x23
#define PPS_OUT_UART1RTS    0x22
#define PPS_OUT_UART1TXDE   0x21
#define PPS_OUT_UART1TX     0x20

#define PPS_OUT_PWM3S1P2    0x1d
#define PPS_OUT_PWM3S1P1    0x1c
#define PPS_OUT_PWM2S1P2    0x1b
#define PPS_OUT_PWM2S1P1    0x1a
#define PPS_OUT_PWM1S1P2    0x19
#define PPS_OUT_PWM1S1P1    0x18
#define PPS_OUT_CCP3        0x17
#define PPS_OUT_CCP2        0x16
#define PPS_OUT_CCP1        0x15
#define PPS_OUT_CWG3D       0x14
#define PPS_OUT_CWG3C       0x13
#define PPS_OUT_CWG3B       0x12
#define PPS_OUT_CWG3A       0x11
#define PPS_OUT_CWG2D       0x10
#define PPS_OUT_CWG2C       0x0f
#define PPS_OUT_CWG2B       0x0e
#define PPS_OUT_CWG2A       0x0d
#define PPS_OUT_CWG1D       0x0c
#define PPS_OUT_CWG1C       0x0b
#define PPS_OUT_CWG1B       0x0a
#define PPS_OUT_CWG1A       0x09
#define PPS_OUT_CLC8        0x08
#define PPS_OUT_CLC7        0x08
#define PPS_OUT_CLC6        0x08
#define PPS_OUT_CLC5        0x08
#define PPS_OUT_CLC4        0x08
#define PPS_OUT_CLC3        0x08
#define PPS_OUT_CLC2        0x08
#define PPS_OUT_CLC1        0x08
#define PPS_OUT_LATxy       0x00

#endif  // __PICREGISTER_H__
