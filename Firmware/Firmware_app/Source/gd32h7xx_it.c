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

void BOARD_PHASE_ADC_IRQHandler(void)
{
    adc_interrupt_flag_clear(BOARD_PHASE_ADC, ADC_INT_FLAG_EOIC);
    MCT_high_frequency_task();
}

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
void TIMER3_IRQHandler(void)
{
    if (SET != timer_interrupt_flag_get(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_FLAG)) {
        return;
    }

    if (SET == timer_flag_get(BOARD_ENC_PWM_TIMER, TIMER_FLAG_CH2O)) {
        timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_FLAG);
        timer_flag_clear(BOARD_ENC_PWM_TIMER, TIMER_FLAG_CH2O);
        ENCODER_pwm_capture_overrun_callback();
        return;
    }

    uint16_t capture = (uint16_t) timer_channel_capture_value_register_read(BOARD_ENC_PWM_TIMER,
                                                                            BOARD_ENC_PWM_TIMER_CH);

    timer_interrupt_flag_clear(BOARD_ENC_PWM_TIMER, BOARD_ENC_PWM_TIMER_FLAG);

    ENCODER_pwm_capture_callback(capture);
}
#endif
