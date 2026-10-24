/* plmc symbol table: nested scopes */
#include "symtab.h"

#include <stdio.h>
#include <string.h>

static int next_scope_id;

Scope *scope_new(Scope *parent, Proc *proc)
{
    Scope *s = xcalloc(1, sizeof *s);
    s->parent = parent;
    s->proc = proc;
    s->id = parent ? ++next_scope_id : 0;
    return s;
}

Symbol *scope_lookup_local(const Scope *s, const char *name)
{
    for (Symbol *sym = s->syms; sym; sym = sym->next)
        if (!strcmp(sym->name, name))
            return sym;
    return NULL;
}

Symbol *scope_lookup(const Scope *s, const char *name)
{
    for (; s; s = s->parent) {
        Symbol *sym = scope_lookup_local(s, name);
        if (sym)
            return sym;
    }
    return NULL;
}

Symbol *scope_define(Scope *s, const char *name, SymKind kind, Loc loc)
{
    Symbol *old = scope_lookup_local(s, name);
    const char *prefix = kind == SYM_VAR ? "V" : kind == SYM_PROC ? "P" : "G";
    char buf[64];

    if (old) {
        /* labels are collected before a block's declarations; report a clash
           at the label, which comes later in the text */
        if (old->kind == SYM_LABEL && kind != SYM_LABEL)
            error_at(old->loc, "%s is already declared at line %d", name, loc.line);
        error_at(loc, "%s is already declared at line %d", name, old->loc.line);
    }
    Symbol *sym = xcalloc(1, sizeof *sym);
    sym->name = xstrdup(name);
    sym->kind = kind;
    sym->scope_id = s->id;
    sym->loc = loc;
    if (s->id == 0)
        snprintf(buf, sizeof buf, "%s_", prefix);
    else
        snprintf(buf, sizeof buf, "%s%d_", prefix, s->id);
    sym->asm_name = xmalloc(strlen(buf) + strlen(name) + 1);
    strcpy(sym->asm_name, buf);
    strcat(sym->asm_name, name);
    sym->next = s->syms;
    s->syms = sym;
    return sym;
}
