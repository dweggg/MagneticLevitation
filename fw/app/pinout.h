#ifndef PINOUT_H
#define PINOUT_H

#include "ch32fun.h"

/*
 * Board pin definitions
 * MCU: CH32X035G8U6
 */


/*
 * Analog measurements
 */
#define PIN_MAG_MEAS       PA2     // magMeas
#define PIN_V_MEAS         PA3     // vMeas
#define PIN_I_REF          PA4     // iRef
#define PIN_I_MEAS         PA5     // iMeas
#define PIN_TEMP_MEAS      PA6     // tempMeas


/*
 * USB
 *
 * CH32X035 USB pins:
 * UDP = USB D+
 * UDM = USB D-
 */
#define PIN_USB_DP         PC10    // UDP
#define PIN_USB_DM         PC11    // UDM


/*
 * UART
 */
#define PIN_UART_TX        PB3
#define PIN_UART_RX        PB4


/*
 * PWM outputs
 */
#define PIN_PWM_AL         PB6
#define PIN_PWM_BL         PB7
#define PIN_PWM_AH         PB9
#define PIN_PWM_BH         PB10
#define PIN_GD_EN          PB8


/*
 * LEDs
 */
#define PIN_LED_WHITE      PB11
#define PIN_LED_RED        PB12


/*
 * User buttons
 */
#define PIN_RST_BUTTON     PC3     // RST button

/*
 * Programming / debugging
 */
#define PIN_SWDIO          PC18
#define PIN_SWCLK          PC19

#endif // PINOUT_H