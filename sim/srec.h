/* Motorola S-record loader for sim6809 */
#ifndef SREC_H
#define SREC_H

#include <stdint.h>

/* Load S1 data records from path into mem.  Sets *entry from an S9 record
   and *has_entry to 1 if one was present.  Returns 0 on success, or -1 after
   printing an error to stderr. */
int srec_load(const char *path, uint8_t *mem, uint16_t *entry, int *has_entry);

#endif
