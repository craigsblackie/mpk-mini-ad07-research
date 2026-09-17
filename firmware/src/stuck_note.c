/*
 * Stuck-note safety net.
 *
 * Reimplements the likely purpose of the original firmware's
 * FUN_08005188 (FIRMWARE_ANALYSIS.md, medium confidence): iterate a
 * fixed set of active-note slots and force-send a Note Off for any
 * that have outstayed a timeout, guarding against a note staying on
 * forever if its matching Note Off event is ever lost (a key release
 * missed on a debounce edge case, or a MIDI event dropped because
 * midi_ring_push() found the ring buffer full).
 *
 * NOT YET CONFIRMED: the original's exact timeout duration/timebase --
 * it's driven by whatever periodic tick calls FUN_08005188 from the
 * main loop, not yet identified (see FIRMWARE_ANALYSIS.md's main loop
 * trace). STUCK_NOTE_TIMEOUT_TICKS below counts main-loop iterations
 * as a placeholder time base, not a calibrated real-world duration.
 * The original's 8-slot count IS confirmed (the doc traced exactly 8
 * fixed slots), reused here as-is.
 */
#include "stuck_note.h"
#include "midi_ring.h"

#define STUCK_NOTE_TIMEOUT_TICKS 100000 /* placeholder -- see file header */

typedef struct {
	uint8_t in_use;
	uint8_t channel;
	uint8_t note;
	uint32_t age;
} slot_t;

static slot_t slots[STUCK_NOTE_SLOTS];

void stuck_note_init(void)
{
	for (int i = 0; i < STUCK_NOTE_SLOTS; i++) {
		slots[i].in_use = 0;
	}
}

void stuck_note_on(uint8_t channel, uint8_t note)
{
	for (int i = 0; i < STUCK_NOTE_SLOTS; i++) {
		if (!slots[i].in_use) {
			slots[i].in_use = 1;
			slots[i].channel = channel;
			slots[i].note = note;
			slots[i].age = 0;
			return;
		}
	}
	/* All 8 slots full -- matches the original's fixed slot count;
	 * a note-on beyond that simply isn't tracked for timeout. */
}

void stuck_note_off(uint8_t channel, uint8_t note)
{
	for (int i = 0; i < STUCK_NOTE_SLOTS; i++) {
		if (slots[i].in_use && slots[i].channel == channel && slots[i].note == note) {
			slots[i].in_use = 0;
			return;
		}
	}
}

void stuck_note_process(void)
{
	for (int i = 0; i < STUCK_NOTE_SLOTS; i++) {
		if (!slots[i].in_use) {
			continue;
		}
		slots[i].age++;
		if (slots[i].age >= STUCK_NOTE_TIMEOUT_TICKS) {
			uint8_t event[4];
			event[0] = 0x08; /* Cable 0, CIN: Note Off */
			event[1] = (uint8_t)(0x80 | slots[i].channel);
			event[2] = slots[i].note;
			event[3] = 0;
			midi_ring_push(event, 4);
			slots[i].in_use = 0;
		}
	}
}
