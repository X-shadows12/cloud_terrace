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

#ifndef __CAN_HW_H__
#define __CAN_HW_H__

#include "can.h"

void     CAN_hw_init(int baudrate);
uint32_t CAN_hw_status_bits(void);
bool     CAN_hw_send(const CanFrame *tx_frame);
bool     CAN_hw_receive(CanFrame *rx_frame);
void     CAN_hw_abort_tx(void);
void     CAN_hw_rearm_rx(void);

#endif
