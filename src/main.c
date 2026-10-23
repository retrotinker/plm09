/* plmc: PL/M (tiny core) to 6809 lwasm compiler -- driver */
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f,
            "usage: plmc [options] file.plm\n"
            "  -o FILE       output assembly file (default: stdout)\n"
            "  --org ADDR    load/start address (default $0100)\n"
            "  --vectors     also emit the reset vector at $FFFE\n"
            "  --tokens      print the token stream and stop\n"
            "  --ast         print the syntax tree and stop\n");
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t cap = 4096, n = 0, k;
    char *buf;

    if (!f)
        fatal("%s: %s", path, strerror(errno));
    buf = xmalloc(cap);
    while ((k = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += k;
        if (n + 1 == cap)
            buf = xrealloc(buf, cap *= 2);
    }
    if (ferror(f))
        fatal("%s: read error", path);
    fclose(f);
    buf[n] = 0;
    if (strlen(buf) != n)
        fatal("%s: file contains NUL bytes", path);
    return buf;
}

/* decimal, 0x hex, $ hex, or PL/M-style trailing H */
static int parse_addr(const char *s, unsigned long *out)
{
    char *end;
    int base = 0;
    size_t n = strlen(s);

    if (*s == '$') {
        s++;
        base = 16;
    } else if (n > 1 && (s[n - 1] == 'H' || s[n - 1] == 'h')) {
        base = 16;
    }
    errno = 0;
    *out = strtoul(s, &end, base);
    if (base == 16 && (*end == 'H' || *end == 'h'))
        end++;
    return *s && !*end && !errno && *out <= 0xffff;
}

int main(int argc, char **argv)
{
    const char *path = NULL, *out_path = NULL;
    int tokens = 0, ast = 0, vectors = 0;
    unsigned long org = 0x100;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--tokens")) {
            tokens = 1;
        } else if (!strcmp(a, "--ast")) {
            ast = 1;
        } else if (!strcmp(a, "--vectors")) {
            vectors = 1;
        } else if (!strcmp(a, "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(a, "--org") && i + 1 < argc) {
            if (!parse_addr(argv[++i], &org))
                fatal("bad --org address '%s'", argv[i]);
        } else if (a[0] != '-' && !path) {
            path = a;
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!path) {
        usage(stderr);
        return 2;
    }

    src_name = path;
    char *text = read_file(path);
    if (tokens) {
        lex_dump(text);
        return 0;
    }
    Program *prog = parse_program(text);
    if (ast) {
        ast_dump(prog);
        return 0;
    }
    sema_program(prog);
    (void)vectors;
    (void)out_path;
    (void)org;
    fatal("compilation is not implemented yet");
}
