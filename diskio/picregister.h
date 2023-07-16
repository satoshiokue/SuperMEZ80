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

#endif  // __PICREGISTER_H__
