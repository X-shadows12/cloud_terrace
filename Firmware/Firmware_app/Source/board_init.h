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

#ifndef __BOARD_INIT_H__
#define __BOARD_INIT_H__

void BOARD_interrupts_disable(void);
void BOARD_interrupts_enable(void);

void BOARD_init_debug_memory(void);
void BOARD_enable_cache(void);
void BOARD_init_peripherals(void);
void BOARD_init_watchdog(void);
void BOARD_enable_watchdog(void);
void BOARD_emergency_shutdown(void);

#endif /* __BOARD_INIT_H__ */
