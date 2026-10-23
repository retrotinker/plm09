/* plmc syntax tree */
#ifndef AST_H
#define AST_H

#include "util.h"

typedef enum Type { TY_NONE, TY_BYTE, TY_ADDRESS } Type;

typedef enum Op {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_AND, OP_OR, OP_XOR,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,       /* relationals */
    OP_NEG, OP_NOT,                                 /* unary */
} Op;

typedef enum ExprKind {
    E_NUM,      /* constant */
    E_NAME,     /* variable, or call of a typed procedure without arguments */
    E_CALL,     /* name(args) */
    E_UNARY,
    E_BINARY,
} ExprKind;

struct Symbol;
struct Scope;

typedef struct Expr {
    ExprKind kind;
    Loc loc;
    Type type;              /* set by sema */
    unsigned value;         /* E_NUM */
    int is_address;         /* E_NUM: two-character string */
    char *name;             /* E_NAME, E_CALL */
    struct Symbol *sym;     /* E_NAME, E_CALL: set by sema */
    Vec args;               /* E_CALL: Expr* */
    Op op;                  /* E_UNARY, E_BINARY */
    struct Expr *lhs, *rhs; /* E_UNARY uses lhs */
} Expr;

typedef enum StmtKind {
    S_ASSIGN, S_CALL, S_IF, S_DO, S_WHILE, S_RETURN, S_HALT, S_NULL,
} StmtKind;

struct Block;

typedef struct Stmt {
    StmtKind kind;
    Loc loc;
    Vec targets;            /* S_ASSIGN: Expr* (E_NAME) */
    Expr *expr;             /* value, called procedure, condition, result */
    struct Stmt *then_s, *else_s;   /* S_IF */
    struct Block *block;    /* S_DO, S_WHILE */
    char *label;            /* S_DO: optional label */
    struct Proc *proc;      /* S_RETURN: enclosing procedure, set by sema */
} Stmt;

typedef struct VarDecl {
    char *name;
    Loc loc;
    Type type;
    struct Symbol *sym;
} VarDecl;

typedef struct Proc {
    char *name;
    Loc loc;
    Vec params;             /* VarDecl*: name and loc; type from the body */
    Type ret;               /* TY_NONE for untyped */
    int external;
    struct Block *body;
    struct Symbol *sym;
    Vec callees;            /* Proc*: set by sema */
    int visit;              /* recursion check state */
} Proc;

typedef enum DeclKind { D_VAR, D_PROC } DeclKind;

typedef struct Decl {
    DeclKind kind;
    VarDecl *var;
    Proc *proc;
} Decl;

typedef struct Block {
    Vec decls;              /* Decl*, in source order */
    Vec stmts;              /* Stmt* */
    struct Scope *scope;    /* set by sema */
} Block;

typedef struct Program {
    char *name;
    Loc loc;
    Block *body;
} Program;

Expr *new_expr(ExprKind kind, Loc loc);
Stmt *new_stmt(StmtKind kind, Loc loc);
Block *new_block(void);

const char *op_name(Op op);
const char *type_name(Type t);
int op_is_relational(Op op);

void ast_dump(const Program *prog);

#endif
