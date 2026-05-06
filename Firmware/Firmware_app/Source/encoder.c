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

#include "encoder.h"
#include "pwm_curr.h"
#include "usr_config.h"
#include "util.h"

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
#include "SEGGER_RTT.h"
#define ENCODER_LOG(format, ...) SEGGER_RTT_printf(0, format, ##__VA_ARGS__)
#else
#define ENCODER_LOG(format, ...)
#endif

tEncoder Encoder;

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
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
#endif

void ENCODER_init(void)
{
    // Init
    Encoder.need_init    = 20;
    Encoder.shadow_count = 0;
    Encoder.pll_pos      = 0;
    Encoder.pll_vel      = 0;

    int encoder_pll_bw     = 100 * M_2PI;
    Encoder.pll_kp         = 2.0f * encoder_pll_bw;      // basic conversion to discrete time
    Encoder.pll_ki         = 0.25f * SQ(Encoder.pll_kp); // Critically damped
    Encoder.snap_threshold = 0.5f * CURRENT_MEASURE_PERIOD * Encoder.pll_ki;
    Encoder.pwm_valid      = 0;
    Encoder.pwm_period_cycles = 0;
    Encoder.pwm_high_cycles   = 0;

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    ENCODER_LOG("[ENC] pwm mode cpr=%u\n", (unsigned) CTM_H759_ENC_PWM_CPR);
#endif
}

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
void ENCODER_hw_init(void)
{
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

    timer_deinit(CTM_H759_ENC_PWM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = encoder_pwm_timer_prescaler();
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 0xFFFFU;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0U;
    timer_init(CTM_H759_ENC_PWM_TIMER, &timer_initpara);

    timer_channel_input_struct_para_init(&icpara);
    icpara.icpolarity   = TIMER_IC_POLARITY_RISING;
    icpara.icselection  = TIMER_IC_SELECTION_DIRECTTI;
    icpara.icprescaler  = TIMER_IC_PSC_DIV1;
    icpara.icfilter     = CTM_H759_ENC_PWM_INPUT_FILTER;
    timer_input_capture_config(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_CH, &icpara);

    timer_interrupt_flag_clear(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_FLAG);
    timer_flag_clear(CTM_H759_ENC_PWM_TIMER, TIMER_FLAG_CH2O);
    timer_interrupt_enable(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_INT);
    timer_enable(CTM_H759_ENC_PWM_TIMER);
}

void ENCODER_pwm_capture_callback(uint16_t capture)
{
    /* Alternate the capture polarity explicitly; GPIO level after the ISR starts
       is not a reliable record of which edge filled the capture register. */
    if (0U != mPwmHaveRise) {
        uint16_t edge_ticks = (uint16_t)(capture - mPwmLastCapture);

        if (edge_ticks < CTM_H759_ENC_PWM_MIN_HIGH_TICKS) {
            return;
        }
    }
    mPwmLastCapture = capture;

    if (0U != mPwmWaitFalling) {
        if (0U != mPwmHaveRise) {
            uint16_t high_ticks = (uint16_t)(capture - mPwmRiseCapture);

            if ((high_ticks >= CTM_H759_ENC_PWM_MIN_HIGH_TICKS)
                && (high_ticks < CTM_H759_ENC_PWM_MAX_PERIOD_TICKS)) {
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

                if (mPwmLockSamples < CTM_H759_ENC_PWM_PERIOD_LOCK_SAMPLES) {
                    mPwmLockSamples++;
                    if (mPwmLockSamples >= CTM_H759_ENC_PWM_PERIOD_LOCK_SAMPLES) {
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

    ticks_per_us = timer_hz / CTM_H759_ENC_PWM_TIMER_TICK_HZ;
    if (0U == ticks_per_us) {
        ticks_per_us = 1U;
    }

    return (uint16_t)(ticks_per_us - 1U);
}

static void encoder_pwm_capture_set_polarity(uint16_t polarity)
{
    uint32_t ctl = TIMER_CHCTL2(CTM_H759_ENC_PWM_TIMER);

    ctl &= (~(uint32_t)(TIMER_CHCTL2_CH2P | TIMER_CHCTL2_MCH2P));
    ctl |= ((uint32_t)polarity << 8U);
    TIMER_CHCTL2(CTM_H759_ENC_PWM_TIMER) = ctl;
}

static bool encoder_pwm_period_in_range(uint16_t period_ticks)
{
    return ((period_ticks >= CTM_H759_ENC_PWM_MIN_PERIOD_TICKS)
            && (period_ticks <= CTM_H759_ENC_PWM_MAX_PERIOD_TICKS));
}

static bool encoder_pwm_period_close(uint16_t period_ticks, uint16_t reference_ticks)
{
    uint16_t diff = (period_ticks > reference_ticks)
                        ? (uint16_t)(period_ticks - reference_ticks)
                        : (uint16_t)(reference_ticks - period_ticks);
    uint32_t tolerance = ((uint32_t)reference_ticks * CTM_H759_ENC_PWM_PERIOD_TOL_PCT) / 100U;

    if (tolerance < CTM_H759_ENC_PWM_MIN_HIGH_TICKS) {
        tolerance = CTM_H759_ENC_PWM_MIN_HIGH_TICKS;
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

    if ((high_ticks <= CTM_H759_ENC_PWM_MIN_HIGH_TICKS) || (high_ticks >= period_ticks)) {
        return (0U != mPwmRawPrevValid) ? mPwmRawPrev : Encoder.raw;
    }

    if ((period_ticks < CTM_H759_ENC_PWM_MIN_PERIOD_TICKS) || (period_ticks > CTM_H759_ENC_PWM_MAX_PERIOD_TICKS)) {
        return (0U != mPwmRawPrevValid) ? mPwmRawPrev : Encoder.raw;
    }

    int32_t raw = (int32_t) (((uint64_t) high_ticks * (uint64_t) CTM_H759_ENC_PWM_CPR) / period_ticks);

    if (raw >= (int32_t) CTM_H759_ENC_PWM_CPR) {
        raw = (int32_t) CTM_H759_ENC_PWM_CPR - 1;
    }

    if (0U != mPwmRawPrevValid) {
        int32_t delta = encoder_count_delta(raw, mPwmRawPrev);

        if (ABS(delta) > (int32_t) CTM_H759_ENC_PWM_MAX_STEP_COUNTS) {
            return mPwmRawPrev;
        }
    }

    mPwmRawPrev      = raw;
    mPwmRawPrevValid = 1U;

    return raw;
}
#else
void ENCODER_hw_init(void)
{
}
#endif

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
static inline void delay_100ns(void)
{
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

    spi_current_data_num_config(CTM_H759_ENC_SPI, 1U);
    spi_master_transfer_start(CTM_H759_ENC_SPI, SPI_TRANS_START);

    while (RESET == spi_i2s_flag_get(CTM_H759_ENC_SPI, SPI_FLAG_TP))
        ;
    spi_i2s_data_transmit(CTM_H759_ENC_SPI, tx_data);

    while (RESET == spi_i2s_flag_get(CTM_H759_ENC_SPI, SPI_FLAG_RP))
        ;
    rx_data = (uint16_t) spi_i2s_data_receive(CTM_H759_ENC_SPI);

    while (RESET == spi_i2s_flag_get(CTM_H759_ENC_SPI, SPI_FLAG_ET))
        ;
    spi_master_transfer_start(CTM_H759_ENC_SPI, SPI_TRANS_IDLE);
    spi_i2s_flag_clear(CTM_H759_ENC_SPI, SPI_FLAG_ET);

    return rx_data;
}

int32_t ENCODER_read(void)
{
    uint16_t data[2];
    uint16_t sample_data;

    ENC_NCS_RESET();
    data[0] = encoder_spi_transfer16(0x8300);
    ENC_NCS_SET();

    delay_100ns();

    ENC_NCS_RESET();
    data[1] = encoder_spi_transfer16(0x8400);
    ENC_NCS_SET();

    sample_data = ((data[0] & 0x00FF) << 8) | (data[1] & 0x00FF);

    return (sample_data >> 2);
}
#else
int32_t ENCODER_read(void)
{
    return encoder_pwm_read();
}
#endif

bool ENCODER_sample(void)
{
    Encoder.raw = ENCODER_read();
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    return (0U != Encoder.pwm_valid);
#else
    return true;
#endif
}

void ENCODER_loop(void)
{
    Encoder.raw = ENCODER_read();
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    static uint32_t pwm_log_tick = 0U;
    if (get_ms_since(pwm_log_tick) > 1000U) {
        pwm_log_tick = SystickCount;
        ENCODER_LOG("[ENC] pwm valid=%u raw=%d high=%u period=%u\n",
                    (unsigned) Encoder.pwm_valid,
                    Encoder.raw,
                    (unsigned) Encoder.pwm_high_cycles,
                    (unsigned) Encoder.pwm_period_cycles);
    }
#endif
    if (UsrConfig.encoder_dir == -1) {
        Encoder.raw = ENCODER_CPR - 1 - Encoder.raw;
    }

    /* Linearization */
    uint32_t lut_pos  = ((uint32_t) Encoder.raw * OFFSET_LUT_NUM);
    uint32_t lut_idx  = lut_pos / ENCODER_CPR;
    uint32_t lut_frac = lut_pos - (lut_idx * ENCODER_CPR);
    int off_1         = UsrConfig.offset_lut[lut_idx];
    int off_2         = UsrConfig.offset_lut[(lut_idx + 1U) % OFFSET_LUT_NUM];
    int off_interp    = off_1 + (int) (((int64_t) (off_2 - off_1) * (int64_t) lut_frac) / ENCODER_CPR);

    int count = Encoder.raw - off_interp - UsrConfig.encoder_offset;

    /*  Wrap in ENCODER_CPR */
    while (count >= ENCODER_CPR)
        count -= ENCODER_CPR;
    while (count < 0)
        count += ENCODER_CPR;

    Encoder.count_in_cpr = count;

    if (Encoder.need_init) {
        Encoder.need_init--;
        Encoder.count_in_cpr_prev = Encoder.count_in_cpr;
        return;
    }

    /* Delta count */
    int delta_count           = Encoder.count_in_cpr - Encoder.count_in_cpr_prev;
    Encoder.count_in_cpr_prev = Encoder.count_in_cpr;
    while (delta_count > +ENCODER_CPR_DIV)
        delta_count -= ENCODER_CPR;
    while (delta_count < -ENCODER_CPR_DIV)
        delta_count += ENCODER_CPR;

    // Run pll (for now pll is in units of encoder counts)
    // Predict current pos
    Encoder.pll_pos += CURRENT_MEASURE_PERIOD * Encoder.pll_vel;
    // Discrete phase detector
    float delta_pos = Encoder.count_in_cpr - floorf(Encoder.pll_pos);
    while (delta_pos > +ENCODER_CPR_DIV)
        delta_pos -= ENCODER_CPR_F;
    while (delta_pos < -ENCODER_CPR_DIV)
        delta_pos += ENCODER_CPR_F;
    // PLL feedback
    Encoder.pll_pos += CURRENT_MEASURE_PERIOD * Encoder.pll_kp * delta_pos;
    while (Encoder.pll_pos > ENCODER_CPR)
        Encoder.pll_pos -= ENCODER_CPR_F;
    while (Encoder.pll_pos < 0)
        Encoder.pll_pos += ENCODER_CPR_F;
    Encoder.pll_vel += CURRENT_MEASURE_PERIOD * Encoder.pll_ki * delta_pos;

    // Align delta-sigma on zero to prevent jitter
    if (ABS(Encoder.pll_vel) < Encoder.snap_threshold) {
        Encoder.pll_vel = 0.0f;
    }

    /* Outputs from Encoder for Controller */
    Encoder.shadow_count += delta_count;
    Encoder.pos       = Encoder.shadow_count / ENCODER_CPR_F;
    Encoder.vel       = Encoder.pll_vel / ENCODER_CPR_F;
    Encoder.phase     = (M_2PI * UsrConfig.motor_pole_pairs) * Encoder.count_in_cpr / ENCODER_CPR_F;
    Encoder.phase_vel = (M_2PI * UsrConfig.motor_pole_pairs) * Encoder.vel;
}
