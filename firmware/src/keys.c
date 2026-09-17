/*
 * Key edge detection -> MIDI Note On/Off.
 *
 * Reimplements the confirmed behavior of the original firmware's
 * FUN_08004990 (FIRMWARE_ANALYSIS.md): diff matrix_state[] against the
 * previous scan, and for each bit that transitioned, emit a Note
 * On (bit now set) or Note Off (bit now clear) USB-MIDI event.
 *
 * CONFIRMED (previously a placeholder): the (column, row-bit) -> key
 * index mapping. Disassembling FUN_08004990 directly against the
 * verified firmware binary (arm-none-eabi-objdump, since finishing
 * this trace needed instruction-level detail the decompiler's
 * pseudocode had flattened away) found the real indexing formula --
 * `table_index = column*4 + (bit >> 1)` -- and the table it indexes
 * into, at flash 0x08006fcd: 28 bytes, `00 01 02 ... 18 FF FF FF`
 * (0x18 = 24). key_index_table[][] below is exactly that formula and
 * that data, not a guess.
 *
 * NOT reimplemented here: the original pairs row-bits 2N and 2N+1 into
 * the SAME key index (hence `bit >> 1` above) and uses a small state
 * machine (armed/fired per key) plus a timestamp delta between the two
 * bits' transitions to derive velocity -- strongly suggestive of a
 * genuine dual-switch, velocity-sensing keybed, not just a single
 * on/off contact per key. This module still treats each bit
 * independently, so a physical keypress that closes both switches of
 * its pair will currently emit two Note On (and later two Note Off)
 * events a scan or two apart instead of one velocity-sensed event.
 * Functionally harmless for basic testing (both events target the same
 * note), but worth fixing once there's a tick/timestamp source to
 * derive real velocity from, and to suppress the duplicate.
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
 * CONFIRMED against the original's own lookup table -- see file header. */
static const uint8_t key_index_table[7][8] = {
	{0, 0, 1, 1, 2, 2, 3, 3},
	{4, 4, 5, 5, 6, 6, 7, 7},
	{8, 8, 9, 9, 10, 10, 11, 11},
	{12, 12, 13, 13, 14, 14, 15, 15},
	{16, 16, 17, 17, 18, 18, 19, 19},
	{20, 20, 21, 21, 22, 22, 23, 23},
	{24, 24, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE},
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
