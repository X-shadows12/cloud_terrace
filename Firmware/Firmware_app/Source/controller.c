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

#include "controller.h"
#include "anticogging.h"
#include "board_port.h"
#include "encoder.h"
#include "foc.h"
#include "mc_task.h"
#include "motor_hw.h"
#include "trapTraj.h"
#include "usr_config.h"
#include "util.h"

tController Controller;
tController ControllerAxes[2];
tTraj TrajAxes[2];

static uint8_t controller_axis_index(motor_hw_axis_t axis);
static bool controller_axis_home_ready(motor_hw_axis_t axis);
static void controller_zero_axis_home(motor_hw_axis_t axis, bool reset_controller);
static void controller_sync_active_from_legacy(void);
static void controller_sync_legacy_from_active(void);

int CONTROLLER_set_op_mode(tControlMode mode)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    int ret = 0;

    ret += CONTROLLER_axis_set_op_mode(MOTOR_HW_AXIS_LEFT, mode);
    ret += CONTROLLER_axis_set_op_mode(MOTOR_HW_AXIS_RIGHT, mode);
    Controller.ctrl_mode = mode;

    return (ret == 0) ? 0 : -1;
#else
    return CONTROLLER_axis_set_op_mode(CONTROLLER_active_axis(), mode);
#endif
}

int CONTROLLER_axis_set_op_mode(motor_hw_axis_t axis, tControlMode mode)
{
    if (mode > CONTROL_MODE_POSITION_PROFILE) {
        return -1;
    }

    CONTROLLER_axis(axis)->ctrl_mode = mode;
    Controller.ctrl_mode = mode;
    CONTROLLER_axis_reset(axis);

    return 0;
}

int CONTROLLER_set_home(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    tFSMState state = MCT_get_state();
    bool left_enabled = MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT);
    bool right_enabled = MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT);

    if ((state != IDLE) && (state != RUN) && (state != CALIBRATION)) {
        return -1;
    }

    if (MCT_axis_is_calibrating(MOTOR_HW_AXIS_LEFT)
        || MCT_axis_is_calibrating(MOTOR_HW_AXIS_RIGHT)) {
        return -1;
    }

    if ((left_enabled && !controller_axis_home_ready(MOTOR_HW_AXIS_LEFT))
        || (right_enabled && !controller_axis_home_ready(MOTOR_HW_AXIS_RIGHT))) {
        return -1;
    }

    MOTOR_HW_enter_critical();
    controller_zero_axis_home(MOTOR_HW_AXIS_LEFT, left_enabled);
    controller_zero_axis_home(MOTOR_HW_AXIS_RIGHT, right_enabled);
    MOTOR_HW_exit_critical();
    controller_sync_legacy_from_active();
    return 0;
#else
    return CONTROLLER_axis_set_home(CONTROLLER_active_axis());
#endif
}

int CONTROLLER_axis_set_home(motor_hw_axis_t axis)
{
    tFSMState state = MCT_get_state();
    bool enabled = MCT_axis_is_enabled(axis);

    if ((state != IDLE) && (state != RUN) && (state != CALIBRATION)) {
        return -1;
    }

    if (MCT_axis_is_calibrating(axis)) {
        return -1;
    }

    if (enabled && !controller_axis_home_ready(axis)) {
        return -1;
    }

    MOTOR_HW_enter_critical();
    controller_zero_axis_home(axis, enabled);
    MOTOR_HW_exit_critical();
    controller_sync_legacy_from_active();
    return 0;
}

void CONTROLLER_sync_callback(void)
{
    CONTROLLER_axis_sync_callback(CONTROLLER_active_axis());
}

void CONTROLLER_axis_sync_callback(motor_hw_axis_t axis)
{
    tController *controller = CONTROLLER_axis(axis);
    volatile tMCStatusword *statusword = MCT_axis_statusword(axis);

    if (!MCT_axis_is_enabled(axis)) {
        return;
    }

    switch (controller->ctrl_mode) {
    case CONTROL_MODE_CURRENT_RAMP:
        controller->input_current = controller->input_current_buffer;
        break;

    case CONTROL_MODE_VELOCITY_RAMP:
        controller->input_velocity = controller->input_velocity_buffer;
        break;

    case CONTROL_MODE_POSITION_FILTER:
        controller->input_position = controller->input_position_buffer;
        break;

    case CONTROL_MODE_POSITION_PROFILE:
        controller->input_position = controller->input_position_buffer;
        controller->input_updated  = true;
        break;

    default:
        break;
    }

    statusword->status.target_reached = 0;
    if (axis == CONTROLLER_active_axis()) {
        controller_sync_legacy_from_active();
    }
}

void CONTROLLER_init(void)
{
    CONTROLLER_axis_init(MOTOR_HW_AXIS_LEFT);
    CONTROLLER_axis_init(MOTOR_HW_AXIS_RIGHT);
    controller_sync_legacy_from_active();
}

void CONTROLLER_update_input_pos_filter_gain(float bw)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    CONTROLLER_update_axis_input_pos_filter_gain(MOTOR_HW_AXIS_LEFT, bw);
    CONTROLLER_update_axis_input_pos_filter_gain(MOTOR_HW_AXIS_RIGHT, bw);
#else
    CONTROLLER_update_axis_input_pos_filter_gain(CONTROLLER_active_axis(), bw);
#endif
    controller_sync_legacy_from_active();
}

void CONTROLLER_reset(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    CONTROLLER_axis_reset(MOTOR_HW_AXIS_LEFT);
    CONTROLLER_axis_reset(MOTOR_HW_AXIS_RIGHT);
#else
    CONTROLLER_axis_reset(CONTROLLER_active_axis());
#endif
    controller_sync_legacy_from_active();
}

void CONTROLLER_loop(void)
{
    controller_sync_active_from_legacy();
    CONTROLLER_axis_loop(CONTROLLER_active_axis());
    controller_sync_legacy_from_active();
}

tController *CONTROLLER_axis(motor_hw_axis_t axis)
{
    return &ControllerAxes[controller_axis_index(axis)];
}

tTraj *CONTROLLER_axis_traj(motor_hw_axis_t axis)
{
    return &TrajAxes[controller_axis_index(axis)];
}

void CONTROLLER_axis_init(motor_hw_axis_t axis)
{
    tController *controller = CONTROLLER_axis(axis);
    tTraj *traj = CONTROLLER_axis_traj(axis);
    tAxisConfig *config = USR_CONFIG_axis(axis);

    if ((config->default_op_mode < CONTROL_MODE_CURRENT_RAMP)
        || (config->default_op_mode > CONTROL_MODE_POSITION_PROFILE)) {
        config->default_op_mode = CONTROL_MODE_POSITION_PROFILE;
    }

    controller->ctrl_mode = (tControlMode) config->default_op_mode;
    controller->input_updated = false;
    traj->profile_done = true;
    CONTROLLER_update_axis_input_pos_filter_gain(axis, config->position_filter_bw);
}

void CONTROLLER_update_axis_input_pos_filter_gain(motor_hw_axis_t axis, float bw)
{
    tController *controller = CONTROLLER_axis(axis);
    float bandwidth = bw * M_2PI;

    controller->input_pos_filter_ki = 2.0f * bandwidth;
    controller->input_pos_filter_kp = 0.25f * SQ(controller->input_pos_filter_ki);
}

void CONTROLLER_axis_reset(motor_hw_axis_t axis)
{
    tController *controller = CONTROLLER_axis(axis);
    tEncoder *encoder = ENCODER_axis(axis);
    tTraj *traj = CONTROLLER_axis_traj(axis);

    MOTOR_HW_enter_critical();

    float pos_meas = encoder->pos;

    if (MCT_get_state() == ANTICOGGING) {
        pos_meas = (encoder->count_in_cpr / ENCODER_CPR_F);
    }

    controller->input_position = pos_meas;
    controller->input_velocity = 0.0f;
    controller->input_current  = 0.0f;

    controller->input_position_buffer = pos_meas;
    controller->input_velocity_buffer = 0.0f;
    controller->input_current_buffer  = 0.0f;

    controller->pos_setpoint = pos_meas;
    controller->vel_setpoint = 0.0f;
    controller->cur_setpoint = 0.0f;

    controller->vel_integrator = 0;

    controller->input_updated = false;
    traj->profile_done        = true;

    FOC_reset_axis_current_integrator(axis);

    MOTOR_HW_exit_critical();
}

void CONTROLLER_axis_loop(motor_hw_axis_t axis)
{
    tController *controller = CONTROLLER_axis(axis);
    tEncoder *encoder = ENCODER_axis(axis);
    tTraj *traj = CONTROLLER_axis_traj(axis);
    tAxisConfig *config = USR_CONFIG_axis(axis);
    volatile tMCStatusword *statusword = MCT_axis_statusword(axis);
    float       vel_des = 0.0f;
    bool        position_hold_deadband = false;
    const float pos_meas = encoder->pos;
    const float vel_meas = encoder->vel;
    const float phase_meas = encoder->phase;
    const float phase_vel_meas = encoder->phase_vel;

    if (MCT_axis_is_enabled(axis)) {
        switch (controller->ctrl_mode) {
        case CONTROL_MODE_CURRENT_RAMP: {
            float max_step_size = ABS(CURRENT_MEASURE_PERIOD * config->current_ramp_rate);
            float full_step = controller->input_current - controller->cur_setpoint;
            float step = CLAMP(full_step, -max_step_size, max_step_size);
            controller->cur_setpoint += step;
        } break;

        case CONTROL_MODE_VELOCITY_RAMP: {
            float max_step_size = ABS(CURRENT_MEASURE_PERIOD * config->velocity_ramp_rate);
            float full_step = controller->input_velocity - controller->vel_setpoint;
            float step = CLAMP(full_step, -max_step_size, max_step_size);
            controller->vel_setpoint += step;
            controller->cur_setpoint = config->current_ff_gain * (step / CURRENT_MEASURE_PERIOD);

            if (statusword->status.target_reached == 0) {
                if (ABS(controller->input_velocity - vel_meas) < config->target_velcity_window) {
                    statusword->status.target_reached = 1;
                }
            }
        } break;

        case CONTROL_MODE_POSITION_FILTER: {
            float delta_pos = controller->input_position - controller->pos_setpoint;
            float delta_vel = controller->input_velocity - controller->vel_setpoint;
            float accel = controller->input_pos_filter_kp * delta_pos
                        + controller->input_pos_filter_ki * delta_vel;
            controller->cur_setpoint = config->current_ff_gain * accel;
            controller->vel_setpoint += CURRENT_MEASURE_PERIOD * accel;
            controller->pos_setpoint += CURRENT_MEASURE_PERIOD * controller->vel_setpoint;
        } break;

        case CONTROL_MODE_POSITION_PROFILE: {
            if (controller->input_updated) {
                controller->input_updated = false;

                TRAJ_plan_axis(traj,
                               controller->input_position,
                               controller->pos_setpoint,
                               controller->vel_setpoint,
                               config->profile_velocity,
                               config->profile_accel,
                               config->profile_decel);
            }

            if (traj->profile_done) {
                if (statusword->status.target_reached == 0) {
                    if (ABS(controller->input_position - pos_meas) < config->target_position_window) {
                        statusword->status.target_reached = 1;
                    }
                }
                break;
            }

            TRAJ_eval_axis(traj);
            controller->pos_setpoint = traj->Y;
            controller->vel_setpoint = traj->Yd;
            controller->cur_setpoint = traj->Ydd * config->current_ff_gain;
        } break;

        default:
            break;
        }

        vel_des = controller->vel_setpoint;
        if (controller->ctrl_mode >= CONTROL_MODE_POSITION_FILTER) {
            float pos_err = controller->pos_setpoint - pos_meas;

            if ((controller->ctrl_mode == CONTROL_MODE_POSITION_PROFILE) && traj->profile_done
                && (ABS(pos_err) < config->target_position_window)) {
                pos_err                    = 0.0f;
                vel_des                    = 0.0f;
                controller->vel_setpoint   = 0.0f;
                controller->cur_setpoint   = 0.0f;
                controller->vel_integrator = 0.0f;
                position_hold_deadband     = true;
            }

            vel_des += config->pos_p_gain * pos_err;
        }
    } else {
        float pos_err = controller->input_position - (encoder->count_in_cpr / ENCODER_CPR_F);
        if (pos_err > +0.5f)
            pos_err -= 1.0f;
        if (pos_err < -0.5f)
            pos_err += 1.0f;
        vel_des = config->pos_p_gain * pos_err;
    }

    vel_des = CLAMP(vel_des, -config->velocity_limit, +config->velocity_limit);

    float iq_set = controller->cur_setpoint;
    float v_err = 0.0f;
    if (position_hold_deadband) {
        iq_set = 0.0f;
    } else if (controller->ctrl_mode >= CONTROL_MODE_VELOCITY_RAMP) {
        v_err = vel_des - vel_meas;
        iq_set += config->vel_p_gain * v_err;
        iq_set += controller->vel_integrator;
    }

    if (controller->ctrl_mode < CONTROL_MODE_VELOCITY_RAMP) {
        float Imax = (+config->velocity_limit - vel_meas) * config->vel_p_gain;
        float Imin = (-config->velocity_limit - vel_meas) * config->vel_p_gain;
        iq_set = CLAMP(iq_set, Imin, Imax);
    }

    if ((!position_hold_deadband)
        && config->anticogging_enable
        && AnticoggingValidAxes[controller_axis_index(axis)]) {
        int16_t index = nearbyintf(COGGING_MAP_NUM * encoder->count_in_cpr / ENCODER_CPR_F);
        if (index >= COGGING_MAP_NUM) {
            index = 0;
        }
        iq_set += USR_CONFIG_cogging_map(axis)->map[index] / 5000.0f;
    }

    bool limited = false;
    if (iq_set > +config->current_limit) {
        limited = true;
        iq_set  = +config->current_limit;
    }
    if (iq_set < -config->current_limit) {
        limited = true;
        iq_set  = -config->current_limit;
    }

    FOC_axis_current(axis, 0, iq_set, phase_meas, phase_vel_meas);

    if ((controller->ctrl_mode < CONTROL_MODE_VELOCITY_RAMP) || position_hold_deadband) {
        controller->vel_integrator = 0.0f;
    } else {
        if (limited) {
            controller->vel_integrator *= 0.99f;
        } else {
            controller->vel_integrator += (config->vel_i_gain * CURRENT_MEASURE_PERIOD) * v_err;
        }
    }
}

void CONTROLLER_broadcast_legacy_command(void)
{
    for (uint8_t i = 0U; i < 2U; i++) {
        tController *controller = &ControllerAxes[i];

        controller->ctrl_mode = Controller.ctrl_mode;
        controller->input_position = Controller.input_position;
        controller->input_velocity = Controller.input_velocity;
        controller->input_current = Controller.input_current;
        controller->input_position_buffer = Controller.input_position_buffer;
        controller->input_velocity_buffer = Controller.input_velocity_buffer;
        controller->input_current_buffer = Controller.input_current_buffer;
        controller->input_updated = Controller.input_updated;
    }
}

motor_hw_axis_t CONTROLLER_active_axis(void)
{
#if defined(BOARD_USE_RIGHT_MOTOR)
    return MOTOR_HW_AXIS_RIGHT;
#else
    return MOTOR_HW_AXIS_LEFT;
#endif
}

static uint8_t controller_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static bool controller_axis_home_ready(motor_hw_axis_t axis)
{
    return ((ABS(ENCODER_axis(axis)->vel) < 0.5f)
            && CONTROLLER_axis_traj(axis)->profile_done);
}

static void controller_zero_axis_home(motor_hw_axis_t axis, bool reset_controller)
{
    tController *controller = CONTROLLER_axis(axis);
    tEncoder *encoder = ENCODER_axis(axis);

    if (reset_controller) {
        controller->input_position = 0.0f;
        controller->input_position_buffer = 0.0f;
        controller->pos_setpoint = 0.0f;
    }

    encoder->shadow_count = 0;
    encoder->pos = 0.0f;
}

static void controller_sync_active_from_legacy(void)
{
    motor_hw_axis_t axis = CONTROLLER_active_axis();

    *CONTROLLER_axis(axis) = Controller;
    *CONTROLLER_axis_traj(axis) = Traj;
}

static void controller_sync_legacy_from_active(void)
{
    motor_hw_axis_t axis = CONTROLLER_active_axis();

    Controller = *CONTROLLER_axis(axis);
    Traj = *CONTROLLER_axis_traj(axis);
}
