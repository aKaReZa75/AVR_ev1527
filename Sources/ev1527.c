/**
 ******************************************************************************
 * @file     ev1527.c
 * @brief    EV1527 RF remote control decoder implementation for AVR
 * 
 * @author   Hossein Bagheri
 * @github   https://github.com/aKaReZa75
 * 
 * @note     This library decodes EV1527 protocol using Timer1 and INT0 interrupt
 *           to capture pulse widths and extract 24-bit data (20-bit address + 4-bit key).
 * 
 * @note     EXECUTION FLOW:
 *           1. Initialization Flow:
 *              └─> ev1527_Init() → Configure Timer1 (prescaler /8, normal mode)
 *                  → Configure INT0 (rising edge trigger) → Enable interrupt
 * 
 *           2. Interrupt State Machine Flow (INT0_vect):
 *              ┌─> First Trigger (Rising Edge):
 *              │   └─> Reset variables → Start Timer1 → Switch to falling edge detection
 *              │
 *              ├─> Falling Edge Detected:
 *              │   └─> Capture HIGH pulse width → Reset Timer1 → Switch to rising edge
 *              │
 *              ├─> Rising Edge Detected:
 *              │   └─> Capture LOW pulse width → Set measureDone flag
 *              │
 *              └─> Measurement Complete (measureDone=true):
 *                  ├─> If no preamble yet:
 *                  │   └─> Check preamble pattern (LOW 25-40× HIGH)
 *                  │       └─> If valid: Set preambleDetec flag
 *                  │
 *                  └─> If preamble detected:
 *                      └─> Validate pulse timing → Decode bit (compare HIGH/LOW)
 *                          → Store bit in rawValue → Increment index
 *                          → If 24 bits received: Set Detect flag → Disable decoder
 * 
 *           3. Data Extraction Flow (After 24 bits):
 *              └─> ev1527_Data.Bits.Detect = 1 → User reads Address & Keys
 *                  → User clears Detect flag → Re-enable decoder if needed
 * 
 *           4. Deinitialization Flow:
 *              └─> ev1527_deInit() → Disable INT0 → Stop Timer1 → Clear flags
 * 
 * @note     FUNCTION SUMMARY:
 *           - ISR(INT0_vect)  : Interrupt handler for edge detection and pulse measurement
 *           - ev1527_Init     : Initialize Timer1 and INT0 for RF signal capture
 *           - ev1527_deInit   : Disable Timer1 and INT0 to stop decoding
 * 
 * @note     Timing Analysis (16MHz AVR with Timer1 prescaler /8):
 *           - Timer resolution: 0.5µs per tick (16MHz / 8 = 2MHz)
 *           - EV1527 base period (T): ~300-350µs (600-700 ticks)
 *           - Logic '0': HIGH=300µs, LOW=900µs (600 + 1800 ticks)
 *           - Logic '1': HIGH=900µs, LOW=300µs (1800 + 600 ticks)
 *           - Preamble: LOW=~10ms, HIGH=~320µs (20000 + 640 ticks)
 * 
 * @note     Edge Detection Strategy:
 *           - Start: Rising edge (initial trigger)
 *           - Switch to falling edge after first rising
 *           - Measure HIGH pulse on falling edge
 *           - Measure LOW pulse on rising edge
 *           - Toggle edge detection after each capture
 * 
 * @note     For detailed documentation with waveform diagrams, visit:
 *           https://github.com/aKaReZa75/AVR_EV1527
 ******************************************************************************
 */

#include "ev1527.h"


/* ============================================================================
 *                         GLOBAL VARIABLES
 * ============================================================================ */
volatile ev1527_T ev1527_Data = {.rawValue = 0x0};  /**< Decoded RF data structure - volatile for ISR access */


/* ============================================================================
 *                         INTERRUPT SERVICE ROUTINE
 * ============================================================================ */

/* -------------------------------------------------------
 * @brief External interrupt service routine for INT0 (RF data pin)
 * @retval None
 * @note This ISR implements a state machine to decode EV1527 protocol:
 *       1. Detects first edge and initializes measurement
 *       2. Alternates between measuring HIGH and LOW pulses
 *       3. Validates preamble pattern
 *       4. Decodes 24 data bits using pulse width comparison
 *       5. Automatically disables decoder after successful reception
 * @note State machine variables:
 *       - firstTime_Trigger: true=waiting for first edge, false=measuring
 *       - measureDone: true=both HIGH and LOW captured, ready to decode
 *       - preambleDetec: true=valid preamble found, ready for data bits
 *       - _Index: current bit position (0-23)
 * ------------------------------------------------------- */
ISR(INT0_vect) 
{
  /* State machine static variables (persist between ISR calls) */
  static bool firstTime_Trigger = true;                    /**< Flag: true=first edge not yet detected, initialize on first edge */
  static bool measureDone = false;                         /**< Flag: true=complete HIGH+LOW pulse measured, ready to process */
  static bool preambleDetec = false;                       /**< Flag: true=valid preamble detected, start decoding data bits */
  static uint8_t _Index = 0;                               /**< Current bit index (0-23) in 24-bit data frame */
  static uint16_t Signal_High_Tick = 0;                    /**< Duration of HIGH pulse in timer ticks */
  static uint16_t Signal_Low_Tick = 0;                     /**< Duration of LOW pulse in timer ticks */
  
  /* ===== FIRST EDGE DETECTION (INITIALIZATION) ===== */
  if(firstTime_Trigger)                                    /**< First edge detected - initialize decoder */
  {
    /* Reset all measurement variables */
    Signal_High_Tick = 0x00;                               /**< Clear HIGH pulse measurement */
    Signal_Low_Tick  = 0x00;                               /**< Clear LOW pulse measurement */
    _Index = 0;                                            /**< Reset bit index to start */
    ev1527_Data.rawValue = 0x0;                            /**< Clear decoded data buffer */
    preambleDetec = false;                                 /**< Clear preamble detection flag */
    
    /* Start timer and configure for next edge */
    EV_Timer_Reset;                                        /**< Reset Timer1 to 0 to start timing */
    firstTime_Trigger = false;                             /**< Mark initialization complete */
    bitClear(EICRA, ISC00);                                /**< Set INT0 to falling edge (ISC01=1, ISC00=0) */
  }
  /* ===== SUBSEQUENT EDGES (MEASUREMENT) ===== */
  else
  {
    /* Check which edge triggered the interrupt */
    if(bitCheck(EICRA, ISC00))                             /**< Check ISC00 bit: 1=rising edge just detected */
    {
      /* Rising edge detected - LOW pulse measurement complete */
      Signal_Low_Tick = EV_Timer_Value;                    /**< Capture LOW pulse duration from timer */
      measureDone = true;                                  /**< Set flag: complete HIGH+LOW pulse captured */
      bitClear(EICRA, ISC00);                              /**< Switch to falling edge detection for next pulse */
    }
    else                                                   /**< ISC00=0: falling edge just detected */
    {
      /* Falling edge detected - HIGH pulse measurement complete */
      Signal_High_Tick = EV_Timer_Value;                   /**< Capture HIGH pulse duration from timer */
      EV_Timer_Reset;                                      /**< Reset timer to start measuring LOW pulse */
      bitSet(EICRA, ISC00);                                /**< Switch to rising edge detection for next pulse */
    }
  };
  
  /* ===== PULSE PROCESSING (DECODE BITS) ===== */
  if(measureDone)                                          /**< Check if complete HIGH+LOW pulse captured */
  {
    /* Check if preamble already detected */
    if(preambleDetec)                                      /**< Preamble found - decode data bits */
    {
      /* Validate pulse timing is within acceptable range */
      if(EV_pulseIsValid(Signal_Low_Tick, Signal_High_Tick))  /**< Check if pulse duration is valid (450-8500 ticks) */
      {
        /* Decode bit and store in result */
        bitChange(ev1527_Data.rawValue, _Index, EV_bitCheck(Signal_Low_Tick, Signal_High_Tick));  /**< Decode bit: HIGH≥1.5×LOW → '1', else '0' */
        _Index++;                                          /**< Move to next bit position */
        
        /* Check if all 24 bits received */
        if(_Index > EV_maxIndexData)                       /**< Check if index exceeded 23 (all 24 bits received) */
        {
          ev1527_Data.Bits.Detect = true;                  /**< Set detection flag - valid code received */
          firstTime_Trigger = true;                        /**< Reset state machine for next frame */
          preambleDetec = false;                           /**< Clear preamble flag */
          ev1527_deInit();                                 /**< Disable decoder (prevent re-triggering until manually re-enabled) */
        };
      }
      else                                                 /**< Invalid pulse timing */
      {
        /* Reset decoder on invalid pulse */
        firstTime_Trigger = true;                          /**< Reset state machine */
        preambleDetec = false;                             /**< Clear preamble flag */
        bitSet(EICRA, ISC00);                              /**< Set to rising edge detection */
      };
    }
    /* Preamble not yet detected - check for preamble pattern */
    else
    {
      /* Check if pulse matches preamble timing (LOW 25-40× HIGH) */
      if(EV_PrembleCheck(Signal_Low_Tick, Signal_High_Tick))  /**< Validate preamble pattern */
      {   
        preambleDetec = true;                              /**< Set preamble detection flag - ready to decode data */
      }
      else
      {
        /* Not a preamble - continue waiting */
      };
    };
    
    /* Prepare for next pulse measurement */
    EV_Timer_Reset;                                        /**< Reset timer for next pulse */
    measureDone = false;                                   /**< Clear measurement complete flag */
  };
};


/* ============================================================================
 *                       INITIALIZATION FUNCTION
 * ============================================================================ */

/* -------------------------------------------------------
 * @brief Initialize EV1527 decoder hardware (Timer1 and INT0)
 * @retval None
 * @note Configuration:
 *       - Timer1: Normal mode, prescaler /8 (0.5µs resolution at 16MHz)
 *       - INT0: Rising edge trigger initially, enabled
 * @note Must call this before attempting to decode RF signals
 *       Global interrupts (sei()) must be enabled separately
 * ------------------------------------------------------- */
void ev1527_Init(void)
{
  /* ===== Configure INT0 External Interrupt ===== */
  GPIO_Config_INPUT(DDRD, 2);
  /* Set INT0 to trigger on rising edge (ISC01:ISC00 = 11) */
  bitSet(EICRA, ISC00);                                    /**< ISC00=1: Rising edge select (part 1) */
  bitSet(EICRA, ISC01);                                    /**< ISC01=1: Rising edge select (part 2) */
  
  /* Enable INT0 interrupt */
  bitSet(EIMSK, INT0);                                     /**< Enable INT0 in External Interrupt Mask Register */
  
  /* ===== Configure Timer1 ===== */
  /* Set Timer1 to Normal mode (WGM13:WGM10 = 0000) */
  bitClear(TCCR1A, WGM10);                                 /**< WGM10=0: Normal mode (part 1) */
  bitClear(TCCR1A, WGM11);                                 /**< WGM11=0: Normal mode (part 2) */
  bitClear(TCCR1B, WGM12);                                 /**< WGM12=0: Normal mode (part 3) */
  
  /* Set Timer1 prescaler to /8 (CS12:CS10 = 010) */
  /* At 16MHz: Timer frequency = 16MHz/8 = 2MHz → 0.5µs per tick */
  bitClear(TCCR1B, CS10);                                  /**< CS10=0: Prescaler /8 (part 1) */
  bitSet(TCCR1B, CS11);                                    /**< CS11=1: Prescaler /8 (part 2) */
  bitClear(TCCR1B, CS12);                                  /**< CS12=0: Prescaler /8 (part 3) */
};


/* ============================================================================
 *                       DEINITIALIZATION FUNCTION
 * ============================================================================ */

/* -------------------------------------------------------
 * @brief Disable EV1527 decoder and release hardware resources
 * @retval None
 * @note Deinitialization sequence:
 *       1. Disable INT0 external interrupt
 *       2. Stop Timer1 (set prescaler to 0 = no clock source)
 *       3. Set Timer1 to normal mode (clear all WGM bits)
 * @note Use this to save power when RF reception not needed
 *       or after successful code reception to prevent re-triggering
 * ------------------------------------------------------- */
void ev1527_deInit(void)
{
  /* ===== Disable INT0 External Interrupt ===== */
  /* Clear INT0 edge detection configuration */
  bitClear(EICRA, ISC00);                                  /**< ISC00=0: Disable edge detection (part 1) */
  bitClear(EICRA, ISC01);                                  /**< ISC01=0: Disable edge detection (part 2) */
  
  /* Disable INT0 interrupt */
  bitClear(EIMSK, INT0);                                   /**< Disable INT0 in External Interrupt Mask Register */
  
  /* ===== Disable Timer1 ===== */
  /* Clear Timer1 mode configuration (set to Normal mode - all WGM bits = 0) */
  bitClear(TCCR1A, WGM10);                                 /**< WGM10=0: Clear mode bit */
  bitClear(TCCR1A, WGM11);                                 /**< WGM11=0: Clear mode bit */
  bitClear(TCCR1B, WGM12);                                 /**< WGM12=0: Clear mode bit */
  
  /* Stop Timer1 by clearing prescaler (CS12:CS10 = 000 = No clock source) */
  bitClear(TCCR1B, CS10);                                  /**< CS10=0: Stop timer (part 1) */
  bitClear(TCCR1B, CS11);                                  /**< CS11=0: Stop timer (part 2) */
  bitClear(TCCR1B, CS12);                                  /**< CS12=0: Stop timer (part 3) - redundant but ensures complete stop */
};
