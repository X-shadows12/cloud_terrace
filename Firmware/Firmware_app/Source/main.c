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

#include "main.h"
#include "board_init.h"
#include "anticogging.h"
#include "can.h"
#include "can_hw.h"
#include "controller.h"
#include "encoder.h"
#include "foc.h"
#include "mc_task.h"
#include "pwm_curr.h"
#include "rtt_scope.h"
#include "runtime.h"
#include "usr_config.h"

int main(void)
{
    BOARD_interrupts_disable();

    BOARD_init_debug_memory();
    RTT_init();
    BOARD_enable_cache();
    BOARD_init_peripherals();
    BOARD_init_watchdog();

    if (USR_CONFIG_read_config()) {
        USR_CONFIG_set_default_config();
    }

    if (0 != USR_CONFIG_axis_read_cogging_map(MOTOR_HW_AXIS_LEFT)) {
        USR_CONFIG_axis_set_default_cogging_map(MOTOR_HW_AXIS_LEFT);
    }
    if (0 != USR_CONFIG_axis_read_cogging_map(MOTOR_HW_AXIS_RIGHT)) {
        USR_CONFIG_axis_set_default_cogging_map(MOTOR_HW_AXIS_RIGHT);
    }
    AnticoggingValid = *USR_CONFIG_anticogging_valid(CONTROLLER_active_axis());

    CAN_set_node_id(UsrConfig.node_id);
    CAN_hw_init(UsrConfig.can_baudrate);

    MCT_init();
    FOC_init();
    PWMC_init();
    ENCODER_init();
    CONTROLLER_init();

    BOARD_enable_watchdog();
    BOARD_interrupts_enable();

    /* wait voltage stable */
    for (uint8_t i = 0, j = 0; i < 250; i++) {
        if (Foc.v_bus_filt > 20) {
            if (++j > 20) {
                break;
            }
        }
        delay_ms(2);
    }

    if (PWMC_CurrentReadingAxisPolarization(MOTOR_HW_AXIS_LEFT) != 0) {
        MCT_axis_statusword(MOTOR_HW_AXIS_LEFT)->errors.selftest = 1;
    }

    if (PWMC_CurrentReadingAxisPolarization(MOTOR_HW_AXIS_RIGHT) != 0) {
        MCT_axis_statusword(MOTOR_HW_AXIS_RIGHT)->errors.selftest = 1;
    }

    MCT_set_state(IDLE);

    while (1) {
        MCT_low_priority_task();
    }
}

void Error_Handler(void)
{
    BOARD_emergency_shutdown();

    while (1) {
    }
}
