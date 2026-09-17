#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_MAX_NOTES 8

/* Manual override, defaults to 1 -- actual enable is this ANDed with
 * the current program's own arp-enabled flag (program_arp_enabled()).
 * See arp.c's header comment. */
extern uint8_t arp_enabled;

void arp_init(void);
void arp_note_on(uint8_t note, uint8_t velocity);
void arp_note_off(uint8_t note);
void arp_process(void);

/* Registers one tap-tempo tap (call on each press of the tap-tempo
 * button -- see transport.c). Confirmed as a real original feature
 * (FUN_08006988), reimplemented simplified: this uses the interval
 * since the immediately preceding tap directly, rather than the
 * original's average over multiple recent taps (its confirmed
 * behavior, but the exact number of taps averaged wasn't reused here
 * -- see arp.c's header). Overrides the program's stored tempo until
 * ~2 seconds pass with no further taps. */
void arp_tap(void);

#endif /* ARP_H */
