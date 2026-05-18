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

#ifndef __MOTOR_HW_H__
#define __MOTOR_HW_H__

#include <stdint.h>

#define MOTOR_PWM_FREQUENCY_HZ       20000U
#define MOTOR_CURRENT_MEASURE_HZ     MOTOR_PWM_FREQUENCY_HZ
#define MOTOR_CURRENT_MEASURE_PERIOD (1.0f / (float) MOTOR_CURRENT_MEASURE_HZ)

/* Backward-compatible timing names used by the existing control code. */
#define PWM_FREQUENCY                MOTOR_PWM_FREQUENCY_HZ
#define CURRENT_MEASURE_HZ           MOTOR_CURRENT_MEASURE_HZ
#define CURRENT_MEASURE_PERIOD       MOTOR_CURRENT_MEASURE_PERIOD

void  MOTOR_HW_enter_critical(void);
void  MOTOR_HW_exit_critical(void);
void  MOTOR_HW_set_phase_duty(float duty_a, float duty_b, float duty_c);
void  MOTOR_HW_turn_on_low_sides(void);
void  MOTOR_HW_switch_off_pwm(void);
float MOTOR_HW_read_vbus_voltage(void);
float MOTOR_HW_read_phase_a_current(void);
float MOTOR_HW_read_phase_b_current(void);
int   MOTOR_HW_read_driver_temp(void);
int   MOTOR_HW_read_ntc_temp(void);

#endif
