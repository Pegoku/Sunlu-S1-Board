/**
 * @file    dryer.h
 * @brief   Filament dryer application – public API
 *
 * Complete UI and control logic for the Sunlu S1 filament dryer.
 * Call Dryer_Init() once after peripheral init, then Dryer_Loop()
 * continuously from the main while(1).
 */
#ifndef DRYER_H
#define DRYER_H

#include "main.h"

/**
 * @brief  Initialise display, PWM outputs and internal state.
 *         Must be called after MX_xxx_Init() functions.
 */
void Dryer_Init(void);

/**
 * @brief  Non-blocking application tick.
 *         Handles buttons, temperature regulation, countdown
 *         and screen updates.  Call as fast as possible.
 */
void Dryer_Loop(void);

#endif /* DRYER_H */
