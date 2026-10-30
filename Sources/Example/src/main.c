/**
 ******************************************************************************
 * @file     main.c
 * @brief    EV1527 RF remote receiver example for AVR
 * 
 * @author   Hossein Bagheri
 * @github   https://github.com/aKaReZa75/AVR_ev1527
 * 
 * @note     This example demonstrates:
 *           - Decoding EV1527 RF remote frames (address and key bits)
 *           - Interrupt-driven reception with a detection flag
 *           - Printing the received address and keys over USART
 ******************************************************************************
 */

#include "aKaReZa.h"

/* ============================================================================
 *                            GLOBAL VARIABLES
 * ============================================================================ */

/* -------------------------------------------------------
 * @brief Text buffer used to format the serial output line
 * ------------------------------------------------------- */
char debugBuffer[100];

/* -------------------------------------------------------
 * @brief Reception state shared with the EV1527 interrupt handler
 * @note Defined in the EV1527 module and updated inside the ISR
 *       ev1527_Data.Bits.Detect : set when a complete frame is received
 *       ev1527_Data.Bits.Address: remote address of the received frame
 *       ev1527_Data.Bits.Keys   : pressed key bits of the received frame
 * ------------------------------------------------------- */
extern volatile bool ev1527_Detec;
extern volatile ev1527_T ev1527_Data;

/* ============================================================================
 *                             MAIN PROGRAM
 * ============================================================================ */

/* -------------------------------------------------------
 * @brief Application entry point
 * @retval None
 * @note Initializes USART and the EV1527 decoder, then prints every
 *       received frame
 *       The decoder runs from interrupts, so global interrupts must be enabled
 * ------------------------------------------------------- */
int main(void)
{
    /* Initialize the serial port and print a start-up message */
    usart_Init(Initialize);
    usart_Putsln("aKaReZa");

    /* Start the EV1527 decoder and enable global interrupts */
    ev1527_Init();
    globalInt_Enable;

    while(1)
    {
        /* A complete frame has been received by the interrupt handler */
        if(ev1527_Data.Bits.Detect)
        { 
            /* Print the address and keys in hexadecimal */
            sprintf(debugBuffer, "ev1527 => Address: 0x%lX, Keys:%x", (uint32_t) ev1527_Data.Bits.Address, ev1527_Data.Bits.Keys);
            usart_Putsln(debugBuffer);

            /* Clear the flag and restart the decoder to wait for the next frame */
            ev1527_Data.Bits.Detect = false;
            ev1527_Init();
        };
    };
};