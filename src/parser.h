/* plmc parser */
#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

/* parse a whole program; syntax errors are fatal */
Program *parse_program(const char *text);

#endif
