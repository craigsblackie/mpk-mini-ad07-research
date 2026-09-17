#ifndef STUCK_NOTE_H
#define STUCK_NOTE_H

#include <stdint.h>

#define STUCK_NOTE_SLOTS 8

void stuck_note_init(void);
void stuck_note_on(uint8_t channel, uint8_t note);
void stuck_note_off(uint8_t channel, uint8_t note);
void stuck_note_process(void);

#endif /* STUCK_NOTE_H */
