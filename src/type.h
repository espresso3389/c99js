#ifndef C99JS_TYPE_H
#define C99JS_TYPE_H

#include "util.h"
#include <stdbool.h>

/* Forward declarations */
typedef struct Type Type;
typedef struct Node Node;

/* Type kind */
typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_LLONG,     /* long long */
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,   /* long double */
    TY_ENUM,
    TY_PTR,
    TY_ARRAY,
    TY_VLA,        /* variable-length array */
    TY_STRUCT,
    TY_UNION,
    TY_FUNC,
    TY_COMPLEX,    /* _Complex variants */
} TypeKind;

/* Type qualifiers */
#define QUAL_CONST    0x01
#define QUAL_VOLATILE 0x02
#define QUAL_RESTRICT 0x04

/* Storage class */
typedef enum {
    SC_NONE = 0,
    SC_TYPEDEF,
    SC_EXTERN,
    SC_STATIC,
    SC_AUTO,
    SC_REGISTER,
} StorageClass;

/* Struct/union member */
typedef struct Member {
    struct Member *next;
    const char *name;
    Type *type;
    int offset;
    int bit_width;    /* -1 if not a bitfield */
    int bit_offset;
    int idx;          /* member index */
} Member;

/* Function parameter */
typedef struct Param {
    struct Param *next;
    const char *name;
    Type *type;
} Param;

/* Type structure */
struct Type {
    TypeKind kind;
    int      size;        /* size in bytes */
    int      align;       /* alignment in bytes */
    bool     is_unsigned;
    int      qual;        /* QUAL_CONST, etc. */
    bool     is_inline;

    /* TY_PTR */
    Type *base;

    /* TY_ARRAY */
    int   array_len;    /* -1 for incomplete */
    Node *vla_size;     /* for VLA: expression for size */

    /* TY_STRUCT / TY_UNION */
    const char *tag;
    Member *members;
    bool is_flexible;   /* has flexible array member */
    bool is_packed;

    /* TY_FUNC */
    Type  *return_type;
    Param *params;
    bool   is_variadic;
    bool   is_oldstyle;

    /* TY_ENUM */
    /* uses tag from struct section */

    /* TY_COMPLEX */
    Type *complex_base;  /* float, double, or long double */
};

/* Predefined types */
extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_char;
extern Type *ty_schar;
extern Type *ty_uchar;
extern Type *ty_short;
extern Type *ty_ushort;
extern Type *ty_int;
extern Type *ty_uint;
extern Type *ty_long;
extern Type *ty_ulong;
extern Type *ty_llong;
extern Type *ty_ullong;
extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;

void type_init(Arena *a);

/* Type constructors */
Type *type_ptr(Arena *a, Type *base);
Type *type_array(Arena *a, Type *base, int len);
Type *type_vla(Arena *a, Type *base, Node *size_expr);
Type *type_func(Arena *a, Type *ret);
Type *type_enum(Arena *a, const char *tag);
Type *type_struct(Arena *a, const char *tag);
Type *type_union(Arena *a, const char *tag);
Type *type_complex(Arena *a, Type *base);
Type *type_copy(Arena *a, Type *t);
Type *type_qualified(Arena *a, Type *t, int qual);
Type *type_unqualified(Type *t);

/* Type queries */
bool type_is_integer(Type *t);
bool type_is_float(Type *t);
bool type_is_arithmetic(Type *t);
bool type_is_scalar(Type *t);
bool type_is_aggregate(Type *t);
bool type_is_void(Type *t);
bool type_is_ptr(Type *t);
bool type_is_array(Type *t);
bool type_is_func(Type *t);
bool type_is_struct(Type *t);
bool type_is_union(Type *t);
bool type_is_compatible(Type *a, Type *b);
bool type_is_complete(Type *t);

/* Type conversions */
Type *type_usual_arith(Arena *a, Type *a_ty, Type *b_ty);
Type *type_int_promote(Arena *a, Type *t);
Type *type_default_arg_promote(Arena *a, Type *t);

/* Size/alignment */
int type_sizeof(Type *t);
int type_alignof(Type *t);

/* Member lookup */
Member *type_find_member(Type *t, const char *name);

#endif /* C99JS_TYPE_H */
