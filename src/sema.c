#include "sema.h"
#include <string.h>

static void check_node(Sema *s, Node *n);
static void check_expr(Sema *s, Node *n);

void sema_init(Sema *s, Arena *a, SymTab *st) {
    s->arena = a;
    s->symtab = st;
    s->current_func_type = NULL;
}

/* Ensure node has a type; assign default if missing */
static void ensure_type(Sema *s, Node *n) {
    if (!n || n->type) return;
    (void)s;
    n->type = ty_int; /* fallback */
}

/* Array-to-pointer decay: char[] -> char*, etc. */
static void decay_array(Sema *s, Node *n) {
    if (n && n->type && n->type->kind == TY_ARRAY)
        n->type = type_ptr(s->arena, n->type->base);
}

/* Insert implicit cast if types differ */
static Node *implicit_cast(Sema *s, Node *n, Type *target) {
    if (!n || !n->type || !target) return n;
    if (type_is_compatible(n->type, target)) return n;

    Node *cast = node_new(s->arena, ND_CAST, n->loc);
    cast->cast_type = target;
    cast->cast_expr = n;
    cast->type = target;
    return cast;
}

static void check_expr(Sema *s, Node *n) {
    if (!n) return;

    switch (n->kind) {
    case ND_INT_LIT:
    case ND_FLOAT_LIT:
    case ND_CHAR_LIT:
    case ND_STRING_LIT:
        /* Already typed by parser */
        break;

    case ND_IDENT: {
        if (!n->type) {
            Symbol *sym = symtab_lookup(s->symtab, n->name);
            if (sym) {
                n->type = sym->type;
            } else {
                error_at(n->loc, "undeclared identifier '%s'", n->name);
                n->type = ty_int;
            }
        }
        break;
    }

    case ND_NEG: case ND_POS:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (!type_is_arithmetic(n->lhs->type))
            error_at(n->loc, "operand of unary +/- must be arithmetic");
        n->type = type_int_promote(s->arena, n->lhs->type);
        break;

    case ND_NOT:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (!type_is_scalar(n->lhs->type))
            error_at(n->loc, "operand of ! must be scalar");
        n->type = ty_int;
        break;

    case ND_BITNOT:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (!type_is_integer(n->lhs->type))
            error_at(n->loc, "operand of ~ must be integer");
        n->type = type_int_promote(s->arena, n->lhs->type);
        break;

    case ND_DEREF:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (!type_is_ptr(n->lhs->type) && !type_is_array(n->lhs->type))
            error_at(n->loc, "cannot dereference non-pointer type");
        else
            n->type = n->lhs->type->base;
        break;

    case ND_ADDR:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        n->type = type_ptr(s->arena, n->lhs->type);
        break;

    case ND_PRE_INC: case ND_PRE_DEC:
    case ND_POST_INC: case ND_POST_DEC:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        n->type = n->lhs->type;
        break;

    case ND_SIZEOF:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        n->type = ty_uint;
        break;

    case ND_SIZEOF_TYPE:
        n->type = ty_uint;
        break;

    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        decay_array(s, n->lhs);
        decay_array(s, n->rhs);

        /* Pointer arithmetic */
        if (n->kind == ND_ADD || n->kind == ND_SUB) {
            if (type_is_ptr(n->lhs->type) && type_is_integer(n->rhs->type)) {
                n->type = n->lhs->type;
                break;
            }
            if (type_is_integer(n->lhs->type) && type_is_ptr(n->rhs->type) && n->kind == ND_ADD) {
                n->type = n->rhs->type;
                break;
            }
            if (type_is_ptr(n->lhs->type) && type_is_ptr(n->rhs->type) && n->kind == ND_SUB) {
                n->type = ty_long; /* ptrdiff_t */
                break;
            }
        }

        if (!type_is_arithmetic(n->lhs->type) || !type_is_arithmetic(n->rhs->type)) {
            error_at(n->loc, "invalid operands to binary expression");
        }
        n->type = type_usual_arith(s->arena, n->lhs->type, n->rhs->type);
        break;

    case ND_LSHIFT: case ND_RSHIFT:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        n->type = type_int_promote(s->arena, n->lhs->type);
        break;

    case ND_LT: case ND_LE: case ND_GT: case ND_GE:
    case ND_EQ: case ND_NE:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        decay_array(s, n->lhs);
        decay_array(s, n->rhs);
        n->type = ty_int;
        break;

    case ND_BITAND: case ND_BITOR: case ND_BITXOR:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        n->type = type_usual_arith(s->arena, n->lhs->type, n->rhs->type);
        break;

    case ND_AND: case ND_OR:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        n->type = ty_int;
        break;

    case ND_TERNARY:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        check_expr(s, n->third);
        ensure_type(s, n->rhs);
        ensure_type(s, n->third);
        /* Result type: common type of rhs and third */
        if (type_is_arithmetic(n->rhs->type) && type_is_arithmetic(n->third->type)) {
            n->type = type_usual_arith(s->arena, n->rhs->type, n->third->type);
        } else {
            n->type = n->rhs->type;
        }
        break;

    case ND_ASSIGN:
    case ND_ADD_ASSIGN: case ND_SUB_ASSIGN:
    case ND_MUL_ASSIGN: case ND_DIV_ASSIGN: case ND_MOD_ASSIGN:
    case ND_LSHIFT_ASSIGN: case ND_RSHIFT_ASSIGN:
    case ND_AND_ASSIGN: case ND_OR_ASSIGN: case ND_XOR_ASSIGN:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        decay_array(s, n->rhs);
        n->rhs = implicit_cast(s, n->rhs, n->lhs->type);
        n->type = n->lhs->type;
        break;

    case ND_COMMA:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->rhs);
        n->type = n->rhs->type;
        break;

    case ND_CALL:
        check_expr(s, n->callee);
        for (Node *arg = n->args; arg; arg = arg->next)
            check_expr(s, arg);
        /* Derive return type from callee's function/function-pointer type.
         * The parser may have defaulted to int if the callee type wasn't
         * available yet (e.g. struct member function pointers). */
        if (n->callee && n->callee->type) {
            Type *ct = n->callee->type;
            if (ct->kind == TY_PTR && ct->base) ct = ct->base;
            if (ct->kind == TY_FUNC && ct->return_type)
                n->type = ct->return_type;
        }
        ensure_type(s, n);
        break;

    case ND_MEMBER:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (n->lhs->type && (n->lhs->type->kind == TY_STRUCT || n->lhs->type->kind == TY_UNION)) {
            Member *m = type_find_member(n->lhs->type, n->name);
            if (m) n->type = m->type;
            else error_at(n->loc, "no member '%s'", n->name);
        }
        break;

    case ND_MEMBER_PTR:
        check_expr(s, n->lhs);
        ensure_type(s, n->lhs);
        if (n->lhs->type && type_is_ptr(n->lhs->type)) {
            Type *base = n->lhs->type->base;
            if (base->kind == TY_STRUCT || base->kind == TY_UNION) {
                Member *m = type_find_member(base, n->name);
                if (m) n->type = m->type;
                else error_at(n->loc, "no member '%s'", n->name);
            }
        }
        break;

    case ND_SUBSCRIPT:
        check_expr(s, n->lhs);
        check_expr(s, n->rhs);
        ensure_type(s, n->lhs);
        ensure_type(s, n->rhs);
        /* a[b] = *(a+b) */
        if (type_is_ptr(n->lhs->type) || type_is_array(n->lhs->type))
            n->type = n->lhs->type->base;
        else if (type_is_ptr(n->rhs->type) || type_is_array(n->rhs->type))
            n->type = n->rhs->type->base;
        else
            error_at(n->loc, "subscript requires array or pointer");
        break;

    case ND_CAST:
        check_expr(s, n->cast_expr);
        n->type = n->cast_type;
        break;

    case ND_COMPOUND_LIT:
        if (n->cast_expr) check_node(s, n->cast_expr);
        n->type = n->cast_type;
        break;

    default:
        break;
    }
}

static void check_node(Sema *s, Node *n) {
    if (!n) return;

    switch (n->kind) {
    case ND_PROGRAM:
        for (Node *d = n->body; d; d = d->next)
            check_node(s, d);
        break;

    case ND_FUNC_DEF: {
        Type *prev = s->current_func_type;
        s->current_func_type = n->type->return_type;
        check_node(s, n->func_body);
        s->current_func_type = prev;
        break;
    }

    case ND_VAR_DECL:
        if (n->var_init) {
            if (n->var_init->kind == ND_INIT_LIST) {
                check_node(s, n->var_init);
            } else {
                check_expr(s, n->var_init);
                ensure_type(s, n->var_init);
                /* Don't insert implicit cast for char array init from string literal */
                if (n->var_init->kind == ND_STRING_LIT &&
                    n->type && (n->type->kind == TY_ARRAY || n->type->kind == TY_VLA) &&
                    n->type->base && n->type->base->kind == TY_CHAR) {
                    /* keep as-is: codegen handles char arr = "str" specially */
                } else {
                    n->var_init = implicit_cast(s, n->var_init, n->type);
                }
            }
        }
        break;

    case ND_BLOCK:
        for (Node *stmt = n->body; stmt; stmt = stmt->next)
            check_node(s, stmt);
        break;

    case ND_EXPR_STMT:
        check_expr(s, n->lhs);
        break;

    case ND_IF:
        check_expr(s, n->lhs);
        check_node(s, n->rhs);
        check_node(s, n->third);
        break;

    case ND_WHILE:
    case ND_DO_WHILE:
        check_expr(s, n->lhs);
        check_node(s, n->rhs);
        break;

    case ND_FOR:
        if (n->for_init) {
            if (n->for_init->kind == ND_VAR_DECL)
                check_node(s, n->for_init);
            else
                check_expr(s, n->for_init);
        }
        if (n->for_cond) check_expr(s, n->for_cond);
        if (n->for_inc) check_expr(s, n->for_inc);
        check_node(s, n->for_body);
        break;

    case ND_SWITCH:
        check_expr(s, n->switch_expr);
        check_node(s, n->switch_body);
        break;

    case ND_CASE:
        check_expr(s, n->case_expr);
        check_node(s, n->case_body);
        break;

    case ND_DEFAULT:
        check_node(s, n->lhs);
        break;

    case ND_RETURN:
        if (n->lhs) {
            check_expr(s, n->lhs);
            ensure_type(s, n->lhs);
            if (s->current_func_type && !type_is_void(s->current_func_type))
                n->lhs = implicit_cast(s, n->lhs, s->current_func_type);
        }
        break;

    case ND_LABEL:
        check_node(s, n->lhs);
        break;

    case ND_INIT_LIST:
        for (Node *item = n->body; item; item = item->next) {
            if (item->kind == ND_DESIGNATOR) {
                if (item->desig_init) check_node(s, item->desig_init);
            } else {
                check_expr(s, item);
            }
        }
        break;

    case ND_TYPEDEF:
    case ND_NULL_STMT:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
        break;

    default:
        /* Expression node used as statement */
        check_expr(s, n);
        break;
    }
}

void sema_check(Sema *s, Node *program) {
    check_node(s, program);
}
