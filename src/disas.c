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

#include <stdio.h>
#include <disas.h>
#include <disas_z80.h>

unsigned int disas_op(const disas_inst_desc_t *ids, uint8_t *insts, unsigned int len, char *buf,
             unsigned int buf_len)
{
    const disas_inst_desc_t *ent;
    uint8_t *pc = insts;
    uint16_t operand;
    unsigned int result;

    if (len == 0)
        return 0;
    for (ent = ids; ent->attr != DISAS_END; ent++) {
        if (ent->op == *pc) {
            break;
        }
    }
    if (ent->op != *pc) {
        sprintf(buf, "???");
        return 1;
    }
    switch (ent->attr) {
    case DISAS_OPERAND_0:
        sprintf(buf, "%s", (char*)ent->ptr);
        result = 1;
        break;
    case DISAS_OPERAND_1:
        if (len < 2)
            return 0;
        operand = *++pc;
        sprintf(buf, (char*)ent->ptr, operand);
        result = 2;
        break;
    case DISAS_OPERAND_2:
        if (len < 3)
            return 0;
        operand = ((uint16_t)pc[2] << 8) + pc[1];
        sprintf(buf, (char*)ent->ptr, operand);
        result = 3;
        break;
    default:
        sprintf(buf, "???");
        break;
    }

    return result;
}

unsigned int disas_ops(const disas_inst_desc_t *ids, uint32_t addr, uint8_t *insts,
                       unsigned int len, unsigned int nops, void (*func)(char *line))
{
    unsigned int n;
    unsigned int result = 0;
    char buf[100];

    while (result < len && 0 < nops) {
        n = disas_op(ids, insts, len - result, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (func) {
        } else {
            printf("%06lX ", addr);
            for (int i = 0; i < 4; i++) {
                if (i < n)
                    printf("%02X ", insts[i]);
                else
                    printf("   ");
            }
            printf("%s\n\r", buf);
        }
        result += n;
        addr += (unsigned)n;
        insts += n;
        nops--;
    }

    return result;
}

#if 0
int main(int ac, char *av[])
{
    static uint8_t rom[] = {
        #include <ipl.inc>
    };

    disas_ops(disas_z80, 0x0000, rom, sizeof(rom), sizeof(rom), NULL);

    return 0;
}
#endif
