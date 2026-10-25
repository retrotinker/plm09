/* plmc code generator: annotated AST -> lwasm 6809 assembly.
 *
 * Expression results: BYTE in B (A undefined), ADDRESS in D.
 * Binary operators evaluate the right operand first and push it, unless it
 * is a constant or a variable of the operation's width, in which case it
 * is used directly as the instruction operand.
 * Procedures use static parameter/local slots, except REENTRANT ones, whose
 * frame Y points at; nothing else is kept in registers across a call.
 * See docs/DESIGN.md.
 */
#include "codegen.h"
#include "lexer.h"
#include "sema.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static FILE *out;
static int label_no;
static int pic;             /* --os9: PC-relative code, U-relative data */
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

static const Symbol *find_var(const char *name)
{
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        if (!strcmp(v->asm_name, name))
            return v;
    }
    return NULL;
}

/* how a variable is addressed in an --os9 module: U-relative in the data
   area, PC-relative if it is stored with the code (DATA), or plainly if it
   is at a fixed address (AT n) */
static const char *pic_suffix(const char *name)
{
    const Symbol *v = find_var(name);
    if (v && v->absolute)
        return "";
    return v && v->in_code ? ",PCR" : ",U";
}

static int in_frame(const Symbol *v)
{
    return v && v->storage == ST_FRAME;
}

/* operand naming a variable, or the byte `offset` bytes into it: absolute,
   or relative to U (the data area) in position-independent mode, or to Y
   (the frame) for a REENTRANT procedure's variable */
static const char *var_ref(const char *name, unsigned offset)
{
    static char bufs[4][80];
    static int k;
    char *b = bufs[k++ & 3];
    char off[16] = "";

    if (offset)
        snprintf(off, sizeof off, "+%u", offset & 0xffff);
    const Symbol *v = find_var(name);
    snprintf(b, sizeof bufs[0], "%s%s%s", name, off,
             in_frame(v) ? ",Y" : pic ? pic_suffix(name) : "");
    return b;
}

/* call or jump to a label: absolute JSR/JMP, or LBSR/LBRA when the code
   must be position-independent */
static void emit_call(const char *label)
{
    emit("%s\t%s", pic ? "LBSR" : "JSR", label);
}

static void emit_jump(const char *label)
{
    emit("%s\t%s", pic ? "LBRA" : "JMP", label);
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
    const char *name;   /* OPND_MEM: variable */
    unsigned value;     /* OPND_MEM: byte offset into it; OPND_IMM: value */
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
        return var_ref(o->name, o->value + (part == LOW));
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

static unsigned type_size(Type t)
{
    return t == TY_BYTE ? 1 : 2;
}

/* bytes in one element of a variable: a structure's size, or 1 or 2 */
static unsigned elem_size(const Expr *e)
{
    return e->sym->st ? e->sym->st->size : type_size(e->sym->type);
}

/* a reference to (part of) a variable: E_NAME, E_INDEX or E_MEMBER */
static int is_ref(const Expr *e)
{
    return is_var(e) || e->kind == E_INDEX || e->kind == E_MEMBER;
}

/* byte offset of a reference from the start of its variable, if every
   subscript in it is constant */
static int const_offset(const Expr *e, unsigned *offset)
{
    unsigned long off = 0;

    if (!is_ref(e))
        return 0;
    if (e->kind == E_INDEX) {
        if (e->lhs->kind != E_NUM)
            return 0;
        off = (unsigned long)e->lhs->value * elem_size(e);
    } else if (e->kind == E_MEMBER) {
        if ((e->lhs && e->lhs->kind != E_NUM) || (e->rhs && e->rhs->kind != E_NUM))
            return 0;
        off = e->member->offset;
        if (e->lhs)
            off += (unsigned long)e->lhs->value * e->sym->st->size;
        if (e->rhs)
            off += (unsigned long)e->rhs->value * type_size(e->member->type);
    }
    *offset = (unsigned)(off & 0xffff);
    return 1;
}

/* a (non-BASED) variable, element or member at a constant offset: a fixed
   location, described by the variable and a byte offset */
static int fixed_location(const Expr *e, unsigned *offset)
{
    return !e->sym->base && const_offset(e, offset);
}

/* the same for a BASED variable: reached as `offset,X` once X holds the
   base (gen_base_x) */
static int based_location(const Expr *e, unsigned *offset)
{
    return e->sym->base && const_offset(e, offset);
}

static const char *x_offset(unsigned offset)
{
    static char buf[16];

    if (offset)
        snprintf(buf, sizeof buf, "%u,X", offset & 0xffff);
    else
        strcpy(buf, ",X");
    return buf;
}

/* X = the address of the start of variable sym: the value of its base if
   it is BASED, otherwise its own location.  D is not changed. */
static void gen_base_x(const Symbol *sym)
{
    if (sym->base)
        emit("LDX\t%s", var_ref(sym->base->asm_name, 0));
    else if ((pic && !sym->absolute) || in_frame(sym))
        emit("LEAX\t%s", var_ref(sym->asm_name, 0));
    else
        emit("LDX\t#%s", sym->asm_name);
}

/* can e be used directly as a w-bit instruction operand? */
static int simple_operand(const Expr *e, int w, Opnd *o)
{
    if (e->kind == E_NUM) {
        o->kind = OPND_IMM;
        o->value = e->value;
        return 1;
    }
    if (is_ref(e) && fixed_location(e, &o->value) && width(e->type) == w) {
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
    case E_INDEX:
        return contains_call(e->lhs);
    case E_ADDR:
        return e->lhs && e->lhs->sym->kind == SYM_VAR && contains_call(e->lhs);
    case E_MEMBER:
        return (e->lhs && contains_call(e->lhs)) || (e->rhs && contains_call(e->rhs));
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

/* D = D * n, for an element size n */
static void scale_d(unsigned n)
{
    if (n && !(n & (n - 1))) {              /* a power of two: shift */
        for (; n > 1; n >>= 1) {
            emit("LSLB");
            emit("ROLA");
        }
        return;
    }
    emit("TFR\tD,X");
    emit("LDD\t#%u", n);
    emit_call("RT_MUL16");
}

/* element or member with a computed subscript: leave its byte offset from
   the start of the variable in D, and the variable's address in X */
static void gen_elem_offset(const Expr *e)
{
    if (e->kind == E_INDEX) {
        gen_as(e->lhs, 16);
        scale_d(elem_size(e));
    } else {                                /* E_MEMBER */
        const Member *m = e->member;
        unsigned long c = m->offset;
        int have = 0;
        if (e->lhs) {
            if (e->lhs->kind == E_NUM) {
                c += (unsigned long)e->lhs->value * e->sym->st->size;
            } else {
                gen_as(e->lhs, 16);
                scale_d(e->sym->st->size);
                have = 1;
            }
        }
        if (e->rhs) {
            if (e->rhs->kind == E_NUM) {
                c += (unsigned long)e->rhs->value * type_size(m->type);
            } else {
                if (have)
                    push(16);
                gen_as(e->rhs, 16);
                scale_d(type_size(m->type));
                if (have)
                    emit("ADDD\t,S++");
                have = 1;
            }
        }
        c &= 0xffff;
        if (!have)
            emit("LDD\t#%lu", c);
        else if (c)
            emit("ADDD\t#%lu", c);
    }
    gen_base_x(e->sym);                     /* after any RT_MUL16, which uses X */
}

/* constant lists of .(...) operands, emitted with the code */
typedef struct Pool {
    int label;
    const Expr *e;
} Pool;

static Pool *pools;
static int npools;

/* label of the data for a .(constant list), emitted with the code */
static int pool_label(const Expr *e)
{
    pools = xrealloc(pools, (npools + 1) * sizeof *pools);
    pools[npools].label = new_label();
    pools[npools].e = e;
    return pools[npools++].label;
}

/* D = the address given by a dot operator */
static void gen_addr(const Expr *e)
{
    const Expr *v = e->lhs;
    unsigned off;

    if (!v) {                                   /* .(constant list) */
        int l = pool_label(e);
        if (pic) {
            emit("LEAX\tL%d,PCR", l);
            emit("TFR\tX,D");
        } else {
            emit("LDD\t#L%d", l);
        }
        return;
    }
    if (v->sym->kind == SYM_PROC) {             /* entry point */
        if (pic) {
            emit("LEAX\t%s,PCR", v->sym->asm_name);
            emit("TFR\tX,D");
        } else {
            emit("LDD\t#%s", v->sym->asm_name);
        }
        return;
    }
    if (fixed_location(v, &off)) {
        if ((pic && !v->sym->absolute) || in_frame(v->sym)) {
            emit("LEAX\t%s", var_ref(v->sym->asm_name, off));
            emit("TFR\tX,D");
        } else if (off) {
            emit("LDD\t#%s+%u", v->sym->asm_name, off);
        } else {
            emit("LDD\t#%s", v->sym->asm_name);
        }
        return;
    }
    if (based_location(v, &off)) {              /* the base's value */
        emit("LDD\t%s", var_ref(v->sym->base->asm_name, 0));
        if (off)
            emit("ADDD\t#%u", off);
        return;
    }
    gen_elem_offset(v);                         /* computed subscript */
    emit("LEAX\tD,X");
    emit("TFR\tX,D");
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
        emit_call("RT_MUL16");
        return;
    case OP_DIV:
    case OP_MOD:
        gen_x_d(e->lhs, e->rhs);
        emit_call("RT_DIV16");
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
        if (e->sym->base) {
            gen_base_x(e->sym);
            emit("%s\t,X", e->type == TY_BYTE ? "LDB" : "LDD");
            break;
        }
        emit("%s\t%s", e->type == TY_BYTE ? "LDB" : "LDD", var_ref(e->sym->asm_name, 0));
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
    case E_INDEX:
    case E_MEMBER: {
        unsigned off;
        const char *ld = e->type == TY_BYTE ? "LDB" : "LDD";
        if (fixed_location(e, &off)) {
            emit("%s\t%s", ld, var_ref(e->sym->asm_name, off));
        } else if (based_location(e, &off)) {
            gen_base_x(e->sym);
            emit("%s\t%s", ld, x_offset(off));
        } else {
            gen_elem_offset(e);
            emit("%s\tD,X", ld);
        }
        break;
    }
    case E_ADDR:
        gen_addr(e);
        break;
    }
}

/* call of a procedure (E_NAME or E_CALL); result, if any, in B or D */
static void gen_call(const Expr *e)
{
    const Proc *p = e->sym->proc;
    int n = e->args.len;

    if (p->external) {
        /* hook ABI: every argument pushed as a word, last one also in D.
           Hooks may change Y, a REENTRANT procedure's frame pointer. */
        if (sema_reentrant)
            emit("PSHS\tY");
        for (int i = 0; i < n; i++) {
            gen_as(e->args.items[i], 16);
            push(16);
        }
        emit_call(p->sym->asm_name);
        if (n)
            emit("LEAS\t%d,S", 2 * n);
        if (sema_reentrant)
            emit("PULS\tY");               /* leaves B/D (the result) alone */
        return;
    }

    if (p->reentrant) {
        /* arguments on the stack, first one deepest; see layout_frame */
        for (int i = 0; i < n; i++) {
            const Symbol *param = ((VarDecl *)p->params.items[i])->sym;
            gen_as(e->args.items[i], width(param->type));
            push(width(param->type));
        }
        emit_call(p->sym->asm_name);
        if (p->arg_size)
            emit("LEAS\t%u,S", p->arg_size);
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
            emit("%s\t%s", param->type == TY_BYTE ? "STB" : "STD",
                 var_ref(param->asm_name, 0));
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
            emit("%s\t%s", param->type == TY_BYTE ? "STB" : "STD",
                 var_ref(param->asm_name, 0));
        }
    }
    emit_call(p->sym->asm_name);
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
static void gen_stmt(const Stmt *s);

static const char *const int_name[INTERRUPTS] = { "IRQ", "FIRQ", "NMI" };
static const unsigned int_vector[INTERRUPTS] = { 0xfff8, 0xfff6, 0xfffc };

static int has_interrupts(void)
{
    for (int n = 0; n < INTERRUPTS; n++)
        if (sema_interrupt[n])
            return 1;
    return 0;
}

/* return from a procedure; B/D (the result) are left alone */
static void gen_epilogue(const Proc *p)
{
    if (p->reentrant) {
        emit("LEAS\t%u,Y", p->frame_size);  /* S = Y + locals: the caller's Y */
        emit("PULS\tY,PC");
    } else {
        emit("RTS");
    }
}

/* DO var = from TO to [BY by]; body END  (PL/M-80 manual 5.1.4):
   from is evaluated once; to is evaluated and compared before every pass
   (exit when var > to); by (default 1) is evaluated after every pass and
   added to var, and the loop ends when that addition wraps around.  All
   three are converted to var's type. */
static void gen_iter(const Stmt *s)
{
    const char *name = s->var->sym->asm_name;
    int w = width(s->var->type);
    const char *ld = w == 8 ? "LDB" : "LDD", *st = w == 8 ? "STB" : "STD";
    const char *cmp = w == 8 ? "CMPB" : "CMPD", *add = w == 8 ? "ADDB" : "ADDD";
    int top = new_label(), end = new_label();
    Opnd o;

    gen_as(s->from, w);
    emit("%s\t%s", st, var_ref(name, 0));
    emit_label(top);
    if (simple_operand(s->to, w, &o)) {
        emit("%s\t%s", ld, var_ref(name, 0));
        emit("%s\t%s", cmp, opnd_text(&o, w, WHOLE));
        emit("LBHI\tL%d", end);
    } else {
        gen_as(s->to, w);
        emit("%s\t%s", cmp, var_ref(name, 0));
        emit("LBLO\tL%d", end);             /* limit < var */
    }
    gen_block(s->block);
    if (!s->by || simple_operand(s->by, w, &o)) {
        emit("%s\t%s", ld, var_ref(name, 0));
        if (s->by)
            emit("%s\t%s", add, opnd_text(&o, w, WHOLE));
        else
            emit("%s\t#1", add);
    } else {
        gen_as(s->by, w);
        emit("%s\t%s", add, var_ref(name, 0));
    }
    emit("%s\t%s", st, var_ref(name, 0));    /* ST leaves C alone */
    emit("LBCC\tL%d", top);                  /* carry out: wrapped, done */
    emit_label(end);
}

/* DO CASE sel; s0; s1; ... END: a jump table of offsets relative to the
   table itself, so the code stays position-independent.  A selector out of
   range (undefined in PL/M) runs no case. */
static void gen_case(const Stmt *s)
{
    const Vec *cases = &s->block->stmts;
    int end = new_label(), tab = new_label();
    int *lab = xmalloc((cases->len ? cases->len : 1) * sizeof *lab);

    gen_as(s->expr, 16);
    emit("CMPD\t#%d", cases->len);
    emit("LBHS\tL%d", end);
    emit("LSLB");
    emit("ROLA");
    emit("LEAX\tL%d,PCR", tab);
    emit("LDD\tD,X");
    emit("JMP\tD,X");
    emit_label(tab);
    for (int i = 0; i < cases->len; i++) {
        lab[i] = new_label();
        emit("FDB\tL%d-L%d", lab[i], tab);
    }
    for (int i = 0; i < cases->len; i++) {
        emit_label(lab[i]);
        gen_stmt(cases->items[i]);
        if (i + 1 < cases->len)
            emit("LBRA\tL%d", end);
    }
    emit_label(end);
    free(lab);
}

static void gen_stmt(const Stmt *s)
{
    if (s->kind != S_DO)
        emit_source(s->loc);
    for (int i = 0; i < s->labels.len; i++)
        fprintf(out, "%s\n", ((Expr *)s->labels.items[i])->sym->asm_name);

    switch (s->kind) {
    case S_ASSIGN: {
        int widen = 0, w;
        gen_expr(s->expr);
        for (int i = 0; i < s->targets.len; i++)
            widen |= ((Expr *)s->targets.items[i])->type == TY_ADDRESS;
        if (widen && s->expr->type == TY_BYTE)
            emit("CLRA");
        w = widen || s->expr->type == TY_ADDRESS ? 16 : 8;  /* width held in B/D */
        for (int i = 0; i < s->targets.len; i++) {
            const Expr *t = s->targets.items[i];
            const char *st = t->type == TY_BYTE ? "STB" : "STD";
            unsigned off;
            if (fixed_location(t, &off)) {
                emit("%s\t%s", st, var_ref(t->sym->asm_name, off));
            } else if (based_location(t, &off)) {
                gen_base_x(t->sym);             /* leaves the value in D alone */
                emit("%s\t%s", st, x_offset(off));
            } else {
                /* computing the element's address needs D: save the value */
                push(w);
                gen_elem_offset(t);
                emit("LEAX\tD,X");
                pull(w);
                emit("%s\t,X", st);
            }
        }
        break;
    }
    case S_CALL:
        gen_call(s->expr);
        if (s->expr->sym->proc->interrupt)
            emit("ANDCC\t#$AF\t\t; a CALLed INTERRUPT procedure enables interrupts");
        break;
    case S_ENABLE:
        emit("ANDCC\t#$AF\t\t; clear I and F");
        break;
    case S_DISABLE:
        emit("ORCC\t#$50\t\t; set I and F");
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
    case S_ITER:
        gen_iter(s);
        break;
    case S_CASE:
        gen_case(s);
        break;
    case S_RETURN:
        if (s->expr)
            gen_as(s->expr, width(s->proc->ret));
        gen_epilogue(s->proc);
        break;
    case S_GOTO:
        /* Statements leave the stack balanced, so a jump within a procedure
           needs nothing else.  Leaving a procedure (only to the main
           program's outer level) restores the main program's S. */
        if (s->goto_out)
            emit("LDS\t%s", var_ref("RT_MAINS", 0));
        emit("LBRA\t%s", s->expr->sym->asm_name);
        break;
    case S_HALT:
        if (has_interrupts())
            emit("ANDCC\t#$AF\t\t; HALT enables interrupts");
        emit_jump("PLM_HALT");
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

/* ------------------------------------------------ INITIAL and DATA */

static unsigned var_bytes(const Symbol *v)
{
    return (v->dim ? (unsigned)v->dim : 1) * (v->st ? v->st->size : type_size(v->type));
}

/* an initial address as an assembler expression (absolute programs) */
static const char *addr_text(const InitVal *iv)
{
    static char buf[96];
    const Expr *e = iv->addr, *v = e->lhs;
    char base[80];
    unsigned off = 0;

    if (!v)
        snprintf(base, sizeof base, "L%d", pool_label(e));
    else if (v->sym->kind == SYM_PROC)
        snprintf(base, sizeof base, "%s", v->sym->asm_name);
    else {
        const_offset(v, &off);
        snprintf(base, sizeof base, "%s", v->sym->asm_name);
    }
    unsigned add = (off + iv->value) & 0xffff;
    if (!add)
        snprintf(buf, sizeof buf, "%s", base);
    else if (add < 0x8000)
        snprintf(buf, sizeof buf, "%s+%u", base, add);
    else
        snprintf(buf, sizeof buf, "%s-%u", base, 0x10000 - add);
    return buf;
}

/* the initialized bytes of v, as FCB/FDB lines after `label`.  A template
   (for copying into an --os9 data area) has 0 for addresses, which the
   startup code fills in, and stops after the last initialized byte. */
static void emit_init(const Symbol *v, const char *label, int is_template)
{
    unsigned used = 0, run = 0;
    unsigned last = 0;

    fprintf(out, "%s", label);
    for (int i = 0; i < v->init_vals.len; i++) {
        const InitVal *iv = v->init_vals.items[i];
        char val[100];
        if (iv->addr)
            snprintf(val, sizeof val, "%s", is_template ? "0" : addr_text(iv));
        else
            snprintf(val, sizeof val, iv->size == 1 ? "$%02X" : "$%04X", iv->value);
        if (iv->size != last || run == 8) {
            fprintf(out, "%s\t%s\t%s", i ? "\n" : "", iv->size == 1 ? "FCB" : "FDB", val);
            run = 1;
        } else {
            fprintf(out, ",%s", val);
            run++;
        }
        last = iv->size;
        used += iv->size;
    }
    fputc('\n', out);
    unsigned rest = var_bytes(v) - used;
    if (!is_template && rest)       /* the uninitialized remainder */
        emit("%s\t%u", pic && v->init == INIT_DATA ? "ZMB" : "RMB", rest);
}

static int *tpl;                    /* --os9: template label per sema_vars entry */

static int needs_template(const Symbol *v)
{
    return pic && v->init == INIT_INITIAL && v->init_vals.len;
}

/* --os9: copy each INITIAL variable's template into the data area, then
   store the initial addresses, which depend on where things were loaded */
static void emit_initial_prologue(void)
{
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        if (!needs_template(v))
            continue;
        unsigned n = 0;
        for (int k = 0; k < v->init_vals.len; k++)
            n += ((InitVal *)v->init_vals.items[k])->size;
        fprintf(out, "; INITIAL %s\n", v->name);
        emit("LEAX\tL%d,PCR", tpl[i]);
        if (v->absolute)
            emit("LDY\t#%s", v->asm_name);
        else
            emit("LEAY\t%s", var_ref(v->asm_name, 0));
        emit("LDD\t#%u", n);
        emit_call("RT_COPY");
        unsigned off = 0;
        for (int k = 0; k < v->init_vals.len; k++) {
            const InitVal *iv = v->init_vals.items[k];
            if (iv->addr) {
                gen_addr(iv->addr);
                if (iv->value)
                    emit("ADDD\t#%u", iv->value);
                emit("STD\t%s", var_ref(v->asm_name, off));
            }
            off += iv->size;
        }
    }
}

/* where an AT variable is: a number, or another variable's label */
static const char *at_text(const Symbol *v)
{
    static char buf[96];
    const InitVal *iv = &v->at->val;
    unsigned off = 0;

    if (!iv->addr) {
        snprintf(buf, sizeof buf, "$%04X", (iv->value + v->at_off) & 0xffff);
        return buf;
    }
    const Expr *t = iv->addr->lhs;
    const_offset(t, &off);
    unsigned add = (off + iv->value + v->at_off) & 0xffff;
    if (!add)
        snprintf(buf, sizeof buf, "%s", t->sym->asm_name);
    else if (add < 0x8000)
        snprintf(buf, sizeof buf, "%s+%u", t->sym->asm_name, add);
    else
        snprintf(buf, sizeof buf, "%s-%u", t->sym->asm_name, 0x10000 - add);
    return buf;
}

/* a REENTRANT procedure's variables: offsets from Y (see layout_frame) */
static void emit_frame(const Proc *p)
{
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        if (!in_frame(v) || v->owner != p)
            continue;
        if (v->at)
            fprintf(out, "%s\tEQU\t%s\n", v->asm_name, at_text(v));
        else
            fprintf(out, "%s\tEQU\t%u\n", v->asm_name, v->frame_off);
    }
}

static void emit_variables(void)
{
    fprintf(out, "\n; variables%s\n", pic ? " (data area, U-relative)" : "");
    if (sema_goto_out)
        fprintf(out, "RT_MAINS\tRMB\t2\t\t; main program's S\n");
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        if (v->init == INIT_DATA || in_frame(v))
            continue;                       /* stored with the code, or on the stack */
        if (v->at) {                        /* no storage of its own */
            fprintf(out, "%s\tEQU\t%s\n", v->asm_name, at_text(v));
            if (v->init == INIT_INITIAL && !pic) {
                /* put the values there, then carry on where we were */
                int here = new_label();
                fprintf(out, "L%d\tEQU\t*\n", here);
                emit("ORG\t%s", v->asm_name);
                emit_init(v, "", 1);
                emit("ORG\tL%d", here);
            }
            continue;
        }
        if (v->init == INIT_INITIAL && !pic)
            emit_init(v, v->asm_name, 0);   /* loaded with the program */
        else
            fprintf(out, "%s\tRMB\t%u\n", v->asm_name, var_bytes(v));
    }
}

/* DATA variables, and (--os9) INITIAL templates: part of the code */
static void emit_code_data(void)
{
    for (int i = 0; i < sema_vars.len; i++) {
        const Symbol *v = sema_vars.items[i];
        char label[32];
        if (v->init == INIT_DATA) {
            fprintf(out, "\n; DATA %s\n", v->name);
            emit_init(v, v->asm_name, 0);
        } else if (needs_template(v)) {
            fprintf(out, "\n; INITIAL values of %s\n", v->name);
            snprintf(label, sizeof label, "L%d", tpl[i]);
            emit_init(v, label, 1);
        }
    }
}

static void emit_pools(void)
{
    if (npools)
        fprintf(out, "\n; constant lists\n");
    for (int i = 0; i < npools; i++) {
        const Expr *e = pools[i].e;
        fprintf(out, "L%d", pools[i].label);
        for (int k = 0; k < e->ndata; k++) {
            if (k % 16 == 0)
                fprintf(out, "%s\tFCB\t", k ? "\n" : "");
            else
                fputc(',', out);
            fprintf(out, "$%02X", e->data[k]);
        }
        fputc('\n', out);
    }
}

static void emit_code(const Program *prog)
{
    int copies = 0;

    fprintf(out, "\n; program %s\nPLM_MAIN\n", prog->name);
    if (sema_goto_out)
        emit("STS\t%s\t; for GOTOs out of procedures", var_ref("RT_MAINS", 0));
    if (pic)
        emit_initial_prologue();
    for (int n = 0; n < INTERRUPTS; n++) {
        if (!sema_interrupt[n])
            continue;
        emit("LDX\t#RT_INT%d\t\t; install the %s handler", n, int_name[n]);
        emit("LDB\t#%d", n);
        emit_call("PLM_SETINT");
    }
    gen_block(prog->body);
    emit("RTS");

    for (int i = 0; i < sema_procs.len; i++) {
        const Proc *p = sema_procs.items[i];
        if (p->external)
            continue;
        fprintf(out, "\n; procedure %s%s\n", p->name, p->reentrant ? " (REENTRANT)" : "");
        if (p->reentrant)
            emit_frame(p);
        fprintf(out, "%s\n", p->sym->asm_name);
        if (p->reentrant) {
            emit("PSHS\tY");
            if (p->frame_size)
                emit("LEAS\t-%u,S", p->frame_size);
            emit("TFR\tS,Y");
        }
        gen_block(p->body);
        gen_epilogue(p);
    }

    /* interrupt entry points: IRQ and NMI stack every register, FIRQ only
       PC and CC; the handler's code expects DP = 0 */
    for (int n = 0; n < INTERRUPTS; n++) {
        const Proc *p = sema_interrupt[n];
        if (!p)
            continue;
        fprintf(out, "\n; %s handler: procedure %s (INTERRUPT %d)\nRT_INT%d\n",
                int_name[n], p->name, n, n);
        if (n == 1)
            emit("PSHS\tU,Y,X,DP,D");
        emit("CLRA");
        emit("TFR\tA,DP");
        emit_call(p->sym->asm_name);
        if (n == 1)
            emit("PULS\tD,DP,X,Y,U");
        emit("RTI\t\t\t; restores CC: interrupts enabled again");
    }

    emit_code_data();

    for (int i = 0; i < sema_vars.len; i++)
        copies |= needs_template(sema_vars.items[i]);
    fputc('\n', out);
    if (copies)
        fprintf(out, "RT_NEED_COPY\tEQU\t1\n");
    emit("INCLUDE\t\"plm_rt.asm\"");
    emit("INCLUDE\t\"plm_hooks.asm\"");
}

void codegen(FILE *f, const Program *prog, const CodegenOptions *opt)
{
    out = f;
    label_no = 0;
    npools = 0;
    last_comment_line = 0;
    pic = opt->os9;
    tpl = xcalloc(sema_vars.len ? (size_t)sema_vars.len : 1, sizeof *tpl);
    for (int i = 0; i < sema_vars.len; i++)
        if (needs_template(sema_vars.items[i]))
            tpl[i] = new_label();

    if (opt->os9) {
        /* OS-9 program module: inside MOD ... EMOD, RMB reserves space in
           the process's data area (offsets from U), not in the module.
           Variables come first so lwasm knows their offsets and can use
           short U-relative forms. */
        fprintf(out, "; generated by plmc from %s (OS-9 program module)\n", src_name);
        emit("PRAGMA\t6809");
        emit("MOD\tRT_EOM,RT_NAME,$11,$81,RT_START,RT_SIZE\t; Prgrm+Objct, ReEnt rev 1");
        emit_variables();
        fprintf(out, "\nRT_NAME\tFCS\t/%s/\n", opt->name ? opt->name : prog->name);
        emit("FCB\t1\t\t; edition");
        emit("INCLUDE\t\"plm_crt0_os9.asm\"");
        emit_code(prog);
        emit_pools();
        fprintf(out, "\nRT_STACK\tRMB\t%u\t\t; stack space (--stack)\n", opt->stack);
        fprintf(out, "RT_SIZE\tEQU\t.\t\t; data area size\n");
        emit("EMOD");
        fprintf(out, "RT_EOM\tEQU\t*\n");
        emit("END");
        return;
    }

    fprintf(out, "; generated by plmc from %s\n", src_name);
    emit("PRAGMA\t6809");
    emit("ORG\t$%04X", opt->org);
    emit("INCLUDE\t\"plm_crt0.asm\"");
    emit_code(prog);
    emit_variables();
    emit_pools();
    if (opt->vectors) {
        for (int n = 0; n < INTERRUPTS; n++) {
            if (!sema_interrupt[n])
                continue;
            emit("ORG\t$%04X", int_vector[n]);
            emit("FDB\tRT_INT%d\t\t; %s", n, int_name[n]);
        }
        emit("ORG\t$FFFE");
        emit("FDB\tRT_START");
    }
    emit("END\tRT_START");
}
