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
tAxisConfig  UsrConfigAxes[MOTOR_HW_AXIS_COUNT];
bool         AnticoggingValidAxes[MOTOR_HW_AXIS_COUNT];
tCoggingMap *pCoggingMapAxes[MOTOR_HW_AXIS_COUNT];
tCoggingMap *pCoggingMap = NULL;

static uint8_t usr_config_axis_index(motor_hw_axis_t axis);
static uint32_t usr_config_cogging_map_slot_size(void);
static uint32_t usr_config_cogging_map_addr(motor_hw_axis_t axis);
static void usr_config_set_default_axis(tAxisConfig *axis_config);
static void usr_config_copy_storage_axis0_to_axis(tAxisConfig *axis_config);
static void usr_config_copy_axis_to_storage_axis0(const tAxisConfig *axis_config);

tAxisConfig *USR_CONFIG_axis(motor_hw_axis_t axis)
{
    return &UsrConfigAxes[usr_config_axis_index(axis)];
}

tCoggingMap *USR_CONFIG_cogging_map(motor_hw_axis_t axis)
{
    uint8_t idx = usr_config_axis_index(axis);

    if (pCoggingMapAxes[idx] == NULL) {
        pCoggingMapAxes[idx] = HEAP_malloc(sizeof(tCoggingMap));
    }
    if (axis == CONTROLLER_active_axis()) {
        pCoggingMap = pCoggingMapAxes[idx];
    }

    return pCoggingMapAxes[idx];
}

bool *USR_CONFIG_anticogging_valid(motor_hw_axis_t axis)
{
    return &AnticoggingValidAxes[usr_config_axis_index(axis)];
}

void USR_CONFIG_sync_axes_from_storage(void)
{
    usr_config_copy_storage_axis0_to_axis(&UsrConfigAxes[0]);
    UsrConfigAxes[1] = UsrConfig.axis_1;
}

void USR_CONFIG_sync_storage_from_axes(void)
{
    usr_config_copy_axis_to_storage_axis0(&UsrConfigAxes[0]);
    UsrConfig.axis_1 = UsrConfigAxes[1];
}

void USR_CONFIG_set_default_config(void)
{
    usr_config_set_default_axis(&UsrConfigAxes[0]);
    usr_config_set_default_axis(&UsrConfigAxes[1]);
    USR_CONFIG_sync_storage_from_axes();

    // CAN
    UsrConfig.node_id               = 1;
    UsrConfig.can_baudrate          = CAN_BAUDRATE_500K;
    UsrConfig.heartbeat_consumer_ms = 0;
    UsrConfig.heartbeat_producer_ms = 0;
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
    } else {
        USR_CONFIG_sync_axes_from_storage();
    }

    return state;
}

int USR_CONFIG_save_config(void)
{
    // Erase
    if (USR_CONFIG_erease_config()) {
        return -1;
    }

    USR_CONFIG_sync_storage_from_axes();
    UsrConfig.crc   = crc32((uint8_t *) &UsrConfig, sizeof(tUsrConfig) - 4);
    if (FLASH_HW_program_words(FLASH_HW_USR_CONFIG_ADDR, (const uint32_t *) &UsrConfig, sizeof(tUsrConfig) / 4U)) {
        return -2;
    }

    return 0;
}

static uint8_t usr_config_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}

static uint32_t usr_config_cogging_map_addr(motor_hw_axis_t axis)
{
    return FLASH_HW_COGGING_MAP_ADDR + (usr_config_axis_index(axis) * usr_config_cogging_map_slot_size());
}

static uint32_t usr_config_cogging_map_slot_size(void)
{
    uint32_t map_size = (uint32_t) sizeof(tCoggingMap);

    return ((map_size + FLASH_HW_PAGE_SIZE - 1U) / FLASH_HW_PAGE_SIZE) * FLASH_HW_PAGE_SIZE;
}

static void usr_config_set_default_axis(tAxisConfig *axis_config)
{
    // Motor
    axis_config->invert_motor_dir       = 0;
    axis_config->motor_pole_pairs       = 4;
    axis_config->motor_phase_resistance = 0.28f;
    axis_config->motor_phase_inductance = 110e-6f;
    axis_config->current_limit          = 5;
    axis_config->velocity_limit         = 60;

    // Calibration
    axis_config->calib_current = 0.3f;
    axis_config->calib_voltage = 3.0f;

    // Controller
    axis_config->pos_p_gain             = 80.0f;
    axis_config->vel_p_gain             = 0.3f;
    axis_config->vel_i_gain             = 5.0f;
    axis_config->current_ff_gain        = 0.001f;
    axis_config->current_ctrl_bw        = 1000;
    axis_config->default_op_mode        = CONTROL_MODE_POSITION_PROFILE;
    axis_config->anticogging_enable     = 1;
    axis_config->sync_target_enable     = 0;
    axis_config->target_velcity_window  = 0.5f;
    axis_config->target_position_window = 0.01f;
    axis_config->current_ramp_rate      = 0.5f;
    axis_config->velocity_ramp_rate     = 50;
    axis_config->position_filter_bw     = 10;
    axis_config->profile_velocity       = 50;
    axis_config->profile_accel          = 50;
    axis_config->profile_decel          = 50;

    // Protect
    axis_config->protect_under_voltage = 6;
    axis_config->protect_over_voltage  = 30;
    axis_config->protect_over_current  = 8;
    axis_config->protect_drv_over_tmp  = 80;
    axis_config->protect_ntc_over_tmp  = 80;

    // Encoder
    axis_config->calib_valid = 0;
    axis_config->encoder_dir = +1;
    axis_config->encoder_offset = 0;
    memset(axis_config->offset_lut, 0, sizeof(axis_config->offset_lut));
}

static void usr_config_copy_storage_axis0_to_axis(tAxisConfig *axis_config)
{
    axis_config->invert_motor_dir       = UsrConfig.invert_motor_dir;
    axis_config->motor_pole_pairs       = UsrConfig.motor_pole_pairs;
    axis_config->motor_phase_resistance = UsrConfig.motor_phase_resistance;
    axis_config->motor_phase_inductance = UsrConfig.motor_phase_inductance;
    axis_config->current_limit          = UsrConfig.current_limit;
    axis_config->velocity_limit         = UsrConfig.velocity_limit;
    axis_config->calib_current          = UsrConfig.calib_current;
    axis_config->calib_voltage          = UsrConfig.calib_voltage;
    axis_config->pos_p_gain             = UsrConfig.pos_p_gain;
    axis_config->vel_p_gain             = UsrConfig.vel_p_gain;
    axis_config->vel_i_gain             = UsrConfig.vel_i_gain;
    axis_config->current_ff_gain        = UsrConfig.current_ff_gain;
    axis_config->current_ctrl_bw        = UsrConfig.current_ctrl_bw;
    axis_config->default_op_mode        = UsrConfig.default_op_mode;
    axis_config->anticogging_enable     = UsrConfig.anticogging_enable;
    axis_config->sync_target_enable     = UsrConfig.sync_target_enable;
    axis_config->target_velcity_window  = UsrConfig.target_velcity_window;
    axis_config->target_position_window = UsrConfig.target_position_window;
    axis_config->current_ramp_rate      = UsrConfig.current_ramp_rate;
    axis_config->velocity_ramp_rate     = UsrConfig.velocity_ramp_rate;
    axis_config->position_filter_bw     = UsrConfig.position_filter_bw;
    axis_config->profile_velocity       = UsrConfig.profile_velocity;
    axis_config->profile_accel          = UsrConfig.profile_accel;
    axis_config->profile_decel          = UsrConfig.profile_decel;
    axis_config->protect_under_voltage  = UsrConfig.protect_under_voltage;
    axis_config->protect_over_voltage   = UsrConfig.protect_over_voltage;
    axis_config->protect_over_current   = UsrConfig.protect_over_current;
    axis_config->protect_drv_over_tmp   = UsrConfig.protect_drv_over_tmp;
    axis_config->protect_ntc_over_tmp   = UsrConfig.protect_ntc_over_tmp;
    axis_config->calib_valid            = UsrConfig.calib_valid;
    axis_config->encoder_dir            = UsrConfig.encoder_dir;
    axis_config->encoder_offset         = UsrConfig.encoder_offset;
    memcpy(axis_config->offset_lut, UsrConfig.offset_lut, sizeof(axis_config->offset_lut));
}

static void usr_config_copy_axis_to_storage_axis0(const tAxisConfig *axis_config)
{
    UsrConfig.invert_motor_dir       = axis_config->invert_motor_dir;
    UsrConfig.motor_pole_pairs       = axis_config->motor_pole_pairs;
    UsrConfig.motor_phase_resistance = axis_config->motor_phase_resistance;
    UsrConfig.motor_phase_inductance = axis_config->motor_phase_inductance;
    UsrConfig.current_limit          = axis_config->current_limit;
    UsrConfig.velocity_limit         = axis_config->velocity_limit;
    UsrConfig.calib_current          = axis_config->calib_current;
    UsrConfig.calib_voltage          = axis_config->calib_voltage;
    UsrConfig.pos_p_gain             = axis_config->pos_p_gain;
    UsrConfig.vel_p_gain             = axis_config->vel_p_gain;
    UsrConfig.vel_i_gain             = axis_config->vel_i_gain;
    UsrConfig.current_ff_gain        = axis_config->current_ff_gain;
    UsrConfig.current_ctrl_bw        = axis_config->current_ctrl_bw;
    UsrConfig.default_op_mode        = axis_config->default_op_mode;
    UsrConfig.anticogging_enable     = axis_config->anticogging_enable;
    UsrConfig.sync_target_enable     = axis_config->sync_target_enable;
    UsrConfig.target_velcity_window  = axis_config->target_velcity_window;
    UsrConfig.target_position_window = axis_config->target_position_window;
    UsrConfig.current_ramp_rate      = axis_config->current_ramp_rate;
    UsrConfig.velocity_ramp_rate     = axis_config->velocity_ramp_rate;
    UsrConfig.position_filter_bw     = axis_config->position_filter_bw;
    UsrConfig.profile_velocity       = axis_config->profile_velocity;
    UsrConfig.profile_accel          = axis_config->profile_accel;
    UsrConfig.profile_decel          = axis_config->profile_decel;
    UsrConfig.protect_under_voltage  = axis_config->protect_under_voltage;
    UsrConfig.protect_over_voltage   = axis_config->protect_over_voltage;
    UsrConfig.protect_over_current   = axis_config->protect_over_current;
    UsrConfig.protect_drv_over_tmp   = axis_config->protect_drv_over_tmp;
    UsrConfig.protect_ntc_over_tmp   = axis_config->protect_ntc_over_tmp;
    UsrConfig.calib_valid            = axis_config->calib_valid;
    UsrConfig.encoder_dir            = axis_config->encoder_dir;
    UsrConfig.encoder_offset         = axis_config->encoder_offset;
    memcpy(UsrConfig.offset_lut, axis_config->offset_lut, sizeof(UsrConfig.offset_lut));
}

void USR_CONFIG_set_default_cogging_map(void)
{
    USR_CONFIG_axis_set_default_cogging_map(CONTROLLER_active_axis());
}

int USR_CONFIG_erease_cogging_map(void)
{
    return USR_CONFIG_axis_erease_cogging_map(CONTROLLER_active_axis());
}

int USR_CONFIG_read_cogging_map(void)
{
    return USR_CONFIG_axis_read_cogging_map(CONTROLLER_active_axis());
}

int USR_CONFIG_save_cogging_map(void)
{
    return USR_CONFIG_axis_save_cogging_map(CONTROLLER_active_axis());
}

void USR_CONFIG_axis_set_default_cogging_map(motor_hw_axis_t axis)
{
    tCoggingMap *map = USR_CONFIG_cogging_map(axis);

    for (int i = 0; i < COGGING_MAP_NUM; i++) {
        map->map[i] = 0;
    }
    map->crc = 0U;
    AnticoggingValidAxes[usr_config_axis_index(axis)] = false;
}

int USR_CONFIG_axis_erease_cogging_map(motor_hw_axis_t axis)
{
    return FLASH_HW_erase_region(usr_config_cogging_map_addr(axis), sizeof(tCoggingMap));
}

int USR_CONFIG_axis_read_cogging_map(motor_hw_axis_t axis)
{
    tCoggingMap *map = USR_CONFIG_cogging_map(axis);
    uint32_t crc;

    memcpy(map, (uint8_t *) usr_config_cogging_map_addr(axis), sizeof(tCoggingMap));

    crc = crc32((uint8_t *) map, sizeof(tCoggingMap) - 4);
    if (crc != map->crc) {
        AnticoggingValidAxes[usr_config_axis_index(axis)] = false;
        return -1;
    }

    AnticoggingValidAxes[usr_config_axis_index(axis)] = true;
    return 0;
}

int USR_CONFIG_axis_save_cogging_map(motor_hw_axis_t axis)
{
    tCoggingMap *map = USR_CONFIG_cogging_map(axis);

    if (USR_CONFIG_axis_erease_cogging_map(axis)) {
        return -1;
    }

    map->crc = crc32((uint8_t *) map, sizeof(tCoggingMap) - 4);
    if (FLASH_HW_program_words(usr_config_cogging_map_addr(axis), (const uint32_t *) map, sizeof(tCoggingMap) / 4U)) {
        return -2;
    }

    AnticoggingValidAxes[usr_config_axis_index(axis)] = true;
    return 0;
}
