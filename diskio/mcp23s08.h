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

#ifndef __MCP23S08_H__
#define __MCP23S08_H__

#include <stdint.h>

struct MCP23S08;
extern struct MCP23S08 *MCP23S08_ctx;

#define MCP23S08_PINMODE_OUTPUT 0
#define MCP23S08_PINMODE_INPUT  1

void mcp23s08_init(struct MCP23S08 *ctx, int clock_speed, uint8_t addr);
int mcp23s08_probe(struct MCP23S08 *ctx, int clock_speed, uint8_t addr);
int mcp23s08_is_alive(struct MCP23S08 *ctx);
int mcp23s08_set_pending(struct MCP23S08 *ctx, int pending);
void mcp23s08_pinmode(struct MCP23S08 *ctx, int gpio, int mode);
void mcp23s08_write(struct MCP23S08 *ctx, int gpio, int val);
void mcp23s08_masked_write(struct MCP23S08 *ctx, uint32_t mask, uint32_t val);
void mcp23s08_dump_regs(struct MCP23S08 *ctx, const char *header);

#endif  // __MCP23S08_H__
