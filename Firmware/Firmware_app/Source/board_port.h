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

#ifndef __BOARD_PORT_H__
#define __BOARD_PORT_H__

#include "gd32h7xx.h"
#include "board_gd32h759.h"

/*
 * Generic board contract used by the reusable hardware facade layer.
 * When porting to another board/MCU, replace the mapping in this file.
 */

#define BOARD_FLASH_BASE                  FLASH_BASE
#define BOARD_FLASH_SECTOR_SIZE           ((uint32_t) 0x1000U)
#define BOARD_FLASH_PAGE_SIZE             BOARD_FLASH_SECTOR_SIZE

#define BOARD_FLASH_APP_MAIN_ADDR         ((uint32_t) (BOARD_FLASH_BASE + 0x00000U))
#define BOARD_FLASH_APP_BACK_ADDR         ((uint32_t) (BOARD_FLASH_BASE + 0x40000U))
#define BOARD_FLASH_APP_MAX_SIZE          ((uint32_t) (0x40000U))

#define BOARD_FLASH_BOOTLOADER_ADDR       ((uint32_t) (BOARD_FLASH_BASE + 0x80000U))
#define BOARD_FLASH_BOOTLOADER_MAX_SIZE   ((uint32_t) (0x10000U))

#define BOARD_FLASH_USR_CONFIG_ADDR       ((uint32_t) (BOARD_FLASH_BASE + 0x90000U))
#define BOARD_FLASH_USR_CONFIG_MAX_SIZE   ((uint32_t) (0x1000U))

#define BOARD_FLASH_COGGING_MAP_ADDR      ((uint32_t) (BOARD_FLASH_BASE + 0x91000U))
#define BOARD_FLASH_COGGING_MAP_MAX_SIZE   ((uint32_t) (0x4000U))

#define BOARD_PWM_TIMER                   CTM_H759_PWM_TIMER
#define BOARD_PWM_AF                      CTM_H759_PWM_AF
#define BOARD_PWM_TIMER_CLK_MHZ           300U
#define BOARD_PWM_TIMER_RCU               RCU_TIMER0
#define BOARD_PWM_TIMER_DBG_HOLD          DBG_TIMER0_HOLD
#define BOARD_PWM_PHASE_A_CV              CTM_H759_PWM_PHASE_A_CV
#define BOARD_PWM_PHASE_B_CV              CTM_H759_PWM_PHASE_B_CV
#define BOARD_PWM_PHASE_C_CV              CTM_H759_PWM_PHASE_C_CV

#define BOARD_PWM_CH0_PORT                CTM_H759_PWM_CH0_PORT
#define BOARD_PWM_CH0_PIN                 CTM_H759_PWM_CH0_PIN
#define BOARD_PWM_MCH0_PORT               CTM_H759_PWM_MCH0_PORT
#define BOARD_PWM_MCH0_PIN                CTM_H759_PWM_MCH0_PIN
#define BOARD_PWM_CH1_PORT                CTM_H759_PWM_CH1_PORT
#define BOARD_PWM_CH1_PIN                 CTM_H759_PWM_CH1_PIN
#define BOARD_PWM_MCH1_PORT               CTM_H759_PWM_MCH1_PORT
#define BOARD_PWM_MCH1_PIN                CTM_H759_PWM_MCH1_PIN
#define BOARD_PWM_CH2_PORT                CTM_H759_PWM_CH2_PORT
#define BOARD_PWM_CH2_PIN                 CTM_H759_PWM_CH2_PIN
#define BOARD_PWM_MCH2_PORT               CTM_H759_PWM_MCH2_PORT
#define BOARD_PWM_MCH2_PIN                CTM_H759_PWM_MCH2_PIN

#define BOARD_SYSTEM_TIMER                TIMER1
#define BOARD_SYSTEM_TIMER_RCU            RCU_TIMER1
#define BOARD_SYSTEM_TIMER_IRQ            TIMER1_IRQn
#define BOARD_SYSTEM_TIMER_IRQHandler     TIMER1_IRQHandler

#define BOARD_PHASE_ADC                   CTM_H759_PHASE_ADC
#define BOARD_PHASE_ADC_RCU               CTM_H759_PHASE_ADC_RCU
#define BOARD_PHASE_ADC_IDX               CTM_H759_PHASE_ADC_IDX
#define BOARD_PHASE_A_PORT                CTM_H759_PHASE_A_PORT
#define BOARD_PHASE_A_PIN                 CTM_H759_PHASE_A_PIN
#define BOARD_PHASE_A_ADC                 CTM_H759_PHASE_A_ADC
#define BOARD_PHASE_A_ADC_CHANNEL         CTM_H759_PHASE_A_ADC_CHANNEL
#define BOARD_PHASE_B_PORT                CTM_H759_PHASE_B_PORT
#define BOARD_PHASE_B_PIN                 CTM_H759_PHASE_B_PIN
#define BOARD_PHASE_B_ADC                 CTM_H759_PHASE_B_ADC
#define BOARD_PHASE_B_ADC_CHANNEL         CTM_H759_PHASE_B_ADC_CHANNEL
#define BOARD_PHASE_ADC_IRQ               CTM_H759_PHASE_ADC_IRQ
#define BOARD_PHASE_ADC_IRQHandler        CTM_H759_PHASE_ADC_IRQHandler
#define BOARD_PHASE_TRIGGER_OUTPUT        CTM_H759_PHASE_TRIGGER_OUTPUT
#define BOARD_PHASE_TRIGGER_INPUT         CTM_H759_PHASE_TRIGGER_INPUT
#define BOARD_PHASE_ADC_SAMPLE_TIME       CTM_H759_PHASE_ADC_SAMPLE_TIME

#ifdef CTM_H759_PHASE_A_ANALOG_SWITCH
#define BOARD_PHASE_A_ANALOG_SWITCH       CTM_H759_PHASE_A_ANALOG_SWITCH
#endif
#ifdef CTM_H759_PHASE_B_ANALOG_SWITCH
#define BOARD_PHASE_B_ANALOG_SWITCH       CTM_H759_PHASE_B_ANALOG_SWITCH
#endif

#define BOARD_HAS_VBUS_ADC                CTM_H759_HAS_VBUS_ADC
#define BOARD_VBUS_PORT                   CTM_H759_VBUS_PORT
#define BOARD_VBUS_PIN                    CTM_H759_VBUS_PIN
#define BOARD_VBUS_ADC                    CTM_H759_VBUS_ADC
#define BOARD_VBUS_ADC_RCU                CTM_H759_VBUS_ADC_RCU
#define BOARD_VBUS_ADC_IDX                CTM_H759_VBUS_ADC_IDX
#define BOARD_VBUS_ADC_CHANNEL            CTM_H759_VBUS_ADC_CHANNEL
#define BOARD_VBUS_ADC_SAMPLE_TIME        CTM_H759_VBUS_ADC_SAMPLE_TIME
#define BOARD_NOMINAL_VBUS                CTM_H759_NOMINAL_VBUS

#define BOARD_HAS_TEMP_ADC                CTM_H759_HAS_TEMP_ADC
#define BOARD_DEFAULT_DRV_TEMP            CTM_H759_DEFAULT_DRV_TEMP
#define BOARD_DEFAULT_NTC_TEMP            CTM_H759_DEFAULT_NTC_TEMP

#define BOARD_ENCODER_IF_SPI              CTM_H759_ENCODER_IF_SPI
#define BOARD_ENCODER_IF_PWM              CTM_H759_ENCODER_IF_PWM
#define BOARD_ENCODER_INTERFACE            CTM_H759_ENCODER_INTERFACE

#define BOARD_ENC_SPI                     CTM_H759_ENC_SPI
#define BOARD_ENC_SPI_RCU                 RCU_SPI0
#define BOARD_ENC_SPI_IDX                 IDX_SPI0
#define BOARD_ENC_SPI_CLK_SOURCE          RCU_SPISRC_PLL0Q
#define BOARD_SPI0_AF                     CTM_H759_SPI0_AF
#define BOARD_SPI0_SCK_PORT               CTM_H759_SPI0_SCK_PORT
#define BOARD_SPI0_SCK_PIN                CTM_H759_SPI0_SCK_PIN
#define BOARD_SPI0_MISO_PORT              CTM_H759_SPI0_MISO_PORT
#define BOARD_SPI0_MISO_PIN               CTM_H759_SPI0_MISO_PIN
#define BOARD_SPI0_MOSI_PORT              CTM_H759_SPI0_MOSI_PORT
#define BOARD_SPI0_MOSI_PIN               CTM_H759_SPI0_MOSI_PIN
#define BOARD_SPI0_CS_X_PORT              CTM_H759_SPI0_CS_X_PORT
#define BOARD_SPI0_CS_X_PIN               CTM_H759_SPI0_CS_X_PIN
#define BOARD_SPI0_CS_R_PORT              CTM_H759_SPI0_CS_R_PORT
#define BOARD_SPI0_CS_R_PIN               CTM_H759_SPI0_CS_R_PIN
#define BOARD_SPI0_CS_L_PORT              CTM_H759_SPI0_CS_L_PORT
#define BOARD_SPI0_CS_L_PIN               CTM_H759_SPI0_CS_L_PIN

#define BOARD_ENC_CS_PORT                 CTM_H759_ENC_CS_PORT
#define BOARD_ENC_CS_PIN                  CTM_H759_ENC_CS_PIN
#define BOARD_ENC_CS_SET()                GPIO_BOP(BOARD_ENC_CS_PORT) = (uint32_t) BOARD_ENC_CS_PIN
#define BOARD_ENC_CS_RESET()              GPIO_BC(BOARD_ENC_CS_PORT) = (uint32_t) BOARD_ENC_CS_PIN

#if (BOARD_ENCODER_INTERFACE == BOARD_ENCODER_IF_PWM)
#define BOARD_ENC_PWM_PORT                CTM_H759_ENC_PWM_PORT
#define BOARD_ENC_PWM_PIN                 CTM_H759_ENC_PWM_PIN
#define BOARD_ENC_PWM_AF                  CTM_H759_ENC_PWM_AF
#define BOARD_ENC_PWM_TIMER               CTM_H759_ENC_PWM_TIMER
#define BOARD_ENC_PWM_TIMER_RCU           RCU_TIMER3
#define BOARD_ENC_PWM_TIMER_CH            CTM_H759_ENC_PWM_TIMER_CH
#define BOARD_ENC_PWM_TIMER_IRQ           CTM_H759_ENC_PWM_TIMER_IRQ
#define BOARD_ENC_PWM_TIMER_INT           CTM_H759_ENC_PWM_TIMER_INT
#define BOARD_ENC_PWM_TIMER_FLAG          CTM_H759_ENC_PWM_TIMER_FLAG
#define BOARD_ENC_PWM_TIMER_TICK_HZ       CTM_H759_ENC_PWM_TIMER_TICK_HZ
#define BOARD_ENC_PWM_CPR                 CTM_H759_ENC_PWM_CPR
#define BOARD_ENC_PWM_MIN_HIGH_TICKS      CTM_H759_ENC_PWM_MIN_HIGH_TICKS
#define BOARD_ENC_PWM_MIN_PERIOD_TICKS    CTM_H759_ENC_PWM_MIN_PERIOD_TICKS
#define BOARD_ENC_PWM_MAX_PERIOD_TICKS    CTM_H759_ENC_PWM_MAX_PERIOD_TICKS
#define BOARD_ENC_PWM_INPUT_FILTER        CTM_H759_ENC_PWM_INPUT_FILTER
#define BOARD_ENC_PWM_PERIOD_TOL_PCT      CTM_H759_ENC_PWM_PERIOD_TOL_PCT
#define BOARD_ENC_PWM_PERIOD_LOCK_SAMPLES CTM_H759_ENC_PWM_PERIOD_LOCK_SAMPLES
#define BOARD_ENC_PWM_MAX_STEP_COUNTS     CTM_H759_ENC_PWM_MAX_STEP_COUNTS
#endif

#define BOARD_CAN                         CTM_H759_CAN
#define BOARD_CAN_IDX                     CTM_H759_CAN_IDX
#define BOARD_CAN_RCU                     RCU_CAN2
#define BOARD_CAN_CLK_SOURCE              RCU_CANSRC_APB2
#define BOARD_CAN_RX_AF                   CTM_H759_CAN_RX_AF
#define BOARD_CAN_TX_AF                   CTM_H759_CAN_TX_AF
#define BOARD_CAN_RX_PORT                 CTM_H759_CAN_RX_PORT
#define BOARD_CAN_RX_PIN                  CTM_H759_CAN_RX_PIN
#define BOARD_CAN_TX_PORT                 CTM_H759_CAN_TX_PORT
#define BOARD_CAN_TX_PIN                  CTM_H759_CAN_TX_PIN
#define BOARD_CAN_IRQ                     CTM_H759_CAN_IRQ
#define BOARD_CAN_IRQHandler              CTM_H759_CAN_IRQHandler
#define BOARD_CAN_RX_MAILBOX              CTM_H759_CAN_RX_MAILBOX
#define BOARD_CAN_TX_MAILBOX              CTM_H759_CAN_TX_MAILBOX

#if defined(CTM_H759_USE_LEFT_MOTOR)
#define BOARD_USE_LEFT_MOTOR              1
#endif
#if defined(CTM_H759_USE_RIGHT_MOTOR)
#define BOARD_USE_RIGHT_MOTOR             1
#endif

#define BOARD_KEY1_PORT                   CTM_H759_KEY1_PORT
#define BOARD_KEY1_PIN                    CTM_H759_KEY1_PIN
#define BOARD_KEY2_PORT                   CTM_H759_KEY2_PORT
#define BOARD_KEY2_PIN                    CTM_H759_KEY2_PIN
#define BOARD_SWDIO_PORT                  CTM_H759_SWDIO_PORT
#define BOARD_SWDIO_PIN                   CTM_H759_SWDIO_PIN
#define BOARD_SWCLK_PORT                  CTM_H759_SWCLK_PORT
#define BOARD_SWCLK_PIN                   CTM_H759_SWCLK_PIN

#endif /* __BOARD_PORT_H__ */
