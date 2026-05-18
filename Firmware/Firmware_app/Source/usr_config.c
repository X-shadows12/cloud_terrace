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

#include "flash_hw.h"
#include "usr_config.h"
#include "controller.h"
#include "heap.h"
#include "util.h"
#include <string.h>

tUsrConfig   UsrConfig;
tCoggingMap *pCoggingMap = NULL;

void USR_CONFIG_set_default_config(void)
{
    // Motor
    UsrConfig.invert_motor_dir       = 0;
    UsrConfig.motor_pole_pairs       = 4;
    UsrConfig.motor_phase_resistance = 0.28f;
    UsrConfig.motor_phase_inductance = 110e-6f;
    UsrConfig.current_limit          = 5;
    UsrConfig.velocity_limit         = 60;

    // Encoder
    UsrConfig.calib_current = 0.3f;
    UsrConfig.calib_voltage = 3.0f;

    // Controller
    UsrConfig.pos_p_gain             = 80.0f;
    UsrConfig.vel_p_gain             = 0.3f;
    UsrConfig.vel_i_gain             = 5.0f;
    UsrConfig.current_ff_gain        = 0.001f;
    UsrConfig.current_ctrl_bw        = 1000;
    UsrConfig.default_op_mode        = CONTROL_MODE_POSITION_PROFILE;
    UsrConfig.anticogging_enable     = 1;
    UsrConfig.sync_target_enable     = 0;
    UsrConfig.target_velcity_window  = 0.5f;
    UsrConfig.target_position_window = 0.01f;
    UsrConfig.current_ramp_rate      = 0.5f;
    UsrConfig.velocity_ramp_rate     = 50;
    UsrConfig.position_filter_bw     = 10;
    UsrConfig.profile_velocity       = 50;
    UsrConfig.profile_accel          = 50;
    UsrConfig.profile_decel          = 50;

    // Protect
    UsrConfig.protect_under_voltage = 6;
    UsrConfig.protect_over_voltage  = 30;
    UsrConfig.protect_over_current  = 8;
    UsrConfig.protect_drv_over_tmp  = 80;
    UsrConfig.protect_ntc_over_tmp  = 80;

    // CAN
    UsrConfig.node_id               = 1;
    UsrConfig.can_baudrate          = CAN_BAUDRATE_500K;
    UsrConfig.heartbeat_consumer_ms = 0;
    UsrConfig.heartbeat_producer_ms = 0;

    // Encoder
    UsrConfig.calib_valid = 0;
}

int USR_CONFIG_erease_config(void)
{
    return FLASH_HW_erase_region(FLASH_HW_USR_CONFIG_ADDR, FLASH_HW_USR_CONFIG_MAX_SIZE);
}

int USR_CONFIG_read_config(void)
{
    int state = 0;

    memcpy(&UsrConfig, (uint8_t *) FLASH_HW_USR_CONFIG_ADDR, sizeof(tUsrConfig));

    uint32_t crc;
    crc = crc32((uint8_t *) &UsrConfig, sizeof(tUsrConfig) - 4);
    if (crc != UsrConfig.crc) {
        state = -1;
    }

    return state;
}

int USR_CONFIG_save_config(void)
{
    // Erase
    if (USR_CONFIG_erease_config()) {
        return -1;
    }

    UsrConfig.crc   = crc32((uint8_t *) &UsrConfig, sizeof(tUsrConfig) - 4);
    if (FLASH_HW_program_words(FLASH_HW_USR_CONFIG_ADDR, (const uint32_t *) &UsrConfig, sizeof(tUsrConfig) / 4U)) {
        return -2;
    }

    return 0;
}

void USR_CONFIG_set_default_cogging_map(void)
{
    if (pCoggingMap == NULL) {
        pCoggingMap = HEAP_malloc(sizeof(tCoggingMap));
    }

    for (int i = 0; i < COGGING_MAP_NUM; i++) {
        pCoggingMap->map[i] = 0;
    }
}

int USR_CONFIG_erease_cogging_map(void)
{
    return FLASH_HW_erase_region(FLASH_HW_COGGING_MAP_ADDR, FLASH_HW_COGGING_MAP_MAX_SIZE);
}

int USR_CONFIG_read_cogging_map(void)
{
    int state = 0;

    if (pCoggingMap == NULL) {
        pCoggingMap = HEAP_malloc(sizeof(tCoggingMap));
    }

    memcpy(pCoggingMap, (uint8_t *) FLASH_HW_COGGING_MAP_ADDR, sizeof(tCoggingMap));

    uint32_t crc;
    crc = crc32((uint8_t *) pCoggingMap, sizeof(tCoggingMap) - 4);
    if (crc != pCoggingMap->crc) {
        state = -1;
    }

    return state;
}

int USR_CONFIG_save_cogging_map(void)
{
    // Erase
    if (USR_CONFIG_erease_cogging_map()) {
        return -1;
    }

    pCoggingMap->crc = crc32((uint8_t *) pCoggingMap, sizeof(tCoggingMap) - 4);
    if (FLASH_HW_program_words(FLASH_HW_COGGING_MAP_ADDR, (const uint32_t *) pCoggingMap, sizeof(tCoggingMap) / 4U)) {
        return -2;
    }

    return 0;
}
