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
#include "can_hw.h"
#include "controller.h"
#include "dfu.h"
#include "encoder.h"
#include "foc.h"
#include "motor_hw.h"
#include "runtime.h"
#include "usr_config.h"
#include "util.h"
#include <string.h>

#define CAN_DIAG_ENABLE 1

#if CAN_DIAG_ENABLE
#include "rtt_scope.h"
#define CAN_LOG(format, ...) DEBUG(format, ##__VA_ARGS__)
#else
#define CAN_LOG(format, ...)
#endif

static uint8_t  mNodeID;
static uint32_t mRxTick       = 0;
static uint32_t mTxTick       = 0;
static uint32_t mCanBusErrNew = 0;
static uint32_t mCanBusErrOld = 0;

static void can_error_check(void);
static bool can_tx(CanFrame *tx_frame);
static bool can_is_idle_and_disarmed(void);
static const char *can_cmd_name(uint8_t cmd);
static bool can_cmd_is_critical(uint8_t cmd);
static void can_log_frame(const char *tag, const CanFrame *frame);

static void fill_value(uint8_t idx, uint8_t *data);
static void parse_frame(CanFrame *frame);
static void config_callback(uint8_t *data, bool isSet);
static void config_reject(uint8_t *data);
static bool config_value_is_valid(int32_t idx, uint8_t *value);
static bool int32_in_range(int32_t value, int32_t min_value, int32_t max_value);
static bool float_in_range(float value, float min_value, float max_value);

void CAN_set_node_id(uint8_t nodeID)
{
    mNodeID = nodeID;
}

void CAN_comm_loop(void)
{
    // rx heartbeat timeout check
    if (UsrConfig.heartbeat_consumer_ms) {
        if (get_ms_since(mRxTick) > UsrConfig.heartbeat_consumer_ms) {
            MCT_set_state(IDLE);
        }
    }

    // tx heartbeat timeout check
    if (UsrConfig.heartbeat_producer_ms) {
        if (get_ms_since(mTxTick) > UsrConfig.heartbeat_producer_ms) {
            // Send heartbeat
            CanFrame tx_frame;
            tx_frame.id  = ID_ECHO_BIT | (mNodeID << 5) | CAN_CMD_HEARTBEAT;
            tx_frame.dlc = 0;
            can_tx(&tx_frame);
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
}

void CAN_reset_tx_timeout(void)
{
    mTxTick = SystickCount;
}

void CAN_tx_statusword(tMCStatusword statusword)
{
    CanFrame tx_frame;
    tx_frame.id      = ID_ECHO_BIT | (mNodeID << 5) | CAN_CMD_STATUSWORD_REPORT;
    tx_frame.data[0] = statusword.status.status_code;
    tx_frame.data[1] = statusword.errors.errors_code;
    tx_frame.dlc     = 2;
    if (!can_tx(&tx_frame)) {
        CAN_LOG("[CAN] drop statusword reply\n");
    }
}

void CAN_calib_report(int32_t step, uint8_t *data)
{
    CanFrame tx_frame;
    tx_frame.id  = ID_ECHO_BIT | (mNodeID << 5) | CAN_CMD_CALIB_REPORT;
    tx_frame.dlc = 0;
    tx_frame.dlc += int32_to_data(step, &tx_frame.data[tx_frame.dlc]);
    memcpy(&tx_frame.data[tx_frame.dlc], data, 4);
    tx_frame.dlc += 4;
    if (!can_tx(&tx_frame)) {
        CAN_LOG("[CAN] drop calib report step=%d\n", (int) step);
    }
}

void CAN_anticogging_report(int32_t step, int32_t value)
{
    CanFrame tx_frame;
    tx_frame.id  = ID_ECHO_BIT | (mNodeID << 5) | CAN_CMD_ANTICOGGING_REPORT;
    tx_frame.dlc = 0;
    tx_frame.dlc += int32_to_data(step, &tx_frame.data[tx_frame.dlc]);
    tx_frame.dlc += int32_to_data(value, &tx_frame.data[tx_frame.dlc]);
    if (!can_tx(&tx_frame)) {
        CAN_LOG("[CAN] drop anticogging report step=%d\n", (int) step);
    }
}

static void can_error_check(void)
{
    mCanBusErrNew = CAN_hw_status_bits();

    if (mCanBusErrOld != mCanBusErrNew) {
        mCanBusErrOld = mCanBusErrNew;
        CAN_LOG("[CAN] err state=%u rx_warn=%u tx_warn=%u busoff=%u\n",
                (unsigned) (mCanBusErrNew & 0x03U),
                (unsigned) !!(mCanBusErrNew & (1UL << 8)),
                (unsigned) !!(mCanBusErrNew & (1UL << 9)),
                (unsigned) !!(mCanBusErrNew & (1UL << 10)));

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
    } else if (can_cmd_is_critical(GET_CMD(tx_frame->id))) {
        CAN_LOG("[CAN] tx busy id=0x%03X cmd=%u(%s) dlc=%u\n",
                (unsigned) (tx_frame->id & 0x7FFU),
                (unsigned) GET_CMD(tx_frame->id),
                can_cmd_name(GET_CMD(tx_frame->id)),
                (unsigned) tx_frame->dlc);
    }

    return sent;
}

static bool can_is_idle_and_disarmed(void)
{
    return ((MCT_get_state() == IDLE) && (!Foc.is_armed));
}

static const char *can_cmd_name(uint8_t cmd)
{
    switch (cmd) {
    case CAN_CMD_SET_OP_MODE:
        return "SET_OP_MODE";
    case CAN_CMD_MOTOR_ENABLE:
        return "MOTOR_ENABLE";
    case CAN_CMD_MOTOR_DISABLE:
        return "MOTOR_DISABLE";
    case CAN_CMD_SET_TORQUE:
        return "SET_TORQUE";
    case CAN_CMD_SET_VELOCITY:
        return "SET_VELOCITY";
    case CAN_CMD_SET_POSITION:
        return "SET_POSITION";
    case CAN_CMD_SYNC:
        return "SYNC";
    case CAN_CMD_CALIB_START:
        return "CALIB_START";
    case CAN_CMD_CALIB_REPORT:
        return "CALIB_REPORT";
    case CAN_CMD_CALIB_ABORT:
        return "CALIB_ABORT";
    case CAN_CMD_ANTICOGGING_START:
        return "ANTICOGGING_START";
    case CAN_CMD_ANTICOGGING_REPORT:
        return "ANTICOGGING_REPORT";
    case CAN_CMD_ANTICOGGING_ABORT:
        return "ANTICOGGING_ABORT";
    case CAN_CMD_SET_HOME:
        return "SET_HOME";
    case CAN_CMD_ERROR_RESET:
        return "ERROR_RESET";
    case CAN_CMD_GET_STATUSWORD:
        return "GET_STATUSWORD";
    case CAN_CMD_STATUSWORD_REPORT:
        return "STATUSWORD_REPORT";
    case CAN_CMD_GET_VALUE_1:
        return "GET_VALUE_1";
    case CAN_CMD_GET_VALUE_2:
        return "GET_VALUE_2";
    case CAN_CMD_HEARTBEAT:
        return "HEARTBEAT";
    case CAN_CMD_SET_CONFIG:
        return "SET_CONFIG";
    case CAN_CMD_GET_CONFIG:
        return "GET_CONFIG";
    case CAN_CMD_SAVE_ALL_CONFIG:
        return "SAVE_ALL_CONFIG";
    case CAN_CMD_RESET_ALL_CONFIG:
        return "RESET_ALL_CONFIG";
    case CAN_CMD_GET_FW_VERSION:
        return "GET_FW_VERSION";
    case CAN_CMD_DFU_START:
        return "DFU_START";
    case CAN_CMD_DFU_DATA:
        return "DFU_DATA";
    case CAN_CMD_DFU_END:
        return "DFU_END";
    default:
        return "UNKNOWN";
    }
}

static bool can_cmd_is_critical(uint8_t cmd)
{
    switch (cmd) {
    case CAN_CMD_SET_OP_MODE:
    case CAN_CMD_MOTOR_ENABLE:
    case CAN_CMD_MOTOR_DISABLE:
    case CAN_CMD_CALIB_START:
    case CAN_CMD_CALIB_ABORT:
    case CAN_CMD_ANTICOGGING_START:
    case CAN_CMD_ANTICOGGING_ABORT:
    case CAN_CMD_SET_HOME:
    case CAN_CMD_ERROR_RESET:
    case CAN_CMD_GET_STATUSWORD:
    case CAN_CMD_GET_FW_VERSION:
    case CAN_CMD_SET_CONFIG:
    case CAN_CMD_GET_CONFIG:
    case CAN_CMD_SAVE_ALL_CONFIG:
    case CAN_CMD_RESET_ALL_CONFIG:
    case CAN_CMD_DFU_START:
    case CAN_CMD_DFU_END:
        return true;
    default:
        return false;
    }
}

static void can_log_frame(const char *tag, const CanFrame *frame)
{
    uint8_t cmd = GET_CMD(frame->id);
    CAN_LOG("[CAN] %s id=0x%03X node=%u cmd=%u(%s) dlc=%u echo=%u\n",
            tag,
            (unsigned) (frame->id & 0x7FFU),
            (unsigned) GET_NODE_ID(frame->id),
            (unsigned) cmd,
            can_cmd_name(cmd),
            (unsigned) frame->dlc,
            (unsigned) !!IS_ECHO(frame->id));
}

static void fill_value(uint8_t idx, uint8_t *data)
{
    switch (idx) {
    case 0:
        if (UsrConfig.invert_motor_dir) {
            float_to_data(-Foc.i_q_filt, data);
        } else {
            float_to_data(+Foc.i_q_filt, data);
        }
        break;

    case 1:
        if (UsrConfig.invert_motor_dir) {
            float_to_data(-Encoder.vel, data);
        } else {
            float_to_data(+Encoder.vel, data);
        }
        break;

    case 2:
        if (UsrConfig.invert_motor_dir) {
            float_to_data(-Encoder.pos, data);
        } else {
            float_to_data(+Encoder.pos, data);
        }
        break;

    case 3:
        float_to_data(Foc.v_bus_filt, data);
        break;

    case 4:
        float_to_data(Foc.i_bus_filt, data);
        break;

    case 5:
        float_to_data(Foc.power_filt, data);
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

static void parse_frame(CanFrame *frame)
{
    int     ret;
    bool    echo    = false;
    uint8_t node_id = GET_NODE_ID(frame->id);
    uint8_t cmd     = GET_CMD(frame->id);

    // Dir check
    if (IS_ECHO(frame->id)) {
        return;
    }

    // set echo bit
    frame->id |= ID_ECHO_BIT;

    // Node id check
    if (node_id != mNodeID && node_id != 0) {
        return;
    }

    CAN_reset_rx_timeout();

    if (can_cmd_is_critical(cmd)) {
        can_log_frame("rx", frame);
    }

    switch (cmd) {
    case CAN_CMD_SET_OP_MODE:
        ret            = (frame->dlc == 1U) ? CONTROLLER_set_op_mode((tControlMode) frame->data[0]) : -1;
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_MOTOR_ENABLE:
        ret            = MCT_set_state(RUN);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_MOTOR_DISABLE:
        ret            = MCT_set_state(IDLE);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_SET_TORQUE:
        if (frame->dlc == 4) {
            if (UsrConfig.invert_motor_dir) {
                Controller.input_current_buffer = -data_to_float(&frame->data[0]);
            } else {
                Controller.input_current_buffer = +data_to_float(&frame->data[0]);
            }
            if (!UsrConfig.sync_target_enable) {
                CONTROLLER_sync_callback();
            }
        }
        break;

    case CAN_CMD_SET_VELOCITY:
        if (frame->dlc == 4) {
            if (UsrConfig.invert_motor_dir) {
                Controller.input_velocity_buffer = -data_to_float(&frame->data[0]);
            } else {
                Controller.input_velocity_buffer = +data_to_float(&frame->data[0]);
            }
            if (!UsrConfig.sync_target_enable) {
                CONTROLLER_sync_callback();
            }
        }
        break;

    case CAN_CMD_SET_POSITION:
        if (frame->dlc == 4) {
            if (UsrConfig.invert_motor_dir) {
                Controller.input_position_buffer = -data_to_float(&frame->data[0]);
            } else {
                Controller.input_position_buffer = +data_to_float(&frame->data[0]);
            }
            if (!UsrConfig.sync_target_enable) {
                CONTROLLER_sync_callback();
            }
        }
        break;

    case CAN_CMD_SYNC:
        if (UsrConfig.sync_target_enable) {
            CONTROLLER_sync_callback();
        }
        break;

    case CAN_CMD_CALIB_START:
        ret            = MCT_set_state(CALIBRATION);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_CALIB_ABORT:
        ret            = MCT_set_state(IDLE);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_ANTICOGGING_START:
        ret            = MCT_set_state(ANTICOGGING);
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
        ret            = CONTROLLER_set_home();
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_ERROR_RESET:
        ret            = MCT_reset_error();
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_GET_STATUSWORD:
        frame->data[0] = StatuswordNew.status.status_code;
        frame->data[1] = StatuswordNew.errors.errors_code;
        frame->dlc     = 2;
        echo           = true;
        break;

    case CAN_CMD_GET_VALUE_1:
        fill_value(frame->data[0], frame->data);
        frame->dlc = 4;
        echo       = true;
        break;

    case CAN_CMD_GET_VALUE_2:
        fill_value(frame->data[0], frame->data);
        frame->dlc = 4;
        echo       = true;
        break;

    case CAN_CMD_HEARTBEAT:
        break;

    case CAN_CMD_SET_CONFIG:
        if (frame->dlc == 8U) {
            config_callback(frame->data, true);
        } else {
            config_reject(frame->data);
        }
        frame->dlc = 8;
        echo       = true;
        break;

    case CAN_CMD_GET_CONFIG:
        if (frame->dlc == 8U) {
            config_callback(frame->data, false);
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
            ret += USR_CONFIG_save_cogging_map();
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
            USR_CONFIG_set_default_cogging_map();
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
            if (!can_tx(frame)) {
                CAN_LOG("[CAN] drop DFU_END ack\n");
            }
            delay_ms(100);
            watch_dog_feed();
            DFU_jump_bootloader();
        }
        break;

    default:
        break;
    }

    if (echo) {
        if (!can_tx(frame)) {
            if (can_cmd_is_critical(cmd)) {
                CAN_LOG("[CAN] drop ack cmd=%u(%s)\n", (unsigned) cmd, can_cmd_name(cmd));
            }
        } else if (can_cmd_is_critical(cmd)) {
            can_log_frame("tx", frame);
        }
    }
}

static void config_callback(uint8_t *data, bool isSet)
{
    int32_t idx           = data_to_int32(data) - 1;
    int32_t config_number = sizeof(tUsrConfig) / 4 - 132;

    if ((idx < 0) || (idx >= config_number)) {
        config_reject(data);
        return;
    }

    uint32_t *pConfig = &(((uint32_t *) (&UsrConfig))[idx]);

    if (isSet) {
        if (!config_value_is_valid(idx, &data[4])) {
            config_reject(data);
            return;
        }

        memcpy(pConfig, &data[4], 4);
        FOC_update_current_ctrl_gain(UsrConfig.current_ctrl_bw);
        CONTROLLER_update_input_pos_filter_gain(UsrConfig.position_filter_bw);
    } else {
        memcpy(&data[4], pConfig, 4);
    }
}

static void config_reject(uint8_t *data)
{
    int32_to_data(-1, &data[0]);
    int32_to_data(0, &data[4]);
}

static bool config_value_is_valid(int32_t idx, uint8_t *value)
{
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
        return (float_in_range(voltage, 0.0f, 50.0f) && (voltage < UsrConfig.protect_over_voltage));
    }
    case 25: {
        float voltage = data_to_float(value);
        return (float_in_range(voltage, 0.0f, 50.0f) && (voltage > UsrConfig.protect_under_voltage));
    }
    case 26:
        return float_in_range(data_to_float(value), 0.0f, 10.0f);
    case 27:
    case 28:
        return int32_in_range(data_to_int32(value), 0, 150);
    case 29:
        return int32_in_range(data_to_int32(value), 1, 31);
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
