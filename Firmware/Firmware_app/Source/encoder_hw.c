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
#include "runtime.h"
#include "usr_config.h"
#include "util.h"

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
#include "rtt_scope.h"
#define ENCODER_LOG(format, ...) DEBUG(format, ##__VA_ARGS__)
#else
#define ENCODER_LOG(format, ...)
#endif

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
static volatile uint16_t mPwmRiseCapture = 0U;
static volatile uint16_t mPwmHighTicks    = 0U;
static volatile uint16_t mPwmPeriodTicks  = 0U;
static volatile uint16_t mPwmPendingHighTicks = 0U;
static volatile uint16_t mPwmLastCapture  = 0U;
static volatile uint16_t mPwmLockedPeriod = 0U;
static volatile uint8_t  mPwmHaveRise     = 0U;
static volatile uint8_t  mPwmHighReady    = 0U;
static volatile uint8_t  mPwmSampleValid  = 0U;
static volatile uint8_t  mPwmLockSamples  = 0U;
static volatile uint8_t  mPwmWaitFalling  = 0U;
static int32_t           mPwmRawPrev      = 0;
static uint8_t           mPwmRawPrevValid = 0U;

static uint16_t encoder_pwm_timer_prescaler(void);
static void     encoder_pwm_capture_set_polarity(uint16_t polarity);
static bool     encoder_pwm_period_in_range(uint16_t period_ticks);
static bool     encoder_pwm_period_close(uint16_t period_ticks, uint16_t reference_ticks);
static int32_t  encoder_count_delta(int32_t a, int32_t b);
static int32_t  encoder_pwm_read(void);
#elif (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
static inline void delay_100ns(void);
static uint16_t    encoder_spi_transfer16(uint16_t tx_data);
#endif

void ENCODER_hw_init(void)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    timer_parameter_struct timer_initpara;
    timer_ic_parameter_struct icpara;

    mPwmRiseCapture = 0U;
    mPwmHighTicks   = 0U;
    mPwmPeriodTicks = 0U;
    mPwmPendingHighTicks = 0U;
    mPwmLastCapture  = 0U;
    mPwmLockedPeriod = 0U;
    mPwmHaveRise    = 0U;
    mPwmHighReady   = 0U;
    mPwmSampleValid = 0U;
    mPwmLockSamples = 0U;
    mPwmWaitFalling = 0U;
    mPwmRawPrev     = 0;
    mPwmRawPrevValid = 0U;

    timer_deinit(BOARD_ENC_PWM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = encoder_pwm_timer_prescaler();
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 0xFFFFU;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0U;
    timer_init(BOARD_ENC_PWM_TIMER, &timer_initpara);

    timer_channel_input_struct_para_init(&icpara);
    icpara.icpolarity   = TIMER_IC_POLARITY_RISING;
    icpara.icselection  = TIMER_IC_SELECTION_DIRECTTI;
    icpara.icprescaler  = TIMER_IC_PSC_DIV1;
    icpara.icfilter     = BOARD_ENC_PWM_INPUT_FILTER;
    timer_input_capture_config(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_CH, &icpara);

    timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_FLAG);
    timer_flag_clear(BOARD_ENC_PWM_TIMER, TIMER_FLAG_CH2O);
    timer_interrupt_enable(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_INT);
    timer_enable(BOARD_ENC_PWM_TIMER);
#endif
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
void ENCODER_pwm_capture_callback(uint16_t capture)
{
    /* Alternate the capture polarity explicitly; GPIO level after the ISR starts
       is not a reliable record of which edge filled the capture register. */
    if (0U != mPwmHaveRise) {
        uint16_t edge_ticks = (uint16_t)(capture - mPwmLastCapture);

        if (edge_ticks < BOARD_ENC_PWM_MIN_HIGH_TICKS) {
            return;
        }
    }
    mPwmLastCapture = capture;

    if (0U != mPwmWaitFalling) {
        if (0U != mPwmHaveRise) {
            uint16_t high_ticks = (uint16_t)(capture - mPwmRiseCapture);

            if ((high_ticks >= BOARD_ENC_PWM_MIN_HIGH_TICKS)
                && (high_ticks < BOARD_ENC_PWM_MAX_PERIOD_TICKS)) {
                mPwmPendingHighTicks = high_ticks;
                mPwmHighReady        = 1U;
            } else {
                mPwmHighReady        = 0U;
                mPwmPendingHighTicks = 0U;
            }
        }

        mPwmWaitFalling = 0U;
        encoder_pwm_capture_set_polarity(TIMER_IC_POLARITY_RISING);
    } else {
        if ((0U != mPwmHaveRise) && (0U != mPwmHighReady)) {
            uint16_t period_ticks = (uint16_t)(capture - mPwmRiseCapture);

            if (encoder_pwm_period_in_range(period_ticks)
                && ((0U == mPwmLockedPeriod) || encoder_pwm_period_close(period_ticks, mPwmLockedPeriod))
                && (mPwmPendingHighTicks < period_ticks)) {
                mPwmHighTicks   = mPwmPendingHighTicks;
                mPwmPeriodTicks = period_ticks;
                mPwmSampleValid = 1U;

                if (mPwmLockSamples < BOARD_ENC_PWM_PERIOD_LOCK_SAMPLES) {
                    mPwmLockSamples++;
                    if (mPwmLockSamples >= BOARD_ENC_PWM_PERIOD_LOCK_SAMPLES) {
                        mPwmLockedPeriod = period_ticks;
                    }
                } else {
                    mPwmLockedPeriod = (uint16_t)(((uint32_t)mPwmLockedPeriod * 7U + (uint32_t)period_ticks) / 8U);
                }
            } else if (0U == mPwmSampleValid) {
                mPwmLockSamples = 0U;
                mPwmLockedPeriod = 0U;
            }

            mPwmHighReady = 0U;
            mPwmPendingHighTicks = 0U;
        }

        mPwmRiseCapture = capture;
        mPwmHaveRise    = 1U;
        mPwmWaitFalling = 1U;
        encoder_pwm_capture_set_polarity(TIMER_IC_POLARITY_FALLING);
    }
}

void ENCODER_pwm_capture_overrun_callback(void)
{
    mPwmHighReady   = 0U;
    mPwmHaveRise    = 0U;
    mPwmPendingHighTicks = 0U;
    mPwmWaitFalling = 0U;
    mPwmLockSamples = 0U;
    mPwmLockedPeriod = 0U;
    encoder_pwm_capture_set_polarity(TIMER_IC_POLARITY_RISING);
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

static void encoder_pwm_capture_set_polarity(uint16_t polarity)
{
    uint32_t ctl = TIMER_CHCTL2(BOARD_ENC_PWM_TIMER);

    ctl &= (~(uint32_t)(TIMER_CHCTL2_CH2P | TIMER_CHCTL2_MCH2P));
    ctl |= ((uint32_t)polarity << 8U);
    TIMER_CHCTL2(BOARD_ENC_PWM_TIMER) = ctl;
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

static int32_t encoder_pwm_read(void)
{
    uint32_t primask;
    uint16_t high_ticks;
    uint16_t period_ticks;
    uint8_t  valid;

    primask = __get_PRIMASK();
    __disable_irq();
    high_ticks   = mPwmHighTicks;
    period_ticks = mPwmPeriodTicks;
    valid        = mPwmSampleValid;
    __set_PRIMASK(primask);

    Encoder.pwm_high_cycles   = high_ticks;
    Encoder.pwm_period_cycles = period_ticks;
    Encoder.pwm_valid         = valid;

    if ((!valid) || (period_ticks == 0U)) {
        return (0U != mPwmRawPrevValid) ? mPwmRawPrev : Encoder.raw;
    }

    if ((high_ticks <= BOARD_ENC_PWM_MIN_HIGH_TICKS) || (high_ticks >= period_ticks)) {
        return (0U != mPwmRawPrevValid) ? mPwmRawPrev : Encoder.raw;
    }

    if ((period_ticks < BOARD_ENC_PWM_MIN_PERIOD_TICKS) || (period_ticks > BOARD_ENC_PWM_MAX_PERIOD_TICKS)) {
        return (0U != mPwmRawPrevValid) ? mPwmRawPrev : Encoder.raw;
    }

    int32_t raw = (int32_t) (((uint64_t) high_ticks * (uint64_t) BOARD_ENC_PWM_CPR) / period_ticks);

    if (raw >= (int32_t) BOARD_ENC_PWM_CPR) {
        raw = (int32_t) BOARD_ENC_PWM_CPR - 1;
    }

    if (0U != mPwmRawPrevValid) {
        int32_t delta = encoder_count_delta(raw, mPwmRawPrev);

        if (ABS(delta) > (int32_t) BOARD_ENC_PWM_MAX_STEP_COUNTS) {
            return mPwmRawPrev;
        }
    }

    mPwmRawPrev      = raw;
    mPwmRawPrevValid = 1U;

    return raw;
}
#endif

int32_t ENCODER_hw_read(void)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    static uint32_t pwm_log_tick = 0U;
    int32_t raw = encoder_pwm_read();

    if (get_ms_since(pwm_log_tick) > 1000U) {
        pwm_log_tick = SystickCount;
        ENCODER_LOG("[ENC] pwm valid=%u raw=%d high=%u period=%u\n",
                    (unsigned) Encoder.pwm_valid,
                    raw,
                    (unsigned) Encoder.pwm_high_cycles,
                    (unsigned) Encoder.pwm_period_cycles);
    }

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
