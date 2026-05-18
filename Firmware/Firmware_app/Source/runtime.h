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

#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include "ctm_types.h"
#include "gd32h7xx.h"

extern volatile uint32_t SystickCount;

static inline void watch_dog_feed(void)
{
    FWDGT_CTL = FWDGT_KEY_RELOAD;
}

static inline uint32_t get_ms_since(uint32_t tick)
{
    return (uint32_t) (SystickCount - tick);
}

void delay_ms(const uint16_t ms);

#endif
