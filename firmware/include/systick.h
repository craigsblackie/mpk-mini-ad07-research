#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/*
 * SysTick-based millisecond timebase.
 *
 * Standard Cortex-M3 SysTick usage (not derived from the original
 * firmware's disassembly -- FIRMWARE_ANALYSIS.md found no evidence the
 * original uses SysTick at all, or any timer peripheral; its apparent
 * all-polling main-loop architecture doesn't need one). This gives
 * this replacement firmware a real wall-clock reference, replacing the
 * main-loop-iteration-counting placeholders previously used by
 * stuck_note.c's timeout and arp.c's step rate.
 */
void systick_init(void);

/* Milliseconds since systick_init(), wrapping at ~49.7 days (uint32_t
 * overflow) -- not handled specially, same as most embedded millis()
 * implementations. */
uint32_t systick_millis(void);

#endif /* SYSTICK_H */
