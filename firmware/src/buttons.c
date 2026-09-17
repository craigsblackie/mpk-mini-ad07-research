/*
 * Matrix column 8 button cluster: pad output mode select (Note/CC/PC).
 *
 * Originally implemented as "octave up/down" (FUN_080044fc's toggle-
 * between-two-values pattern). That guess is now resolved properly:
 * reading the function in full and tracing what its state variables
 * actually feed (via `get_xrefs_to`) shows `DAT_080045c0+2` is read by
 * `FUN_08003ab8` (the pad velocity handler, as `*DAT_08003e18` -- the
 * same SRAM address, 0x20000023, under a different literal-pool
 * alias) -- the shared runtime variable this project's `pads.c` and
 * `program.c` already call the "pad output mode" (Note=1/CC=2/PC=3),
 * previously only confirmed from the consumer side. This is the other
 * half: **these are the buttons that select it.**
 *
 * Confirmed logic, reproduced from the decompiled source:
 *  - **Bit 2**: pressing it toggles pad mode between CC and Note --
 *    if currently CC, back to Note; otherwise, to CC.
 *  - **Bit 3**: pressing it toggles pad mode between Program Change
 *    and Note, the same way.
 *  - **Bit 1**: sets a local flag (`mode_flag` below) and a separate
 *    byte to a reset sentinel (0xFF) -- purpose not reimplemented,
 *    tracked only.
 *  - **Bit 0**: the complement/idle case -- clears that same flag,
 *    same sentinel write. Not reimplemented beyond tracking the flag.
 *
 * Both the mode-flag byte this sets and the 0xFF-sentinel byte are
 * read elsewhere in the original by functions this project hasn't
 * traced -- genuinely open, not guessed at.
 */
#include "buttons.h"
#include "program.h"

#define BIT_IDLE (1u << 0)
#define BIT_ALT (1u << 1)
#define BIT_CC_MODE (1u << 2)
#define BIT_PC_MODE (1u << 3)

static uint8_t previous_status;
static uint8_t mode_flag;

void buttons_init(void)
{
	previous_status = 0;
	mode_flag = 0;
}

void buttons_process(uint8_t status_byte)
{
	uint8_t changed = status_byte ^ previous_status;
	previous_status = status_byte;

	if (changed & BIT_ALT) {
		if (status_byte & BIT_ALT) {
			mode_flag = 1;
		}
	}

	if (changed & BIT_IDLE) {
		if (status_byte & BIT_IDLE) {
			mode_flag = 0;
		}
	}

	if ((changed & BIT_CC_MODE) && (status_byte & BIT_CC_MODE)) {
		if (program_pad_mode() == PAD_MODE_CC) {
			program_set_pad_mode(PAD_MODE_NOTE);
		} else {
			program_set_pad_mode(PAD_MODE_CC);
		}
	}

	if ((changed & BIT_PC_MODE) && (status_byte & BIT_PC_MODE)) {
		if (program_pad_mode() == PAD_MODE_PC) {
			program_set_pad_mode(PAD_MODE_NOTE);
		} else {
			program_set_pad_mode(PAD_MODE_PC);
		}
	}
}
