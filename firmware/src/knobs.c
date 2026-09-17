/*
 * Knob -> MIDI Control Change.
 *
 * Reimplements the confirmed shape of the original firmware's knob
 * handler (FIRMWARE_ANALYSIS.md): 4x oversample the raw ADC readings,
 * scale to the 7-bit CC range, and send a CC message on meaningful
 * change (with a small deadband so ADC noise doesn't spam CC
 * messages). Simplified relative to the original's more elaborate
 * per-program CC-number/channel table and asymmetric
 * increase/decrease smoothing formula -- this is a clean, original
 * reimplementation of the same observed behavior, not a port of that
 * exact math.
 *
 * NOT YET CONFIRMED: the actual per-knob CC number assignments --
 * these come from the per-program 101-byte configuration record
 * (FIRMWARE_ANALYSIS.md's SysEx editor-protocol section), not yet
 * fully decoded. default_cc[] below is a placeholder using common CC
 * numbers, not the original's actual defaults.
 */
#include "knobs.h"
#include "adc.h"
#include "midi_ring.h"

#define OVERSAMPLE_SHIFT 2 /* average 4 samples (2^2) -- matches the
                            * original's confirmed 4x oversampling */
#define OVERSAMPLE_COUNT (1 << OVERSAMPLE_SHIFT)
#define CC_DEADBAND 1 /* minimum CC-value change before sending */
#define MIDI_CHANNEL 0

static const uint8_t default_cc[ADC_NUM_CHANNELS] = {
	70, 71, 72, 73, 74, 75, 76, 77, /* placeholder -- see file header */
};

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
	uint8_t event[4];
	event[0] = 0x0B; /* Cable 0, CIN: Control Change */
	event[1] = (uint8_t)(0xB0 | MIDI_CHANNEL);
	event[2] = default_cc[knob];
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
