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

#include <xc.h>
#include <stdio.h>
#include "ff.h"
#include "diskio.h"
#include "SDCard.h"
#include "utils.h"
#include "fatdisk_debug.h"

#define DEBUG
#if defined(DEBUG)
static int debug_flags = 0;
#else
static const int debug_flags = 0;
#endif

#define dprintf(args) do { if (debug_flags) printf args; } while(0)
#define drprintf(args) do { if (debug_flags & FATDISK_DEBUG_READ) printf args; } while(0)
#define dwprintf(args) do { if (debug_flags & FATDISK_DEBUG_WRITE) printf args; } while(0)

#define SECTOR_SIZE 512
#define INVALID_LBA 0xffffffff
static uint32_t start_lba = INVALID_LBA;

DWORD get_fattime()
{
    // 1990/01/01
    return (DWORD)0 << 25 | (DWORD)1 << 21 | (DWORD)1 << 16;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    uint8_t buf[128];

    dprintf(("disk_initialize(%d)...\n\r", pdrv));
    if (pdrv != 0) {
        dprintf(("physical driver != 0\n\r"));
        return STA_NODISK;
    }

    // read MBR partition table at sector 0
    dprintf(("read MBR partition table at sector 0\n\r"));
    if (SDCard_read512(0, 384, buf, 128) != SDCARD_SUCCESS) {
        dprintf(("failed to read sector 0\n\r"));
        return STA_NODISK;
    } else {
        if (debug_flags)
            util_hexdump("", buf, 128);
    }
    if (buf[126] != 0x55 || buf[127] != 0xaa) {
        dprintf(("no MBR signature\n\r"));
        return STA_NODISK;
    }
    //
    // 0x01 FAT12
    // 0x04 FAT16 (up to 32MB)
    // 0x06 FAT16 (over 32MB)
    // 0x0b FAT32
    // 0x0c FAT32 LBA
    // 0x0e FAT16 LBA
    //
    if (buf[66] != 0x01 && buf[66] != 0x04 && buf[66] != 0x06 &&
        buf[66] != 0x0b && buf[66] != 0x0c && buf[66] != 0x0e) {
        dprintf(("no FAT32 partition\n\r"));
        return STA_NODISK;
    }
    start_lba = buf[73];
    start_lba = (start_lba << 8) | buf[72];
    start_lba = (start_lba << 8) | buf[71];
    start_lba = (start_lba << 8) | buf[70];
    dprintf(("partition starts at sector %ld\n\r", start_lba));

    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) {
        dprintf(("physical driver != 0\n\r"));
        return STA_NODISK;
    }
    if (start_lba == INVALID_LBA) {
        dprintf(("no valid volume\n\r"));
        return STA_NODISK;
    }
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    drprintf(("disk_read:  pdrv=%d, sector=%ld, count=%d\n\r", pdrv, sector, count));

    for (int i = 0; i < count; i++) {
        if (SDCard_read512(start_lba + sector, 0, buff, SECTOR_SIZE) != SDCARD_SUCCESS) {
            dprintf(("failed to read sector %ld\n\r", sector));
            return RES_ERROR;
        }
        if ((debug_flags & FATDISK_DEBUG_READ) && (debug_flags & FATDISK_DEBUG_VERBOSE)) {
            util_addrdump("fat: ", sector * SECTOR_SIZE, buff, SECTOR_SIZE);
        }
        sector++;
        buff += SECTOR_SIZE;
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    dwprintf(("disk_write: pdrv=%d, sector=%ld, count=%d\n\r", pdrv, sector, count));

    for (int i = 0; i < count; i++) {
        if (SDCard_write512(start_lba + sector, 0, buff, SECTOR_SIZE) != SDCARD_SUCCESS) {
            dprintf(("failed to write sector %ld\n\r", sector));
            return RES_ERROR;
        }
        if ((debug_flags & FATDISK_DEBUG_WRITE) && (debug_flags & FATDISK_DEBUG_VERBOSE)) {
            util_addrdump("fat: ", sector * SECTOR_SIZE, buff, SECTOR_SIZE);
        }
        sector++;
        buff += SECTOR_SIZE;
    }

    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    DRESULT res = RES_ERROR;

    // See diskio.h for meaning of ioctl commands
    switch (cmd) {
    case CTRL_SYNC:
        res = RES_OK;
        break;
    case GET_SECTOR_COUNT:
    case GET_SECTOR_SIZE:
    case GET_BLOCK_SIZE:
    case CTRL_TRIM:
    default:
        printf("disk_ioctl: pdrv=%d, cmd=%d: Not handled.\n\r", pdrv, cmd);
        break;
    }

    dprintf(("disk_ioctl: pdrv=%d, cmd=%d, buff=0x%lx: res=%d\n\r", pdrv, cmd, (long)buff, res));
    return res;
}

int fatdisk_debug(int newval)
{
    int res = debug_flags;
#if defined(DEBUG)
    debug_flags = newval;
#endif
    return res;
}
