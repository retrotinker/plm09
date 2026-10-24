/* plmc semantic analysis.
 *
 * Resolves names (declare before use, nested scopes), annotates every
 * expression with BYTE or ADDRESS, checks procedure use, and rejects
 * recursion, since procedures keep parameters and locals in static storage.
 *
 * Type rules (PL/M-80):
 *   constants <= 255 are BYTE, larger ones and 2-character strings ADDRESS
 *   + - MOD AND OR XOR: BYTE if both operands are BYTE, else ADDRESS
 *   * /: always ADDRESS
 *   relationals: BYTE (0FFH true, 0 false)
 *   unary - and NOT: type of the operand
 *   array element A(i): the array's type; an unsubscripted array name
 *   means element 0.  LENGTH(A) and LAST(A) are ADDRESS constants.
 */
#include "sema.h"

#include <stdlib.h>
#include <string.h>

Vec sema_vars;      /* Symbol*: every variable, for the data section */
Vec sema_procs;     /* Proc*: every procedure, in declaration order */

static const char *const register_names[] = {
    "A", "B", "D", "X", "Y", "U", "S", "PC", "CC", "DP", "PCR",
};

static void check_block(Block *b, Scope *scope);
static void check_stmt(Stmt *s, Scope *scope);

static Type binary_type(Op op, Type l, Type r)
{
    if (op_is_relational(op))
        return TY_BYTE;
    if (op == OP_MUL || op == OP_DIV)
        return TY_ADDRESS;
    return l == TY_BYTE && r == TY_BYTE ? TY_BYTE : TY_ADDRESS;
}

static void add_call_edge(Scope *scope, Proc *callee)
{
    Proc *caller = scope->proc;

    if (!caller || callee->external)
        return;
    for (int i = 0; i < caller->callees.len; i++)
        if (caller->callees.items[i] == callee)
            return;
    vec_push(&caller->callees, callee);
}

static Symbol *lookup(Scope *scope, const char *name, Loc loc)
{
    Symbol *sym = scope_lookup(scope, name);
    if (!sym)
        error_at(loc, "%s is not declared", name);
    return sym;
}

static void check_expr(Expr *e, Scope *scope);

/* E_NAME or E_CALL naming a procedure: check arguments, record the call */
static void check_call(Expr *e, Scope *scope, Symbol *sym)
{
    Proc *p = sym->proc;

    if (e->args.len != p->params.len)
        error_at(e->loc, "procedure %s expects %d argument%s, got %d", p->name,
                 p->params.len, p->params.len == 1 ? "" : "s", e->args.len);
    for (int i = 0; i < e->args.len; i++)
        check_expr(e->args.items[i], scope);
    add_call_edge(scope, p);
}

/* PL/M-80 built-ins LENGTH(A) and LAST(A): used only when the name isn't
   declared.  Folded into an ADDRESS constant. */
static int check_builtin(Expr *e, Scope *scope)
{
    int is_length = !strcmp(e->name, "LENGTH");
    Expr *arg;
    Symbol *sym;

    if (e->kind != E_CALL || (!is_length && strcmp(e->name, "LAST")))
        return 0;
    if (e->args.len != 1 || (arg = e->args.items[0])->kind != E_NAME
        || !(sym = scope_lookup(scope, arg->name)) || sym->kind != SYM_VAR)
        error_at(e->loc, "%s needs the name of a variable", e->name);
    unsigned n = sym->dim ? sym->dim : 1;
    e->kind = E_NUM;
    e->value = is_length ? n : n - 1;
    e->is_address = 1;
    e->type = TY_ADDRESS;
    return 1;
}

static void check_expr(Expr *e, Scope *scope)
{
    Symbol *sym;

    switch (e->kind) {
    case E_NUM:
        e->type = e->is_address || e->value > 0xff ? TY_ADDRESS : TY_BYTE;
        break;
    case E_NAME:
    case E_CALL:
        if (!scope_lookup(scope, e->name) && check_builtin(e, scope))
            break;
        sym = e->sym = lookup(scope, e->name, e->loc);
        if (sym->kind == SYM_VAR) {
            if (e->kind == E_CALL) {
                /* NAME(i) on an array is an element, not a call */
                if (!sym->dim)
                    error_at(e->loc, "%s is not a procedure or an array", e->name);
                if (e->args.len != 1)
                    error_at(e->loc, "array %s takes exactly one subscript", e->name);
                e->kind = E_INDEX;
                e->lhs = e->args.items[0];
                check_expr(e->lhs, scope);
            }
            e->type = sym->type;
            break;
        }
        if (sym->type == TY_NONE)
            error_at(e->loc, "untyped procedure %s cannot be used in an expression", e->name);
        check_call(e, scope, sym);
        e->type = sym->type;
        break;
    case E_UNARY:
        check_expr(e->lhs, scope);
        e->type = e->lhs->type;
        break;
    case E_BINARY:
        check_expr(e->lhs, scope);
        check_expr(e->rhs, scope);
        e->type = binary_type(e->op, e->lhs->type, e->rhs->type);
        break;
    case E_INDEX:       /* only assignment targets arrive here already indexed */
        sym = e->sym = lookup(scope, e->name, e->loc);
        if (sym->kind != SYM_VAR)
            error_at(e->loc, "cannot assign to procedure %s", e->name);
        if (!sym->dim)
            error_at(e->loc, "%s is not an array", e->name);
        check_expr(e->lhs, scope);
        e->type = sym->type;
        break;
    }
}

/* ------------------------------------------------------------ declarations */

static void declare_var(VarDecl *v, Scope *scope)
{
    Symbol *sym = scope_lookup_local(scope, v->name);

    if ((unsigned long)v->dim * (v->type == TY_BYTE ? 1 : 2) > 0xffff)
        error_at(v->dim_loc, "array %s is too large (more than 65535 bytes)", v->name);

    /* a declaration of a parameter gives the parameter its type */
    if (sym && sym->is_param && sym->type == TY_NONE) {
        if (v->dim)
            error_at(v->loc, "parameter %s cannot be an array", v->name);
        sym->type = v->type;
        v->sym = sym;
        return;
    }
    sym = scope_define(scope, v->name, SYM_VAR, v->loc);
    sym->type = v->type;
    sym->dim = (int)v->dim;
    v->sym = sym;
    vec_push(&sema_vars, sym);
}

static int is_register_name(const char *name)
{
    for (size_t i = 0; i < sizeof register_names / sizeof *register_names; i++)
        if (!strcmp(name, register_names[i]))
            return 1;
    return 0;
}

static void declare_proc(Proc *p, Scope *scope)
{
    Symbol *sym = scope_define(scope, p->name, SYM_PROC, p->loc);
    Scope *inner = scope_new(scope, p);

    sym->type = p->ret;
    sym->proc = p;
    p->sym = sym;
    vec_push(&sema_procs, p);
    if (p->external) {
        if (is_register_name(p->name))
            error_at(p->loc, "EXTERNAL name %s is also a 6809 register name", p->name);
        free(sym->asm_name);
        sym->asm_name = xstrdup(p->name);
    }

    for (int i = 0; i < p->params.len; i++) {
        VarDecl *v = p->params.items[i];
        Symbol *ps = scope_define(inner, v->name, SYM_VAR, v->loc);
        ps->is_param = 1;
        v->sym = ps;
        if (!p->external)
            vec_push(&sema_vars, ps);
    }

    if (p->external) {
        for (int i = 0; i < p->body->decls.len; i++) {
            Decl *d = p->body->decls.items[i];
            Symbol *ps = d->kind == D_VAR ? scope_lookup_local(inner, d->var->name) : NULL;
            if (!ps || !ps->is_param || ps->type != TY_NONE)
                error_at(d->kind == D_VAR ? d->var->loc : d->proc->loc,
                         "an EXTERNAL procedure may only declare its parameters");
            if (d->var->dim)
                error_at(d->var->loc, "parameter %s cannot be an array", d->var->name);
            ps->type = d->var->type;
            d->var->sym = ps;
        }
        if (p->body->stmts.len)
            error_at(((Stmt *)p->body->stmts.items[0])->loc,
                     "an EXTERNAL procedure cannot have statements");
        p->body->scope = inner;
        for (int i = 0; i < p->params.len; i++) {
            VarDecl *v = p->params.items[i];
            if (v->sym->type == TY_NONE)
                error_at(v->loc, "parameter %s of %s is not declared", v->name, p->name);
        }
    } else {
        check_block(p->body, inner);
    }

    if (!p->external && p->ret != TY_NONE) {
        Vec *st = &p->body->stmts;
        Stmt *last = st->len ? st->items[st->len - 1] : NULL;
        if (!last || (last->kind != S_RETURN && last->kind != S_HALT))
            warning_at(p->loc, "typed procedure %s may reach its END without RETURN", p->name);
    }
}

/* ------------------------------------------------------------- statements */

static void check_block(Block *b, Scope *scope)
{
    b->scope = scope;
    for (int i = 0; i < b->decls.len; i++) {
        Decl *d = b->decls.items[i];
        if (d->kind == D_VAR)
            declare_var(d->var, scope);
        else
            declare_proc(d->proc, scope);
    }
    /* parameters must be declared before the procedure's statements */
    if (scope->proc && scope->proc->body == b) {
        Proc *p = scope->proc;
        for (int i = 0; i < p->params.len; i++) {
            VarDecl *v = p->params.items[i];
            if (v->sym->type == TY_NONE)
                error_at(v->loc, "parameter %s of %s is not declared", v->name, p->name);
        }
    }
    for (int i = 0; i < b->stmts.len; i++)
        check_stmt(b->stmts.items[i], scope);
}

static void check_stmt(Stmt *s, Scope *scope)
{
    Proc *cur = scope->proc;

    switch (s->kind) {
    case S_ASSIGN:
        check_expr(s->expr, scope);
        for (int i = 0; i < s->targets.len; i++) {
            Expr *t = s->targets.items[i];
            if (t->kind == E_INDEX) {
                check_expr(t, scope);
                continue;
            }
            Symbol *sym = t->sym = lookup(scope, t->name, t->loc);
            if (sym->kind != SYM_VAR)
                error_at(t->loc, "cannot assign to procedure %s", t->name);
            t->type = sym->type;
        }
        break;
    case S_CALL: {
        Expr *e = s->expr;
        Symbol *sym = e->sym = lookup(scope, e->name, e->loc);
        if (sym->kind != SYM_PROC)
            error_at(e->loc, "%s is not a procedure", e->name);
        if (sym->type != TY_NONE)
            error_at(e->loc, "typed procedure %s must be used in an expression, not CALLed",
                     e->name);
        check_call(e, scope, sym);
        break;
    }
    case S_IF:
        check_expr(s->expr, scope);
        check_stmt(s->then_s, scope);
        if (s->else_s)
            check_stmt(s->else_s, scope);
        break;
    case S_DO:
        check_block(s->block, s->block->decls.len ? scope_new(scope, cur) : scope);
        break;
    case S_WHILE:
        check_expr(s->expr, scope);
        check_block(s->block, scope);
        break;
    case S_RETURN:
        if (!cur)
            error_at(s->loc, "RETURN outside a procedure");
        s->proc = cur;
        if (s->expr) {
            if (cur->ret == TY_NONE)
                error_at(s->loc, "untyped procedure %s cannot return a value", cur->name);
            check_expr(s->expr, scope);
        } else if (cur->ret != TY_NONE) {
            error_at(s->loc, "RETURN in typed procedure %s needs a value", cur->name);
        }
        break;
    case S_HALT:
    case S_NULL:
        break;
    }
}

/* ----------------------------------------------------------- recursion */

enum { UNVISITED, ACTIVE, DONE };

static Vec path;

static void find_cycle(Proc *p)
{
    p->visit = ACTIVE;
    vec_push(&path, p);
    for (int i = 0; i < p->callees.len; i++) {
        Proc *q = p->callees.items[i];
        if (q->visit == ACTIVE) {
            char chain[512] = "";
            int start = 0;
            while (path.items[start] != q)
                start++;
            for (int k = start; k < path.len; k++) {
                strncat(chain, ((Proc *)path.items[k])->name, sizeof chain - strlen(chain) - 1);
                strncat(chain, " -> ", sizeof chain - strlen(chain) - 1);
            }
            strncat(chain, q->name, sizeof chain - strlen(chain) - 1);
            error_at(q->loc, "procedure %s is recursive (%s); recursion needs REENTRANT, "
                     "which is not supported", q->name, chain);
        }
        if (q->visit == UNVISITED)
            find_cycle(q);
    }
    path.len--;
    p->visit = DONE;
}

void sema_program(Program *prog)
{
    Scope *global = scope_new(NULL, NULL);

    check_block(prog->body, global);
    for (int i = 0; i < sema_procs.len; i++) {
        Proc *p = sema_procs.items[i];
        if (p->visit == UNVISITED)
            find_cycle(p);
    }
}
