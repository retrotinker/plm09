/* plmc: PL/M (tiny core) to 6809 lwasm compiler -- driver */
#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

#include <ctype.h>
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
            "  --os9         generate a position-independent OS-9 program module\n"
            "  --stack N     with --os9: stack bytes in the data area (default 256)\n"
            "  --name NAME   with --os9: module name (default: the program's name)\n"
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

/* OS-9 module names: a letter, then letters, digits, '_', '.' or '$' */
static int valid_module_name(const char *s)
{
    size_t n = strlen(s);

    if (n < 1 || n > 29 || !isalpha((unsigned char)s[0]))
        return 0;
    for (size_t i = 1; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && !strchr("_.$", s[i]))
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *path = NULL, *out_path = NULL, *mod_name = NULL;
    int tokens = 0, ast = 0, vectors = 0, os9 = 0, org_given = 0, stack_given = 0;
    unsigned long org = 0x100, stack = 256;

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
        } else if (!strcmp(a, "--os9")) {
            os9 = 1;
        } else if (!strcmp(a, "--name") && i + 1 < argc) {
            mod_name = argv[++i];
            if (!valid_module_name(mod_name))
                fatal("bad --name '%s' (1-29 letters, digits, '_', '.' or '$', starting with a letter)",
                      mod_name);
        } else if (!strcmp(a, "--stack") && i + 1 < argc) {
            if (!parse_addr(argv[++i], &stack) || stack < 32)
                fatal("bad --stack size '%s' (32 to 65535 bytes)", argv[i]);
            stack_given = 1;
        } else if (!strcmp(a, "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(a, "--org") && i + 1 < argc) {
            if (!parse_addr(argv[++i], &org))
                fatal("bad --org address '%s'", argv[i]);
            org_given = 1;
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

    if (os9 && (org_given || vectors))
        fatal("--org and --vectors cannot be used with --os9 (OS-9 modules are position-independent)");
    if (stack_given && !os9)
        fatal("--stack is only used with --os9");
    if (mod_name && !os9)
        fatal("--name is only used with --os9");

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

    CodegenOptions opt = { (unsigned)org, vectors, os9, (unsigned)stack, mod_name };
    FILE *f = stdout;
    if (out_path && !(f = fopen(out_path, "w")))
        fatal("%s: %s", out_path, strerror(errno));
    codegen(f, prog, &opt);
    if (ferror(f) || (f != stdout && fclose(f) != 0)) {
        if (out_path)
            remove(out_path);
        fatal("error writing %s", out_path ? out_path : "output");
    }
    return 0;
}
