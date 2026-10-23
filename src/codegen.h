/* plmc code generator */
#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

#include <stdio.h>

typedef struct CodegenOptions {
    unsigned org;       /* ORG address */
    int vectors;        /* emit FDB RT_START at $FFFE */
} CodegenOptions;

/* write lwasm source for a checked program */
void codegen(FILE *f, const Program *prog, const CodegenOptions *opt);

#endif
