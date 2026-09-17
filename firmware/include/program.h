#ifndef PROGRAM_H
#define PROGRAM_H

#include <stdint.h>

/*
 * Per-program configuration record.
 *
 * Layout is the original firmware's own 101-byte per-program record,
 * decoded from FIRMWARE_ANALYSIS.md's "Major new finding: 101-byte
 * per-program record layout" and cross-confirmed against the actual
 * consumer functions (the knob and pad handlers) -- not a from-scratch
 * design. PROGRAM_COUNT (5) matches the original's own range check
 * (FUN_08005ac8 validates `param_1 < 5`).
 */
#define PROGRAM_COUNT 5
#define PROGRAM_RECORD_SIZE 101

#define PAD_MODE_NOTE 1
#define PAD_MODE_CC 2
#define PAD_MODE_PC 3

typedef struct {
	uint8_t raw[PROGRAM_RECORD_SIZE];
	/* NOT part of the original's own 101-byte record -- see
	 * program_pad_mode()'s comment below. Kept alongside raw[] rather
	 * than inside it so raw[] stays an exact match to the original's
	 * confirmed layout. */
	uint8_t pad_mode;
} program_record_t;

extern program_record_t programs[PROGRAM_COUNT];
extern uint8_t current_program;

void program_init(void);

/* record+0x00: MIDI channel (0-15), confirmed shared by the knob
 * handler. Reused here for pads too (not independently confirmed for
 * pads -- a reasonable assumption of one MIDI channel per program). */
uint8_t program_channel(void);

/* record+0x04: arp on/off. record+0x06: arp clock-division selector
 * (0-7), see FIRMWARE_ANALYSIS.md's confirmed tick/step table.
 * record+0x0a/+0x0b: tempo (BPM), combined per the original's own
 * validation formula. record+0x05: arp mode (0-5) -- confirmed via
 * FUN_08002588's 6-case switch: 0=up, 1=down (factory default), 2=up-
 * down, 3=down-up, 4=random (confirmed LCG, `x = x*0x6255 + 0x3619`),
 * 5=shape-identical to up but bounded against a different held-note
 * count (`DAT_08002c98` vs. `DAT_0800299c` elsewhere in that function)
 * -- plausibly an "order played" mode, not independently confirmed;
 * see ARP_MODE_* below and FIRMWARE_ANALYSIS.md. record+0x0c: arp
 * octave range (0-3 additional octave repeats above the base pass,
 * confirmed via that same function's per-pass-completion increment
 * adding `range_pass * 12` semitones to the note before sending). */
uint8_t program_arp_enabled(void);
/* Confirmed a real button toggles this directly (FUN_08006988 bit 0,
 * a literal `flag = (flag == 0)` boolean flip) -- see transport.c. */
void program_toggle_arp_enabled(void);
uint8_t program_arp_clock_div(void);
uint16_t program_tempo_bpm(void);
uint8_t program_arp_mode(void);
uint8_t program_arp_range(void);

/* record+0x02/+0x03: the keyboard's base-note transpose, confirmed via
 * FUN_08004990 (the key edge detector)'s own note computation: `note =
 * key_index + record[2]*12 + record[3]`. record+0x02 (0-8, factory
 * default 4) is octave-scale (each unit = 12 semitones; default 4
 * lands key_index 0 on... the formula's default gives 4*12+12=60,
 * i.e. middle C, when record+0x03 is also at its factory default of
 * 12). record+0x03 (0-24, factory default 12) is a finer transpose
 * within that. Confirmed via `FUN_08006988`'s transport-button
 * handler (see FIRMWARE_ANALYSIS.md) that record+0x02 specifically is
 * live-adjustable by two dedicated buttons (increment/decrement,
 * clamped 0-8) with a reset-to-4 combo -- i.e. these are the real
 * octave up/down buttons, on matrix column 7 (see transport.c), not
 * the column-8 buttons buttons.c implements (whose actual purpose is
 * now uncertain again -- see buttons.c's header). */
uint8_t program_octave(void);
uint8_t program_fine_transpose(void);
void program_set_octave(uint8_t octave); /* clamped 0-8 */

#define ARP_MODE_UP 0
#define ARP_MODE_DOWN 1
#define ARP_MODE_UP_DOWN 2
#define ARP_MODE_DOWN_UP 3
#define ARP_MODE_RANDOM 4
#define ARP_MODE_ORDER 5

/* record+0x4d + knob*3: knob CC number (0 = knob unassigned, per the
 * original's confirmed "gate byte doubles as CC number" behavior). */
uint8_t program_knob_cc(uint8_t knob);

/* record+0x0d + pad*8: pad sub-record. +0x0 note#, +0x2 program-change
 * number, +0x4 CC number -- whichever is used depends on the active
 * pad output mode. */
uint8_t program_pad_note(uint8_t pad);
uint8_t program_pad_pc(uint8_t pad);
uint8_t program_pad_cc(uint8_t pad);

/* NOT part of the confirmed per-program record -- the original reads
 * this from a single shared runtime variable (*DAT_08003e18 in
 * FIRMWARE_ANALYSIS.md's pad velocity section), not obviously indexed
 * by program. Stored here per-program anyway for architectural
 * consistency with how record+0x04's arp flag is cached into runtime
 * state on program change (FUN_08005734's confirmed pattern) --
 * genuinely uncertain whether the original does the same for this
 * field. Returns PAD_MODE_NOTE/CC/PC. */
uint8_t program_pad_mode(void);

/*
 * SysEx wire <-> record conversion.
 *
 * Reproduces the exact byte-reorder table read directly out of the
 * original firmware's SysEx 'a' (write program) and 'c' (dump program)
 * command handlers (FUN_08002eac) -- FIRMWARE_ANALYSIS.md's "Follow-up
 * pass: full command set and wire encoding" section has the full
 * derivation. It's a pure reorder (same 101 bytes both ways), not a
 * bit-packing scheme, despite an earlier pass of that document
 * guessing otherwise before the function was read in full.
 *
 * wire[] here is just the 101-byte payload -- the 8-byte message
 * header (F0 47 <id> 7C <cmd> <len_hi> <len_lo> <program#>) and
 * trailing F7 are sysex.c's concern, not this module's.
 */
void program_load_from_wire(uint8_t program_index, const uint8_t wire[PROGRAM_RECORD_SIZE]);
void program_save_to_wire(uint8_t program_index, uint8_t wire[PROGRAM_RECORD_SIZE]);

#endif /* PROGRAM_H */
