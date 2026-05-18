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

#define FLASH_HW_DCACHE_LINE_SIZE 32U

void FLASH_HW_invalidate_cache(uint32_t addr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (size == 0U) {
        return;
    }

    uint32_t start = addr & ~(FLASH_HW_DCACHE_LINE_SIZE - 1U);
    uint32_t end   = (addr + size + FLASH_HW_DCACHE_LINE_SIZE - 1U) & ~(FLASH_HW_DCACHE_LINE_SIZE - 1U);

    SCB_InvalidateDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) addr;
    (void) size;
#endif
}

int FLASH_HW_erase_region(uint32_t addr, uint32_t size)
{
    uint32_t       end = addr + size;
    fmc_state_enum status;

    fmc_unlock();

    for (uint32_t cur = addr; cur < end; cur += FLASH_HW_PAGE_SIZE) {
        fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_WPERR | FMC_FLAG_PGSERR);
        status = fmc_sector_erase(cur);
        if (status != FMC_READY) {
            fmc_lock();
            return -1;
        }
    }

    fmc_lock();

    FLASH_HW_invalidate_cache(addr, size);

    for (uint32_t cur = addr; cur < end; cur += 4U) {
        if (0xFFFFFFFFU != REG32(cur)) {
            return -2;
        }
    }

    return 0;
}

int FLASH_HW_program_words(uint32_t addr, const uint32_t *data, uint32_t word_count)
{
    fmc_state_enum status;

    fmc_unlock();

    for (uint32_t i = 0U; i < word_count; i++) {
        uint32_t cur = addr + (i * 4U);
        fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_WPERR | FMC_FLAG_PGSERR);
        status = fmc_word_program(cur, data[i]);
        if (status != FMC_READY) {
            fmc_lock();
            return -1;
        }
    }

    fmc_lock();

    FLASH_HW_invalidate_cache(addr, word_count * 4U);

    for (uint32_t i = 0U; i < word_count; i++) {
        uint32_t cur = addr + (i * 4U);
        if (data[i] != REG32(cur)) {
            return -2;
        }
    }

    return 0;
}

void FLASH_HW_jump_bootloader(void)
{
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    LED_ACT_RESET();

    __set_MSP(*(uint32_t *) FLASH_HW_BOOTLOADER_ADDR);
    (*(void (*)(void))(*(uint32_t *) (FLASH_HW_BOOTLOADER_ADDR + 4U)))();
}
