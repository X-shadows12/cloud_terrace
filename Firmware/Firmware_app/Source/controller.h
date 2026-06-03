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

#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include "ctm_types.h"
#include "motor_hw.h"
#include "trapTraj.h"

typedef enum {
    CONTROL_MODE_CURRENT_RAMP     = 0,
    CONTROL_MODE_VELOCITY_RAMP    = 1,
    CONTROL_MODE_POSITION_FILTER  = 2,
    CONTROL_MODE_POSITION_PROFILE = 3,
} tControlMode;

typedef struct sController
{
    int   ctrl_mode;
    float input_position;
    float input_velocity;
    float input_current;

    float input_position_buffer;
    float input_velocity_buffer;
    float input_current_buffer;

    float pos_setpoint;
    float vel_setpoint;
    float cur_setpoint;

    bool  input_updated;
    float input_pos_filter_kp;
    float input_pos_filter_ki;
    float vel_integrator;
} tController;

extern tController Controller;
extern tController ControllerAxes[2];
extern tTraj TrajAxes[2];

int  CONTROLLER_set_op_mode(tControlMode mode);
int  CONTROLLER_set_home(void);
void CONTROLLER_sync_callback(void);
int  CONTROLLER_axis_set_op_mode(motor_hw_axis_t axis, tControlMode mode);
int  CONTROLLER_axis_set_home(motor_hw_axis_t axis);
void CONTROLLER_axis_sync_callback(motor_hw_axis_t axis);

void CONTROLLER_init(void);
void CONTROLLER_update_input_pos_filter_gain(float bw);
void CONTROLLER_reset(void);
void CONTROLLER_loop(void);
tController *CONTROLLER_axis(motor_hw_axis_t axis);
tTraj *CONTROLLER_axis_traj(motor_hw_axis_t axis);
void CONTROLLER_axis_init(motor_hw_axis_t axis);
void CONTROLLER_update_axis_input_pos_filter_gain(motor_hw_axis_t axis, float bw);
void CONTROLLER_axis_reset(motor_hw_axis_t axis);
void CONTROLLER_axis_loop(motor_hw_axis_t axis);
void CONTROLLER_broadcast_legacy_command(void);
motor_hw_axis_t CONTROLLER_active_axis(void);

#endif
