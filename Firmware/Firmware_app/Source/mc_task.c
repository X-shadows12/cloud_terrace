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

#include "mc_task.h"
#include "anticogging.h"
#include "board_port.h"
#include "calibration.h"
#include "can.h"
#include "controller.h"
#include "encoder.h"
#include "foc.h"
#include "motor_hw.h"
#include "status_led.h"
#include "rtt_scope.h"
#include "runtime.h"
#include "usr_config.h"
#include "util.h"
#include <math.h>
#include <string.h>

typedef struct sFSM
{
    tFSMState state;
    tFSMState state_next;
    uint8_t   state_next_ready;
} tFSM;

static volatile tFSM mFSM;

volatile tMCStatusword StatuswordNew;
volatile tMCStatusword StatuswordOld;
volatile tMCStatusword StatuswordNewAxes[MOTOR_HW_AXIS_COUNT];
volatile tMCStatusword StatuswordOldAxes[MOTOR_HW_AXIS_COUNT];

#define CHARGE_BOOT_CAP_MS    10
#define CHARGE_BOOT_CAP_TICKS (uint16_t) ((MOTOR_PWM_FREQUENCY_HZ * CHARGE_BOOT_CAP_MS) / 1000U)
static uint16_t mChargeBootCapDelay = 0;
static uint8_t  mRunAxisMask = 0U;
static uint8_t  mRunAxisPendingMask = 0U;
static uint8_t  mCalibrationAxisMask = 0U;
static uint16_t mAxisChargeBootCapDelay[MOTOR_HW_AXIS_COUNT];
static uint16_t mCalibrationChargeBootCapDelay = 0U;

static void enter_state(void);
static void exit_state(void);
static void led_act_loop(void);
static void mct_arm_run_outputs(void);
static void mct_disarm_outputs(void);
static int  mct_start_axis_calibration(motor_hw_axis_t axis);
static void mct_stop_axis_calibration(motor_hw_axis_t axis);
static void mct_finish_axis_calibration(motor_hw_axis_t axis);
static int  mct_enable_axis_for_run(motor_hw_axis_t axis);
static void mct_disable_axis_for_run(motor_hw_axis_t axis);
static void mct_update_axis_enable_delay(motor_hw_axis_t axis);
static void mct_update_run_axes_control(void);
static void mct_check_run_axes_over_current(void);
static void mct_process_axis_calibration(void);
static uint8_t mct_axis_mask(motor_hw_axis_t axis);
static bool mct_axis_over_current(motor_hw_axis_t axis);
static bool mct_any_axis_over_current(void);
static void mct_update_axis_feedback(motor_hw_axis_t axis);
static void mct_run_axis_control(motor_hw_axis_t axis);
static uint8_t mct_axis_index(motor_hw_axis_t axis);
static bool mct_axis_has_error(motor_hw_axis_t axis);
static bool mct_any_axis_has_error(void);
static bool mct_axis_calibrated(motor_hw_axis_t axis);
static bool mct_all_axes_calibrated(void);
static void mct_set_axis_status(motor_hw_axis_t axis, uint8_t switched_on, uint8_t target_reached);
static void mct_sync_legacy_status_from_active(void);

void MCT_init(void)
{
    mFSM.state            = BOOT_UP;
    mFSM.state_next       = BOOT_UP;
    mFSM.state_next_ready = 0;
    mRunAxisMask          = 0U;
    mRunAxisPendingMask   = 0U;
    mCalibrationAxisMask  = 0U;
    mCalibrationChargeBootCapDelay = 0U;
    mAxisChargeBootCapDelay[0] = 0U;
    mAxisChargeBootCapDelay[1] = 0U;

    StatuswordNew.status.status_code = 0;
    StatuswordNew.errors.errors_code = 0;
    StatuswordOld                    = StatuswordNew;
    StatuswordNewAxes[0]             = StatuswordNew;
    StatuswordNewAxes[1]             = StatuswordNew;
    StatuswordOldAxes[0]             = StatuswordNew;
    StatuswordOldAxes[1]             = StatuswordNew;
}

int MCT_reset_error(void)
{
    StatuswordNewAxes[0].errors.errors_code &= 0x80;
    StatuswordNewAxes[1].errors.errors_code &= 0x80;
    StatuswordOldAxes[0].errors.errors_code &= 0x80;
    StatuswordOldAxes[1].errors.errors_code &= 0x80;
    mct_sync_legacy_status_from_active();
    return 0;
}

int MCT_axis_reset_error(motor_hw_axis_t axis)
{
    uint8_t idx = mct_axis_index(axis);

    StatuswordNewAxes[idx].errors.errors_code &= 0x80;
    StatuswordOldAxes[idx].errors.errors_code &= 0x80;
    mct_sync_legacy_status_from_active();

    return 0;
}

volatile tMCStatusword *MCT_axis_statusword(motor_hw_axis_t axis)
{
    return &StatuswordNewAxes[mct_axis_index(axis)];
}

tFSMState MCT_get_state(void)
{
    return mFSM.state;
}

bool MCT_axis_is_enabled(motor_hw_axis_t axis)
{
    return ((mRunAxisMask & mct_axis_mask(axis)) != 0U);
}

bool MCT_axis_is_calibrating(motor_hw_axis_t axis)
{
    return ((mCalibrationAxisMask & mct_axis_mask(axis)) != 0U);
}

// return
//    0 Success
//   -1 Invalid
//   -2 Error code
//   -3 Calib invalid
int MCT_set_state(tFSMState state)
{
    int ret = 0;

    switch (mFSM.state) {
    case BOOT_UP:
        if (state == IDLE) {
            mFSM.state_next = IDLE;
        } else {
            ret = -1;
        }
        break;

    case IDLE:
        switch (state) {
        case IDLE:
            if (mCalibrationAxisMask != 0U) {
                CALIBRATION_end();
            }
            mct_disarm_outputs();
            mRunAxisMask = 0U;
            mRunAxisPendingMask = 0U;
            mCalibrationAxisMask = 0U;
            mCalibrationChargeBootCapDelay = 0U;
            mAxisChargeBootCapDelay[0] = 0U;
            mAxisChargeBootCapDelay[1] = 0U;
            mChargeBootCapDelay = 0;
            mFSM.state_next     = IDLE;
        break;

        case RUN:
        {
            uint8_t run_axis_mask_old = mRunAxisMask;

            if (mCalibrationAxisMask != 0U) {
                ret = -1;
                break;
            }

            ret = mct_enable_axis_for_run(MOTOR_HW_AXIS_LEFT);
            if (ret == 0) {
                ret = mct_enable_axis_for_run(MOTOR_HW_AXIS_RIGHT);
            }
            if (ret == 0) {
                mChargeBootCapDelay = CHARGE_BOOT_CAP_TICKS;
                mFSM.state_next     = RUN;
            } else {
                mct_disarm_outputs();
                mRunAxisMask = run_axis_mask_old;
                mRunAxisPendingMask = 0U;
                mCalibrationChargeBootCapDelay = 0U;
                mAxisChargeBootCapDelay[0] = 0U;
                mAxisChargeBootCapDelay[1] = 0U;
            }
            break;
        }

        case CALIBRATION:
            ret = mct_start_axis_calibration(CALIBRATION_active_axis());
            if (ret == 0) {
                mFSM.state_next = CALIBRATION;
            }
            break;

        case ANTICOGGING:
            if (mCalibrationAxisMask != 0U) {
                ret = -1;
            } else if (mct_any_axis_has_error()) {
                ret = -2;
            } else if (!mct_axis_calibrated(ANTICOGGING_active_axis())) {
                ret = -3;
            } else {
                mct_disarm_outputs();
                mRunAxisMask = 0U;
                mRunAxisPendingMask = 0U;
                mCalibrationAxisMask = 0U;
                mCalibrationChargeBootCapDelay = 0U;
                mAxisChargeBootCapDelay[0] = 0U;
                mAxisChargeBootCapDelay[1] = 0U;
                FOC_axis_arm(ANTICOGGING_active_axis());
                mChargeBootCapDelay = CHARGE_BOOT_CAP_TICKS;
                mFSM.state_next     = ANTICOGGING;
            }
            break;

        default:
            ret = -1;
            break;
        }
        break;

    case RUN:
        switch (state) {
        case IDLE:
            mFSM.state_next = IDLE;
            break;

        case CALIBRATION:
            ret = mct_start_axis_calibration(CALIBRATION_active_axis());
            break;

        default:
            ret = -1;
            break;
        }
        if ((ret == 0) && (state == CALIBRATION) && (mRunAxisMask == 0U)) {
            mFSM.state_next = CALIBRATION;
        }
        break;

    default:
        if (state == IDLE) {
            mFSM.state_next = IDLE;
        } else {
            ret = -1;
        }
        break;
    }

    mFSM.state_next_ready = 0;

    return ret;
}

int MCT_axis_enable(motor_hw_axis_t axis)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    int ret = 0;

    if (MCT_axis_is_calibrating(axis)) {
        return MCT_axis_calibration_abort(axis);
    }

    switch (mFSM.state) {
    case IDLE:
        if (mCalibrationAxisMask != 0U) {
            ret = -1;
            break;
        }
        ret = mct_enable_axis_for_run(axis);
        if (ret == 0) {
            mChargeBootCapDelay = CHARGE_BOOT_CAP_TICKS;
            mFSM.state_next     = RUN;
            mFSM.state_next_ready = 0;
        }
        break;

    case CALIBRATION:
        ret = mct_enable_axis_for_run(axis);
        if (ret == 0) {
            mFSM.state_next = RUN;
            mFSM.state_next_ready = 1;
        }
        break;

    case RUN:
        ret = mct_enable_axis_for_run(axis);
        break;

    default:
        ret = -1;
        break;
    }

    mct_sync_legacy_status_from_active();

    return ret;
#else
    (void) axis;
    return MCT_set_state(RUN);
#endif
}

int MCT_axis_disable(motor_hw_axis_t axis)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    int ret = 0;

    switch (mFSM.state) {
    case IDLE:
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        if (!MCT_axis_is_enabled(axis) && (mRunAxisMask == 0U)) {
            mct_set_axis_status(axis, 0U, 0U);
            break;
        }
#endif
        mct_disable_axis_for_run(axis);
        if (mRunAxisMask == 0U) {
            mChargeBootCapDelay = 0U;
            mFSM.state_next     = IDLE;
            mFSM.state_next_ready = 0;
            mRunAxisPendingMask = 0U;
        }
        break;

    case RUN:
        mct_disable_axis_for_run(axis);
        if ((mRunAxisMask == 0U) && (mCalibrationAxisMask == 0U)) {
            ret = MCT_set_state(IDLE);
        }
        break;

    case CALIBRATION:
        mct_disable_axis_for_run(axis);
        break;

    default:
        ret = -1;
        break;
    }

    mct_sync_legacy_status_from_active();

    return ret;
#else
    (void) axis;
    return MCT_set_state(IDLE);
#endif
}

int MCT_axis_calibration_abort(motor_hw_axis_t axis)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (!MCT_axis_is_calibrating(axis)) {
        return 0;
    }

    mct_stop_axis_calibration(axis);
    if ((mRunAxisMask == 0U) && (mCalibrationAxisMask == 0U)) {
        mFSM.state_next = IDLE;
        mFSM.state_next_ready = 0;
    }
    mct_sync_legacy_status_from_active();

    return 0;
#else
    (void) axis;
    return MCT_set_state(IDLE);
#endif
}

static void enter_state(void)
{
    switch (mFSM.state) {
    case BOOT_UP:
        break;

    case IDLE:
        break;

    case RUN:
    #if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)) {
            CONTROLLER_axis_reset(MOTOR_HW_AXIS_LEFT);
            FOC_axis_arm(MOTOR_HW_AXIS_LEFT);
            mct_set_axis_status(MOTOR_HW_AXIS_LEFT, 1U, 1U);
        }
        if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)) {
            CONTROLLER_axis_reset(MOTOR_HW_AXIS_RIGHT);
            FOC_axis_arm(MOTOR_HW_AXIS_RIGHT);
            mct_set_axis_status(MOTOR_HW_AXIS_RIGHT, 1U, 1U);
        }
    #else
        CONTROLLER_reset();
        mct_arm_run_outputs();
        mct_set_axis_status(MOTOR_HW_AXIS_LEFT, 1U, 1U);
        mct_set_axis_status(MOTOR_HW_AXIS_RIGHT, 1U, 1U);
    #endif
        mct_sync_legacy_status_from_active();
        break;

    case CALIBRATION:
        if (mCalibrationAxisMask == 0U) {
            CALIBRATION_start();
            mCalibrationAxisMask |= mct_axis_mask(CALIBRATION_active_axis());
        }
        break;

    case ANTICOGGING:
        CONTROLLER_axis_reset(ANTICOGGING_active_axis());
        ANTICOGGING_start();
        break;

    default:
        break;
    }
}

static void exit_state(void)
{
    switch (mFSM.state) {
    case BOOT_UP:
        CAN_reset_rx_timeout();
        CAN_reset_tx_timeout();
        mFSM.state_next_ready = 1;
        break;

    case IDLE:
        if (mChargeBootCapDelay) {
            mChargeBootCapDelay--;
        } else {
            mFSM.state_next_ready = 1;
        }
        break;

    case RUN:
        if (mFSM.state_next == CALIBRATION) {
            mFSM.state_next_ready = 1;
            break;
        }
        if (mCalibrationAxisMask != 0U) {
            CALIBRATION_end();
            mCalibrationAxisMask = 0U;
            mCalibrationChargeBootCapDelay = 0U;
        }
        mct_disarm_outputs();
        mRunAxisMask = 0U;
        mRunAxisPendingMask = 0U;
        mct_set_axis_status(MOTOR_HW_AXIS_LEFT, 0U, 0U);
        mct_set_axis_status(MOTOR_HW_AXIS_RIGHT, 0U, 0U);
        mct_sync_legacy_status_from_active();
        mFSM.state_next_ready = 1;
        break;

    case CALIBRATION:
        if (mFSM.state_next == RUN) {
            mFSM.state_next_ready = 1;
            break;
        }
        if (mCalibrationAxisMask != 0U) {
            CALIBRATION_end();
            mCalibrationAxisMask = 0U;
        }
        mFSM.state_next_ready = 1;
        break;

    case ANTICOGGING:
        ANTICOGGING_end();
        mFSM.state_next_ready = 1;
        break;

    default:
        break;
    }
}

void MCT_high_frequency_task(void)
{
    static uint8_t rtt_scope_div = 0U;

    /* state transition management */
    if (mFSM.state_next != mFSM.state) {
        exit_state();
        if (mFSM.state_next_ready) {
            mFSM.state = mFSM.state_next;
            enter_state();
        }
    }

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    ENCODER_axis_loop(MOTOR_HW_AXIS_LEFT);
    ENCODER_axis_loop(MOTOR_HW_AXIS_RIGHT);
    mct_update_axis_feedback(MOTOR_HW_AXIS_LEFT);
    mct_update_axis_feedback(MOTOR_HW_AXIS_RIGHT);
#else
    ENCODER_loop();
    Foc.v_bus = MOTOR_HW_read_vbus_voltage();
    UTILS_LP_FAST(Foc.v_bus_filt, Foc.v_bus, 0.05f);
    Foc.i_a = MOTOR_HW_read_phase_a_current();
    Foc.i_b = MOTOR_HW_read_phase_b_current();
    Foc.i_c = -(Foc.i_a + Foc.i_b);
#endif

    if (++rtt_scope_div >= 20U) {
        rtt_scope_div = 0U;
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        RTT_scope_write6(FOC_axis(MOTOR_HW_AXIS_LEFT)->i_q_filt,
                         ENCODER_axis(MOTOR_HW_AXIS_LEFT)->pos,
                         ENCODER_axis(MOTOR_HW_AXIS_LEFT)->vel,
                         FOC_axis(MOTOR_HW_AXIS_RIGHT)->i_q_filt,
                         ENCODER_axis(MOTOR_HW_AXIS_RIGHT)->pos,
                         ENCODER_axis(MOTOR_HW_AXIS_RIGHT)->vel);
#else
        RTT_scope_write6(Foc.i_a, Foc.i_b, Foc.i_c, Foc.v_bus_filt, Encoder.pos, Encoder.vel);
#endif
    }

    switch (mFSM.state) {
    case BOOT_UP:
        break;

    case IDLE:
        break;

    case CALIBRATION:
    {
        mct_process_axis_calibration();
        break;
    }

    case ANTICOGGING:
        ANTICOGGING_loop();
        CONTROLLER_axis_loop(ANTICOGGING_active_axis());

        if (mct_axis_over_current(ANTICOGGING_active_axis())) {
            mct_disarm_outputs();
            MCT_set_state(IDLE);
            MCT_axis_statusword(ANTICOGGING_active_axis())->errors.over_current = 1;
            mct_sync_legacy_status_from_active();
        }
        break;

    case RUN:
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        if (mCalibrationAxisMask != 0U) {
            mct_process_axis_calibration();
        }

        mct_update_run_axes_control();
#else
        CONTROLLER_loop();
#endif

        // check over current
        mct_check_run_axes_over_current();
        break;

    default:
        break;
    }
}

static void mct_arm_run_outputs(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)) {
        FOC_axis_arm(MOTOR_HW_AXIS_LEFT);
    }
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)) {
        FOC_axis_arm(MOTOR_HW_AXIS_RIGHT);
    }
#else
    FOC_arm();
#endif
}

static void mct_disarm_outputs(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    FOC_axis_disarm(MOTOR_HW_AXIS_LEFT);
    FOC_axis_disarm(MOTOR_HW_AXIS_RIGHT);
#else
    FOC_disarm();
#endif
}

static int mct_start_axis_calibration(motor_hw_axis_t axis)
{
    if (mct_axis_has_error(axis)) {
        return -2;
    }
    if (mCalibrationAxisMask != 0U) {
        return -1;
    }

    mct_disable_axis_for_run(axis);
    CALIBRATION_select_axis(axis);
    CALIBRATION_axis_start(axis);
    FOC_axis_arm(axis);
    mCalibrationAxisMask = mct_axis_mask(axis);
    mChargeBootCapDelay = (mRunAxisMask == 0U) ? CHARGE_BOOT_CAP_TICKS : 0U;
    mCalibrationChargeBootCapDelay = CHARGE_BOOT_CAP_TICKS;

    return 0;
}

static void mct_stop_axis_calibration(motor_hw_axis_t axis)
{
    uint8_t mask = mct_axis_mask(axis);

    if ((mCalibrationAxisMask & mask) == 0U) {
        return;
    }
    CALIBRATION_end();
    mCalibrationAxisMask &= (uint8_t) ~mask;
    mChargeBootCapDelay = 0U;
    mCalibrationChargeBootCapDelay = 0U;
}

static void mct_finish_axis_calibration(motor_hw_axis_t axis)
{
    mct_stop_axis_calibration(axis);
    mct_sync_legacy_status_from_active();
}

static int mct_enable_axis_for_run(motor_hw_axis_t axis)
{
    if (mct_axis_has_error(axis)) {
        return -2;
    }
    if (!mct_axis_calibrated(axis)) {
        return -3;
    }
    if (MCT_axis_is_calibrating(axis)) {
        return -1;
    }
    if (MCT_axis_is_enabled(axis)) {
        return 0;
    }

    mRunAxisMask |= mct_axis_mask(axis);

    if (mFSM.state == IDLE) {
        mRunAxisPendingMask &= (uint8_t) ~mct_axis_mask(axis);
        mAxisChargeBootCapDelay[mct_axis_index(axis)] = 0U;
        FOC_axis_arm(axis);
    } else if ((mFSM.state == RUN) || (mFSM.state == CALIBRATION)) {
        mRunAxisPendingMask |= mct_axis_mask(axis);
        mAxisChargeBootCapDelay[mct_axis_index(axis)] = CHARGE_BOOT_CAP_TICKS;
        CONTROLLER_axis_reset(axis);
        FOC_axis_arm(axis);
        mct_set_axis_status(axis, 1U, 1U);
    } else if ((mFSM.state == BOOT_UP) && (mFSM.state_next == RUN)) {
        mRunAxisPendingMask &= (uint8_t) ~mct_axis_mask(axis);
        mAxisChargeBootCapDelay[mct_axis_index(axis)] = 0U;
        FOC_axis_arm(axis);
    }

    return 0;
}

static void mct_disable_axis_for_run(motor_hw_axis_t axis)
{
    uint8_t mask = mct_axis_mask(axis);

    mRunAxisMask &= (uint8_t) ~mask;
    mRunAxisPendingMask &= (uint8_t) ~mask;
    mAxisChargeBootCapDelay[mct_axis_index(axis)] = 0U;
    FOC_axis_disarm(axis);
    mct_set_axis_status(axis, 0U, 0U);
}

static void mct_update_axis_enable_delay(motor_hw_axis_t axis)
{
    uint8_t idx = mct_axis_index(axis);
    uint8_t mask = mct_axis_mask(axis);

    if ((mRunAxisPendingMask & mask) == 0U) {
        return;
    }

    if (mAxisChargeBootCapDelay[idx] > 0U) {
        mAxisChargeBootCapDelay[idx]--;
    } else {
        mRunAxisPendingMask &= (uint8_t) ~mask;
    }
}

static void mct_update_run_axes_control(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    mct_update_axis_enable_delay(MOTOR_HW_AXIS_LEFT);
    mct_update_axis_enable_delay(MOTOR_HW_AXIS_RIGHT);

    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)
        && ((mRunAxisPendingMask & mct_axis_mask(MOTOR_HW_AXIS_LEFT)) == 0U)) {
        mct_run_axis_control(MOTOR_HW_AXIS_LEFT);
    }
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)
        && ((mRunAxisPendingMask & mct_axis_mask(MOTOR_HW_AXIS_RIGHT)) == 0U)) {
        mct_run_axis_control(MOTOR_HW_AXIS_RIGHT);
    }
#endif
}

static void mct_check_run_axes_over_current(void)
{
    if (!mct_any_axis_over_current()) {
        return;
    }

    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)
        && mct_axis_over_current(MOTOR_HW_AXIS_LEFT)) {
        mct_disable_axis_for_run(MOTOR_HW_AXIS_LEFT);
        MCT_axis_statusword(MOTOR_HW_AXIS_LEFT)->errors.over_current = 1;
    }
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)
        && mct_axis_over_current(MOTOR_HW_AXIS_RIGHT)) {
        mct_disable_axis_for_run(MOTOR_HW_AXIS_RIGHT);
        MCT_axis_statusword(MOTOR_HW_AXIS_RIGHT)->errors.over_current = 1;
    }
    if ((mRunAxisMask == 0U) && (mCalibrationAxisMask == 0U)) {
        MCT_set_state(IDLE);
    }
    mct_sync_legacy_status_from_active();
}

static void mct_process_axis_calibration(void)
{
    tCalibrationResult calib_result;
    motor_hw_axis_t calib_axis;

    if (mCalibrationAxisMask == 0U) {
        if (mRunAxisMask == 0U) {
            MCT_set_state(IDLE);
        }
        return;
    }

    if (mCalibrationChargeBootCapDelay > 0U) {
        mCalibrationChargeBootCapDelay--;
        return;
    }

    calib_axis = CALIBRATION_active_axis();
    calib_result = CALIBRATION_loop();

    if (mct_axis_over_current(calib_axis)) {
        MCT_axis_statusword(calib_axis)->errors.over_current = 1;
        mct_stop_axis_calibration(calib_axis);
        if (mRunAxisMask == 0U) {
            MCT_set_state(IDLE);
        }
        mct_sync_legacy_status_from_active();
    } else if (calib_result == CALIBRATION_RESULT_DONE) {
        mct_finish_axis_calibration(calib_axis);
        if (mRunAxisMask == 0U) {
            MCT_set_state(IDLE);
        }
    } else if (calib_result == CALIBRATION_RESULT_FAILED) {
        mct_stop_axis_calibration(calib_axis);
        if (mRunAxisMask == 0U) {
            MCT_set_state(IDLE);
        }
        mct_sync_legacy_status_from_active();
    }
}

static uint8_t mct_axis_mask(motor_hw_axis_t axis)
{
    return (uint8_t) (1U << mct_axis_index(axis));
}

static bool mct_axis_over_current(motor_hw_axis_t axis)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    tFOC *foc = FOC_axis(axis);
    tAxisConfig *config = USR_CONFIG_axis(axis);

    if (!MCT_axis_is_enabled(axis) && !MCT_axis_is_calibrating(axis)) {
        return false;
    }

    return ((ABS(foc->i_a) > config->protect_over_current)
            || (ABS(foc->i_b) > config->protect_over_current)
            || (ABS(foc->i_c) > config->protect_over_current));
#else
    (void) axis;
    return ((ABS(Foc.i_a) > USR_CONFIG_axis(CONTROLLER_active_axis())->protect_over_current)
            || (ABS(Foc.i_b) > USR_CONFIG_axis(CONTROLLER_active_axis())->protect_over_current)
            || (ABS(Foc.i_c) > USR_CONFIG_axis(CONTROLLER_active_axis())->protect_over_current));
#endif
}

static bool mct_any_axis_over_current(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    bool over_current = false;

    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)) {
        over_current = over_current || mct_axis_over_current(MOTOR_HW_AXIS_LEFT);
    }
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)) {
        over_current = over_current || mct_axis_over_current(MOTOR_HW_AXIS_RIGHT);
    }

    return over_current;
#else
    return mct_axis_over_current(CONTROLLER_active_axis());
#endif
}

static void mct_update_axis_feedback(motor_hw_axis_t axis)
{
    tFOC *foc = FOC_axis(axis);
    tEncoder *encoder = ENCODER_axis(axis);

    foc->v_bus = MOTOR_HW_read_vbus_voltage();
    UTILS_LP_FAST(foc->v_bus_filt, foc->v_bus, 0.05f);
    foc->i_a = MOTOR_HW_read_axis_phase_a_current(axis);
    foc->i_b = MOTOR_HW_read_axis_phase_b_current(axis);
    foc->i_c = -(foc->i_a + foc->i_b);

    if (axis == CONTROLLER_active_axis()) {
        Foc = *foc;
        Encoder = *encoder;
    }
}

static void mct_run_axis_control(motor_hw_axis_t axis)
{
    CONTROLLER_axis_loop(axis);

    if (axis == CONTROLLER_active_axis()) {
        Foc = *FOC_axis(axis);
        Encoder = *ENCODER_axis(axis);
        Controller = *CONTROLLER_axis(axis);
        Traj = *CONTROLLER_axis_traj(axis);
    }
}

static uint8_t mct_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static bool mct_axis_has_error(motor_hw_axis_t axis)
{
    return (StatuswordNewAxes[mct_axis_index(axis)].errors.errors_code != 0U);
}

static bool mct_any_axis_has_error(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    return (mct_axis_has_error(MOTOR_HW_AXIS_LEFT)
            || mct_axis_has_error(MOTOR_HW_AXIS_RIGHT));
#else
    return mct_axis_has_error(CONTROLLER_active_axis());
#endif
}

static bool mct_axis_calibrated(motor_hw_axis_t axis)
{
    return (USR_CONFIG_axis(axis)->calib_valid != 0);
}

static bool mct_all_axes_calibrated(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    return (mct_axis_calibrated(MOTOR_HW_AXIS_LEFT)
            && mct_axis_calibrated(MOTOR_HW_AXIS_RIGHT));
#else
    return mct_axis_calibrated(CONTROLLER_active_axis());
#endif
}

static void mct_set_axis_status(motor_hw_axis_t axis, uint8_t switched_on, uint8_t target_reached)
{
    uint8_t idx = mct_axis_index(axis);

    StatuswordNewAxes[idx].status.switched_on = switched_on;
    StatuswordNewAxes[idx].status.target_reached = target_reached;
    StatuswordOldAxes[idx].status = StatuswordNewAxes[idx].status;
}

static void mct_sync_legacy_status_from_active(void)
{
    uint8_t idx = mct_axis_index(CONTROLLER_active_axis());

    StatuswordNew = StatuswordNewAxes[idx];
    StatuswordOld = StatuswordOldAxes[idx];
}

void MCT_safety_task(void)
{
    // VBUS check
    if (mFSM.state != BOOT_UP) {
        for (uint8_t i = 0U; i < MOTOR_HW_AXIS_COUNT; i++) {
            motor_hw_axis_t axis = (i == 0U) ? MOTOR_HW_AXIS_LEFT : MOTOR_HW_AXIS_RIGHT;
            tFOC *foc = FOC_axis(axis);
            tAxisConfig *config = USR_CONFIG_axis(axis);

            // Over voltage check
            if (foc->v_bus > config->protect_over_voltage) {
                StatuswordNewAxes[i].errors.over_voltage = 1;
            }

            // Under voltage check
            if (foc->v_bus < config->protect_under_voltage) {
                StatuswordNewAxes[i].errors.under_voltage = 1;
            }

            // drv over tmp
            if (MOTOR_HW_read_driver_temp() > config->protect_drv_over_tmp) {
                StatuswordNewAxes[i].errors.drv_over_tmp = 1;
            }

            // ntc over tmp
            if (MOTOR_HW_read_ntc_temp() > config->protect_ntc_over_tmp) {
                StatuswordNewAxes[i].errors.ntc_over_tmp = 1;
            }
        }
        mct_sync_legacy_status_from_active();
    }

    watch_dog_feed();
}

void MCT_low_priority_task(void)
{
    for (uint8_t i = 0U; i < MOTOR_HW_AXIS_COUNT; i++) {
        motor_hw_axis_t axis = (i == 0U) ? MOTOR_HW_AXIS_LEFT : MOTOR_HW_AXIS_RIGHT;

        // State check
        if (StatuswordOldAxes[i].status.status_code != StatuswordNewAxes[i].status.status_code) {
            StatuswordOldAxes[i].status.status_code = StatuswordNewAxes[i].status.status_code;
        }

        // Error check
        if (StatuswordOldAxes[i].errors.errors_code != StatuswordNewAxes[i].errors.errors_code) {
            if (StatuswordNewAxes[i].errors.errors_code) {
                if (MCT_axis_is_calibrating(axis)) {
                    mct_stop_axis_calibration(axis);
                } else {
                    mct_disable_axis_for_run(axis);
                }
                if ((mRunAxisMask == 0U) && (mCalibrationAxisMask == 0U)) {
                    MCT_set_state(IDLE);
                }
            }
            StatuswordOldAxes[i].errors.errors_code = StatuswordNewAxes[i].errors.errors_code;
            CAN_tx_axis_statusword(axis, StatuswordNewAxes[i]);
        }
    }

    mct_sync_legacy_status_from_active();

    led_act_loop();
    CAN_comm_loop();
}

static void led_act_loop(void)
{
    static uint16_t tick       = 0;
    static uint32_t tick_100Hz = 0;

    // 100Hz
    if (get_ms_since(tick_100Hz) < 10) {
        return;
    }
    tick_100Hz = SystickCount;

    switch (mFSM.state) {
    case IDLE:
        if (tick == 0) {
            LED_ACT_SET();
        } else if (tick == 10) {
            LED_ACT_RESET();
        } else if (tick > 100) {
            tick = 0xFFFF;
        }
        break;

    case RUN:
        if (tick == 0) {
            LED_ACT_SET();
        } else if (tick == 10) {
            LED_ACT_RESET();
        } else if (tick == 20) {
            LED_ACT_SET();
        } else if (tick == 30) {
            LED_ACT_RESET();
        } else if (tick > 100) {
            tick = 0xFFFF;
        }
        break;

    case CALIBRATION:
        if (tick == 0) {
            LED_ACT_SET();
        } else if (tick == 10) {
            LED_ACT_RESET();
        } else if (tick == 20) {
            LED_ACT_SET();
        } else if (tick == 30) {
            LED_ACT_RESET();
        } else if (tick == 40) {
            LED_ACT_SET();
        } else if (tick == 50) {
            LED_ACT_RESET();
        } else if (tick > 150) {
            tick = 0xFFFF;
        }
        break;

    case ANTICOGGING:
        if (tick == 0) {
            LED_ACT_SET();
        } else if (tick == 10) {
            LED_ACT_RESET();
        } else if (tick == 20) {
            LED_ACT_SET();
        } else if (tick == 30) {
            LED_ACT_RESET();
        } else if (tick == 40) {
            LED_ACT_SET();
        } else if (tick == 50) {
            LED_ACT_RESET();
        } else if (tick == 60) {
            LED_ACT_SET();
        } else if (tick == 70) {
            LED_ACT_RESET();
        } else if (tick > 200) {
            tick = 0xFFFF;
        }
        break;

    default:
        break;
    }

    tick++;
}
