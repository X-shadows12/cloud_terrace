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

#include "encoder.h"
#include "board_port.h"
#include "encoder_hw.h"
#include "motor_hw.h"
#include "usr_config.h"
#include "util.h"

tEncoder Encoder;
tEncoder EncoderAxes[2];

static uint8_t encoder_axis_index(motor_hw_axis_t axis);
static void encoder_init_instance(tEncoder *encoder);
static bool encoder_sample_instance(motor_hw_axis_t axis, tEncoder *encoder);
static void encoder_loop_instance(motor_hw_axis_t axis, tEncoder *encoder);
static void encoder_sync_legacy_from_active(void);
static void encoder_sync_legacy_if_active(motor_hw_axis_t axis);

void ENCODER_init(void)
{
    ENCODER_axis_init(MOTOR_HW_AXIS_LEFT);
    ENCODER_axis_init(MOTOR_HW_AXIS_RIGHT);
    encoder_sync_legacy_from_active();
}

bool ENCODER_sample(void)
{
    bool valid = ENCODER_axis_sample(ENCODER_active_axis());

    encoder_sync_legacy_from_active();
    return valid;
}

void ENCODER_loop(void)
{
    ENCODER_axis_loop(ENCODER_active_axis());
    encoder_sync_legacy_from_active();
}

tEncoder *ENCODER_axis(motor_hw_axis_t axis)
{
    return &EncoderAxes[encoder_axis_index(axis)];
}

void ENCODER_axis_init(motor_hw_axis_t axis)
{
    encoder_init_instance(ENCODER_axis(axis));
    encoder_sync_legacy_if_active(axis);
}

bool ENCODER_axis_sample(motor_hw_axis_t axis)
{
    bool valid = encoder_sample_instance(axis, ENCODER_axis(axis));

    encoder_sync_legacy_if_active(axis);
    return valid;
}

void ENCODER_axis_loop(motor_hw_axis_t axis)
{
    encoder_loop_instance(axis, ENCODER_axis(axis));
    encoder_sync_legacy_if_active(axis);
}

motor_hw_axis_t ENCODER_active_axis(void)
{
#if defined(BOARD_USE_RIGHT_MOTOR)
    return MOTOR_HW_AXIS_RIGHT;
#else
    return MOTOR_HW_AXIS_LEFT;
#endif
}

static uint8_t encoder_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static void encoder_init_instance(tEncoder *encoder)
{
    encoder->need_init    = 20;
    encoder->shadow_count = 0;
    encoder->pll_pos      = 0;
    encoder->pll_vel      = 0;

    int encoder_pll_bw     = 100 * M_2PI;
    encoder->pll_kp         = 2.0f * encoder_pll_bw;       // basic conversion to discrete time
    encoder->pll_ki         = 0.25f * SQ(encoder->pll_kp);  // Critically damped
    encoder->snap_threshold = 0.5f * CURRENT_MEASURE_PERIOD * encoder->pll_ki;
    encoder->pwm_valid      = 0;
    encoder->pwm_period_cycles = 0;
    encoder->pwm_high_cycles   = 0;
}

static bool encoder_sample_instance(motor_hw_axis_t axis, tEncoder *encoder)
{
    encoder->raw = ENCODER_hw_read_axis(axis);
    ENCODER_hw_get_axis_pwm_status(axis,
                                   &encoder->pwm_valid,
                                   &encoder->pwm_period_cycles,
                                   &encoder->pwm_high_cycles);
    return (0U != encoder->pwm_valid);
}

static void encoder_loop_instance(motor_hw_axis_t axis, tEncoder *encoder)
{
    tAxisConfig *config = USR_CONFIG_axis(axis);

    encoder->raw = ENCODER_hw_read_axis(axis);
    ENCODER_hw_get_axis_pwm_status(axis,
                                   &encoder->pwm_valid,
                                   &encoder->pwm_period_cycles,
                                   &encoder->pwm_high_cycles);

    if (config->encoder_dir == -1) {
        encoder->raw = ENCODER_CPR - 1 - encoder->raw;
    }

    /* Linearization */
    uint32_t lut_pos  = ((uint32_t) encoder->raw * OFFSET_LUT_NUM);
    uint32_t lut_idx  = lut_pos / ENCODER_CPR;
    uint32_t lut_frac = lut_pos - (lut_idx * ENCODER_CPR);
    int off_1         = config->offset_lut[lut_idx];
    int off_2         = config->offset_lut[(lut_idx + 1U) % OFFSET_LUT_NUM];
    int off_interp    = off_1 + (int) (((int64_t) (off_2 - off_1) * (int64_t) lut_frac) / ENCODER_CPR);

    int count = encoder->raw - off_interp - config->encoder_offset;

    /*  Wrap in ENCODER_CPR */
    while (count >= ENCODER_CPR)
        count -= ENCODER_CPR;
    while (count < 0)
        count += ENCODER_CPR;

    encoder->count_in_cpr = count;

    if (encoder->need_init) {
        encoder->need_init--;
        encoder->count_in_cpr_prev = encoder->count_in_cpr;
        return;
    }

    /* Delta count */
    int delta_count            = encoder->count_in_cpr - encoder->count_in_cpr_prev;
    encoder->count_in_cpr_prev = encoder->count_in_cpr;
    while (delta_count > +ENCODER_CPR_DIV)
        delta_count -= ENCODER_CPR;
    while (delta_count < -ENCODER_CPR_DIV)
        delta_count += ENCODER_CPR;

    // Run pll (for now pll is in units of encoder counts)
    // Predict current pos
    encoder->pll_pos += CURRENT_MEASURE_PERIOD * encoder->pll_vel;
    // Discrete phase detector
    float delta_pos = encoder->count_in_cpr - floorf(encoder->pll_pos);
    while (delta_pos > +ENCODER_CPR_DIV)
        delta_pos -= ENCODER_CPR_F;
    while (delta_pos < -ENCODER_CPR_DIV)
        delta_pos += ENCODER_CPR_F;
    // PLL feedback
    encoder->pll_pos += CURRENT_MEASURE_PERIOD * encoder->pll_kp * delta_pos;
    while (encoder->pll_pos > ENCODER_CPR)
        encoder->pll_pos -= ENCODER_CPR_F;
    while (encoder->pll_pos < 0)
        encoder->pll_pos += ENCODER_CPR_F;
    encoder->pll_vel += CURRENT_MEASURE_PERIOD * encoder->pll_ki * delta_pos;

    // Align delta-sigma on zero to prevent jitter
    if (ABS(encoder->pll_vel) < encoder->snap_threshold) {
        encoder->pll_vel = 0.0f;
    }

    /* Outputs from Encoder for Controller */
    encoder->shadow_count += delta_count;
    encoder->pos       = encoder->shadow_count / ENCODER_CPR_F;
    encoder->vel       = encoder->pll_vel / ENCODER_CPR_F;
    encoder->phase     = (M_2PI * config->motor_pole_pairs) * encoder->count_in_cpr / ENCODER_CPR_F;
    encoder->phase_vel = (M_2PI * config->motor_pole_pairs) * encoder->vel;
}

static void encoder_sync_legacy_from_active(void)
{
    Encoder = *ENCODER_axis(ENCODER_active_axis());
}

static void encoder_sync_legacy_if_active(motor_hw_axis_t axis)
{
    if (axis == ENCODER_active_axis()) {
        encoder_sync_legacy_from_active();
    }
}
