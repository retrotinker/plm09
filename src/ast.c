/* plmc syntax tree: constructors and --ast dump */
#include "ast.h"

#include <stdio.h>

Expr *new_expr(ExprKind kind, Loc loc)
{
    Expr *e = xcalloc(1, sizeof *e);
    e->kind = kind;
    e->loc = loc;
    return e;
}

Stmt *new_stmt(StmtKind kind, Loc loc)
{
    Stmt *s = xcalloc(1, sizeof *s);
    s->kind = kind;
    s->loc = loc;
    return s;
}

Block *new_block(void)
{
    return xcalloc(1, sizeof(Block));
}

const char *op_name(Op op)
{
    static const char *const names[] = {
        [OP_ADD] = "+", [OP_SUB] = "-", [OP_MUL] = "*", [OP_DIV] = "/",
        [OP_MOD] = "MOD", [OP_AND] = "AND", [OP_OR] = "OR", [OP_XOR] = "XOR",
        [OP_EQ] = "=", [OP_NE] = "<>", [OP_LT] = "<", [OP_GT] = ">",
        [OP_LE] = "<=", [OP_GE] = ">=", [OP_NEG] = "NEG", [OP_NOT] = "NOT",
    };
    return names[op];
}

const char *type_name(Type t)
{
    return t == TY_BYTE ? "BYTE" : t == TY_ADDRESS ? "ADDRESS" : "untyped";
}

int op_is_relational(Op op)
{
    return op >= OP_EQ && op <= OP_GE;
}

static void indent(int depth)
{
    printf("%*s", 2 * depth, "");
}

static void dump_expr(const Expr *e)
{
    switch (e->kind) {
    case E_NUM:
        printf("%u", e->value);
        break;
    case E_NAME:
        printf("%s", e->name);
        break;
    case E_CALL:
        printf("(CALL %s", e->name);
        for (int i = 0; i < e->args.len; i++) {
            putchar(' ');
            dump_expr(e->args.items[i]);
        }
        putchar(')');
        break;
    case E_UNARY:
        printf("(%s ", op_name(e->op));
        dump_expr(e->lhs);
        putchar(')');
        break;
    case E_BINARY:
        printf("(%s ", op_name(e->op));
        dump_expr(e->lhs);
        putchar(' ');
        dump_expr(e->rhs);
        putchar(')');
        break;
    case E_INDEX:
        printf("(INDEX %s ", e->name);
        dump_expr(e->lhs);
        putchar(')');
        break;
    case E_ADDR:
        printf("(ADDR ");
        if (e->lhs) {
            dump_expr(e->lhs);
        } else {
            printf("DATA");
            for (int i = 0; i < e->ndata; i++)
                printf(" %u", e->data[i]);
        }
        putchar(')');
        break;
    }
}

static void dump_block(const Block *b, int depth);

static void dump_stmt(const Stmt *s, int depth)
{
    indent(depth);
    for (int i = 0; i < s->labels.len; i++)
        printf("%s: ", ((Expr *)s->labels.items[i])->name);
    switch (s->kind) {
    case S_ASSIGN:
        printf("ASSIGN");
        for (int i = 0; i < s->targets.len; i++) {
            printf("%s ", i ? "," : "");
            dump_expr(s->targets.items[i]);
        }
        printf(" = ");
        dump_expr(s->expr);
        putchar('\n');
        break;
    case S_CALL:
        printf("CALL ");
        dump_expr(s->expr);
        putchar('\n');
        break;
    case S_IF:
        printf("IF ");
        dump_expr(s->expr);
        putchar('\n');
        indent(depth);
        printf("THEN\n");
        dump_stmt(s->then_s, depth + 1);
        if (s->else_s) {
            indent(depth);
            printf("ELSE\n");
            dump_stmt(s->else_s, depth + 1);
        }
        break;
    case S_DO:
        printf("DO\n");
        dump_block(s->block, depth + 1);
        break;
    case S_WHILE:
        printf("DO WHILE ");
        dump_expr(s->expr);
        putchar('\n');
        dump_block(s->block, depth + 1);
        break;
    case S_ITER:
        printf("DO %s = ", s->var->name);
        dump_expr(s->from);
        printf(" TO ");
        dump_expr(s->to);
        if (s->by) {
            printf(" BY ");
            dump_expr(s->by);
        }
        putchar('\n');
        dump_block(s->block, depth + 1);
        break;
    case S_CASE:
        printf("DO CASE ");
        dump_expr(s->expr);
        putchar('\n');
        for (int i = 0; i < s->block->stmts.len; i++) {
            indent(depth + 1);
            printf("CASE %d\n", i);
            dump_stmt(s->block->stmts.items[i], depth + 2);
        }
        break;
    case S_RETURN:
        printf("RETURN");
        if (s->expr) {
            putchar(' ');
            dump_expr(s->expr);
        }
        putchar('\n');
        break;
    case S_HALT:
        printf("HALT\n");
        break;
    case S_NULL:
        printf("NULL\n");
        break;
    case S_GOTO:
        printf("GOTO %s\n", s->expr->name);
        break;
    }
}

static void dump_block(const Block *b, int depth)
{
    for (int i = 0; i < b->decls.len; i++) {
        const Decl *d = b->decls.items[i];
        indent(depth);
        if (d->kind == D_VAR) {
            printf("DECLARE %s", d->var->name);
            if (d->var->base)
                printf(" BASED %s", d->var->base);
            if (d->var->dim)
                printf("(%u)", d->var->dim);
            printf(" %s\n", type_name(d->var->type));
            continue;
        }
        const Proc *p = d->proc;
        printf("PROCEDURE %s", p->name);
        if (p->params.len) {
            printf(" (");
            for (int k = 0; k < p->params.len; k++)
                printf("%s%s", k ? ", " : "", ((VarDecl *)p->params.items[k])->name);
            putchar(')');
        }
        if (p->ret != TY_NONE)
            printf(" %s", type_name(p->ret));
        if (p->external)
            printf(" EXTERNAL");
        putchar('\n');
        dump_block(p->body, depth + 1);
    }
    for (int i = 0; i < b->stmts.len; i++)
        dump_stmt(b->stmts.items[i], depth);
}

void ast_dump(const Program *prog)
{
    printf("PROGRAM %s\n", prog->name);
    dump_block(prog->body, 1);
}
