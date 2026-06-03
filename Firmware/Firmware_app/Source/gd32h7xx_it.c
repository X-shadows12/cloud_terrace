/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
*/

#include "gd32h7xx_it.h"
#include "board_port.h"
#include "can.h"
#include "encoder_hw.h"
#include "runtime.h"
#include "mc_task.h"

void NMI_Handler(void)
{
    Error_Handler();
}

void HardFault_Handler(void)
{
    Error_Handler();
}

void MemManage_Handler(void)
{
    Error_Handler();
}

void BusFault_Handler(void)
{
    Error_Handler();
}

void UsageFault_Handler(void)
{
    Error_Handler();
}

void SVC_Handler(void)
{
    Error_Handler();
}

void DebugMon_Handler(void)
{
    Error_Handler();
}

void PendSV_Handler(void)
{
    Error_Handler();
}

static void phase_adc_irq_handler(uint32_t adc_periph, uint8_t run_control_loop)
{
    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_EOIC);
    if (run_control_loop != 0U) {
        MCT_high_frequency_task();
    }
}

#if BOARD_ENABLE_DUAL_MOTOR_CONTROL
void BOARD_LEFT_PHASE_ADC_IRQHandler(void)
{
    phase_adc_irq_handler(BOARD_LEFT_PHASE_ADC, 1U);
}

void BOARD_RIGHT_PHASE_ADC_IRQHandler(void)
{
    phase_adc_irq_handler(BOARD_RIGHT_PHASE_ADC, 0U);
}
#else
void BOARD_PHASE_ADC_IRQHandler(void)
{
    phase_adc_irq_handler(BOARD_PHASE_ADC, 1U);
}
#endif

void BOARD_SYSTEM_TIMER_IRQHandler(void)
{
    timer_interrupt_flag_clear(BOARD_SYSTEM_TIMER, TIMER_INT_FLAG_UP);

    MCT_safety_task();

    SystickCount++;
}

void BOARD_CAN_IRQHandler(void)
{
    if (RESET != can_interrupt_flag_get(BOARD_CAN, CAN_INT_FLAG_MB0)) {
        CAN_receive_callback();
    }
}

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
static void encoder_pwm_handle_capture(motor_hw_axis_t axis,
                                       uint32_t port,
                                       uint32_t pin,
                                       uint16_t channel,
                                       uint32_t int_flag,
                                       uint32_t overrun_flag)
{
    if (SET != timer_interrupt_flag_get(BOARD_ENC_PWM_TIMER, int_flag)) {
        return;
    }

    if (SET == timer_flag_get(BOARD_ENC_PWM_TIMER, overrun_flag)) {
        timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, int_flag);
        timer_flag_clear(BOARD_ENC_PWM_TIMER, overrun_flag);
        ENCODER_pwm_capture_overrun_callback(axis);
    } else {
        uint16_t capture = (uint16_t) timer_channel_capture_value_register_read(BOARD_ENC_PWM_TIMER, channel);
        uint8_t signal_high = (RESET != gpio_input_bit_get(port, pin)) ? 1U : 0U;

        timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, int_flag);
        ENCODER_pwm_capture_callback(axis, capture, signal_high);
    }
}

void TIMER3_IRQHandler(void)
{
    encoder_pwm_handle_capture(MOTOR_HW_AXIS_LEFT,
                               BOARD_LEFT_ENC_PWM_PORT,
                               BOARD_LEFT_ENC_PWM_PIN,
                               BOARD_LEFT_ENC_PWM_TIMER_CH,
                               BOARD_LEFT_ENC_PWM_TIMER_FLAG,
                               BOARD_LEFT_ENC_PWM_TIMER_OV_FLAG);
    encoder_pwm_handle_capture(MOTOR_HW_AXIS_RIGHT,
                               BOARD_RIGHT_ENC_PWM_PORT,
                               BOARD_RIGHT_ENC_PWM_PIN,
                               BOARD_RIGHT_ENC_PWM_TIMER_CH,
                               BOARD_RIGHT_ENC_PWM_TIMER_FLAG,
                               BOARD_RIGHT_ENC_PWM_TIMER_OV_FLAG);
}
#endif
