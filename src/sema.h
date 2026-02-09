#ifndef C99JS_SEMA_H
#define C99JS_SEMA_H

#include "ast.h"
#include "symtab.h"

typedef struct {
    Arena  *arena;
    SymTab *symtab;
    Type   *current_func_type; /* return type of current function */
} Sema;

void sema_init(Sema *s, Arena *a, SymTab *st);
void sema_check(Sema *s, Node *program);

#endif /* C99JS_SEMA_H */
