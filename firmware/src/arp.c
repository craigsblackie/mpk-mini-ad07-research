/*
 * Arpeggiator.
 *
 * Reimplements the likely shape of the original firmware's
 * FUN_08002588 (FIRMWARE_ANALYSIS.md, medium confidence): step through
 * a set of currently-held notes, advancing one step each time a
 * threshold is reached against a stored step counter, gated on the
 * per-program arp-enabled flag (record+0x04, now decoded -- see
 * program.c) and rate-scaled by the record's clock-division selector
 * (record+0x06) and tempo (record+0x0a/+0x0b), both also now decoded.
 *
 * Still a genuinely partial reimplementation: the range (in octaves),
 * direction mode (up/down/up-down/random), gate length, and latch
 * on/off parameters aren't decoded, so this stays a fixed, ascending-
 * only ("up") arpeggiator over whatever notes are currently held. What
 * changed from the original skeleton: the enable flag and step rate
 * are now driven by real per-program data instead of a hardcoded
 * constant and an always-off default.
 *
 * The confirmed clock-division tick table (FIRMWARE_ANALYSIS.md) gives
 * ticks-per-step relative to the standard MIDI convention of 24 clock
 * ticks per quarter note, and tempo gives a real BPM -- combined with
 * systick.c's now-real millisecond timebase, step timing is genuinely
 * calibrated: step_ms = ticks_per_step * 2500 / bpm (2500 = 60000ms
 * per minute / 24 ticks per quarter note). At 120 BPM with division 0
 * (24 ticks = one quarter note), that's exactly 500ms/step, matching a
 * standard "1/4 note" arpeggiator rate.
 *
 * NOT WIRED UP YET: arp_note_on()/arp_note_off() aren't called from
 * anywhere -- keys.c and pads.c still send Note On/Off straight to
 * the MIDI ring buffer. Routing key/pad presses through here instead
 * when the program's arp flag is set is a follow-up.
 */
#include "arp.h"
#include "midi_ring.h"
#include "program.h"
#include "systick.h"

/* record+0x06 (0-7) -> ticks-per-step, read directly from the
 * original's confirmed switch table (FIRMWARE_ANALYSIS.md). */
static const uint8_t clock_div_ticks[8] = {24, 16, 12, 8, 6, 4, 3, 2};

uint8_t arp_enabled = 1; /* manual override, ANDed with the program's
                          * own arp-enabled flag below -- defaults on
                          * now that a real per-program source exists. */

static uint8_t held_notes[ARP_MAX_NOTES];
static uint8_t held_velocity[ARP_MAX_NOTES];
static uint8_t held_count;
static uint8_t step_index;
static uint32_t next_step_at_ms;
static uint8_t last_sent_note;
static uint8_t last_sent_active;

void arp_init(void)
{
	arp_enabled = 1;
	held_count = 0;
	step_index = 0;
	next_step_at_ms = 0;
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
	event[1] = (uint8_t)((on ? 0x90 : 0x80) | program_channel());
	event[2] = note;
	event[3] = velocity;
	midi_ring_push(event, 4);
}

static uint32_t step_interval_ms(void)
{
	uint8_t ticks_per_step = clock_div_ticks[program_arp_clock_div()];
	uint16_t bpm = program_tempo_bpm();
	return ((uint32_t)ticks_per_step * 2500u) / bpm;
}

void arp_process(void)
{
	if (!arp_enabled || !program_arp_enabled() || held_count == 0) {
		if (last_sent_active) {
			send_event(0, last_sent_note, 0);
			last_sent_active = 0;
		}
		step_index = 0;
		next_step_at_ms = systick_millis();
		return;
	}

	uint32_t now = systick_millis();
	if ((int32_t)(now - next_step_at_ms) < 0) {
		return;
	}
	next_step_at_ms = now + step_interval_ms();

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
