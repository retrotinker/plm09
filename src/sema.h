/* plmc semantic analysis */
#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "symtab.h"

extern Vec sema_vars;   /* Symbol*: every variable, for the data section */
extern Vec sema_procs;  /* Proc*: every procedure, in declaration order */
extern int sema_goto_out;   /* some GOTO leaves a procedure (needs RT_MAINS) */
extern Vec sema_public_labels; /* Symbol*: LABEL PUBLIC, exported by name */
extern int sema_reentrant;  /* some procedure is REENTRANT (Y is a frame pointer) */

/* What the program needs to know about the last + - PLUS MINUS SCL SCR or
   DEC executed (the "flags" of PL/M-80 chapter 12), kept at run time:
   its carry (PLUS MINUS SCL SCR DEC CARRY), its half carry (DEC), and its
   result (ZERO SIGN PARITY) */
extern int sema_carry, sema_half, sema_result;

/* runtime routines the program uses (codegen assembles only those) */
extern int sema_uses_copy, sema_uses_time, sema_uses_parity;

/* INTERRUPT n procedures: 0 = IRQ, 1 = FIRQ, 2 = NMI; NULL if none */
enum { INTERRUPTS = 3 };
extern Proc *sema_interrupt[INTERRUPTS];
extern int sema_pic;        /* compiling an --os9 module (set before sema) */

/* check the program and annotate it; errors are fatal */
void sema_program(Program *prog);

#endif
