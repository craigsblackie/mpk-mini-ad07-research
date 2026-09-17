/*
 * Arpeggiator.
 *
 * Reimplements the shape of the original firmware's FUN_08002588,
 * fully traced (FIRMWARE_ANALYSIS.md): step through the currently-held
 * notes each time the clock-division-scaled step interval elapses,
 * gated on the per-program arp-enabled flag (record+0x04) and rate-
 * scaled by the record's clock-division selector (record+0x06) and
 * tempo (record+0x0a/+0x0b).
 *
 * Direction mode (record+0x05, confirmed via FUN_08002588's 6-case
 * switch -- see program.h's ARP_MODE_* and its comment) and octave
 * range (record+0x0c, confirmed via that function's per-pass-
 * completion note offset) are now both implemented: Up, Down, Up-Down
 * and Down-Up (ping-pong), Random (same LCG constants the original
 * uses, `x = x*0x6255 + 0x3619`, for a bit of extra fidelity even
 * though this is a clean reimplementation, not a port), and Order
 * (medium confidence this is what case 5 means -- reimplemented
 * identically to Up here rather than guessed at further, since telling
 * it apart from Up would need tracking insertion order separately from
 * pitch order for uncertain benefit). Octave range repeats the full
 * pass 0-3 additional times, each pass transposed up (or, in Down
 * mode, down through descending passes) by 12 semitones, clamped to
 * the valid MIDI note range.
 *
 * Gate length and latch (hold notes after release) aren't decoded and
 * aren't implemented -- notes play for one step's worth of Note On/Off
 * around each step boundary, and releasing all held notes stops the
 * arp immediately.
 *
 * The confirmed clock-division tick table gives ticks-per-step
 * relative to the standard MIDI convention of 24 clock ticks per
 * quarter note, and tempo gives a real BPM -- combined with
 * systick.c's real millisecond timebase, step timing is genuinely
 * calibrated: step_ms = ticks_per_step * 2500 / bpm. At 120 BPM with
 * clock division 0 (24 ticks = one quarter note), that's exactly
 * 500ms/step, a standard "1/4 note" arpeggiator rate.
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
static int8_t direction; /* +1 or -1, used by the ping-pong modes */
static uint8_t octave_pass;
static uint8_t random_step_count;
static uint16_t rng_state;
static uint32_t next_step_at_ms;
static uint8_t last_sent_note;
static uint8_t last_sent_active;
static uint8_t running;

void arp_init(void)
{
	arp_enabled = 1;
	held_count = 0;
	step_index = 0;
	direction = 1;
	octave_pass = 0;
	random_step_count = 0;
	rng_state = 1;
	next_step_at_ms = 0;
	last_sent_active = 0;
	running = 0;
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

static void advance_octave_pass(void)
{
	uint8_t range = program_arp_range();
	octave_pass++;
	if (octave_pass > range) {
		octave_pass = 0;
	}
}

static uint16_t rng_next(void)
{
	rng_state = (uint16_t)(rng_state * 0x6255u + 0x3619u);
	return rng_state;
}

/* Advances step_index (and, on completing a full pass, octave_pass)
 * according to the program's arp mode. Called once per step, after
 * the previous step's note was already sent. */
static void advance_step(void)
{
	uint8_t mode = program_arp_mode();

	switch (mode) {
	case ARP_MODE_DOWN:
		if (step_index == 0) {
			step_index = (uint8_t)(held_count - 1);
			advance_octave_pass();
		} else {
			step_index--;
		}
		break;
	case ARP_MODE_UP_DOWN:
		if (held_count == 1) {
			advance_octave_pass();
			break;
		}
		if (direction > 0) {
			step_index++;
			if (step_index >= held_count - 1) {
				step_index = (uint8_t)(held_count - 1);
				direction = -1;
			}
		} else {
			step_index--;
			if (step_index == 0) {
				direction = 1;
				advance_octave_pass();
			}
		}
		break;
	case ARP_MODE_DOWN_UP:
		if (held_count == 1) {
			advance_octave_pass();
			break;
		}
		if (direction < 0) {
			step_index--;
			if (step_index == 0) {
				direction = 1;
			}
		} else {
			step_index++;
			if (step_index >= held_count - 1) {
				step_index = (uint8_t)(held_count - 1);
				direction = -1;
				advance_octave_pass();
			}
		}
		break;
	case ARP_MODE_RANDOM:
		step_index = (uint8_t)(rng_next() % held_count);
		random_step_count++;
		if (random_step_count >= held_count) {
			random_step_count = 0;
			advance_octave_pass();
		}
		break;
	case ARP_MODE_UP:
	case ARP_MODE_ORDER: /* see file header -- treated the same as Up */
	default:
		step_index++;
		if (step_index >= held_count) {
			step_index = 0;
			advance_octave_pass();
		}
		break;
	}
}

static uint8_t note_with_octave_pass(uint8_t base_note)
{
	int16_t note = (int16_t)base_note + (int16_t)octave_pass * 12;
	if (note > 127) {
		note = 127;
	}
	return (uint8_t)note;
}

void arp_process(void)
{
	if (!arp_enabled || !program_arp_enabled() || held_count == 0) {
		if (last_sent_active) {
			send_event(0, last_sent_note, 0);
			last_sent_active = 0;
		}
		running = 0;
		next_step_at_ms = systick_millis();
		return;
	}

	if (!running) {
		/* Starting a fresh run -- pick the mode-appropriate first step
		 * (Down and Down-Up start at the top, matching the original's
		 * confirmed init-on-start behavior; everything else starts at
		 * the bottom). */
		uint8_t mode = program_arp_mode();
		step_index = (mode == ARP_MODE_DOWN || mode == ARP_MODE_DOWN_UP) ? (uint8_t)(held_count - 1) : 0;
		direction = (mode == ARP_MODE_DOWN_UP) ? -1 : 1;
		octave_pass = 0;
		random_step_count = 0;
		running = 1;
		next_step_at_ms = systick_millis();
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

	uint8_t note = note_with_octave_pass(held_notes[step_index]);
	send_event(1, note, held_velocity[step_index]);
	last_sent_note = note;
	last_sent_active = 1;

	advance_step();
}
