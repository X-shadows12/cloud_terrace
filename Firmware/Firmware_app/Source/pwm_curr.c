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

#include "pwm_curr.h"
#include "pwm_curr_hw.h"
#include "runtime.h"

uint16_t adc_buff[3];
int16_t  phase_a_adc_offset = 0;
int16_t  phase_b_adc_offset = 0;
static int16_t left_phase_a_adc_offset  = 0;
static int16_t left_phase_b_adc_offset  = 0;
static int16_t right_phase_a_adc_offset = 0;
static int16_t right_phase_b_adc_offset = 0;

static const int16_t temp_table[]
    = {277, 277, 227, 201, 184, 172, 162, 154, 148, 142, 137, 132, 128, 125, 121, 118, 115, 113, 110, 108, 105, 103,
       101, 99,  98,  96,  94,  93,  91,  90,  88,  87,  86,  84,  83,  82,  81,  80,  79,  77,  76,  75,  74,  73,
       72,  72,  71,  70,  69,  68,  67,  66,  66,  65,  64,  63,  63,  62,  61,  60,  60,  59,  58,  58,  57,  56,
       56,  55,  54,  54,  53,  53,  52,  51,  51,  50,  50,  49,  48,  48,  47,  47,  46,  46,  45,  45,  44,  44,
       43,  43,  42,  42,  41,  41,  40,  40,  39,  39,  38,  38,  37,  37,  36,  36,  35,  35,  35,  34,  34,  33,
       33,  32,  32,  31,  31,  31,  30,  30,  29,  29,  28,  28,  28,  27,  27,  26,  26,  25,  25,  25,  24,  24,
       23,  23,  22,  22,  22,  21,  21,  20,  20,  20,  19,  19,  18,  18,  18,  17,  17,  16,  16,  16,  15,  15,
       14,  14,  13,  13,  13,  12,  12,  11,  11,  11,  10,  10,  9,   9,   8,   8,   8,   7,   7,   6,   6,   5,
       5,   5,   4,   4,   3,   3,   2,   2,   1,   1,   1,   0,   -0,  -1,  -1,  -2,  -2,  -3,  -3,  -4,  -4,  -5,
       -5,  -6,  -6,  -7,  -7,  -8,  -8,  -9,  -9,  -10, -11, -11, -12, -12, -13, -13, -14, -15, -15, -16, -17, -17,
       -18, -19, -19, -20, -21, -21, -22, -23, -24, -25, -26, -26, -27, -28, -29, -30, -31, -32, -33, -35, -36, -37,
       -38, -40, -41, -43, -45, -47, -49, -51, -54, -57, -61, -65, -72, -82};

#define PWM_UPDATE_WAIT_TIMEOUT 1000000U

static void update_vbus_adc_sample(void)
{
#if BOARD_HAS_VBUS_ADC
    if (SET == adc_flag_get(BOARD_VBUS_ADC, ADC_FLAG_ROVF)) {
        adc_flag_clear(BOARD_VBUS_ADC, ADC_FLAG_ROVF);
    }

    if (SET == adc_flag_get(BOARD_VBUS_ADC, ADC_FLAG_EOC)) {
        adc_buff[0] = (uint16_t) adc_regular_data_read(BOARD_VBUS_ADC);
        adc_flag_clear(BOARD_VBUS_ADC, ADC_FLAG_EOC);
        adc_software_trigger_enable(BOARD_VBUS_ADC, ADC_REGULAR_CHANNEL);
    }
#endif
}

static void phase_adc_enable_calibrate(uint32_t adc_periph, uint8_t enable_eoic_interrupt)
{
    adc_interrupt_disable(adc_periph, ADC_INT_EOIC);
    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_EOIC);

    adc_enable(adc_periph);
    delay_ms(1);
    adc_calibration_mode_config(adc_periph, ADC_CALIBRATION_OFFSET);
    adc_calibration_number(adc_periph, ADC_CALIBRATION_NUM1);
    adc_calibration_enable(adc_periph);

    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_EOIC);
    if (enable_eoic_interrupt != 0U) {
        adc_interrupt_enable(adc_periph, ADC_INT_EOIC);
    }
}

static void pwm_outputs_disable(uint32_t timer_periph)
{
    timer_channel_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCX_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCXN_DISABLE);
}

static void pwm_outputs_enable(uint32_t timer_periph)
{
    timer_channel_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCX_ENABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCXN_ENABLE);
}

static int pwm_wait_update(uint32_t timer_periph)
{
    uint32_t timeout = PWM_UPDATE_WAIT_TIMEOUT;

    timer_flag_clear(timer_periph, TIMER_FLAG_UP);
    while (RESET == timer_flag_get(timer_periph, TIMER_FLAG_UP)) {
        if (--timeout == 0U) {
            return -1;
        }
    };
    timer_flag_clear(timer_periph, TIMER_FLAG_UP);
    return 0;
}

static void set_all_motor_pwm_mid_duty(void)
{
    const uint32_t mid_duty = ((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2);

    set_left_a_duty(mid_duty);
    set_left_b_duty(mid_duty);
    set_left_c_duty(mid_duty);
    set_right_a_duty(mid_duty);
    set_right_b_duty(mid_duty);
    set_right_c_duty(mid_duty);
}

static motor_hw_axis_t active_axis(void)
{
#if defined(BOARD_USE_RIGHT_MOTOR)
    return MOTOR_HW_AXIS_RIGHT;
#else
    return MOTOR_HW_AXIS_LEFT;
#endif
}

static uint32_t axis_pwm_timer(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return BOARD_RIGHT_PWM_TIMER;
    }

    return BOARD_LEFT_PWM_TIMER;
}

static uint32_t axis_phase_adc(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return BOARD_RIGHT_PHASE_ADC;
    }

    return BOARD_LEFT_PHASE_ADC;
}

static uint16_t axis_read_phase_a_adc(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return READ_RIGHT_IPHASE_A_ADC();
    }

    return READ_LEFT_IPHASE_A_ADC();
}

static uint16_t axis_read_phase_b_adc(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return READ_RIGHT_IPHASE_B_ADC();
    }

    return READ_LEFT_IPHASE_B_ADC();
}

static int16_t *axis_phase_a_offset(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return &right_phase_a_adc_offset;
    }

    return &left_phase_a_adc_offset;
}

static int16_t *axis_phase_b_offset(motor_hw_axis_t axis)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        return &right_phase_b_adc_offset;
    }

    return &left_phase_b_adc_offset;
}

static void set_axis_duty_cycles(motor_hw_axis_t axis, uint32_t duty_a,
                                 uint32_t duty_b, uint32_t duty_c)
{
    if (axis == MOTOR_HW_AXIS_RIGHT) {
        set_right_a_duty(duty_a);
        set_right_b_duty(duty_b);
        set_right_c_duty(duty_c);
    } else {
        set_left_a_duty(duty_a);
        set_left_b_duty(duty_b);
        set_left_c_duty(duty_c);
    }
}

static void set_axis_mid_duty(motor_hw_axis_t axis)
{
    const uint32_t mid_duty = ((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2);

    set_axis_duty_cycles(axis, mid_duty, mid_duty, mid_duty);
}

void MOTOR_HW_enter_critical(void)
{
    __disable_irq();
}

void MOTOR_HW_exit_critical(void)
{
    __enable_irq();
}

void MOTOR_HW_set_phase_duty(float duty_a, float duty_b, float duty_c)
{
    MOTOR_HW_set_axis_phase_duty(active_axis(), duty_a, duty_b, duty_c);
}

void MOTOR_HW_set_axis_phase_duty(motor_hw_axis_t axis, float duty_a, float duty_b, float duty_c)
{
    set_axis_duty_cycles(axis,
                         (uint16_t) (duty_a * (float) HALF_PWM_PERIOD_CYCLES),
                         (uint16_t) (duty_b * (float) HALF_PWM_PERIOD_CYCLES),
                         (uint16_t) (duty_c * (float) HALF_PWM_PERIOD_CYCLES));
}

void MOTOR_HW_turn_on_low_sides(void)
{
    MOTOR_HW_turn_on_axis_low_sides(active_axis());
}

void MOTOR_HW_turn_on_axis_low_sides(motor_hw_axis_t axis)
{
    PWMC_TurnOnAxisLowSides(axis);
}

void MOTOR_HW_switch_off_pwm(void)
{
    MOTOR_HW_switch_off_axis_pwm(active_axis());
}

void MOTOR_HW_switch_off_axis_pwm(motor_hw_axis_t axis)
{
    PWMC_SwitchOffAxisPWM(axis);
}

float MOTOR_HW_read_vbus_voltage(void)
{
#if BOARD_HAS_VBUS_ADC
    update_vbus_adc_sample();
    return (float) (adc_buff[0]) * V_SCALE;
#else
    return BOARD_NOMINAL_VBUS;
#endif
}

float MOTOR_HW_read_phase_a_current(void)
{
    return MOTOR_HW_read_axis_phase_a_current(active_axis());
}

float MOTOR_HW_read_phase_b_current(void)
{
    return MOTOR_HW_read_axis_phase_b_current(active_axis());
}

float MOTOR_HW_read_axis_phase_a_current(motor_hw_axis_t axis)
{
    return (float) (*axis_phase_a_offset(axis) - axis_read_phase_a_adc(axis)) * I_SCALE;
}

float MOTOR_HW_read_axis_phase_b_current(motor_hw_axis_t axis)
{
    return (float) (axis_read_phase_b_adc(axis) - *axis_phase_b_offset(axis)) * I_SCALE;
}

int MOTOR_HW_read_driver_temp(void)
{
#if BOARD_HAS_TEMP_ADC
    return temp_table[adc_buff[1] >> 4];
#else
    return BOARD_DEFAULT_DRV_TEMP;
#endif
}

int MOTOR_HW_read_ntc_temp(void)
{
#if BOARD_HAS_TEMP_ADC
    return temp_table[adc_buff[2] >> 4];
#else
    return BOARD_DEFAULT_NTC_TEMP;
#endif
}

int MOTOR_HW_calibrate_axis_current_offset(motor_hw_axis_t axis)
{
    return PWMC_CurrentReadingAxisPolarization(axis);
}

void PWMC_init(void)
{
#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    phase_adc_enable_calibrate(BOARD_LEFT_PHASE_ADC, 1U);
    phase_adc_enable_calibrate(BOARD_RIGHT_PHASE_ADC, 1U);
#else
    phase_adc_enable_calibrate(BOARD_LEFT_PHASE_ADC, (BOARD_PHASE_ADC == BOARD_LEFT_PHASE_ADC));
    phase_adc_enable_calibrate(BOARD_RIGHT_PHASE_ADC, (BOARD_PHASE_ADC == BOARD_RIGHT_PHASE_ADC));
#endif

    /* Hold both motor PWM timers when core is halted. */
    dbg_periph_enable(BOARD_LEFT_PWM_TIMER_DBG_HOLD);
    dbg_periph_enable(BOARD_RIGHT_PWM_TIMER_DBG_HOLD);

    /* Enable both timer counters so their CH3 events can trigger phase ADC sampling. */
    timer_enable(BOARD_LEFT_PWM_TIMER);
    timer_enable(BOARD_RIGHT_PWM_TIMER);
    timer_repetition_value_config(BOARD_LEFT_PWM_TIMER, TIMER_CREP0_ENABLE, 1);
    timer_repetition_value_config(BOARD_RIGHT_PWM_TIMER, TIMER_CREP0_ENABLE, 1);

    /* Set all duty to 50% */
    set_all_motor_pwm_mid_duty();

    pwm_outputs_disable(BOARD_LEFT_PWM_TIMER);
    pwm_outputs_disable(BOARD_RIGHT_PWM_TIMER);
    timer_primary_output_config(BOARD_LEFT_PWM_TIMER, DISABLE);
    timer_primary_output_config(BOARD_RIGHT_PWM_TIMER, DISABLE);

    /* Keep phase outputs disabled, but allow each timer's CH3 event to trigger ADC sampling. */
    timer_primary_output_config(BOARD_LEFT_PWM_TIMER, ENABLE);
    timer_primary_output_config(BOARD_RIGHT_PWM_TIMER, ENABLE);
}

void PWMC_SwitchOnPWM(void)
{
    PWMC_SwitchOnAxisPWM(active_axis());
}

void PWMC_SwitchOnAxisPWM(motor_hw_axis_t axis)
{
    const uint32_t timer_periph = axis_pwm_timer(axis);

    set_axis_mid_duty(axis);
    pwm_wait_update(timer_periph);
    timer_primary_output_config(timer_periph, ENABLE);
    pwm_outputs_enable(timer_periph);
}

void PWMC_SwitchOffPWM(void)
{
    PWMC_SwitchOffAxisPWM(active_axis());
}

void PWMC_SwitchOffAxisPWM(motor_hw_axis_t axis)
{
    const uint32_t timer_periph = axis_pwm_timer(axis);

    pwm_outputs_disable(timer_periph);
    pwm_wait_update(timer_periph);
}

void PWMC_TurnOnLowSides(void)
{
    PWMC_TurnOnAxisLowSides(active_axis());
}

void PWMC_TurnOnAxisLowSides(motor_hw_axis_t axis)
{
    const uint32_t timer_periph = axis_pwm_timer(axis);

    set_axis_duty_cycles(axis, 0U, 0U, 0U);
    pwm_wait_update(timer_periph);
    timer_primary_output_config(timer_periph, ENABLE);
    pwm_outputs_enable(timer_periph);
}

int PWMC_CurrentReadingPolarization(void)
{
    return PWMC_CurrentReadingAxisPolarization(active_axis());
}

int PWMC_CurrentReadingAxisPolarization(motor_hw_axis_t axis)
{
    int i         = 0;
    int adc_sum_a = 0;
    int adc_sum_b = 0;
    const uint32_t timer_periph = axis_pwm_timer(axis);
    const uint32_t adc_periph   = axis_phase_adc(axis);
    int16_t       *offset_a     = axis_phase_a_offset(axis);
    int16_t       *offset_b     = axis_phase_b_offset(axis);
    uint16_t       update_count = 0U;

    if (pwm_wait_update(timer_periph) != 0) {
        return -1;
    }
    timer_flag_clear(timer_periph, TIMER_FLAG_UP);
    adc_interrupt_disable(adc_periph, ADC_INT_EOIC);
    adc_flag_clear(adc_periph, ADC_FLAG_EOIC);

    while ((i < 64) && (update_count < 512U)) {
        if (SET == adc_flag_get(adc_periph, ADC_FLAG_EOIC)) {
            adc_flag_clear(adc_periph, ADC_FLAG_EOIC);
            adc_sum_a += axis_read_phase_a_adc(axis);
            adc_sum_b += axis_read_phase_b_adc(axis);
            i++;
        }
        if (timer_flag_get(timer_periph, TIMER_FLAG_UP) == SET) {
            timer_flag_clear(timer_periph, TIMER_FLAG_UP);
            update_count++;
        }
    }

    adc_flag_clear(adc_periph, ADC_FLAG_EOIC);
    adc_interrupt_enable(adc_periph, ADC_INT_EOIC);

    if (i == 0) {
        return -1;
    }

    *offset_a = adc_sum_a / i;
    *offset_b = adc_sum_b / i;

    if (axis == active_axis()) {
        phase_a_adc_offset = *offset_a;
        phase_b_adc_offset = *offset_b;
    }

    // offset check
    i                         = 0;
    const int Vout            = 2048;
    const int check_threshold = (axis == MOTOR_HW_AXIS_RIGHT) ? 400 : 200;
    if (*offset_a > (Vout + check_threshold) || *offset_a < (Vout - check_threshold)) {
        i = -1;
    }
    if (*offset_b > (Vout + check_threshold) || *offset_b < (Vout - check_threshold)) {
        i = -1;
    }

    return i;
}
