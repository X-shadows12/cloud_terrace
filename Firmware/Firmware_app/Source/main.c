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
#include "anticogging.h"
#include "can.h"
#include "controller.h"
#include "encoder.h"
#include "foc.h"
#include "mc_task.h"
#include "pwm_curr.h"
#include "rtt_mem.h"
#include "usr_config.h"

volatile uint32_t SystickCount = 0;

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

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
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

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
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
    rcu_periph_clock_enable(RCU_TIMER0);
    rcu_periph_clock_enable(RCU_TIMER1);
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    rcu_periph_clock_enable(RCU_TIMER3);
#endif
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL2);

    /* enable phase-current ADC clock */
    rcu_periph_clock_enable(CTM_H759_PHASE_ADC_RCU);
    rcu_adc_clock_config(CTM_H759_PHASE_ADC_IDX, RCU_ADCSRC_PER);

#if CTM_H759_HAS_VBUS_ADC
    /* enable DC-bus voltage ADC clock */
    rcu_periph_clock_enable(CTM_H759_VBUS_ADC_RCU);
    rcu_adc_clock_config(CTM_H759_VBUS_ADC_IDX, RCU_ADCSRC_PER);
#endif

    /* enable trigger selector */
    rcu_periph_clock_enable(RCU_TRIGSEL);

    /* enable CAN2 clock */
    rcu_periph_clock_enable(RCU_CAN2);
    rcu_can_clock_config(CTM_H759_CAN_IDX, RCU_CANSRC_APB2);

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
    /* enable SPI0 clock */
    rcu_periph_clock_enable(RCU_SPI0);
    rcu_spi_clock_config(IDX_SPI0, RCU_SPISRC_PLL0Q);
#endif
}

static void GPIO_init(void)
{
    /* User keys from the schematic: KEY1/PA0, KEY2/PC0. */
    gpio_input(CTM_H759_KEY1_PORT, CTM_H759_KEY1_PIN);
    gpio_input(CTM_H759_KEY2_PORT, CTM_H759_KEY2_PIN);

#if defined(CTM_H759_PHASE_A_ANALOG_SWITCH)
    syscfg_analog_switch_enable(CTM_H759_PHASE_A_ANALOG_SWITCH);
#endif
#if defined(CTM_H759_PHASE_B_ANALOG_SWITCH)
    syscfg_analog_switch_enable(CTM_H759_PHASE_B_ANALOG_SWITCH);
#endif

    /* Phase current ADC */
    gpio_analog(CTM_H759_PHASE_A_PORT, CTM_H759_PHASE_A_PIN);
    gpio_analog(CTM_H759_PHASE_B_PORT, CTM_H759_PHASE_B_PIN);

#if CTM_H759_HAS_VBUS_ADC
    /* DC-bus voltage ADC */
    gpio_analog(CTM_H759_VBUS_PORT, CTM_H759_VBUS_PIN);
#endif

    /* FOC PWM */
    gpio_af_pp(CTM_H759_PWM_CH0_PORT, CTM_H759_PWM_CH0_PIN, CTM_H759_PWM_AF);
    gpio_af_pp(CTM_H759_PWM_MCH0_PORT, CTM_H759_PWM_MCH0_PIN, CTM_H759_PWM_AF);
    gpio_af_pp(CTM_H759_PWM_CH1_PORT, CTM_H759_PWM_CH1_PIN, CTM_H759_PWM_AF);
    gpio_af_pp(CTM_H759_PWM_MCH1_PORT, CTM_H759_PWM_MCH1_PIN, CTM_H759_PWM_AF);
    gpio_af_pp(CTM_H759_PWM_CH2_PORT, CTM_H759_PWM_CH2_PIN, CTM_H759_PWM_AF);
    gpio_af_pp(CTM_H759_PWM_MCH2_PORT, CTM_H759_PWM_MCH2_PIN, CTM_H759_PWM_AF);

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
    /* SPI0 encoder / external SPI bus */
    gpio_af_pp(CTM_H759_SPI0_SCK_PORT, CTM_H759_SPI0_SCK_PIN, CTM_H759_SPI0_AF);
    gpio_af_pp(CTM_H759_SPI0_MISO_PORT, CTM_H759_SPI0_MISO_PIN, CTM_H759_SPI0_AF);
    gpio_af_pp(CTM_H759_SPI0_MOSI_PORT, CTM_H759_SPI0_MOSI_PIN, CTM_H759_SPI0_AF);

    gpio_bit_set(CTM_H759_SPI0_CS_X_PORT, CTM_H759_SPI0_CS_X_PIN);
    gpio_output_pp(CTM_H759_SPI0_CS_X_PORT, CTM_H759_SPI0_CS_X_PIN);
    gpio_bit_set(CTM_H759_SPI0_CS_R_PORT, CTM_H759_SPI0_CS_R_PIN);
    gpio_output_pp(CTM_H759_SPI0_CS_R_PORT, CTM_H759_SPI0_CS_R_PIN);
    gpio_bit_set(CTM_H759_SPI0_CS_L_PORT, CTM_H759_SPI0_CS_L_PIN);
    gpio_output_pp(CTM_H759_SPI0_CS_L_PORT, CTM_H759_SPI0_CS_L_PIN);
#elif (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    /* PWM encoder input on PB8 / TIMER3_CH2 / AF2 */
    gpio_af_input(CTM_H759_ENC_PWM_PORT, CTM_H759_ENC_PWM_PIN, CTM_H759_ENC_PWM_AF,
                  GPIO_PUPD_PULLUP);
#endif

    /* CAN2 */
    gpio_af_pp(CTM_H759_CAN_RX_PORT, CTM_H759_CAN_RX_PIN, CTM_H759_CAN_RX_AF);
    gpio_af_pp(CTM_H759_CAN_TX_PORT, CTM_H759_CAN_TX_PIN, CTM_H759_CAN_TX_AF);
}

static void SPI0_init(void)
{
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
    spi_parameter_struct spi_init_struct;

    spi_i2s_deinit(CTM_H759_ENC_SPI);
    spi_struct_para_init(&spi_init_struct);

    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.data_size            = SPI_DATASIZE_16BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    spi_init_struct.prescale             = SPI_PSC_64;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;
    spi_init(CTM_H759_ENC_SPI, &spi_init_struct);

    spi_word_access_enable(CTM_H759_ENC_SPI);
    spi_enable(CTM_H759_ENC_SPI);
#endif
}

static void ADC_PHASE_init(void)
{
    adc_deinit(CTM_H759_PHASE_ADC);

    adc_clock_config(CTM_H759_PHASE_ADC, ADC_CLK_SYNC_HCLK_DIV6);
    adc_special_function_config(CTM_H759_PHASE_ADC, ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(CTM_H759_PHASE_ADC, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(CTM_H759_PHASE_ADC, ADC_INSERTED_CHANNEL_AUTO, DISABLE);
    adc_resolution_config(CTM_H759_PHASE_ADC, ADC_RESOLUTION_12B);
    adc_data_alignment_config(CTM_H759_PHASE_ADC, ADC_DATAALIGN_RIGHT);

    adc_channel_length_config(CTM_H759_PHASE_ADC, ADC_INSERTED_CHANNEL, 2U);
    adc_inserted_channel_config(CTM_H759_PHASE_ADC, 0U, CTM_H759_PHASE_A_ADC_CHANNEL,
                                CTM_H759_PHASE_ADC_SAMPLE_TIME);
    adc_inserted_channel_config(CTM_H759_PHASE_ADC, 1U, CTM_H759_PHASE_B_ADC_CHANNEL,
                                CTM_H759_PHASE_ADC_SAMPLE_TIME);

    adc_external_trigger_config(CTM_H759_PHASE_ADC, ADC_INSERTED_CHANNEL, EXTERNAL_TRIGGER_RISING);
    trigsel_init(CTM_H759_PHASE_TRIGGER_OUTPUT, CTM_H759_PHASE_TRIGGER_INPUT);

    adc_interrupt_flag_clear(CTM_H759_PHASE_ADC, ADC_INT_FLAG_EOC);
    adc_interrupt_flag_clear(CTM_H759_PHASE_ADC, ADC_INT_FLAG_EOIC);
}

static void ADC_VBUS_init(void)
{
#if CTM_H759_HAS_VBUS_ADC
    adc_deinit(CTM_H759_VBUS_ADC);

    adc_clock_config(CTM_H759_VBUS_ADC, ADC_CLK_SYNC_HCLK_DIV6);
    adc_special_function_config(CTM_H759_VBUS_ADC, ADC_SCAN_MODE, DISABLE);
    adc_special_function_config(CTM_H759_VBUS_ADC, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(CTM_H759_VBUS_ADC, ADC_INSERTED_CHANNEL_AUTO, DISABLE);
    adc_resolution_config(CTM_H759_VBUS_ADC, ADC_RESOLUTION_12B);
    adc_data_alignment_config(CTM_H759_VBUS_ADC, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(CTM_H759_VBUS_ADC, ADC_REGULAR_CHANNEL, 1U);
    adc_regular_channel_config(CTM_H759_VBUS_ADC, 0U, CTM_H759_VBUS_ADC_CHANNEL,
                               CTM_H759_VBUS_ADC_SAMPLE_TIME);
    adc_external_trigger_config(CTM_H759_VBUS_ADC, ADC_REGULAR_CHANNEL, EXTERNAL_TRIGGER_DISABLE);
    adc_interrupt_flag_clear(CTM_H759_VBUS_ADC, ADC_INT_FLAG_EOC);
    adc_interrupt_flag_clear(CTM_H759_VBUS_ADC, ADC_INT_FLAG_ROVF);

    adc_enable(CTM_H759_VBUS_ADC);
    delay_ms(1);
    adc_calibration_mode_config(CTM_H759_VBUS_ADC, ADC_CALIBRATION_OFFSET);
    adc_calibration_number(CTM_H759_VBUS_ADC, ADC_CALIBRATION_NUM1);
    adc_calibration_enable(CTM_H759_VBUS_ADC);

    adc_flag_clear(CTM_H759_VBUS_ADC, ADC_FLAG_EOC);
    adc_flag_clear(CTM_H759_VBUS_ADC, ADC_FLAG_ROVF);
    adc_software_trigger_enable(CTM_H759_VBUS_ADC, ADC_REGULAR_CHANNEL);

    for (uint32_t timeout = 0U; timeout < 100000U; timeout++) {
        if (SET == adc_flag_get(CTM_H759_VBUS_ADC, ADC_FLAG_EOC)) {
            adc_buff[0] = (uint16_t) adc_regular_data_read(CTM_H759_VBUS_ADC);
            adc_flag_clear(CTM_H759_VBUS_ADC, ADC_FLAG_EOC);
            break;
        }
    }

    adc_software_trigger_enable(CTM_H759_VBUS_ADC, ADC_REGULAR_CHANNEL);
#endif
}

static void TIMER0_init(void)
{
    timer_parameter_struct       timer_initpara;
    timer_oc_parameter_struct    timer_ocinitpara;
    timer_break_parameter_struct timer_breakpara;

    timer_deinit(CTM_H759_PWM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = 0;
    timer_initpara.alignedmode       = TIMER_COUNTER_CENTER_UP;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = HALF_PWM_PERIOD_CYCLES;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(CTM_H759_PWM_TIMER, &timer_initpara);

    timer_channel_output_struct_para_init(&timer_ocinitpara);
    timer_ocinitpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocinitpara.outputnstate = TIMER_CCXN_ENABLE;
    timer_ocinitpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocinitpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocinitpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_ocinitpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;
    timer_channel_output_config(CTM_H759_PWM_TIMER, TIMER_CH_0, &timer_ocinitpara);
    timer_channel_output_config(CTM_H759_PWM_TIMER, TIMER_CH_1, &timer_ocinitpara);
    timer_channel_output_config(CTM_H759_PWM_TIMER, TIMER_CH_2, &timer_ocinitpara);
    timer_channel_output_config(CTM_H759_PWM_TIMER, TIMER_CH_3, &timer_ocinitpara);

    timer_channel_output_pulse_value_config(CTM_H759_PWM_TIMER, TIMER_CH_0, 0);
    timer_channel_output_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_0, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(CTM_H759_PWM_TIMER, TIMER_CH_0, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_0, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(CTM_H759_PWM_TIMER, TIMER_CH_1, 0);
    timer_channel_output_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_1, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(CTM_H759_PWM_TIMER, TIMER_CH_1, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_1, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(CTM_H759_PWM_TIMER, TIMER_CH_2, 0);
    timer_channel_output_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_2, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(CTM_H759_PWM_TIMER, TIMER_CH_2, TIMER_OC_SHADOW_ENABLE);
    timer_multi_mode_channel_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_2, TIMER_MCH_MODE_COMPLEMENTARY);

    timer_channel_output_pulse_value_config(CTM_H759_PWM_TIMER, TIMER_CH_3, (HALF_PWM_PERIOD_CYCLES - 5U));
    timer_channel_output_mode_config(CTM_H759_PWM_TIMER, TIMER_CH_3, TIMER_OC_MODE_PWM1);
    timer_channel_output_shadow_config(CTM_H759_PWM_TIMER, TIMER_CH_3, TIMER_OC_SHADOW_DISABLE);

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
    timer_break_config(CTM_H759_PWM_TIMER, &timer_breakpara);
}

static void TIMER1_init(void)
{
    timer_parameter_struct timer_initpara;

    timer_deinit(TIMER1);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler        = 300U - 1U;
    timer_initpara.alignedmode      = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.period           = 1000U - 1U;
    timer_initpara.clockdivision    = TIMER_CKDIV_DIV1;
    timer_init(TIMER1, &timer_initpara);

    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);

    timer_enable(TIMER1);
}

static void LOCK_pins(void)
{
    gpio_pin_lock(CTM_H759_PHASE_A_PORT, CTM_H759_PHASE_A_PIN);
    gpio_pin_lock(CTM_H759_PHASE_B_PORT, CTM_H759_PHASE_B_PIN);

#if CTM_H759_HAS_VBUS_ADC
    gpio_pin_lock(CTM_H759_VBUS_PORT, CTM_H759_VBUS_PIN);
#endif

    gpio_pin_lock(CTM_H759_PWM_CH0_PORT, CTM_H759_PWM_CH0_PIN);
    gpio_pin_lock(CTM_H759_PWM_MCH0_PORT, CTM_H759_PWM_MCH0_PIN);
    gpio_pin_lock(CTM_H759_PWM_CH1_PORT, CTM_H759_PWM_CH1_PIN);
    gpio_pin_lock(CTM_H759_PWM_MCH1_PORT, CTM_H759_PWM_MCH1_PIN);
    gpio_pin_lock(CTM_H759_PWM_CH2_PORT, CTM_H759_PWM_CH2_PIN);
    gpio_pin_lock(CTM_H759_PWM_MCH2_PORT, CTM_H759_PWM_MCH2_PIN);

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_SPI)
    gpio_pin_lock(CTM_H759_SPI0_SCK_PORT, CTM_H759_SPI0_SCK_PIN);
    gpio_pin_lock(CTM_H759_SPI0_MISO_PORT, CTM_H759_SPI0_MISO_PIN);
    gpio_pin_lock(CTM_H759_SPI0_MOSI_PORT, CTM_H759_SPI0_MOSI_PIN);
    gpio_pin_lock(CTM_H759_ENC_CS_PORT, CTM_H759_ENC_CS_PIN);
#else
    gpio_pin_lock(CTM_H759_ENC_PWM_PORT, CTM_H759_ENC_PWM_PIN);
#endif

    gpio_pin_lock(CTM_H759_CAN_RX_PORT, CTM_H759_CAN_RX_PIN);
    gpio_pin_lock(CTM_H759_CAN_TX_PORT, CTM_H759_CAN_TX_PIN);
}

static void NVIC_init(void)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    nvic_irq_enable(CTM_H759_PHASE_ADC_IRQ, 0U, 0U);
    nvic_irq_enable(TIMER1_IRQn, 1U, 0U);
    nvic_irq_enable(CTM_H759_CAN_IRQ, 2U, 0U);
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
    nvic_irq_enable(CTM_H759_ENC_PWM_TIMER_IRQ, 3U, 0U);
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

int main(void)
{
    __disable_irq();

    rtt_mpu_enable();
    RTT_init();
    cache_enable();
    RCU_init();
    GPIO_init();
    SPI0_init();
    ENCODER_hw_init();
    ADC_PHASE_init();
    ADC_VBUS_init();
    TIMER0_init();
    TIMER1_init();
    LOCK_pins();
    NVIC_init();
    WATCH_DOG_init();

    if (USR_CONFIG_read_config()) {
        USR_CONFIG_set_default_config();
    }

    if (0 == USR_CONFIG_read_cogging_map()) {
        AnticoggingValid = true;
    } else {
        USR_CONFIG_set_default_cogging_map();
    }

    CAN_set_node_id(UsrConfig.node_id);
    CAN_hw_init(UsrConfig.can_baudrate);

    MCT_init();
    FOC_init();
    PWMC_init();
    ENCODER_init();
    CONTROLLER_init();

    fwdgt_enable();
    __enable_irq();

    /* wait voltage stable */
    for (uint8_t i = 0, j = 0; i < 250; i++) {
        if (Foc.v_bus_filt > 20) {
            if (++j > 20) {
                break;
            }
        }
        delay_ms(2);
    }

    if (PWMC_CurrentReadingPolarization() != 0) {
        StatuswordNew.errors.selftest = 1;
    }

    MCT_set_state(IDLE);

    uint32_t tick = 0;

    while (1) {
        MCT_low_priority_task();

        if (get_ms_since(tick) > 1000) {
            tick = SystickCount;
        }
    }
}

void Error_Handler(void)
{
    __disable_irq();

    timer_primary_output_config(CTM_H759_PWM_TIMER, DISABLE);

    while (1) {
    }
}

void delay_ms(const uint16_t ms)
{
    volatile uint32_t i = (SystemCoreClock / 7000U) * (uint32_t) ms;
    while (i-- > 0U) {
        __NOP();
    }
}
