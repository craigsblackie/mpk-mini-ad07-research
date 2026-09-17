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
 * validation formula. */
uint8_t program_arp_enabled(void);
uint8_t program_arp_clock_div(void);
uint16_t program_tempo_bpm(void);

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

#endif /* PROGRAM_H */
