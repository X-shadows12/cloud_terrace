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

#include "encoder_hw.h"
#include "encoder.h"
#include "board_port.h"
#include "usr_config.h"
#include "util.h"

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
typedef struct {
    volatile uint16_t rise_capture;
    volatile uint16_t high_ticks;
    volatile uint16_t period_ticks;
    volatile uint16_t locked_period;
    volatile uint16_t pending_high_ticks;
    volatile uint8_t  have_rise;
    volatile uint8_t  high_ready;
    volatile uint8_t  sample_valid;
    volatile uint8_t  lock_samples;
    volatile uint8_t  wait_falling;
    int32_t           raw_prev;
    uint8_t           raw_prev_valid;
} encoder_pwm_state_t;

#define ENCODER_PWM_AXIS_COUNT 2U

static encoder_pwm_state_t mPwmState[ENCODER_PWM_AXIS_COUNT];

static uint16_t encoder_pwm_timer_prescaler(void);
static void     encoder_pwm_state_reset(motor_hw_axis_t axis);
static void     encoder_pwm_capture_config_axis(motor_hw_axis_t axis,
                                                uint16_t channel,
                                                uint16_t polarity,
                                                uint16_t selection,
                                                uint16_t filter);
static bool     encoder_pwm_period_in_range(uint16_t period_ticks);
static bool     encoder_pwm_period_close(uint16_t period_ticks, uint16_t reference_ticks);
static int32_t  encoder_count_delta(int32_t a, int32_t b);
static int32_t  encoder_pwm_read(motor_hw_axis_t axis, int fallback_raw, uint8_t update_legacy_status);
static uint8_t  encoder_pwm_axis_index(motor_hw_axis_t axis);
#elif (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
static inline void delay_100ns(void);
static uint16_t    encoder_spi_transfer16(uint16_t tx_data);
#endif

static motor_hw_axis_t encoder_hw_active_axis(void);

void ENCODER_hw_init(void)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    timer_parameter_struct timer_initpara;

    encoder_pwm_state_reset(MOTOR_HW_AXIS_LEFT);
    encoder_pwm_state_reset(MOTOR_HW_AXIS_RIGHT);

    timer_deinit(BOARD_ENC_PWM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = encoder_pwm_timer_prescaler();
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 0xFFFFU;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0U;
    timer_init(BOARD_ENC_PWM_TIMER, &timer_initpara);

    encoder_pwm_capture_config_axis(MOTOR_HW_AXIS_LEFT,
                                    BOARD_LEFT_ENC_PWM_TIMER_CH,
                                    TIMER_IC_POLARITY_BOTH_EDGE,
                                    TIMER_IC_SELECTION_DIRECTTI,
                                    BOARD_ENC_PWM_INPUT_FILTER);
    encoder_pwm_capture_config_axis(MOTOR_HW_AXIS_RIGHT,
                                    BOARD_RIGHT_ENC_PWM_TIMER_CH,
                                    TIMER_IC_POLARITY_BOTH_EDGE,
                                    TIMER_IC_SELECTION_DIRECTTI,
                                    BOARD_ENC_PWM_INPUT_FILTER);

    timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_LEFT_ENC_PWM_TIMER_FLAG);
    timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_RIGHT_ENC_PWM_TIMER_FLAG);
    timer_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_LEFT_ENC_PWM_TIMER_OV_FLAG);
    timer_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_RIGHT_ENC_PWM_TIMER_OV_FLAG);
    timer_interrupt_enable(BOARD_ENC_PWM_TIMER, BOARD_LEFT_ENC_PWM_TIMER_INT);
    timer_interrupt_enable(BOARD_ENC_PWM_TIMER, BOARD_RIGHT_ENC_PWM_TIMER_INT);
    timer_enable(BOARD_ENC_PWM_TIMER);
#endif
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
void ENCODER_pwm_capture_callback(motor_hw_axis_t axis, uint16_t capture, uint8_t signal_high)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];

    if (0U == pwm->have_rise) {
        if (0U != signal_high) {
            ENCODER_pwm_capture_rise_callback(axis, capture);
        }
        return;
    }

    if (0U != pwm->wait_falling) {
        ENCODER_pwm_capture_fall_callback(axis, capture);
    } else {
        ENCODER_pwm_capture_rise_callback(axis, capture);
    }
}

void ENCODER_pwm_capture_rise_callback(motor_hw_axis_t axis, uint16_t capture)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];

    if ((0U != pwm->have_rise) && (0U != pwm->high_ready)) {
        uint16_t period_ticks = (uint16_t)(capture - pwm->rise_capture);

        if (encoder_pwm_period_in_range(period_ticks)
            && ((0U == pwm->locked_period) || encoder_pwm_period_close(period_ticks, pwm->locked_period))
            && (pwm->pending_high_ticks < period_ticks)) {
            pwm->high_ticks   = pwm->pending_high_ticks;
            pwm->period_ticks = period_ticks;
            pwm->sample_valid = 1U;

            if (pwm->lock_samples < BOARD_ENC_PWM_PERIOD_LOCK_SAMPLES) {
                pwm->lock_samples++;
                if (pwm->lock_samples >= BOARD_ENC_PWM_PERIOD_LOCK_SAMPLES) {
                    pwm->locked_period = period_ticks;
                }
            } else {
                pwm->locked_period = (uint16_t)(((uint32_t)pwm->locked_period * 7U + (uint32_t)period_ticks) / 8U);
            }
        } else if (0U == pwm->sample_valid) {
            pwm->lock_samples  = 0U;
            pwm->locked_period = 0U;
        }
    }

    pwm->rise_capture      = capture;
    pwm->have_rise         = 1U;
    pwm->high_ready        = 0U;
    pwm->pending_high_ticks = 0U;
    pwm->wait_falling      = 1U;
}

void ENCODER_pwm_capture_fall_callback(motor_hw_axis_t axis, uint16_t capture)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];

    if (0U != pwm->have_rise) {
        uint16_t high_ticks = (uint16_t)(capture - pwm->rise_capture);

        if ((high_ticks >= BOARD_ENC_PWM_MIN_HIGH_TICKS)
            && (high_ticks < BOARD_ENC_PWM_MAX_PERIOD_TICKS)) {
            pwm->pending_high_ticks = high_ticks;
            pwm->high_ready         = 1U;
        }
    }

    pwm->wait_falling = 0U;
}

void ENCODER_pwm_capture_overrun_callback(motor_hw_axis_t axis)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];

    pwm->high_ready         = 0U;
    pwm->have_rise          = 0U;
    pwm->pending_high_ticks = 0U;
    pwm->lock_samples       = 0U;
    pwm->locked_period      = 0U;
    pwm->wait_falling       = 0U;
}

static uint16_t encoder_pwm_timer_prescaler(void)
{
    uint32_t ahb_hz = rcu_clock_freq_get(CK_AHB);
    uint32_t apb1_hz = rcu_clock_freq_get(CK_APB1);
    uint32_t timer_hz;
    uint32_t ticks_per_us;

    if ((apb1_hz == ahb_hz) || (apb1_hz == (ahb_hz / 2U))) {
        timer_hz = ahb_hz;
    } else {
        timer_hz = apb1_hz * 2U;
    }

    ticks_per_us = timer_hz / BOARD_ENC_PWM_TIMER_TICK_HZ;
    if (0U == ticks_per_us) {
        ticks_per_us = 1U;
    }

    return (uint16_t)(ticks_per_us - 1U);
}

static void encoder_pwm_state_reset(motor_hw_axis_t axis)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];

    pwm->rise_capture      = 0U;
    pwm->high_ticks        = 0U;
    pwm->period_ticks      = 0U;
    pwm->locked_period     = 0U;
    pwm->pending_high_ticks = 0U;
    pwm->have_rise         = 0U;
    pwm->high_ready        = 0U;
    pwm->sample_valid      = 0U;
    pwm->lock_samples      = 0U;
    pwm->wait_falling      = 0U;
    pwm->raw_prev          = 0;
    pwm->raw_prev_valid    = 0U;
}

static void encoder_pwm_capture_config_axis(motor_hw_axis_t axis,
                                            uint16_t channel,
                                            uint16_t polarity,
                                            uint16_t selection,
                                            uint16_t filter)
{
    timer_ic_parameter_struct icpara;

    (void) axis;
    timer_channel_input_struct_para_init(&icpara);
    icpara.icpolarity  = polarity;
    icpara.icselection = selection;
    icpara.icprescaler = TIMER_IC_PSC_DIV1;
    icpara.icfilter    = filter;
    timer_input_capture_config(BOARD_ENC_PWM_TIMER, channel, &icpara);
}

static bool encoder_pwm_period_in_range(uint16_t period_ticks)
{
    return ((period_ticks >= BOARD_ENC_PWM_MIN_PERIOD_TICKS)
            && (period_ticks <= BOARD_ENC_PWM_MAX_PERIOD_TICKS));
}

static bool encoder_pwm_period_close(uint16_t period_ticks, uint16_t reference_ticks)
{
    uint16_t diff = (period_ticks > reference_ticks)
                        ? (uint16_t)(period_ticks - reference_ticks)
                        : (uint16_t)(reference_ticks - period_ticks);
    uint32_t tolerance = ((uint32_t)reference_ticks * BOARD_ENC_PWM_PERIOD_TOL_PCT) / 100U;

    if (tolerance < BOARD_ENC_PWM_MIN_HIGH_TICKS) {
        tolerance = BOARD_ENC_PWM_MIN_HIGH_TICKS;
    }

    return ((uint32_t)diff <= tolerance);
}

static int32_t encoder_count_delta(int32_t a, int32_t b)
{
    int32_t delta = a - b;

    while (delta > +ENCODER_CPR_DIV)
        delta -= ENCODER_CPR;
    while (delta < -ENCODER_CPR_DIV)
        delta += ENCODER_CPR;

    return delta;
}

static int32_t encoder_pwm_read(motor_hw_axis_t axis, int fallback_raw, uint8_t update_legacy_status)
{
    encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];
    uint32_t primask;
    uint16_t high_ticks;
    uint16_t period_ticks;
    uint8_t  valid;

    primask = __get_PRIMASK();
    __disable_irq();
    high_ticks   = pwm->high_ticks;
    period_ticks = pwm->period_ticks;
    valid        = pwm->sample_valid;
    __set_PRIMASK(primask);

    if (0U != update_legacy_status) {
        Encoder.pwm_high_cycles   = high_ticks;
        Encoder.pwm_period_cycles = period_ticks;
        Encoder.pwm_valid         = valid;
    }

    if ((!valid) || (period_ticks == 0U)) {
        return (0U != pwm->raw_prev_valid) ? pwm->raw_prev : fallback_raw;
    }

    if ((high_ticks <= BOARD_ENC_PWM_MIN_HIGH_TICKS) || (high_ticks >= period_ticks)) {
        return (0U != pwm->raw_prev_valid) ? pwm->raw_prev : fallback_raw;
    }

    if ((period_ticks < BOARD_ENC_PWM_MIN_PERIOD_TICKS) || (period_ticks > BOARD_ENC_PWM_MAX_PERIOD_TICKS)) {
        return (0U != pwm->raw_prev_valid) ? pwm->raw_prev : fallback_raw;
    }

    int32_t raw = (int32_t) (((uint64_t) high_ticks * (uint64_t) BOARD_ENC_PWM_CPR) / period_ticks);

    if (raw >= (int32_t) BOARD_ENC_PWM_CPR) {
        raw = (int32_t) BOARD_ENC_PWM_CPR - 1;
    }

    if (0U != pwm->raw_prev_valid) {
        int32_t delta = encoder_count_delta(raw, pwm->raw_prev);

        if (ABS(delta) > (int32_t) BOARD_ENC_PWM_MAX_STEP_COUNTS) {
            return pwm->raw_prev;
        }
    }

    pwm->raw_prev       = raw;
    pwm->raw_prev_valid = 1U;

    return raw;
}

static uint8_t encoder_pwm_axis_index(motor_hw_axis_t axis)
{
    return (axis == MOTOR_HW_AXIS_RIGHT) ? 1U : 0U;
}
#endif

static motor_hw_axis_t encoder_hw_active_axis(void)
{
#if defined(BOARD_USE_RIGHT_MOTOR)
    return MOTOR_HW_AXIS_RIGHT;
#else
    return MOTOR_HW_AXIS_LEFT;
#endif
}

int32_t ENCODER_hw_read(void)
{
    return ENCODER_hw_read_axis(encoder_hw_active_axis());
}

int32_t ENCODER_hw_read_axis(motor_hw_axis_t axis)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    uint8_t update_legacy_status = (axis == encoder_hw_active_axis()) ? 1U : 0U;
    int32_t raw = encoder_pwm_read(axis, ENCODER_axis(axis)->raw, update_legacy_status);

    return raw;
#elif (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
    uint16_t data[2];
    uint16_t sample_data;

    BOARD_ENC_CS_RESET();
    data[0] = encoder_spi_transfer16(0x8300);
    BOARD_ENC_CS_SET();

    delay_100ns();

    BOARD_ENC_CS_RESET();
    data[1] = encoder_spi_transfer16(0x8400);
    BOARD_ENC_CS_SET();

    sample_data = ((data[0] & 0x00FF) << 8) | (data[1] & 0x00FF);

    Encoder.pwm_valid         = 1U;
    Encoder.pwm_period_cycles = 0U;
    Encoder.pwm_high_cycles   = 0U;

    return (sample_data >> 2);
#else
    Encoder.pwm_valid         = 1U;
    Encoder.pwm_period_cycles = 0U;
    Encoder.pwm_high_cycles   = 0U;
    return 0;
#endif
}

void ENCODER_hw_get_axis_pwm_status(motor_hw_axis_t axis,
                                    uint8_t *valid,
                                    uint32_t *period_cycles,
                                    uint32_t *high_cycles)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    const encoder_pwm_state_t *pwm = &mPwmState[encoder_pwm_axis_index(axis)];
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (valid != NULL) {
        *valid = pwm->sample_valid;
    }
    if (period_cycles != NULL) {
        *period_cycles = pwm->period_ticks;
    }
    if (high_cycles != NULL) {
        *high_cycles = pwm->high_ticks;
    }
    __set_PRIMASK(primask);
#else
    (void) axis;
    if (valid != NULL) {
        *valid = 1U;
    }
    if (period_cycles != NULL) {
        *period_cycles = 0U;
    }
    if (high_cycles != NULL) {
        *high_cycles = 0U;
    }
#endif
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
static inline void delay_100ns(void)
{
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static uint16_t encoder_spi_transfer16(uint16_t tx_data)
{
    uint16_t rx_data;

    spi_current_data_num_config(BOARD_ENC_SPI, 1U);
    spi_master_transfer_start(BOARD_ENC_SPI, SPI_TRANS_START);

    while (RESET == spi_i2s_flag_get(BOARD_ENC_SPI, SPI_FLAG_TP))
        ;
    spi_i2s_data_transmit(BOARD_ENC_SPI, tx_data);

    while (RESET == spi_i2s_flag_get(BOARD_ENC_SPI, SPI_FLAG_RP))
        ;
    rx_data = (uint16_t) spi_i2s_data_receive(BOARD_ENC_SPI);

    while (RESET == spi_i2s_flag_get(BOARD_ENC_SPI, SPI_FLAG_ET))
        ;
    spi_master_transfer_start(BOARD_ENC_SPI, SPI_TRANS_IDLE);
    spi_i2s_flag_clear(BOARD_ENC_SPI, SPI_FLAG_ET);

    return rx_data;
}
#endif
