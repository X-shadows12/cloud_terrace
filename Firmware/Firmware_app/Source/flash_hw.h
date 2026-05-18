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

#ifndef __FLASH_HW_H__
#define __FLASH_HW_H__

#include "board_port.h"
#include "ctm_types.h"

#define FLASH_HW_SECTOR_SIZE           BOARD_FLASH_SECTOR_SIZE
#define FLASH_HW_PAGE_SIZE             BOARD_FLASH_PAGE_SIZE

#define FLASH_HW_APP_MAIN_ADDR         BOARD_FLASH_APP_MAIN_ADDR
#define FLASH_HW_APP_BACK_ADDR         BOARD_FLASH_APP_BACK_ADDR
#define FLASH_HW_APP_MAX_SIZE          BOARD_FLASH_APP_MAX_SIZE

#define FLASH_HW_BOOTLOADER_ADDR       BOARD_FLASH_BOOTLOADER_ADDR
#define FLASH_HW_BOOTLOADER_MAX_SIZE   BOARD_FLASH_BOOTLOADER_MAX_SIZE

#define FLASH_HW_USR_CONFIG_ADDR       BOARD_FLASH_USR_CONFIG_ADDR
#define FLASH_HW_USR_CONFIG_MAX_SIZE   BOARD_FLASH_USR_CONFIG_MAX_SIZE

#define FLASH_HW_COGGING_MAP_ADDR      BOARD_FLASH_COGGING_MAP_ADDR
#define FLASH_HW_COGGING_MAP_MAX_SIZE  BOARD_FLASH_COGGING_MAP_MAX_SIZE

void FLASH_HW_invalidate_cache(uint32_t addr, uint32_t size);
int  FLASH_HW_erase_region(uint32_t addr, uint32_t size);
int  FLASH_HW_program_words(uint32_t addr, const uint32_t *data, uint32_t word_count);
void FLASH_HW_jump_bootloader(void);

#endif
