/*
 * Key edge detection -> MIDI Note On/Off.
 *
 * Reimplements the confirmed behavior of the original firmware's
 * FUN_08004990 (FIRMWARE_ANALYSIS.md): diff matrix_state[] against the
 * previous scan, and for each bit that transitioned, emit a Note
 * On (bit now set) or Note Off (bit now clear) USB-MIDI event.
 *
 * NOT YET CONFIRMED: the exact mapping from (column, row-bit) to which
 * of the keyboard's 25 physical keys it is. The original firmware's
 * lookup table (flash 0x08006fcd) contains a clean sequential 0..24
 * byte sequence -- strongly suggesting a simple "key index" that gets
 * added to a base/octave note elsewhere -- but the precise indexing
 * formula from (column, bit-position) into that table wasn't fully
 * traced (FIRMWARE_ANALYSIS.md's FUN_08004990 notes). Rather than
 * guess and risk silently wrong note mappings, key_index_table[][]
 * below is left as "no key" placeholders. TODO: fill in from real
 * hardware testing (press each key, observe which (col,row) fires) or
 * further trace analysis, before this is useful on real hardware.
 */
#include "keys.h"
#include "matrix.h"
#include "midi_ring.h"
#include "buttons.h"
#include "stuck_note.h"

#define KEY_NONE 0xFF
#define MIDI_CHANNEL 0 /* channel 1 */

uint8_t keys_base_note = 36; /* C2 -- a reasonable default starting point
                               * for a 25-key controller; not confirmed
                               * against the original's actual default. */

/* [column 0..6][row bit 0..7] -> key index 0..24, or KEY_NONE.
 * TODO: unverified placeholder, see file header. */
static const uint8_t key_index_table[7][8] = {
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
	{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
};

static uint8_t previous_state[MATRIX_COLS];

void keys_init(void)
{
	for (int i = 0; i < MATRIX_COLS; i++) {
		previous_state[i] = 0;
	}
}

static void send_note(uint8_t key_index, uint8_t on, uint8_t velocity)
{
	if (key_index == KEY_NONE) {
		return;
	}
	int16_t note = (int16_t)keys_base_note + key_index + (int16_t)buttons_octave_offset * 12;
	if (note < 0) {
		note = 0;
	}
	if (note > 127) {
		note = 127;
	}
	uint8_t event[4];
	event[0] = on ? 0x09 : 0x08; /* Cable 0, CIN: Note On / Note Off */
	event[1] = (uint8_t)((on ? 0x90 : 0x80) | MIDI_CHANNEL);
	event[2] = (uint8_t)note;
	event[3] = velocity;
	midi_ring_push(event, 4);

	if (on) {
		stuck_note_on(MIDI_CHANNEL, (uint8_t)note);
	} else {
		stuck_note_off(MIDI_CHANNEL, (uint8_t)note);
	}

	/* TODO: this is the intended mirror point for the ESP32-C3 BLE
	 * MIDI project -- e.g.:
	 *   usart1_send(&event[1], 3);
	 * once USART1 output is implemented. Hooking here (rather than
	 * inside midi_ring_push itself) keeps the ring buffer generic
	 * and puts the mirror specifically at "a new key event was just
	 * decided", matching where FUN_08006d54 sits in the original's
	 * call graph relative to its callers. */
}

void keys_process(void)
{
	/* Columns 0-6 carry the 7x8 key matrix per FIRMWARE_ANALYSIS.md;
	 * columns 7-8 are special-cased in the original for a smaller
	 * number of inputs (transport/other buttons, not full 8-row
	 * columns) -- not yet reimplemented here (TODO). */
	for (int col = 0; col < 7; col++) {
		uint8_t now = matrix_state[col];
		uint8_t prev = previous_state[col];
		uint8_t changed = now ^ prev;

		if (changed == 0) {
			continue;
		}

		for (int bit = 0; bit < 8; bit++) {
			uint8_t mask = (uint8_t)(1u << bit);
			if ((changed & mask) == 0) {
				continue;
			}
			uint8_t key_index = key_index_table[col][bit];
			uint8_t pressed = (now & mask) != 0;
			send_note(key_index, pressed, KEYS_DEFAULT_VELOCITY);
		}

		previous_state[col] = now;
	}
}
