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

#ifndef __UTILS_H__
#define __UTILS_H__

#define UTIL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define UTIL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define UTIL_ARRAYSIZEOF(a) (sizeof(a)/sizeof(*(a)))

void util_hexdump(const char *header, const void *addr, unsigned int size);
void util_hexdump_sum(const char *header, const void *addr, unsigned int size);
void util_addrdump(const char *header, uint32_t addr_offs, const void *addr, unsigned int size);
int util_stricmp(const char *a, const char *b);

#endif  // __UTILS_H__
