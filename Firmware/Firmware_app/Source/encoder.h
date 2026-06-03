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

#ifndef __ENCODER_H__
#define __ENCODER_H__

#include "ctm_types.h"
#include "motor_hw.h"

#define ENCODER_CPR     (int) 32768
#define ENCODER_CPR_F   (32768.0f)
#define ENCODER_CPR_DIV (ENCODER_CPR >> 1)

typedef struct sEncoder
{
    uint8_t need_init;

    int raw;
    int count_in_cpr;
    int count_in_cpr_prev;

    int64_t shadow_count;

    float pos;
    float vel;
    float phase;
    float phase_vel;

    // pll use
    float pll_pos;
    float pll_vel;

    float pll_kp;
    float pll_ki;
    float interpolation;
    float snap_threshold;

    uint8_t pwm_valid;
    uint32_t pwm_period_cycles;
    uint32_t pwm_high_cycles;
} tEncoder;

extern tEncoder Encoder;
extern tEncoder EncoderAxes[2];

void ENCODER_init(void);
bool ENCODER_sample(void);
void ENCODER_loop(void);
tEncoder *ENCODER_axis(motor_hw_axis_t axis);
void ENCODER_axis_init(motor_hw_axis_t axis);
bool ENCODER_axis_sample(motor_hw_axis_t axis);
void ENCODER_axis_loop(motor_hw_axis_t axis);
motor_hw_axis_t ENCODER_active_axis(void);

#endif
