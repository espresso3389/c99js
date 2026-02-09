#include "symtab.h"
#include <string.h>

static unsigned int sym_hash(const char *name) {
    unsigned int h = 0;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h % SCOPE_HASH_SIZE;
}

static Scope *new_scope(Arena *a, Scope *parent) {
    Scope *s = arena_calloc(a, sizeof(Scope));
    s->parent = parent;
    s->depth = parent ? parent->depth + 1 : 0;
    return s;
}

void symtab_init(SymTab *st, Arena *a) {
    st->arena = a;
    st->file_scope = new_scope(a, NULL);
    st->current = st->file_scope;
}

void symtab_enter_scope(SymTab *st) {
    st->current = new_scope(st->arena, st->current);
}

void symtab_leave_scope(SymTab *st) {
    if (st->current->parent)
        st->current = st->current->parent;
}

void symtab_enter_func_scope(SymTab *st) {
    symtab_enter_scope(st);
    st->current->is_func_scope = true;
}

Symbol *symtab_define(SymTab *st, const char *name, SymKind kind, Type *type, SrcLoc loc) {
    unsigned int h = sym_hash(name);
    /* Check for redefinition in current scope */
    for (Symbol *s = st->current->syms[h]; s; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            /* Allow compatible redeclaration for functions */
            if (s->kind == SYM_FUNC && kind == SYM_FUNC) {
                if (!s->is_defined) {
                    s->type = type;
                    return s;
                }
            }
            /* Allow extern redeclaration */
            if (s->sc == SC_EXTERN) {
                s->type = type;
                return s;
            }
            error_at(loc, "redefinition of '%s'", name);
            return s;
        }
    }
    Symbol *s = arena_calloc(st->arena, sizeof(Symbol));
    s->name = name;
    s->kind = kind;
    s->type = type;
    s->loc = loc;
    s->is_local = (st->current != st->file_scope);
    s->next = st->current->syms[h];
    st->current->syms[h] = s;
    return s;
}

Symbol *symtab_lookup(SymTab *st, const char *name) {
    unsigned int h = sym_hash(name);
    for (Scope *sc = st->current; sc; sc = sc->parent) {
        for (Symbol *s = sc->syms[h]; s; s = s->next) {
            if (strcmp(s->name, name) == 0)
                return s;
        }
    }
    return NULL;
}

Symbol *symtab_lookup_current(SymTab *st, const char *name) {
    unsigned int h = sym_hash(name);
    for (Symbol *s = st->current->syms[h]; s; s = s->next) {
        if (strcmp(s->name, name) == 0)
            return s;
    }
    return NULL;
}

Tag *symtab_define_tag(SymTab *st, const char *name, Type *type, SrcLoc loc) {
    (void)loc;
    unsigned int h = sym_hash(name);
    for (Tag *t = st->current->tags[h]; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            t->type = type;
            return t;
        }
    }
    Tag *t = arena_calloc(st->arena, sizeof(Tag));
    t->name = name;
    t->type = type;
    t->next = st->current->tags[h];
    st->current->tags[h] = t;
    return t;
}

Tag *symtab_lookup_tag(SymTab *st, const char *name) {
    unsigned int h = sym_hash(name);
    for (Scope *sc = st->current; sc; sc = sc->parent) {
        for (Tag *t = sc->tags[h]; t; t = t->next) {
            if (strcmp(t->name, name) == 0)
                return t;
        }
    }
    return NULL;
}

Tag *symtab_lookup_tag_current(SymTab *st, const char *name) {
    unsigned int h = sym_hash(name);
    for (Tag *t = st->current->tags[h]; t; t = t->next) {
        if (strcmp(t->name, name) == 0)
            return t;
    }
    return NULL;
}

Label *symtab_define_label(SymTab *st, const char *name, SrcLoc loc) {
    /* Labels are in function scope */
    Scope *fsc = st->current;
    while (fsc && !fsc->is_func_scope) fsc = fsc->parent;
    if (!fsc) fsc = st->current;

    for (Label *l = fsc->labels; l; l = l->next) {
        if (strcmp(l->name, name) == 0) {
            if (l->defined) {
                error_at(loc, "duplicate label '%s'", name);
            }
            l->defined = true;
            l->loc = loc;
            return l;
        }
    }
    Label *l = arena_calloc(st->arena, sizeof(Label));
    l->name = name;
    l->defined = true;
    l->loc = loc;
    l->next = fsc->labels;
    fsc->labels = l;
    return l;
}

Label *symtab_lookup_label(SymTab *st, const char *name) {
    Scope *fsc = st->current;
    while (fsc && !fsc->is_func_scope) fsc = fsc->parent;
    if (!fsc) fsc = st->current;

    for (Label *l = fsc->labels; l; l = l->next) {
        if (strcmp(l->name, name) == 0)
            return l;
    }
    return NULL;
}

bool symtab_is_typedef(SymTab *st, const char *name) {
    Symbol *s = symtab_lookup(st, name);
    return s && s->kind == SYM_TYPEDEF;
}
