/* Motorola S-record loader for sim6809 */
#include "srec.h"

#include <stdio.h>
#include <string.h>

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

/* decode the hex byte at s[0..1], or -1 */
static int hexbyte(const char *s)
{
    int hi = hexval((unsigned char)s[0]), lo;
    if (hi < 0 || (lo = hexval((unsigned char)s[1])) < 0)
        return -1;
    return hi << 4 | lo;
}

int srec_load(const char *path, uint8_t *mem, uint16_t *entry, int *has_entry)
{
    FILE *f = fopen(path, "r");
    char line[600];
    int lineno = 0;

    if (!f) {
        perror(path);
        return -1;
    }
    *has_entry = 0;
    while (fgets(line, sizeof line, f)) {
        uint8_t rec[256];
        size_t len;
        int count, sum = 0;

        lineno++;
        len = strcspn(line, "\r\n");
        line[len] = 0;
        if (len == 0)
            continue;
        if (line[0] != 'S' || len < 4 || (count = hexbyte(line + 2)) < 0
            || len != (size_t)(4 + 2 * count) || count < 3) {
            fprintf(stderr, "%s:%d: malformed S-record\n", path, lineno);
            goto fail;
        }
        for (int i = 0; i < count; i++) {
            int v = hexbyte(line + 4 + 2 * i);
            if (v < 0) {
                fprintf(stderr, "%s:%d: bad hex digit\n", path, lineno);
                goto fail;
            }
            rec[i] = (uint8_t)v;
            sum += v;
        }
        if (((sum + count) & 0xff) != 0xff) {
            fprintf(stderr, "%s:%d: checksum mismatch\n", path, lineno);
            goto fail;
        }
        uint16_t addr = (uint16_t)(rec[0] << 8 | rec[1]);
        switch (line[1]) {
        case '0':
        case '5':
            break;
        case '1':
            for (int i = 2; i < count - 1; i++)
                mem[(uint16_t)(addr + i - 2)] = rec[i];
            break;
        case '9':
            *entry = addr;
            *has_entry = 1;
            break;
        default:
            fprintf(stderr, "%s:%d: unsupported record type S%c\n", path, lineno, line[1]);
            goto fail;
        }
    }
    fclose(f);
    return 0;
fail:
    fclose(f);
    return -1;
}
