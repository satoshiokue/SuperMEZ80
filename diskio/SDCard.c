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
#include "SPI.h"
#include "SDCard.h"
#include "utils.h"

#define DEBUG
#if defined(DEBUG)
static int debug_flags = 0;
#else
static const int debug_flags = 0;
#endif

#define dprintf(args) do { if (debug_flags) printf args; } while(0)
#define drprintf(args) do { if (debug_flags & SDCARD_DEBUG_READ) printf args; } while(0)
#define dwprintf(args) do { if (debug_flags & SDCARD_DEBUG_WRITE) printf args; } while(0)

static struct SDCard {
    struct SPI *spi;
    uint16_t timeout;
    unsigned int calc_read_crc :1;
} ctx_ = { 0 };

void SDCard_end_transaction()
{
    struct SPI *spi = ctx_.spi;
    SPI(end_transaction)(spi);
    SPI(dummy_clocks)(spi, 1);
}

int SDCard_init(int initial_clock_speed, int clock_speed, uint16_t timeout)
{
    ctx_.spi = SPI(ctx);
    ctx_.timeout = timeout;
    ctx_.calc_read_crc = 0;
    struct SPI *spi = ctx_.spi;
    SPI(begin)(spi);

    uint8_t buf[5];
    dprintf(("\n\rSD Card: initialize ...\n\r"));

    SPI(configure)(spi, initial_clock_speed, SPI_MSBFIRST, SPI_MODE0);
    SPI(begin_transaction)(spi);
    SPI(dummy_clocks)(spi, 10);
    SDCard_end_transaction();

    // CMD0 go idle state
    SDCard_command(0, 0, buf, 1);
    dprintf(("SD Card: CMD0, R1=%02x\n\r", buf[0]));
    if (buf[0] != SDCARD_R1_IDLE_STATE) {
        dprintf(("SD Card: timeout\n\r"));
        return SDCARD_TIMEOUT;
    }

    // CMD8 send interface condition
    SDCard_command(8, 0x000001aa, buf, 5);
    dprintf(("SD Card: CMD8, R7=%02x %02x %02x %02x %02x\n\r",
             buf[0], buf[1], buf[2], buf[3], buf[4]));
    if (buf[0] != SDCARD_R1_IDLE_STATE || (buf[3] & 0x01) != 0x01 || buf[4] != 0xaa) {
        dprintf(("SD Card: not supoprted\n\r"));
        return SDCARD_NOTSUPPORTED;
    }

    // ACMD41 send operating condition
    for (int i = 0; i < 3000; i++) {
        SDCard_command(55, 0, buf, 1);
        SDCard_command(41, 1UL << 30 /* HCS bit (Host Capacity Support) is 1 */, buf, 5);
        if (buf[0] == 0x00)
            break;
    }
    dprintf(("SD Card: ACMD41, R1=%02x\n\r", buf[0]));
    if (buf[0] != 0x00) {
        dprintf(("SD Card: ACMD41 response is %02x\n\r", buf[0]));
        return SDCARD_TIMEOUT;
    }

    // CMD58 read OCR register
    SDCard_command(58, 0, buf, 5);
    dprintf(("SD Card: CMD58, R3=%02x %02x %02x %02x %02x\n\r",
             buf[0], buf[1], buf[2], buf[3], buf[4]));
    if (buf[0] & 0xfe) {
        dprintf(("SD Card: unexpected response %02x\n\r", buf[0]));
        return SDCARD_BADRESPONSE;
    }
    if (!(buf[1] & 0x40)) {
        dprintf(("SD Card: CCS (Card Capacity Status) is 0\n\r"));
        return SDCARD_NOTSUPPORTED;
    }
    dprintf(("SD Card: SDHC or SDXC card detected\n\r"));

    if (!(buf[1] & 0x80)) {
        dprintf(("SD Card: Card power up status bis is 0\n\r"));
        return SDCARD_BADRESPONSE;
    }
    dprintf(("SD Card: ready.\n\r"));

    // CMD59 turn on CRC
    SDCard_command(59, 1, buf, 1);
    if (buf[0] != 0x00) {
        dprintf(("SD Card: CMD59 response is %02x\n\r", buf[0]));
        return SDCARD_BADRESPONSE;
    }

    SPI(configure)(spi, clock_speed, SPI_MSBFIRST, SPI_MODE0);

    dprintf(("SD Card: initialize ... succeeded\n\r"));

    return SDCARD_SUCCESS;
}

static uint8_t __SDCard_wait_response(uint8_t no_response, unsigned int attempts)
{
    struct SPI *spi = ctx_.spi;
    uint8_t response;
    do {
        response = SPI(receive_byte)(spi);
    } while ((response == no_response) && 0 < --attempts);
    return response;
}

static int __SDCard_command_r1(uint8_t command, uint32_t argument, uint8_t *r1)
{
    struct SPI *spi = ctx_.spi;
    uint8_t buf[6];
    uint8_t response;

    buf[0] = command | 0x40;
    buf[1] = (argument >> 24) & 0xff;
    buf[2] = (argument >> 16) & 0xff;
    buf[3] = (argument >>  8) & 0xff;
    buf[4] = (argument >>  0) & 0xff;
    buf[5] = SDCard_crc(buf, 5) | 0x01;

    SPI(begin_transaction)(spi);
    SPI(dummy_clocks)(spi, 1);
    SPI(send)(spi, buf, 6);

    response = __SDCard_wait_response(0xff, ctx_.timeout);
    *r1 = response;
    if (response == 0xff) {
        return SDCARD_TIMEOUT;
    }

    return SDCARD_SUCCESS;
}

int SDCard_read512(uint32_t addr, unsigned int offs, void *buf, unsigned int count)
{
    struct SPI *spi = ctx_.spi;
    int result;
    uint8_t response;
    uint16_t crc, resp_crc;
    int retry = 5;

    drprintf(("SD Card:  read512: addr=%8ld, offs=%d, count=%d\n\r", addr, offs, count));

 retry:
    result = __SDCard_command_r1(17, addr, &response);
    if (result != SDCARD_SUCCESS) {
        goto done;
    }
    if (response != 0) {
        result = SDCARD_BADRESPONSE;
        goto done;
    }

    response = __SDCard_wait_response(0xff, 3000);
    if (response == 0xff) {
        result = SDCARD_TIMEOUT;
        goto done;
    }
    if (response != 0xfe) {
        result = SDCARD_BADRESPONSE;
        goto done;
    }

    crc = 0;
    for (int i = 0; i < offs; i++) {
        response = SPI(receive_byte)(spi);
        if (ctx_.calc_read_crc)
            crc = __SDCard_crc16(crc, &response, 1);
    }
    SPI(receive)(spi, buf, count);
    if (ctx_.calc_read_crc)
        crc = __SDCard_crc16(crc, buf, count);
    for (unsigned int i = 0; i < 512 - offs - count; i++) {
        response = SPI(receive_byte)(spi);
        if (ctx_.calc_read_crc)
            crc = __SDCard_crc16(crc, &response, 1);
    }
    if ((debug_flags & SDCARD_DEBUG_READ) && (debug_flags & SDCARD_DEBUG_VERBOSE)) {
        util_addrdump("SD: ", (addr * 512) + offs, buf, count);
    }

    resp_crc = (uint16_t)SPI(receive_byte)(spi) << 8;
    resp_crc |= SPI(receive_byte)(spi);
    if (ctx_.calc_read_crc && resp_crc != crc) {
        dprintf(("SD Card: read512: CRC error (%04x != %04x, retry=%d)\n\r",
                 crc, resp_crc, retry));
        if (--retry < 1) {
            result = SDCARD_CRC_ERROR;
            goto done;
        }
        SDCard_end_transaction();
        goto retry;
    }

    result = SDCARD_SUCCESS;

 done:
    SDCard_end_transaction();
    return result;
}

int SDCard_write512(uint32_t addr, unsigned int offs, const void *buf, unsigned int count)
{
    struct SPI *spi = ctx_.spi;
    int result;
    uint8_t response;
    uint16_t crc;
    int retry = 5;

    dwprintf(("SD Card: write512: addr=%8ld, offs=%d, count=%d\n\r", addr, offs, count));
    if ((debug_flags & SDCARD_DEBUG_WRITE) && (debug_flags & SDCARD_DEBUG_VERBOSE)) {
        util_addrdump("SD: ", (addr * 512) + offs, buf, count);
    }

    crc = 0;
    response = 0xff;
    for (int i = 0; i < offs; i++) {
        crc = __SDCard_crc16(crc, &response, 1);
    }
    crc = __SDCard_crc16(crc, buf, count);
    for (unsigned int i = 0; i < 512 - offs - count; i++) {
        crc = __SDCard_crc16(crc, &response, 1);
    }

 retry:
    result = __SDCard_command_r1(24, addr, &response);
    if (result != SDCARD_SUCCESS) {
        goto done;
    }
    if (response != 0) {
        result = SDCARD_BADRESPONSE;
        goto done;
    }

    response = 0xfe;
    SPI(send)(spi, &response, 1);
    SPI(dummy_clocks)(spi, offs);
    SPI(send)(spi, buf, count);
    SPI(dummy_clocks)(spi, 512 - offs - count);
    response = (crc >> 8) & 0xff;
    SPI(send)(spi, &response, 1);
    response = crc & 0xff;
    SPI(send)(spi, &response, 1);

    response = __SDCard_wait_response(0xff, 3000);
    if (response == 0xff) {
        dprintf(("SD Card: write512: failed to get token, timeout\n\r"));
        result = SDCARD_TIMEOUT;
        goto done;
    }
    if ((response & 0x1f) != 0x05) {
        dprintf(("SD Card: write512: token is %02x\n\r", response));
        if ((response & 0x1f) == 0x0b) {
            dprintf(("SD Card: write512: CRC error (retry=%d)\n\r", retry));
            if (--retry < 1) {
                result = SDCARD_CRC_ERROR;
                goto done;
            }
            __SDCard_wait_response(0xff, 30000);
            SDCard_end_transaction();
            goto retry;
        }
        result = SDCARD_BADRESPONSE;
        goto done;
    }

    response = __SDCard_wait_response(0x00, 30000);
    if (response == 0x00) {
        dprintf(("SD Card: write512: timeout, response is %02x\n\r", response));
        result = SDCARD_TIMEOUT;
        goto done;
    }

    result = SDCARD_SUCCESS;

 done:
    SDCard_end_transaction();
    return result;
}

int SDCard_command(uint8_t command, uint32_t argument, void *response_buffer, unsigned int length)
{
    struct SPI *spi = ctx_.spi;
    int result;
    uint8_t *responsep = (uint8_t*)response_buffer;

    result = __SDCard_command_r1(command, argument, responsep);
    if (result != SDCARD_SUCCESS) {
        SDCard_end_transaction();
        return result;
    }

    SPI(receive)(spi, &responsep[1], length - 1);
    SDCard_end_transaction();

    return SDCARD_SUCCESS;
}

uint8_t SDCard_crc(const void *buf, unsigned int count)
{
    uint8_t crc = 0;
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    while (p < endp) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x80)
                crc ^= 0x89;
            crc <<= 1;
        }
    }

    return crc;
}

uint16_t __SDCard_crc16(uint16_t crc, const void *buf, unsigned int count)
{
    uint8_t *p = (uint8_t*)buf;
    uint8_t *endp = p + count;

    while (p < endp) {
        crc = (crc >> 8)|(crc << 8);
        crc ^= *p++;
        crc ^= ((crc & 0xff) >> 4);
        crc ^= (crc << 12);
        crc ^= ((crc & 0xff) << 5);
    }

    return crc;
}

uint16_t SDCard_crc16(const void *buf, unsigned int count)
{
    return __SDCard_crc16(0, buf, count);
}

int SDCard_debug(int newval)
{
    int res = debug_flags;
#if defined(DEBUG)
    debug_flags = newval;
#endif
    return res;
}
