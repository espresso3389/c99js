#include "type.h"
#include <stdlib.h>
#include <string.h>

/* Predefined types */
Type *ty_void;
Type *ty_bool;
Type *ty_char;
Type *ty_schar;
Type *ty_uchar;
Type *ty_short;
Type *ty_ushort;
Type *ty_int;
Type *ty_uint;
Type *ty_long;
Type *ty_ulong;
Type *ty_llong;
Type *ty_ullong;
Type *ty_float;
Type *ty_double;
Type *ty_ldouble;

static Type *new_type(Arena *a, TypeKind kind, int size, int align) {
    Type *t = arena_calloc(a, sizeof(Type));
    t->kind = kind;
    t->size = size;
    t->align = align;
    return t;
}

void type_init(Arena *a) {
    ty_void    = new_type(a, TY_VOID, 0, 1);
    ty_bool    = new_type(a, TY_BOOL, 1, 1);
    ty_bool->is_unsigned = true;

    ty_char    = new_type(a, TY_CHAR, 1, 1);
    ty_schar   = new_type(a, TY_CHAR, 1, 1);
    ty_uchar   = new_type(a, TY_CHAR, 1, 1);
    ty_uchar->is_unsigned = true;

    ty_short   = new_type(a, TY_SHORT, 2, 2);
    ty_ushort  = new_type(a, TY_SHORT, 2, 2);
    ty_ushort->is_unsigned = true;

    ty_int     = new_type(a, TY_INT, 4, 4);
    ty_uint    = new_type(a, TY_INT, 4, 4);
    ty_uint->is_unsigned = true;

    ty_long    = new_type(a, TY_LONG, 4, 4);  /* 32-bit long for JS compat */
    ty_ulong   = new_type(a, TY_LONG, 4, 4);
    ty_ulong->is_unsigned = true;

    ty_llong   = new_type(a, TY_LLONG, 8, 8);
    ty_ullong  = new_type(a, TY_LLONG, 8, 8);
    ty_ullong->is_unsigned = true;

    ty_float   = new_type(a, TY_FLOAT, 4, 4);
    ty_double  = new_type(a, TY_DOUBLE, 8, 8);
    ty_ldouble = new_type(a, TY_LDOUBLE, 8, 8); /* treat as double in JS */
}

/* ---- Type constructors ---- */
Type *type_ptr(Arena *a, Type *base) {
    Type *t = new_type(a, TY_PTR, 4, 4); /* 32-bit pointers */
    t->base = base;
    t->is_unsigned = true;
    return t;
}

Type *type_array(Arena *a, Type *base, int len) {
    Type *t = new_type(a, TY_ARRAY, base->size * len, base->align);
    t->base = base;
    t->array_len = len;
    return t;
}

Type *type_vla(Arena *a, Type *base, Node *size_expr) {
    Type *t = new_type(a, TY_VLA, 0, base->align);
    t->base = base;
    t->vla_size = size_expr;
    return t;
}

Type *type_func(Arena *a, Type *ret) {
    Type *t = new_type(a, TY_FUNC, 0, 1);
    t->return_type = ret;
    t->params = NULL;
    t->is_variadic = false;
    t->is_oldstyle = false;
    return t;
}

Type *type_enum(Arena *a, const char *tag) {
    Type *t = new_type(a, TY_ENUM, 4, 4);
    t->tag = tag;
    return t;
}

Type *type_struct(Arena *a, const char *tag) {
    Type *t = new_type(a, TY_STRUCT, 0, 1);
    t->tag = tag;
    t->members = NULL;
    return t;
}

Type *type_union(Arena *a, const char *tag) {
    Type *t = new_type(a, TY_UNION, 0, 1);
    t->tag = tag;
    t->members = NULL;
    return t;
}

Type *type_complex(Arena *a, Type *base) {
    Type *t = new_type(a, TY_COMPLEX, base->size * 2, base->align);
    t->complex_base = base;
    return t;
}

Type *type_copy(Arena *a, Type *t) {
    Type *c = arena_alloc(a, sizeof(Type));
    *c = *t;
    return c;
}

Type *type_qualified(Arena *a, Type *t, int qual) {
    if (t->qual == qual) return t;
    Type *c = type_copy(a, t);
    c->qual = qual;
    return c;
}

Type *type_unqualified(Type *t) {
    /* Return the same type pointer but conceptually ignoring qualifiers */
    return t;
}

/* ---- Type queries ---- */
bool type_is_integer(Type *t) {
    switch (t->kind) {
    case TY_BOOL: case TY_CHAR: case TY_SHORT: case TY_INT:
    case TY_LONG: case TY_LLONG: case TY_ENUM:
        return true;
    default: return false;
    }
}

bool type_is_float(Type *t) {
    return t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE;
}

bool type_is_arithmetic(Type *t) {
    return type_is_integer(t) || type_is_float(t) || t->kind == TY_COMPLEX;
}

bool type_is_scalar(Type *t) {
    return type_is_arithmetic(t) || t->kind == TY_PTR;
}

bool type_is_aggregate(Type *t) {
    return t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_ARRAY;
}

bool type_is_void(Type *t) { return t->kind == TY_VOID; }
bool type_is_ptr(Type *t) { return t->kind == TY_PTR; }
bool type_is_array(Type *t) { return t->kind == TY_ARRAY || t->kind == TY_VLA; }
bool type_is_func(Type *t) { return t->kind == TY_FUNC; }
bool type_is_struct(Type *t) { return t->kind == TY_STRUCT; }
bool type_is_union(Type *t) { return t->kind == TY_UNION; }

bool type_is_complete(Type *t) {
    if (t->kind == TY_VOID) return false;
    if (t->kind == TY_ARRAY && t->array_len < 0) return false;
    if ((t->kind == TY_STRUCT || t->kind == TY_UNION) && t->size == 0 && !t->members)
        return false;
    return true;
}

bool type_is_compatible(Type *a, Type *b) {
    if (a->kind != b->kind) return false;
    if (a->is_unsigned != b->is_unsigned) return false;

    switch (a->kind) {
    case TY_PTR:
        return type_is_compatible(a->base, b->base);
    case TY_ARRAY:
        if (a->array_len >= 0 && b->array_len >= 0 && a->array_len != b->array_len)
            return false;
        return type_is_compatible(a->base, b->base);
    case TY_FUNC: {
        if (!type_is_compatible(a->return_type, b->return_type)) return false;
        Param *pa = a->params, *pb = b->params;
        while (pa && pb) {
            if (!type_is_compatible(pa->type, pb->type)) return false;
            pa = pa->next;
            pb = pb->next;
        }
        return pa == NULL && pb == NULL;
    }
    case TY_STRUCT: case TY_UNION: case TY_ENUM:
        return a == b; /* must be same type object (same tag in same scope) */
    default:
        return true;
    }
}

/* ---- Type conversions (C99 6.3) ---- */
static int type_rank(Type *t) {
    switch (t->kind) {
    case TY_BOOL:   return 1;
    case TY_CHAR:   return 2;
    case TY_SHORT:  return 3;
    case TY_INT:    return 4;
    case TY_LONG:   return 5;
    case TY_LLONG:  return 6;
    case TY_ENUM:   return 4; /* same as int */
    default:        return 0;
    }
}

Type *type_int_promote(Arena *a, Type *t) {
    (void)a;
    if (type_rank(t) < type_rank(ty_int)) {
        /* Everything smaller than int promotes to int */
        return ty_int;
    }
    return t;
}

Type *type_usual_arith(Arena *a, Type *at, Type *bt) {
    /* If either is long double, convert to long double */
    if (at->kind == TY_LDOUBLE || bt->kind == TY_LDOUBLE) return ty_ldouble;
    if (at->kind == TY_DOUBLE || bt->kind == TY_DOUBLE) return ty_double;
    if (at->kind == TY_FLOAT || bt->kind == TY_FLOAT) return ty_float;

    /* Integer promotions */
    at = type_int_promote(a, at);
    bt = type_int_promote(a, bt);

    if (at == bt) return at;

    int ra = type_rank(at), rb = type_rank(bt);

    /* If both signed or both unsigned, convert to higher rank */
    if (at->is_unsigned == bt->is_unsigned) {
        return ra >= rb ? at : bt;
    }

    /* One signed, one unsigned */
    Type *u = at->is_unsigned ? at : bt;
    Type *s = at->is_unsigned ? bt : at;

    if (type_rank(u) >= type_rank(s)) return u;
    if (s->size > u->size) return s;

    /* Convert to unsigned version of signed type */
    if (s->kind == TY_INT) return ty_uint;
    if (s->kind == TY_LONG) return ty_ulong;
    if (s->kind == TY_LLONG) return ty_ullong;
    return ty_uint;
}

Type *type_default_arg_promote(Arena *a, Type *t) {
    if (type_is_integer(t)) return type_int_promote(a, t);
    if (t->kind == TY_FLOAT) return ty_double;
    return t;
}

int type_sizeof(Type *t) { return t->size; }
int type_alignof(Type *t) { return t->align; }

Member *type_find_member(Type *t, const char *name) {
    for (Member *m = t->members; m; m = m->next) {
        if (m->name && strcmp(m->name, name) == 0) return m;
        /* Anonymous struct/union: search recursively */
        if (!m->name && (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION)) {
            Member *found = type_find_member(m->type, name);
            if (found) return found;
        }
    }
    return NULL;
}
