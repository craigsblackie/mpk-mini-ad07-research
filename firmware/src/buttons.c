/*
 * Matrix column 8 button cluster -- purpose reopened, NOT octave.
 *
 * Originally implemented as "octave up/down" based on FUN_080044fc's
 * toggle-between-two-values pattern (FIRMWARE_ANALYSIS.md's "Revised:
 * octave/program buttons" section). That interpretation is now
 * superseded: `FUN_08006988` (a separate, later-traced function reading
 * a *different* matrix column) was found to implement clean, unambiguous
 * increment/decrement/reset-to-default buttons directly modifying
 * record+0x02 -- the field `FUN_08004990` actually uses as the
 * keyboard's real octave in its note formula (`key_index +
 * record[2]*12 + record[3]`, see program.h). That's a far cleaner match
 * for "the real octave buttons" than this file's toggle-between-two-
 * values mechanism ever was. See transport.c, which implements that
 * confirmed mechanism on matrix column 7.
 *
 * This file's own mechanism (matrix column 8, confirmed via
 * FIRMWARE_ANALYSIS.md's tracing of FUN_080044fc back to the matrix
 * scanner) is still real and still implemented below, but its actual
 * purpose is open again -- `buttons_octave_offset` is computed but no
 * longer applied to note pitch anywhere (keys.c now uses
 * program_octave()/program_fine_transpose() instead). Left in place,
 * decoupled, rather than deleted, since the underlying input-reading
 * mechanism is confirmed real even though this project's guess at what
 * it's *for* wasn't.
 *
 * NOT YET CONFIRMED:
 *  - What this button cluster actually controls (previously guessed
 *    octave; now believed more likely something else -- program
 *    select, a mode/bank toggle, or a feature this project hasn't
 *    identified -- given the "toggle between two specific values"
 *    shape doesn't match a simple up/down counter as cleanly as
 *    transport.c's column-7 mechanism does).
 *  - Which specific bit is which (using bit 3/bit 2 here, matching the
 *    doc's bit numbering, without confirming which physical button
 *    that corresponds to).
 *  - Bit 1's "mode flag" behavior -- not reimplemented, just tracked.
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
