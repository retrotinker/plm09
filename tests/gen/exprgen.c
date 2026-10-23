/* exprgen: random PL/M expression programs with expected output.
 *
 * usage: exprgen SEED out.plm out.expected
 *
 * Writes a program that sets some BYTE and ADDRESS variables, then
 * evaluates random fully parenthesized expressions and prints each result
 * with PUTDEC, either assigned to an ADDRESS, assigned to a BYTE, or as an
 * IF condition (prints 1 or 0).  The expected output comes from an
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
        return leaf(buf);

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
        switch (rnd(3)) {
        case 0:
            fprintf(plm, "RW = %s;\nCALL PUTDEC(RW);\n", buf);
            fprintf(exp, "%u\n", v.v);
            break;
        case 1:
            fprintf(plm, "RB = %s;\nCALL PUTDEC(RB);\n", buf);
            fprintf(exp, "%u\n", v.v & 0xff);
            break;
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
