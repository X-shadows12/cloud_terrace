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

#include "can_hw.h"
#include "board_port.h"
#include "gd32h7xx.h"
#include "usr_config.h"
#include <string.h>

static can_mailbox_descriptor_struct mCanRxMessage;
static uint32_t mCanRxData[2];
static FlagStatus mCanTxPrimed = RESET;

static void can_baudrate_config(int baudrate, can_parameter_struct *can_parameter);
static void can_rx_mailbox_arm(void);

void CAN_hw_init(int baudrate)
{
    can_parameter_struct can_parameter;

    can_interrupt_disable(BOARD_CAN, CAN_INT_MB0);
    can_deinit(BOARD_CAN);

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

    can_init(BOARD_CAN, &can_parameter);
    can_operation_mode_enter(BOARD_CAN, CAN_NORMAL_MODE);

    can_rx_mailbox_arm();
    can_interrupt_enable(BOARD_CAN, CAN_INT_MB0);
    mCanTxPrimed = RESET;
}

uint32_t CAN_hw_status_bits(void)
{
    uint32_t err = (uint32_t) can_error_state_get(BOARD_CAN);

    if (SET == can_flag_get(BOARD_CAN, CAN_FLAG_RX_WARNING)) {
        err |= (1UL << 8);
    }
    if (SET == can_flag_get(BOARD_CAN, CAN_FLAG_TX_WARNING)) {
        err |= (1UL << 9);
    }
    if (SET == can_flag_get(BOARD_CAN, CAN_FLAG_BUSOFF)) {
        err |= (1UL << 10);
    }

    return err;
}

bool CAN_hw_send(const CanFrame *tx_frame)
{
    can_mailbox_descriptor_struct tx_message;
    uint32_t                    tx_data[2];
    uint8_t dlc = tx_frame->dlc > 8U ? 8U : tx_frame->dlc;
    bool busy = false;
    bool sent = false;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if ((SET == mCanTxPrimed) && (RESET == can_flag_get(BOARD_CAN, CAN_FLAG_MB1))) {
        busy = true;
    } else {
        if (SET == mCanTxPrimed) {
            can_flag_clear(BOARD_CAN, CAN_FLAG_MB1);
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
        can_mailbox_config(BOARD_CAN, BOARD_CAN_TX_MAILBOX, &tx_message);

        mCanTxPrimed = SET;
        sent = true;
    }
    __set_PRIMASK(primask);

    (void) busy;
    return sent;
}

bool CAN_hw_receive(CanFrame *rx_frame)
{
    uint8_t dlc;

    if (RESET == can_interrupt_flag_get(BOARD_CAN, CAN_INT_FLAG_MB0)) {
        return false;
    }

    if (SUCCESS != can_mailbox_receive_data_read(BOARD_CAN, BOARD_CAN_RX_MAILBOX, &mCanRxMessage)) {
        can_interrupt_flag_clear(BOARD_CAN, CAN_INT_FLAG_MB0);
        can_rx_mailbox_arm();
        return false;
    }

    dlc = mCanRxMessage.data_bytes > 8U ? 8U : (uint8_t) mCanRxMessage.data_bytes;
    rx_frame->id  = mCanRxMessage.id & 0x7FFU;
    rx_frame->dlc = dlc;
    memcpy(rx_frame->data, (uint8_t *) mCanRxData, dlc);

    can_rx_mailbox_arm();

    return true;
}

void CAN_hw_abort_tx(void)
{
    can_mailbox_transmit_abort(BOARD_CAN, BOARD_CAN_TX_MAILBOX);
    mCanTxPrimed = RESET;
    can_rx_mailbox_arm();
}

void CAN_hw_rearm_rx(void)
{
    can_rx_mailbox_arm();
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
    can_mailbox_config(BOARD_CAN, BOARD_CAN_RX_MAILBOX, &mCanRxMessage);
}
