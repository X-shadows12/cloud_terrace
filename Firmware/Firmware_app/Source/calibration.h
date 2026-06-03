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

#ifndef __CALIBRATION_H__
#define __CALIBRATION_H__

#include "ctm_types.h"
#include "motor_hw.h"

void CALIBRATION_start(void);
void CALIBRATION_axis_start(motor_hw_axis_t axis);
void CALIBRATION_select_axis(motor_hw_axis_t axis);
void CALIBRATION_end(void);
motor_hw_axis_t CALIBRATION_active_axis(void);

typedef enum eCalibrationResult {
    CALIBRATION_RESULT_RUNNING = 0,
    CALIBRATION_RESULT_DONE,
    CALIBRATION_RESULT_FAILED,
} tCalibrationResult;

tCalibrationResult CALIBRATION_loop(void);

#endif
