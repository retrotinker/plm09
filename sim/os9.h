/* OS-9 program-module environment for sim6809 --os9 */
#ifndef OS9_H
#define OS9_H

#include "cpu6809.h"

/* Load the OS-9 program module in file `path` at address `load`, after
   checking its sync bytes, header parity, CRC and type.  Set up a process
   as F$Fork would: a page-aligned data area at `data` with `params` (plus
   a CR) at its top, U = data, X = S = parameters, Y = top, D = parameter
   size, DP = data page, PC = entry point; and install os9_syscall as the
   SWI2 handler.  Returns 0, or -1 after printing an error. */
int os9_load(Cpu *c, const char *path, uint16_t load, uint16_t data, const char *params);

/* SWI2 handler: F$Exit, I$Read, I$ReadLn, I$Write, I$WritLn */
int os9_syscall(Cpu *c);

#endif
