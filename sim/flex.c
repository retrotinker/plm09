/* FLEX 9 environment for sim6809 --flex.
 *
 * Loads a FLEX binary (.CMD) file and emulates the user-callable DOS
 * routines that simple programs need (FLEX Advanced Programmer's Guide).
 * The terminal is stdin/stdout: an input newline is delivered as CR, and
 * output CR (or PCRLF) is written as a newline, LF being dropped.  WARMS,
 * the return to FLEX, ends the simulation; its exit status is B (a
 * sim6809 convention: real FLEX has no exit status).
 */
#define _POSIX_C_SOURCE 200809L    /* strdup */
#include "flex.h"

#include <stdio.h>
#include <stdlib.h>
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
    LPOINT = 0xcc14,            /* line buffer pointer */
    LINBUF = 0xc080,            /* line buffer: the command line */
    LINBUF_SIZE = 128,
    ENTRY_LO = 0xcd00,          /* the DOS jump table */
    ENTRY_HI = 0xcd51,
    FMSINI = 0xd400,            /* the File Management System's entries */
    FMSCLS = 0xd403,
    FMS = 0xd406,
    SECDATA = 252,              /* data bytes in a sector */
};

/* FCB offsets */
enum {
    FCB_FUNC = 0, FCB_ERR = 1, FCB_ACT = 2, FCB_NAME = 4, FCB_NEWNAME = 53,
    FCB_COMP = 59,              /* space compression: 0 on, $FF off, else a count */
};

/* FMS errors */
enum {
    FE_FUNC = 1, FE_INUSE = 2, FE_EXISTS = 3, FE_NOTFOUND = 4, FE_EOF = 8,
    FE_STATUS = 18, FE_SPEC = 21,
};

/* the open files: host files named NAME.EXT, holding the data bytes of
   FLEX's sectors (compressed text, zero-padded to whole sectors) */
#define NFILES 16
static struct {
    uint16_t fcb;               /* 0 if free */
    FILE *f;
    int write;
    long stored;                /* bytes written to the file */
} files[NFILES];

static int fail(const char *path, const char *msg)
{
    fprintf(stderr, "sim6809: %s: %s\n", path, msg);
    return -1;
}

int flex_load(Cpu *c, const char *path, const char *params)
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

    /* the command line, in the line buffer: NAME [params] CR */
    const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    size_t nl = strcspn(base, "."), pl = strlen(params);
    if (nl + 1 + pl + 1 > LINBUF_SIZE)
        return fail(path, "the name and --param don't fit FLEX's line buffer (128 bytes)");
    for (size_t k = 0; k < nl; k++)
        c->mem[LINBUF + k] = (uint8_t)(base[k] >= 'a' && base[k] <= 'z' ? base[k] - 32 : base[k]);
    size_t len = nl;
    if (pl) {
        c->mem[LINBUF + len++] = ' ';
        memcpy(c->mem + LINBUF + len, params, pl);
        len += pl;
    }
    c->mem[LINBUF + len] = 0x0d;

    memset(c->mem + 0xcc00, 0, 0x100);         /* DOS variables: no files open */
    c->mem[MEMEND] = 0xbf;
    c->mem[MEMEND + 1] = 0xff;
    /* past the name and the blank after it, as FLEX leaves it (at the
       CR if there are no parameters) */
    uint16_t lp = (uint16_t)(LINBUF + nl + (pl ? 1 : 0));
    c->mem[LPOINT] = (uint8_t)(lp >> 8);
    c->mem[LPOINT + 1] = (uint8_t)lp;
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

/* the name (8 bytes) and extension (3) at fcb + off as a host name,
   NAME.EXT; -1 if there is no name */
static int hostname(Cpu *c, uint16_t fcb, int off, char *name)
{
    int n = 0;

    for (int i = 0; i < 8; i++) {
        uint8_t ch = c->mem[(uint16_t)(fcb + off + i)];
        if (ch == 0 || ch == ' ')
            break;
        name[n++] = (char)ch;
    }
    if (n == 0)
        return -1;
    for (int i = 0; i < 3; i++) {
        uint8_t ch = c->mem[(uint16_t)(fcb + off + 8 + i)];
        if (ch == 0 || ch == ' ')
            break;
        if (i == 0)
            name[n++] = '.';
        name[n++] = (char)ch;
    }
    name[n] = 0;
    return 0;
}

static int fileof(uint16_t fcb)
{
    for (int i = 0; i < NFILES; i++)
        if (files[i].f && files[i].fcb == fcb)
            return i;
    return -1;
}

static void store(int i, int ch)
{
    putc(ch, files[i].f);
    files[i].stored++;
}

/* the spaces counted in the FCB's compression flag: one as itself, more
   as $09 and the count */
static void spaces(Cpu *c, int i)
{
    uint8_t *comp = &c->mem[(uint16_t)(files[i].fcb + FCB_COMP)];

    if (*comp == 1) {
        store(i, ' ');
    } else if (*comp > 1 && *comp != 0xff) {
        store(i, 0x09);
        store(i, *comp);
    }
    if (*comp != 0xff)
        *comp = 0;
}

/* close file i: a written file is padded to whole sectors (and removed
   if nothing was written, as FLEX removes it) */
static void fclose_flex(Cpu *c, int i, const char *name)
{
    if (files[i].write) {
        spaces(c, i);
        if (files[i].stored == 0) {
            fclose(files[i].f);
            files[i].f = NULL;
            if (name)
                remove(name);
            return;
        }
        while (files[i].stored % SECDATA)
            store(i, 0);
    }
    fclose(files[i].f);
    files[i].f = NULL;
}

static char *names_of[NFILES];  /* each open file's host name */

/* the FMS call at X; returns the error (0 if none) */
static int fms(Cpu *c)
{
    uint16_t x = c->x;
    uint8_t *fcb = &c->mem[x];
    char name[16], newname[16];
    int i = fileof(x), ch;

    switch (fcb[FCB_FUNC]) {
    case 0:                                     /* read or write the next byte */
        if (i < 0)
            return FE_STATUS;
        if (files[i].write) {
            if (fcb[FCB_COMP] != 0xff && c->a == ' ') {
                if (++fcb[FCB_COMP] == 127)
                    spaces(c, i);
                return 0;
            }
            spaces(c, i);
            store(i, c->a);
            return 0;
        }
        if (fcb[FCB_COMP] != 0 && fcb[FCB_COMP] != 0xff) {
            fcb[FCB_COMP]--;                    /* (a run of spaces) */
            c->a = ' ';
            return 0;
        }
        for (;;) {
            if ((ch = getc(files[i].f)) == EOF)
                return FE_EOF;
            if (fcb[FCB_COMP] == 0xff)
                break;
            if (ch == 0)                        /* (text: NULs are skipped) */
                continue;
            if (ch == 0x09) {
                if ((ch = getc(files[i].f)) == EOF)
                    return FE_EOF;
                if (ch > 1)
                    fcb[FCB_COMP] = (uint8_t)(ch - 1);
                ch = ' ';
            }
            break;
        }
        c->a = (uint8_t)ch;
        return 0;
    case 1:                                     /* open for read */
    case 2:                                     /* open for write */
        if (i >= 0 || fcb[FCB_ACT])
            return FE_INUSE;
        if (hostname(c, x, FCB_NAME, name) < 0)
            return FE_SPEC;
        for (i = 0; i < NFILES && files[i].f; i++)
            ;
        if (i == NFILES)
            return FE_INUSE;
        if (fcb[FCB_FUNC] == 2) {
            FILE *t = fopen(name, "rb");
            if (t) {
                fclose(t);
                return FE_EXISTS;
            }
            files[i].f = fopen(name, "wb");
        } else {
            files[i].f = fopen(name, "rb");
        }
        if (!files[i].f)
            return FE_NOTFOUND;
        files[i].fcb = x;
        files[i].write = fcb[FCB_FUNC] == 2;
        files[i].stored = 0;
        free(names_of[i]);
        names_of[i] = strdup(name);
        fcb[FCB_ACT] = fcb[FCB_FUNC];
        fcb[FCB_COMP] = 0;                      /* text, unless changed */
        fcb[FCB_FUNC] = 0;
        return 0;
    case 4:                                     /* close */
        if (i < 0)
            return 13;                          /* (FCB not open) */
        fclose_flex(c, i, names_of[i]);
        fcb[FCB_ACT] = 0;
        return 0;
    case 5:                                     /* rewind */
        if (i < 0 || files[i].write)
            return FE_STATUS;
        rewind(files[i].f);
        fcb[FCB_COMP] = 0;                      /* (text again, as FLEX makes it) */
        fcb[FCB_FUNC] = 0;
        return 0;
    case 12:                                    /* delete */
        if (hostname(c, x, FCB_NAME, name) < 0)
            return FE_SPEC;
        if (remove(name))
            return FE_NOTFOUND;
        fcb[FCB_NAME] = 0xff;                   /* (marked deleted, as FLEX does) */
        return 0;
    case 13: {                                  /* rename */
        FILE *t;
        if (hostname(c, x, FCB_NAME, name) < 0 ||
            hostname(c, x, FCB_NEWNAME, newname) < 0)
            return FE_SPEC;
        if (!(t = fopen(name, "rb")))
            return FE_NOTFOUND;
        fclose(t);
        if ((t = fopen(newname, "rb"))) {
            fclose(t);
            return FE_EXISTS;
        }
        if (rename(name, newname))
            return FE_NOTFOUND;
        return 0;
    }
    default:
        return -1;
    }
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
    if (pc == FMSINI || pc == FMSCLS || pc == FMS) {
        int err = 0;
        if (pc == FMSCLS) {
            for (int i = 0; i < NFILES; i++)
                if (files[i].f) {
                    c->mem[(uint16_t)(files[i].fcb + FCB_ACT)] = 0;
                    fclose_flex(c, i, names_of[i]);
                }
        } else if (pc == FMS && (err = fms(c)) < 0) {
            snprintf(msg, sizeof msg, "unsupported FMS function %d",
                     c->mem[c->x]);
            return cpu_stop(c, STOP_TRAP, msg);
        }
        if (pc == FMS)
            c->mem[(uint16_t)(c->x + FCB_ERR)] = (uint8_t)err;
        if (err)
            c->cc &= (uint8_t)~CC_Z;
        else
            c->cc |= CC_Z;
        c->pc = read16(c, c->s);
        c->s = (uint16_t)(c->s + 2);
        return c->stop;
    }
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
