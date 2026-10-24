/* OS-9 program-module environment for sim6809 --os9.
 *
 * Loads a 6809 OS-9 program module, sets up a process the way F$Fork does,
 * and emulates the handful of system calls (SWI2 + function byte) that
 * simple programs need.  Paths 0, 1 and 2 are stdin, stdout and stderr.
 * OS-9 ends lines with CR: output CR is written as a newline, and an input
 * newline is delivered as CR.
 */
#include "os9.h"

#include <stdio.h>
#include <string.h>

enum {
    F_EXIT = 0x06,
    I_READ = 0x89, I_READLN = 0x8b, I_WRITE = 0x8a, I_WRITLN = 0x8c,
    E_BPNUM = 0xc9,     /* bad path number */
    E_EOF = 0xd3,       /* end of file */
};

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

static int sys_write(Cpu *c, int line)
{
    FILE *f = c->a == 1 ? stdout : c->a == 2 ? stderr : NULL;
    uint16_t n = 0;

    if (!f)
        return error(c, E_BPNUM);
    while (n < c->y) {
        uint8_t ch = c->mem[(uint16_t)(c->x + n++)];
        fputc(ch == 0x0d ? '\n' : ch, f);
        if (line && ch == 0x0d)
            break;
    }
    c->y = n;
    ok(c);
    return 0;
}

static int sys_read(Cpu *c, int line)
{
    uint16_t n = 0, want = c->y;

    if (c->a != 0)
        return error(c, E_BPNUM);
    fflush(stdout);
    while (n < want) {
        int ch = getchar();
        if (ch == EOF)
            break;
        if (ch == '\n')
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
    default:
        snprintf(msg, sizeof msg, "unsupported OS-9 system call $%02X", fn);
        return cpu_stop(c, STOP_TRAP, msg);
    }
}
