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

#include "can.h"
#include "anticogging.h"
#include "board_port.h"
#include "can_hw.h"
#include "calibration.h"
#include "controller.h"
#include "dfu.h"
#include "encoder.h"
#include "foc.h"
#include "motor_hw.h"
#include "runtime.h"
#include "usr_config.h"
#include "util.h"
#include <string.h>

static uint8_t  mNodeID;
static uint32_t mRxTick       = 0;
static uint32_t mAxisRxTick[MOTOR_HW_AXIS_COUNT];
static uint32_t mTxTick       = 0;
static uint32_t mCanBusErrNew = 0;
static uint32_t mCanBusErrOld = 0;

static void can_error_check(void);
static bool can_tx(CanFrame *tx_frame);
static bool can_is_idle_and_disarmed(void);
static uint8_t can_axis_node_id(motor_hw_axis_t axis);
static bool can_node_to_axis(uint8_t node_id, motor_hw_axis_t *axis);
static bool can_frame_is_broadcast(const CanFrame *frame);
static bool can_for_each_target_axis(uint8_t node_id, motor_hw_axis_t *axis, uint8_t *cursor);
static void can_reset_axis_rx_timeout(motor_hw_axis_t axis);
static void can_reset_target_rx_timeout(uint8_t node_id);
static void can_check_rx_timeout(void);
static void can_tx_heartbeat(void);
static uint8_t can_axis_index(motor_hw_axis_t axis);

static void fill_value(motor_hw_axis_t axis, uint8_t idx, uint8_t *data);
static void can_tx_axis_statusword_reply(motor_hw_axis_t axis);
static void can_tx_axis_value_reply(motor_hw_axis_t axis, uint8_t cmd, uint8_t value_index);
static void parse_frame(CanFrame *frame);
static void config_callback(motor_hw_axis_t axis, uint8_t *data, bool isSet);
static void config_reject(uint8_t *data);
static bool config_value_is_valid(motor_hw_axis_t axis, int32_t idx, uint8_t *value);
static bool int32_in_range(int32_t value, int32_t min_value, int32_t max_value);
static bool float_in_range(float value, float min_value, float max_value);

void CAN_set_node_id(uint8_t nodeID)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (nodeID > 30U) {
        nodeID = 30U;
    }
#endif
    if (nodeID == 0U) {
        nodeID = 1U;
    }
    mNodeID = nodeID;
}

void CAN_comm_loop(void)
{
    can_check_rx_timeout();

    // tx heartbeat timeout check
    if (UsrConfig.heartbeat_producer_ms) {
        if (get_ms_since(mTxTick) > UsrConfig.heartbeat_producer_ms) {
            can_tx_heartbeat();
        }
    }

    // CAN bus error check
    can_error_check();
}

void CAN_receive_callback(void)
{
    CanFrame rxframe;
    while (CAN_hw_receive(&rxframe)) {
        parse_frame(&rxframe);
    }
}

void CAN_reset_rx_timeout(void)
{
    mRxTick = SystickCount;
    mAxisRxTick[0] = SystickCount;
    mAxisRxTick[1] = SystickCount;
}

void CAN_reset_tx_timeout(void)
{
    mTxTick = SystickCount;
}

void CAN_tx_statusword(tMCStatusword statusword)
{
    CAN_tx_axis_statusword(CONTROLLER_active_axis(), statusword);
}

void CAN_tx_axis_statusword(motor_hw_axis_t axis, tMCStatusword statusword)
{
    CanFrame tx_frame;
    tx_frame.id      = ID_ECHO_BIT | (can_axis_node_id(axis) << 5) | CAN_CMD_STATUSWORD_REPORT;
    tx_frame.data[0] = statusword.status.status_code;
    tx_frame.data[1] = statusword.errors.errors_code;
    tx_frame.dlc     = 2;
    (void) can_tx(&tx_frame);
}

void CAN_calib_report(int32_t step, uint8_t *data)
{
    CanFrame tx_frame;
    tx_frame.id  = ID_ECHO_BIT | (can_axis_node_id(CALIBRATION_active_axis()) << 5) | CAN_CMD_CALIB_REPORT;
    tx_frame.dlc = 0;
    tx_frame.dlc += int32_to_data(step, &tx_frame.data[tx_frame.dlc]);
    memcpy(&tx_frame.data[tx_frame.dlc], data, 4);
    tx_frame.dlc += 4;
    (void) can_tx(&tx_frame);
}

void CAN_anticogging_report(int32_t step, int32_t value)
{
    CanFrame tx_frame;
    tx_frame.id  = ID_ECHO_BIT | (can_axis_node_id(ANTICOGGING_active_axis()) << 5) | CAN_CMD_ANTICOGGING_REPORT;
    tx_frame.dlc = 0;
    tx_frame.dlc += int32_to_data(step, &tx_frame.data[tx_frame.dlc]);
    tx_frame.dlc += int32_to_data(value, &tx_frame.data[tx_frame.dlc]);
    (void) can_tx(&tx_frame);
}

static void can_error_check(void)
{
    mCanBusErrNew = CAN_hw_status_bits();

    if (mCanBusErrOld != mCanBusErrNew) {
        mCanBusErrOld = mCanBusErrNew;
        if ((CAN_ERROR_STATE_PASSIVE == (can_error_state_enum) (mCanBusErrNew & 0x03U))
            || (CAN_ERROR_STATE_BUS_OFF == (can_error_state_enum) (mCanBusErrNew & 0x03U))) {
            CAN_hw_abort_tx();
        }
    }
}

static bool can_tx(CanFrame *tx_frame)
{
    bool sent = CAN_hw_send(tx_frame);

    if (sent) {
        CAN_reset_tx_timeout();
    }

    return sent;
}

static uint8_t can_axis_node_id(motor_hw_axis_t axis)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return (uint8_t) (mNodeID + 1U);
    }
#else
    (void) axis;
#endif

    return mNodeID;
}

static bool can_node_to_axis(uint8_t node_id, motor_hw_axis_t *axis)
{
    if (node_id == mNodeID) {
        *axis = MOTOR_HW_AXIS_LEFT;
        return true;
    }

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (node_id == (uint8_t) (mNodeID + 1U)) {
        *axis = MOTOR_HW_AXIS_RIGHT;
        return true;
    }
#endif

    return false;
}

static bool can_frame_is_broadcast(const CanFrame *frame)
{
    return (GET_NODE_ID(frame->id) == 0U);
}

static bool can_for_each_target_axis(uint8_t node_id, motor_hw_axis_t *axis, uint8_t *cursor)
{
    if (node_id == 0U) {
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        if (*cursor == 0U) {
            *axis = MOTOR_HW_AXIS_LEFT;
            *cursor = 1U;
            return true;
        }
        if (*cursor == 1U) {
            *axis = MOTOR_HW_AXIS_RIGHT;
            *cursor = 2U;
            return true;
        }
        return false;
#else
        if (*cursor == 0U) {
            *axis = CONTROLLER_active_axis();
            *cursor = 1U;
            return true;
        }
        return false;
#endif
    }

    if (*cursor != 0U) {
        return false;
    }

    *cursor = 1U;
    return can_node_to_axis(node_id, axis);
}

static void can_reset_axis_rx_timeout(motor_hw_axis_t axis)
{
    mAxisRxTick[can_axis_index(axis)] = SystickCount;
}

static void can_reset_target_rx_timeout(uint8_t node_id)
{
    motor_hw_axis_t axis;
    uint8_t cursor = 0U;

    mRxTick = SystickCount;
    while (can_for_each_target_axis(node_id, &axis, &cursor)) {
        can_reset_axis_rx_timeout(axis);
    }
}

static void can_check_rx_timeout(void)
{
    if (!UsrConfig.heartbeat_consumer_ms) {
        return;
    }

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_LEFT)
        && (get_ms_since(mAxisRxTick[can_axis_index(MOTOR_HW_AXIS_LEFT)]) > UsrConfig.heartbeat_consumer_ms)) {
        MCT_axis_disable(MOTOR_HW_AXIS_LEFT);
    }
    if (MCT_axis_is_enabled(MOTOR_HW_AXIS_RIGHT)
        && (get_ms_since(mAxisRxTick[can_axis_index(MOTOR_HW_AXIS_RIGHT)]) > UsrConfig.heartbeat_consumer_ms)) {
        MCT_axis_disable(MOTOR_HW_AXIS_RIGHT);
    }

    if (MCT_axis_is_calibrating(MOTOR_HW_AXIS_LEFT)
        && (get_ms_since(mAxisRxTick[can_axis_index(MOTOR_HW_AXIS_LEFT)]) > UsrConfig.heartbeat_consumer_ms)) {
        MCT_axis_calibration_abort(MOTOR_HW_AXIS_LEFT);
    }
    if (MCT_axis_is_calibrating(MOTOR_HW_AXIS_RIGHT)
        && (get_ms_since(mAxisRxTick[can_axis_index(MOTOR_HW_AXIS_RIGHT)]) > UsrConfig.heartbeat_consumer_ms)) {
        MCT_axis_calibration_abort(MOTOR_HW_AXIS_RIGHT);
    }
#else
    if (get_ms_since(mRxTick) > UsrConfig.heartbeat_consumer_ms) {
        MCT_set_state(IDLE);
    }
#endif
}

static void can_tx_heartbeat(void)
{
    CanFrame tx_frame;

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    tx_frame.id  = ID_ECHO_BIT | (can_axis_node_id(MOTOR_HW_AXIS_LEFT) << 5) | CAN_CMD_HEARTBEAT;
    tx_frame.dlc = 0;
    can_tx(&tx_frame);

    tx_frame.id  = ID_ECHO_BIT | (can_axis_node_id(MOTOR_HW_AXIS_RIGHT) << 5) | CAN_CMD_HEARTBEAT;
    tx_frame.dlc = 0;
    can_tx(&tx_frame);
#else
    tx_frame.id  = ID_ECHO_BIT | (mNodeID << 5) | CAN_CMD_HEARTBEAT;
    tx_frame.dlc = 0;
    can_tx(&tx_frame);
#endif
}

static uint8_t can_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static bool can_is_idle_and_disarmed(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    return ((MCT_get_state() == IDLE)
            && (!FOC_axis(MOTOR_HW_AXIS_LEFT)->is_armed)
            && (!FOC_axis(MOTOR_HW_AXIS_RIGHT)->is_armed));
#else
    return ((MCT_get_state() == IDLE) && (!Foc.is_armed));
#endif
}

static void fill_value(motor_hw_axis_t axis, uint8_t idx, uint8_t *data)
{
    tAxisConfig *config = USR_CONFIG_axis(axis);
    tFOC *foc = FOC_axis(axis);
    tEncoder *encoder = ENCODER_axis(axis);

    switch (idx) {
    case 0:
        if (config->invert_motor_dir) {
            float_to_data(-foc->i_q_filt, data);
        } else {
            float_to_data(+foc->i_q_filt, data);
        }
        break;

    case 1:
        if (config->invert_motor_dir) {
            float_to_data(-encoder->vel, data);
        } else {
            float_to_data(+encoder->vel, data);
        }
        break;

    case 2:
        if (config->invert_motor_dir) {
            float_to_data(-encoder->pos, data);
        } else {
            float_to_data(+encoder->pos, data);
        }
        break;

    case 3:
        float_to_data(foc->v_bus_filt, data);
        break;

    case 4:
        float_to_data(foc->i_bus_filt, data);
        break;

    case 5:
        float_to_data(foc->power_filt, data);
        break;

    case 6:
        float_to_data((float) MOTOR_HW_read_driver_temp(), data);
        break;

    case 7:
        float_to_data((float) MOTOR_HW_read_ntc_temp(), data);
        break;

    default:
        break;
    }
}

static void can_tx_axis_statusword_reply(motor_hw_axis_t axis)
{
    CanFrame tx_frame;

    tx_frame.id      = ID_ECHO_BIT | (can_axis_node_id(axis) << 5) | CAN_CMD_GET_STATUSWORD;
    tx_frame.data[0] = MCT_axis_statusword(axis)->status.status_code;
    tx_frame.data[1] = MCT_axis_statusword(axis)->errors.errors_code;
    tx_frame.dlc     = 2;
    (void) can_tx(&tx_frame);
}

static void can_tx_axis_value_reply(motor_hw_axis_t axis, uint8_t cmd, uint8_t value_index)
{
    CanFrame tx_frame;

    tx_frame.id = ID_ECHO_BIT | (can_axis_node_id(axis) << 5) | cmd;
    fill_value(axis, value_index, tx_frame.data);
    tx_frame.dlc = 4;
    (void) can_tx(&tx_frame);
}

static void parse_frame(CanFrame *frame)
{
    int     ret;
    bool    echo    = false;
    uint8_t node_id = GET_NODE_ID(frame->id);
    uint8_t cmd     = GET_CMD(frame->id);
    motor_hw_axis_t axis = CONTROLLER_active_axis();

    // Dir check
    if (IS_ECHO(frame->id)) {
        return;
    }

    // set echo bit
    frame->id |= ID_ECHO_BIT;

    // Node id check
    if ((node_id != 0U) && !can_node_to_axis(node_id, &axis)) {
        return;
    }

    can_reset_target_rx_timeout(node_id);

    switch (cmd) {
    case CAN_CMD_SET_OP_MODE: {
        uint8_t cursor = 0U;
        ret = (frame->dlc == 1U) ? 0 : -1;
        while ((ret == 0) && can_for_each_target_axis(node_id, &axis, &cursor)) {
            ret += CONTROLLER_axis_set_op_mode(axis, (tControlMode) frame->data[0]);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
    } break;

    case CAN_CMD_MOTOR_ENABLE:
        if (can_frame_is_broadcast(frame)) {
            ret = MCT_set_state(RUN);
        } else {
            ret = MCT_axis_enable(axis);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_MOTOR_DISABLE:
        if (can_frame_is_broadcast(frame)) {
            ret = MCT_set_state(IDLE);
        } else {
            ret = MCT_axis_disable(axis);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_SET_TORQUE:
        if (frame->dlc == 4) {
            uint8_t cursor = 0U;
            while (can_for_each_target_axis(node_id, &axis, &cursor)) {
                tAxisConfig *config = USR_CONFIG_axis(axis);
                tController *controller = CONTROLLER_axis(axis);

                if (config->invert_motor_dir) {
                    controller->input_current_buffer = -data_to_float(&frame->data[0]);
                } else {
                    controller->input_current_buffer = +data_to_float(&frame->data[0]);
                }
                if (!config->sync_target_enable) {
                    CONTROLLER_axis_sync_callback(axis);
                }
            }
        }
        break;

    case CAN_CMD_SET_VELOCITY:
        if (frame->dlc == 4) {
            uint8_t cursor = 0U;
            while (can_for_each_target_axis(node_id, &axis, &cursor)) {
                tAxisConfig *config = USR_CONFIG_axis(axis);
                tController *controller = CONTROLLER_axis(axis);

                if (config->invert_motor_dir) {
                    controller->input_velocity_buffer = -data_to_float(&frame->data[0]);
                } else {
                    controller->input_velocity_buffer = +data_to_float(&frame->data[0]);
                }
                if (!config->sync_target_enable) {
                    CONTROLLER_axis_sync_callback(axis);
                }
            }
        }
        break;

    case CAN_CMD_SET_POSITION:
        if (frame->dlc == 4) {
            uint8_t cursor = 0U;
            while (can_for_each_target_axis(node_id, &axis, &cursor)) {
                tAxisConfig *config = USR_CONFIG_axis(axis);
                tController *controller = CONTROLLER_axis(axis);

                if (config->invert_motor_dir) {
                    controller->input_position_buffer = -data_to_float(&frame->data[0]);
                } else {
                    controller->input_position_buffer = +data_to_float(&frame->data[0]);
                }
                if (!config->sync_target_enable) {
                    CONTROLLER_axis_sync_callback(axis);
                }
            }
        }
        break;

    case CAN_CMD_SYNC:
        {
            uint8_t cursor = 0U;
            while (can_for_each_target_axis(node_id, &axis, &cursor)) {
                if (USR_CONFIG_axis(axis)->sync_target_enable) {
                    CONTROLLER_axis_sync_callback(axis);
                }
            }
        }
        break;

    case CAN_CMD_CALIB_START:
        if (can_frame_is_broadcast(frame)) {
            ret = -1;
        } else {
            CALIBRATION_select_axis(axis);
            ret = MCT_set_state(CALIBRATION);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_CALIB_ABORT:
        if (can_frame_is_broadcast(frame)) {
            ret = MCT_set_state(IDLE);
        } else {
            ret = MCT_axis_calibration_abort(axis);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_ANTICOGGING_START:
        if (can_frame_is_broadcast(frame)) {
            ret = -1;
        } else {
            ANTICOGGING_select_axis(axis);
            ret = MCT_set_state(ANTICOGGING);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_ANTICOGGING_ABORT:
        ret            = MCT_set_state(IDLE);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_SET_HOME:
        if (can_frame_is_broadcast(frame)) {
            ret = CONTROLLER_set_home();
        } else {
            ret = CONTROLLER_axis_set_home(axis);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_ERROR_RESET:
        if (can_frame_is_broadcast(frame)) {
            ret = MCT_reset_error();
        } else {
            ret = MCT_axis_reset_error(axis);
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_GET_STATUSWORD:
        if (can_frame_is_broadcast(frame)) {
            can_tx_axis_statusword_reply(MOTOR_HW_AXIS_LEFT);
            can_tx_axis_statusword_reply(MOTOR_HW_AXIS_RIGHT);
            break;
        }
        frame->data[0] = MCT_axis_statusword(axis)->status.status_code;
        frame->data[1] = MCT_axis_statusword(axis)->errors.errors_code;
        frame->dlc     = 2;
        echo           = true;
        break;

    case CAN_CMD_GET_VALUE_1:
        if (can_frame_is_broadcast(frame)) {
            uint8_t value_index = frame->data[0];
            can_tx_axis_value_reply(MOTOR_HW_AXIS_LEFT, cmd, value_index);
            can_tx_axis_value_reply(MOTOR_HW_AXIS_RIGHT, cmd, value_index);
            break;
        }
        fill_value(axis, frame->data[0], frame->data);
        frame->dlc = 4;
        echo       = true;
        break;

    case CAN_CMD_GET_VALUE_2:
        if (can_frame_is_broadcast(frame)) {
            uint8_t value_index = frame->data[0];
            can_tx_axis_value_reply(MOTOR_HW_AXIS_LEFT, cmd, value_index);
            can_tx_axis_value_reply(MOTOR_HW_AXIS_RIGHT, cmd, value_index);
            break;
        }
        fill_value(axis, frame->data[0], frame->data);
        frame->dlc = 4;
        echo       = true;
        break;

    case CAN_CMD_HEARTBEAT:
        break;

    case CAN_CMD_SET_CONFIG:
        if ((frame->dlc == 8U) && !can_frame_is_broadcast(frame)) {
            config_callback(axis, frame->data, true);
        } else {
            config_reject(frame->data);
        }
        frame->dlc = 8;
        echo       = true;
        break;

    case CAN_CMD_GET_CONFIG:
        if ((frame->dlc == 8U) && !can_frame_is_broadcast(frame)) {
            config_callback(axis, frame->data, false);
        } else {
            config_reject(frame->data);
        }
        frame->dlc = 8;
        echo       = true;
        break;

    case CAN_CMD_SAVE_ALL_CONFIG:
        if (can_is_idle_and_disarmed()) {
            ret = 0;
            ret += USR_CONFIG_save_config();
            ret += USR_CONFIG_axis_save_cogging_map(MOTOR_HW_AXIS_LEFT);
            ret += USR_CONFIG_axis_save_cogging_map(MOTOR_HW_AXIS_RIGHT);
        } else {
            ret = -1;
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_RESET_ALL_CONFIG:
        if (can_is_idle_and_disarmed()) {
            USR_CONFIG_set_default_config();
            USR_CONFIG_axis_set_default_cogging_map(MOTOR_HW_AXIS_LEFT);
            USR_CONFIG_axis_set_default_cogging_map(MOTOR_HW_AXIS_RIGHT);
            ret = 0;
        } else {
            ret = -1;
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_GET_FW_VERSION:
        frame->data[0] = FW_VERSION_MAJOR;
        frame->data[1] = FW_VERSION_MINOR;
        frame->dlc     = 2;
        echo           = true;
        break;

    case CAN_CMD_DFU_START:
        ret = MCT_set_state(IDLE);
        if ((ret == 0) && can_is_idle_and_disarmed()) {
            watch_dog_feed();
            ret = DFU_write_app_back_start();
        } else {
            ret = -1;
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_DFU_DATA:
        ret            = can_is_idle_and_disarmed() ? DFU_write_app_back(&frame->data[0], frame->dlc) : -1;
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_DFU_END:
        watch_dog_feed();
        if ((frame->dlc == 8U) && can_is_idle_and_disarmed()) {
            ret = DFU_check_app_back(data_to_uint32(&frame->data[0]), data_to_uint32(&frame->data[4]));
        } else {
            ret = -1;
        }
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        if (ret == 0) {
            watch_dog_feed();
            (void) can_tx(frame);
            delay_ms(100);
            watch_dog_feed();
            DFU_jump_bootloader();
        }
        break;

    default:
        break;
    }

    if (echo) {
        frame->id = (frame->id & ~(uint32_t) ID_NODE_BIT) | ((uint32_t) can_axis_node_id(axis) << 5);
        (void) can_tx(frame);
    }
}

static void config_callback(motor_hw_axis_t axis, uint8_t *data, bool isSet)
{
    int32_t idx           = data_to_int32(data) - 1;
    int32_t axis_config_number = 29;
    int32_t board_config_base = axis_config_number;
    int32_t board_config_number = 4;

    if (idx < 0) {
        config_reject(data);
        return;
    }

    if (idx < axis_config_number) {
        tAxisConfig *config = USR_CONFIG_axis(axis);
        uint32_t *pConfig = &(((uint32_t *) config)[idx]);

        if (isSet) {
            if (!config_value_is_valid(axis, idx, &data[4])) {
                config_reject(data);
                return;
            }

            memcpy(pConfig, &data[4], 4);
            FOC_update_axis_current_ctrl_gain(axis, config->current_ctrl_bw);
            CONTROLLER_update_axis_input_pos_filter_gain(axis, config->position_filter_bw);
        } else {
            memcpy(&data[4], pConfig, 4);
        }
        return;
    }

    idx -= board_config_base;
    if ((idx < 0) || (idx >= board_config_number)) {
        config_reject(data);
        return;
    }

    uint32_t *pBoardConfig = &(((uint32_t *) (&UsrConfig.node_id))[idx]);

    if (isSet) {
        int32_t board_idx = 29 + idx;
        if (!config_value_is_valid(axis, board_idx, &data[4])) {
            config_reject(data);
            return;
        }

        memcpy(pBoardConfig, &data[4], 4);
    } else {
        memcpy(&data[4], pBoardConfig, 4);
    }
}

static void config_reject(uint8_t *data)
{
    int32_to_data(-1, &data[0]);
    int32_to_data(0, &data[4]);
}

static bool config_value_is_valid(motor_hw_axis_t axis, int32_t idx, uint8_t *value)
{
    tAxisConfig *config = USR_CONFIG_axis(axis);

    switch (idx) {
    case 0:
        return int32_in_range(data_to_int32(value), 0, 1);
    case 1:
        return int32_in_range(data_to_int32(value), 2, 30);
    case 2:
        return float_in_range(data_to_float(value), 0.0f, 10.0f);
    case 3:
        return float_in_range(data_to_float(value), 1.0e-9f, 1.0f);
    case 4:
        return float_in_range(data_to_float(value), 0.0f, 10.0f);
    case 5:
        return float_in_range(data_to_float(value), 0.0f, 100.0f);
    case 6:
        return float_in_range(data_to_float(value), 1.0e-6f, 10.0f);
    case 7:
        return float_in_range(data_to_float(value), 1.0e-6f, 50.0f);
    case 8:
    case 9:
    case 10:
        return float_in_range(data_to_float(value), 0.0f, 1000000.0f);
    case 11:
        return float_in_range(data_to_float(value), -1000000.0f, 1000000.0f);
    case 12:
        return float_in_range(data_to_float(value), 100.0f, 2000.0f);
    case 13:
        return int32_in_range(data_to_int32(value), CONTROL_MODE_CURRENT_RAMP, CONTROL_MODE_POSITION_PROFILE);
    case 14:
    case 15:
        return int32_in_range(data_to_int32(value), 0, 1);
    case 16:
    case 17:
        return float_in_range(data_to_float(value), 0.0f, 1000.0f);
    case 18:
        return float_in_range(data_to_float(value), 0.0f, 1000.0f);
    case 19:
        return float_in_range(data_to_float(value), 0.0f, 10000.0f);
    case 20:
        return float_in_range(data_to_float(value), 0.0f, 1000.0f);
    case 21:
        return float_in_range(data_to_float(value), 1.0e-6f, 1000.0f);
    case 22:
    case 23:
        return float_in_range(data_to_float(value), 1.0e-6f, 10000.0f);
    case 24: {
        float voltage = data_to_float(value);
        return (float_in_range(voltage, 0.0f, 50.0f) && (voltage < config->protect_over_voltage));
    }
    case 25: {
        float voltage = data_to_float(value);
        return (float_in_range(voltage, 0.0f, 50.0f) && (voltage > config->protect_under_voltage));
    }
    case 26:
        return float_in_range(data_to_float(value), 0.0f, 10.0f);
    case 27:
    case 28:
        return int32_in_range(data_to_int32(value), 0, 150);
    case 29:
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
        return int32_in_range(data_to_int32(value), 1, 30);
#else
        return int32_in_range(data_to_int32(value), 1, 31);
#endif
    case 30:
        return int32_in_range(data_to_int32(value), CAN_BAUDRATE_250K, CAN_BAUDRATE_1000K);
    case 31:
    case 32:
        return int32_in_range(data_to_int32(value), 0, 600000);
    default:
        return false;
    }
}

static bool int32_in_range(int32_t value, int32_t min_value, int32_t max_value)
{
    return ((value >= min_value) && (value <= max_value));
}

static bool float_in_range(float value, float min_value, float max_value)
{
    return ((value == value) && (value >= min_value) && (value <= max_value));
}
