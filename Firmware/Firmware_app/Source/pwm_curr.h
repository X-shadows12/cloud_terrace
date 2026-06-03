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

#ifndef __PWM_CURR_H__
#define __PWM_CURR_H__

#include "motor_hw.h"

void PWMC_init(void);
void PWMC_SwitchOnPWM(void);
void PWMC_SwitchOffPWM(void);
void PWMC_TurnOnLowSides(void);
int  PWMC_CurrentReadingPolarization(void);
void PWMC_SwitchOnAxisPWM(motor_hw_axis_t axis);
void PWMC_SwitchOffAxisPWM(motor_hw_axis_t axis);
void PWMC_TurnOnAxisLowSides(motor_hw_axis_t axis);
int  PWMC_CurrentReadingAxisPolarization(motor_hw_axis_t axis);

#endif
