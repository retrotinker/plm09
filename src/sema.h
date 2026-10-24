/* plmc semantic analysis */
#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "symtab.h"

extern Vec sema_vars;   /* Symbol*: every variable, for the data section */
extern Vec sema_procs;  /* Proc*: every procedure, in declaration order */
extern int sema_goto_out;   /* some GOTO leaves a procedure (needs RT_MAINS) */

/* check the program and annotate it; errors are fatal */
void sema_program(Program *prog);

#endif
