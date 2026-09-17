#ifndef MATRIX_H
#define MATRIX_H

#include <stdint.h>

#define MATRIX_COLS 9
#define MATRIX_ROWS 8

/*
 * Debounced state of each column's row bits, one byte per column
 * (bit N = row N). Updated by matrix_scan(); read by whatever turns
 * transitions into MIDI (not yet reimplemented -- see
 * FIRMWARE_ANALYSIS.md's notes on FUN_08004990).
 */
extern uint8_t matrix_state[MATRIX_COLS];

void matrix_init(void);
void matrix_scan(void);

#endif /* MATRIX_H */
