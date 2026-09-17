#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_MAX_NOTES 8

/* Off by default -- see arp.c's header comment. Nothing currently sets
 * this to 1; wiring it to a real trigger (a mode button, a decoded
 * program-record flag) is a follow-up. */
extern uint8_t arp_enabled;

void arp_init(void);
void arp_note_on(uint8_t note, uint8_t velocity);
void arp_note_off(uint8_t note);
void arp_process(void);

#endif /* ARP_H */
