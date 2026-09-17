/*
 * Pad velocity sensing -> MIDI Note On/Off, Control Change, or Program
 * Change, depending on the active pad output mode.
 *
 * Reimplements the confirmed shape of the original firmware's
 * FUN_08003ab8 (FIRMWARE_ANALYSIS.md's "Revised: pad velocity sensing"):
 * each pad's ADC channel is compared against two thresholds with
 * hysteresis (attack: value > 0x80, release: value < 0x41) rather than
 * a single trigger point -- standard practice for a piezo-based
 * velocity-sensitive pad, and matching the doc's "hit/release/decay
 * phases" description of the original's per-pad state machine. Once a
 * hit is detected, velocity is derived from the same reading via the
 * original's confirmed scaling formula: ((value - 0x80) * 0x7F) / 0x220,
 * clamped to 1-127.
 *
 * Note/PC/CC number, and which of the three modes is active, now come
 * from the decoded per-program record (program.c/program.h) instead of
 * a standalone placeholder table -- see FIRMWARE_ANALYSIS.md's "Major
 * new finding" section, which found this exact three-mode structure by
 * reading FUN_08003ab8 in full.
 *
 * NOT YET CONFIRMED (same placeholder-honesty policy as knobs.c/keys.c):
 *  - Exactly which of the 8 ADC pad channels (adc_raw[ADC_NUM_CHANNELS
 *    + N]) corresponds to which of the 8 physical pads. AKAI's own
 *    schematic confirms the *group* (pads are ADC8-15, not some other
 *    range -- see adc.c's header) but its trace routing between the
 *    8 individual pads and ADC8-15 isn't legible at this project's
 *    scan resolution; using numeric order (pad N = channel 8+N) as a
 *    reasonable placeholder.
 *  - The exact bit-width/scaling the original's thresholds and formula
 *    operate on (this reimplementation applies them directly to the raw
 *    12-bit adc_raw[] reading, as literally documented; the original
 *    may operate on a pre-scaled 8/10-bit derivative instead -- worth
 *    re-checking against real hardware captures).
 *  - What exactly happens on release in CC/PC mode. The original's
 *    Note-mode release (Note Off) is confirmed; for CC/PC this module
 *    approximates a reasonable behavior (CC value 0 on release, PC
 *    fires once on hit only) rather than the original's exact bytes,
 *    which weren't fully traced for those two branches.
 *  - Pad output mode is CONFIRMED not per-program stored data --
 *    a shared runtime variable, set by matrix column 8's buttons
 *    (buttons.c) -- see program.c's program_pad_mode() comment.
 */
#include "pads.h"
#include "adc.h"
#include "midi_ring.h"
#include "stuck_note.h"
#include "program.h"

#define ATTACK_THRESHOLD 0x80
#define RELEASE_THRESHOLD 0x41
#define VELOCITY_SCALE_BASE 0x80
#define VELOCITY_SCALE_DIV 0x220

static uint8_t pad_active[PADS_NUM];

void pads_init(void)
{
	for (int i = 0; i < PADS_NUM; i++) {
		pad_active[i] = 0;
	}
}

static void send_pad_event(uint8_t pad, uint8_t on, uint8_t velocity)
{
	uint8_t channel = program_channel();
	uint8_t mode = program_pad_mode();
	uint8_t event[4];

	switch (mode) {
	case PAD_MODE_CC: {
		uint8_t cc = program_pad_cc(pad);
		event[0] = 0x0B; /* Cable 0, CIN: Control Change */
		event[1] = (uint8_t)(0xB0 | channel);
		event[2] = cc;
		event[3] = on ? velocity : 0;
		midi_ring_push(event, 4);
		break;
	}
	case PAD_MODE_PC:
		if (!on) {
			return; /* fires once on hit, no release event */
		}
		event[0] = 0x0C; /* Cable 0, CIN: Program Change (2-byte message) */
		event[1] = (uint8_t)(0xC0 | channel);
		event[2] = program_pad_pc(pad);
		event[3] = 0;
		midi_ring_push(event, 4);
		break;
	case PAD_MODE_NOTE:
	default: {
		uint8_t note = program_pad_note(pad);
		event[0] = on ? 0x09 : 0x08; /* Cable 0, CIN: Note On / Note Off */
		event[1] = (uint8_t)((on ? 0x90 : 0x80) | channel);
		event[2] = note;
		event[3] = velocity;
		midi_ring_push(event, 4);

		if (on) {
			stuck_note_on(channel, note);
		} else {
			stuck_note_off(channel, note);
		}
		break;
	}
	}

	/* TODO: ESP32-C3 mirror hook, same as keys.c's send_note(). */
}

void pads_process(void)
{
	for (int i = 0; i < PADS_NUM; i++) {
		uint16_t value = adc_raw[ADC_NUM_CHANNELS + i];

		if (!pad_active[i]) {
			if (value > ATTACK_THRESHOLD) {
				int32_t scaled = ((int32_t)value - VELOCITY_SCALE_BASE) * 0x7F / VELOCITY_SCALE_DIV;
				if (scaled < 1) {
					scaled = 1;
				}
				if (scaled > 127) {
					scaled = 127;
				}
				pad_active[i] = 1;
				send_pad_event((uint8_t)i, 1, (uint8_t)scaled);
			}
		} else {
			if (value < RELEASE_THRESHOLD) {
				pad_active[i] = 0;
				send_pad_event((uint8_t)i, 0, 0);
			}
		}
	}
}
