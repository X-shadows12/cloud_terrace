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

#include "gd32h7xx.h"

/* FLASH MAP ---------------------------------------------*/
#define FLASH_SECTOR_SIZE ((uint32_t) 0x1000U) // 4KB sector on GD32H7 main flash
#define PAGE_SIZE         FLASH_SECTOR_SIZE

#define APP_MAIN_ADDR     ((uint32_t) (FLASH_BASE + 0x00000U))
#define APP_BACK_ADDR     ((uint32_t) (FLASH_BASE + 0x40000U))
#define APP_MAX_SIZE      ((uint32_t) (0x40000U)) // 256KB

#define BOOTLOADER_ADDR   ((uint32_t) (FLASH_BASE + 0x80000U))
#define BOOTLOADER_SIZE   ((uint32_t) (0x10000U)) // 64KB

#define FMC_ERROR_FLAGS   (FMC_FLAG_END | FMC_FLAG_WPERR | FMC_FLAG_PGSERR)

static inline void watch_dog_feed(void)
{
    FWDGT_CTL = FWDGT_KEY_RELOAD;
}

static int erase_app_main(void)
{
    uint32_t       addr;
    fmc_state_enum status;

    fmc_unlock();

    // Erase
    for (addr = APP_MAIN_ADDR; addr < APP_MAIN_ADDR + APP_MAX_SIZE; addr += PAGE_SIZE) {
        watch_dog_feed();
        fmc_flag_clear(FMC_ERROR_FLAGS);
        status = fmc_sector_erase(addr);
        if (status != FMC_READY) {
            fmc_lock();
            return -1;
        }
    }

    fmc_lock();

    // Check
    for (addr = APP_MAIN_ADDR; addr < APP_MAIN_ADDR + APP_MAX_SIZE; addr += 4) {
        watch_dog_feed();
        if (0xFFFFFFFF != *((uint32_t *) addr)) {
            return -2;
        }
    }

    return 0;
}

static int write_app_main(uint8_t *data, uint32_t offset, uint32_t len)
{
    uint32_t       addr;
    uint32_t      *word;
    fmc_state_enum status;

    fmc_unlock();

    len /= 4;
    word = (uint32_t *) data;

    // Program
    for (uint32_t i = 0; i < len; i++) {
        watch_dog_feed();
        addr = APP_MAIN_ADDR + offset + 4 * i;
        fmc_flag_clear(FMC_ERROR_FLAGS);
        status = fmc_word_program(addr, word[i]);
        if (status != FMC_READY) {
            fmc_lock();
            return -1;
        }
    }

    fmc_lock();

    // Check
    for (uint32_t i = 0; i < len; i++) {
        watch_dog_feed();
        addr = APP_MAIN_ADDR + offset + 4 * i;
        if (word[i] != REG32(addr)) {
            return -2;
        }
    }

    return 0;
}

static int write_new_app(void)
{
    int ret;

    ret = erase_app_main();
    if (ret != 0) {
        return ret;
    }

    ret = write_app_main((uint8_t *) APP_BACK_ADDR, 0, APP_MAX_SIZE);

    return ret;
}

int main(void)
{
    // Vector Table Relocation in Internal FLASH.
    SCB->VTOR = BOOTLOADER_ADDR;

    // Up to 5 retries
    for (int i = 0; i < 5; i++) {
        int res = write_new_app();
        if (res == 0) {
            // Success
            NVIC_SystemReset();
        }
    }

    // Fail Should not happen
    for (;;) {
        watch_dog_feed();
    }
}
