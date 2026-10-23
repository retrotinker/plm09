/* plmc utilities: memory, diagnostics */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

typedef struct Loc {
    int line, col;
} Loc;

extern const char *src_name;    /* file name used in diagnostics */
extern int warning_count;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* "file:line:col: error: msg" and exit(1) */
_Noreturn void error_at(Loc loc, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void warning_at(Loc loc, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
_Noreturn void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* growable pointer array */
typedef struct Vec {
    void **items;
    int len, cap;
} Vec;

void vec_push(Vec *v, void *item);

#endif
