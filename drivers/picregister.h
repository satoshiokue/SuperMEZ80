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
#define PORT_CAT4(a, b, c, d) PORT_CAT4_(a, b, c, d)
#define PORT_CAT4_(a, b, c, d) a ## b ## c ## d

#define PORT_NAME(port) PORT_CAT(PORTNAME_, port)
#define PORT_BIT(port) PORT_CAT(PORTBIT_, port)

#define TRIS(port) PORT_CAT(TRIS, port)
#define LAT(port) PORT_CAT(LAT, port)
#define R(port) PORT_CAT(R, port)
#define PPS(port) PORT_CAT3(R, port, PPS)
#define WPU(port) PORT_CAT(WPU, port)
#define PORT(port) PORT_CAT(PORT, port)
#define PPS_IN(port) PORT_CAT(PPS_IN, port)
#define PPS_OUT(port) PORT_CAT(PPS_OUT_, port)
#define SLRCON(port) PORT_CAT4(SLRCON, PORT_NAME(port), bits.SLR, port)

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
#define PPS_OUT_CLC7        0x07
#define PPS_OUT_CLC6        0x06
#define PPS_OUT_CLC5        0x05
#define PPS_OUT_CLC4        0x04
#define PPS_OUT_CLC3        0x03
#define PPS_OUT_CLC2        0x02
#define PPS_OUT_CLC1        0x01
#define PPS_OUT_LATxy       0x00

// CLC input selection
#define CLC_IN_0                0
#define CLC_IN_1                1
#define CLC_IN_2                2
#define CLC_IN_3                3
#define CLC_IN_4                4
#define CLC_IN_5                5
#define CLC_IN_6                6
#define CLC_IN_7                7
#define CLC_IN_FOSC             8
#define CLC_IN_HFINTOSC         9
#define CLC_IN_LFINTOSC         10
#define CLC_IN_MFINTOSC         11
#define CLC_IN_MFINTOSC_32KHZ   12
#define CLC_IN_SFINTOSC_1MHZ    13
#define CLC_IN_SOSC             14
#define CLC_IN_EXTOSC           15
#define CLC_IN_ADCRC            16
#define CLC_IN_CLKR             17
#define CLC_IN_TMR0             18
#define CLC_IN_TMR1             19
#define CLC_IN_TMR2             20
#define CLC_IN_TMR3             21
#define CLC_IN_TMR4             22
#define CLC_IN_TMR5             23
#define CLC_IN_TMR6             24
#define CLC_IN_SMT1             30
#define CLC_IN_CCP1             31
#define CLC_IN_CCP2             32
#define CLC_IN_CCP3             33
#define CLC_IN_PWM1S1P1_OUT     34
#define CLC_IN_PWM1S1P2_OUT     35
#define CLC_IN_PWM2S1P1_OUT     36
#define CLC_IN_PWM2S1P2_OUT     37
#define CLC_IN_PWM3S1P1_OUT     38
#define CLC_IN_PWM3S1P2_OUT     39
#define CLC_IN_NCO1             42
#define CLC_IN_NCO2             43
#define CLC_IN_NCO3             44
#define CLC_IN_CMP1_OUT         45
#define CLC_IN_CMP2_OUT         46
#define CLC_IN_ZCD              47
#define CLC_IN_IOC              48
#define CLC_IN_DSM1             49
#define CLC_IN_HLVD_OUT         50
#define CLC_IN_CLC1             51
#define CLC_IN_CLC2             52
#define CLC_IN_CLC3             53
#define CLC_IN_CLC4             54
#define CLC_IN_CLC5             55
#define CLC_IN_CLC6             56
#define CLC_IN_CLC7             57
#define CLC_IN_CLC8             58
#define CLC_IN_U1TX             59
#define CLC_IN_U2TX             60
#define CLC_IN_U3TX             61
#define CLC_IN_U4TX             62
#define CLC_IN_U5TX             63
#define CLC_IN_SPI1_SDO         64
#define CLC_IN_SPI1_SCK         65
#define CLC_IN_SPI1_SS          66
#define CLC_IN_SPI2_SDO         67
#define CLC_IN_SPI2_SCK         68
#define CLC_IN_SPI2_SS          69
#define CLC_IN_I2C_SCL          70
#define CLC_IN_I2C_SDA          71
#define CLC_IN_CWG1A            72
#define CLC_IN_CWG1B            73
#define CLC_IN_CWG2A            74
#define CLC_IN_CWG2B            75
#define CLC_IN_CWG3A            76
#define CLC_IN_CWG3B            77

#define PORTNAME_A0 A
#define PORTNAME_A1 A
#define PORTNAME_A2 A
#define PORTNAME_A3 A
#define PORTNAME_A4 A
#define PORTNAME_A5 A
#define PORTNAME_A6 A
#define PORTNAME_A7 A
#define PORTBIT_A0 0
#define PORTBIT_A1 1
#define PORTBIT_A2 2
#define PORTBIT_A3 3
#define PORTBIT_A4 4
#define PORTBIT_A5 5
#define PORTBIT_A6 6
#define PORTBIT_A7 7

#define PORTNAME_B0 B
#define PORTNAME_B1 B
#define PORTNAME_B2 B
#define PORTNAME_B3 B
#define PORTNAME_B4 B
#define PORTNAME_B5 B
#define PORTNAME_B6 B
#define PORTNAME_B7 B
#define PORTBIT_B0 0
#define PORTBIT_B1 1
#define PORTBIT_B2 2
#define PORTBIT_B3 3
#define PORTBIT_B4 4
#define PORTBIT_B5 5
#define PORTBIT_B6 6
#define PORTBIT_B7 7

#define PORTNAME_C0 C
#define PORTNAME_C1 C
#define PORTNAME_C2 C
#define PORTNAME_C3 C
#define PORTNAME_C4 C
#define PORTNAME_C5 C
#define PORTNAME_C6 C
#define PORTNAME_C7 C
#define PORTBIT_C0 0
#define PORTBIT_C1 1
#define PORTBIT_C2 2
#define PORTBIT_C3 3
#define PORTBIT_C4 4
#define PORTBIT_C5 5
#define PORTBIT_C6 6
#define PORTBIT_C7 7

#define PORTNAME_D0 D
#define PORTNAME_D1 D
#define PORTNAME_D2 D
#define PORTNAME_D3 D
#define PORTNAME_D4 D
#define PORTNAME_D5 D
#define PORTNAME_D6 D
#define PORTNAME_D7 D
#define PORTBIT_D0 0
#define PORTBIT_D1 1
#define PORTBIT_D2 2
#define PORTBIT_D3 3
#define PORTBIT_D4 4
#define PORTBIT_D5 5
#define PORTBIT_D6 6
#define PORTBIT_D7 7

#define PORTNAME_E0 E
#define PORTNAME_E1 E
#define PORTNAME_E2 E
#define PORTNAME_E3 E
#define PORTNAME_E4 E
#define PORTNAME_E5 E
#define PORTNAME_E6 E
#define PORTNAME_E7 E
#define PORTBIT_E0 0
#define PORTBIT_E1 1
#define PORTBIT_E2 2
#define PORTBIT_E3 3
#define PORTBIT_E4 4
#define PORTBIT_E5 5
#define PORTBIT_E6 6
#define PORTBIT_E7 7

#endif  // __PICREGISTER_H__
