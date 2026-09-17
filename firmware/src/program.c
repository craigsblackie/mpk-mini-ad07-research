/*
 * Per-program configuration record storage.
 *
 * Field offsets below are reproduced directly from FIRMWARE_ANALYSIS.md's
 * "Major new finding: 101-byte per-program record layout" section --
 * resolved by reading FUN_08005ac8 (the original's own range-validation
 * function, whose clamp/default pairs double as a field map) and cross-
 * confirming against the two functions that actually consume each range
 * (FUN_0800478c for knobs, FUN_08003ab8 for pads). Not guessed.
 *
 * What's NOT confirmed: the exact default values AKAI's firmware ships
 * a factory-reset program with. default_program below picks reasonable
 * values within each field's confirmed valid range (same
 * placeholder-honesty policy as the rest of this firmware) -- channel 0,
 * arp off, a mid-range clock division, 120 BPM, knob CCs 70-77, pad
 * notes 36-43 (matching this project's earlier standalone placeholders
 * in knobs.c/pads.c, now sourced from one place instead of two).
 */
#include "program.h"

#define OFF_CHANNEL 0x00
#define OFF_ARP_ENABLED 0x04
#define OFF_ARP_CLOCK_DIV 0x06
#define OFF_TEMPO_LOW 0x0a
#define OFF_TEMPO_HIGH 0x0b
#define OFF_PAD_BASE 0x0d
#define PAD_STRIDE 8
#define OFF_PAD_NOTE 0x0
#define OFF_PAD_PC 0x2
#define OFF_PAD_CC 0x4
#define OFF_KNOB_BASE 0x4d
#define KNOB_STRIDE 3
#define OFF_KNOB_CC 0x0

program_record_t programs[PROGRAM_COUNT];
uint8_t current_program;

static void init_one(program_record_t *p, uint8_t base_cc, uint8_t base_note)
{
	for (int i = 0; i < PROGRAM_RECORD_SIZE; i++) {
		p->raw[i] = 0;
	}

	p->raw[OFF_CHANNEL] = 0;
	p->raw[OFF_ARP_ENABLED] = 0;
	p->raw[OFF_ARP_CLOCK_DIV] = 2; /* one of the confirmed 0-7 divisions */
	/* Tempo = byte[0xb] + byte[0xa]*0x80 = 120 -> byte[0xa]=0, byte[0xb]=120 */
	p->raw[OFF_TEMPO_LOW] = 0;
	p->raw[OFF_TEMPO_HIGH] = 120;

	for (int k = 0; k < 8; k++) {
		p->raw[OFF_KNOB_BASE + k * KNOB_STRIDE + OFF_KNOB_CC] = (uint8_t)(base_cc + k);
	}
	for (int pad = 0; pad < 8; pad++) {
		int base = OFF_PAD_BASE + pad * PAD_STRIDE;
		p->raw[base + OFF_PAD_NOTE] = (uint8_t)(base_note + pad);
		p->raw[base + OFF_PAD_PC] = (uint8_t)pad;
		p->raw[base + OFF_PAD_CC] = (uint8_t)(base_cc + pad); /* placeholder */
	}
	p->pad_mode = PAD_MODE_NOTE;
}

void program_init(void)
{
	current_program = 0;
	for (int i = 0; i < PROGRAM_COUNT; i++) {
		init_one(&programs[i], 70, 36);
	}
}

uint8_t program_channel(void)
{
	uint8_t ch = programs[current_program].raw[OFF_CHANNEL];
	return (uint8_t)(ch & 0x0F);
}

uint8_t program_arp_enabled(void)
{
	return programs[current_program].raw[OFF_ARP_ENABLED] != 0;
}

uint8_t program_arp_clock_div(void)
{
	uint8_t d = programs[current_program].raw[OFF_ARP_CLOCK_DIV];
	return (uint8_t)(d & 0x07);
}

uint16_t program_tempo_bpm(void)
{
	uint16_t bpm = (uint16_t)programs[current_program].raw[OFF_TEMPO_LOW] * 0x80 +
	               programs[current_program].raw[OFF_TEMPO_HIGH];
	if (bpm < 30) {
		bpm = 30;
	}
	if (bpm > 240) {
		bpm = 240;
	}
	return bpm;
}

uint8_t program_knob_cc(uint8_t knob)
{
	return programs[current_program].raw[OFF_KNOB_BASE + knob * KNOB_STRIDE + OFF_KNOB_CC];
}

uint8_t program_pad_note(uint8_t pad)
{
	return programs[current_program].raw[OFF_PAD_BASE + pad * PAD_STRIDE + OFF_PAD_NOTE];
}

uint8_t program_pad_pc(uint8_t pad)
{
	return programs[current_program].raw[OFF_PAD_BASE + pad * PAD_STRIDE + OFF_PAD_PC];
}

uint8_t program_pad_cc(uint8_t pad)
{
	return programs[current_program].raw[OFF_PAD_BASE + pad * PAD_STRIDE + OFF_PAD_CC];
}

uint8_t program_pad_mode(void)
{
	return programs[current_program].pad_mode;
}
