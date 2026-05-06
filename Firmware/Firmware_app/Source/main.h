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

#ifndef __MAIN_H__
#define __MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "gd32h7xx.h"
#include "gd32h7xx_syscfg.h"
#include "board_gd32h759.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CTM_ENABLE_RTT_SCOPE
#define CTM_ENABLE_RTT_SCOPE 1
#endif

#define CTM_RTT_LOG_ENABLE CTM_ENABLE_RTT_SCOPE

#if CTM_RTT_LOG_ENABLE
// clang-format off
    #include "SEGGER_RTT.h"
    #define DEBUG(format, ...) SEGGER_RTT_printf(0, format, ##__VA_ARGS__);
// clang-format on
#else
#define DEBUG(format, ...)
#endif

#if CTM_ENABLE_RTT_SCOPE
void RTT_init(void);
void RTT_scope_write_raw(const void *data, uint32_t size);
void RTT_scope_write2(float f1, float f2);
void RTT_scope_write6(float f1, float f2, float f3, float f4, float f5, float f6);
#define DEBUG_PLOT(f1, f2) RTT_scope_write2((f1), (f2))
#else
static inline void RTT_init(void)
{
}
static inline void RTT_scope_write_raw(const void *data, uint32_t size)
{
    (void) data;
    (void) size;
}
static inline void RTT_scope_write2(float f1, float f2)
{
    (void) f1;
    (void) f2;
}
static inline void RTT_scope_write6(float f1, float f2, float f3, float f4, float f5, float f6)
{
    (void) f1;
    (void) f2;
    (void) f3;
    (void) f4;
    (void) f5;
    (void) f6;
}
#define DEBUG_PLOT(f1, f2) ((void) 0)
#endif

extern volatile uint32_t SystickCount;

// LED ACT: the GD32H759 control board schematic does not expose a dedicated
// MCU-driven status LED, so keep the old status LED API as a no-op.
#define CTM_H759_HAS_STATUS_LED 0
#define LED_ACT_SET()           ((void) 0)
#define LED_ACT_RESET()         ((void) 0)
#define LED_ACT_GET()           (0U)

// SPI0 encoder chip-select.
#define ENC_NCS_SET()        GPIO_BOP(CTM_H759_ENC_CS_PORT) = (uint32_t) CTM_H759_ENC_CS_PIN
#define ENC_NCS_RESET()      GPIO_BC(CTM_H759_ENC_CS_PORT) = (uint32_t) CTM_H759_ENC_CS_PIN

/* FLASH MAP ---------------------------------------------*/
#define FLASH_SECTOR_SIZE    ((uint32_t) 0x1000U) // 4KB sector on GD32H7 main flash
#define PAGE_SIZE            FLASH_SECTOR_SIZE

#define APP_MAIN_ADDR        ((uint32_t) (FLASH_BASE + 0x00000U))
#define APP_BACK_ADDR        ((uint32_t) (FLASH_BASE + 0x40000U))
#define APP_MAX_SIZE         ((uint32_t) (0x40000U)) // 256KB

#define BOOTLOADER_ADDR      ((uint32_t) (FLASH_BASE + 0x80000U))
#define BOOTLOADER_MAX_SIZE  ((uint32_t) (0x10000U)) // 64KB

#define USR_CONFIG_ADDR      ((uint32_t) (FLASH_BASE + 0x90000U))
#define USR_CONFIG_MAX_SIZE  ((uint32_t) (0x1000U))

#define COGGING_MAP_ADDR     ((uint32_t) (FLASH_BASE + 0x91000U))
#define COGGING_MAP_MAX_SIZE ((uint32_t) (0x4000U))

/* Exported functions prototypes ---------------------------------------------*/
static inline void watch_dog_feed(void)
{
    FWDGT_CTL = FWDGT_KEY_RELOAD;
}

static inline uint32_t get_ms_since(uint32_t tick)
{
    return (uint32_t) (SystickCount - tick);
}

void Error_Handler(void);
void delay_ms(const uint16_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
