#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

/* Default velocity for Note On (the original firmware may derive real
 * velocity from scan timing between two row transitions -- not yet
 * confirmed/reimplemented; see keys.c). */
#define KEYS_DEFAULT_VELOCITY 100

/* Base MIDI note for key index 0 (bottom-left key). Represents the
 * octave-shift state; the original hardware has physical octave
 * up/down buttons that presumably adjust an equivalent value at
 * runtime -- not yet wired up here (TODO). */
extern uint8_t keys_base_note;

void keys_init(void);

/* Call once per main loop iteration, after matrix_scan(). Detects
 * transitions in matrix_state[] and pushes Note On/Off USB-MIDI
 * events to the MIDI ring buffer. */
void keys_process(void);

#endif /* KEYS_H */
