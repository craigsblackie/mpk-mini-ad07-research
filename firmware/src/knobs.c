/*
 * Knob -> MIDI Control Change.
 *
 * Reimplements the confirmed shape of the original firmware's knob
 * handler (FIRMWARE_ANALYSIS.md): 4x oversample the raw ADC readings,
 * scale to the 7-bit CC range, and send a CC message on meaningful
 * change (with a small deadband so ADC noise doesn't spam CC
 * messages). Simplified relative to the original's more elaborate
 * asymmetric increase/decrease smoothing formula -- this is a clean,
 * original reimplementation of the same observed behavior, not a port
 * of that exact math.
 *
 * The per-knob CC number and MIDI channel now come from the decoded
 * per-program record (program.c/program.h, record+0x4d+knob*3 and
 * record+0x00 respectively -- see FIRMWARE_ANALYSIS.md's "Major new
 * finding" section) instead of a standalone placeholder table. The
 * record's own default values are still placeholders (see program.c's
 * header comment) -- what's no longer a placeholder is *where* this
 * module gets the numbers from, which now matches the original's
 * architecture.
 */
#include "knobs.h"
#include "adc.h"
#include "midi_ring.h"
#include "program.h"

#define OVERSAMPLE_SHIFT 2 /* average 4 samples (2^2) -- matches the
                            * original's confirmed 4x oversampling */
#define OVERSAMPLE_COUNT (1 << OVERSAMPLE_SHIFT)
#define CC_DEADBAND 1 /* minimum CC-value change before sending */

static uint32_t accum[ADC_NUM_CHANNELS];
static uint8_t sample_count;
static uint8_t last_cc_value[ADC_NUM_CHANNELS];

void knobs_init(void)
{
	for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
		accum[i] = 0;
		last_cc_value[i] = 0xFF; /* force a send on first real reading */
	}
	sample_count = 0;
}

static void send_cc(uint8_t knob, uint8_t value)
{
	uint8_t cc_number = program_knob_cc(knob);
	if (cc_number == 0) {
		return; /* 0 = knob unassigned, per the original's confirmed
		         * "gate byte doubles as CC number" behavior. */
	}

	uint8_t event[4];
	event[0] = 0x0B; /* Cable 0, CIN: Control Change */
	event[1] = (uint8_t)(0xB0 | program_channel());
	event[2] = cc_number;
	event[3] = value;
	midi_ring_push(event, 4);

	/* TODO: ESP32-C3 mirror hook, same as keys.c's send_note(). */
}

void knobs_process(void)
{
	for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
		accum[i] += adc_raw[i]; /* 12-bit values; DMA keeps these fresh */
	}
	sample_count++;

	if (sample_count < OVERSAMPLE_COUNT) {
		return;
	}
	sample_count = 0;

	for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
		uint16_t averaged = (uint16_t)(accum[i] >> OVERSAMPLE_SHIFT); /* back to 12-bit range */
		accum[i] = 0;

		uint8_t cc_value = (uint8_t)(averaged >> 5); /* 12-bit -> 7-bit: 4095>>5 = 127 exactly */

		int16_t diff = (int16_t)cc_value - (int16_t)last_cc_value[i];
		if (diff < 0) {
			diff = -diff;
		}
		if (last_cc_value[i] == 0xFF || diff >= CC_DEADBAND) {
			last_cc_value[i] = cc_value;
			send_cc((uint8_t)i, cc_value);
		}
	}
}
