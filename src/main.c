/*==================================================================================================
 * Project : RTD AUTOSAR 4.7
 * Platform : CORTEXM
 * Peripheral : S32K14X
 * Dependencies : none
 *
 * Autosar Version : 4.7.0
 * Autosar Revision : ASR_REL_4_7_REV_0000
 * Autosar Conf.Variant :
 * SW Version : 3.0.0
 * Build Version : S32K1_RTD_3_0_0_QLP04_D2509_ASR_REL_4_7_REV_0000_20250930
 *
 * Copyright 2020-2026 NXP
 *
 *   Application: Read PCT2075DP Temperature I2C Sensor and Send UART Messages
 *
 *   NXP Proprietary. This software is owned or controlled by NXP and may only be
 *   used strictly in accordance with the applicable license terms. By expressly
 *   accepting such terms or by downloading, installing, activating and/or otherwise
 *   using the software, you are agreeing that you have read, and that you agree to
 *   comply with and are bound by, such license terms. If you do not agree to be
 *   bound by the applicable license terms, then you may not retain, install,
 *   activate or otherwise use the software.
 ==================================================================================================*/

/**
 *   @file main.c
 *
 *   @addtogroup main_module main module documentation
 *   @{
 */

#ifdef __cplusplus
extern "C"{
#endif

/* Including necessary configuration files. */
#include "Mcal.h"
#include "Clock_Ip.h"
#include "OsIf.h"
#include "Port_Ci_Port_Ip_Cfg.h"
#include "Port_Ci_Port_Ip.h"
#include "Lpuart_Uart_Ip.h"
/* board_gpio.h is a committed, self-contained header in src/ that provides
 * Board_Gpio_WritePin and Board_Gpio_ReadPin via direct register access.
 * It does not depend on any generated RTD file and is not affected by
 * "Update Code" runs from S32 Configuration Tool. */
#include "board_gpio.h"
#include "pct2075dp.h"
#include <string.h>
#include <stdio.h>

/*
 * LPUART1 maps to the OpenSDA USB-UART bridge (PTC6 RX / PTC7 TX).
 * Instance number must match Lpuart_Uart_Ip_xHwConfigPB_1 in generated config.
 */
#define LPUART_INSTANCE     1U
#define SAMPLE_PERIOD_MS    1000U
#define MSG_BUFFER_SIZE     150U

/*
 * LEDs on FRDM-A-S32K144 are active-high:
 *   Board_Gpio_WritePin(base, pin, 1) -> LED on
 *   Board_Gpio_WritePin(base, pin, 0) -> LED off
 */

static char BannerMsg[] =
        "==============================================================\r\n"
        " Read PCT2075DP Temperature I2C Sensor and Send UART Messages\r\n"
        " Platform : NXP FRDM-A-S32K144 (S32K1 RTD AUTOSAR 4.7)\r\n"
        " Sensor   : PCT2075DP @ I2C 0x48 (LPI2C0), Console: LPUART1\r\n"
        "==============================================================\r\n\r\n";

static char FormattedMessage[MSG_BUFFER_SIZE];

/* -------------------------------------------------------------------------
 * Delay helper using OsIf system counter.
 * delayMs is treated as milliseconds.
 * ------------------------------------------------------------------------- */
static void Delay(uint32 delayMs)
{
    uint32 cur     = OsIf_GetCounter(OSIF_COUNTER_SYSTEM);
    uint32 elapsed = 0U;
    uint32 timeout = OsIf_MicrosToTicks(delayMs * 1000U, OSIF_COUNTER_SYSTEM);
    while (elapsed < timeout) {
        elapsed += OsIf_GetElapsed(&cur, OSIF_COUNTER_SYSTEM);
    }
}

/* -------------------------------------------------------------------------
 * UART transmit helper - sends every byte of a NUL-terminated string
 * using the LPUART IP synchronous send API.
 * ------------------------------------------------------------------------- */
static void SendMessage(const char *msg)
{
    uint32 msgLen = (uint32)strlen(msg);
    uint8  txByte;
    uint32 idx;

    for (idx = 0U; idx < msgLen; idx++) {
        txByte = (uint8)msg[idx];
        /* SyncSend blocks until the byte is transmitted. */
        (void)Lpuart_Uart_Ip_SyncSend(LPUART_INSTANCE, &txByte, 1U, 100000U);
    }
}

/* -------------------------------------------------------------------------
 * Brief UART activity indicator on the blue LED (heartbeat per sample).
 * Active-high: write 1 = on, write 0 = off.
 * ------------------------------------------------------------------------- */
static void BlinkBlueLed(void)
{
    Board_Gpio_WritePin(LED_BLUE_PORT, LED_BLUE_PIN, 1U);
    Delay(50U);
    Board_Gpio_WritePin(LED_BLUE_PORT, LED_BLUE_PIN, 0U);
}

/*!
 * \brief The main function for the project.
 * \details Startup sequence:
 *   - startup asm routine
 *   - main()
 */
int main(void)
{
    /* ------------------------------------------------------------------
     * MCU clock initialization (IP layer).
     * ------------------------------------------------------------------ */
    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);

    /* Initialize OsIf (used by Delay). */
    OsIf_Init(NULL_PTR);

    /* Initialize all configured pins. */
    Port_Ci_Port_Ip_Init(
        NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
        g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

    /* Initialize LPUART1. */
    Lpuart_Uart_Ip_Init(LPUART_INSTANCE, &Lpuart_Uart_Ip_xHwConfigPB_1);

    /* Initialize the PCT2075DP temperature sensor over LPI2C0. */
    (void)PCT2075DP_Init();

    /* ------------------------------------------------------------------
     * Safe initial state - all LEDs off (active-high, write 0 = off).
     * ------------------------------------------------------------------ */
    Board_Gpio_WritePin(LED_RED_PORT,   LED_RED_PIN,   0U);
    Board_Gpio_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, 0U);
    Board_Gpio_WritePin(LED_BLUE_PORT,  LED_BLUE_PIN,  0U);

    /* ------------------------------------------------------------------
     * One-time welcome banner.
     * ------------------------------------------------------------------ */
    SendMessage(BannerMsg);

    /* ------------------------------------------------------------------
     * Main loop: read temperature and print it once per second.
     * ------------------------------------------------------------------ */
    while (TRUE)
    {
        float tempC = 0.0f;

        if (PCT2075DP_GetTemp(&tempC) == PCT2075DP_OK)
        {
            /* Print with one decimal without relying on %f (which some
             * newlib-nano configs disable). Scale to tenths of a degree. */
            sint32 tenths = (sint32)(tempC * 10.0f);
            sint32 whole  = tenths / 10;
            sint32 frac   = tenths % 10;
            if (frac < 0) {
                frac = -frac;
            }
            (void)snprintf(FormattedMessage, sizeof(FormattedMessage),
                           "Temperature: %ld.%ld C\r\n",
                           (long)whole, (long)frac);
        }
        else
        {
            (void)snprintf(FormattedMessage, sizeof(FormattedMessage),
                           "Temperature read failed (I2C error)\r\n");
        }

        SendMessage(FormattedMessage);

        /* Visual heartbeat on each sample. */
        BlinkBlueLed();

        /* Wait ~1 second before the next sample (minus the 50 ms blink). */
        Delay(SAMPLE_PERIOD_MS - 50U);
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

/** @} */
