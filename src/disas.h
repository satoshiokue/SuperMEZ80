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

#ifndef __DISAS_H__
#define __DISAS_H__

#include <stdint.h>

#define DISAS_OPERAND     0x00
#define DISAS_OPERAND_0   0x00
#define DISAS_OPERAND_1   0x01
#define DISAS_OPERAND_2   0x02
#define DISAS_PREFIX_FUNC 0x10
#define DISAS_BIG_ENDIAN  0xe0
#define DISAS_END         0xf0

typedef struct disas_inst_desc {
    uint8_t attr;
    uint8_t op;
    void *ptr;
} disas_inst_desc_t;

int disas_op(const disas_inst_desc_t *ids, uint8_t *inst, int len, char *buf, int buf_len);
int disas_ops(const disas_inst_desc_t *ids, uint32_t addr, uint8_t *insts, int len, int nops,
              void (*func)(char *line));

#endif  // __DISAS_H__
