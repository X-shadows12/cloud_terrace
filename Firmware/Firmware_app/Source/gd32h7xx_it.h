/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
*/

#ifndef GD32H7XX_IT_H
#define GD32H7XX_IT_H

#include "main.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void CTM_H759_PHASE_ADC_IRQHandler(void);
void TIMER1_IRQHandler(void);
#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
void TIMER3_IRQHandler(void);
#endif
void CTM_H759_CAN_IRQHandler(void);

#endif /* GD32H7XX_IT_H */
