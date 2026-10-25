/* plmc syntax tree */
#ifndef AST_H
#define AST_H

#include "util.h"

typedef enum Type { TY_NONE, TY_BYTE, TY_ADDRESS, TY_STRUCT } Type;

/* a STRUCTURE type: members in declaration order, contiguous */
typedef struct Member {
    char *name;
    Loc loc;
    Type type;              /* TY_BYTE or TY_ADDRESS */
    unsigned dim;           /* 0 for a scalar member */
    unsigned offset;        /* bytes from the start of the structure */
} Member;

typedef struct StructType {
    Vec members;            /* Member* */
    unsigned size;          /* bytes; set by sema */
} StructType;

typedef enum Op {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_AND, OP_OR, OP_XOR,
    OP_PLUS, OP_MINUS,                              /* with carry / borrow */
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,       /* relationals */
    OP_NEG, OP_NOT,                                 /* unary */
} Op;

typedef enum ExprKind {
    E_NUM,      /* constant */
    E_NAME,     /* variable, or call of a typed procedure without arguments */
    E_CALL,     /* name(args) */
    E_UNARY,
    E_BINARY,
    E_INDEX,    /* array element: name(lhs) */
    E_ADDR,     /* .lhs (a variable, element or procedure), or .(constants) */
    E_MEMBER,   /* name[(lhs)].mname[(rhs)]: a structure member */
    E_BUILTIN,  /* HIGH LOW DOUBLE (lhs), SHL SHR ROL ROR (lhs, rhs = count);
                   made from an E_CALL by sema */
} ExprKind;

typedef enum Builtin {
    BI_HIGH, BI_LOW, BI_DOUBLE, BI_SHL, BI_SHR, BI_ROL, BI_ROR,
} Builtin;

struct Symbol;
struct Scope;

typedef struct Expr {
    ExprKind kind;
    Loc loc;
    Type type;              /* set by sema */
    unsigned value;         /* E_NUM */
    int is_address;         /* E_NUM: two-character string */
    char *name;             /* E_NAME, E_CALL, E_INDEX */
    struct Symbol *sym;     /* E_NAME, E_CALL, E_INDEX: set by sema */
    Vec args;               /* E_CALL: Expr* */
    Op op;                  /* E_UNARY, E_BINARY */
    Builtin builtin;        /* E_BUILTIN */
    struct Expr *lhs, *rhs; /* E_UNARY uses lhs; E_INDEX: lhs = subscript;
                               E_ADDR: lhs = operand, or NULL with data */
    char *mname;            /* E_MEMBER: member name */
    struct Member *member;  /* E_MEMBER: set by sema */
    unsigned char *data;    /* E_ADDR of a constant list: the bytes */
    int ndata;
} Expr;

typedef enum StmtKind {
    S_ASSIGN, S_CALL, S_IF, S_DO, S_WHILE, S_ITER, S_CASE, S_RETURN, S_HALT, S_NULL,
    S_ENABLE, S_DISABLE,
    S_GOTO,
} StmtKind;

struct Block;

typedef struct Stmt {
    StmtKind kind;
    Loc loc;
    Vec targets;            /* S_ASSIGN: Expr* (E_NAME or E_INDEX) */
    Expr *expr;             /* value, called procedure, condition, result,
                               S_CASE selector */
    Expr *var, *from, *to, *by;     /* S_ITER: DO var = from TO to [BY by] */
    struct Stmt *then_s, *else_s;   /* S_IF */
    struct Block *block;    /* S_DO, S_WHILE, S_ITER; S_CASE: stmts = cases */
    Vec labels;             /* Expr* (E_NAME): labels on this statement */
    int goto_out;           /* S_GOTO (expr = label) leaving a procedure */
    struct Proc *proc;      /* S_RETURN: enclosing procedure, set by sema */
} Stmt;

/* INITIAL (...) / DATA (...) value list, shared by the names of one
   declaration (the values fill them in order) */
typedef enum InitKind { INIT_NONE, INIT_INITIAL, INIT_DATA } InitKind;

typedef struct InitItem {
    Loc loc;
    char *str;              /* a string item (any length), or NULL */
    int len;
    struct Expr *expr;      /* otherwise a restricted expression */
} InitItem;

typedef struct InitList {
    InitKind kind;
    Loc loc;
    Vec items;              /* InitItem* */
    int item, chr;          /* sema: next item, and next character of it */
} InitList;

/* one initialized scalar: a number, or an address (an E_ADDR) + value */
typedef struct InitVal {
    unsigned size;          /* 1 (BYTE) or 2 (ADDRESS) */
    unsigned value;
    struct Expr *addr;      /* a location reference, or NULL */
} InitVal;

/* AT (restricted expression), shared by the names of one declaration:
   the first is placed there, the others follow in sequence */
typedef struct AtSpec {
    struct Expr *expr;
    Loc loc;
    int done;               /* sema: evaluated into val */
    InitVal val;
    unsigned next;          /* sema: offset of the next name */
} AtSpec;

typedef struct VarDecl {
    char *name;
    Loc loc;
    Type type;
    unsigned dim;           /* number of elements; 0 for a scalar */
    Loc dim_loc;
    StructType *st;         /* STRUCTURE variable (type TY_STRUCT) */
    int implicit_dim;       /* (*): dim comes from the INITIAL/DATA list */
    InitList *init;         /* INITIAL/DATA values, or NULL */
    AtSpec *at;             /* AT (...), or NULL */
    char *base;             /* BASED variable: name of its base, else NULL */
    Loc base_loc;
    struct Symbol *sym;
} VarDecl;

typedef struct Proc {
    char *name;
    Loc loc;
    Vec params;             /* VarDecl*: name and loc; type from the body */
    Type ret;               /* TY_NONE for untyped */
    int external;
    int reentrant;          /* REENTRANT: parameters and locals in a stack frame */
    Loc reentrant_loc;
    int interrupt;          /* INTERRUPT n: n + 1, or 0 */
    Loc interrupt_loc;
    struct Block *body;
    struct Symbol *sym;
    Vec callees;            /* Proc*: set by sema */
    int visit;              /* recursion check: last search that visited it */
    unsigned frame_size;    /* REENTRANT: bytes of locals below the saved Y */
    unsigned arg_size;      /* REENTRANT: bytes of arguments the caller pushes */
} Proc;

typedef enum DeclKind { D_VAR, D_PROC, D_LABEL } DeclKind;

/* attribute of a LABEL declaration */
typedef enum LabelAttr { LBL_PLAIN, LBL_PUBLIC, LBL_EXTERNAL } LabelAttr;

typedef struct Decl {
    DeclKind kind;
    VarDecl *var;           /* D_VAR; for D_LABEL only name and loc */
    Proc *proc;
    LabelAttr attr;         /* D_LABEL */
    Loc attr_loc;
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
