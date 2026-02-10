#ifndef C99JS_CODEGEN_H
#define C99JS_CODEGEN_H

#include "ast.h"
#include "symtab.h"

/* Local variable entry for codegen */
#define CG_VAR_TABLE_SIZE 256
typedef struct CGVar {
    const char *name;
    int         addr;       /* offset from bp (negative for locals) */
    bool        is_local;
    bool        is_param;   /* parameter passed by value */
    Type       *type;
    struct CGVar *next;
} CGVar;

typedef struct {
    Arena  *arena;
    Buf     out;          /* output JavaScript buffer */
    int     indent;       /* current indentation level */
    int     label_count;  /* for generating unique labels */
    int     str_count;    /* string literal counter */
    int     tmp_count;    /* temporary variable counter */
    Buf     data_section; /* string literals and global data */
    Buf     decl_section; /* forward declarations */
    int     stack_offset; /* current stack frame offset */
    bool    in_func;      /* inside a function? */
    SymTab *symtab;

    /* Local variable map (per function) */
    CGVar  *locals[CG_VAR_TABLE_SIZE];

    /* Global variable map */
    CGVar  *globals[CG_VAR_TABLE_SIZE];

    /* goto support */
    bool    has_goto;     /* current function uses goto */
    Buf     goto_labels;  /* label → state mapping */
    int     goto_state;   /* state counter for goto */

    /* struct return support */
    Type   *current_func_ret_type; /* return type of current function */

    /* setjmp/longjmp support */
    int     setjmp_counter;   /* unique setjmp variable counter */
    int     current_setjmp_id; /* active setjmp context (-1 if none) */
} CodeGen;

void codegen_init(CodeGen *cg, Arena *a, SymTab *st);
void codegen_generate(CodeGen *cg, Node *program);
char *codegen_get_output(CodeGen *cg);

#endif /* C99JS_CODEGEN_H */
