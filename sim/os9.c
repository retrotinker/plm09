/* OS-9 program-module environment for sim6809 --os9.
 *
 * Loads a 6809 OS-9 program module, sets up a process the way F$Fork does,
 * and emulates the handful of system calls (SWI2 + function byte) that
 * simple programs need.  Paths 0, 1 and 2 are stdin, stdout and stderr, a
 * terminal (SCF): OS-9 ends lines with CR, so output CR is written as a
 * newline, and an input newline is delivered as CR.  I$Open and I$Create
 * open host files (RBF), named relative to the current directory, read and
 * written as they are.
 */
#define _POSIX_C_SOURCE 200809L    /* fileno, ftruncate */
#include "os9.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    F_EXIT = 0x06,
    I_CREATE = 0x83, I_OPEN = 0x84, I_DELETE = 0x87, I_SEEK = 0x88,
    I_READ = 0x89, I_READLN = 0x8b, I_WRITE = 0x8a, I_WRITLN = 0x8c,
    I_GETSTT = 0x8d, I_SETSTT = 0x8e, I_CLOSE = 0x8f,
    SS_OPT = 0, SS_SIZE = 2, SS_POS = 5, SS_EOF = 6,
    E_PTHFUL = 0xc8,    /* path table full */
    E_BPNUM = 0xc9,     /* bad path number */
    E_BMODE = 0xcb,     /* bad mode */
    E_UNKSVC = 0xd0,    /* unknown service */
    E_EOF = 0xd3,       /* end of file */
    E_PNNF = 0xd8,      /* path name not found */
    E_CEF = 0xda,       /* creating an existing file */
    E_WRITE = 0xf5,     /* write error */
};

/* The paths: 0-2 the terminal (no FILE: stdin, stdout, stderr), the rest
   host files.  `open` is 0 for a free (or closed) path. */
#define NPATHS 16
static struct {
    int open;
    FILE *f;
} paths[NPATHS] = { { 1, NULL }, { 1, NULL }, { 1, NULL } };

/* 24-bit module CRC: polynomial $800063, preset $FFFFFF, MSB first */
static uint32_t crc24(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xffffff;

    while (n--) {
        crc ^= (uint32_t)*p++ << 16;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x800000) ? (crc << 1) ^ 0x800063 : crc << 1;
        crc &= 0xffffff;
    }
    return crc;
}

static int fail(const char *path, const char *msg)
{
    fprintf(stderr, "sim6809: %s: %s\n", path, msg);
    return -1;
}

int os9_load(Cpu *c, const char *path, uint16_t load, uint16_t data, const char *params)
{
    static uint8_t m[65536];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f) {
        perror(path);
        return -1;
    }
    n = fread(m, 1, sizeof m, f);
    fclose(f);

    if (n < 13 || m[0] != 0x87 || m[1] != 0xcd)
        return fail(path, "not an OS-9 module (no $87CD sync bytes)");
    uint16_t size = (uint16_t)(m[2] << 8 | m[3]);
    if (size < 16 || size > n)
        return fail(path, "module size in header exceeds the file");
    uint8_t parity = 0;
    for (int i = 0; i < 9; i++)
        parity ^= m[i];
    if (parity != 0xff)
        return fail(path, "bad module header parity");
    if (crc24(m, size) != 0x800fe3)
        return fail(path, "bad module CRC");
    if ((m[6] >> 4) != 1 || (m[6] & 0x0f) != 1)
        return fail(path, "not a 6809 object-code program module (type/language must be $11)");
    uint16_t exec = (uint16_t)(m[9] << 8 | m[10]);
    uint16_t dsize = (uint16_t)(m[11] << 8 | m[12]);

    /* F$Fork: data area (rounded up to whole pages) with the parameter
       string at its top; S and X point at the parameters */
    size_t plen = strlen(params) + 1;           /* parameters end with CR */
    uint32_t total = ((uint32_t)dsize + plen + 255) & ~255u;
    if ((uint32_t)load + size > 0x10000 || (uint32_t)data + total > c->io_base
        || (data < (uint32_t)load + size && load < data + total))
        return fail(path, "module and data area overlap, or don't fit below the I/O block");
    if (data & 0xff)
        return fail(path, "--data must be page aligned");

    memcpy(c->mem + load, m, size);
    uint16_t top = (uint16_t)(data + total);
    uint16_t pstart = (uint16_t)(top - plen);
    memcpy(c->mem + pstart, params, plen - 1);
    c->mem[(uint16_t)(top - 1)] = 0x0d;

    c->pc = (uint16_t)(load + exec);
    c->u = data;
    c->y = top;
    c->x = pstart;
    c->s = pstart;
    c->a = (uint8_t)(plen >> 8);
    c->b = (uint8_t)plen;
    c->dp = (uint8_t)(data >> 8);
    c->cc = 0;
    c->swi2_hook = os9_syscall;
    c->protect = 1;
    c->mod_lo = load;
    c->mod_hi = (uint32_t)load + size;
    c->data_lo = data;
    c->data_hi = (uint32_t)data + total;
    return 0;
}

static void ok(Cpu *c)
{
    c->cc &= (uint8_t)~0x01;
}

static int error(Cpu *c, uint8_t code)
{
    c->cc |= 0x01;
    c->b = code;
    return 0;
}

/* the open path A, or -1 */
static int path(Cpu *c)
{
    return c->a < NPATHS && paths[c->a].open ? c->a : -1;
}

static int sys_write(Cpu *c, int line)
{
    int p = path(c);
    FILE *f = p == 1 ? stdout : p == 2 ? stderr : p > 2 ? paths[p].f : NULL;
    uint16_t n = 0;

    if (!f)
        return error(c, E_BPNUM);
    if (f == stderr)    /* (in order, when both go to one file) */
        fflush(stdout);
    while (n < c->y) {
        uint8_t ch = c->mem[(uint16_t)(c->x + n++)];
        if (fputc(p <= 2 && ch == 0x0d ? '\n' : ch, f) == EOF)
            return error(c, E_WRITE);
        if (line && ch == 0x0d)
            break;
    }
    c->y = n;
    ok(c);
    return 0;
}

static int sys_read(Cpu *c, int line)
{
    int p = path(c);
    FILE *f = p == 0 ? stdin : p > 2 ? paths[p].f : NULL;
    uint16_t n = 0, want = c->y;

    if (!f)
        return error(c, E_BPNUM);
    fflush(stdout);
    while (n < want) {
        int ch = getc(f);
        if (ch == EOF)
            break;
        if (p == 0 && ch == '\n')
            ch = 0x0d;
        c->mem[(uint16_t)(c->x + n++)] = (uint8_t)ch;
        if (line && ch == 0x0d)
            break;
    }
    c->y = n;
    if (n == 0 && want > 0)
        return error(c, E_EOF);
    ok(c);
    return 0;
}

/* The path name at X (letters, digits and . _ $ /, up to anything else)
   into name; X goes past it and any blanks after it.  Returns 0, or -1 if
   there is none. */
static int pathname(Cpu *c, char *name, size_t size)
{
    size_t n = 0;
    uint16_t x = c->x;

    for (;;) {
        uint8_t ch = c->mem[x];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
              ch == '$' || ch == '/') || n + 1 >= size)
            break;
        name[n++] = (char)ch;
        x++;
    }
    name[n] = '\0';
    while (c->mem[x] == ' ')
        x++;
    c->x = x;
    return n ? 0 : -1;
}

/* I$Open (create 0) or I$Create (1): mode A (1 read, 2 write, 3 both),
   the path name at X; the new path in A */
static int sys_open(Cpu *c, int create)
{
    char name[256];
    FILE *f;
    int p, mode = c->a & 3;

    if (mode == 0 || (c->a & 0x80))
        return error(c, E_BMODE);
    if (pathname(c, name, sizeof name) < 0)
        return error(c, E_PNNF);
    for (p = 3; p < NPATHS && paths[p].open; p++)
        ;
    if (p == NPATHS)
        return error(c, E_PTHFUL);
    if (create) {
        if ((f = fopen(name, "rb"))) {
            fclose(f);
            return error(c, E_CEF);
        }
        f = fopen(name, "w+b");
    } else
        f = fopen(name, mode == 1 ? "rb" : "r+b");
    if (!f)
        return error(c, E_PNNF);
    paths[p].open = 1;
    paths[p].f = f;
    c->a = (uint8_t)p;
    ok(c);
    return 0;
}

static int sys_close(Cpu *c)
{
    int p = path(c);

    if (p < 0)
        return error(c, E_BPNUM);
    if (paths[p].f)
        fclose(paths[p].f);
    paths[p].open = 0;
    paths[p].f = NULL;
    ok(c);
    return 0;
}

/* I$Seek: path A to X:U (a terminal ignores it) */
static int sys_seek(Cpu *c)
{
    int p = path(c);

    if (p < 0)
        return error(c, E_BPNUM);
    if (p > 2 && fseek(paths[p].f, (long)c->x << 16 | c->u, SEEK_SET))
        return error(c, E_BPNUM);
    ok(c);
    return 0;
}

/* I$GetStt (set 0) or I$SetStt (1): status code B on path A */
static int sys_stt(Cpu *c, int set)
{
    int p = path(c);
    FILE *f;
    long pos, size;

    if (p < 0)
        return error(c, E_BPNUM);
    f = paths[p].f;
    if (c->b == SS_OPT) {
        /* the options: the device type first, 0 SCF (a terminal) or
           1 RBF (a file); setting them changes nothing */
        if (!set) {
            for (int i = 0; i < 32; i++)
                c->mem[(uint16_t)(c->x + i)] = 0;
            c->mem[c->x] = p > 2 ? 1 : 0;
        }
        ok(c);
        return 0;
    }
    if (!f || (c->b != SS_SIZE && (set || (c->b != SS_POS && c->b != SS_EOF))))
        return error(c, E_UNKSVC);
    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (set) {      /* SS.Size: the file's size is X:U */
        fflush(f);
        size = (long)c->x << 16 | c->u;
        if (ftruncate(fileno(f), size))
            return error(c, E_WRITE);
        if (pos > size)
            pos = size;
    } else if (c->b == SS_EOF) {
        fseek(f, pos, SEEK_SET);
        if (pos >= size)
            return error(c, E_EOF);
        ok(c);
        return 0;
    } else {
        long v = c->b == SS_SIZE ? size : pos;
        c->x = (uint16_t)(v >> 16);
        c->u = (uint16_t)v;
    }
    fseek(f, pos, SEEK_SET);
    ok(c);
    return 0;
}

/* I$Delete: the file named at X */
static int sys_delete(Cpu *c)
{
    char name[256];

    if (pathname(c, name, sizeof name) < 0 || remove(name))
        return error(c, E_PNNF);
    ok(c);
    return 0;
}

int os9_syscall(Cpu *c)
{
    static char msg[48];
    uint8_t fn = c->mem[c->pc];

    c->pc++;
    switch (fn) {
    case F_EXIT:
        c->exit_code = c->b;
        return cpu_stop(c, STOP_EXIT, "F$Exit");
    case I_WRITE:
        return sys_write(c, 0);
    case I_WRITLN:
        return sys_write(c, 1);
    case I_READ:
        return sys_read(c, 0);
    case I_READLN:
        return sys_read(c, 1);
    case I_OPEN:
        return sys_open(c, 0);
    case I_CREATE:
        return sys_open(c, 1);
    case I_CLOSE:
        return sys_close(c);
    case I_SEEK:
        return sys_seek(c);
    case I_GETSTT:
        return sys_stt(c, 0);
    case I_SETSTT:
        return sys_stt(c, 1);
    case I_DELETE:
        return sys_delete(c);
    default:
        snprintf(msg, sizeof msg, "unsupported OS-9 system call $%02X", fn);
        return cpu_stop(c, STOP_TRAP, msg);
    }
}
