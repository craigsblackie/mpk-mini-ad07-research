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
 * The header (record+0x00..0x0c) and each pad sub-record's first byte
 * (record+0x0d+pad*8) now use the original's own confirmed factory-
 * default values, not guesses -- read directly out of the SysEx 'j'
 * (0x7F sub-case) bootstrap/factory-reset handler in FUN_08002eac,
 * which writes literal byte constants into a fresh record (see
 * FIRMWARE_ANALYSIS.md's SysEx section for the negative-offset-to-
 * record-offset derivation). What's NOT confirmed: the rest of each
 * pad sub-record, and the entire knob CC region (record+0x4d..0x64) --
 * the 'j' handler doesn't touch either, so this project has no
 * evidence for their true factory defaults. Those still use reasonable
 * placeholder values (knob CCs 70-77, pad PC# = pad index).
 */
#include "program.h"

#define OFF_CHANNEL 0x00
#define OFF_OCTAVE 0x02 /* confirmed -- keyboard octave, see program.h */
#define OFF_FINE_TRANSPOSE 0x03 /* confirmed -- keyboard fine transpose, see program.h */
#define OFF_ARP_ENABLED 0x04
#define OFF_ARP_MODE 0x05 /* confirmed via FUN_08002588's switch -- see program.h */
#define OFF_ARP_CLOCK_DIV 0x06
#define OFF_UNKNOWN_9 0x09 /* confirmed factory default 3; meaning unidentified */
#define OFF_TEMPO_LOW 0x0a
#define OFF_TEMPO_HIGH 0x0b
#define OFF_ARP_RANGE 0x0c /* confirmed via FUN_08002588 -- see program.h */
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

static void init_one(program_record_t *p, uint8_t base_cc)
{
	for (int i = 0; i < PROGRAM_RECORD_SIZE; i++) {
		p->raw[i] = 0;
	}

	p->raw[OFF_CHANNEL] = 0;
	p->raw[OFF_OCTAVE] = 4; /* confirmed factory default -- middle C at key_index 0 */
	p->raw[OFF_FINE_TRANSPOSE] = 12; /* confirmed factory default */
	p->raw[OFF_ARP_ENABLED] = 0;
	p->raw[OFF_ARP_MODE] = 1; /* confirmed factory default -- DOWN, see program.h */
	p->raw[OFF_ARP_CLOCK_DIV] = 5;
	p->raw[OFF_UNKNOWN_9] = 3;
	/* Tempo = byte[0xb] + byte[0xa]*0x80 = 120 -> byte[0xa]=0, byte[0xb]=120 */
	p->raw[OFF_TEMPO_LOW] = 0;
	p->raw[OFF_TEMPO_HIGH] = 120;
	p->raw[OFF_ARP_RANGE] = 0; /* confirmed factory default -- no octave repeat */

	for (int k = 0; k < 8; k++) {
		p->raw[OFF_KNOB_BASE + k * KNOB_STRIDE + OFF_KNOB_CC] = (uint8_t)(base_cc + k);
	}
	for (int pad = 0; pad < 8; pad++) {
		int base = OFF_PAD_BASE + pad * PAD_STRIDE;
		p->raw[base + OFF_PAD_NOTE] = (uint8_t)(pad + 1); /* confirmed factory default */
		p->raw[base + OFF_PAD_PC] = (uint8_t)pad;          /* placeholder */
		p->raw[base + OFF_PAD_CC] = (uint8_t)(base_cc + pad); /* placeholder */
	}
}

static uint8_t pad_output_mode; /* shared runtime state, not per-program -- see program.h */

void program_init(void)
{
	pad_output_mode = PAD_MODE_NOTE;
	current_program = 0;
	for (int i = 0; i < PROGRAM_COUNT; i++) {
		init_one(&programs[i], 70);
	}
}

uint8_t program_channel(void)
{
	uint8_t ch = programs[current_program].raw[OFF_CHANNEL];
	return (uint8_t)(ch & 0x0F);
}

uint8_t program_octave(void)
{
	uint8_t o = programs[current_program].raw[OFF_OCTAVE];
	return (uint8_t)(o > 8 ? 8 : o);
}

uint8_t program_fine_transpose(void)
{
	return programs[current_program].raw[OFF_FINE_TRANSPOSE];
}

void program_set_octave(uint8_t octave)
{
	if (octave > 8) {
		octave = 8;
	}
	programs[current_program].raw[OFF_OCTAVE] = octave;
}

uint8_t program_arp_enabled(void)
{
	return programs[current_program].raw[OFF_ARP_ENABLED] != 0;
}

void program_toggle_arp_enabled(void)
{
	uint8_t *flag = &programs[current_program].raw[OFF_ARP_ENABLED];
	*flag = (*flag == 0) ? 1 : 0;
}

uint8_t program_arp_clock_div(void)
{
	uint8_t d = programs[current_program].raw[OFF_ARP_CLOCK_DIV];
	return (uint8_t)(d & 0x07);
}

uint8_t program_arp_mode(void)
{
	uint8_t m = programs[current_program].raw[OFF_ARP_MODE];
	return (uint8_t)(m > 5 ? 5 : m);
}

uint8_t program_arp_range(void)
{
	uint8_t r = programs[current_program].raw[OFF_ARP_RANGE];
	return (uint8_t)(r > 3 ? 3 : r);
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
	return pad_output_mode;
}

void program_set_pad_mode(uint8_t mode)
{
	pad_output_mode = mode;
}

/*
 * SysEx wire<->record reorder tables. Generated (and its permutation
 * property verified) from the exact copy sequence read out of the
 * original's FUN_08002eac -- see program.h's comment and
 * FIRMWARE_ANALYSIS.md for the full derivation. record_to_wire is the
 * inverse permutation of wire_to_record.
 */
static const uint8_t WIRE_TO_RECORD[PROGRAM_RECORD_SIZE] = {
	1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
	13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37,
	39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63,
	65, 67, 69, 71, 73, 75, 14, 16, 18, 20, 22, 24, 26,
	28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52,
	54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 77,
	78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
};

static const uint8_t RECORD_TO_WIRE[PROGRAM_RECORD_SIZE] = {
	1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
	13, 45, 14, 46, 15, 47, 16, 48, 17, 49, 18, 50, 19,
	51, 20, 52, 21, 53, 22, 54, 23, 55, 24, 56, 25, 57,
	26, 58, 27, 59, 28, 60, 29, 61, 30, 62, 31, 63, 32,
	64, 33, 65, 34, 66, 35, 67, 36, 68, 37, 69, 38, 70,
	39, 71, 40, 72, 41, 73, 42, 74, 43, 75, 44, 76, 77,
	78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
};

void program_load_from_wire(uint8_t program_index, const uint8_t wire[PROGRAM_RECORD_SIZE])
{
	if (program_index >= PROGRAM_COUNT) {
		return;
	}
	uint8_t *raw = programs[program_index].raw;
	for (int w = 0; w < PROGRAM_RECORD_SIZE; w++) {
		raw[WIRE_TO_RECORD[w]] = wire[w];
	}
}

void program_save_to_wire(uint8_t program_index, uint8_t wire[PROGRAM_RECORD_SIZE])
{
	if (program_index >= PROGRAM_COUNT) {
		return;
	}
	const uint8_t *raw = programs[program_index].raw;
	for (int r = 0; r < PROGRAM_RECORD_SIZE; r++) {
		wire[RECORD_TO_WIRE[r]] = raw[r];
	}
}
