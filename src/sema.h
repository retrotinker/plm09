/* plmc semantic analysis */
#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "symtab.h"

extern Vec sema_vars;   /* Symbol*: every variable, for the data section */
extern Vec sema_procs;  /* Proc*: every procedure, in declaration order */

/* check the program and annotate it; errors are fatal */
void sema_program(Program *prog);

#endif
