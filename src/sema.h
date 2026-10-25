/* plmc semantic analysis */
#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "symtab.h"

extern Vec sema_vars;   /* Symbol*: every variable, for the data section */
extern Vec sema_procs;  /* Proc*: every procedure, in declaration order */
extern int sema_goto_out;   /* some GOTO leaves a procedure (needs RT_MAINS) */
extern int sema_reentrant;  /* some procedure is REENTRANT (Y is a frame pointer) */

/* INTERRUPT n procedures: 0 = IRQ, 1 = FIRQ, 2 = NMI; NULL if none */
enum { INTERRUPTS = 3 };
extern Proc *sema_interrupt[INTERRUPTS];
extern int sema_pic;        /* compiling an --os9 module (set before sema) */

/* check the program and annotate it; errors are fatal */
void sema_program(Program *prog);

#endif
