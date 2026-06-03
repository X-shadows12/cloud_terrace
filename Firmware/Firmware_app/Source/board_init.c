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

#include "board_init.h"
#include "board_port.h"
#include "gd32h7xx_syscfg.h"
#include "gd32h7xx_trigsel.h"
#include "encoder_hw.h"
#include "pwm_curr_hw.h"
#include "rtt_mem.h"
#include "runtime.h"

static void cache_enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

static void rtt_mpu_enable(void)
{
    const uint32_t rtt_mpu_region = 0U;
    const uint32_t rtt_mpu_rasr =
        (1UL << MPU_RASR_XN_Pos) |
        (ARM_MPU_AP_FULL << MPU_RASR_AP_Pos) |
        (1UL << MPU_RASR_TEX_Pos) |
        (1UL << MPU_RASR_S_Pos) |
        (ARM_MPU_REGION_SIZE_16KB << MPU_RASR_SIZE_Pos) |
        MPU_RASR_ENABLE_Msk;

    /* Keep the RTT window non-cacheable so the probe and CPU see the same bytes. */
    ARM_MPU_Disable();
    ARM_MPU_SetRegionEx(rtt_mpu_region, ARM_MPU_RBAR(rtt_mpu_region, RTT_RAM_BASE), rtt_mpu_rasr);
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
static void gpio_output_pp(uint32_t port, uint32_t pin)
{
    gpio_mode_set(port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, pin);
    gpio_output_options_set(port, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, pin);
}
#endif

static void gpio_af_pp(uint32_t port, uint32_t pin, uint32_t af)
{
    gpio_af_set(port, af, pin);
    gpio_mode_set(port, GPIO_MODE_AF, GPIO_PUPD_NONE, pin);
    gpio_output_options_set(port, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, pin);
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
static void gpio_af_input(uint32_t port, uint32_t pin, uint32_t af, uint32_t pupd)
{
    gpio_af_set(port, af, pin);
    gpio_mode_set(port, GPIO_MODE_AF, pupd, pin);
}
#endif

static void gpio_analog(uint32_t port, uint32_t pin)
{
    gpio_mode_set(port, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, pin);
}

static void gpio_input(uint32_t port, uint32_t pin)
{
    gpio_mode_set(port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, pin);
}

static void RCU_init(void)
{
    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOF);
    rcu_periph_clock_enable(RCU_GPIOG);
    rcu_periph_clock_enable(RCU_GPIOJ);
    rcu_periph_clock_enable(RCU_GPIOK);

    rcu_periph_clock_enable(RCU_SYSCFG);

    /* enable TIMER clock */
    rcu_periph_clock_enable(BOARD_LEFT_PWM_TIMER_RCU);
    rcu_periph_clock_enable(BOARD_RIGHT_PWM_TIMER_RCU);
    rcu_periph_clock_enable(BOARD_SYSTEM_TIMER_RCU);
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    rcu_periph_clock_enable(BOARD_ENC_PWM_TIMER_RCU);
#endif
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    /* enable phase-current ADC clock */
    rcu_periph_clock_enable(BOARD_LEFT_PHASE_ADC_RCU);
    rcu_periph_clock_enable(BOARD_RIGHT_PHASE_ADC_RCU);
    rcu_adc_clock_config(BOARD_LEFT_PHASE_ADC_IDX, RCU_ADCSRC_PER);
    rcu_adc_clock_config(BOARD_RIGHT_PHASE_ADC_IDX, RCU_ADCSRC_PER);

#if BOARD_HAS_VBUS_ADC
    /* enable DC-bus voltage ADC clock */
    rcu_periph_clock_enable(BOARD_VBUS_ADC_RCU);
    rcu_adc_clock_config(BOARD_VBUS_ADC_IDX, RCU_ADCSRC_PER);
#endif

    /* enable trigger selector */
    rcu_periph_clock_enable(RCU_TRIGSEL);

    /* enable CAN2 clock */
    rcu_periph_clock_enable(BOARD_CAN_RCU);
    rcu_can_clock_config(BOARD_CAN_IDX, BOARD_CAN_CLK_SOURCE);

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
    /* enable SPI0 clock */
    rcu_periph_clock_enable(BOARD_ENC_SPI_RCU);
    rcu_spi_clock_config(BOARD_ENC_SPI_IDX, BOARD_ENC_SPI_CLK_SOURCE);
#endif
}

static void GPIO_init(void)
{
    /* User keys from the schematic: KEY1/PA0, KEY2/PC0. */
    gpio_input(BOARD_KEY1_PORT, BOARD_KEY1_PIN);
    gpio_input(BOARD_KEY2_PORT, BOARD_KEY2_PIN);

    syscfg_analog_switch_enable(BOARD_LEFT_PHASE_A_ANALOG_SWITCH);
    syscfg_analog_switch_enable(BOARD_LEFT_PHASE_B_ANALOG_SWITCH);

    /* Phase current ADC */
    gpio_analog(BOARD_LEFT_PHASE_A_PORT, BOARD_LEFT_PHASE_A_PIN);
    gpio_analog(BOARD_LEFT_PHASE_B_PORT, BOARD_LEFT_PHASE_B_PIN);
    gpio_analog(BOARD_RIGHT_PHASE_A_PORT, BOARD_RIGHT_PHASE_A_PIN);
    gpio_analog(BOARD_RIGHT_PHASE_B_PORT, BOARD_RIGHT_PHASE_B_PIN);

#if BOARD_HAS_VBUS_ADC
    /* DC-bus voltage ADC */
    gpio_analog(BOARD_VBUS_PORT, BOARD_VBUS_PIN);
#endif

    /* FOC PWM */
    gpio_af_pp(BOARD_LEFT_PWM_CH0_PORT, BOARD_LEFT_PWM_CH0_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_LEFT_PWM_MCH0_PORT, BOARD_LEFT_PWM_MCH0_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_LEFT_PWM_CH1_PORT, BOARD_LEFT_PWM_CH1_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_LEFT_PWM_MCH1_PORT, BOARD_LEFT_PWM_MCH1_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_LEFT_PWM_CH2_PORT, BOARD_LEFT_PWM_CH2_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_LEFT_PWM_MCH2_PORT, BOARD_LEFT_PWM_MCH2_PIN, BOARD_LEFT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_CH0_PORT, BOARD_RIGHT_PWM_CH0_PIN, BOARD_RIGHT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_MCH0_PORT, BOARD_RIGHT_PWM_MCH0_PIN, BOARD_RIGHT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_CH1_PORT, BOARD_RIGHT_PWM_CH1_PIN, BOARD_RIGHT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_MCH1_PORT, BOARD_RIGHT_PWM_MCH1_PIN, BOARD_RIGHT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_CH2_PORT, BOARD_RIGHT_PWM_CH2_PIN, BOARD_RIGHT_PWM_AF);
    gpio_af_pp(BOARD_RIGHT_PWM_MCH2_PORT, BOARD_RIGHT_PWM_MCH2_PIN, BOARD_RIGHT_PWM_AF);

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
    /* SPI0 encoder / external SPI bus */
    gpio_af_pp(BOARD_SPI0_SCK_PORT, BOARD_SPI0_SCK_PIN, BOARD_SPI0_AF);
    gpio_af_pp(BOARD_SPI0_MISO_PORT, BOARD_SPI0_MISO_PIN, BOARD_SPI0_AF);
    gpio_af_pp(BOARD_SPI0_MOSI_PORT, BOARD_SPI0_MOSI_PIN, BOARD_SPI0_AF);

    gpio_bit_set(BOARD_SPI0_CS_X_PORT, BOARD_SPI0_CS_X_PIN);
    gpio_output_pp(BOARD_SPI0_CS_X_PORT, BOARD_SPI0_CS_X_PIN);
    gpio_bit_set(BOARD_SPI0_CS_R_PORT, BOARD_SPI0_CS_R_PIN);
    gpio_output_pp(BOARD_SPI0_CS_R_PORT, BOARD_SPI0_CS_R_PIN);
    gpio_bit_set(BOARD_SPI0_CS_L_PORT, BOARD_SPI0_CS_L_PIN);
    gpio_output_pp(BOARD_SPI0_CS_L_PORT, BOARD_SPI0_CS_L_PIN);
#elif (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    /* PWM encoder inputs: left PB8/TIMER3_CH2, right PB9/TIMER3_CH3. */
    gpio_af_input(BOARD_LEFT_ENC_PWM_PORT, BOARD_LEFT_ENC_PWM_PIN, BOARD_LEFT_ENC_PWM_AF,
                  GPIO_PUPD_PULLUP);
    gpio_af_input(BOARD_RIGHT_ENC_PWM_PORT, BOARD_RIGHT_ENC_PWM_PIN, BOARD_RIGHT_ENC_PWM_AF,
                  GPIO_PUPD_PULLUP);
#endif

    /* CAN2 */
    gpio_af_pp(BOARD_CAN_RX_PORT, BOARD_CAN_RX_PIN, BOARD_CAN_RX_AF);
    gpio_af_pp(BOARD_CAN_TX_PORT, BOARD_CAN_TX_PIN, BOARD_CAN_TX_AF);
}

static void SPI0_init(void)
{
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
    spi_parameter_struct spi_init_struct;

    spi_i2s_deinit(BOARD_ENC_SPI);
    spi_struct_para_init(&spi_init_struct);

    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.data_size            = SPI_DATASIZE_16BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_PSC_64;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(BOARD_ENC_SPI, &spi_init_struct);

    spi_word_access_enable(BOARD_ENC_SPI);
    spi_enable(BOARD_ENC_SPI);
#endif
}

static void ADC_PHASE_init_axis(uint32_t adc_periph, uint8_t channel_a, uint8_t channel_b,
                                trigsel_periph_enum trigger_output,
                                trigsel_source_enum trigger_input)
{
    adc_deinit(adc_periph);

    adc_clock_config(adc_periph, ADC_CLK_SYNC_HCLK_DIV6);
    adc_special_function_config(adc_periph, ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(adc_periph, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(adc_periph, ADC_INSERTED_CHANNEL_AUTO, DISABLE);
    adc_resolution_config(adc_periph, ADC_RESOLUTION_12B);
    adc_data_alignment_config(adc_periph, ADC_DATAALIGN_RIGHT);

    adc_channel_length_config(adc_periph, ADC_INSERTED_CHANNEL, 2U);
    adc_inserted_channel_config(adc_periph, 0U, channel_a, BOARD_PHASE_ADC_SAMPLE_TIME);
    adc_inserted_channel_config(adc_periph, 1U, channel_b, BOARD_PHASE_ADC_SAMPLE_TIME);

    adc_external_trigger_config(adc_periph, ADC_INSERTED_CHANNEL, EXTERNAL_TRIGGER_RISING);
    trigsel_init(trigger_output, trigger_input);

    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_EOC);
    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_EOIC);
}

static void ADC_PHASE_init(void)
{
    ADC_PHASE_init_axis(BOARD_LEFT_PHASE_ADC,
                        BOARD_LEFT_PHASE_A_ADC_CHANNEL,
                        BOARD_LEFT_PHASE_B_ADC_CHANNEL,
                        BOARD_LEFT_PHASE_TRIGGER_OUTPUT,
                        BOARD_LEFT_PHASE_TRIGGER_INPUT);
    ADC_PHASE_init_axis(BOARD_RIGHT_PHASE_ADC,
                        BOARD_RIGHT_PHASE_A_ADC_CHANNEL,
                        BOARD_RIGHT_PHASE_B_ADC_CHANNEL,
                        BOARD_RIGHT_PHASE_TRIGGER_OUTPUT,
                        BOARD_RIGHT_PHASE_TRIGGER_INPUT);
}

static void ADC_VBUS_init(void)
{
#if BOARD_HAS_VBUS_ADC
    adc_deinit(BOARD_VBUS_ADC);

    adc_clock_config(BOARD_VBUS_ADC, ADC_CLK_SYNC_HCLK_DIV6);
    adc_special_function_config(BOARD_VBUS_ADC, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(BOARD_VBUS_ADC, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(BOARD_VBUS_ADC, ADC_INSERTED_CHANNEL_AUTO, DISABLE);
    adc_resolution_config(BOARD_VBUS_ADC, ADC_RESOLUTION_12B);
    adc_data_alignment_config(BOARD_VBUS_ADC, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(BOARD_VBUS_ADC, ADC_REGULAR_CHANNEL, 1U);
    adc_regular_channel_config(BOARD_VBUS_ADC, 0U, BOARD_VBUS_ADC_CHANNEL,
                               BOARD_VBUS_ADC_SAMPLE_TIME);
    adc_external_trigger_config(BOARD_VBUS_ADC, ADC_REGULAR_CHANNEL, EXTERNAL_TRIGGER_DISABLE);
    adc_interrupt_flag_clear(BOARD_VBUS_ADC, ADC_INT_FLAG_EOC);
    adc_interrupt_flag_clear(BOARD_VBUS_ADC, ADC_INT_FLAG_ROVF);

    adc_enable(BOARD_VBUS_ADC);
    delay_ms(1);
    adc_calibration_mode_config(BOARD_VBUS_ADC, ADC_CALIBRATION_OFFSET);
    adc_calibration_number(BOARD_VBUS_ADC, ADC_CALIBRATION_NUM1);
    adc_calibration_enable(BOARD_VBUS_ADC);

    adc_flag_clear(BOARD_VBUS_ADC, ADC_FLAG_EOC);
    adc_flag_clear(BOARD_VBUS_ADC, ADC_FLAG_ROVF);
    adc_software_trigger_enable(BOARD_VBUS_ADC, ADC_REGULAR_CHANNEL);

    for (uint32_t timeout = 0U; timeout < 100000U; timeout++) {
        if (SET == adc_flag_get(BOARD_VBUS_ADC, ADC_FLAG_EOC)) {
            adc_buff[0] = (uint16_t) adc_regular_data_read(BOARD_VBUS_ADC);
            adc_flag_clear(BOARD_VBUS_ADC, ADC_FLAG_EOC);
            break;
        }
    }

    adc_software_trigger_enable(BOARD_VBUS_ADC, ADC_REGULAR_CHANNEL);
#endif
}

static void PWM_TIMER_init_axis(uint32_t timer_periph)
{
    timer_parameter_struct       timer_initpara;
    timer_oc_parameter_struct    timer_ocinitpara;
    timer_break_parameter_struct timer_breakpara;

    timer_deinit(timer_periph);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = 0;
    timer_initpara.alignedmode       = TIMER_COUNTER_CENTER_UP;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = HALF_PWM_PERIOD_CYCLES;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(timer_periph, &timer_initpara);

    timer_channel_output_struct_para_init(&timer_ocinitpara);
    timer_ocinitpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocinitpara.outputnstate = TIMER_CCXN_ENABLE;
    timer_ocinitpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocinitpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocinitpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_ocinitpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;
    timer_channel_output_config(timer_periph, TIMER_CH_0, &timer_ocinitpara);
    timer_channel_output_config(timer_periph, TIMER_CH_1, &timer_ocinitpara);
    timer_channel_output_config(timer_periph, TIMER_CH_2, &timer_ocinitpara);
    timer_channel_output_config(timer_periph, TIMER_CH_3, &timer_ocinitpara);

    timer_channel_output_pulse_value_config(timer_periph, TIMER_CH_0, 0);
    timer_channel_output_mode_config(timer_periph, TIMER_CH_0, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer_periph, TIMER_CH_0, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(timer_periph, TIMER_CH_0, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(timer_periph, TIMER_CH_1, 0);
    timer_channel_output_mode_config(timer_periph, TIMER_CH_1, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer_periph, TIMER_CH_1, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(timer_periph, TIMER_CH_1, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(timer_periph, TIMER_CH_2, 0);
    timer_channel_output_mode_config(timer_periph, TIMER_CH_2, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer_periph, TIMER_CH_2, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(timer_periph, TIMER_CH_2, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(timer_periph, TIMER_CH_3, (HALF_PWM_PERIOD_CYCLES - 5U));
    timer_channel_output_mode_config(timer_periph, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    timer_channel_output_shadow_config(timer_periph, TIMER_CH_3, TIMER_OC_SHADOW_DISABLE);

    timer_break_struct_para_init(&timer_breakpara);
    timer_breakpara.runoffstate     = TIMER_ROS_STATE_DISABLE;
    timer_breakpara.ideloffstate    = TIMER_IOS_STATE_DISABLE;
    timer_breakpara.deadtime        = 0U;
    timer_breakpara.outputautostate = TIMER_OUTAUTO_DISABLE;
    timer_breakpara.protectmode     = TIMER_CCHP_PROT_OFF;
    timer_breakpara.break0state     = TIMER_BREAK0_DISABLE;
    timer_breakpara.break0polarity  = TIMER_BREAK0_POLARITY_LOW;
    timer_breakpara.break0lock      = TIMER_BREAK0_LK_DISABLE;
    timer_breakpara.break0release   = TIMER_BREAK0_UNRELEASE;
    timer_breakpara.break1state     = TIMER_BREAK1_DISABLE;
    timer_breakpara.break1polarity  = TIMER_BREAK1_POLARITY_LOW;
    timer_breakpara.break1lock      = TIMER_BREAK1_LK_DISABLE;
    timer_breakpara.break1release   = TIMER_BREAK1_UNRELEASE;
    timer_break_config(timer_periph, &timer_breakpara);

    timer_channel_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCX_DISABLE);
    timer_channel_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCX_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_0, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_1, TIMER_CCXN_DISABLE);
    timer_channel_complementary_output_state_config(timer_periph, TIMER_CH_2, TIMER_CCXN_DISABLE);
    timer_primary_output_config(timer_periph, DISABLE);
}

static void PWM_TIMER_init(void)
{
    PWM_TIMER_init_axis(BOARD_LEFT_PWM_TIMER);
    PWM_TIMER_init_axis(BOARD_RIGHT_PWM_TIMER);
}

static void SYSTEM_TIMER_init(void)
{
    timer_parameter_struct timer_initpara;

    timer_deinit(BOARD_SYSTEM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler        = 300U - 1U;
    timer_initpara.alignedmode      = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.period           = 1000U - 1U;
    timer_initpara.clockdivision    = TIMER_CKDIV_DIV1;
    timer_init(BOARD_SYSTEM_TIMER, &timer_initpara);

    timer_interrupt_flag_clear(BOARD_SYSTEM_TIMER, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(BOARD_SYSTEM_TIMER, TIMER_INT_UP);

    timer_enable(BOARD_SYSTEM_TIMER);
}

static void LOCK_pins(void)
{
    gpio_pin_lock(BOARD_LEFT_PHASE_A_PORT, BOARD_LEFT_PHASE_A_PIN);
    gpio_pin_lock(BOARD_LEFT_PHASE_B_PORT, BOARD_LEFT_PHASE_B_PIN);
    gpio_pin_lock(BOARD_RIGHT_PHASE_A_PORT, BOARD_RIGHT_PHASE_A_PIN);
    gpio_pin_lock(BOARD_RIGHT_PHASE_B_PORT, BOARD_RIGHT_PHASE_B_PIN);

#if BOARD_HAS_VBUS_ADC
    gpio_pin_lock(BOARD_VBUS_PORT, BOARD_VBUS_PIN);
#endif

    gpio_pin_lock(BOARD_LEFT_PWM_CH0_PORT, BOARD_LEFT_PWM_CH0_PIN);
    gpio_pin_lock(BOARD_LEFT_PWM_MCH0_PORT, BOARD_LEFT_PWM_MCH0_PIN);
    gpio_pin_lock(BOARD_LEFT_PWM_CH1_PORT, BOARD_LEFT_PWM_CH1_PIN);
    gpio_pin_lock(BOARD_LEFT_PWM_MCH1_PORT, BOARD_LEFT_PWM_MCH1_PIN);
    gpio_pin_lock(BOARD_LEFT_PWM_CH2_PORT, BOARD_LEFT_PWM_CH2_PIN);
    gpio_pin_lock(BOARD_LEFT_PWM_MCH2_PORT, BOARD_LEFT_PWM_MCH2_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_CH0_PORT, BOARD_RIGHT_PWM_CH0_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_MCH0_PORT, BOARD_RIGHT_PWM_MCH0_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_CH1_PORT, BOARD_RIGHT_PWM_CH1_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_MCH1_PORT, BOARD_RIGHT_PWM_MCH1_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_CH2_PORT, BOARD_RIGHT_PWM_CH2_PIN);
    gpio_pin_lock(BOARD_RIGHT_PWM_MCH2_PORT, BOARD_RIGHT_PWM_MCH2_PIN);

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_SPI)
    gpio_pin_lock(BOARD_SPI0_SCK_PORT, BOARD_SPI0_SCK_PIN);
    gpio_pin_lock(BOARD_SPI0_MISO_PORT, BOARD_SPI0_MISO_PIN);
    gpio_pin_lock(BOARD_SPI0_MOSI_PORT, BOARD_SPI0_MOSI_PIN);
    gpio_pin_lock(BOARD_ENC_CS_PORT, BOARD_ENC_CS_PIN);
#else
    gpio_pin_lock(BOARD_LEFT_ENC_PWM_PORT, BOARD_LEFT_ENC_PWM_PIN);
    gpio_pin_lock(BOARD_RIGHT_ENC_PWM_PORT, BOARD_RIGHT_ENC_PWM_PIN);
#endif

    gpio_pin_lock(BOARD_CAN_RX_PORT, BOARD_CAN_RX_PIN);
    gpio_pin_lock(BOARD_CAN_TX_PORT, BOARD_CAN_TX_PIN);
}

static void NVIC_init(void)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
    nvic_irq_enable(BOARD_LEFT_PHASE_ADC_IRQ, 0U, 0U);
    nvic_irq_enable(BOARD_RIGHT_PHASE_ADC_IRQ, 0U, 0U);
#else
    nvic_irq_enable(BOARD_PHASE_ADC_IRQ, 0U, 0U);
#endif
    nvic_irq_enable(BOARD_SYSTEM_TIMER_IRQ, 1U, 0U);
    nvic_irq_enable(BOARD_CAN_IRQ, 2U, 0U);
#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
    nvic_irq_enable(BOARD_ENC_PWM_TIMER_IRQ, 3U, 0U);
#endif
}

static void WATCH_DOG_init(void)
{
    rcu_osci_on(RCU_IRC32K);

    while (SUCCESS != rcu_osci_stab_wait(RCU_IRC32K)) {
    }

    /* Keep enough margin for H7 flash sector erase/program during DFU boot copy. */
    fwdgt_config(156U, FWDGT_PSC_DIV256);
}

void BOARD_interrupts_disable(void)
{
    __disable_irq();
}

void BOARD_interrupts_enable(void)
{
    __enable_irq();
}

void BOARD_init_debug_memory(void)
{
    rtt_mpu_enable();
}

void BOARD_enable_cache(void)
{
    cache_enable();
}

void BOARD_init_peripherals(void)
{
    RCU_init();
    GPIO_init();
    SPI0_init();
    ENCODER_hw_init();
    ADC_PHASE_init();
    ADC_VBUS_init();
    PWM_TIMER_init();
    SYSTEM_TIMER_init();
    LOCK_pins();
    NVIC_init();
}

void BOARD_init_watchdog(void)
{
    WATCH_DOG_init();
}

void BOARD_enable_watchdog(void)
{
    fwdgt_enable();
}

void BOARD_emergency_shutdown(void)
{
    __disable_irq();
    timer_primary_output_config(BOARD_LEFT_PWM_TIMER, DISABLE);
    timer_primary_output_config(BOARD_RIGHT_PWM_TIMER, DISABLE);
}
