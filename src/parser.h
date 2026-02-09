#ifndef C99JS_PARSER_H
#define C99JS_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "symtab.h"

typedef struct {
    Lexer  *lexer;
    Arena  *arena;
    SymTab *symtab;
    int     loop_depth;    /* nesting level for break/continue */
    int     switch_depth;  /* nesting level for switch */
} Parser;

void  parser_init(Parser *p, Lexer *l, Arena *a, SymTab *st);
Node *parser_parse(Parser *p); /* returns ND_PROGRAM */

#endif /* C99JS_PARSER_H */
