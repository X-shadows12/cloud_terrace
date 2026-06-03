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

#ifndef __ENCODER_HW_H__
#define __ENCODER_HW_H__

#include "ctm_types.h"
#include "motor_hw.h"

void ENCODER_hw_init(void);
int32_t ENCODER_hw_read(void);
int32_t ENCODER_hw_read_axis(motor_hw_axis_t axis);
void ENCODER_hw_get_axis_pwm_status(motor_hw_axis_t axis,
                                    uint8_t *valid,
                                    uint32_t *period_cycles,
                                    uint32_t *high_cycles);

void ENCODER_pwm_capture_callback(motor_hw_axis_t axis, uint16_t capture, uint8_t signal_high);
void ENCODER_pwm_capture_rise_callback(motor_hw_axis_t axis, uint16_t capture);
void ENCODER_pwm_capture_fall_callback(motor_hw_axis_t axis, uint16_t capture);
void ENCODER_pwm_capture_overrun_callback(motor_hw_axis_t axis);

#endif
