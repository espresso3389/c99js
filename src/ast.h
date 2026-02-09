#ifndef C99JS_AST_H
#define C99JS_AST_H

#include "type.h"
#include "lexer.h"

/* AST node kind */
typedef enum {
    /* Expressions */
    ND_INT_LIT,       /* integer constant */
    ND_FLOAT_LIT,     /* floating constant */
    ND_STRING_LIT,    /* string literal */
    ND_CHAR_LIT,      /* character constant */
    ND_IDENT,         /* identifier (variable reference) */

    /* Unary operators */
    ND_NEG,           /* -x */
    ND_POS,           /* +x */
    ND_NOT,           /* !x */
    ND_BITNOT,        /* ~x */
    ND_DEREF,         /* *x */
    ND_ADDR,          /* &x */
    ND_PRE_INC,       /* ++x */
    ND_PRE_DEC,       /* --x */
    ND_POST_INC,      /* x++ */
    ND_POST_DEC,      /* x-- */
    ND_SIZEOF,        /* sizeof(x) */
    ND_SIZEOF_TYPE,   /* sizeof(type) */

    /* Binary operators */
    ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD,
    ND_LSHIFT, ND_RSHIFT,
    ND_LT, ND_LE, ND_GT, ND_GE,
    ND_EQ, ND_NE,
    ND_BITAND, ND_BITOR, ND_BITXOR,
    ND_AND, ND_OR,

    /* Assignment */
    ND_ASSIGN,
    ND_ADD_ASSIGN, ND_SUB_ASSIGN, ND_MUL_ASSIGN, ND_DIV_ASSIGN,
    ND_MOD_ASSIGN, ND_LSHIFT_ASSIGN, ND_RSHIFT_ASSIGN,
    ND_AND_ASSIGN, ND_OR_ASSIGN, ND_XOR_ASSIGN,

    /* Other expressions */
    ND_TERNARY,       /* a ? b : c */
    ND_COMMA,         /* a, b */
    ND_CALL,          /* f(args) */
    ND_MEMBER,        /* a.b */
    ND_MEMBER_PTR,    /* a->b */
    ND_SUBSCRIPT,     /* a[b] */
    ND_CAST,          /* (type)x */
    ND_COMPOUND_LIT,  /* (type){...} */

    /* Statements */
    ND_BLOCK,         /* { ... } */
    ND_EXPR_STMT,     /* expression statement */
    ND_IF,            /* if-else */
    ND_WHILE,         /* while */
    ND_DO_WHILE,      /* do-while */
    ND_FOR,           /* for */
    ND_SWITCH,        /* switch */
    ND_CASE,          /* case expr: */
    ND_DEFAULT,       /* default: */
    ND_BREAK,
    ND_CONTINUE,
    ND_RETURN,
    ND_GOTO,
    ND_LABEL,
    ND_NULL_STMT,     /* empty statement */

    /* Declarations */
    ND_VAR_DECL,      /* variable declaration */
    ND_FUNC_DEF,      /* function definition */
    ND_TYPEDEF,       /* typedef */

    /* Initializer */
    ND_INIT_LIST,     /* { expr, expr, ... } */
    ND_DESIGNATOR,    /* .field or [index] */

    /* Translation unit */
    ND_PROGRAM,       /* top-level: list of declarations */
} NodeKind;

/* AST Node */
struct Node {
    NodeKind kind;
    Type    *type;        /* expression type (after type-checking) */
    SrcLoc   loc;

    /* Children pointers (meaning depends on kind) */
    Node *lhs;            /* left child / condition / init */
    Node *rhs;            /* right child / then-branch */
    Node *third;          /* else-branch / increment / case-body */

    /* Linked list of siblings (for block, args, params, etc.) */
    Node *next;

    /* Kind-specific data */
    union {
        /* ND_INT_LIT */
        unsigned long long ival;
        /* ND_FLOAT_LIT */
        double fval;
        /* ND_STRING_LIT */
        struct { const char *sval; int slen; };
        /* ND_CHAR_LIT */
        int cval;
        /* ND_IDENT, ND_VAR_DECL, ND_LABEL, ND_GOTO, ND_MEMBER, ND_MEMBER_PTR */
        const char *name;
        /* ND_CALL */
        struct { Node *callee; Node *args; };
        /* ND_FUNC_DEF */
        struct { const char *func_name; Node *func_params; Node *func_body;
                 StorageClass func_sc; bool func_is_inline; };
        /* ND_VAR_DECL (extended) */
        struct { const char *var_name; Node *var_init; StorageClass var_sc; int var_addr; };
        /* ND_FOR */
        struct { Node *for_init; Node *for_cond; Node *for_inc; Node *for_body; };
        /* ND_SWITCH */
        struct { Node *switch_expr; Node *switch_body;
                 Node *switch_cases; Node *switch_default; };
        /* ND_CASE */
        struct { Node *case_expr; Node *case_body; long long case_val;
                 Node *case_next; };
        /* ND_BLOCK, ND_PROGRAM, ND_INIT_LIST */
        struct { Node *body; };
        /* ND_SIZEOF_TYPE, ND_CAST, ND_COMPOUND_LIT */
        struct { Type *cast_type; Node *cast_expr; };
        /* ND_DESIGNATOR */
        struct { const char *desig_name; Node *desig_index; Node *desig_init; };
    };
};

/* Node constructors */
Node *node_new(Arena *a, NodeKind kind, SrcLoc loc);
Node *node_unary(Arena *a, NodeKind kind, Node *operand, SrcLoc loc);
Node *node_binary(Arena *a, NodeKind kind, Node *lhs, Node *rhs, SrcLoc loc);
Node *node_int_lit(Arena *a, unsigned long long val, Type *type, SrcLoc loc);
Node *node_float_lit(Arena *a, double val, Type *type, SrcLoc loc);
Node *node_string_lit(Arena *a, const char *s, int len, SrcLoc loc);
Node *node_ident(Arena *a, const char *name, SrcLoc loc);

#endif /* C99JS_AST_H */
