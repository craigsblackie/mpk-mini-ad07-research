#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

/*
 * Matrix column 7 button cluster: sustain pedal, octave up/down, tap
 * tempo. See transport.c for the full derivation (FUN_08006988).
 */
void transport_init(void);

/* status_byte: matrix_state[7] (confirmed source -- see transport.c). */
void transport_process(uint8_t status_byte);

#endif /* TRANSPORT_H */
