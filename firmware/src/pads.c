/*
 * Pad velocity sensing -> MIDI Note On/Off.
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
 * NOT YET CONFIRMED (same placeholder-honesty policy as knobs.c/keys.c):
 *  - Which of the 8 ADC pad channels (adc_raw[ADC_NUM_CHANNELS + N])
 *    corresponds to which of the 8 physical pads -- using channel order
 *    0-7 = pad 0-7 as a placeholder.
 *  - The exact bit-width/scaling the original's thresholds and formula
 *    operate on (this reimplementation applies them directly to the raw
 *    12-bit adc_raw[] reading, as literally documented; the original
 *    may operate on a pre-scaled 8/10-bit derivative instead -- worth
 *    re-checking against real hardware captures).
 *  - Pad note-number assignments, which (like knob CC numbers) live in
 *    the per-program SysEx record, not yet decoded -- pad_base_note[]
 *    below uses a standard GM-drum-style base (C1=36) as a reasonable
 *    placeholder, not the original's actual defaults.
 *  - The record layout section of FIRMWARE_ANALYSIS.md ("Major new
 *    finding: 101-byte per-program record layout") found that each
 *    pad's config sub-record (record+0x0d + pad*8) can apparently
 *    select between sending a Note, a Program Change, or a Control
 *    Change per pad hit, via a shared mode byte -- this module only
 *    implements the Note case, matching what was already built here.
 *    Not revisited yet, since the mode-byte's own location isn't
 *    confirmed.
 */
#include "pads.h"
#include "adc.h"
#include "midi_ring.h"
#include "stuck_note.h"

#define ATTACK_THRESHOLD 0x80
#define RELEASE_THRESHOLD 0x41
#define VELOCITY_SCALE_BASE 0x80
#define VELOCITY_SCALE_DIV 0x220
#define MIDI_CHANNEL 0

static const uint8_t pad_base_note[PADS_NUM] = {
	36, 37, 38, 39, 40, 41, 42, 43, /* placeholder -- see file header */
};

static uint8_t pad_active[PADS_NUM];

void pads_init(void)
{
	for (int i = 0; i < PADS_NUM; i++) {
		pad_active[i] = 0;
	}
}

static void send_pad_note(uint8_t pad, uint8_t on, uint8_t velocity)
{
	uint8_t event[4];
	event[0] = on ? 0x09 : 0x08; /* Cable 0, CIN: Note On / Note Off */
	event[1] = (uint8_t)((on ? 0x90 : 0x80) | MIDI_CHANNEL);
	event[2] = pad_base_note[pad];
	event[3] = velocity;
	midi_ring_push(event, 4);

	if (on) {
		stuck_note_on(MIDI_CHANNEL, pad_base_note[pad]);
	} else {
		stuck_note_off(MIDI_CHANNEL, pad_base_note[pad]);
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
				send_pad_note((uint8_t)i, 1, (uint8_t)scaled);
			}
		} else {
			if (value < RELEASE_THRESHOLD) {
				pad_active[i] = 0;
				send_pad_note((uint8_t)i, 0, 0);
			}
		}
	}
}
