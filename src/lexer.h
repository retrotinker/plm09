/* plmc lexer */
#ifndef LEXER_H
#define LEXER_H

#include "util.h"

typedef enum TokType {
    T_EOF, T_IDENT, T_NUMBER, T_STRING,
    /* punctuation */
    T_SEMI, T_COLON, T_COMMA, T_LPAREN, T_RPAREN, T_PLUS, T_MINUS, T_STAR,
    T_SLASH, T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE, T_DOT, T_ASSIGN,
    /* keywords: every PL/M-80 reserved word, used or not */
    K_FIRST,
    K_ADDRESS = K_FIRST, K_AND, K_AT, K_BASED, K_BY, K_BYTE, K_CALL, K_CASE, K_DATA,
    K_DECLARE, K_DISABLE, K_DO, K_ELSE, K_ENABLE, K_END, K_EOF, K_EXTERNAL,
    K_GO, K_GOTO, K_HALT, K_IF, K_INITIAL, K_INTERRUPT, K_LABEL, K_LITERALLY,
    K_MINUS, K_MOD, K_NOT, K_OR, K_PLUS, K_PROCEDURE, K_PUBLIC, K_REENTRANT,
    K_RETURN, K_STRUCTURE, K_THEN, K_TO, K_WHILE, K_XOR,
    T_COUNT
} TokType;

typedef struct Token {
    TokType type;
    Loc loc;
    char *text;         /* T_IDENT: name (upper case, '$' removed);
                           T_STRING: contents ('' already turned into ') */
    unsigned value;     /* T_NUMBER; T_STRING of 1-2 characters */
    int is_address;     /* T_STRING of 2 characters (an ADDRESS constant) */
    int depth;          /* LITERALLY expansion depth (parser) */
    int raw;            /* never expand this token (parser) */
} Token;

void lex_init(const char *src);
Token lex_next(void);

/* a directory to look for INCLUDE files in (-I), after the including
   file's own */
void lex_add_include_dir(const char *dir);

/* tokenize text (a LITERALLY body) into a Vec of Token*, all located at
   `at`; the main input's position is saved and restored */
Vec lex_tokens(const char *text, Loc at);

/* describe a token type for messages: "';'", "DO", "identifier" */
const char *tok_name(TokType t);

/* the source line at loc (its file and line) without its newline, or NULL */
const char *src_line(Loc loc);

/* --tokens output */
void lex_dump(const char *src);

#endif
