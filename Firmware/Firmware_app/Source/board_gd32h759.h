/*
    GD32H759 board resource map.

    This file captures the low-level pin/peripheral assignment extracted from
    the GD32H759 motor-control schematic PDF and checked against the
    GD32H759xx datasheet.

    This file is the board map used by the GD32H73x_75x CMSIS/SPL build.
*/

#ifndef __BOARD_GD32H759_H__
#define __BOARD_GD32H759_H__

#if !defined(GD32H7XX)
#error "board_gd32h759.h requires GD32H73x_75x device headers."
#endif

/* MCU -----------------------------------------------------------------------
 * Part/package: GD32H759, LQFP176.
 * HXTAL: PH0/PH1, 25 MHz on the schematic.
 */

/* Motor PWM -----------------------------------------------------------------
 * Left driver U3:
 *   L_HIN1 PE9  TIMER0_CH0     L_LIN1 PE8  TIMER0_MCH0
 *   L_HIN2 PE11 TIMER0_CH1     L_LIN2 PE10 TIMER0_MCH1
 *   L_HIN3 PE13 TIMER0_CH2     L_LIN3 PE12 TIMER0_MCH2
 *
 * Right driver U1:
 *   R_HIN1 PJ8  TIMER7_CH0     R_LIN1 PJ9  TIMER7_MCH0
 *   R_HIN2 PJ10 TIMER7_CH1     R_LIN2 PJ11 TIMER7_MCH1
 *   R_HIN3 PK0  TIMER7_CH2     R_LIN3 PK1  TIMER7_MCH2
 */
#define CTM_H759_LEFT_PWM_TIMER               TIMER0
#define CTM_H759_LEFT_PWM_AF                  GPIO_AF_1
#define CTM_H759_LEFT_PWM_TIMER_RCU           RCU_TIMER0
#define CTM_H759_LEFT_PWM_TIMER_DBG_HOLD      DBG_TIMER0_HOLD

#define CTM_H759_RIGHT_PWM_TIMER              TIMER7
#define CTM_H759_RIGHT_PWM_AF                 GPIO_AF_3
#define CTM_H759_RIGHT_PWM_TIMER_RCU          RCU_TIMER7
#define CTM_H759_RIGHT_PWM_TIMER_DBG_HOLD     DBG_TIMER7_HOLD

#if !defined(CTM_H759_USE_RIGHT_MOTOR)
#define CTM_H759_USE_LEFT_MOTOR               1
#endif

#if defined(CTM_H759_USE_RIGHT_MOTOR) && defined(CTM_H759_USE_LEFT_MOTOR)
#error "Select either left or right GD32H759 motor driver, not both."
#endif

#define CTM_H759_L_HIN1_PORT                  GPIOE
#define CTM_H759_L_HIN1_PIN                   GPIO_PIN_9
#define CTM_H759_L_LIN1_PORT                  GPIOE
#define CTM_H759_L_LIN1_PIN                   GPIO_PIN_8
#define CTM_H759_L_HIN2_PORT                  GPIOE
#define CTM_H759_L_HIN2_PIN                   GPIO_PIN_11
#define CTM_H759_L_LIN2_PORT                  GPIOE
#define CTM_H759_L_LIN2_PIN                   GPIO_PIN_10
#define CTM_H759_L_HIN3_PORT                  GPIOE
#define CTM_H759_L_HIN3_PIN                   GPIO_PIN_13
#define CTM_H759_L_LIN3_PORT                  GPIOE
#define CTM_H759_L_LIN3_PIN                   GPIO_PIN_12

#define CTM_H759_R_HIN1_PORT                  GPIOJ
#define CTM_H759_R_HIN1_PIN                   GPIO_PIN_8
#define CTM_H759_R_LIN1_PORT                  GPIOJ
#define CTM_H759_R_LIN1_PIN                   GPIO_PIN_9
#define CTM_H759_R_HIN2_PORT                  GPIOJ
#define CTM_H759_R_HIN2_PIN                   GPIO_PIN_10
#define CTM_H759_R_LIN2_PORT                  GPIOJ
#define CTM_H759_R_LIN2_PIN                   GPIO_PIN_11
#define CTM_H759_R_HIN3_PORT                  GPIOK
#define CTM_H759_R_HIN3_PIN                   GPIO_PIN_0
#define CTM_H759_R_LIN3_PORT                  GPIOK
#define CTM_H759_R_LIN3_PIN                   GPIO_PIN_1

#define CTM_H759_LEFT_PWM_PHASE_A_CV          TIMER_CH0CV(CTM_H759_LEFT_PWM_TIMER)
#define CTM_H759_LEFT_PWM_PHASE_B_CV          TIMER_CH2CV(CTM_H759_LEFT_PWM_TIMER)
#define CTM_H759_LEFT_PWM_PHASE_C_CV          TIMER_CH1CV(CTM_H759_LEFT_PWM_TIMER)
#define CTM_H759_LEFT_PWM_CH0_PORT            CTM_H759_L_HIN1_PORT
#define CTM_H759_LEFT_PWM_CH0_PIN             CTM_H759_L_HIN1_PIN
#define CTM_H759_LEFT_PWM_MCH0_PORT           CTM_H759_L_LIN1_PORT
#define CTM_H759_LEFT_PWM_MCH0_PIN            CTM_H759_L_LIN1_PIN
#define CTM_H759_LEFT_PWM_CH1_PORT            CTM_H759_L_HIN2_PORT
#define CTM_H759_LEFT_PWM_CH1_PIN             CTM_H759_L_HIN2_PIN
#define CTM_H759_LEFT_PWM_MCH1_PORT           CTM_H759_L_LIN2_PORT
#define CTM_H759_LEFT_PWM_MCH1_PIN            CTM_H759_L_LIN2_PIN
#define CTM_H759_LEFT_PWM_CH2_PORT            CTM_H759_L_HIN3_PORT
#define CTM_H759_LEFT_PWM_CH2_PIN             CTM_H759_L_HIN3_PIN
#define CTM_H759_LEFT_PWM_MCH2_PORT           CTM_H759_L_LIN3_PORT
#define CTM_H759_LEFT_PWM_MCH2_PIN            CTM_H759_L_LIN3_PIN

#define CTM_H759_RIGHT_PWM_PHASE_A_CV         TIMER_CH0CV(CTM_H759_RIGHT_PWM_TIMER)
#define CTM_H759_RIGHT_PWM_PHASE_B_CV         TIMER_CH2CV(CTM_H759_RIGHT_PWM_TIMER)
#define CTM_H759_RIGHT_PWM_PHASE_C_CV         TIMER_CH1CV(CTM_H759_RIGHT_PWM_TIMER)
#define CTM_H759_RIGHT_PWM_CH0_PORT           CTM_H759_R_HIN1_PORT
#define CTM_H759_RIGHT_PWM_CH0_PIN            CTM_H759_R_HIN1_PIN
#define CTM_H759_RIGHT_PWM_MCH0_PORT          CTM_H759_R_LIN1_PORT
#define CTM_H759_RIGHT_PWM_MCH0_PIN           CTM_H759_R_LIN1_PIN
#define CTM_H759_RIGHT_PWM_CH1_PORT           CTM_H759_R_HIN2_PORT
#define CTM_H759_RIGHT_PWM_CH1_PIN            CTM_H759_R_HIN2_PIN
#define CTM_H759_RIGHT_PWM_MCH1_PORT          CTM_H759_R_LIN2_PORT
#define CTM_H759_RIGHT_PWM_MCH1_PIN           CTM_H759_R_LIN2_PIN
#define CTM_H759_RIGHT_PWM_CH2_PORT           CTM_H759_R_HIN3_PORT
#define CTM_H759_RIGHT_PWM_CH2_PIN            CTM_H759_R_HIN3_PIN
#define CTM_H759_RIGHT_PWM_MCH2_PORT          CTM_H759_R_LIN3_PORT
#define CTM_H759_RIGHT_PWM_MCH2_PIN           CTM_H759_R_LIN3_PIN

/* Phase current ADC ---------------------------------------------------------
 * Left driver current amplifiers:
 *   L_I_U PC2_C ADC2_IN0
 *   L_I_W PC3_C ADC2_IN1
 *
 * Right driver current amplifiers:
 *   R_I_U PF11  ADC0_IN2
 *   R_I_W PF12  ADC0_IN6
 */
#define CTM_H759_L_I_U_PORT                   GPIOC
#define CTM_H759_L_I_U_PIN                    GPIO_PIN_2
#define CTM_H759_L_I_U_ADC                    ADC2
#define CTM_H759_L_I_U_ADC_CHANNEL            ADC_CHANNEL_0
#define CTM_H759_L_I_U_ANALOG_SWITCH          SYSCFG_PC2_ANALOG_SWITCH

#define CTM_H759_L_I_W_PORT                   GPIOC
#define CTM_H759_L_I_W_PIN                    GPIO_PIN_3
#define CTM_H759_L_I_W_ADC                    ADC2
#define CTM_H759_L_I_W_ADC_CHANNEL            ADC_CHANNEL_1
#define CTM_H759_L_I_W_ANALOG_SWITCH          SYSCFG_PC3_ANALOG_SWITCH

#define CTM_H759_R_I_U_PORT                   GPIOF
#define CTM_H759_R_I_U_PIN                    GPIO_PIN_11
#define CTM_H759_R_I_U_ADC                    ADC0
#define CTM_H759_R_I_U_ADC_CHANNEL            ADC_CHANNEL_2

#define CTM_H759_R_I_W_PORT                   GPIOF
#define CTM_H759_R_I_W_PIN                    GPIO_PIN_12
#define CTM_H759_R_I_W_ADC                    ADC0
#define CTM_H759_R_I_W_ADC_CHANNEL            ADC_CHANNEL_6

#define CTM_H759_LEFT_PHASE_ADC               ADC2
#define CTM_H759_LEFT_PHASE_ADC_RCU           RCU_ADC2
#define CTM_H759_LEFT_PHASE_ADC_IDX           IDX_ADC2
#define CTM_H759_LEFT_PHASE_A_PORT            CTM_H759_L_I_U_PORT
#define CTM_H759_LEFT_PHASE_A_PIN             CTM_H759_L_I_U_PIN
#define CTM_H759_LEFT_PHASE_A_ADC             CTM_H759_L_I_U_ADC
#define CTM_H759_LEFT_PHASE_A_ADC_CHANNEL     CTM_H759_L_I_U_ADC_CHANNEL
#define CTM_H759_LEFT_PHASE_A_ANALOG_SWITCH   CTM_H759_L_I_U_ANALOG_SWITCH
#define CTM_H759_LEFT_PHASE_B_PORT            CTM_H759_L_I_W_PORT
#define CTM_H759_LEFT_PHASE_B_PIN             CTM_H759_L_I_W_PIN
#define CTM_H759_LEFT_PHASE_B_ADC             CTM_H759_L_I_W_ADC
#define CTM_H759_LEFT_PHASE_B_ADC_CHANNEL     CTM_H759_L_I_W_ADC_CHANNEL
#define CTM_H759_LEFT_PHASE_B_ANALOG_SWITCH   CTM_H759_L_I_W_ANALOG_SWITCH
#define CTM_H759_LEFT_PHASE_ADC_IRQ           ADC2_IRQn
#define CTM_H759_LEFT_PHASE_ADC_IRQHandler    ADC2_IRQHandler
#define CTM_H759_LEFT_PHASE_TRIGGER_OUTPUT    TRIGSEL_OUTPUT_ADC2_INSTRG
#define CTM_H759_LEFT_PHASE_TRIGGER_INPUT     TRIGSEL_INPUT_TIMER0_CH3

#define CTM_H759_RIGHT_PHASE_ADC              ADC0
#define CTM_H759_RIGHT_PHASE_ADC_RCU          RCU_ADC0
#define CTM_H759_RIGHT_PHASE_ADC_IDX          IDX_ADC0
#define CTM_H759_RIGHT_PHASE_A_PORT           CTM_H759_R_I_U_PORT
#define CTM_H759_RIGHT_PHASE_A_PIN            CTM_H759_R_I_U_PIN
#define CTM_H759_RIGHT_PHASE_A_ADC            CTM_H759_R_I_U_ADC
#define CTM_H759_RIGHT_PHASE_A_ADC_CHANNEL    CTM_H759_R_I_U_ADC_CHANNEL
#define CTM_H759_RIGHT_PHASE_B_PORT           CTM_H759_R_I_W_PORT
#define CTM_H759_RIGHT_PHASE_B_PIN            CTM_H759_R_I_W_PIN
#define CTM_H759_RIGHT_PHASE_B_ADC            CTM_H759_R_I_W_ADC
#define CTM_H759_RIGHT_PHASE_B_ADC_CHANNEL    CTM_H759_R_I_W_ADC_CHANNEL
#define CTM_H759_RIGHT_PHASE_ADC_IRQ          ADC0_1_IRQn
#define CTM_H759_RIGHT_PHASE_ADC_IRQHandler   ADC0_1_IRQHandler
#define CTM_H759_RIGHT_PHASE_TRIGGER_OUTPUT   TRIGSEL_OUTPUT_ADC0_INSTRG
#define CTM_H759_RIGHT_PHASE_TRIGGER_INPUT    TRIGSEL_INPUT_TIMER7_CH3
#define CTM_H759_PHASE_ADC_SAMPLE_TIME        20U

#if defined(CTM_H759_USE_LEFT_MOTOR)
#define CTM_H759_PWM_TIMER                    CTM_H759_LEFT_PWM_TIMER
#define CTM_H759_PWM_AF                       CTM_H759_LEFT_PWM_AF
#define CTM_H759_PWM_TIMER_RCU                CTM_H759_LEFT_PWM_TIMER_RCU
#define CTM_H759_PWM_TIMER_DBG_HOLD           CTM_H759_LEFT_PWM_TIMER_DBG_HOLD
#define CTM_H759_PWM_PHASE_A_CV               CTM_H759_LEFT_PWM_PHASE_A_CV
#define CTM_H759_PWM_PHASE_B_CV               CTM_H759_LEFT_PWM_PHASE_B_CV
#define CTM_H759_PWM_PHASE_C_CV               CTM_H759_LEFT_PWM_PHASE_C_CV
#define CTM_H759_PWM_CH0_PORT                 CTM_H759_LEFT_PWM_CH0_PORT
#define CTM_H759_PWM_CH0_PIN                  CTM_H759_LEFT_PWM_CH0_PIN
#define CTM_H759_PWM_MCH0_PORT                CTM_H759_LEFT_PWM_MCH0_PORT
#define CTM_H759_PWM_MCH0_PIN                 CTM_H759_LEFT_PWM_MCH0_PIN
#define CTM_H759_PWM_CH1_PORT                 CTM_H759_LEFT_PWM_CH1_PORT
#define CTM_H759_PWM_CH1_PIN                  CTM_H759_LEFT_PWM_CH1_PIN
#define CTM_H759_PWM_MCH1_PORT                CTM_H759_LEFT_PWM_MCH1_PORT
#define CTM_H759_PWM_MCH1_PIN                 CTM_H759_LEFT_PWM_MCH1_PIN
#define CTM_H759_PWM_CH2_PORT                 CTM_H759_LEFT_PWM_CH2_PORT
#define CTM_H759_PWM_CH2_PIN                  CTM_H759_LEFT_PWM_CH2_PIN
#define CTM_H759_PWM_MCH2_PORT                CTM_H759_LEFT_PWM_MCH2_PORT
#define CTM_H759_PWM_MCH2_PIN                 CTM_H759_LEFT_PWM_MCH2_PIN
#define CTM_H759_PHASE_ADC                    CTM_H759_LEFT_PHASE_ADC
#define CTM_H759_PHASE_ADC_RCU                CTM_H759_LEFT_PHASE_ADC_RCU
#define CTM_H759_PHASE_ADC_IDX                CTM_H759_LEFT_PHASE_ADC_IDX
#define CTM_H759_PHASE_A_PORT                 CTM_H759_LEFT_PHASE_A_PORT
#define CTM_H759_PHASE_A_PIN                  CTM_H759_LEFT_PHASE_A_PIN
#define CTM_H759_PHASE_A_ADC                  CTM_H759_LEFT_PHASE_A_ADC
#define CTM_H759_PHASE_A_ADC_CHANNEL          CTM_H759_LEFT_PHASE_A_ADC_CHANNEL
#define CTM_H759_PHASE_A_ANALOG_SWITCH        CTM_H759_LEFT_PHASE_A_ANALOG_SWITCH
#define CTM_H759_PHASE_B_PORT                 CTM_H759_LEFT_PHASE_B_PORT
#define CTM_H759_PHASE_B_PIN                  CTM_H759_LEFT_PHASE_B_PIN
#define CTM_H759_PHASE_B_ADC                  CTM_H759_LEFT_PHASE_B_ADC
#define CTM_H759_PHASE_B_ADC_CHANNEL          CTM_H759_LEFT_PHASE_B_ADC_CHANNEL
#define CTM_H759_PHASE_B_ANALOG_SWITCH        CTM_H759_LEFT_PHASE_B_ANALOG_SWITCH
#define CTM_H759_PHASE_ADC_IRQ                CTM_H759_LEFT_PHASE_ADC_IRQ
#define CTM_H759_PHASE_ADC_IRQHandler         CTM_H759_LEFT_PHASE_ADC_IRQHandler
#define CTM_H759_PHASE_TRIGGER_OUTPUT         CTM_H759_LEFT_PHASE_TRIGGER_OUTPUT
#define CTM_H759_PHASE_TRIGGER_INPUT          CTM_H759_LEFT_PHASE_TRIGGER_INPUT
#elif defined(CTM_H759_USE_RIGHT_MOTOR)
#define CTM_H759_PWM_TIMER                    CTM_H759_RIGHT_PWM_TIMER
#define CTM_H759_PWM_AF                       CTM_H759_RIGHT_PWM_AF
#define CTM_H759_PWM_TIMER_RCU                CTM_H759_RIGHT_PWM_TIMER_RCU
#define CTM_H759_PWM_TIMER_DBG_HOLD           CTM_H759_RIGHT_PWM_TIMER_DBG_HOLD
#define CTM_H759_PWM_PHASE_A_CV               CTM_H759_RIGHT_PWM_PHASE_A_CV
#define CTM_H759_PWM_PHASE_B_CV               CTM_H759_RIGHT_PWM_PHASE_B_CV
#define CTM_H759_PWM_PHASE_C_CV               CTM_H759_RIGHT_PWM_PHASE_C_CV
#define CTM_H759_PWM_CH0_PORT                 CTM_H759_RIGHT_PWM_CH0_PORT
#define CTM_H759_PWM_CH0_PIN                  CTM_H759_RIGHT_PWM_CH0_PIN
#define CTM_H759_PWM_MCH0_PORT                CTM_H759_RIGHT_PWM_MCH0_PORT
#define CTM_H759_PWM_MCH0_PIN                 CTM_H759_RIGHT_PWM_MCH0_PIN
#define CTM_H759_PWM_CH1_PORT                 CTM_H759_RIGHT_PWM_CH1_PORT
#define CTM_H759_PWM_CH1_PIN                  CTM_H759_RIGHT_PWM_CH1_PIN
#define CTM_H759_PWM_MCH1_PORT                CTM_H759_RIGHT_PWM_MCH1_PORT
#define CTM_H759_PWM_MCH1_PIN                 CTM_H759_RIGHT_PWM_MCH1_PIN
#define CTM_H759_PWM_CH2_PORT                 CTM_H759_RIGHT_PWM_CH2_PORT
#define CTM_H759_PWM_CH2_PIN                  CTM_H759_RIGHT_PWM_CH2_PIN
#define CTM_H759_PWM_MCH2_PORT                CTM_H759_RIGHT_PWM_MCH2_PORT
#define CTM_H759_PWM_MCH2_PIN                 CTM_H759_RIGHT_PWM_MCH2_PIN
#define CTM_H759_PHASE_ADC                    CTM_H759_RIGHT_PHASE_ADC
#define CTM_H759_PHASE_ADC_RCU                CTM_H759_RIGHT_PHASE_ADC_RCU
#define CTM_H759_PHASE_ADC_IDX                CTM_H759_RIGHT_PHASE_ADC_IDX
#define CTM_H759_PHASE_A_PORT                 CTM_H759_RIGHT_PHASE_A_PORT
#define CTM_H759_PHASE_A_PIN                  CTM_H759_RIGHT_PHASE_A_PIN
#define CTM_H759_PHASE_A_ADC                  CTM_H759_RIGHT_PHASE_A_ADC
#define CTM_H759_PHASE_A_ADC_CHANNEL          CTM_H759_RIGHT_PHASE_A_ADC_CHANNEL
#define CTM_H759_PHASE_B_PORT                 CTM_H759_RIGHT_PHASE_B_PORT
#define CTM_H759_PHASE_B_PIN                  CTM_H759_RIGHT_PHASE_B_PIN
#define CTM_H759_PHASE_B_ADC                  CTM_H759_RIGHT_PHASE_B_ADC
#define CTM_H759_PHASE_B_ADC_CHANNEL          CTM_H759_RIGHT_PHASE_B_ADC_CHANNEL
#define CTM_H759_PHASE_ADC_IRQ                CTM_H759_RIGHT_PHASE_ADC_IRQ
#define CTM_H759_PHASE_ADC_IRQHandler         CTM_H759_RIGHT_PHASE_ADC_IRQHandler
#define CTM_H759_PHASE_TRIGGER_OUTPUT         CTM_H759_RIGHT_PHASE_TRIGGER_OUTPUT
#define CTM_H759_PHASE_TRIGGER_INPUT          CTM_H759_RIGHT_PHASE_TRIGGER_INPUT
#endif

/* DC bus voltage ADC --------------------------------------------------------
 *   VSENSE PF13 ADC1_IN2
 */
#define CTM_H759_HAS_VBUS_ADC                 1
#define CTM_H759_VBUS_PORT                    GPIOF
#define CTM_H759_VBUS_PIN                     GPIO_PIN_13
#define CTM_H759_VBUS_ADC                     ADC1
#define CTM_H759_VBUS_ADC_RCU                 RCU_ADC1
#define CTM_H759_VBUS_ADC_IDX                 IDX_ADC1
#define CTM_H759_VBUS_ADC_CHANNEL             ADC_CHANNEL_2
#define CTM_H759_VBUS_ADC_SAMPLE_TIME         200U
#define CTM_H759_NOMINAL_VBUS                 24.0f
#define CTM_H759_HAS_TEMP_ADC                 0
#define CTM_H759_DEFAULT_DRV_TEMP             25
#define CTM_H759_DEFAULT_NTC_TEMP             25

/* Encoder -------------------------------------------------------------------
 * The original CTM firmware reads the magnetic encoder through SPI0. This
 * board variant defaults to absolute PWM encoder inputs on TIMER3:
 *   Left encoder:  PB9 TIMER3_CH3 AF2
 *   Right encoder: PB8 TIMER3_CH2 AF2
 *
 * Set CTM_H759_ENCODER_INTERFACE to:
 *   CTM_H759_ENCODER_IF_PWM: absolute PWM encoder input
 *   CTM_H759_ENCODER_IF_SPI: original SPI encoder
 */
#define CTM_H759_ENCODER_IF_SPI             0
#define CTM_H759_ENCODER_IF_PWM             1

#ifndef CTM_H759_ENCODER_INTERFACE
#define CTM_H759_ENCODER_INTERFACE          CTM_H759_ENCODER_IF_PWM
#endif

/* SPI encoder / external SPI0 bus ------------------------------------------ */
#define CTM_H759_ENC_SPI                      SPI0
#define CTM_H759_SPI0_AF                      GPIO_AF_5
#define CTM_H759_SPI0_SCK_PORT                GPIOB
#define CTM_H759_SPI0_SCK_PIN                 GPIO_PIN_3
#define CTM_H759_SPI0_MISO_PORT               GPIOG
#define CTM_H759_SPI0_MISO_PIN                GPIO_PIN_9
#define CTM_H759_SPI0_MOSI_PORT               GPIOD
#define CTM_H759_SPI0_MOSI_PIN                GPIO_PIN_7

#define CTM_H759_SPI0_CS_X_PORT               GPIOE
#define CTM_H759_SPI0_CS_X_PIN                GPIO_PIN_1
#define CTM_H759_SPI0_CS_R_PORT               GPIOE
#define CTM_H759_SPI0_CS_R_PIN                GPIO_PIN_0
#define CTM_H759_SPI0_CS_L_PORT               GPIOB
#define CTM_H759_SPI0_CS_L_PIN                GPIO_PIN_9

#if defined(CTM_H759_USE_RIGHT_MOTOR)
#define CTM_H759_ENC_CS_PORT                  CTM_H759_SPI0_CS_R_PORT
#define CTM_H759_ENC_CS_PIN                   CTM_H759_SPI0_CS_R_PIN
#else
#define CTM_H759_ENC_CS_PORT                  CTM_H759_SPI0_CS_L_PORT
#define CTM_H759_ENC_CS_PIN                   CTM_H759_SPI0_CS_L_PIN
#endif

#if (CTM_H759_ENCODER_INTERFACE == CTM_H759_ENCODER_IF_PWM)
#define CTM_H759_LEFT_ENC_PWM_PORT            GPIOB
#define CTM_H759_LEFT_ENC_PWM_PIN             GPIO_PIN_9
#define CTM_H759_LEFT_ENC_PWM_AF              GPIO_AF_2
#define CTM_H759_LEFT_ENC_PWM_TIMER           TIMER3
#define CTM_H759_LEFT_ENC_PWM_TIMER_CH        TIMER_CH_3
#define CTM_H759_LEFT_ENC_PWM_TIMER_IRQ       TIMER3_IRQn
#define CTM_H759_LEFT_ENC_PWM_TIMER_INT       TIMER_INT_CH3
#define CTM_H759_LEFT_ENC_PWM_TIMER_FLAG      TIMER_INT_FLAG_CH3
#define CTM_H759_LEFT_ENC_PWM_TIMER_OV_FLAG   TIMER_FLAG_CH3O

#define CTM_H759_RIGHT_ENC_PWM_PORT           GPIOB
#define CTM_H759_RIGHT_ENC_PWM_PIN            GPIO_PIN_8
#define CTM_H759_RIGHT_ENC_PWM_AF             GPIO_AF_2
#define CTM_H759_RIGHT_ENC_PWM_TIMER          TIMER3
#define CTM_H759_RIGHT_ENC_PWM_TIMER_CH       TIMER_CH_2
#define CTM_H759_RIGHT_ENC_PWM_TIMER_IRQ      TIMER3_IRQn
#define CTM_H759_RIGHT_ENC_PWM_TIMER_INT      TIMER_INT_CH2
#define CTM_H759_RIGHT_ENC_PWM_TIMER_FLAG     TIMER_INT_FLAG_CH2
#define CTM_H759_RIGHT_ENC_PWM_TIMER_OV_FLAG  TIMER_FLAG_CH2O

#if defined(CTM_H759_USE_RIGHT_MOTOR)
#define CTM_H759_ENC_PWM_PORT                 CTM_H759_RIGHT_ENC_PWM_PORT
#define CTM_H759_ENC_PWM_PIN                  CTM_H759_RIGHT_ENC_PWM_PIN
#define CTM_H759_ENC_PWM_AF                   CTM_H759_RIGHT_ENC_PWM_AF
#define CTM_H759_ENC_PWM_TIMER                CTM_H759_RIGHT_ENC_PWM_TIMER
#define CTM_H759_ENC_PWM_TIMER_CH             CTM_H759_RIGHT_ENC_PWM_TIMER_CH
#define CTM_H759_ENC_PWM_TIMER_IRQ            CTM_H759_RIGHT_ENC_PWM_TIMER_IRQ
#define CTM_H759_ENC_PWM_TIMER_INT            CTM_H759_RIGHT_ENC_PWM_TIMER_INT
#define CTM_H759_ENC_PWM_TIMER_FLAG           CTM_H759_RIGHT_ENC_PWM_TIMER_FLAG
#define CTM_H759_ENC_PWM_TIMER_OV_FLAG        CTM_H759_RIGHT_ENC_PWM_TIMER_OV_FLAG
#else
#define CTM_H759_ENC_PWM_PORT                 CTM_H759_LEFT_ENC_PWM_PORT
#define CTM_H759_ENC_PWM_PIN                  CTM_H759_LEFT_ENC_PWM_PIN
#define CTM_H759_ENC_PWM_AF                   CTM_H759_LEFT_ENC_PWM_AF
#define CTM_H759_ENC_PWM_TIMER                CTM_H759_LEFT_ENC_PWM_TIMER
#define CTM_H759_ENC_PWM_TIMER_CH             CTM_H759_LEFT_ENC_PWM_TIMER_CH
#define CTM_H759_ENC_PWM_TIMER_IRQ            CTM_H759_LEFT_ENC_PWM_TIMER_IRQ
#define CTM_H759_ENC_PWM_TIMER_INT            CTM_H759_LEFT_ENC_PWM_TIMER_INT
#define CTM_H759_ENC_PWM_TIMER_FLAG           CTM_H759_LEFT_ENC_PWM_TIMER_FLAG
#define CTM_H759_ENC_PWM_TIMER_OV_FLAG        CTM_H759_LEFT_ENC_PWM_TIMER_OV_FLAG
#endif

/* 2 MHz capture tick gives 0.5 us resolution for a 15-bit PWM encoder. */
#define CTM_H759_ENC_PWM_TIMER_TICK_HZ        2000000U
#define CTM_H759_ENC_PWM_CPR                  32768U
#define CTM_H759_ENC_PWM_MIN_HIGH_TICKS       2U
#define CTM_H759_ENC_PWM_MIN_PERIOD_TICKS     1000U
#define CTM_H759_ENC_PWM_MAX_PERIOD_TICKS     40000U
#define CTM_H759_ENC_PWM_INPUT_FILTER         8U
#define CTM_H759_ENC_PWM_PERIOD_TOL_PCT       25U
#define CTM_H759_ENC_PWM_PERIOD_LOCK_SAMPLES  3U
#define CTM_H759_ENC_PWM_MAX_STEP_COUNTS      (CTM_H759_ENC_PWM_CPR / 16U)
#endif

/* CAN transceiver ----------------------------------------------------------- */
#define CTM_H759_CAN                          CAN2
#define CTM_H759_CAN_IDX                      IDX_CAN2
#define CTM_H759_CAN_RX_AF                    GPIO_AF_5
#define CTM_H759_CAN_TX_AF                    GPIO_AF_5
#define CTM_H759_CAN_RX_PORT                  GPIOD
#define CTM_H759_CAN_RX_PIN                   GPIO_PIN_12
#define CTM_H759_CAN_TX_PORT                  GPIOD
#define CTM_H759_CAN_TX_PIN                   GPIO_PIN_13
#define CTM_H759_CAN_IRQ                      CAN2_Message_IRQn
#define CTM_H759_CAN_IRQHandler               CAN2_Message_IRQHandler
#define CTM_H759_CAN_RX_MAILBOX               0U
#define CTM_H759_CAN_TX_MAILBOX               1U

/* Debug, reset and simple inputs ------------------------------------------- */
#define CTM_H759_HAS_STATUS_LED           0
#define LED_ACT_SET()                     ((void) 0)
#define LED_ACT_RESET()                   ((void) 0)
#define LED_ACT_GET()                     (0U)

#define ENC_NCS_SET()                     GPIO_BOP(CTM_H759_ENC_CS_PORT) = (uint32_t) CTM_H759_ENC_CS_PIN
#define ENC_NCS_RESET()                   GPIO_BC(CTM_H759_ENC_CS_PORT) = (uint32_t) CTM_H759_ENC_CS_PIN

#define CTM_H759_SWDIO_PORT                   GPIOA
#define CTM_H759_SWDIO_PIN                    GPIO_PIN_13
#define CTM_H759_SWCLK_PORT                   GPIOA
#define CTM_H759_SWCLK_PIN                    GPIO_PIN_14
#define CTM_H759_KEY1_PORT                    GPIOA
#define CTM_H759_KEY1_PIN                     GPIO_PIN_0
#define CTM_H759_KEY2_PORT                    GPIOC
#define CTM_H759_KEY2_PIN                     GPIO_PIN_0

#endif /* __BOARD_GD32H759_H__ */
