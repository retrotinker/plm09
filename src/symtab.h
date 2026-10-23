/* plmc symbol table: nested scopes */
#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

typedef enum SymKind { SYM_VAR, SYM_PROC } SymKind;

/* where a variable lives; only ST_STATIC is generated today.  ST_FRAME is
   reserved for REENTRANT procedures (U-relative slots). */
typedef enum Storage { ST_STATIC, ST_FRAME } Storage;

typedef struct Symbol {
    char *name;
    SymKind kind;
    Type type;              /* variable type, or procedure result type */
    int dim;                /* 0 = scalar; reserved for arrays */
    Storage storage;
    int scope_id;
    char *asm_name;         /* label in the generated assembly */
    Loc loc;
    int is_param;
    Proc *proc;             /* SYM_PROC */
    struct Symbol *next;    /* next symbol in the same scope */
} Symbol;

typedef struct Scope {
    struct Scope *parent;
    Symbol *syms;
    int id;                 /* 0 for the outermost (program) scope */
    Proc *proc;             /* innermost enclosing procedure, or NULL */
} Scope;

Scope *scope_new(Scope *parent, Proc *proc);
Symbol *scope_lookup_local(const Scope *s, const char *name);
Symbol *scope_lookup(const Scope *s, const char *name);
/* define name in s; a redeclaration in the same scope is an error */
Symbol *scope_define(Scope *s, const char *name, SymKind kind, Loc loc);

#endif
