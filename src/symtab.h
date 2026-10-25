/* plmc symbol table: nested scopes */
#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"

typedef enum SymKind { SYM_VAR, SYM_PROC, SYM_LABEL } SymKind;

/* where a variable lives: at a fixed place (in memory, or in an --os9
   data area), or in the stack frame of a REENTRANT procedure (Y-relative) */
typedef enum Storage { ST_STATIC, ST_FRAME } Storage;

typedef struct Symbol {
    char *name;
    SymKind kind;
    Type type;              /* variable type, or procedure result type */
    int dim;                /* number of array elements; 0 = scalar */
    Storage storage;
    int scope_id;
    char *asm_name;         /* label in the generated assembly */
    Loc loc;
    int is_param;
    struct Symbol *base;    /* BASED variable: its base (an ADDRESS scalar) */
    StructType *st;         /* STRUCTURE variable: its layout */
    InitKind init;          /* INITIAL or DATA */
    Vec init_vals;          /* InitVal*: the initialized scalars, in order */
    AtSpec *at;             /* declared AT: no storage of its own */
    unsigned at_off;        /* offset from the AT address (factored names) */
    int absolute;           /* at a fixed numeric address (AT n): never U/PC-relative */
    int in_code;            /* stored with the code (DATA, or AT one) */
    Proc *proc;             /* SYM_PROC */
    Proc *owner;            /* SYM_LABEL: enclosing procedure, NULL in main;
                               ST_FRAME variable: its procedure */
    unsigned frame_off;     /* ST_FRAME: offset from Y */
    int pending;            /* REENTRANT procedure not declared yet */
    int label_declared;     /* SYM_LABEL from a LABEL declaration */
    LabelAttr label_attr;   /* ... and its attribute */
    int defined;            /* SYM_LABEL: a statement carries it */
    int used;               /* SYM_LABEL: some GOTO names it */
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
