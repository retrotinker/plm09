/* exprgen: random PL/M expression programs with expected output.
 *
 * usage: exprgen SEED out.plm out.expected
 *
 * Writes a program that sets some BYTE and ADDRESS variables and the arrays
 * BA(4) BYTE and WA(4) ADDRESS, then evaluates random fully parenthesized
 * expressions and prints each result with PUTDEC: assigned to an ADDRESS,
 * assigned to a BYTE, assigned to an array element (printed back with a
 * constant subscript), or as an IF condition (prints 1 or 0).  Array
 * operands use a constant subscript or "(expr) AND 3", so they stay in
 * range.  The expected output comes from an
 * independent C model of the PL/M-80 rules:
 *   constants <= 255 are BYTE
 *   + - MOD AND OR XOR: BYTE if both operands BYTE (wrapping), else ADDRESS
 *   * /: ADDRESS;  relationals: BYTE 0FFH/0;  NOT, unary -: operand type
 *   IF tests bit 0; ADDRESS -> BYTE assignment keeps the low byte.
 * Expressions that would divide by zero are regenerated.
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
        return rnd(5) ? leaf(buf) : element(buf, depth);

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
    fprintf(plm, "DECLARE (B0, B1, B2, B3, RB) BYTE, (W0, W1, W2, W3, RW) ADDRESS;\n");
    fprintf(plm, "DECLARE BA(4) BYTE, WA(4) ADDRESS;\n");
    for (int i = 0; i < NVARS; i++) {
        bvals[i] = rnd(4) ? rnd(256) : 255 * rnd(2);
        wvals[i] = rnd(4) ? rnd(65536) : 65535 * rnd(2);
        fprintf(plm, "B%d = %u; W%d = %u;\n", i, bvals[i], i, wvals[i]);
    }
    for (int i = 0; i < 4; i++) {
        barr[i] = rnd(256);
        warr[i] = rnd(65536);
        fprintf(plm, "BA(%d) = %u; WA(%d) = %u;\n", i, barr[i], i, warr[i]);
    }
    for (int s = 0; s < NSTMTS; s++) {
        Val v;
        do {
            div_by_zero = 0;
            v = expr(buf, 0);
        } while (div_by_zero);
        switch (rnd(4)) {
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
        default:
            fprintf(plm, "IF %s THEN CALL PUTDEC(1); ELSE CALL PUTDEC(0);\n", buf);
            fprintf(exp, "%u\n", v.v & 1);
            break;
        }
    }
    fprintf(plm, "END FUZZ;\n");
    fclose(plm);
    fclose(exp);
    return 0;
}
