/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"
#include "app.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/
/* Secventa SALUT: 1=aprins 0=stins, fiecare element = 0.5s */
static const uint8_t g_salut[] = {
/* S: . . .   */ 1,0, 1,0, 1,  0,0,0,0,
/* A: . -     */ 1,0, 1,1,      0,0,0,0,
/* L: . - . . */ 1,0, 1,1,0, 1,0, 1,  0,0,0,0,
/* U: . . -   */ 1,0, 1,0, 1,1,  0,0,0,0,
/* T: -       */ 1,1,            0,0,0,0,0,0,0,0,
};

#define SECVENTA_SIZE (sizeof(g_salut) / sizeof(g_salut[0]))

static volatile uint8_t g_index = 0U;

void SysTick_Handler(void)
{
    /* Seteaza LED dupa valoarea curenta */
    if (g_salut[g_index] == 1U)
    {
        LED_RED_ON();
    }
    else
    {
        LED_RED_OFF();
    }

    /* Avanseaza la urmatorul element, reluand de la inceput */
    g_index++;
    if (g_index >= SECVENTA_SIZE)
    {
        g_index = 0U;
    }
}

int main(void)
{
    BOARD_InitHardware(); /* SysTick = 0.5s, din app.c */

    LED_RED_OFF();

    while (1)
    {
        /* Totul se intampla in SysTick_Handler */
    }
}


/*!
 * @brief Main function
 */
