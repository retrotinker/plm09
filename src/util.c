/* plmc utilities: memory, diagnostics */
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *src_name = "<input>";
int warning_count;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void report(Loc loc, const char *kind, const char *fmt, va_list ap)
{
    fprintf(stderr, "%s:%d:%d: %s: ", src_name, loc.line, loc.col, kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void error_at(Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(loc, "error", fmt, ap);
    va_end(ap);
    exit(1);
}

void warning_at(Loc loc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(loc, "warning", fmt, ap);
    va_end(ap);
    warning_count++;
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "plmc: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

void vec_push(Vec *v, void *item)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 8;
        v->items = xrealloc(v->items, v->cap * sizeof *v->items);
    }
    v->items[v->len++] = item;
}
