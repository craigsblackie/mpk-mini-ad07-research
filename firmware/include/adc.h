#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_NUM_CHANNELS 8 /* one per knob -- see adc.c header comment */

/* Raw 12-bit ADC readings, continuously updated by DMA in the
 * background -- no polling/blocking needed to read these. */
extern volatile uint16_t adc_raw[ADC_NUM_CHANNELS];

void adc_init(void);

#endif /* ADC_H */
