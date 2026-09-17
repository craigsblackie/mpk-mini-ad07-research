/*
 * Key/pad matrix scanner for the AD07 mainboard.
 *
 * Reimplementation based on documented, byte-verified behavior from
 * FIRMWARE_ANALYSIS.md (see "Confirmed: key/pad matrix scanner"), not
 * copied from the original decompiled code. The column-select values
 * (PC7..PC15, one-hot) are hardware-wiring facts read from the
 * original firmware's data table, reused here because they describe
 * this specific board's physical PCB trace layout, not any original
 * expression -- the same way a keyboard firmware's matrix pinout is
 * always hardware-specific, factual data.
 *
 * 9 columns driven via GPIOC[15:7] (one-hot, active low), 8 rows read
 * from GPIOB_IDR[15:8]. Debounce: a column's row-byte must read the
 * same value on two consecutive scans before matrix_state[] updates.
 */
#include "matrix.h"
#include "stm32f102.h"

/* PC7..PC15, one bit per column, active low (matches the original
 * firmware's table at flash 0x08006da8 -- confirmed via raw-byte
 * read against the verified dump). */
static const uint16_t column_mask[MATRIX_COLS] = {
	0x0080, 0x0100, 0x0200, 0x0400, 0x0800,
	0x1000, 0x2000, 0x4000, 0x8000,
};

uint8_t matrix_state[MATRIX_COLS];
static uint8_t pending_state[MATRIX_COLS];
static uint8_t stable_count[MATRIX_COLS];

void matrix_init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;

	/* GPIOC[15:7]: general purpose push-pull output, 2 MHz.
	 * TODO: this sets CRL/CRH per-pin config; not yet written out
	 * bit-by-bit here -- placeholder until the exact original
	 * GPIO speed/mode setup (confirmed separately in
	 * FIRMWARE_ANALYSIS.md's AFIO/GPIO init functions) is
	 * reimplemented. Functionally, columns need to be push-pull
	 * outputs and rows need to be inputs (pull-up recommended so
	 * an unconnected/open row reads high, matching the original's
	 * active-low column / active-high-when-unpressed row logic). */
	GPIOC->ODR |= 0xFF80u; /* all columns idle high (deselected) */

	for (int i = 0; i < MATRIX_COLS; i++) {
		matrix_state[i] = 0;
		pending_state[i] = 0;
		stable_count[i] = 0;
	}
}

void matrix_scan(void)
{
	for (int col = 0; col < MATRIX_COLS; col++) {
		GPIOC->ODR = (GPIOC->ODR | 0xFF80u) & ~column_mask[col];

		for (volatile int d = 0; d < 40; d++) {
		}

		uint8_t rows = (uint8_t)(GPIOB->IDR >> 8);

		if (rows == pending_state[col]) {
			if (stable_count[col] < 0xF0) {
				stable_count[col]++;
			}
			if (stable_count[col] == 1) {
				matrix_state[col] = rows;
			}
		} else {
			pending_state[col] = rows;
			stable_count[col] = 0;
		}
	}

	GPIOC->ODR |= 0xFF80u; /* deselect all columns when done */
}
