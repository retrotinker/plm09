/* FLEX 9 environment for sim6809 --flex */
#ifndef FLEX_H
#define FLEX_H

#include "cpu6809.h"

/* Load the FLEX binary (.CMD) file `path`: $02 records (address, count,
   data) and a $16 transfer address record, which is required.  Set up
   the machine as FLEX runs a command: S = $C07F, PC = transfer address,
   memory end ($CC2B) = $BFFF; emulate the DOS entry points at $CD00-$CD4E
   (flex_call); and stop the program with an error if it writes to FLEX's
   memory above its stack ($C080-$DFFF) or runs code there.  Returns 0, or
   -1 after printing an error. */
int flex_load(Cpu *c, const char *path);

/* PC hook: emulate the DOS routine at c->pc, if it is one; returns -1 if
   the PC isn't a FLEX entry point, otherwise c->stop */
int flex_call(Cpu *c);

#endif
