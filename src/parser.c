/* plmc recursive-descent parser.
 *
 * program  := IDENT ':' DO ';' block END [IDENT] ';'
 * block    := { declaration } { statement }
 * expr     := and { (OR|XOR) and }
 * and      := not { AND not }
 * not      := NOT not | rel
 * rel      := add { relop add }
 * add      := mul { (+|-) mul }
 * mul      := unary { (*|/|MOD) unary }
 * unary    := (-|+) unary | primary
 * primary  := NUMBER | IDENT [ '(' expr {',' expr} ')' ] | '(' expr ')'
 *
 * NAME(args) in an expression is parsed as a call; sema turns it into an
 * array element (E_INDEX) when NAME is an array.
 */
#include "parser.h"
#include "lexer.h"

#include <string.h>

/* ---------------------------------------------------- LITERALLY scopes */

/* Names declared in each open block, so that a LITERALLY name is expanded
   only where it is visible and not hidden by an inner declaration.
   text is the literal's body, or NULL for any other kind of name. */
typedef struct LitName {
    char *name;
    char *text;
    int line;
} LitName;

typedef struct LitScope {
    Vec names;                  /* LitName* */
    struct LitScope *up;
} LitScope;

static LitScope *lit_scope;

static void lit_push(void)
{
    LitScope *sc = xcalloc(1, sizeof *sc);
    sc->up = lit_scope;
    lit_scope = sc;
}

static void lit_pop(void)
{
    lit_scope = lit_scope->up;
}

static LitName *lit_find_local(const LitScope *sc, const char *name)
{
    for (int i = 0; i < sc->names.len; i++) {
        LitName *n = sc->names.items[i];
        if (!strcmp(n->name, name))
            return n;
    }
    return NULL;
}

/* record a declared name; text is NULL unless it is a LITERALLY */
static void declare_name(const char *name, const char *text, Loc loc)
{
    LitName *old = lit_find_local(lit_scope, name);

    if (old) {
        if (old->text || text)
            error_at(loc, "%s is already declared at line %d", name, old->line);
        return;                 /* ordinary duplicates are sema's business */
    }
    LitName *n = xcalloc(1, sizeof *n);
    n->name = xstrdup(name);
    n->text = text ? xstrdup(text) : NULL;
    n->line = loc.line;
    vec_push(&lit_scope->names, n);
}

/* body of the literal `name` visible here, or NULL */
static const char *literal_text(const char *name)
{
    for (const LitScope *sc = lit_scope; sc; sc = sc->up) {
        const LitName *n = lit_find_local(sc, name);
        if (n)
            return n->text;
    }
    return NULL;
}

/* ------------------------------------------------------------ tokens */

/* Token buffer with lookahead.  Tokens are expanded lazily: peek_at(k)
   replaces any LITERALLY name at positions 0..k by the tokens of its body
   (which may expand in turn).  Tokens marked raw -- names being defined --
   are never expanded. */
static Token *buf;
static int nbuf, cap;

#define MAX_LITERAL_DEPTH 32

static void fill(int k)
{
    while (nbuf <= k) {
        if (nbuf == cap)
            buf = xrealloc(buf, (cap = cap ? 2 * cap : 16) * sizeof *buf);
        buf[nbuf++] = lex_next();
    }
}

static void expand_at(int i)
{
    for (;;) {
        fill(i);
        Token *t = &buf[i];
        const char *text;
        if (t->type != T_IDENT || t->raw || !(text = literal_text(t->text)))
            return;
        if (t->depth >= MAX_LITERAL_DEPTH)
            error_at(t->loc, "literal %s expands recursively (more than %d levels)",
                     t->text, MAX_LITERAL_DEPTH);
        int depth = t->depth + 1;
        Vec v = lex_tokens(text, t->loc);
        while (nbuf + v.len >= cap)
            buf = xrealloc(buf, (cap *= 2) * sizeof *buf);
        memmove(buf + i + v.len, buf + i + 1, (nbuf - i - 1) * sizeof *buf);
        for (int k = 0; k < v.len; k++) {
            buf[i + k] = *(Token *)v.items[k];
            buf[i + k].depth = depth;
        }
        nbuf += v.len - 1;
    }
}

static Token *peek_at(int k)
{
    for (int i = 0; i <= k; i++)
        expand_at(i);
    return &buf[k];
}

static Token *peek(void)
{
    return peek_at(0);
}

static TokType peek_type(int k)
{
    return peek_at(k)->type;
}

/* type of the token at k without expanding it (or anything before it) */
static TokType raw_type(int k)
{
    fill(k);
    return buf[k].type;
}

static Token next(void)
{
    Token t = *peek();
    memmove(buf, buf + 1, (nbuf - 1) * sizeof *buf);
    nbuf--;
    return t;
}

static int accept(TokType t)
{
    if (peek()->type != t)
        return 0;
    next();
    return 1;
}

static _Noreturn void unexpected(const char *what)
{
    Token *t = peek();
    if (t->type == T_IDENT)
        error_at(t->loc, "expected %s, found '%s'", what, t->text);
    error_at(t->loc, "expected %s, found %s", what, tok_name(t->type));
}

static Token expect(TokType t)
{
    if (peek()->type != t)
        unexpected(tok_name(t));
    return next();
}

/* a name being defined (declared variable, procedure, parameter, label,
   END label): taken as written, never expanded as a literal */
static Token next_raw_ident(void)
{
    fill(0);
    if (buf[0].type != T_IDENT)
        unexpected("identifier");
    buf[0].raw = 1;
    return next();
}

static _Noreturn void unsupported(Loc loc, const char *what)
{
    error_at(loc, "%s is not supported", what);
}

/* ------------------------------------------------------------ expressions */

static Expr *parse_expr(void);

static Expr *binary(Op op, Loc loc, Expr *lhs, Expr *rhs)
{
    Expr *e = new_expr(E_BINARY, loc);
    e->op = op;
    e->lhs = lhs;
    e->rhs = rhs;
    return e;
}

static Expr *unary(Op op, Loc loc, Expr *operand)
{
    Expr *e = new_expr(E_UNARY, loc);
    e->op = op;
    e->lhs = operand;
    return e;
}

/* e is NAME, NAME(args) or NAME(subscript); a following .MEMBER[(j)]
   makes it a structure member reference */
static Expr *member_suffix(Expr *e)
{
    if (peek_type(0) != T_DOT || raw_type(1) != T_IDENT)
        return e;
    next();                                 /* the '.' */
    Token m = next_raw_ident();
    Expr *r = new_expr(E_MEMBER, e->loc);
    r->name = e->name;
    r->mname = m.text;
    if (e->kind == E_CALL) {
        if (e->args.len != 1)
            error_at(e->loc, "an array takes exactly one subscript");
        r->lhs = e->args.items[0];
    } else if (e->kind == E_INDEX) {
        r->lhs = e->lhs;
    }
    if (accept(T_LPAREN)) {
        r->rhs = parse_expr();
        if (peek_type(0) == T_COMMA)
            error_at(peek()->loc, "an array takes exactly one subscript");
        expect(T_RPAREN);
    }
    return r;
}

static Expr *parse_primary(void)
{
    Token *t = peek();

    switch (t->type) {
    case T_NUMBER: {
        Token n = next();
        Expr *e = new_expr(E_NUM, n.loc);
        e->value = n.value;
        e->is_address = n.is_address;
        return e;
    }
    case T_STRING: {
        /* one or two characters are a BYTE / ADDRESS constant */
        if (strlen(t->text) > 2)
            error_at(t->loc, "strings longer than 2 characters are not supported");
        Token str = next();
        Expr *e = new_expr(E_NUM, str.loc);
        e->value = str.value;
        e->is_address = str.is_address;
        return e;
    }
    case T_IDENT: {
        Token id = next();
        if (!accept(T_LPAREN)) {
            Expr *e = new_expr(E_NAME, id.loc);
            e->name = id.text;
            return member_suffix(e);
        }
        Expr *e = new_expr(E_CALL, id.loc);
        e->name = id.text;
        do
            vec_push(&e->args, parse_expr());
        while (accept(T_COMMA));
        expect(T_RPAREN);
        return member_suffix(e);
    }
    case T_LPAREN: {
        next();
        Expr *e = parse_expr();
        expect(T_RPAREN);
        return e;
    }
    case T_DOT: {
        /* .name, .name(subscript), or .(constant list) */
        Expr *e = new_expr(E_ADDR, next().loc);
        if (accept(T_LPAREN)) {
            size_t cap = 16;
            e->data = xmalloc(cap);
            do {
                Token *c = peek();
                if (c->type == T_NUMBER) {
                    if (c->value > 0xff)
                        error_at(c->loc, "a number in a constant list must fit in a byte");
                } else if (c->type != T_STRING) {
                    unexpected("a number or a string");
                }
                Token tok = next();
                const char *bytes = tok.type == T_STRING ? tok.text : NULL;
                size_t n = bytes ? strlen(bytes) : 1;
                while (e->ndata + n > cap)
                    e->data = xrealloc(e->data, cap *= 2);
                if (bytes)
                    memcpy(e->data + e->ndata, bytes, n);
                else
                    e->data[e->ndata] = (unsigned char)tok.value;
                e->ndata += (int)n;
            } while (accept(T_COMMA));
            expect(T_RPAREN);
            return e;
        }
        Token id = expect(T_IDENT);
        Expr *v = new_expr(E_NAME, id.loc);
        v->name = id.text;
        if (accept(T_LPAREN)) {
            v->kind = E_CALL;
            do
                vec_push(&v->args, parse_expr());
            while (accept(T_COMMA));
            expect(T_RPAREN);
        }
        e->lhs = member_suffix(v);
        return e;
    }
    case K_PLUS:
    case K_MINUS:
        unsupported(t->loc, "PLUS/MINUS (add/subtract with carry)");
    default:
        unexpected("an expression");
    }
}

static Expr *parse_unary(void)
{
    Loc loc = peek()->loc;

    if (accept(T_MINUS))
        return unary(OP_NEG, loc, parse_unary());
    if (accept(T_PLUS))
        return parse_unary();
    return parse_primary();
}

static Expr *parse_mul(void)
{
    Expr *e = parse_unary();

    for (;;) {
        Loc loc = peek()->loc;
        Op op;
        switch (peek()->type) {
        case T_STAR: op = OP_MUL; break;
        case T_SLASH: op = OP_DIV; break;
        case K_MOD: op = OP_MOD; break;
        default: return e;
        }
        next();
        e = binary(op, loc, e, parse_unary());
    }
}

static Expr *parse_add(void)
{
    Expr *e = parse_mul();

    for (;;) {
        Loc loc = peek()->loc;
        Op op;
        switch (peek()->type) {
        case T_PLUS: op = OP_ADD; break;
        case T_MINUS: op = OP_SUB; break;
        case K_PLUS:
        case K_MINUS:
            unsupported(loc, "PLUS/MINUS (add/subtract with carry)");
        default: return e;
        }
        next();
        e = binary(op, loc, e, parse_mul());
    }
}

static Expr *parse_rel(void)
{
    Expr *e = parse_add();

    for (;;) {
        Loc loc = peek()->loc;
        Op op;
        switch (peek()->type) {
        case T_EQ: op = OP_EQ; break;
        case T_NE: op = OP_NE; break;
        case T_LT: op = OP_LT; break;
        case T_GT: op = OP_GT; break;
        case T_LE: op = OP_LE; break;
        case T_GE: op = OP_GE; break;
        default: return e;
        }
        next();
        e = binary(op, loc, e, parse_add());
    }
}

static Expr *parse_not(void)
{
    Loc loc = peek()->loc;

    if (accept(K_NOT))
        return unary(OP_NOT, loc, parse_not());
    return parse_rel();
}

static Expr *parse_and(void)
{
    Expr *e = parse_not();

    while (peek_type(0) == K_AND) {
        Loc loc = next().loc;
        e = binary(OP_AND, loc, e, parse_not());
    }
    return e;
}

static Expr *parse_expr(void)
{
    Expr *e = parse_and();

    for (;;) {
        Loc loc = peek()->loc;
        if (accept(K_OR))
            e = binary(OP_OR, loc, e, parse_and());
        else if (accept(K_XOR))
            e = binary(OP_XOR, loc, e, parse_and());
        else
            return e;
    }
}

/* ---------------------------------------------------------- declarations */

static Block *parse_block(int allow_decls);

/* STRUCTURE ( member {, member} ),  member := IDENT [(NUMBER)] BYTE|ADDRESS
   (PL/M-80 manual 6.2.7: no factoring, no BASED members, no nesting).
   Member names are read raw: they are not names in any block scope. */
static StructType *parse_members(void)
{
    StructType *st = xcalloc(1, sizeof *st);

    expect(T_LPAREN);
    do {
        if (raw_type(0) == T_LPAREN)
            error_at(peek()->loc, "structure members cannot be factored");
        Token id = next_raw_ident();
        for (int i = 0; i < st->members.len; i++)
            if (!strcmp(((Member *)st->members.items[i])->name, id.text))
                error_at(id.loc, "member %s is declared twice", id.text);
        Member *m = xcalloc(1, sizeof *m);
        m->name = id.text;
        m->loc = id.loc;
        if (peek_type(0) == K_BASED)
            error_at(peek()->loc, "a structure member cannot be BASED");
        if (accept(T_LPAREN)) {
            Loc dl = peek()->loc;
            if (peek_type(0) != T_NUMBER)
                unexpected("an array dimension (a number)");
            m->dim = next().value;
            if (m->dim == 0)
                error_at(dl, "array dimension must be from 1 to 65535");
            expect(T_RPAREN);
        }
        if (peek_type(0) == K_STRUCTURE)
            error_at(peek()->loc, "structures cannot be nested: a member must be BYTE or ADDRESS");
        if (accept(K_BYTE))
            m->type = TY_BYTE;
        else if (accept(K_ADDRESS))
            m->type = TY_ADDRESS;
        else
            unexpected("BYTE or ADDRESS");
        vec_push(&st->members, m);
    } while (accept(T_COMMA));
    expect(T_RPAREN);
    return st;
}

/* BYTE | ADDRESS | STRUCTURE (...); *st is set for a structure */
static Type parse_type(StructType **st)
{
    Token *t = peek();

    *st = NULL;
    if (accept(K_BYTE))
        return TY_BYTE;
    if (accept(K_ADDRESS))
        return TY_ADDRESS;
    if (accept(K_STRUCTURE)) {
        *st = parse_members();
        return TY_STRUCT;
    }
    switch (t->type) {
    case K_LABEL: unsupported(t->loc, "LABEL declarations");
    case K_DATA:
    case K_INITIAL: unsupported(t->loc, "initialized data");
    default: unexpected("BYTE, ADDRESS or STRUCTURE");
    }
}

/* a declared name, read raw, and an optional "BASED base" right after it */
static void decl_name(Vec *names, Vec *bases)
{
    Token *copy = xmalloc(sizeof *copy);
    Token *base = NULL;

    *copy = next_raw_ident();
    vec_push(names, copy);
    if (accept(K_BASED)) {
        base = xmalloc(sizeof *base);
        *base = expect(T_IDENT);
    }
    vec_push(bases, base);
}

/* DECLARE item {, item} ;
   item := (name | '(' name {, name} ')') ['(' NUMBER ')'] type
   name := IDENT [BASED IDENT] */
static void parse_declare(Block *b)
{
    expect(K_DECLARE);
    do {
        Vec names = { 0 }, bases = { 0 };
        /* names are read raw: declaring a name hides an outer literal */
        if (raw_type(0) == T_LPAREN) {
            next();
            do
                decl_name(&names, &bases);
            while (accept(T_COMMA));
            expect(T_RPAREN);
        } else {
            decl_name(&names, &bases);
        }
        Token *first = names.items[0];
        unsigned dim = 0;
        Loc dim_loc = peek()->loc;
        if (accept(T_LPAREN)) {
            if (peek_type(0) == T_STAR)
                unsupported(peek()->loc, "an implicit dimension '(*)'");
            dim_loc = peek()->loc;
            if (peek_type(0) != T_NUMBER)
                unexpected("an array dimension (a number)");
            dim = next().value;
            if (dim == 0)
                error_at(dim_loc, "array dimension must be from 1 to 65535");
            expect(T_RPAREN);
        }
        if (peek_type(0) == K_LITERALLY) {
            Loc lit_loc = next().loc;
            if (names.len > 1)
                error_at(lit_loc, "a LITERALLY declaration cannot be factored");
            if (dim)
                error_at(dim_loc, "a LITERALLY declaration cannot have a dimension");
            if (bases.items[0])
                error_at(lit_loc, "a LITERALLY declaration cannot be BASED");
            if (peek_type(0) != T_STRING)
                unexpected("a string");
            declare_name(first->text, next().text, first->loc);
            continue;
        }
        StructType *st;
        Type type = parse_type(&st);
        for (int i = 0; i < names.len; i++) {
            Token *id = names.items[i];
            VarDecl *v = xcalloc(1, sizeof *v);
            Decl *d = xcalloc(1, sizeof *d);
            declare_name(id->text, NULL, id->loc);
            v->name = id->text;
            v->loc = id->loc;
            v->type = type;
            v->st = st;                 /* factored names share the layout */
            v->dim = dim;
            v->dim_loc = dim_loc;
            if (bases.items[i]) {
                Token *base = bases.items[i];
                v->base = base->text;
                v->base_loc = base->loc;
            }
            d->kind = D_VAR;
            d->var = v;
            vec_push(&b->decls, d);
        }
    } while (accept(T_COMMA));
    expect(T_SEMI);
}

/* END [name] ;  -- name must match `label` if given */
static void parse_end(const char *label, const char *what)
{
    expect(K_END);
    if (raw_type(0) == T_IDENT) {
        Token id = next_raw_ident();
        if (!label)
            error_at(id.loc, "END %s does not match an unlabeled %s", id.text, what);
        if (strcmp(id.text, label))
            error_at(id.loc, "END %s does not match %s %s", id.text, what, label);
    }
    expect(T_SEMI);
}

/* END [name] ; for a DO block -- name must be one of its labels */
static void parse_end_labels(const Vec *labels)
{
    expect(K_END);
    if (raw_type(0) == T_IDENT) {
        Token id = next_raw_ident();
        if (!labels->len)
            error_at(id.loc, "END %s does not match an unlabeled DO block", id.text);
        int found = 0;
        for (int i = 0; i < labels->len; i++)
            found |= !strcmp(((Expr *)labels->items[i])->name, id.text);
        if (!found)
            error_at(id.loc, "END %s does not match DO block %s", id.text,
                     ((Expr *)labels->items[labels->len - 1])->name);
    }
    expect(T_SEMI);
}

/* name: PROCEDURE [(params)] [type] [EXTERNAL] ; block END [name] ; */
static Proc *parse_procedure(void)
{
    Token name = next_raw_ident();
    Proc *p = xcalloc(1, sizeof *p);

    declare_name(name.text, NULL, name.loc);
    lit_push();                         /* parameters and the body's names */
    expect(T_COLON);
    expect(K_PROCEDURE);
    p->name = name.text;
    p->loc = name.loc;
    if (accept(T_LPAREN)) {
        do {
            Token id = next_raw_ident();
            VarDecl *v = xcalloc(1, sizeof *v);
            declare_name(id.text, NULL, id.loc);
            v->name = id.text;
            v->loc = id.loc;
            vec_push(&p->params, v);
        } while (accept(T_COMMA));
        expect(T_RPAREN);
    }
    if (accept(K_BYTE))
        p->ret = TY_BYTE;
    else if (accept(K_ADDRESS))
        p->ret = TY_ADDRESS;
    for (;;) {
        Token *t = peek();
        if (accept(K_EXTERNAL))
            p->external = 1;
        else if (t->type == K_REENTRANT)
            unsupported(t->loc, "REENTRANT");
        else if (t->type == K_INTERRUPT)
            unsupported(t->loc, "INTERRUPT");
        else if (t->type == K_PUBLIC)
            unsupported(t->loc, "PUBLIC");
        else
            break;
    }
    expect(T_SEMI);
    p->body = parse_block(1);
    lit_pop();
    parse_end(p->name, "PROCEDURE");
    return p;
}

/* ------------------------------------------------------------ statements */

static Stmt *parse_stmt(void);

/* DO ; block END   or   DO WHILE expr ; stmts END  (DO already consumed) */
static Stmt *parse_do(Loc loc, const Vec *labels)
{
    Token *t = peek();
    Stmt *s;

    /* every DO block is a block: it scopes labels (and so literal names) */
    if (accept(K_WHILE)) {
        s = new_stmt(S_WHILE, loc);
        s->expr = parse_expr();
        expect(T_SEMI);
        lit_push();
        s->block = parse_block(0);
        lit_pop();
    } else if (accept(K_CASE)) {
        /* DO CASE expr; one statement per case */
        s = new_stmt(S_CASE, loc);
        s->expr = parse_expr();
        expect(T_SEMI);
        lit_push();
        s->block = parse_block(0);
        lit_pop();
    } else if (t->type == T_IDENT) {
        /* DO var = from TO to [BY by]; */
        Token id = next();
        s = new_stmt(S_ITER, loc);
        s->var = new_expr(E_NAME, id.loc);
        s->var->name = id.text;
        expect(T_EQ);
        s->from = parse_expr();
        expect(K_TO);
        s->to = parse_expr();
        if (accept(K_BY))
            s->by = parse_expr();
        expect(T_SEMI);
        lit_push();
        s->block = parse_block(0);
        lit_pop();
    } else {
        expect(T_SEMI);
        s = new_stmt(S_DO, loc);
        lit_push();
        s->block = parse_block(1);
        lit_pop();
    }
    parse_end_labels(labels);
    return s;
}

/* target := IDENT [ '(' expr ')' ] [ '.' IDENT [ '(' expr ')' ] ] */
static Expr *parse_target(void)
{
    Token id = expect(T_IDENT);
    Expr *e = new_expr(E_NAME, id.loc);

    e->name = id.text;
    if (accept(T_LPAREN)) {
        e->kind = E_INDEX;
        e->lhs = parse_expr();
        if (peek_type(0) == T_COMMA)
            error_at(peek()->loc, "an array takes exactly one subscript");
        expect(T_RPAREN);
    }
    return member_suffix(e);
}

static Stmt *parse_assignment(void)
{
    Stmt *s = new_stmt(S_ASSIGN, peek()->loc);

    do
        vec_push(&s->targets, parse_target());
    while (accept(T_COMMA));
    if (peek_type(0) == T_ASSIGN)
        unsupported(peek()->loc, "embedded assignment ':='");
    expect(T_EQ);
    s->expr = parse_expr();
    expect(T_SEMI);
    return s;
}

static Stmt *parse_unlabeled(const Vec *labels);

/* [label: ...] statement -- labels are read raw (they are definitions) */
static Stmt *parse_stmt(void)
{
    Vec labels = { 0 };

    while (raw_type(0) == T_IDENT && raw_type(1) == T_COLON) {
        Token l = next_raw_ident();
        Expr *e = new_expr(E_NAME, l.loc);
        next();
        e->name = l.text;
        declare_name(l.text, NULL, l.loc);
        vec_push(&labels, e);
    }
    if (labels.len) {
        Loc at = ((Expr *)labels.items[0])->loc;
        if (peek_type(0) == K_PROCEDURE || peek_type(0) == K_DECLARE)
            error_at(at, "declarations must come before statements");
        if (peek_type(0) == K_END || peek_type(0) == T_EOF)
            error_at(peek()->loc, "a label must be followed by a statement (use 'L: ;')");
    }
    Stmt *s = parse_unlabeled(&labels);
    s->labels = labels;
    return s;
}

static Stmt *parse_unlabeled(const Vec *labels)
{
    Token *t = peek();
    Loc loc = t->loc;
    Stmt *s;

    switch (t->type) {
    case T_IDENT:
        return parse_assignment();
    case K_CALL: {
        next();
        Token id = expect(T_IDENT);
        Expr *e = new_expr(E_NAME, id.loc);
        e->name = id.text;
        if (accept(T_LPAREN)) {
            e->kind = E_CALL;
            do
                vec_push(&e->args, parse_expr());
            while (accept(T_COMMA));
            expect(T_RPAREN);
        }
        expect(T_SEMI);
        s = new_stmt(S_CALL, loc);
        s->expr = e;
        return s;
    }
    case K_IF:
        next();
        s = new_stmt(S_IF, loc);
        s->expr = parse_expr();
        expect(K_THEN);
        s->then_s = parse_stmt();
        if (accept(K_ELSE))
            s->else_s = parse_stmt();
        return s;
    case K_DO:
        next();
        return parse_do(loc, labels);
    case K_RETURN:
        next();
        s = new_stmt(S_RETURN, loc);
        if (!accept(T_SEMI)) {
            s->expr = parse_expr();
            expect(T_SEMI);
        }
        return s;
    case K_HALT:
        next();
        expect(T_SEMI);
        return new_stmt(S_HALT, loc);
    case T_SEMI:
        next();
        return new_stmt(S_NULL, loc);
    case K_DECLARE:
        error_at(loc, "declarations must come before statements");
    case K_GO:                              /* GO TO label; */
        next();
        expect(K_TO);
        goto label_ref;
    case K_GOTO: {                          /* GOTO label; */
        next();
    label_ref:;
        Token id = expect(T_IDENT);
        s = new_stmt(S_GOTO, loc);
        s->expr = new_expr(E_NAME, id.loc);
        s->expr->name = id.text;
        expect(T_SEMI);
        return s;
    }
    case K_ENABLE:
    case K_DISABLE:
        unsupported(loc, "ENABLE/DISABLE");
    default:
        unexpected("a statement");
    }
}

/* NAME: PROCEDURE -- the name is raw, PROCEDURE may come from a literal */
static int at_procedure(void)
{
    if (raw_type(0) != T_IDENT || raw_type(1) != T_COLON)
        return 0;
    buf[0].raw = 1;
    return peek_type(2) == K_PROCEDURE;
}

static Block *parse_block(int allow_decls)
{
    Block *b = new_block();

    for (;;) {
        Token *t = peek();
        if (t->type == K_DECLARE || at_procedure()) {
            if (!allow_decls)
                error_at(t->loc, "declarations are only allowed in procedures and simple DO blocks");
            if (t->type == K_DECLARE) {
                parse_declare(b);
            } else {
                Decl *d = xcalloc(1, sizeof *d);
                d->kind = D_PROC;
                d->proc = parse_procedure();
                vec_push(&b->decls, d);
            }
            continue;
        }
        break;
    }
    while (peek_type(0) != K_END) {
        if (peek_type(0) == T_EOF)
            unexpected("END");
        vec_push(&b->stmts, parse_stmt());
    }
    return b;
}

Program *parse_program(const char *text)
{
    Program *prog = xcalloc(1, sizeof *prog);

    lex_init(text);
    nbuf = 0;
    lit_scope = NULL;
    lit_push();
    if (raw_type(0) != T_IDENT || raw_type(1) != T_COLON)
        unexpected("a program of the form 'name: DO; ... END name;'");
    buf[0].raw = 1;
    if (peek_type(2) != K_DO)
        unexpected("a program of the form 'name: DO; ... END name;'");
    Token name = next();
    next();
    next();
    expect(T_SEMI);
    prog->name = name.text;
    prog->loc = name.loc;
    prog->body = parse_block(1);
    lit_pop();
    parse_end(prog->name, "program");
    accept(K_EOF);
    if (peek_type(0) != T_EOF)
        unexpected("end of file after the program");
    return prog;
}
