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
 * NOT YET CONFIRMED: which physical GPIOA pin (ADC channel N = pin
 * PAN, fixed STM32F1 hardware mapping) each knob is actually wired
 * to, and whether it's really channels 0-7 rather than some other
 * subset (the original reads 16 buffer slots, not 8 -- possibly
 * covering the joystick's 2 axes too, or just headroom). Using
 * channels 0-7 / PA0-PA7 as a reasonable placeholder; needs
 * confirming against the schematic or real hardware before the CC
 * values coming out actually correspond to the right physical knob.
 */
#include "adc.h"
#include "stm32f102.h"

volatile uint16_t adc_raw[ADC_NUM_CHANNELS];

void adc_init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;

	/* PA0..PA7 as analog input (CRL nibble = 0b0000 per pin). */
	GPIOA->CRL &= ~0xFFFFFFFFu;

	/* Regular sequence: 8 conversions, channels 0..7 in order.
	 * SQR3 holds sequence positions 1-6 (4 bits each), SQR2 holds
	 * 7-12. SQR1's L field (bits 20-23) = number of conversions - 1. */
	ADC1->SQR3 = (0u << 0) | (1u << 5) | (2u << 10) | (3u << 15) | (4u << 20) | (5u << 25);
	ADC1->SQR2 = (6u << 0) | (7u << 5);
	ADC1->SQR1 = (uint32_t)(ADC_NUM_CHANNELS - 1) << 20;

	/* Sample time: max (239.5 cycles) for all channels -- knobs don't
	 * need speed, and a longer sample time reduces noise. SMPR2
	 * covers channels 0-9, 3 bits each. */
	ADC1->SMPR2 = 0x3FFFFFFFu;

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
	DMA1->CH[0].CNDTR = ADC_NUM_CHANNELS;
	DMA1->CH[0].CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE_16 | DMA_CCR_MSIZE_16 | DMA_CCR_EN;

	/* Scan mode (bit 8 of CR1), continuous conversion, DMA, then
	 * start -- ADC1 now free-runs, continuously refreshing adc_raw[]
	 * via DMA with no further software intervention needed. */
	ADC1->CR1 |= (1u << 8); /* SCAN */
	ADC1->CR2 |= ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL_SWSTART;
	ADC1->CR2 |= ADC_CR2_SWSTART;
}
