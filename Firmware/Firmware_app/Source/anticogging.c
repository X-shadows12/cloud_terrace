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

#include "anticogging.h"
#include "can.h"
#include "controller.h"
#include "foc.h"
#include "mc_task.h"
#include "usr_config.h"
#include "util.h"

bool AnticoggingValid = false;

static int mNumber;
static int mLoopCount;
static motor_hw_axis_t mAnticoggingAxis = MOTOR_HW_AXIS_LEFT;

static uint8_t anticogging_axis_index(motor_hw_axis_t axis);
static void anticogging_refresh_legacy_valid(void);

void ANTICOGGING_start(void)
{
    ANTICOGGING_axis_start(mAnticoggingAxis);
}

void ANTICOGGING_select_axis(motor_hw_axis_t axis)
{
    mAnticoggingAxis = axis;
}

void ANTICOGGING_axis_start(motor_hw_axis_t axis)
{
    mAnticoggingAxis = axis;
    mNumber          = 0;
    mLoopCount       = 0;
    AnticoggingValidAxes[anticogging_axis_index(axis)] = false;
    anticogging_refresh_legacy_valid();
    USR_CONFIG_axis_set_default_cogging_map(axis);
}

void ANTICOGGING_end(void)
{
    FOC_axis_disarm(mAnticoggingAxis);

    if (!AnticoggingValidAxes[anticogging_axis_index(mAnticoggingAxis)]) {
        USR_CONFIG_axis_set_default_cogging_map(mAnticoggingAxis);
    }
    anticogging_refresh_legacy_valid();
}

void ANTICOGGING_loop(void)
{
    static const int gap_number = 100;
    tController *controller = CONTROLLER_axis(mAnticoggingAxis);
    tFOC *foc = FOC_axis(mAnticoggingAxis);
    tCoggingMap *map = USR_CONFIG_cogging_map(mAnticoggingAxis);

    // loop contrl 0.025s
    if (++mLoopCount < 500) {
        return;
    }
    mLoopCount = 0;

    mNumber++;

    if (mNumber <= gap_number) {
        // CW
        const float delta   = (1.0f / (float) COGGING_MAP_NUM);
        float       pos_ref = controller->input_position + delta;
        if (pos_ref > 1.0f) {
            pos_ref -= 1.0f;
        }
        controller->input_position = pos_ref;
    } else if (mNumber <= (gap_number + COGGING_MAP_NUM)) {
        int16_t tmp   = (int16_t) (foc->i_q_filt * 5000.0f);
        int16_t index = nearbyintf(COGGING_MAP_NUM * controller->input_position);
        if (index >= COGGING_MAP_NUM) {
            index = 0;
        }
        map->map[index] = tmp;

        CAN_anticogging_report(index, map->map[index]);

        // CW
        const float delta   = (1.0f / (float) COGGING_MAP_NUM);
        float       pos_ref = controller->input_position + delta;
        if (pos_ref > 1.0f) {
            pos_ref -= 1.0f;
        }
        controller->input_position = pos_ref;
    } else if (mNumber <= (gap_number + COGGING_MAP_NUM + gap_number)) {
        // CCW
        const float delta   = -(1.0f / (float) COGGING_MAP_NUM);
        float       pos_ref = controller->input_position + delta;
        if (pos_ref < 0.0f) {
            pos_ref += 1.0f;
        }
        controller->input_position = pos_ref;
    } else if (mNumber <= (gap_number + COGGING_MAP_NUM + gap_number + COGGING_MAP_NUM)) {
        int16_t tmp   = (int16_t) (foc->i_q_filt * 5000.0f);
        int16_t index = nearbyintf(COGGING_MAP_NUM * controller->input_position);
        if (index >= COGGING_MAP_NUM) {
            index = 0;
        }
        map->map[index] = (map->map[index] + tmp) / 2;

        CAN_anticogging_report(index, tmp);

        // CCW
        const float delta   = -(1.0f / (float) COGGING_MAP_NUM);
        float       pos_ref = controller->input_position + delta;
        if (pos_ref < 0.0f) {
            pos_ref += 1.0f;
        }
        controller->input_position = pos_ref;
    } else {
        // End
        CAN_anticogging_report(5000, 0);
        AnticoggingValidAxes[anticogging_axis_index(mAnticoggingAxis)] = true;
        anticogging_refresh_legacy_valid();
        MCT_set_state(IDLE);
    }
}

motor_hw_axis_t ANTICOGGING_active_axis(void)
{
    return mAnticoggingAxis;
}

static uint8_t anticogging_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static void anticogging_refresh_legacy_valid(void)
{
    AnticoggingValid = AnticoggingValidAxes[anticogging_axis_index(CONTROLLER_active_axis())];
}
