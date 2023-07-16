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

#endif  // __PICREGISTER_H__
