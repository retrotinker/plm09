/* plmc code generator: annotated AST -> lwasm 6809 assembly.
 *
 * Expression results: BYTE in B (A undefined), ADDRESS in D.
 * Binary operators evaluate the right operand first and push it, unless it
 * is a constant or a variable of the operation's width, in which case it
 * is used directly as the instruction operand.
 * Procedures use static parameter/local slots; nothing is kept in
 * registers across a call.  See docs/DESIGN.md.
 */
#include "codegen.h"
#include "lexer.h"
#include "sema.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *out;
static int label_no;
static int last_comment_line;

static int new_label(void)
{
    return ++label_no;
}

/* emit one instruction: "\tMNEMONIC\toperand" */
static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...)
{
    va_list ap;
    fputc('\t', out);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
}

static void emit_label(int n)
{
    fprintf(out, "L%d\n", n);
}

/* source line as a comment, once per line */
static void emit_source(Loc loc)
{
    const char *text = src_line(loc.line);

    if (!text || loc.line == last_comment_line)
        return;
    last_comment_line = loc.line;
    while (*text == ' ' || *text == '\t')
        text++;
    fprintf(out, "; %d: %s\n", loc.line, text);
}

static int width(Type t)
{
    return t == TY_BYTE ? 8 : 16;
}

/* ------------------------------------------------------------- operands */

/* the right-hand operand of an accumulator instruction */
typedef enum { OPND_STACK, OPND_MEM, OPND_IMM } OpndKind;

typedef struct Opnd {
    OpndKind kind;
    const char *name;   /* OPND_MEM */
    unsigned value;     /* OPND_IMM */
} Opnd;

enum { WHOLE, HIGH, LOW };

/* operand text; part selects the high or low byte of a 16-bit operand */
static const char *opnd_text(const Opnd *o, int w, int part)
{
    static char buf[80];

    switch (o->kind) {
    case OPND_STACK:
        return w == 16 && part == WHOLE ? ",S++" : ",S+";
    case OPND_MEM:
        snprintf(buf, sizeof buf, "%s%s", o->name, part == LOW ? "+1" : "");
        return buf;
    default:
        if (w == 16 && part == WHOLE)
            snprintf(buf, sizeof buf, "#$%04X", o->value & 0xffff);
        else
            snprintf(buf, sizeof buf, "#$%02X",
                     (part == HIGH ? o->value >> 8 : o->value) & 0xff);
        return buf;
    }
}

static int is_var(const Expr *e)
{
    return e->kind == E_NAME && e->sym->kind == SYM_VAR;
}

/* can e be used directly as a w-bit instruction operand? */
static int simple_operand(const Expr *e, int w, Opnd *o)
{
    if (e->kind == E_NUM) {
        o->kind = OPND_IMM;
        o->value = e->value;
        return 1;
    }
    if (is_var(e) && width(e->type) == w) {
        o->kind = OPND_MEM;
        o->name = e->sym->asm_name;
        return 1;
    }
    return 0;
}

static int contains_call(const Expr *e)
{
    switch (e->kind) {
    case E_NUM:
        return 0;
    case E_NAME:
        return e->sym->kind == SYM_PROC;
    case E_CALL:
        return 1;
    case E_UNARY:
        return contains_call(e->lhs);
    case E_BINARY:
        return contains_call(e->lhs) || contains_call(e->rhs);
    }
    return 1;
}

/* ---------------------------------------------------------- expressions */

static void gen_expr(const Expr *e);

/* evaluate e and widen it to w bits */
static void gen_as(const Expr *e, int w)
{
    gen_expr(e);
    if (w == 16 && e->type == TY_BYTE)
        emit("CLRA");
}

static void push(int w)
{
    emit(w == 8 ? "PSHS\tB" : "PSHS\tD");
}

static void pull(int w)
{
    emit(w == 8 ? "PULS\tB" : "PULS\tD");
}

/* evaluate lhs into the accumulator and describe rhs as an operand,
   both at width w */
static void gen_operands(const Expr *lhs, const Expr *rhs, int w, Opnd *o)
{
    if (simple_operand(rhs, w, o)) {
        gen_as(lhs, w);
        return;
    }
    gen_as(rhs, w);
    push(w);
    gen_as(lhs, w);
    o->kind = OPND_STACK;
}

/* lhs into X and rhs into D, both 16-bit, for RT_MUL16 / RT_DIV16 */
static void gen_x_d(const Expr *lhs, const Expr *rhs)
{
    Opnd o;

    if (simple_operand(rhs, 16, &o)) {
        gen_as(lhs, 16);
        emit("TFR\tD,X");
        emit("LDD\t%s", opnd_text(&o, 16, WHOLE));
        return;
    }
    gen_as(rhs, 16);
    push(16);
    gen_as(lhs, 16);
    emit("TFR\tD,X");
    pull(16);
}

static const char *branch_for(Op op)
{
    switch (op) {
    case OP_EQ: return "BEQ";
    case OP_NE: return "BNE";
    case OP_LT: return "BLO";
    case OP_GT: return "BHI";
    case OP_LE: return "BLS";
    default: return "BHS";     /* OP_GE */
    }
}

static Op inverse(Op op)
{
    switch (op) {
    case OP_EQ: return OP_NE;
    case OP_NE: return OP_EQ;
    case OP_LT: return OP_GE;
    case OP_GT: return OP_LE;
    case OP_LE: return OP_GT;
    default: return OP_LT;      /* OP_GE */
    }
}

/* compare lhs with rhs, setting CC for an unsigned branch */
static void gen_compare(const Expr *e)
{
    int w = e->lhs->type == TY_BYTE && e->rhs->type == TY_BYTE ? 8 : 16;
    Opnd o;

    gen_operands(e->lhs, e->rhs, w, &o);
    emit("%s\t%s", w == 8 ? "CMPB" : "CMPD", opnd_text(&o, w, WHOLE));
}

static void gen_call(const Expr *e);

static void gen_binary(const Expr *e)
{
    int w = width(e->type);
    Opnd o;

    if (op_is_relational(e->op)) {
        int t = new_label(), end = new_label();
        gen_compare(e);
        emit("%s\tL%d", branch_for(e->op), t);
        emit("CLRB");
        emit("BRA\tL%d", end);
        emit_label(t);
        emit("LDB\t#$FF");
        emit_label(end);
        return;
    }

    switch (e->op) {
    case OP_MUL:
        if (e->lhs->type == TY_BYTE && e->rhs->type == TY_BYTE) {
            if (simple_operand(e->rhs, 8, &o)) {
                gen_expr(e->lhs);
                emit("TFR\tB,A");
                emit("LDB\t%s", opnd_text(&o, 8, WHOLE));
            } else {
                gen_expr(e->rhs);
                push(8);
                gen_expr(e->lhs);
                emit("TFR\tB,A");
                pull(8);
            }
            emit("MUL");
            return;
        }
        gen_x_d(e->lhs, e->rhs);
        emit("JSR\tRT_MUL16");
        return;
    case OP_DIV:
    case OP_MOD:
        gen_x_d(e->lhs, e->rhs);
        emit("JSR\tRT_DIV16");
        if (e->op == OP_MOD)
            emit("TFR\tX,D");
        return;
    default:
        break;
    }

    gen_operands(e->lhs, e->rhs, w, &o);
    switch (e->op) {
    case OP_ADD:
        emit("%s\t%s", w == 8 ? "ADDB" : "ADDD", opnd_text(&o, w, WHOLE));
        break;
    case OP_SUB:
        emit("%s\t%s", w == 8 ? "SUBB" : "SUBD", opnd_text(&o, w, WHOLE));
        break;
    default: {
        const char *mn = e->op == OP_AND ? "AND" : e->op == OP_OR ? "OR" : "EOR";
        if (w == 16)
            emit("%sA\t%s", mn, opnd_text(&o, w, HIGH));
        emit("%sB\t%s", mn, opnd_text(&o, w, w == 16 ? LOW : WHOLE));
        break;
    }
    }
}

static void gen_expr(const Expr *e)
{
    switch (e->kind) {
    case E_NUM:
        if (e->type == TY_BYTE)
            emit("LDB\t#$%02X", e->value);
        else
            emit("LDD\t#$%04X", e->value);
        break;
    case E_NAME:
        if (e->sym->kind == SYM_PROC) {
            gen_call(e);
            break;
        }
        emit("%s\t%s", e->type == TY_BYTE ? "LDB" : "LDD", e->sym->asm_name);
        break;
    case E_CALL:
        gen_call(e);
        break;
    case E_UNARY:
        gen_expr(e->lhs);
        if (e->op == OP_NEG) {
            if (e->type == TY_BYTE) {
                emit("NEGB");
            } else {
                emit("NEGA");
                emit("NEGB");
                emit("SBCA\t#0");
            }
        } else {
            if (e->type != TY_BYTE)
                emit("COMA");
            emit("COMB");
        }
        break;
    case E_BINARY:
        gen_binary(e);
        break;
    }
}

/* call of a procedure (E_NAME or E_CALL); result, if any, in B or D */
static void gen_call(const Expr *e)
{
    const Proc *p = e->sym->proc;
    int n = e->args.len;

    if (p->external) {
        /* hook ABI: every argument pushed as a word, last one also in D */
        for (int i = 0; i < n; i++) {
            gen_as(e->args.items[i], 16);
            push(16);
        }
        emit("JSR\t%s", p->sym->asm_name);
        if (n)
            emit("LEAS\t%d,S", 2 * n);
        return;
    }

    int nested = 0;
    for (int i = 0; i < n; i++)
        nested |= contains_call(e->args.items[i]);
    if (!nested) {
        /* no argument can disturb the parameter slots: store directly */
        for (int i = 0; i < n; i++) {
            const Symbol *param = ((VarDecl *)p->params.items[i])->sym;
            gen_as(e->args.items[i], width(param->type));
            emit("%s\t%s", param->type == TY_BYTE ? "STB" : "STD", param->asm_name);
        }
    } else {
        /* evaluate everything first: an argument may call this procedure */
        for (int i = 0; i < n; i++) {
            const Symbol *param = ((VarDecl *)p->params.items[i])->sym;
            gen_as(e->args.items[i], width(param->type));
            push(width(param->type));
        }
        for (int i = n - 1; i >= 0; i--) {
            const Symbol *param = ((VarDecl *)p->params.items[i])->sym;
            pull(width(param->type));
            emit("%s\t%s", param->type == TY_BYTE ? "STB" : "STD", param->asm_name);
        }
    }
    emit("JSR\t%s", p->sym->asm_name);
}

/* ------------------------------------------------------------ statements */

/* branch to label `false_label` unless e is true (bit 0 set) */
static void gen_cond_false(const Expr *e, int false_label)
{
    if (e->kind == E_BINARY && op_is_relational(e->op)) {
        gen_compare(e);
        emit("L%s\tL%d", branch_for(inverse(e->op)), false_label);
        return;
    }
    gen_expr(e);
    emit("LSRB");
    emit("LBCC\tL%d", false_label);
}

static void gen_block(const Block *b);

static void gen_stmt(const Stmt *s)
{
    if (s->kind != S_DO)
        emit_source(s->loc);

    switch (s->kind) {
    case S_ASSIGN: {
        int widen = 0;
        gen_expr(s->expr);
        for (int i = 0; i < s->targets.len; i++)
            widen |= ((Expr *)s->targets.items[i])->type == TY_ADDRESS;
        if (widen && s->expr->type == TY_BYTE)
            emit("CLRA");
        for (int i = 0; i < s->targets.len; i++) {
            const Expr *t = s->targets.items[i];
            emit("%s\t%s", t->type == TY_BYTE ? "STB" : "STD", t->sym->asm_name);
        }
        break;
    }
    case S_CALL:
        gen_call(s->expr);
        break;
    case S_IF: {
        int else_label = new_label();
        gen_cond_false(s->expr, else_label);
        gen_stmt(s->then_s);
        if (s->else_s) {
            int end = new_label();
            emit("LBRA\tL%d", end);
            emit_label(else_label);
            gen_stmt(s->else_s);
            emit_label(end);
        } else {
            emit_label(else_label);
        }
        break;
    }
    case S_DO:
        gen_block(s->block);
        break;
    case S_WHILE: {
        int top = new_label(), end = new_label();
        emit_label(top);
        gen_cond_false(s->expr, end);
        gen_block(s->block);
        emit("LBRA\tL%d", top);
        emit_label(end);
        break;
    }
    case S_RETURN:
        if (s->expr)
            gen_as(s->expr, width(s->proc->ret));
        emit("RTS");
        break;
    case S_HALT:
        emit("JMP\tPLM_HALT");
        break;
    case S_NULL:
        break;
    }
}

static void gen_block(const Block *b)
{
    for (int i = 0; i < b->stmts.len; i++)
        gen_stmt(b->stmts.items[i]);
}

void codegen(FILE *f, const Program *prog, const CodegenOptions *opt)
{
    out = f;
    label_no = 0;
    last_comment_line = 0;

    fprintf(out, "; generated by plmc from %s\n", src_name);
    emit("PRAGMA\t6809");
    emit("ORG\t$%04X", opt->org);
    emit("INCLUDE\t\"plm_crt0.asm\"");

    fprintf(out, "\n; program %s\nPLM_MAIN\n", prog->name);
    gen_block(prog->body);
    emit("RTS");

    for (int i = 0; i < sema_procs.len; i++) {
        const Proc *p = sema_procs.items[i];
        if (p->external)
            continue;
        fprintf(out, "\n; procedure %s\n%s\n", p->name, p->sym->asm_name);
        gen_block(p->body);
        emit("RTS");
    }

    fputc('\n', out);
    emit("INCLUDE\t\"plm_rt.asm\"");
    emit("INCLUDE\t\"plm_hooks.asm\"");

    fprintf(out, "\n; variables\n");
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        fprintf(out, "%s\tRMB\t%d\n", v->asm_name, v->type == TY_BYTE ? 1 : 2);
    }

    if (opt->vectors) {
        emit("ORG\t$FFFE");
        emit("FDB\tRT_START");
    }
    emit("END\tRT_START");
}
