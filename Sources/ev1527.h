/**
 ******************************************************************************
 * @file     ev1527.h
 * @brief    EV1527 RF remote control decoder header for AVR microcontrollers
 * 
 * @author   Hossein Bagheri
 * @github   https://github.com/aKaReZa75
 * 
 * @note     This library provides complete decoder for EV1527 encoding protocol
 *           used in 433MHz/315MHz RF remote controls and transmitters.
 * 
 * @note     FUNCTION SUMMARY:
 *           - ev1527_Init   : Initialize Timer1 and external interrupt for RF signal capture
 *           - ev1527_deInit : Disable Timer1 and external interrupt to stop decoding
 * 
 * @note     EV1527 Protocol Specifications:
 *           - Encoding: Manchester-like pulse width modulation
 *           - Data format: 24 bits total (20-bit address + 4-bit data/key)
 *           - Bit encoding:
 *             * Logic '0': Short HIGH (1×T) + Long LOW (3×T)
 *             * Logic '1': Long HIGH (3×T) + Short LOW (1×T)
 *           - Preamble: Very long LOW pulse (31×T) + Short HIGH (1×T)
 *           - Base period (T): ~300-350µs typical
 *           - Frame structure: [Preamble][20-bit Address][4-bit Key][Sync]
 * 
 * @note     Timing Specifications:
 *           - Minimum pulse width: ~300µs (1×T)
 *           - Maximum pulse width: ~1200µs (4×T for preamble)
 *           - Preamble LOW: 25-40× longer than preamble HIGH
 *           - Valid pulse range: 450-8500 timer ticks
 *           - Bit decision: HIGH ≥ 1.5× LOW duration → '1', else → '0'
 * 
 * @note     Hardware Requirements:
 *           - 433MHz/315MHz RF receiver module connected to external interrupt pin
 *           - Timer1 configured for precise timing measurement (µs resolution)
 *           - External interrupt on data pin (falling/rising edge detection)
 *           - Receiver output: Active LOW during transmission
 * 
 * @note     Decoding Flow:
 *           1. External interrupt detects edge transition
 *           2. Timer1 measures pulse width (HIGH or LOW duration)
 *           3. Validate pulse is within acceptable range
 *           4. Detect preamble (long LOW followed by short HIGH)
 *           5. Decode 24 data bits using pulse width comparison
 *           6. Extract 20-bit address and 4-bit key code
 *           7. Set detection flag when valid frame received
 * 
 * @note     Usage Example:
 *           ev1527_T remote;
 *           ev1527_Init();              // Initialize decoder
 *           
 *           // In main loop or interrupt handler:
 *           if (remote.Bits.Detect) {   // Check if valid code received
 *               uint32_t address = remote.Bits.Address;  // 20-bit address
 *               uint8_t key = remote.Bits.Keys;          // 4-bit key (0-15)
 *               remote.Bits.Detect = 0;  // Clear flag
 *           }
 * 
 * @note     Common Applications:
 *           - Wireless remote controls (garage doors, gates, alarms)
 *           - RF switches and relay controllers
 *           - Home automation devices
 *           - Security systems
 * 
 * @note     For detailed documentation with waveform diagrams, visit:
 *           https://github.com/aKaReZa75/AVR_EV1527
 ******************************************************************************
 */
#ifndef _ev1527_H_
#define _ev1527_H_

#include "aKaReZa.h"


/* ============================================================================
 *                    CRITICAL DEPENDENCY CHECK
 * ============================================================================
 *  This library requires the aKaReZa.h base library to compile correctly.
 *  If the file is missing, please download it or contact for support.
 * ============================================================================ */
#ifndef _aKaReZa_H_
    #warning "============================================================"
    #warning " [WARNING] Missing required dependency: aKaReZa.h"
    #warning "------------------------------------------------------------"
    #warning "  This module depends on the aKaReZa.h base library."
    #warning "  Please download it from: https://github.com/aKaReZa75/AVR_RawProject"
    #warning "  Or contact for support: akaReza75@gmail.com"
    #warning "------------------------------------------------------------"
    #error   "Compilation aborted: Required file 'aKaReZa.h' not found!"
    #warning "============================================================"
#endif


/* ============================================================================
 *                         TIMER CONTROL MACROS
 * ============================================================================ */

/**
 * @brief Reset Timer1 counter to zero
 * @note Used at start of pulse measurement to begin timing
 */
#define EV_Timer_Reset TCNT1 = 0x0000    /**< Clear Timer1 counter register for new measurement */

/**
 * @brief Read current Timer1 counter value
 * @note Returns elapsed time since last reset in timer ticks
 */
#define EV_Timer_Value TCNT1             /**< Current Timer1 count value (elapsed ticks) */


/* ============================================================================
 *                         PROTOCOL PARAMETERS
 * ============================================================================ */

#define EV_maxIndexData  23              /**< Maximum bit index (0-23 for 24 bits total: 20 address + 4 key) */


/* ============================================================================
 *                         TIMING THRESHOLDS
 * ============================================================================ */

/**
 * @brief Minimum valid pulse duration in timer ticks
 * @note Pulses shorter than this are considered noise
 *       Typical: ~150µs at 16MHz with prescaler 8 → 450 ticks
 */
#define HPL_min          450             /**< Minimum combined HIGH+LOW pulse duration (noise filter) */

/**
 * @brief Maximum valid pulse duration in timer ticks
 * @note Pulses longer than this are invalid or timeout
 *       Typical: ~3ms at 16MHz with prescaler 8 → 8500 ticks
 */
#define HPL_Max          8500            /**< Maximum combined HIGH+LOW pulse duration (timeout threshold) */


/* ============================================================================
 *                         VALIDATION MACROS
 * ============================================================================ */

/**
 * @brief Validate if pulse duration is within acceptable range
 * @param _tickLow: LOW pulse duration in timer ticks
 * @param _tickHigh: HIGH pulse duration in timer ticks
 * @retval true if total pulse duration is valid, false otherwise
 * @note Filters out noise and invalid pulses
 *       Valid range: HPL_min (450) to HPL_Max (8500) ticks
 */
#define EV_pulseIsValid(_tickLow, _tickHigh)  (((_tickLow + _tickHigh) > HPL_min) && ((_tickLow + _tickHigh) < HPL_Max))

/**
 * @brief Check if pulse pattern matches EV1527 preamble
 * @param _tickLow: LOW pulse duration (should be very long)
 * @param _tickHigh: HIGH pulse duration (should be short)
 * @retval true if pattern matches preamble timing, false otherwise
 * @note Preamble timing: LOW pulse is 25-40× longer than HIGH pulse
 *       Example: LOW=10000µs, HIGH=320µs → ratio=31.25 → valid preamble
 *       This marks the start of a valid EV1527 data frame
 */
#define EV_PrembleCheck(_tickLow, _tickHigh)  ((_tickLow >= 25*_tickHigh) && (_tickLow <= 40*_tickHigh))

/**
 * @brief Decode bit value from pulse width comparison
 * @param _tickLow: LOW pulse duration in timer ticks
 * @param _tickHigh: HIGH pulse duration in timer ticks
 * @retval 1 if HIGH duration ≥ 1.5× LOW duration (logic '1'), else 0 (logic '0')
 * @note EV1527 bit encoding:
 *       - Logic '0': Short HIGH (1×T) + Long LOW (3×T) → HIGH/LOW ratio ≈ 0.33
 *       - Logic '1': Long HIGH (3×T) + Short LOW (1×T) → HIGH/LOW ratio ≈ 3.0
 *       - Threshold: 1.5× provides robust discrimination
 * @note Example:
 *       - Bit '0': HIGH=300µs, LOW=900µs → 300 < 1.5×900 → returns 0
 *       - Bit '1': HIGH=900µs, LOW=300µs → 900 ≥ 1.5×300 → returns 1
 */
#define EV_bitCheck(_tickLow, _tickHigh)      ((_tickHigh >= 1.5*_tickLow) ? 1 : 0)


/* ============================================================================
 *                         DATA STRUCTURE
 * ============================================================================ */

/**
 * @brief EV1527 decoded data structure with bit-field access
 * @note Union allows access to 32-bit value or individual bit fields
 *       Total: 32 bits (24 data bits + 1 detect flag + 7 reserved)
 */
typedef union 
{
    uint32_t rawValue;                   /**< Direct 32-bit access to entire decoded value */
    
    struct
    {
        uint32_t Address : 20;           /**< 20-bit unique transmitter address (0 to 1,048,575) */
        uint32_t Keys    : 4;            /**< 4-bit key/button code (0 to 15) - identifies which button pressed */
        uint32_t Detect  : 1;            /**< Detection flag: 1=valid code received, 0=no detection */
        uint32_t Reserve : 7;            /**< Reserved bits for future use or alignment */
    } Bits;                              /**< Bit-field structure for easy field access */
} ev1527_T;


/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize EV1527 decoder hardware (Timer1 and external interrupt)
 * @retval None
 * @note Initialization sequence:
 *       1. Configure Timer1 for pulse width measurement
 *          - Set prescaler for µs resolution (typically /8 at 16MHz)
 *          - Enable normal counting mode (count up from 0)
 *       2. Configure external interrupt on RF data pin
 *          - Set to trigger on both edges (rising and falling)
 *          - Enable interrupt for pulse detection
 *       3. Initialize global variables for decoding state machine
 * @note Must be called before attempting to decode RF signals
 * @note Global interrupts (sei()) must be enabled for operation
 */
void ev1527_Init(void);

/**
 * @brief Disable EV1527 decoder and release hardware resources
 * @retval None
 * @note Deinitialization sequence:
 *       1. Disable external interrupt (stop pulse detection)
 *       2. Stop Timer1 (disable clock source)
 *       3. Clear any pending interrupt flags
 *       4. Reset decoder state machine variables
 * @note Use this to save power when RF reception not needed
 *       or before reconfiguring hardware for other purposes
 */
void ev1527_deInit(void);

#endif /* _ev1527_H_ */