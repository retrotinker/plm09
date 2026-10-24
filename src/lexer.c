/* plmc lexer.
 *
 * Case-insensitive; identifiers and keywords are folded to upper case.
 * '$' is ignored inside identifiers and numbers.  Numbers take a base
 * suffix H, B, O, Q or D (hex must start with a digit).  Strings of up to
 * 255 characters become T_STRING tokens; the parser treats one- and
 * two-character strings as numeric constants and longer ones as LITERALLY
 * bodies.
 */
#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *src;
static const char *p;
static int line, col;
static Vec lines;           /* char * per source line */

static const char *const keywords[] = {
    "ADDRESS", "AND", "BASED", "BY", "BYTE", "CALL", "CASE", "DATA",
    "DECLARE", "DISABLE", "DO", "ELSE", "ENABLE", "END", "EOF", "EXTERNAL",
    "GO", "GOTO", "HALT", "IF", "INITIAL", "INTERRUPT", "LABEL", "LITERALLY",
    "MINUS", "MOD", "NOT", "OR", "PLUS", "PROCEDURE", "PUBLIC", "REENTRANT",
    "RETURN", "STRUCTURE", "THEN", "TO", "WHILE", "XOR",
};

static const char *const punct_names[] = {
    [T_SEMI] = ";", [T_COLON] = ":", [T_COMMA] = ",", [T_LPAREN] = "(",
    [T_RPAREN] = ")", [T_PLUS] = "+", [T_MINUS] = "-", [T_STAR] = "*",
    [T_SLASH] = "/", [T_EQ] = "=", [T_NE] = "<>", [T_LT] = "<", [T_GT] = ">",
    [T_LE] = "<=", [T_GE] = ">=", [T_DOT] = ".", [T_ASSIGN] = ":=",
};

const char *tok_name(TokType t)
{
    static char buf[8];

    if (t == T_EOF)
        return "end of file";
    if (t == T_IDENT)
        return "identifier";
    if (t == T_NUMBER)
        return "number";
    if (t == T_STRING)
        return "string";
    if (t >= K_FIRST)
        return keywords[t - K_FIRST];
    snprintf(buf, sizeof buf, "'%s'", punct_names[t]);
    return buf;
}

void lex_init(const char *text)
{
    const char *s = text, *nl;

    src = p = text;
    line = col = 1;
    lines.len = 0;
    while (*s) {
        nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        if (n && s[n - 1] == '\r')
            n--;
        vec_push(&lines, xstrndup(s, n));
        s = nl ? nl + 1 : s + strlen(s);
    }
}

const char *src_line(int n)
{
    return n >= 1 && n <= lines.len ? lines.items[n - 1] : NULL;
}

static int peekc(int k)
{
    for (int i = 0; i < k; i++)
        if (!p[i])
            return 0;
    return (unsigned char)p[k];
}

static void advance(void)
{
    if (*p == '\n') {
        line++;
        col = 1;
    } else if (*p == '\t') {
        col += 8 - (col - 1) % 8;
    } else {
        col++;
    }
    p++;
}

static Loc here(void)
{
    return (Loc){ line, col };
}

static void skip_space(void)
{
    for (;;) {
        while (*p && isspace((unsigned char)*p))
            advance();
        if (p[0] == '/' && p[1] == '*') {
            Loc start = here();
            advance();
            advance();
            while (*p && !(p[0] == '*' && p[1] == '/'))
                advance();
            if (!*p)
                error_at(start, "unterminated comment");
            advance();
            advance();
            continue;
        }
        return;
    }
}

/* scan a word of letters, digits and '$'; returns it upper-cased with the
   '$' characters removed */
static char *scan_word(void)
{
    size_t cap = 16, n = 0;
    char *w = xmalloc(cap);

    while (isalnum((unsigned char)*p) || *p == '$') {
        if (*p != '$') {
            if (n + 1 >= cap)
                w = xrealloc(w, cap *= 2);
            w[n++] = (char)toupper((unsigned char)*p);
        }
        advance();
    }
    w[n] = 0;
    return w;
}

static unsigned number_value(Loc loc, const char *w)
{
    size_t n = strlen(w);
    unsigned base = 10, v = 0;
    char suffix = w[n - 1];

    if (!isdigit((unsigned char)suffix)) {
        switch (suffix) {
        case 'H': base = 16; break;
        case 'B': base = 2; break;
        case 'O': case 'Q': base = 8; break;
        case 'D': base = 10; break;
        default:
            error_at(loc, "invalid number '%s'", w);
        }
        n--;
    }
    if (n == 0)
        error_at(loc, "invalid number '%s'", w);
    for (size_t i = 0; i < n; i++) {
        int c = w[i];
        unsigned d = isdigit(c) ? (unsigned)(c - '0')
                   : (c >= 'A' && c <= 'F') ? (unsigned)(c - 'A' + 10) : 99;
        if (d >= base)
            error_at(loc, "invalid digit '%c' in number '%s'", c, w);
        v = v * base + d;
        if (v > 0xffff)
            error_at(loc, "number '%s' does not fit in 16 bits", w);
    }
    return v;
}

static Token scan_string(Loc loc)
{
    Token t = { T_STRING, loc, NULL, 0, 0, 0, 0 };
    char buf[256];
    int n = 0;

    advance();                          /* opening quote */
    for (;;) {
        if (!*p || *p == '\n')
            error_at(loc, "unterminated string");
        if (*p == '\'') {
            if (peekc(1) != '\'')
                break;
            advance();                  /* '' is a quote character */
        }
        if (n == 255)
            error_at(loc, "strings are limited to 255 characters");
        buf[n++] = *p;
        if (n <= 2)
            t.value = t.value << 8 | (unsigned char)*p;
        advance();
    }
    advance();                          /* closing quote */
    if (n == 0)
        error_at(loc, "empty string");
    t.text = xstrndup(buf, n);
    t.is_address = n == 2;
    return t;
}

Token lex_next(void)
{
    Token t = { T_EOF, { 0, 0 }, NULL, 0, 0, 0, 0 };
    int c;

    skip_space();
    t.loc = here();
    c = (unsigned char)*p;
    if (!c)
        return t;

    if (isalpha(c)) {
        t.text = scan_word();
        t.type = T_IDENT;
        for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++)
            if (!strcmp(t.text, keywords[i]))
                t.type = (TokType)(K_FIRST + i);
        return t;
    }
    if (isdigit(c)) {
        char *w = scan_word();
        t.type = T_NUMBER;
        t.value = number_value(t.loc, w);
        free(w);
        return t;
    }
    if (c == '\'')
        return scan_string(t.loc);

    advance();
    switch (c) {
    case ';': t.type = T_SEMI; break;
    case ',': t.type = T_COMMA; break;
    case '(': t.type = T_LPAREN; break;
    case ')': t.type = T_RPAREN; break;
    case '+': t.type = T_PLUS; break;
    case '-': t.type = T_MINUS; break;
    case '*': t.type = T_STAR; break;
    case '/': t.type = T_SLASH; break;
    case '=': t.type = T_EQ; break;
    case '.': t.type = T_DOT; break;
    case ':':
        t.type = T_COLON;
        if (*p == '=') {
            advance();
            t.type = T_ASSIGN;
        }
        break;
    case '<':
        t.type = T_LT;
        if (*p == '>') {
            advance();
            t.type = T_NE;
        } else if (*p == '=') {
            advance();
            t.type = T_LE;
        }
        break;
    case '>':
        t.type = T_GT;
        if (*p == '=') {
            advance();
            t.type = T_GE;
        }
        break;
    default:
        if (isprint(c))
            error_at(t.loc, "invalid character '%c'", c);
        error_at(t.loc, "invalid character (code %d)", c);
    }
    return t;
}

Vec lex_tokens(const char *text, Loc at)
{
    const char *save_p = p;
    int save_line = line, save_col = col;
    Vec v = { 0 };
    Token t;

    p = text;
    line = at.line;
    col = at.col;
    while ((t = lex_next()).type != T_EOF) {
        Token *copy = xmalloc(sizeof *copy);
        t.loc = at;
        *copy = t;
        vec_push(&v, copy);
    }
    p = save_p;
    line = save_line;
    col = save_col;
    return v;
}

void lex_dump(const char *text)
{
    Token t;

    lex_init(text);
    do {
        t = lex_next();
        printf("%d:%d ", t.loc.line, t.loc.col);
        switch (t.type) {
        case T_EOF:
            printf("EOF\n");
            break;
        case T_IDENT:
            printf("IDENT %s\n", t.text);
            break;
        case T_NUMBER:
            printf("NUMBER %u\n", t.value);
            break;
        case T_STRING:
            if (strlen(t.text) <= 2)
                printf("STRING '%s' = %u%s\n", t.text, t.value, t.is_address ? " ADDRESS" : "");
            else
                printf("STRING '%s'\n", t.text);
            break;
        default:
            if (t.type >= K_FIRST)
                printf("KEYWORD %s\n", tok_name(t.type));
            else
                printf("PUNCT %s\n", punct_names[t.type]);
        }
    } while (t.type != T_EOF);
}
