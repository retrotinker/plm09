/* FLEX 9 environment for sim6809 --flex.
 *
 * Loads a FLEX binary (.CMD) file and emulates the user-callable DOS
 * routines that simple programs need (FLEX Advanced Programmer's Guide).
 * The terminal is stdin/stdout: an input newline is delivered as CR, and
 * output CR (or PCRLF) is written as a newline, LF being dropped.  WARMS,
 * the return to FLEX, ends the simulation; its exit status is B (a
 * sim6809 convention: real FLEX has no exit status).
 */
#include "flex.h"

#include <stdio.h>
#include <string.h>

enum {
    FLEX_STACK = 0xc07f,        /* FLEX's stack pointer for commands */
    FLEX_LO = 0xc700,           /* FLEX itself: no writes, no code (below it
                                   are FLEX's stack and line buffer, and the
                                   utility command area, which belongs to
                                   the running command) */
    FLEX_HI = 0xe000,
    MEMEND = 0xcc2b,            /* end of user memory */
    LASTTRM = 0xcc11,           /* Last Terminator */
    ENTRY_LO = 0xcd00,          /* the DOS jump table */
    ENTRY_HI = 0xcd51,
};

static int fail(const char *path, const char *msg)
{
    fprintf(stderr, "sim6809: %s: %s\n", path, msg);
    return -1;
}

int flex_load(Cpu *c, const char *path)
{
    static uint8_t f[65536 * 2];
    FILE *fp = fopen(path, "rb");
    size_t n, i = 0;
    int have_xfer = 0;
    uint16_t xfer = 0;

    if (!fp) {
        perror(path);
        return -1;
    }
    n = fread(f, 1, sizeof f, fp);
    fclose(fp);

    while (i < n) {
        if (f[i] == 0) {                        /* padding to the end of a sector */
            i++;
        } else if (f[i] == 0x02) {              /* load record */
            if (i + 4 > n || i + 4 + f[i + 3] > n)
                return fail(path, "binary record runs past the end of the file");
            uint16_t addr = (uint16_t)(f[i + 1] << 8 | f[i + 2]);
            for (int k = 0; k < f[i + 3]; k++)
                c->mem[(uint16_t)(addr + k)] = f[i + 4 + k];
            i += 4 + f[i + 3];
        } else if (f[i] == 0x16) {              /* transfer address */
            if (i + 3 > n)
                return fail(path, "transfer address record runs past the end of the file");
            xfer = (uint16_t)(f[i + 1] << 8 | f[i + 2]);
            have_xfer = 1;
            i += 3;
        } else {
            return fail(path, "not a FLEX binary file (expected a $02 or $16 record)");
        }
    }
    if (!have_xfer)
        return fail(path, "no transfer address: not a FLEX command");

    memset(c->mem + 0xcc00, 0, 0x100);         /* DOS variables: no files open */
    c->mem[MEMEND] = 0xbf;
    c->mem[MEMEND + 1] = 0xff;
    c->pc = xfer;
    c->s = FLEX_STACK;
    c->cc = 0;
    c->pc_hook = flex_call;
    c->ro_lo = FLEX_LO;
    c->ro_hi = FLEX_HI;
    return 0;
}

static void out(int ch)
{
    if (ch == 0x0d)
        putchar('\n');
    else if (ch != 0x0a)
        putchar(ch);
}

static uint16_t read16(Cpu *c, uint16_t a)
{
    return (uint16_t)(c->mem[a] << 8 | c->mem[(uint16_t)(a + 1)]);
}

int flex_call(Cpu *c)
{
    static char msg[48];
    static const char *const names[] = {
        "COLDS", "WARMS", "RENTER", "INCH", "INCH2", "OUTCH", "OUTCH2", "GETCHR",
        "PUTCHR", "INBUFF", "PSTRNG", "CLASS", "PCRLF", "NXTCH", "RSTRIO", "GETFIL",
        "LOAD", "SETEXT", "ADDBX", "OUTDEC", "OUTHEX", "RPTERR", "GETHEX", "OUTADR",
        "INDEC", "DOCMND", "STAT",
    };
    uint16_t pc = c->pc;

    if (pc < FLEX_LO || pc >= FLEX_HI)
        return -1;
    if (pc < ENTRY_LO || pc >= ENTRY_HI || (pc - ENTRY_LO) % 3)
        return cpu_stop(c, STOP_ILLEGAL, "code run in FLEX's memory, not at a DOS entry point");

    int k = (pc - ENTRY_LO) / 3;
    switch (k) {
    case 1:                                     /* WARMS */
    case 2:                                     /* RENTER */
        fflush(stdout);
        c->exit_code = c->b;
        return cpu_stop(c, STOP_EXIT, names[k]);
    case 3:                                     /* INCH */
    case 4:                                     /* INCH2 */
    case 7: {                                   /* GETCHR */
        int ch;
        fflush(stdout);
        ch = getchar();
        c->a = ch == EOF ? 0 : ch == '\n' ? 0x0d : (uint8_t)ch;
        break;
    }
    case 5:                                     /* OUTCH */
    case 6:                                     /* OUTCH2 */
    case 8:                                     /* PUTCHR */
        out(c->a);
        break;
    case 10: {                                  /* PSTRNG: CR LF, then up to EOT */
        uint16_t p = c->x;
        putchar('\n');
        while (c->mem[p] != 0x04)
            out(c->mem[p++]);
        break;
    }
    case 11: {                                  /* CLASS */
        uint8_t ch = c->a;
        int alnum = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= 'a' && ch <= 'z');
        if (alnum) {
            c->cc &= (uint8_t)~CC_C;
        } else {
            c->cc |= CC_C;
            c->mem[LASTTRM] = ch;
        }
        break;
    }
    case 12:                                    /* PCRLF */
        putchar('\n');
        break;
    case 14:                                    /* RSTRIO */
        break;
    case 18:                                    /* ADDBX */
        c->x = (uint16_t)(c->x + c->b);
        break;
    case 19:                                    /* OUTDEC */
        printf(c->b ? "%5u" : "%u", (unsigned)read16(c, c->x));
        break;
    case 20:                                    /* OUTHEX */
        printf("%02X", c->mem[c->x]);
        break;
    case 23:                                    /* OUTADR */
        printf("%04X", (unsigned)read16(c, c->x));
        break;
    default:
        snprintf(msg, sizeof msg, "unsupported FLEX call %s ($%04X)",
                 k < (int)(sizeof names / sizeof *names) ? names[k] : "?", pc);
        return cpu_stop(c, STOP_TRAP, msg);
    }
    /* return to the caller (RTS) */
    c->pc = read16(c, c->s);
    c->s = (uint16_t)(c->s + 2);
    return c->stop;
}
