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

#endif /* ARP_H */
