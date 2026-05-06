/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
*/

#include "gd32h7xx_it.h"
#include "can.h"
#include "encoder.h"
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

void CTM_H759_PHASE_ADC_IRQHandler(void)
{
    adc_interrupt_flag_clear(CTM_H759_PHASE_ADC, ADC_INT_FLAG_EOIC);
    MCT_high_frequency_task();
}

void TIMER1_IRQHandler(void)
{
    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);

    MCT_safety_task();

    SystickCount++;
}

void CTM_H759_CAN_IRQHandler(void)
{
    if (RESET != can_interrupt_flag_get(CTM_H759_CAN, CAN_INT_FLAG_MB0)) {
        CAN_receive_callback();
    }
}

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
void TIMER3_IRQHandler(void)
{
    if (SET != timer_interrupt_flag_get(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_FLAG)) {
        return;
    }

    if (SET == timer_flag_get(CTM_H759_ENC_PWM_TIMER, TIMER_FLAG_CH2O)) {
        timer_interrupt_flag_clear(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_FLAG);
        timer_flag_clear(CTM_H759_ENC_PWM_TIMER, TIMER_FLAG_CH2O);
        ENCODER_pwm_capture_overrun_callback();
        return;
    }

    uint16_t capture = (uint16_t) timer_channel_capture_value_register_read(CTM_H759_ENC_PWM_TIMER,
                                                                            CTM_H759_ENC_PWM_TIMER_CH);

    timer_interrupt_flag_clear(CTM_H759_ENC_PWM_TIMER, CTM_H759_ENC_PWM_TIMER_FLAG);

    ENCODER_pwm_capture_callback(capture);
}
#endif
