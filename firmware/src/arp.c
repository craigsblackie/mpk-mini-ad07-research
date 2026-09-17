/*
 * Arpeggiator (skeleton).
 *
 * Reimplements the likely shape of the original firmware's
 * FUN_08002588 (FIRMWARE_ANALYSIS.md, medium confidence): step through
 * a set of currently-held notes, advancing one step each time a
 * threshold is reached against a stored step counter, gated on a
 * per-program record field (record+7) this project hasn't decoded yet
 * (see the SysEx editor-protocol section).
 *
 * This is a genuinely partial reimplementation, more so than this
 * firmware's other modules -- the real per-program parameters
 * (tempo/clock division, range in octaves, direction mode: up/down/
 * up-down/random, gate length, latch on/off) all live in that
 * undecoded record. What's implemented here is a fixed-tempo,
 * ascending-only ("up") arpeggiator over whatever notes are currently
 * held, as a functional placeholder -- NOT a reimplementation of the
 * original's exact parameters.
 *
 * NOT WIRED UP YET: arp_note_on()/arp_note_off() aren't called from
 * anywhere -- keys.c and pads.c still send Note On/Off straight to
 * the MIDI ring buffer. Routing key/pad presses through here instead
 * (gated on arp_enabled, which defaults to 0/off) is a follow-up, once
 * there's a real trigger condition to gate it on. Until then,
 * arp_process() runs every main-loop iteration as a harmless no-op.
 */
#include "arp.h"
#include "midi_ring.h"

#define STEP_INTERVAL_TICKS 30000 /* placeholder tempo -- see file header */
#define MIDI_CHANNEL 0

uint8_t arp_enabled;

static uint8_t held_notes[ARP_MAX_NOTES];
static uint8_t held_velocity[ARP_MAX_NOTES];
static uint8_t held_count;
static uint8_t step_index;
static uint32_t tick;
static uint8_t last_sent_note;
static uint8_t last_sent_active;

void arp_init(void)
{
	arp_enabled = 0;
	held_count = 0;
	step_index = 0;
	tick = 0;
	last_sent_active = 0;
}

void arp_note_on(uint8_t note, uint8_t velocity)
{
	if (held_count >= ARP_MAX_NOTES) {
		return;
	}
	/* Insertion sort into ascending pitch order -- keeps "up" mode
	 * simple (just walk the array in order). */
	int i = held_count;
	while (i > 0 && held_notes[i - 1] > note) {
		held_notes[i] = held_notes[i - 1];
		held_velocity[i] = held_velocity[i - 1];
		i--;
	}
	held_notes[i] = note;
	held_velocity[i] = velocity;
	held_count++;
}

void arp_note_off(uint8_t note)
{
	for (int i = 0; i < held_count; i++) {
		if (held_notes[i] == note) {
			for (int j = i; j < held_count - 1; j++) {
				held_notes[j] = held_notes[j + 1];
				held_velocity[j] = held_velocity[j + 1];
			}
			held_count--;
			if (step_index > (uint8_t)i && step_index > 0) {
				step_index--;
			}
			return;
		}
	}
}

static void send_event(uint8_t on, uint8_t note, uint8_t velocity)
{
	uint8_t event[4];
	event[0] = on ? 0x09 : 0x08;
	event[1] = (uint8_t)((on ? 0x90 : 0x80) | MIDI_CHANNEL);
	event[2] = note;
	event[3] = velocity;
	midi_ring_push(event, 4);
}

void arp_process(void)
{
	if (!arp_enabled || held_count == 0) {
		if (last_sent_active) {
			send_event(0, last_sent_note, 0);
			last_sent_active = 0;
		}
		tick = 0;
		step_index = 0;
		return;
	}

	tick++;
	if (tick < STEP_INTERVAL_TICKS) {
		return;
	}
	tick = 0;

	if (last_sent_active) {
		send_event(0, last_sent_note, 0);
	}

	if (step_index >= held_count) {
		step_index = 0;
	}
	send_event(1, held_notes[step_index], held_velocity[step_index]);
	last_sent_note = held_notes[step_index];
	last_sent_active = 1;
	step_index++;
}
