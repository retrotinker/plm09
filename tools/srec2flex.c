/* srec2flex: convert S-records (lwasm -f srec) to a FLEX binary (.CMD) file.
 *
 * usage: srec2flex in.s19 out.cmd
 *
 * Each run of consecutive loaded bytes becomes $02 records (address,
 * count, data) of at most 252 bytes, in address order; the S9 entry
 * address becomes the $16 transfer address record that makes the file a
 * FLEX command.  S0 and S5 records are ignored; checksums are verified.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAXREC = 252 };          /* as FLEX's own SAVE writes them */

static unsigned char mem[65536], used[65536];

static int hexval(const char *s, int n, unsigned *v)
{
    *v = 0;
    for (int i = 0; i < n; i++) {
        int c = s[i], d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return 0;
        *v = *v << 4 | (unsigned)d;
    }
    return 1;
}

static int bad(const char *path, int line, const char *msg)
{
    fprintf(stderr, "srec2flex: %s:%d: %s\n", path, line, msg);
    return 1;
}

int main(int argc, char **argv)
{
    char buf[600];
    unsigned entry = 0;
    int have_entry = 0, line = 0;
    FILE *in, *out;

    if (argc != 3) {
        fprintf(stderr, "usage: srec2flex in.s19 out.cmd\n");
        return 2;
    }
    if (!(in = fopen(argv[1], "r"))) {
        perror(argv[1]);
        return 2;
    }
    while (fgets(buf, sizeof buf, in)) {
        unsigned count, addr, sum, v;
        size_t len = strcspn(buf, "\r\n");
        line++;
        if (!len)
            continue;
        if (buf[0] != 'S' || len < 10 || !hexval(buf + 2, 2, &count) || len < 4 + 2 * count)
            return bad(argv[1], line, "not an S-record");
        sum = count;
        for (unsigned i = 0; i < count; i++) {
            if (!hexval(buf + 4 + 2 * i, 2, &v))
                return bad(argv[1], line, "bad hex digit");
            sum += v;
        }
        if ((sum & 0xff) != 0xff)
            return bad(argv[1], line, "bad checksum");
        hexval(buf + 4, 4, &addr);
        if (buf[1] == '1') {
            for (unsigned i = 0; i + 3 < count; i++) {
                hexval(buf + 8 + 2 * i, 2, &v);
                mem[(addr + i) & 0xffff] = (unsigned char)v;
                used[(addr + i) & 0xffff] = 1;
            }
        } else if (buf[1] == '9') {
            entry = addr;
            have_entry = 1;
        } else if (buf[1] != '0' && buf[1] != '5') {
            return bad(argv[1], line, "only S0, S1, S5 and S9 records are supported");
        }
    }
    fclose(in);
    if (!have_entry) {
        fprintf(stderr, "srec2flex: %s: no S9 entry record (a FLEX command needs a "
                "transfer address; use END label)\n", argv[1]);
        return 1;
    }

    if (!(out = fopen(argv[2], "wb"))) {
        perror(argv[2]);
        return 2;
    }
    for (unsigned a = 0; a < 65536;) {
        if (!used[a]) {
            a++;
            continue;
        }
        unsigned n = 0;
        while (a + n < 65536 && used[a + n] && n < MAXREC)
            n++;
        fputc(0x02, out);
        fputc((int)(a >> 8), out);
        fputc((int)(a & 0xff), out);
        fputc((int)n, out);
        fwrite(mem + a, 1, n, out);
        a += n;
    }
    fputc(0x16, out);
    fputc((int)(entry >> 8), out);
    fputc((int)(entry & 0xff), out);
    if (fclose(out) != 0) {
        perror(argv[2]);
        return 2;
    }
    return 0;
}
