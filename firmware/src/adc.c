/*
 * Knob ADC sampling: ADC1 in continuous scan mode, DMA1 channel 1
 * transferring results to SRAM in the background.
 *
 * Reimplemented from confirmed original-firmware behavior
 * (FIRMWARE_ANALYSIS.md's "Confirmed: knob sampling is ADC1 + DMA1"),
 * but simplified: the original software-triggers each conversion
 * batch and polls a DMA completion flag from the main loop; this
 * version uses ADC1's continuous-conversion mode instead, so DMA1
 * keeps adc_raw[] fresh autonomously with no per-iteration triggering
 * needed. Same peripherals, same end result (fresh raw ADC values
 * available every loop iteration), simpler control flow -- written
 * fresh, not copied from the disassembly.
 *
 * CONFIRMED (previously a placeholder): the channel ranges. AKAI's own
 * schematic (this project's `ad07-schematic-page7.jpg`, page "AD07_MPK8
 * V0.03") labels the knob connector's pins ADC0-ADC7 (one per
 * potentiometer, VR1-VR8 in wiring order) and the pad connector's pins
 * ADC8-ADC15 -- matching this driver's channel layout exactly (0-7
 * knobs, 8-15 pads) via the standard STM32F1 ADC12_IN0-15 GPIO mapping
 * (PA0-7 for 0-7, PB0-1 + PC0-5 for 8-15 -- pins not otherwise used by
 * the key/pad matrix scanner in matrix.c, which owns PB8-15/PC7-15).
 *
 * The original reads 16 buffer slots, not 8 -- FIRMWARE_ANALYSIS.md's
 * pad-velocity section (FUN_08003ab8) revised this to most likely 8
 * knobs + 8 pad-velocity-sense channels, not unused headroom, and the
 * schematic now independently confirms that revision was correct.
 *
 * STILL NOT CONFIRMED: the exact wiring *within* each 8-channel group
 * -- i.e. whether VR1 is specifically ADC0 (this driver's assumption,
 * matching the schematic's own left-to-right pin order) or the traces
 * cross somewhere between the connector and the pot, and likewise
 * which of PAD1-8 lands on which of ADC8-15 (the schematic shows
 * non-trivial, crossing trace routing here that this project's copy of
 * the schematic isn't high enough resolution to read reliably --
 * misreading it would be worse than leaving it a placeholder). The
 * *order* used here (VR1..VR8 -> channel 0..7 in schematic pin order;
 * PAD1..PAD8 -> channel 8..15 in numeric order) is a reasonable
 * assumption, not independently traced pad-by-pad.
 */
#include "adc.h"
#include "stm32f102.h"

volatile uint16_t adc_raw[ADC_TOTAL_CHANNELS];

void adc_init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_ADC1EN;
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;

	/* Analog input mode (CNF=00, MODE=00) on every pin used for ADC
	 * input. PA0..PA7 (knobs): all of CRL. PB0..PB1 (pads): low byte
	 * of CRL. PC0..PC5 (pads): low 3 bytes of CRL. */
	GPIOA->CRL &= ~0xFFFFFFFFu;
	GPIOB->CRL &= ~0x000000FFu;
	GPIOC->CRL &= ~0x00FFFFFFu;

	/* Regular sequence: 16 conversions -- channels 0-7 (knobs) then
	 * 8-15 (pads), in order. SQR3 holds sequence positions 1-6 (5
	 * bits each), SQR2 holds 7-12, SQR1 holds 13-16 plus the L field
	 * (bits 20-23) = number of conversions - 1. */
	ADC1->SQR3 = (0u << 0) | (1u << 5) | (2u << 10) | (3u << 15) | (4u << 20) | (5u << 25);
	ADC1->SQR2 = (6u << 0) | (7u << 5) | (8u << 10) | (9u << 15) | (10u << 20) | (11u << 25);
	ADC1->SQR1 = (12u << 0) | (13u << 5) | (14u << 10) | (15u << 15) |
	             ((uint32_t)(ADC_TOTAL_CHANNELS - 1) << 20);

	/* Sample time: max (239.5 cycles) for all channels -- neither
	 * knobs nor pad-velocity sensing need speed, and a longer sample
	 * time reduces noise. SMPR2 covers channels 0-9, SMPR1 covers
	 * 10-17, 3 bits each. */
	ADC1->SMPR2 = 0x3FFFFFFFu;
	ADC1->SMPR1 = 0x00FFFFFFu;

	ADC1->CR1 = 0; /* independent mode, scan handled via CR2 below */
	ADC1->CR2 = ADC_CR2_ADON;
	for (volatile int i = 0; i < 1000; i++) {
	} /* tREF wake-up time */

	/* Calibration (recommended before first use, per RM0008). */
	ADC1->CR2 |= ADC_CR2_RSTCAL;
	while (ADC1->CR2 & ADC_CR2_RSTCAL) {
	}
	ADC1->CR2 |= ADC_CR2_CAL;
	while (ADC1->CR2 & ADC_CR2_CAL) {
	}

	/* DMA1 channel 1 (fixed hardware mapping for ADC1): peripheral =
	 * ADC1->DR, memory = adc_raw[], circular, 16-bit, memory
	 * increment, transfer-complete driven implicitly by circular
	 * mode (no interrupt needed -- adc_raw[] is just always being
	 * refreshed). */
	DMA1->CH[0].CPAR = (uint32_t)&ADC1->DR;
	DMA1->CH[0].CMAR = (uint32_t)adc_raw;
	DMA1->CH[0].CNDTR = ADC_TOTAL_CHANNELS;
	DMA1->CH[0].CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE_16 | DMA_CCR_MSIZE_16 | DMA_CCR_EN;

	/* Scan mode (bit 8 of CR1), continuous conversion, DMA, then
	 * start -- ADC1 now free-runs, continuously refreshing adc_raw[]
	 * via DMA with no further software intervention needed. */
	ADC1->CR1 |= (1u << 8); /* SCAN */
	ADC1->CR2 |= ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL_SWSTART;
	ADC1->CR2 |= ADC_CR2_SWSTART;
}
