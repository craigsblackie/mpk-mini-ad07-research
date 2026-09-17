#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/*
 * Octave up/down + mode-flag button handling. See buttons.c for the
 * full honesty-policy caveats on the input source and bit mapping.
 */

/* Current octave offset (applied to keys.c's note output), clamped to
 * +/-4 -- a typical range for a 25-key controller, not confirmed
 * against the original firmware's actual limits. */
extern int8_t buttons_octave_offset;

void buttons_init(void);

/* status_byte: the raw edge-detected input FUN_080044fc reads in the
 * original firmware. Source not yet confirmed -- main.c currently
 * passes matrix_state[7] as a placeholder (see buttons.c). */
void buttons_process(uint8_t status_byte);

#endif /* BUTTONS_H */
