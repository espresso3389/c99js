#include "ast.h"
#include <string.h>

Node *node_new(Arena *a, NodeKind kind, SrcLoc loc) {
    Node *n = arena_calloc(a, sizeof(Node));
    n->kind = kind;
    n->loc = loc;
    return n;
}

Node *node_unary(Arena *a, NodeKind kind, Node *operand, SrcLoc loc) {
    Node *n = node_new(a, kind, loc);
    n->lhs = operand;
    return n;
}

Node *node_binary(Arena *a, NodeKind kind, Node *lhs, Node *rhs, SrcLoc loc) {
    Node *n = node_new(a, kind, loc);
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}

Node *node_int_lit(Arena *a, unsigned long long val, Type *type, SrcLoc loc) {
    Node *n = node_new(a, ND_INT_LIT, loc);
    n->ival = val;
    n->type = type;
    return n;
}

Node *node_float_lit(Arena *a, double val, Type *type, SrcLoc loc) {
    Node *n = node_new(a, ND_FLOAT_LIT, loc);
    n->fval = val;
    n->type = type;
    return n;
}

Node *node_string_lit(Arena *a, const char *s, int len, SrcLoc loc) {
    Node *n = node_new(a, ND_STRING_LIT, loc);
    n->sval = s;
    n->slen = len;
    n->type = type_array(a, ty_char, len + 1);
    return n;
}

Node *node_ident(Arena *a, const char *name, SrcLoc loc) {
    Node *n = node_new(a, ND_IDENT, loc);
    n->name = name;
    return n;
}
