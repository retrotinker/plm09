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
 *   means element 0.  Structure member X.M, X(i).M(j): the member's type;
 *   a whole structure is not a value.  LENGTH, LAST (BYTE or ADDRESS) and
 *   SIZE (ADDRESS) are constants.
 *   .X (address of a variable, element or outer-level procedure) and
 *   .(constants): ADDRESS.
 */
#include "sema.h"

#include <stdlib.h>
#include <string.h>

Vec sema_vars;      /* Symbol*: every variable, for the data section */
Vec sema_procs;     /* Proc*: every procedure, in declaration order */
int sema_goto_out;  /* some GOTO leaves a procedure */
int sema_pic;       /* compiling an --os9 module */

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


/* Contexts where a reference may name a whole structure (the operand of
   the dot operator, or of LENGTH/LAST/SIZE), and where an array of
   structures may be followed by a member without a subscript (the
   LIST.INFO shorthand of the built-ins).  Cleared for subscripts. */
static int whole_ok, shorthand_ok;

static void check_sub(Expr *x, Scope *scope)
{
    int w = whole_ok, sh = shorthand_ok;
    whole_ok = shorthand_ok = 0;
    check_expr(x, scope);
    whole_ok = w;
    shorthand_ok = sh;
}

static unsigned type_size(Type t)
{
    return t == TY_BYTE ? 1 : 2;
}

/* bytes in one element of sym (the whole of a scalar) */
static unsigned elem_bytes(const Symbol *sym)
{
    return sym->st ? sym->st->size : type_size(sym->type);
}

/* PL/M-80 built-ins LENGTH, LAST and SIZE (manual 11.1.2), used only when
   the name isn't declared.  The argument is a reference that is not
   evaluated; the result is folded into a constant.  LENGTH and LAST are
   BYTE when the value fits, else ADDRESS; SIZE is ADDRESS. */
static int check_builtin(Expr *e, Scope *scope)
{
    int is_length = !strcmp(e->name, "LENGTH"), is_last = !strcmp(e->name, "LAST");
    int is_size = !strcmp(e->name, "SIZE");
    Expr *arg;
    Symbol *sym;

    if (e->kind != E_CALL || !(is_length || is_last || is_size))
        return 0;
    if (e->args.len != 1
        || ((arg = e->args.items[0])->kind != E_NAME && arg->kind != E_CALL
            && arg->kind != E_MEMBER)
        || !(sym = scope_lookup(scope, arg->name)) || sym->kind != SYM_VAR)
        error_at(e->loc, "%s needs the name of a variable", e->name);

    int w = whole_ok, sh = shorthand_ok;
    whole_ok = shorthand_ok = 1;
    check_expr(arg, scope);
    whole_ok = w;
    shorthand_ok = sh;

    unsigned elems, bytes;              /* of the referenced object */
    if (arg->kind == E_NAME) {
        elems = sym->dim ? (unsigned)sym->dim : 1;
        bytes = elems * elem_bytes(sym);
    } else if (arg->kind == E_INDEX) {  /* one element, e.g. LIST(5) */
        elems = 1;
        bytes = elem_bytes(sym);
    } else {                            /* a member, possibly an array */
        const Member *m = arg->member;
        elems = !arg->rhs && m->dim ? m->dim : 1;
        bytes = elems * type_size(m->type);
    }
    unsigned v = is_size ? bytes : is_length ? elems : elems - 1;
    e->kind = E_NUM;
    e->value = v;
    e->is_address = is_size || v > 0xff;
    e->type = e->is_address ? TY_ADDRESS : TY_BYTE;
    return 1;
}

/* a whole structure where a BYTE or ADDRESS value is needed */
static _Noreturn void whole_structure(const Expr *e)
{
    const Member *m = e->sym->st->members.items[0];
    error_at(e->loc, "structure %s cannot be used as a value; select a member, as in %s.%s",
             e->name, e->name, m->name);
}

/* E_MEMBER: X[(i)].M[(j)] */
static void check_member(Expr *e, Scope *scope)
{
    Symbol *sym = e->sym = lookup(scope, e->name, e->loc);
    const Member *m = NULL;

    if (sym->kind != SYM_VAR || !sym->st)
        error_at(e->loc, "%s is not a structure", e->name);
    if (e->lhs) {
        if (!sym->dim)
            error_at(e->loc, "%s is not an array", e->name);
        check_sub(e->lhs, scope);
    } else if (sym->dim && !shorthand_ok) {
        error_at(e->loc, "%s is an array of structures: select one, as in %s(i).%s",
                 e->name, e->name, e->mname);
    }
    for (int i = 0; i < sym->st->members.len; i++)
        if (!strcmp(((Member *)sym->st->members.items[i])->name, e->mname))
            m = sym->st->members.items[i];
    if (!m)
        error_at(e->loc, "structure %s has no member %s", e->name, e->mname);
    if (e->rhs) {
        if (!m->dim)
            error_at(e->loc, "%s.%s is not an array", e->name, e->mname);
        check_sub(e->rhs, scope);
    }
    e->member = (Member *)m;
    e->type = m->type;
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
        if (sym->kind == SYM_LABEL)
            error_at(e->loc, "%s is a label, not a variable or procedure", e->name);
        if (sym->kind == SYM_VAR) {
            if (e->kind == E_CALL) {
                /* NAME(i) on an array is an element, not a call */
                if (!sym->dim)
                    error_at(e->loc, "%s is not a procedure or an array", e->name);
                if (e->args.len != 1)
                    error_at(e->loc, "array %s takes exactly one subscript", e->name);
                e->kind = E_INDEX;
                e->lhs = e->args.items[0];
                check_sub(e->lhs, scope);
            }
            e->type = sym->type;
            if (e->type == TY_STRUCT && !whole_ok)
                whole_structure(e);
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
    case E_ADDR: {
        e->type = TY_ADDRESS;
        if (!e->lhs)
            break;                      /* .(constant list) */
        Expr *v = e->lhs;
        sym = scope_lookup(scope, v->name);
        if (sym && sym->kind == SYM_PROC) {
            if (v->kind != E_NAME || sym->scope_id != 0)
                error_at(v->loc, "the address of a procedure needs a procedure declared at "
                         "the outer level, written without arguments");
            v->sym = sym;
            v->type = TY_ADDRESS;
            break;
        }
        int w = whole_ok;
        whole_ok = 1;               /* .REC, .LIST(5) are allowed */
        check_expr(v, scope);       /* NAME(i) on an array becomes E_INDEX */
        whole_ok = w;
        if (!((v->kind == E_NAME || v->kind == E_INDEX || v->kind == E_MEMBER)
              && v->sym && v->sym->kind == SYM_VAR))
            error_at(v->loc, "the dot operator needs a variable, an array element or a procedure");
        break;
    }
    case E_INDEX:       /* only assignment targets arrive here already indexed */
        sym = e->sym = lookup(scope, e->name, e->loc);
        if (sym->kind != SYM_VAR)
            error_at(e->loc, "cannot assign to %s %s",
                     sym->kind == SYM_LABEL ? "label" : "procedure", e->name);
        if (!sym->dim)
            error_at(e->loc, "%s is not an array", e->name);
        check_sub(e->lhs, scope);
        e->type = sym->type;
        if (e->type == TY_STRUCT && !whole_ok)
            whole_structure(e);
        break;
    case E_MEMBER:
        check_member(e, scope);
        break;
    }
}

/* ------------------------------------------------------------ declarations */

/* ------------------------------------------------- INITIAL and DATA */

static _Noreturn void bad_initial(Loc loc)
{
    error_at(loc, "an initial value must be a number, a string, or a location "
             "reference plus or minus numbers");
}

/* is this the operand of a location reference with constant subscripts? */
static int constant_ref(const Expr *v)
{
    if (v->kind == E_INDEX)
        return v->lhs->kind == E_NUM;
    if (v->kind == E_MEMBER)
        return (!v->lhs || v->lhs->kind == E_NUM) && (!v->rhs || v->rhs->kind == E_NUM);
    return 1;
}

/* a restricted expression (PL/M-80 manual 6.2.8): a numeric constant or
   a location reference, followed by + or - numeric constants */
static void restricted(Expr *e, Scope *scope, InitVal *iv)
{
    switch (e->kind) {
    case E_NUM:
        iv->value = e->value;
        return;
    case E_UNARY:
        if (e->op != OP_NEG || e->lhs->kind != E_NUM)
            bad_initial(e->loc);
        iv->value = (0u - e->lhs->value) & 0xffff;
        return;
    case E_ADDR:
        check_expr(e, scope);
        if (e->lhs && e->lhs->sym->kind == SYM_VAR) {
            if (e->lhs->sym->base)
                error_at(e->loc, "an initial value cannot use the address of a BASED variable");
            if (!constant_ref(e->lhs))
                error_at(e->loc, "subscripts in an initial value must be constants");
        }
        iv->addr = e;
        iv->value = 0;
        return;
    case E_BINARY: {
        InitVal r = { 0, 0, NULL };
        if (e->op != OP_ADD && e->op != OP_SUB)
            bad_initial(e->loc);
        restricted(e->lhs, scope, iv);
        restricted(e->rhs, scope, &r);
        if (r.addr)
            bad_initial(e->rhs->loc);
        iv->value = (e->op == OP_ADD ? iv->value + r.value : iv->value - r.value) & 0xffff;
        return;
    }
    default:
        bad_initial(e->loc);
    }
}

/* take the value for the next scalar of type t from l; 0 when l is used up */
static int take_value(Symbol *sym, InitList *l, Type t, Scope *scope)
{
    if (l->item >= l->items.len)
        return 0;
    InitItem *it = l->items.items[l->item];
    InitVal *iv = xcalloc(1, sizeof *iv);
    iv->size = t == TY_BYTE ? 1 : 2;
    if (it->str) {                      /* one character per BYTE, two per ADDRESS */
        iv->value = (unsigned char)it->str[l->chr++];
        if (t == TY_ADDRESS && l->chr < it->len)
            iv->value = iv->value << 8 | (unsigned char)it->str[l->chr++];
        if (l->chr >= it->len) {
            l->item++;
            l->chr = 0;
        }
    } else {
        restricted(it->expr, scope, iv);
        l->item++;
        if (t == TY_BYTE && iv->addr)
            error_at(it->loc, "an address can only initialize an ADDRESS");
        if (t == TY_BYTE && iv->value > 0xff)
            error_at(it->loc, "an initial value for a BYTE must be from 0 to 255");
    }
    if (iv->addr && sym->init == INIT_DATA && sema_pic)
        error_at(it->loc, "DATA in an --os9 module cannot hold addresses (use INITIAL)");
    vec_push(&sym->init_vals, iv);
    return 1;
}

/* fill sym's scalars, in storage order, from its value list */
static void take_values(Symbol *sym, InitList *l, Scope *scope)
{
    unsigned n = sym->dim ? (unsigned)sym->dim : 1;

    for (unsigned e = 0; e < n; e++) {
        if (!sym->st) {
            if (!take_value(sym, l, sym->type, scope))
                return;
            continue;
        }
        for (int i = 0; i < sym->st->members.len; i++) {
            const Member *m = sym->st->members.items[i];
            for (unsigned k = 0; k < (m->dim ? m->dim : 1); k++)
                if (!take_value(sym, l, m->type, scope))
                    return;
        }
    }
}

/* number of scalars of type t that a value list fills, for (*) */
static unsigned count_values(const InitList *l, Type t)
{
    unsigned n = 0;
    for (int i = 0; i < l->items.len; i++) {
        const InitItem *it = l->items.items[i];
        n += it->str ? (t == TY_BYTE ? (unsigned)it->len : (unsigned)(it->len + 1) / 2) : 1;
    }
    return n;
}

/* the base of a BASED variable: a declared ADDRESS scalar, not itself
   based (PL/M-80 manual 3.6.3) */
static Symbol *resolve_base(VarDecl *v, Scope *scope)
{
    Symbol *b = scope_lookup(scope, v->base);

    if (!b)
        error_at(v->base_loc, "%s, the base of %s, is not declared", v->base, v->name);
    if (b->kind != SYM_VAR || b->type != TY_ADDRESS || b->dim || b->base)
        error_at(v->base_loc, "the base of a BASED variable must be an ADDRESS scalar "
                 "that is not itself BASED");
    return b;
}

static void declare_var(VarDecl *v, Scope *scope)
{
    Symbol *sym = scope_lookup_local(scope, v->name);

    if (v->init) {
        if (v->base)
            error_at(v->loc, "a BASED variable cannot be initialized");
        if (sym && sym->is_param)
            error_at(v->loc, "parameter %s cannot be initialized", v->name);
        if (v->init->kind == INIT_INITIAL && scope->id != 0)
            error_at(v->init->loc, "INITIAL is only allowed at the outer level of the main "
                     "program (use DATA)");
        if (v->implicit_dim)
            v->dim = count_values(v->init, v->type);
    }

    if (v->st && !v->st->size) {        /* lay out the members (once if factored) */
        unsigned long off = 0;
        for (int i = 0; i < v->st->members.len; i++) {
            Member *m = v->st->members.items[i];
            m->offset = (unsigned)off;
            off += (unsigned long)(m->dim ? m->dim : 1) * type_size(m->type);
            if (off > 0xffff)
                error_at(m->loc, "structure %s is too large (more than 65535 bytes)", v->name);
        }
        v->st->size = (unsigned)off;
    }
    unsigned long each = v->st ? v->st->size : type_size(v->type);
    if ((unsigned long)(v->dim ? v->dim : 1) * each > 0xffff)
        error_at(v->dim ? v->dim_loc : v->loc, "array %s is too large (more than 65535 bytes)",
                 v->name);

    /* a declaration of a parameter gives the parameter its type */
    if (sym && sym->is_param && sym->type == TY_NONE) {
        if (v->dim)
            error_at(v->loc, "parameter %s cannot be an array", v->name);
        if (v->base)
            error_at(v->loc, "parameter %s cannot be BASED", v->name);
        if (v->st)
            error_at(v->loc, "parameter %s cannot be a structure", v->name);
        sym->type = v->type;
        v->sym = sym;
        return;
    }
    Symbol *base = v->base ? resolve_base(v, scope) : NULL;
    sym = scope_define(scope, v->name, SYM_VAR, v->loc);
    sym->type = v->type;
    sym->dim = (int)v->dim;
    sym->base = base;
    sym->st = v->st;
    v->sym = sym;
    if (v->init) {
        sym->init = v->init->kind;
        take_values(sym, v->init, scope);
    }
    if (!base)                  /* a BASED variable has no storage of its own */
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
            if (d->var->base)
                error_at(d->var->loc, "parameter %s cannot be BASED", d->var->name);
            if (d->var->st)
                error_at(d->var->loc, "parameter %s cannot be a structure", d->var->name);
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

/* Labels are declared implicitly in the smallest enclosing block (PL/M-80
   manual 6.3.1, 9.3) and may be used before their definition, so each
   block's labels are collected before its declarations (procedure bodies)
   and statements are checked.  An IF
   is not a block; every DO is, and its own labels belong outside it. */
static void collect_labels(Stmt *s, Scope *scope)
{
    for (int i = 0; i < s->labels.len; i++) {
        Expr *l = s->labels.items[i];
        Symbol *sym = scope_define(scope, l->name, SYM_LABEL, l->loc);
        sym->owner = scope->proc;
        l->sym = sym;
    }
    if (s->kind == S_IF) {
        collect_labels(s->then_s, scope);
        if (s->else_s)
            collect_labels(s->else_s, scope);
    }
}

static void check_goto(Stmt *s, Scope *scope)
{
    Expr *l = s->expr;
    Symbol *sym = l->sym = lookup(scope, l->name, l->loc);

    if (sym->kind != SYM_LABEL)
        error_at(l->loc, "%s is not a label", l->name);
    if (sym->owner == scope->proc)
        return;                         /* a jump within one procedure (or main) */
    /* leaving a procedure: only to the outer level of the main program */
    if (sym->owner || sym->scope_id != 0)
        error_at(l->loc, "a GOTO out of a procedure must go to a label at the outer "
                 "level of the main program");
    s->goto_out = 1;
    sema_goto_out = 1;
}

static void check_block(Block *b, Scope *scope)
{
    b->scope = scope;
    /* labels first: procedures declared in this block may GOTO them */
    for (int i = 0; i < b->stmts.len; i++)
        collect_labels(b->stmts.items[i], scope);
    for (int i = 0; i < b->decls.len; i++) {
        Decl *d = b->decls.items[i];
        if (d->kind == D_VAR)
            declare_var(d->var, scope);
        else
            declare_proc(d->proc, scope);
        /* the last name sharing a value list: every value must be used */
        InitList *l = d->kind == D_VAR ? d->var->init : NULL;
        Decl *next = i + 1 < b->decls.len ? b->decls.items[i + 1] : NULL;
        if (l && !(next && next->kind == D_VAR && next->var->init == l) && l->item < l->items.len)
            error_at(((InitItem *)l->items.items[l->item])->loc,
                     "more initial values than scalars to initialize");
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
            if (t->kind == E_INDEX || t->kind == E_MEMBER) {
                check_expr(t, scope);
                if (t->sym->init == INIT_DATA)
                    error_at(t->loc, "%s is DATA and cannot be assigned", t->name);
                continue;
            }
            Symbol *sym = t->sym = lookup(scope, t->name, t->loc);
            if (sym->kind != SYM_VAR)
                error_at(t->loc, "cannot assign to %s %s",
                         sym->kind == SYM_LABEL ? "label" : "procedure", t->name);
            t->type = sym->type;
            if (t->type == TY_STRUCT)
                whole_structure(t);
            if (sym->init == INIT_DATA)
                error_at(t->loc, "%s is DATA and cannot be assigned", t->name);
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
        check_block(s->block, scope_new(scope, cur));   /* every DO is a block */
        break;
    case S_WHILE:
        check_expr(s->expr, scope);
        check_block(s->block, scope_new(scope, cur));
        break;
    case S_ITER: {
        Expr *v = s->var;
        Symbol *sym = v->sym = scope_lookup(scope, v->name);
        if (sym && sym->init == INIT_DATA)
            error_at(v->loc, "%s is DATA and cannot be assigned", v->name);
        if (!sym || sym->kind != SYM_VAR || sym->dim || sym->base || sym->st)
            error_at(v->loc, "the index of an iterative DO must be a scalar variable%s",
                     sym ? "" : " (not declared)");
        v->type = sym->type;
        check_expr(s->from, scope);
        check_expr(s->to, scope);
        if (s->by)
            check_expr(s->by, scope);
        check_block(s->block, scope_new(scope, cur));
        break;
    }
    case S_CASE:
        check_expr(s->expr, scope);
        check_block(s->block, scope_new(scope, cur));
        break;
    case S_GOTO:
        check_goto(s, scope);
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
