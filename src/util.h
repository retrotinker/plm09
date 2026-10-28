/* plmc utilities: memory, diagnostics */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

typedef struct Loc {
    int line, col;
    int file;           /* index in src_files: 0 the main file, then INCLUDE files */
} Loc;

/* growable pointer array */
typedef struct Vec {
    void **items;
    int len, cap;
} Vec;

extern const char *src_name;    /* the main file's name */
extern Vec src_files;           /* char * per source file (INCLUDE), [0] = src_name */
extern int warning_count;

/* the name of loc's file, for messages */
const char *loc_file(Loc loc);

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

/* a whole text file, or NULL (errno set) if it can't be opened; fatal on
   read errors and NUL bytes */
char *read_text(const char *path);

void vec_push(Vec *v, void *item);

#endif
