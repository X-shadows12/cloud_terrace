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
#include "controller.h"
#include "dfu.h"
#include "encoder.h"
#include "foc.h"
#include "pwm_curr.h"
#include "usr_config.h"
#include "util.h"
#include <string.h>

#define CAN_DIAG_ENABLE 1

#if CAN_DIAG_ENABLE
#include "SEGGER_RTT.h"
#define CAN_LOG(format, ...) SEGGER_RTT_printf(0, format, ##__VA_ARGS__)
#else
#define CAN_LOG(format, ...)
#endif

static uint8_t  mNodeID;
static uint32_t mRxTick       = 0;
static uint32_t mTxTick       = 0;
static uint32_t mCanBusErrNew = 0;
static uint32_t mCanBusErrOld = 0;
static FlagStatus mCanTxPrimed = RESET;

static can_mailbox_descriptor_struct mCanRxMessage;
static uint32_t mCanRxData[2];

static void can_error_check(void);
static void can_baudrate_config(int baudrate, can_parameter_struct *can_parameter);
static void can_rx_mailbox_arm(void);
static bool can_tx(CanFrame *tx_frame);
static bool can_rx(CanFrame *rx_frame);
static const char *can_cmd_name(uint8_t cmd);
static bool can_cmd_is_critical(uint8_t cmd);
static void can_log_frame(const char *tag, const CanFrame *frame);

static void fill_value(uint8_t idx, uint8_t *data);
static void parse_frame(CanFrame *frame);
static void config_callback(uint8_t *data, bool isSet);

void CAN_set_node_id(uint8_t nodeID)
{
    mNodeID = nodeID;
}

void CAN_hw_init(int baudrate)
{
    can_parameter_struct can_parameter;

    can_interrupt_disable(CTM_H759_CAN, CAN_INT_MB0);
    can_deinit(CTM_H759_CAN);

    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
    can_parameter.internal_counter_source          = CAN_TIMER_SOURCE_BIT_CLOCK;
    can_parameter.self_reception                   = DISABLE;
    can_parameter.mb_tx_order                      = CAN_TX_HIGH_PRIORITY_MB_FIRST;
    can_parameter.mb_tx_abort_enable               = ENABLE;
    can_parameter.local_priority_enable            = DISABLE;
    can_parameter.mb_rx_ide_rtr_type               = CAN_IDE_RTR_FILTERED;
    can_parameter.mb_remote_frame                  = CAN_STORE_REMOTE_REQUEST_FRAME;
    can_parameter.rx_private_filter_queue_enable   = DISABLE;
    can_parameter.edge_filter_enable               = DISABLE;
    can_parameter.protocol_exception_enable        = DISABLE;
    can_parameter.rx_filter_order                  = CAN_RX_FILTER_ORDER_MAILBOX_FIRST;
    can_parameter.memory_size                      = CAN_MEMSIZE_32_UNIT;
    /* Accept all CAN IDs; protocol filtering is handled in parse_frame(). */
    can_parameter.mb_public_filter                 = 0x00000000U;
    can_baudrate_config(baudrate, &can_parameter);

    can_init(CTM_H759_CAN, &can_parameter);
    can_operation_mode_enter(CTM_H759_CAN, CAN_NORMAL_MODE);

    can_rx_mailbox_arm();
    can_interrupt_enable(CTM_H759_CAN, CAN_INT_MB0);
    mCanTxPrimed = RESET;
    CAN_LOG("[CAN] init baud=%d node=%u\n", baudrate, (unsigned)mNodeID);
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
    while (can_rx(&rxframe)) {
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
    mCanBusErrNew = (uint32_t) can_error_state_get(CTM_H759_CAN);
    if (SET == can_flag_get(CTM_H759_CAN, CAN_FLAG_RX_WARNING)) {
        mCanBusErrNew |= (1UL << 8);
    }
    if (SET == can_flag_get(CTM_H759_CAN, CAN_FLAG_TX_WARNING)) {
        mCanBusErrNew |= (1UL << 9);
    }
    if (SET == can_flag_get(CTM_H759_CAN, CAN_FLAG_BUSOFF)) {
        mCanBusErrNew |= (1UL << 10);
    }

    if (mCanBusErrOld != mCanBusErrNew) {
        mCanBusErrOld = mCanBusErrNew;
        CAN_LOG("[CAN] err state=%u rx_warn=%u tx_warn=%u busoff=%u\n",
                (unsigned) (mCanBusErrNew & 0x03U),
                (unsigned) !!(mCanBusErrNew & (1UL << 8)),
                (unsigned) !!(mCanBusErrNew & (1UL << 9)),
                (unsigned) !!(mCanBusErrNew & (1UL << 10)));

        if ((CAN_ERROR_STATE_PASSIVE == (can_error_state_enum) (mCanBusErrNew & 0x03U))
            || (CAN_ERROR_STATE_BUS_OFF == (can_error_state_enum) (mCanBusErrNew & 0x03U))) {
            can_mailbox_transmit_abort(CTM_H759_CAN, CTM_H759_CAN_TX_MAILBOX);
            mCanTxPrimed = RESET;
            can_rx_mailbox_arm();
        }
    }
}

static void can_baudrate_config(int baudrate, can_parameter_struct *can_parameter)
{
    can_parameter->resync_jump_width = 1U;
    can_parameter->prop_time_segment = 2U;
    can_parameter->time_segment_1    = 9U;
    can_parameter->time_segment_2    = 3U;

    switch ((tCanBaudrate) baudrate) {
    case CAN_BAUDRATE_250K:
        can_parameter->prescaler = 80U;
        break;

    case CAN_BAUDRATE_500K:
        can_parameter->prescaler = 40U;
        break;

    case CAN_BAUDRATE_800K:
        can_parameter->prescaler = 25U;
        break;

    case CAN_BAUDRATE_1000K:
        can_parameter->prescaler = 20U;
        break;

    default:
        can_parameter->prescaler = 40U;
        break;
    }
}

static void can_rx_mailbox_arm(void)
{
    memset(mCanRxData, 0, sizeof(mCanRxData));
    can_struct_para_init(CAN_MDSC_STRUCT, &mCanRxMessage);
    mCanRxMessage.rtr        = 0U;
    mCanRxMessage.ide        = 0U;
    mCanRxMessage.code       = CAN_MB_RX_STATUS_EMPTY;
    mCanRxMessage.id         = 0U;
    mCanRxMessage.data       = mCanRxData;
    mCanRxMessage.data_bytes = 8U;
    can_mailbox_config(CTM_H759_CAN, CTM_H759_CAN_RX_MAILBOX, &mCanRxMessage);
}

static bool can_tx(CanFrame *tx_frame)
{
    can_mailbox_descriptor_struct tx_message;
    uint32_t                    tx_data[2];
    uint8_t dlc = tx_frame->dlc > 8U ? 8U : tx_frame->dlc;
    bool busy = false;
    bool sent = false;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if ((SET == mCanTxPrimed) && (RESET == can_flag_get(CTM_H759_CAN, CAN_FLAG_MB1))) {
        busy = true;
    } else {
        if (SET == mCanTxPrimed) {
            can_flag_clear(CTM_H759_CAN, CAN_FLAG_MB1);
        }

        memset(tx_data, 0, sizeof(tx_data));
        memcpy((uint8_t *) tx_data, tx_frame->data, dlc);

        can_struct_para_init(CAN_MDSC_STRUCT, &tx_message);
        tx_message.rtr        = 0U;
        tx_message.ide        = 0U;
        tx_message.code       = CAN_MB_TX_STATUS_DATA;
        tx_message.brs        = 0U;
        tx_message.fdf        = 0U;
        tx_message.prio       = 0U;
        tx_message.data_bytes = dlc;
        tx_message.data       = tx_data;
        tx_message.id         = tx_frame->id & 0x7FFU;
        can_mailbox_config(CTM_H759_CAN, CTM_H759_CAN_TX_MAILBOX, &tx_message);

        mCanTxPrimed = SET;
        CAN_reset_tx_timeout();
        sent = true;
    }
    __set_PRIMASK(primask);

    if (busy && can_cmd_is_critical(GET_CMD(tx_frame->id))) {
        CAN_LOG("[CAN] tx busy id=0x%03X cmd=%u(%s) dlc=%u\n",
                (unsigned) (tx_frame->id & 0x7FFU),
                (unsigned) GET_CMD(tx_frame->id),
                can_cmd_name(GET_CMD(tx_frame->id)),
                (unsigned) dlc);
    }

    return sent;
}

static bool can_rx(CanFrame *rx_frame)
{
    uint8_t dlc;

    if (RESET == can_interrupt_flag_get(CTM_H759_CAN, CAN_INT_FLAG_MB0)) {
        return false;
    }

    if (SUCCESS != can_mailbox_receive_data_read(CTM_H759_CAN, CTM_H759_CAN_RX_MAILBOX, &mCanRxMessage)) {
        can_interrupt_flag_clear(CTM_H759_CAN, CAN_INT_FLAG_MB0);
        can_rx_mailbox_arm();
        CAN_LOG("[CAN] rx read failed\n");
        return false;
    }

    dlc = mCanRxMessage.data_bytes > 8U ? 8U : (uint8_t) mCanRxMessage.data_bytes;
    rx_frame->id  = mCanRxMessage.id & 0x7FFU;
    rx_frame->dlc = dlc;
    memcpy(rx_frame->data, (uint8_t *) mCanRxData, dlc);

    can_rx_mailbox_arm();

    return true;
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
        float_to_data((float) read_drv_temp(), data);
        break;

    case 7:
        float_to_data((float) read_ntc_temp(), data);
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
        ret            = CONTROLLER_set_op_mode((tControlMode) frame->data[0]);
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
        config_callback(frame->data, true);
        frame->dlc = 8;
        echo       = true;
        break;

    case CAN_CMD_GET_CONFIG:
        config_callback(frame->data, false);
        frame->dlc = 8;
        echo       = true;
        break;

    case CAN_CMD_SAVE_ALL_CONFIG:
        ret = 0;
        ret += USR_CONFIG_save_config();
        ret += USR_CONFIG_save_cogging_map();
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_RESET_ALL_CONFIG:
        if (MCT_get_state() == IDLE) {
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
        MCT_set_state(IDLE);
        watch_dog_feed();
        ret            = DFU_write_app_back_start();
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_DFU_DATA:
        ret            = DFU_write_app_back(&frame->data[0], frame->dlc);
        frame->data[0] = ret == 0 ? 0x00 : 0xEE;
        frame->dlc     = 1;
        echo           = true;
        break;

    case CAN_CMD_DFU_END:
        watch_dog_feed();
        ret            = DFU_check_app_back(data_to_uint32(&frame->data[0]), data_to_uint32(&frame->data[4]));
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

    if (idx >= config_number) {
        int32_to_data(-1, &data[0]);
        int32_to_data(0, &data[4]);
        return;
    }

    uint32_t *pConfig = &(((uint32_t *) (&UsrConfig))[idx]);

    if (isSet) {
        memcpy(pConfig, &data[4], 4);
        FOC_update_current_ctrl_gain(UsrConfig.current_ctrl_bw);
        CONTROLLER_update_input_pos_filter_gain(UsrConfig.position_filter_bw);
    } else {
        memcpy(&data[4], pConfig, 4);
    }
}
