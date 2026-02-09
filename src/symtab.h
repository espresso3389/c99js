#ifndef C99JS_SYMTAB_H
#define C99JS_SYMTAB_H

#include "type.h"
#include "ast.h"

/* Symbol kinds */
typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_TYPEDEF,
    SYM_ENUM_CONST,
    SYM_PARAM,
} SymKind;

/* Symbol */
typedef struct Symbol {
    const char *name;
    SymKind     kind;
    Type       *type;
    StorageClass sc;
    int         addr;        /* memory address for codegen */
    long long   enum_val;    /* for SYM_ENUM_CONST */
    bool        is_defined;  /* has definition (vs just declaration) */
    bool        is_local;    /* local variable */
    SrcLoc      loc;
    struct Symbol *next;     /* hash chain */
} Symbol;

/* Tag (struct/union/enum tag) */
typedef struct Tag {
    const char *name;
    Type       *type;
    struct Tag *next;
} Tag;

/* Label (goto target) */
typedef struct Label {
    const char *name;
    bool        defined;
    SrcLoc      loc;
    struct Label *next;
} Label;

/* Scope */
#define SCOPE_HASH_SIZE 64

typedef struct Scope {
    Symbol *syms[SCOPE_HASH_SIZE];
    Tag    *tags[SCOPE_HASH_SIZE];
    Label  *labels;          /* only in function scope */
    struct Scope *parent;
    int    depth;
    bool   is_func_scope;    /* function-level scope */
} Scope;

/* Symbol table */
typedef struct {
    Arena *arena;
    Scope *current;
    Scope *file_scope;       /* global scope */
} SymTab;

void    symtab_init(SymTab *st, Arena *a);
void    symtab_enter_scope(SymTab *st);
void    symtab_leave_scope(SymTab *st);
void    symtab_enter_func_scope(SymTab *st);

Symbol *symtab_define(SymTab *st, const char *name, SymKind kind, Type *type, SrcLoc loc);
Symbol *symtab_lookup(SymTab *st, const char *name);
Symbol *symtab_lookup_current(SymTab *st, const char *name);

Tag    *symtab_define_tag(SymTab *st, const char *name, Type *type, SrcLoc loc);
Tag    *symtab_lookup_tag(SymTab *st, const char *name);
Tag    *symtab_lookup_tag_current(SymTab *st, const char *name);

Label  *symtab_define_label(SymTab *st, const char *name, SrcLoc loc);
Label  *symtab_lookup_label(SymTab *st, const char *name);

/* Check if a name refers to a typedef in current scope */
bool    symtab_is_typedef(SymTab *st, const char *name);

#endif /* C99JS_SYMTAB_H */
