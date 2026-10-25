/* plmc code generator */
#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

#include <stdio.h>

typedef struct CodegenOptions {
    unsigned org;       /* ORG address */
    int vectors;        /* emit the reset (and interrupt) vectors */
    int os9;            /* OS-9 program module: position-independent code,
                           U-relative data */
    unsigned stack;     /* --os9: stack space reserved in the data area */
    unsigned memory;    /* --os9: bytes of the data area for MEMORY */
    const char *name;   /* --os9: module name, or NULL for the program name */
} CodegenOptions;

/* write lwasm source for a checked program */
void codegen(FILE *f, const Program *prog, const CodegenOptions *opt);

#endif
