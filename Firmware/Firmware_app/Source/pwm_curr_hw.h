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

#ifndef __PWM_CURR_HW_H__
#define __PWM_CURR_HW_H__

#include "board_port.h"
#include "motor_hw.h"
#include <math.h>

#define PWM_TIMER_CLK_MHz      BOARD_PWM_TIMER_CLK_MHZ
#define PWM_PERIOD_CYCLES      (uint16_t) ((PWM_TIMER_CLK_MHz * (uint32_t) 1000000u / ((uint32_t) (PWM_FREQUENCY))) & 0xFFFE)
#define HALF_PWM_PERIOD_CYCLES (uint16_t) (PWM_PERIOD_CYCLES / 2U)

#define SHUNT_RESISTENCE       (0.05f)
#define CURRENT_AMP_GAIN       (20.0f)
#define VBUS_DIVIDER_GAIN      (11.0f)
#define V_SCALE                ((float) (VBUS_DIVIDER_GAIN * 3.3f / 4095.0f))
#define I_SCALE                ((float) ((3.3f / 4095.0f) / SHUNT_RESISTENCE / CURRENT_AMP_GAIN))

#define READ_IPHASE_A_ADC()    ((uint16_t) (ADC_IDATA0(BOARD_PHASE_ADC)))
#define READ_IPHASE_B_ADC()    ((uint16_t) (ADC_IDATA1(BOARD_PHASE_ADC)))

extern uint16_t adc_buff[3];
extern int16_t  phase_a_adc_offset;
extern int16_t  phase_b_adc_offset;

static inline float read_vbus(void)
{
    return MOTOR_HW_read_vbus_voltage();
}

static inline int read_drv_temp(void)
{
    return MOTOR_HW_read_driver_temp();
}

static inline int read_ntc_temp(void)
{
    return MOTOR_HW_read_ntc_temp();
}

static inline float read_iphase_a(void)
{
    return MOTOR_HW_read_phase_a_current();
}

static inline float read_iphase_b(void)
{
    return MOTOR_HW_read_phase_b_current();
}

static inline void set_a_duty(uint32_t duty)
{
    BOARD_PWM_PHASE_A_CV = duty;
}

static inline void set_b_duty(uint32_t duty)
{
    BOARD_PWM_PHASE_B_CV = duty;
}

static inline void set_c_duty(uint32_t duty)
{
    BOARD_PWM_PHASE_C_CV = duty;
}

#endif
