/*
 * Octave up/down buttons.
 *
 * Reimplements the behavior described in FIRMWARE_ANALYSIS.md's
 * "Revised: octave/program buttons" section (FUN_080044fc, medium-high
 * confidence): a single status byte is edge-detected against its
 * previous value, and two of its bits each toggle a state between two
 * values -- interpreted there as octave up/down (a toggle-between-two-
 * states pattern rather than a simple increment/decrement, but the
 * *effect* an octave button should have is "move up/down one octave
 * per press", which is what this reimplements -- an original take on
 * the same observed behavior, not a transcription of the exact FSM).
 *
 * NOT YET CONFIRMED (same placeholder-honesty policy as keys.c):
 *  - The actual source of the status byte FUN_080044fc reads. It isn't
 *    established whether this comes from the key/pad matrix at all, or
 *    a separate GPIO/status register. main.c currently passes
 *    matrix_state[7] (one of the two "extra" columns matrix.c's own
 *    comment flags as carrying fewer physical inputs -- likely
 *    transport/other buttons) as a reasonable placeholder.
 *  - Which specific bit is octave-up vs. octave-down (using bit 3 =
 *    up, bit 2 = down here, matching the doc's bit numbering, but
 *    without confirming which physical button that corresponds to).
 *  - Bit 1's "mode flag" behavior -- not reimplemented, just tracked
 *    and exposed for whatever later turns out to need it.
 *  - The original's exact octave range limits -- clamped to +/-4 here
 *    as a reasonable placeholder for a 25-key controller.
 */
#include "buttons.h"

#define BIT_MODE (1u << 1)
#define BIT_OCTAVE_DOWN (1u << 2)
#define BIT_OCTAVE_UP (1u << 3)

int8_t buttons_octave_offset;
static uint8_t previous_status;
static uint8_t mode_flag;

void buttons_init(void)
{
	buttons_octave_offset = 0;
	previous_status = 0;
	mode_flag = 0;
}

void buttons_process(uint8_t status_byte)
{
	uint8_t changed = status_byte ^ previous_status;
	previous_status = status_byte;

	if (changed & BIT_MODE) {
		if (status_byte & BIT_MODE) {
			mode_flag = !mode_flag;
		}
	}

	if (changed & BIT_OCTAVE_UP) {
		if ((status_byte & BIT_OCTAVE_UP) && buttons_octave_offset < 4) {
			buttons_octave_offset++;
		}
	}

	if (changed & BIT_OCTAVE_DOWN) {
		if ((status_byte & BIT_OCTAVE_DOWN) && buttons_octave_offset > -4) {
			buttons_octave_offset--;
		}
	}
}
