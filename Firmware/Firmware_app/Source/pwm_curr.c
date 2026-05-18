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
    set_a_duty((uint16_t) (duty_a * (float) HALF_PWM_PERIOD_CYCLES));
    set_b_duty((uint16_t) (duty_b * (float) HALF_PWM_PERIOD_CYCLES));
    set_c_duty((uint16_t) (duty_c * (float) HALF_PWM_PERIOD_CYCLES));
}

void MOTOR_HW_turn_on_low_sides(void)
{
    PWMC_TurnOnLowSides();
}

void MOTOR_HW_switch_off_pwm(void)
{
    PWMC_SwitchOffPWM();
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
    return (float) (phase_a_adc_offset - READ_IPHASE_A_ADC()) * I_SCALE;
}

float MOTOR_HW_read_phase_b_current(void)
{
    return (float) (READ_IPHASE_B_ADC() - phase_b_adc_offset) * I_SCALE;
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

void PWMC_init(void)
{
    /* Disable ADC interrupt */
    adc_interrupt_disable(BOARD_PHASE_ADC, ADC_INT_EOIC);
    adc_interrupt_flag_clear(BOARD_PHASE_ADC, ADC_INT_FLAG_EOIC);

    /* enable phase-current ADC */
    adc_enable(BOARD_PHASE_ADC);
    /* Wait ADC startup */
    delay_ms(1);
    /* ADC calibration */
    adc_calibration_mode_config(BOARD_PHASE_ADC, ADC_CALIBRATION_OFFSET);
    adc_calibration_number(BOARD_PHASE_ADC, ADC_CALIBRATION_NUM1);
    adc_calibration_enable(BOARD_PHASE_ADC);

    /* Phase-current injected convert complete interrupt */
    adc_interrupt_flag_clear(BOARD_PHASE_ADC, ADC_INT_FLAG_EOIC);
    adc_interrupt_enable(BOARD_PHASE_ADC, ADC_INT_EOIC);

    /* Hold the PWM timer when core is halted */
    dbg_periph_enable(BOARD_PWM_TIMER_DBG_HOLD);

    /* Enable PWM timer counter */
    timer_enable(BOARD_PWM_TIMER);

    timer_repetition_value_config(BOARD_PWM_TIMER, TIMER_CREP0_ENABLE, 1);

    /* Set all duty to 50% */
    set_a_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));
    set_b_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));
    set_c_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));

    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCX_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCXN_DISABLE);

    /* Main PWM Output Enable */
    timer_primary_output_config(BOARD_PWM_TIMER, ENABLE);
}

void PWMC_SwitchOnPWM(void)
{
    /* Set all duty to 50% */
    set_a_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));
    set_b_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));
    set_c_duty(((uint32_t) HALF_PWM_PERIOD_CYCLES / (uint32_t) 2));

    /* wait for a new PWM period */
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);
    while (RESET == timer_flag_get(BOARD_PWM_TIMER, TIMER_FLAG_UP)) {
    };
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);

    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCX_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCXN_ENABLE);
}

void PWMC_SwitchOffPWM(void)
{
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCX_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCXN_DISABLE);

    /* wait for a new PWM period */
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);
    while (RESET == timer_flag_get(BOARD_PWM_TIMER, TIMER_FLAG_UP)) {
    };
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);
}

void PWMC_TurnOnLowSides(void)
{
    /* Set all duty to 0% */
    set_a_duty(0);
    set_b_duty(0);
    set_c_duty(0);

    /* wait for a new PWM period */
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);
    while (RESET == timer_flag_get(BOARD_PWM_TIMER, TIMER_FLAG_UP)) {
    };
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);

    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCX_ENABLE);
    timer_channel_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCX_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_0, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_1, TIMER_CCXN_ENABLE);
    timer_channel_complementary_output_state_config(BOARD_PWM_TIMER, TIMER_CH_2, TIMER_CCXN_ENABLE);
}

int PWMC_CurrentReadingPolarization(void)
{
    int i         = 0;
    int adc_sum_a = 0;
    int adc_sum_b = 0;

    /* Clear Update Flag */
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);
    /* Wait until next update */
    while (RESET == timer_flag_get(BOARD_PWM_TIMER, TIMER_FLAG_UP)) {
    };
    /* Clear Update Flag */
    timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);

    while (i < 64) {
        if (timer_flag_get(BOARD_PWM_TIMER, TIMER_FLAG_UP) == SET) {
            timer_flag_clear(BOARD_PWM_TIMER, TIMER_FLAG_UP);

            i++;
            adc_sum_a += READ_IPHASE_A_ADC();
            adc_sum_b += READ_IPHASE_B_ADC();
        }
    }

    phase_a_adc_offset = adc_sum_a / i;
    phase_b_adc_offset = adc_sum_b / i;

    // offset check
    i                         = 0;
    const int Vout            = 2048;
    const int check_threshold = 200;
    if (phase_a_adc_offset > (Vout + check_threshold) || phase_a_adc_offset < (Vout - check_threshold)) {
        i = -1;
    }
    if (phase_b_adc_offset > (Vout + check_threshold) || phase_b_adc_offset < (Vout - check_threshold)) {
        i = -1;
    }

    return i;
}
