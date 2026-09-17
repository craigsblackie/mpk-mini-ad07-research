/*
 * Matrix column 7 button cluster: sustain pedal, octave up/down, tap
 * tempo.
 *
 * Reimplements behavior found by fully tracing `FUN_08006988` (live
 * Ghidra access) -- a function this project had earlier only
 * partially characterized as a "tap-tempo candidate, lowest
 * confidence." Reading it in full resolves much more:
 *
 * - Its input byte is confirmed, via `get_xrefs_to` on the SRAM
 *   address, to be written by the matrix scanner at **column 7**
 *   (`0x20000012`, `pcVar2[2]` in the scanner -- the *other* "extra"
 *   column, distinct from column 8's cluster in buttons.c).
 * - **Bit 2** (mask 0x04) is tracked by level (not edge) and sends
 *   Control Change 64 -- the standard MIDI **sustain pedal** CC --
 *   value 127 while held, 0 on release. Unambiguous: CC 64 with a
 *   momentary press/release shape is definitionally a sustain pedal.
 * - **Bits 4 and 5** (0x10, 0x20) directly increment/decrement
 *   record+0x02 (clamped 0-8), with holding both together (0x30)
 *   resetting it to 4 (the factory default). record+0x02 is the same
 *   field `FUN_08004990` uses as the keyboard's real octave in its
 *   note formula (see program.h's `program_octave()`) -- so these are
 *   the **real octave up/down buttons**, on column 7, not column 8 as
 *   this project originally guessed (see buttons.c's header for that
 *   reinterpretation).
 * - **Bit 1** (mask 0x02) drives a **tap-tempo** calculation: each
 *   press logs an interval since the previous one into a small ring
 *   buffer, and once enough taps have accumulated (record+0x09,
 *   confirmed factory default 3 -- plausibly the tap count required,
 *   though this project didn't independently verify that specific
 *   role for the field), averages them into a tempo value clamped to
 *   250-2000ms per interval. Reimplemented simplified in arp.c's
 *   `arp_tap()`: this project's version uses the single most recent
 *   tap interval directly rather than an N-tap rolling average --
 *   same feature, simpler math, not a byte-exact port.
 * - **Bit 0** (mask 0x01) and further bit-3 (mask 0x08) logic exists
 *   in the original around arp-hold-array resets but wasn't resolved
 *   with enough confidence to reimplement -- not wired up here.
 */
#include "transport.h"
#include "midi_ring.h"
#include "program.h"
#include "arp.h"

#define BIT_TAP (1u << 1)
#define BIT_SUSTAIN (1u << 2)
#define BIT_OCTAVE_DOWN (1u << 4)
#define BIT_OCTAVE_UP (1u << 5)
#define BOTH_OCTAVE_BITS (BIT_OCTAVE_DOWN | BIT_OCTAVE_UP)

static uint8_t previous_status;

void transport_init(void)
{
	previous_status = 0;
}

static void send_sustain(uint8_t on)
{
	uint8_t event[4];
	event[0] = 0x0B; /* Cable 0, CIN: Control Change */
	event[1] = (uint8_t)(0xB0 | program_channel());
	event[2] = 64; /* standard MIDI sustain pedal CC */
	event[3] = on ? 127 : 0;
	midi_ring_push(event, 4);
}

void transport_process(uint8_t status_byte)
{
	uint8_t changed = status_byte ^ previous_status;
	previous_status = status_byte;

	if (changed & BIT_SUSTAIN) {
		send_sustain((status_byte & BIT_SUSTAIN) != 0);
	}

	if (changed & BIT_TAP) {
		if (status_byte & BIT_TAP) {
			arp_tap();
		}
	}

	if (changed & BOTH_OCTAVE_BITS) {
		if ((status_byte & BOTH_OCTAVE_BITS) == BOTH_OCTAVE_BITS) {
			program_set_octave(4); /* confirmed factory default */
		} else if (status_byte & BIT_OCTAVE_UP) {
			uint8_t o = program_octave();
			if (o < 8) {
				program_set_octave((uint8_t)(o + 1));
			}
		} else if (status_byte & BIT_OCTAVE_DOWN) {
			uint8_t o = program_octave();
			if (o > 0) {
				program_set_octave((uint8_t)(o - 1));
			}
		}
	}
}
