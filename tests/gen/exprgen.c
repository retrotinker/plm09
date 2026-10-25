/* exprgen: random PL/M expression programs with expected output.
 *
 * usage: exprgen SEED out.plm out.expected
 *
 * Writes a program that sets some BYTE and ADDRESS variables and (with
 * INITIAL) the arrays BA(4) BYTE and WA(4) ADDRESS and the structures SA, then evaluates random fully parenthesized
 * expressions and prints each result with PUTDEC: assigned to an ADDRESS,
 * assigned to a BYTE, assigned to an array element (printed back with a
 * constant subscript), or as an IF condition (prints 1 or 0).  Array
 * operands use a constant subscript or "(expr) AND 3", so they stay in
 * range.  Two more statement kinds exercise control flow: an iterative DO
 * over random bounds and step (printing the sum of the index values and
 * the final index, modelled step by step as in the PL/M-80 manual 5.1.4),
 * and a DO CASE with a "(expr) AND 3" selector and four cases.  Pointer
 * statements set P to the address of an array element (.BA(k), .WA(k), or
 * .BA / .WA) and read or write through the BASED variables PB, PW, PBA(i)
 * and PWA(i).  Structure operands SA(i).K and SA(i).V(j) (SA is an array
 * of 5-byte structures, so subscripts are scaled by a multiply) appear in
 * expressions and as assignment targets; i is a constant or "(expr) MOD 3",
 * j a constant or "(expr) AND 1".  The expected output comes from an
 * independent C model of the PL/M-80 rules:
 *   constants <= 255 are BYTE
 *   + - MOD AND OR XOR: BYTE if both operands BYTE (wrapping), else ADDRESS
 *   * /: ADDRESS;  relationals: BYTE 0FFH/0;  NOT, unary -: operand type
 *   IF tests bit 0; ADDRESS -> BYTE assignment keeps the low byte.
 * Expressions that would divide by zero are regenerated.
 *
 * Carry statements test PLUS and MINUS: "RB = x + y;" sets a known carry,
 * then an expression of + - PLUS MINUS over leaves is assigned and
 * printed.  The carry is the one out of the last + - PLUS MINUS executed
 * (8 or 16 bits, as the operation), and plmc evaluates a right operand
 * before the left one, so the model generates (and evaluates) right
 * operands first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVARS 4
#define NSTMTS 40
#define MAXDEPTH 4
#define BUFSZ 4096

/* Expressions are bounded: leaves are at most 6 characters and each level
   adds about 11, so depth 4 stays under 300 characters, far below BUFSZ.
   GCC cannot see that through the recursion. */
#pragma GCC diagnostic ignored "-Wformat-truncation"

typedef struct Val {
    unsigned v;
    int w;          /* 8 or 16 */
} Val;

static unsigned long long rng;

static unsigned rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng % n);
}

static unsigned bvals[NVARS], wvals[NVARS];
static unsigned barr[4], warr[4];      /* BA(0..3), WA(0..3) */
static unsigned sk[3], sv[3][2];       /* SA(0..2).K, SA(0..2).V(0..1) */
static int div_by_zero;

static unsigned mask(int w)
{
    return w == 8 ? 0xffu : 0xffffu;
}

static Val leaf(char *buf)
{
    Val r;
    switch (rnd(5)) {
    case 0: {
        int i = rnd(NVARS);
        sprintf(buf, "B%d", i);
        r.v = bvals[i];
        r.w = 8;
        return r;
    }
    case 1: {
        int i = rnd(NVARS);
        sprintf(buf, "W%d", i);
        r.v = wvals[i];
        r.w = 16;
        return r;
    }
    case 2: r.v = rnd(16); break;
    case 3: r.v = rnd(256); break;
    default: r.v = rnd(65536); break;
    }
    r.w = r.v <= 255 ? 8 : 16;
    /* vary the spelling: decimal or hex */
    if (rnd(2))
        sprintf(buf, "%u", r.v);
    else
        sprintf(buf, "0%XH", r.v);
    return r;
}

static Val expr(char *buf, int depth);

/* PLUS/MINUS: the carry left by the last + - PLUS MINUS, and one of them */
static unsigned carry;

static Val carry_op(int op, Val x, Val y)
{
    Val r;
    r.w = x.w == 8 && y.w == 8 ? 8 : 16;
    unsigned m = mask(r.w), c = op >= 2 ? carry : 0;
    if (op == 0 || op == 2) {               /* + PLUS */
        unsigned s = x.v + y.v + c;
        carry = s > m;
        r.v = s & m;
    } else {                                /* - MINUS */
        carry = x.v < y.v + c;
        r.v = (x.v - y.v - c) & m;
    }
    return r;
}

/* an expression of + - PLUS MINUS over leaves; the right operand is made
   (and so evaluated) first */
static Val carry_expr(char *buf, int depth)
{
    static const char *const ops[] = { "+", "-", "PLUS", "MINUS" };
    char a[BUFSZ], b[BUFSZ];

    if (depth >= MAXDEPTH || rnd(3) == 0)
        return leaf(buf);
    int op = rnd(4);
    Val y = carry_expr(b, depth + 1);
    Val x = carry_expr(a, depth + 1);
    snprintf(buf, BUFSZ, "(%s) %s (%s)", a, ops[op], b);
    return carry_op(op, x, y);
}

/* subscript 0..3: a constant, or (expr) AND 3 */
static unsigned subscript(char *buf, int depth)
{
    char a[BUFSZ];

    if (depth >= MAXDEPTH || rnd(2)) {
        unsigned k = rnd(4);
        sprintf(buf, "%u", k);
        return k;
    }
    Val x = expr(a, depth + 1);
    snprintf(buf, BUFSZ, "(%s) AND 3", a);
    return x.v & 3;
}

/* subscript 0..n-1 (n = 2 or 3): a constant, or (expr) AND 1 / MOD 3 */
static unsigned small_subscript(char *buf, int depth, unsigned n)
{
    char a[BUFSZ];

    if (depth >= MAXDEPTH || rnd(2)) {
        unsigned k = rnd(n);
        sprintf(buf, "%u", k);
        return k;
    }
    Val x = expr(a, depth + 1);
    snprintf(buf, BUFSZ, n == 2 ? "(%s) AND 1" : "(%s) MOD 3", a);
    return n == 2 ? x.v & 1 : x.v % 3;
}

/* SA(i).K or SA(i).V(j) */
static Val member(char *buf, int depth)
{
    char a[BUFSZ], b[BUFSZ];
    unsigned i = small_subscript(a, depth, 3);
    Val r;

    if (rnd(2)) {
        snprintf(buf, BUFSZ, "SA(%s).K", a);
        r.v = sk[i];
        r.w = 8;
    } else {
        unsigned j = small_subscript(b, depth, 2);
        snprintf(buf, BUFSZ, "SA(%s).V(%s)", a, b);
        r.v = sv[i][j];
        r.w = 16;
    }
    return r;
}

static Val element(char *buf, int depth)
{
    char a[BUFSZ];
    int word = rnd(2);
    unsigned k = subscript(a, depth);
    Val r;

    snprintf(buf, BUFSZ, "%s(%s)", word ? "WA" : "BA", a);
    r.v = word ? warr[k] : barr[k];
    r.w = word ? 16 : 8;
    return r;
}

static Val expr(char *buf, int depth)
{
    static const char *const ops[] = {
        "+", "-", "*", "/", "MOD", "AND", "OR", "XOR",
        "=", "<>", "<", ">", "<=", ">=",
    };
    char a[BUFSZ], b[BUFSZ];
    Val x, y, r;
    int k;

    if (depth >= MAXDEPTH || rnd(4) == 0)
    {
        int k = rnd(10);
        return k < 7 ? leaf(buf) : k < 9 ? element(buf, depth) : member(buf, depth);
    }

    k = rnd(17);
    if (k >= 14) {                          /* unary */
        x = expr(a, depth + 1);
        r.w = x.w;
        if (k == 14) {
            snprintf(buf, BUFSZ, "-(%s)", a);
            r.v = (0u - x.v) & mask(x.w);
        } else {
            snprintf(buf, BUFSZ, "NOT (%s)", a);
            r.v = ~x.v & mask(x.w);
        }
        return r;
    }

    x = expr(a, depth + 1);
    y = expr(b, depth + 1);
    snprintf(buf, BUFSZ, "(%s) %s (%s)", a, ops[k], b);
    r.w = x.w == 8 && y.w == 8 ? 8 : 16;
    switch (k) {
    case 0: r.v = x.v + y.v; break;
    case 1: r.v = x.v - y.v; break;
    case 2: r.v = x.v * y.v; r.w = 16; break;
    case 3:
        if (!y.v)
            div_by_zero = 1;
        r.v = y.v ? x.v / y.v : 0;
        r.w = 16;
        break;
    case 4:
        if (!y.v)
            div_by_zero = 1;
        r.v = y.v ? x.v % y.v : 0;
        break;
    case 5: r.v = x.v & y.v; break;
    case 6: r.v = x.v | y.v; break;
    case 7: r.v = x.v ^ y.v; break;
    default: {
        int t;
        switch (k) {
        case 8: t = x.v == y.v; break;
        case 9: t = x.v != y.v; break;
        case 10: t = x.v < y.v; break;
        case 11: t = x.v > y.v; break;
        case 12: t = x.v <= y.v; break;
        default: t = x.v >= y.v; break;
        }
        r.v = t ? 0xff : 0;
        r.w = 8;
        break;
    }
    }
    r.v &= mask(r.w);
    return r;
}

/* an expression for a loop bound or step: small, no division by zero */
static Val small_expr(char *buf)
{
    Val v;
    do {
        div_by_zero = 0;
        v = rnd(2) ? expr(buf, 2) : leaf(buf);
    } while (div_by_zero);
    return v;
}

/* DO index = from TO to [BY by]; SUM = SUM + index; END;  Returns 0 (and
   writes nothing) if the loop would make more than 300 passes. */
static int gen_loop(FILE *plm, FILE *exp)
{
    char from[BUFSZ], to[BUFSZ], by[BUFSZ];
    int word = rnd(2), has_by = rnd(2);
    unsigned mask = word ? 0xffff : 0xff;
    Val f = small_expr(from), t = small_expr(to), b = { 1, 8 };
    unsigned i, sum = 0, passes = 0;

    if (has_by)
        b = small_expr(by);
    /* the bounds and step don't involve the index or SUM, so re-evaluating
       them each pass (as PL/M does) gives the same values */
    i = f.v & mask;
    for (;;) {
        if (i > (t.v & mask))
            break;
        sum = (sum + i) & 0xffff;
        if (++passes > 300)
            return 0;
        unsigned next = i + (b.v & mask);
        i = next & mask;
        if (next > mask)                  /* wrapped around */
            break;
    }
    const char *ix = word ? "IW" : "IB";
    fprintf(plm, "SUM = 0;\nDO %s = %s TO %s", ix, from, to);
    if (has_by)
        fprintf(plm, " BY %s", by);
    fprintf(plm, ";\n    SUM = SUM + %s;\nEND;\nCALL PUTDEC(SUM);\nCALL PUTDEC(%s);\n", ix, ix);
    fprintf(exp, "%u\n%u\n", sum, i);
    return 1;
}

/* DO CASE (expr) AND 3; with four cases assigning RW, one maybe null */
static void gen_case(FILE *plm, FILE *exp)
{
    char sel[BUFSZ], e[BUFSZ];
    unsigned k, rw = 12345, chosen = rw;
    int null_case = rnd(2) ? (int)rnd(4) : -1;

    do {
        div_by_zero = 0;
        k = subscript(sel, 1);
    } while (div_by_zero);
    fprintf(plm, "RW = 12345;\nDO CASE %s;\n", sel);
    for (int c = 0; c < 4; c++) {
        if (c == null_case) {
            fprintf(plm, "    ;\n");
            continue;
        }
        Val v = small_expr(e);
        fprintf(plm, "    RW = %s;\n", e);
        if ((unsigned)c == k)
            chosen = v.v;
    }
    fprintf(plm, "END;\nCALL PUTDEC(RW);\n");
    fprintf(exp, "%u\n", chosen);
}

/* P = .BA(k) / .WA(k), then read or write through PB / PW; or P = .BA /
   .WA and read through PBA(i) / PWA(i) */
static void gen_pointer(FILE *plm, FILE *exp)
{
    char sub[BUFSZ], e[BUFSZ];
    int word = rnd(2);
    const char *arr = word ? "WA" : "BA";
    unsigned *vals = word ? warr : barr;
    unsigned k;

    do {
        div_by_zero = 0;
        k = subscript(sub, 1);
    } while (div_by_zero);
    switch (rnd(3)) {
    case 0:                                     /* read through PB / PW */
        fprintf(plm, "P = .%s(%s);\nRW = %s;\nCALL PUTDEC(RW);\n", arr, sub, word ? "PW" : "PB");
        fprintf(exp, "%u\n", vals[k]);
        break;
    case 1: {                                   /* write through PB / PW */
        Val v = small_expr(e);
        fprintf(plm, "P = .%s(%s);\n%s = %s;\nCALL PUTDEC(%s(%u));\n",
                arr, sub, word ? "PW" : "PB", e, arr, k);
        vals[k] = word ? v.v : v.v & 0xff;
        fprintf(exp, "%u\n", vals[k]);
        break;
    }
    default:                                    /* a BASED array over BA / WA */
        fprintf(plm, "P = .%s;\nRW = %s(%s);\nCALL PUTDEC(RW);\n", arr, word ? "PWA" : "PBA", sub);
        fprintf(exp, "%u\n", vals[k]);
        break;
    }
}

int main(int argc, char **argv)
{
    FILE *plm, *exp;
    char buf[BUFSZ];

    if (argc != 4) {
        fprintf(stderr, "usage: exprgen SEED out.plm out.expected\n");
        return 2;
    }
    rng = strtoull(argv[1], NULL, 10) * 0x9E3779B97F4A7C15ull + 1;
    if (!(plm = fopen(argv[2], "w")) || !(exp = fopen(argv[3], "w"))) {
        perror("exprgen");
        return 2;
    }

    fprintf(plm, "/* exprgen seed %s */\nFUZZ: DO;\n", argv[1]);
    fprintf(plm, "PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS; END PUTDEC;\n");
    /* the arrays and structures start with INITIAL values */
    for (int i = 0; i < 4; i++) {
        barr[i] = rnd(256);
        warr[i] = rnd(65536);
    }
    for (int i = 0; i < 3; i++) {
        sk[i] = rnd(256);
        sv[i][0] = rnd(65536);
        sv[i][1] = rnd(65536);
    }
    /* half the programs run in a REENTRANT procedure, so that the scalars
       are in its stack frame (the INITIAL arrays must stay global) */
    int reent = rnd(2);
    fprintf(plm, "DECLARE BA(4) BYTE INITIAL (%u, %u, %u, %u);\n", barr[0], barr[1], barr[2], barr[3]);
    fprintf(plm, "DECLARE WA(4) ADDRESS INITIAL (%u, %u, %u, %u);\n", warr[0], warr[1], warr[2], warr[3]);
    fprintf(plm, "DECLARE SA (3) STRUCTURE (K BYTE, V (2) ADDRESS) INITIAL (");
    for (int i = 0; i < 3; i++)
        fprintf(plm, "%s%u, %u, %u", i ? ", " : "", sk[i], sv[i][0], sv[i][1]);
    fprintf(plm, ");\n");
    if (reent)
        fprintf(plm, "BODY: PROCEDURE REENTRANT;\n");
    fprintf(plm, "DECLARE (B0, B1, B2, B3, RB) BYTE, (W0, W1, W2, W3, RW) ADDRESS;\n");
    fprintf(plm, "DECLARE IB BYTE, (IW, SUM) ADDRESS;\n");
    fprintf(plm, "DECLARE P ADDRESS, PB BASED P BYTE, PW BASED P ADDRESS;\n");
    fprintf(plm, "DECLARE (PBA BASED P) (4) BYTE, (PWA BASED P) (4) ADDRESS;\n");
    for (int i = 0; i < NVARS; i++) {
        bvals[i] = rnd(4) ? rnd(256) : 255 * rnd(2);
        wvals[i] = rnd(4) ? rnd(65536) : 65535 * rnd(2);
        fprintf(plm, "B%d = %u; W%d = %u;\n", i, bvals[i], i, wvals[i]);
    }
    for (int s = 0; s < NSTMTS; s++) {
        Val v;
        do {
            div_by_zero = 0;
            v = expr(buf, 0);
        } while (div_by_zero);
        switch (rnd(9)) {
        case 8: {
            /* PLUS and MINUS, after a + that sets a known carry */
            char a[BUFSZ], b[BUFSZ], c[BUFSZ];
            Val x = leaf(a), y = leaf(b);
            Val r = carry_op(0, x, y);
            Val e = carry_expr(c, 1);
            int word = rnd(2);
            fprintf(plm, "RB = (%s) + (%s);\n%s = %s;\nCALL PUTDEC(%s);\n",
                    a, b, word ? "RW" : "RB", c, word ? "RW" : "RB");
            fprintf(exp, "%u\n", word ? e.v : e.v & 0xff);
            (void)r;
            break;
        }
        case 0:
            fprintf(plm, "RW = %s;\nCALL PUTDEC(RW);\n", buf);
            fprintf(exp, "%u\n", v.v);
            break;
        case 1:
            fprintf(plm, "RB = %s;\nCALL PUTDEC(RB);\n", buf);
            fprintf(exp, "%u\n", v.v & 0xff);
            break;
        case 2: {
            /* store into an element; the subscript has no side effects, so
               evaluating it before or after the value gives the same k */
            char sub[BUFSZ];
            unsigned k;
            int word = rnd(2);
            do {
                div_by_zero = 0;
                k = subscript(sub, 1);
            } while (div_by_zero);
            fprintf(plm, "%s(%s) = %s;\nCALL PUTDEC(%s(%u));\n",
                    word ? "WA" : "BA", sub, buf, word ? "WA" : "BA", k);
            if (word)
                warr[k] = v.v;
            else
                barr[k] = v.v & 0xff;
            fprintf(exp, "%u\n", word ? v.v : v.v & 0xff);
            break;
        }
        case 4:
            if (!gen_loop(plm, exp))
                s--;                        /* too many passes: try again */
            break;
        case 5:
            gen_case(plm, exp);
            break;
        case 6:
            gen_pointer(plm, exp);
            break;
        case 7: {
            /* store into a structure member, print it back */
            char a[BUFSZ], b[BUFSZ];
            unsigned i, j = 0;
            int word = rnd(2);
            do {
                div_by_zero = 0;
                i = small_subscript(a, 1, 3);
                if (word)
                    j = small_subscript(b, 1, 2);
            } while (div_by_zero);
            if (word) {
                fprintf(plm, "SA(%s).V(%s) = %s;\nCALL PUTDEC(SA(%u).V(%u));\n", a, b, buf, i, j);
                sv[i][j] = v.v;
                fprintf(exp, "%u\n", v.v);
            } else {
                fprintf(plm, "SA(%s).K = %s;\nCALL PUTDEC(SA(%u).K);\n", a, buf, i);
                sk[i] = v.v & 0xff;
                fprintf(exp, "%u\n", sk[i]);
            }
            break;
        }
        default:
            fprintf(plm, "IF %s THEN CALL PUTDEC(1); ELSE CALL PUTDEC(0);\n", buf);
            fprintf(exp, "%u\n", v.v & 1);
            break;
        }
    }
    if (reent)
        fprintf(plm, "END BODY;\nCALL BODY;\n");
    fprintf(plm, "END FUZZ;\n");
    fclose(plm);
    fclose(exp);
    return 0;
}
