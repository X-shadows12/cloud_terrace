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

#include "calibration.h"
#include "anticogging.h"
#include "can.h"
#include "controller.h"
#include "encoder.h"
#include "foc.h"
#include "heap.h"
#include "mc_task.h"
#include "motor_hw.h"
#include "usr_config.h"
#include "util.h"

typedef enum eCalibStep {
    CS_NULL = 0,

    CS_MOTOR_R_START,
    CS_MOTOR_R_LOOP,
    CS_MOTOR_R_END,

    CS_MOTOR_L_START,
    CS_MOTOR_L_LOOP,
    CS_MOTOR_L_END,

    CS_DIR_PP_START,
    CS_DIR_PP_LOOP,
    CS_DIR_PP_END,

    CS_ENCODER_START,
    CS_ENCODER_CW_LOOP,
    CS_ENCODER_CCW_LOOP,
    CS_ENCODER_END,

    CS_REPORT_OFFSET_LUT,
} tCalibStep;

#define MAX_MOTOR_POLE_PAIRS 30U
#define SAMPLES_PER_PPAIR    128U

static int       *p_error_arr = NULL;
static tCalibStep mCalibStep  = CS_NULL;
static motor_hw_axis_t mCalibAxis = MOTOR_HW_AXIS_LEFT;

void CALIBRATION_start(void)
{
    CALIBRATION_axis_start(mCalibAxis);
}

void CALIBRATION_axis_start(motor_hw_axis_t axis)
{
    mCalibAxis = axis;
    tAxisConfig *config = USR_CONFIG_axis(mCalibAxis);

    uint8_t cogging_axis_index = (mCalibAxis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;

    // free
    if (pCoggingMapAxes[cogging_axis_index] != NULL) {
        AnticoggingValidAxes[cogging_axis_index] = false;
        HEAP_free(pCoggingMapAxes[cogging_axis_index]);
        pCoggingMapAxes[cogging_axis_index] = NULL;
        if (mCalibAxis == CONTROLLER_active_axis()) {
            pCoggingMap = NULL;
            AnticoggingValid = false;
        }
    }

    // malloc
    if (p_error_arr == NULL) {
        p_error_arr = HEAP_malloc(SAMPLES_PER_PPAIR * MAX_MOTOR_POLE_PAIRS * sizeof(int));
    }

    config->encoder_dir = +1;
    config->calib_valid = false;
    mCalibStep            = CS_MOTOR_R_START;
}

void CALIBRATION_select_axis(motor_hw_axis_t axis)
{
    mCalibAxis = axis;
}

void CALIBRATION_end(void)
{
    FOC_axis_disarm(mCalibAxis);

    mCalibStep = CS_NULL;

    // free
    if (p_error_arr != NULL) {
        HEAP_free(p_error_arr);
        p_error_arr = NULL;
    }

    // malloc
    uint8_t cogging_axis_index = (mCalibAxis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;

    if (0 == USR_CONFIG_axis_read_cogging_map(mCalibAxis)) {
        AnticoggingValidAxes[cogging_axis_index] = true;
    } else {
        USR_CONFIG_axis_set_default_cogging_map(mCalibAxis);
        AnticoggingValidAxes[cogging_axis_index] = false;
    }
    if (mCalibAxis == CONTROLLER_active_axis()) {
        AnticoggingValid = AnticoggingValidAxes[cogging_axis_index];
    }
}

motor_hw_axis_t CALIBRATION_active_axis(void)
{
    return mCalibAxis;
}

tCalibrationResult CALIBRATION_loop(void)
{
    static uint32_t loop_count;
    tCalibrationResult result = CALIBRATION_RESULT_RUNNING;

    // R
    static const float    kI           = 2.0f;
    static const uint32_t num_R_cycles = CURRENT_MEASURE_HZ * 2;

    // L
    static float          Ialphas[2];
    static float          voltages[2];
    static const uint32_t num_L_cycles = CURRENT_MEASURE_HZ / 2;

    static const float calib_phase_vel = M_PI;

    static float   phase_set;
    static int64_t start_count;

    static int16_t sample_count;
    static float   next_sample_time;

    tAxisConfig *config = USR_CONFIG_axis(mCalibAxis);
    tFOC *foc = FOC_axis(mCalibAxis);
    tEncoder *encoder = ENCODER_axis(mCalibAxis);
    float       time    = (float) loop_count * CURRENT_MEASURE_PERIOD;
    const float voltage = fabsf(config->calib_current * config->motor_phase_resistance * 3.0f / 2.0f);

    switch (mCalibStep) {
    case CS_NULL:
        break;

    case CS_MOTOR_R_START:
        loop_count  = 0;
        voltages[0] = 0.0f;
        mCalibStep  = CS_MOTOR_R_LOOP;
        break;

    case CS_MOTOR_R_LOOP:
        voltages[0] += kI * CURRENT_MEASURE_PERIOD * (config->calib_current - foc->i_a);

        // Test voltage along phase A
        FOC_axis_voltage(mCalibAxis, voltages[0], 0, 0);

        if (loop_count >= num_R_cycles) {
            MOTOR_HW_turn_on_axis_low_sides(mCalibAxis);
            mCalibStep = CS_MOTOR_R_END;
        }
        break;

    case CS_MOTOR_R_END:
        config->motor_phase_resistance = (voltages[0] / config->calib_current) * 2.0f / 3.0f;
        {
            uint8_t data[4];
            float_to_data(config->motor_phase_resistance, data);
            CAN_calib_report(1, data);
        }
        mCalibStep = CS_MOTOR_L_START;
        break;

    case CS_MOTOR_L_START:
        loop_count  = 0;
        Ialphas[0]  = 0.0f;
        Ialphas[1]  = 0.0f;
        voltages[0] = -config->calib_voltage;
        voltages[1] = +config->calib_voltage;
        FOC_axis_voltage(mCalibAxis, voltages[0], 0, 0);
        mCalibStep = CS_MOTOR_L_LOOP;
        break;

    case CS_MOTOR_L_LOOP: {
        int i = loop_count & 1;
        Ialphas[i] += foc->i_a;

        // Test voltage along phase A
        FOC_axis_voltage(mCalibAxis, voltages[i], 0, 0);

        if (loop_count >= (num_L_cycles << 1)) {
            MOTOR_HW_turn_on_axis_low_sides(mCalibAxis);
            mCalibStep = CS_MOTOR_L_END;
        }
    } break;

    case CS_MOTOR_L_END: {
        float dI_by_dt                   = (Ialphas[1] - Ialphas[0]) / (float) (CURRENT_MEASURE_PERIOD * num_L_cycles);
        if (!(dI_by_dt > 1.0f)) {
            MCT_axis_statusword(mCalibAxis)->errors.selftest = 1;
            result = CALIBRATION_RESULT_FAILED;
            break;
        }
        float L                          = config->calib_voltage / dI_by_dt;
        config->motor_phase_inductance = L * 2.0f / 3.0f;
        FOC_update_axis_current_ctrl_gain(mCalibAxis, config->current_ctrl_bw);

        uint8_t data[4];
        float_to_data(config->motor_phase_inductance, data);
        CAN_calib_report(2, data);

        phase_set  = 0;
        loop_count = 0;
        mCalibStep = CS_DIR_PP_START;
    } break;

    case CS_DIR_PP_START:
        FOC_axis_voltage(mCalibAxis, (voltage * time / 2.0f), 0, phase_set);
        if (time >= 2.0f) {
            start_count = encoder->shadow_count;
            mCalibStep  = CS_DIR_PP_LOOP;
            break;
        }
        break;

    case CS_DIR_PP_LOOP:
        phase_set += calib_phase_vel * CURRENT_MEASURE_PERIOD;
        FOC_axis_voltage(mCalibAxis, voltage, 0, phase_set);
        if (phase_set >= 8.0f * M_2PI) {
            mCalibStep = CS_DIR_PP_END;
        }
        break;

    case CS_DIR_PP_END: {
        int64_t diff = encoder->shadow_count - start_count;
        float   elec_revs;
        float   mech_revs;
        int64_t abs_diff;

        abs_diff  = (diff >= 0) ? diff : -diff;
        mech_revs = (float) abs_diff / ENCODER_CPR_F;
        elec_revs = fabsf(phase_set) / M_2PI;

        if (mech_revs < 0.001f) {
            MCT_axis_statusword(mCalibAxis)->errors.selftest = 1;
            result = CALIBRATION_RESULT_FAILED;
            break;
        }

        // Check direction
        if (diff > 0) {
            config->encoder_dir = +1;
        } else {
            config->encoder_dir = -1;
        }

        // Motor pole pairs are estimated from motion magnitude only;
        // direction is handled separately above.
        config->motor_pole_pairs = (int32_t) roundf(elec_revs / mech_revs);
        if (config->motor_pole_pairs < 2 || config->motor_pole_pairs > (int32_t) MAX_MOTOR_POLE_PAIRS) {
            MCT_axis_statusword(mCalibAxis)->errors.selftest = 1;
            result = CALIBRATION_RESULT_FAILED;
            break;
        }

        {
            uint8_t data[4];

            float_to_data((float) config->motor_pole_pairs, data);
            CAN_calib_report(3, data);

            float_to_data((float) config->encoder_dir, data);
            CAN_calib_report(4, data);
        }

        mCalibStep = CS_ENCODER_START;
    } break;

    case CS_ENCODER_START:
        phase_set        = 0;
        loop_count       = 0;
        sample_count     = 0;
        next_sample_time = 0;
        mCalibStep       = CS_ENCODER_CW_LOOP;
        break;

    case CS_ENCODER_CW_LOOP:
        if (sample_count < (config->motor_pole_pairs * SAMPLES_PER_PPAIR)) {
            if (time > next_sample_time) {
                next_sample_time += M_2PI / ((float) SAMPLES_PER_PPAIR * calib_phase_vel);

                int count_ref = (phase_set * ENCODER_CPR_F) / (M_2PI * (float) config->motor_pole_pairs);
                int error     = encoder->raw - count_ref;
                error += ENCODER_CPR * (error < 0);
                p_error_arr[sample_count] = error;

                sample_count++;
            }

            phase_set += calib_phase_vel * CURRENT_MEASURE_PERIOD;
        } else {
            phase_set -= calib_phase_vel * CURRENT_MEASURE_PERIOD;
            loop_count = 0;
            sample_count--;
            next_sample_time = 0;
            mCalibStep       = CS_ENCODER_CCW_LOOP;
            break;
        }
        FOC_axis_voltage(mCalibAxis, voltage, 0, phase_set);
        break;

    case CS_ENCODER_CCW_LOOP:
        if (sample_count >= 0) {
            if (time > next_sample_time) {
                next_sample_time += M_2PI / ((float) SAMPLES_PER_PPAIR * calib_phase_vel);

                int count_ref = (phase_set * ENCODER_CPR_F) / (M_2PI * (float) config->motor_pole_pairs);
                int error     = encoder->raw - count_ref;
                error += ENCODER_CPR * (error < 0);
                p_error_arr[sample_count] = (p_error_arr[sample_count] + error) / 2;

                sample_count--;
            }

            phase_set -= calib_phase_vel * CURRENT_MEASURE_PERIOD;
        } else {
            MOTOR_HW_turn_on_axis_low_sides(mCalibAxis);
            mCalibStep = CS_ENCODER_END;
            break;
        }
        FOC_axis_voltage(mCalibAxis, voltage, 0, phase_set);
        break;

    case CS_ENCODER_END: {
        // Calculate average offset
        int64_t moving_avg = 0;
        for (int i = 0; i < (config->motor_pole_pairs * SAMPLES_PER_PPAIR); i++) {
            moving_avg += p_error_arr[i];
        }
        config->encoder_offset = moving_avg / (config->motor_pole_pairs * SAMPLES_PER_PPAIR);

        {
            uint8_t data[4];
            float_to_data((float) config->encoder_offset, data);
            CAN_calib_report(5, data);
        }

        // FIR and map measurements to lut
        int window     = SAMPLES_PER_PPAIR;
        int lut_offset = p_error_arr[0] * OFFSET_LUT_NUM / ENCODER_CPR;
        for (int i = 0; i < OFFSET_LUT_NUM; i++) {
            moving_avg = 0;
            for (int j = (-window) / 2; j < (window) / 2; j++) {
                int index = i * config->motor_pole_pairs * SAMPLES_PER_PPAIR / OFFSET_LUT_NUM + j;
                if (index < 0) {
                    index += (SAMPLES_PER_PPAIR * config->motor_pole_pairs);
                } else if (index > (SAMPLES_PER_PPAIR * config->motor_pole_pairs - 1)) {
                    index -= (SAMPLES_PER_PPAIR * config->motor_pole_pairs);
                }
                moving_avg += p_error_arr[index];
            }
            moving_avg    = moving_avg / window;
            int lut_index = lut_offset + i;
            if (lut_index > (OFFSET_LUT_NUM - 1)) {
                lut_index -= OFFSET_LUT_NUM;
            }
            config->offset_lut[lut_index] = moving_avg - config->encoder_offset;
        }

        loop_count       = 0;
        sample_count     = 0;
        next_sample_time = 0;
        mCalibStep       = CS_REPORT_OFFSET_LUT;
    } break;

    case CS_REPORT_OFFSET_LUT:
        if (sample_count < OFFSET_LUT_NUM) {
            if (time > next_sample_time) {
                next_sample_time += 0.001f;
                {
                    uint8_t data[4];
                    float_to_data((float) config->offset_lut[sample_count], data);
                    CAN_calib_report(10 + sample_count, data);
                }
                sample_count++;
            }
        } else {
            mCalibStep            = CS_NULL;
            config->calib_valid = true;
            result = CALIBRATION_RESULT_DONE;
        }
        break;

    default:
        break;
    }

    loop_count++;

    return result;
}
