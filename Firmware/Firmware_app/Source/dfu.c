/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
    The ctm firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    The ctm firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "flash_hw.h"
#include "dfu.h"
#include "usr_config.h"
#include "util.h"
#include <string.h>

static uint32_t mByteCount;
static bool     mDfuActive;

int DFU_write_app_back_start(void)
{
    mDfuActive = false;

    if (FLASH_HW_erase_region(FLASH_HW_APP_BACK_ADDR, FLASH_HW_APP_MAX_SIZE)) {
        return -1;
    }

    mByteCount = 0;
    mDfuActive = true;

    return 0;
}

int DFU_write_app_back(uint8_t *data, uint8_t num)
{
    int ret = 0;

    if (!mDfuActive) {
        return -2;
    }

    if ((num == 0U) || (num > 8U) || ((num % 4U) != 0U)) {
        mDfuActive = false;
        return -3;
    }

    if ((mByteCount + num) <= FLASH_HW_APP_MAX_SIZE) {
        uint32_t words[2] = {0xFFFFFFFFU, 0xFFFFFFFFU};

        memcpy(words, data, num);
        ret = FLASH_HW_program_words(FLASH_HW_APP_BACK_ADDR + mByteCount, words, num / 4U);
        if (ret == 0) {
            mByteCount += num;
        } else {
            mDfuActive = false;
        }
    } else {
        mDfuActive = false;
        ret = -1;
    }

    return ret;
}

int DFU_check_app_back(uint32_t size, uint32_t crc)
{
    if (!mDfuActive) {
        return -3;
    }

    if ((size == 0U) || (size > FLASH_HW_APP_MAX_SIZE) || ((size % 4U) != 0U)) {
        mDfuActive = false;
        return -4;
    }

    if (size != mByteCount) {
        mDfuActive = false;
        return -1;
    }

    FLASH_HW_invalidate_cache(FLASH_HW_APP_BACK_ADDR, size);
    uint32_t crc_calc = crc32((uint8_t *) (FLASH_HW_APP_BACK_ADDR), size);
    if (crc_calc != crc) {
        mDfuActive = false;
        return -2;
    }

    return 0;
}

void DFU_jump_bootloader(void)
{
    FLASH_HW_jump_bootloader();
}
