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

#include "foc.h"
#include "board_port.h"
#include "motor_hw.h"
#include "usr_config.h"
#include "util.h"
#include <math.h>

tFOC Foc;
tFOC FocAxes[2];

static uint8_t foc_axis_index(motor_hw_axis_t axis);
static motor_hw_axis_t foc_active_axis(void);
static void foc_init_instance(tFOC *foc);
static void foc_update_current_ctrl_gain_instance(tFOC *foc, const tAxisConfig *config, float bw);
static void foc_sync_legacy_from_active(void);
static void foc_sync_legacy_if_active(motor_hw_axis_t axis);

void FOC_init(void)
{
    FOC_axis_init(MOTOR_HW_AXIS_LEFT);
    FOC_axis_init(MOTOR_HW_AXIS_RIGHT);
    foc_sync_legacy_from_active();
}

void FOC_update_current_ctrl_gain(float bw)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    FOC_update_axis_current_ctrl_gain(MOTOR_HW_AXIS_LEFT, bw);
    FOC_update_axis_current_ctrl_gain(MOTOR_HW_AXIS_RIGHT, bw);
#else
    FOC_update_axis_current_ctrl_gain(foc_active_axis(), bw);
#endif
    foc_sync_legacy_from_active();
}

void FOC_arm(void)
{
    FOC_axis_arm(foc_active_axis());
    foc_sync_legacy_from_active();
}

void FOC_disarm(void)
{
    FOC_axis_disarm(foc_active_axis());
    foc_sync_legacy_from_active();
}

void FOC_voltage(float Vd_set, float Vq_set, float phase)
{
    FOC_axis_voltage(foc_active_axis(), Vd_set, Vq_set, phase);
    foc_sync_legacy_from_active();
}

void FOC_current(float Id_set, float Iq_set, float phase, float phase_vel)
{
    FOC_axis_current(foc_active_axis(), Id_set, Iq_set, phase, phase_vel);
    foc_sync_legacy_from_active();
}

void FOC_reset_current_integrator(void)
{
    FOC_reset_axis_current_integrator(foc_active_axis());
    foc_sync_legacy_from_active();
}

tFOC *FOC_axis(motor_hw_axis_t axis)
{
    return &FocAxes[foc_axis_index(axis)];
}

void FOC_axis_init(motor_hw_axis_t axis)
{
    tFOC *foc = FOC_axis(axis);

    foc_init_instance(foc);
    foc_update_current_ctrl_gain_instance(foc, USR_CONFIG_axis(axis), USR_CONFIG_axis(axis)->current_ctrl_bw);
    foc_sync_legacy_if_active(axis);
}

void FOC_update_axis_current_ctrl_gain(motor_hw_axis_t axis, float bw)
{
    foc_update_current_ctrl_gain_instance(FOC_axis(axis), USR_CONFIG_axis(axis), bw);
    foc_sync_legacy_if_active(axis);
}

void FOC_axis_arm(motor_hw_axis_t axis)
{
    tFOC *foc = FOC_axis(axis);

    if (foc->is_armed) {
        foc_sync_legacy_if_active(axis);
        return;
    }

    MOTOR_HW_enter_critical();

    foc->i_q        = 0;
    foc->i_q_filt   = 0;
    foc->i_bus      = 0;
    foc->i_bus_filt = 0;
    foc->power_filt = 0;

    foc->current_ctrl_integral_d = 0;
    foc->current_ctrl_integral_q = 0;

    MOTOR_HW_turn_on_axis_low_sides(axis);

    foc->is_armed = true;

    MOTOR_HW_exit_critical();
    foc_sync_legacy_if_active(axis);
}

void FOC_axis_disarm(motor_hw_axis_t axis)
{
    tFOC *foc = FOC_axis(axis);

    if (!foc->is_armed) {
        foc_sync_legacy_if_active(axis);
        return;
    }

    MOTOR_HW_enter_critical();

    foc->i_q        = 0;
    foc->i_q_filt   = 0;
    foc->i_bus      = 0;
    foc->i_bus_filt = 0;
    foc->power_filt = 0;

    MOTOR_HW_switch_off_axis_pwm(axis);

    foc->is_armed = false;

    MOTOR_HW_exit_critical();
    foc_sync_legacy_if_active(axis);
}

void FOC_axis_voltage(motor_hw_axis_t axis, float Vd_set, float Vq_set, float phase)
{
    tFOC *foc = FOC_axis(axis);
    float v_to_mod = 1.5f / foc->v_bus_filt; // = 1.0f / (foc->v_bus_filt * 2.0f / 3.0f);
    float mod_vd   = Vd_set * v_to_mod;
    float mod_vq   = Vq_set * v_to_mod;

    // Vector modulation saturation, lock integrator if saturated
    float factor = 0.9f * SQRT3_BY_2 / sqrtf(SQ(mod_vd) + SQ(mod_vq));
    if (factor < 1.0f) {
        mod_vd *= factor;
        mod_vq *= factor;
    }

    // Inverse park transform
    float alpha, beta;
    float pwm_phase = phase;
    inverse_park(mod_vd, mod_vq, pwm_phase, &alpha, &beta);

    // SVM
    if (0 == svm(alpha, beta, &foc->dtc_a, &foc->dtc_b, &foc->dtc_c)) {
        MOTOR_HW_set_axis_phase_duty(axis, foc->dtc_a, foc->dtc_b, foc->dtc_c);
    }

    foc_sync_legacy_if_active(axis);
}

void FOC_axis_current(motor_hw_axis_t axis, float Id_set, float Iq_set, float phase, float phase_vel)
{
    tFOC *foc = FOC_axis(axis);

    // Clarke transform
    float i_alpha, i_beta;
    clarke_transform(foc->i_a, foc->i_b, foc->i_c, &i_alpha, &i_beta);

    // Park transform
    float i_d, i_q;
    park_transform(i_alpha, i_beta, phase, &i_d, &i_q);

    // Current PI control
    float i_d_err = Id_set - i_d;
    float i_q_err = Iq_set - i_q;
    float v_d     = i_d_err * foc->current_ctrl_p_gain + foc->current_ctrl_integral_d;
    float v_q     = i_q_err * foc->current_ctrl_p_gain + foc->current_ctrl_integral_q;

    // voltage normalize = 1/(2/3*v_bus)
    float v_to_mod = 1.5f / foc->v_bus_filt;
    float mod_vd   = v_d * v_to_mod;
    float mod_vq   = v_q * v_to_mod;

    // Vector modulation saturation, lock integrator if saturated
    float factor = 0.9f * SQRT3_BY_2 / sqrtf(SQ(mod_vd) + SQ(mod_vq));
    if (factor < 1.0f) {
        mod_vd *= factor;
        mod_vq *= factor;
        foc->current_ctrl_integral_d *= 0.99f;
        foc->current_ctrl_integral_q *= 0.99f;
    } else {
        foc->current_ctrl_integral_d += i_d_err * (foc->current_ctrl_i_gain * CURRENT_MEASURE_PERIOD);
        foc->current_ctrl_integral_q += i_q_err * (foc->current_ctrl_i_gain * CURRENT_MEASURE_PERIOD);
    }

    // Inverse park transform
    float alpha, beta;
    float pwm_phase = phase + phase_vel * CURRENT_MEASURE_PERIOD;
    inverse_park(mod_vd, mod_vq, pwm_phase, &alpha, &beta);

    // SVM
    if (0 == svm(alpha, beta, &foc->dtc_a, &foc->dtc_b, &foc->dtc_c)) {
        MOTOR_HW_set_axis_phase_duty(axis, foc->dtc_a, foc->dtc_b, foc->dtc_c);
    }

    // used for report
    foc->i_q = i_q;
    UTILS_LP_FAST(foc->i_q_filt, foc->i_q, 0.01f);
    foc->i_d = i_d;
    UTILS_LP_FAST(foc->i_d_filt, foc->i_d, 0.01f);
    foc->i_bus = (mod_vd * i_d + mod_vq * i_q);
    UTILS_LP_FAST(foc->i_bus_filt, foc->i_bus, 0.01f);
    foc->power_filt = foc->v_bus_filt * foc->i_bus_filt;
    foc_sync_legacy_if_active(axis);
}

void FOC_reset_axis_current_integrator(motor_hw_axis_t axis)
{
    tFOC *foc = FOC_axis(axis);

    foc->current_ctrl_integral_d = 0;
    foc->current_ctrl_integral_q = 0;
    foc_sync_legacy_if_active(axis);
}

static uint8_t foc_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static motor_hw_axis_t foc_active_axis(void)
{
#if defined(BOARD_USE_RIGHT_MOTOR)
    return MOTOR_HW_AXIS_RIGHT;
#else
    return MOTOR_HW_AXIS_LEFT;
#endif
}

static void foc_init_instance(tFOC *foc)
{
    foc->v_bus      = 0;
    foc->v_bus_filt = 0;
    foc->i_q        = 0;
    foc->i_q_filt   = 0;
    foc->i_bus      = 0;
    foc->i_bus_filt = 0;
    foc->power_filt = 0;

    foc->is_armed = false;
}

static void foc_update_current_ctrl_gain_instance(tFOC *foc, const tAxisConfig *config, float bw)
{
    float bandwidth             = bw * M_2PI;
    foc->current_ctrl_p_gain    = config->motor_phase_inductance * bandwidth;
    foc->current_ctrl_i_gain    = config->motor_phase_resistance * bandwidth;
}

static void foc_sync_legacy_from_active(void)
{
    Foc = *FOC_axis(foc_active_axis());
}

static void foc_sync_legacy_if_active(motor_hw_axis_t axis)
{
    if (axis == foc_active_axis()) {
        foc_sync_legacy_from_active();
    }
}
