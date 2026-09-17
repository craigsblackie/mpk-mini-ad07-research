/*
 * Key edge detection -> velocity-sensed MIDI Note On/Off.
 *
 * Reimplements the confirmed behavior of the original firmware's
 * FUN_08004990, fully traced (FIRMWARE_ANALYSIS.md's "Resolved: the
 * key-index lookup table" and "Follow-up: the dual-switch mechanism in
 * full" sections):
 *
 * - **Key-index mapping**: `table_index = column*4 + (bit >> 1)`,
 *   indexing a 28-byte table at flash 0x08006fcd (`00 01 02 ... 18 FF
 *   FF FF`) -- confirmed against the raw binary, not a guess.
 * - **Dual-switch velocity sensing**: row-bits 2N and 2N+1 within a
 *   column share one key index (hence `bit >> 1` above). Each key has
 *   a 3-state machine (idle/armed/fired). The odd-numbered bit arms
 *   the key and records a tick when it reaches its "released" value
 *   while idle, and sends Note Off (resetting to idle) when it reaches
 *   its "pressed" value while fired. The even-numbered bit, when it
 *   reaches its "released" value while armed, computes a tick delta
 *   since arming, fires Note On with velocity = 127 - clamp(delta, 0,
 *   126) (a plain linear inversion, not a curve -- read directly off
 *   a 127-byte table at flash 0x08006f4e), and marks the key fired.
 *   This is reproduced literally from the decompiled logic, including
 *   the (initially counterintuitive) polarity of testing each bit's
 *   *"released"* value to arm/fire rather than "pressed" -- see the
 *   FIRMWARE_ANALYSIS.md section above for why this project isn't
 *   fully certain of the physical reason (possibly reversed wiring
 *   convention on these specific velocity-sense contacts vs. the main
 *   matrix), but is confident in the logic as literally read.
 *
 * NOT CALIBRATED TO REAL TIME: the original's own reference for this
 * timing was traced to a plain wraparound counter, incremented once
 * per call to the function that contains it from elsewhere in the
 * main loop -- NOT the SysTick hardware timer (SysTick is configured
 * in the original but its interrupt handler is a no-op stub, `bx lr`,
 * confirmed by reading it directly). So this reimplementation uses an
 * equivalent free-running counter incremented once per keys_process()
 * call (i.e. once per main-loop iteration) rather than systick.c's
 * real millisecond timebase -- matching the original's actual
 * category of timing source. The absolute "feel" (how fast a press
 * needs to be for max velocity) will differ from the original if this
 * firmware's main loop runs at a different rate, but the *relative*
 * behavior (faster double-contact closure = higher velocity) is real.
 */
#include "keys.h"
#include "matrix.h"
#include "midi_ring.h"
#include "buttons.h"
#include "stuck_note.h"

#define KEY_NONE 0xFF
#define KEY_COUNT 25
#define MIDI_CHANNEL 0 /* channel 1 */

#define KEY_IDLE 0
#define KEY_ARMED 1
#define KEY_FIRED 2

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
static uint8_t key_state[KEY_COUNT];
static uint16_t key_arm_tick[KEY_COUNT];
static uint16_t scan_tick;

void keys_init(void)
{
	for (int i = 0; i < MATRIX_COLS; i++) {
		previous_state[i] = 0;
	}
	for (int i = 0; i < KEY_COUNT; i++) {
		key_state[i] = KEY_IDLE;
		key_arm_tick[i] = 0;
	}
	scan_tick = 0;
}

static uint8_t note_for_key(uint8_t key_index)
{
	int16_t note = (int16_t)keys_base_note + key_index + (int16_t)buttons_octave_offset * 12;
	if (note < 0) {
		note = 0;
	}
	if (note > 127) {
		note = 127;
	}
	return (uint8_t)note;
}

static void send_note(uint8_t key_index, uint8_t on, uint8_t velocity)
{
	uint8_t note = note_for_key(key_index);
	uint8_t event[4];
	event[0] = on ? 0x09 : 0x08; /* Cable 0, CIN: Note On / Note Off */
	event[1] = (uint8_t)((on ? 0x90 : 0x80) | MIDI_CHANNEL);
	event[2] = note;
	event[3] = velocity;
	midi_ring_push(event, 4);

	if (on) {
		stuck_note_on(MIDI_CHANNEL, note);
	} else {
		stuck_note_off(MIDI_CHANNEL, note);
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

static void handle_bit(uint8_t key_index, int bit, uint8_t released)
{
	if (key_index == KEY_NONE) {
		return;
	}

	if (bit & 1) {
		/* Odd sub-switch: arm on reaching "released", Note Off on
		 * reaching "pressed" while fired. */
		if (released) {
			if (key_state[key_index] == KEY_IDLE) {
				key_state[key_index] = KEY_ARMED;
				key_arm_tick[key_index] = scan_tick;
			}
		} else {
			if (key_state[key_index] == KEY_FIRED) {
				send_note(key_index, 0, 0);
			}
			key_state[key_index] = KEY_IDLE;
		}
	} else {
		/* Even sub-switch: fire Note On on reaching "released" while
		 * armed, velocity from the tick delta since arming. */
		if (released && key_state[key_index] == KEY_ARMED) {
			uint16_t delta = (uint16_t)(scan_tick - key_arm_tick[key_index]);
			if (delta > 126) {
				delta = 126;
			}
			uint8_t velocity = (uint8_t)(127 - delta);
			key_state[key_index] = KEY_FIRED;
			send_note(key_index, 1, velocity);
		}
	}
}

void keys_process(void)
{
	scan_tick++;

	/* Columns 0-6 carry the 7x8 key matrix per FIRMWARE_ANALYSIS.md;
	 * columns 7-8 are special-cased in the original for a smaller
	 * number of inputs (transport/other buttons -- see buttons.c). */
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
			uint8_t released = (now & mask) == 0;
			handle_bit(key_index, bit, released);
		}

		previous_state[col] = now;
	}
}
