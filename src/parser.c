#include "parser.h"
#include <stdlib.h>
#include <string.h>

/* ---- Forward declarations ---- */
static Node *parse_expr(Parser *p);
static Node *parse_assign_expr(Parser *p);
static Node *parse_cond_expr(Parser *p);
static Node *parse_cast_expr(Parser *p);
static Node *parse_unary_expr(Parser *p);
static Node *parse_postfix_expr(Parser *p);
static Node *parse_primary_expr(Parser *p);
static Node *parse_stmt(Parser *p);
static Node *parse_compound_stmt(Parser *p);
static Node *parse_declaration(Parser *p);
static Type *parse_type_specifier(Parser *p, StorageClass *sc);
static Type *parse_declarator(Parser *p, Type *base, const char **name);
static Type *parse_abstract_declarator(Parser *p, Type *base);
static Node *parse_initializer(Parser *p);
static bool  is_type_name(Parser *p);

#define TOK (p->lexer->cur)
#define NEXT() lexer_next(p->lexer)
#define PEEK() lexer_peek(p->lexer)
#define MATCH(k) lexer_match(p->lexer, (k))
#define EXPECT(k) lexer_expect(p->lexer, (k))
#define LOC (p->lexer->cur.loc)

/* Try to evaluate a constant expression at compile time.
 * Returns true and sets *result if the expression is a compile-time constant. */
static bool try_eval_const(Node *n, long long *result) {
    if (!n) return false;
    if (n->kind == ND_INT_LIT) { *result = (long long)n->ival; return true; }
    if (n->kind == ND_CHAR_LIT) { *result = n->cval; return true; }
    long long l, r;
    switch (n->kind) {
    case ND_NEG: return try_eval_const(n->lhs, &l) ? (*result = -l, true) : false;
    case ND_NOT: return try_eval_const(n->lhs, &l) ? (*result = !l, true) : false;
    case ND_BITNOT: return try_eval_const(n->lhs, &l) ? (*result = ~l, true) : false;
    case ND_ADD: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l + r, true) : false;
    case ND_SUB: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l - r, true) : false;
    case ND_MUL: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l * r, true) : false;
    case ND_DIV: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r) && r != 0) ? (*result = l / r, true) : false;
    case ND_MOD: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r) && r != 0) ? (*result = l % r, true) : false;
    case ND_LSHIFT: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l << r, true) : false;
    case ND_RSHIFT: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l >> r, true) : false;
    case ND_LT: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l < r, true) : false;
    case ND_LE: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l <= r, true) : false;
    case ND_GT: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l > r, true) : false;
    case ND_GE: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l >= r, true) : false;
    case ND_EQ: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l == r, true) : false;
    case ND_NE: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l != r, true) : false;
    case ND_BITAND: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l & r, true) : false;
    case ND_BITOR: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l | r, true) : false;
    case ND_BITXOR: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l ^ r, true) : false;
    case ND_AND: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l && r, true) : false;
    case ND_OR: return (try_eval_const(n->lhs, &l) && try_eval_const(n->rhs, &r)) ? (*result = l || r, true) : false;
    case ND_TERNARY: {
        long long c, t, e;
        if (try_eval_const(n->lhs, &c) && try_eval_const(n->rhs, &t) && try_eval_const(n->third, &e))
            return *result = c ? t : e, true;
        return false;
    }
    case ND_CAST:
        return try_eval_const(n->cast_expr, result);
    default: return false;
    }
}

void parser_init(Parser *p, Lexer *l, Arena *a, SymTab *st) {
    p->lexer = l;
    p->arena = a;
    p->symtab = st;
    p->loop_depth = 0;
    p->switch_depth = 0;
    NEXT(); /* prime the first token */
}

/* ---- Type parsing ---- */

static bool is_storage_class(TokenKind k) {
    return k == TK_TYPEDEF || k == TK_EXTERN || k == TK_STATIC ||
           k == TK_AUTO || k == TK_REGISTER;
}

static bool is_type_spec_qual(Parser *p) {
    TokenKind k = TOK.kind;
    if (token_is_type_keyword(k)) return true;
    if (k == TK_IDENT && symtab_is_typedef(p->symtab, TOK.str)) return true;
    return false;
}

static bool is_type_name(Parser *p) {
    return is_storage_class(TOK.kind) || is_type_spec_qual(p);
}

/* Parse declaration specifiers: storage-class + type-specifier + qualifiers */
static Type *parse_type_specifier(Parser *p, StorageClass *sc) {
    if (sc) *sc = SC_NONE;

    int type_flags = 0;
    #define TF_VOID     0x0001
    #define TF_BOOL     0x0002
    #define TF_CHAR     0x0004
    #define TF_SHORT    0x0008
    #define TF_INT      0x0010
    #define TF_LONG     0x0020
    #define TF_LLONG    0x0040
    #define TF_FLOAT    0x0080
    #define TF_DOUBLE   0x0100
    #define TF_SIGNED   0x0200
    #define TF_UNSIGNED 0x0400
    #define TF_COMPLEX  0x0800
    #define TF_OTHER    0x1000

    int qual = 0;
    bool is_inline = false;
    Type *custom_type = NULL;

    for (;;) {
        TokenKind k = TOK.kind;

        /* Storage class */
        if (sc && is_storage_class(k)) {
            switch (k) {
            case TK_TYPEDEF:  *sc = SC_TYPEDEF; break;
            case TK_EXTERN:   *sc = SC_EXTERN; break;
            case TK_STATIC:   *sc = SC_STATIC; break;
            case TK_AUTO:     *sc = SC_AUTO; break;
            case TK_REGISTER: *sc = SC_REGISTER; break;
            default: break;
            }
            NEXT();
            continue;
        }

        /* Qualifiers */
        if (k == TK_CONST)    { qual |= QUAL_CONST;    NEXT(); continue; }
        if (k == TK_VOLATILE) { qual |= QUAL_VOLATILE;  NEXT(); continue; }
        if (k == TK_RESTRICT) { qual |= QUAL_RESTRICT;  NEXT(); continue; }
        if (k == TK_INLINE)   { is_inline = true;       NEXT(); continue; }

        /* Type specifiers */
        if (k == TK_VOID)     { type_flags |= TF_VOID;     NEXT(); continue; }
        if (k == TK_BOOL)     { type_flags |= TF_BOOL;     NEXT(); continue; }
        if (k == TK_CHAR)     { type_flags |= TF_CHAR;     NEXT(); continue; }
        if (k == TK_SHORT)    { type_flags |= TF_SHORT;    NEXT(); continue; }
        if (k == TK_INT)      { type_flags |= TF_INT;      NEXT(); continue; }
        if (k == TK_FLOAT)    { type_flags |= TF_FLOAT;    NEXT(); continue; }
        if (k == TK_DOUBLE)   { type_flags |= TF_DOUBLE;   NEXT(); continue; }
        if (k == TK_SIGNED)   { type_flags |= TF_SIGNED;   NEXT(); continue; }
        if (k == TK_UNSIGNED) { type_flags |= TF_UNSIGNED;  NEXT(); continue; }
        if (k == TK_COMPLEX)  { type_flags |= TF_COMPLEX;   NEXT(); continue; }
        if (k == TK_LONG) {
            if (type_flags & TF_LONG) {
                type_flags = (type_flags & ~TF_LONG) | TF_LLONG;
            } else {
                type_flags |= TF_LONG;
            }
            NEXT();
            continue;
        }

        /* struct/union/enum */
        if (k == TK_STRUCT || k == TK_UNION) {
            bool is_struct = (k == TK_STRUCT);
            SrcLoc tag_loc = LOC;
            NEXT();
            const char *tag = NULL;
            if (TOK.kind == TK_IDENT) {
                tag = TOK.str;
                NEXT();
            }

            Type *ty = NULL;
            if (TOK.kind == TK_LBRACE) {
                /* Definition */
                NEXT();
                if (tag) {
                    Tag *existing = symtab_lookup_tag_current(p->symtab, tag);
                    if (existing) {
                        ty = existing->type;
                    } else {
                        ty = is_struct ? type_struct(p->arena, tag) : type_union(p->arena, tag);
                        symtab_define_tag(p->symtab, tag, ty, tag_loc);
                    }
                } else {
                    ty = is_struct ? type_struct(p->arena, NULL) : type_union(p->arena, NULL);
                }

                /* Parse members */
                Member head = {0};
                Member *cur = &head;
                int offset = 0, max_align = 1, idx = 0;

                while (TOK.kind != TK_RBRACE && TOK.kind != TK_EOF) {
                    Type *mbase = parse_type_specifier(p, NULL);
                    do {
                        const char *mname = NULL;
                        Type *mtype;

                        if (TOK.kind == TK_COLON) {
                            /* Anonymous bitfield */
                            mtype = mbase;
                        } else if (TOK.kind == TK_SEMICOLON) {
                            /* Anonymous struct/union member */
                            mtype = mbase;
                            mname = NULL;
                        } else {
                            mtype = parse_declarator(p, mbase, &mname);
                        }

                        int bit_width = -1;
                        if (TOK.kind == TK_COLON) {
                            NEXT();
                            /* Parse constant expression for bitfield width */
                            Node *bw = parse_cond_expr(p);
                            if (bw->kind == ND_INT_LIT)
                                bit_width = (int)bw->ival;
                            else
                                bit_width = 1;
                        }

                        Member *m = arena_calloc(p->arena, sizeof(Member));
                        m->name = mname;
                        m->type = mtype;
                        m->bit_width = bit_width;
                        m->idx = idx++;

                        if (is_struct) {
                            int align = type_alignof(mtype);
                            if (align > max_align) max_align = align;
                            offset = (offset + align - 1) & ~(align - 1);
                            m->offset = offset;
                            if (bit_width < 0)
                                offset += type_sizeof(mtype);
                        } else {
                            m->offset = 0;
                            int sz = type_sizeof(mtype);
                            if (sz > offset) offset = sz;
                            int align = type_alignof(mtype);
                            if (align > max_align) max_align = align;
                        }

                        cur->next = m;
                        cur = m;
                    } while (MATCH(TK_COMMA));
                    EXPECT(TK_SEMICOLON);
                }
                EXPECT(TK_RBRACE);

                /* Flatten anonymous struct/union members recursively:
                 * copy sub-members into the parent with adjusted offsets */
                {
                    int changed = 1;
                    while (changed) {
                        changed = 0;
                        Member flat_head = {0};
                        Member *flat_cur = &flat_head;
                        for (Member *m = head.next; m; m = m->next) {
                            if (!m->name && m->type &&
                                (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION)) {
                                changed = 1;
                                for (Member *sub = m->type->members; sub; sub = sub->next) {
                                    Member *copy = arena_calloc(p->arena, sizeof(Member));
                                    *copy = *sub;
                                    copy->offset += m->offset;
                                    copy->next = NULL;
                                    flat_cur->next = copy;
                                    flat_cur = copy;
                                }
                            } else {
                                Member *copy = arena_calloc(p->arena, sizeof(Member));
                                *copy = *m;
                                copy->next = NULL;
                                flat_cur->next = copy;
                                flat_cur = copy;
                            }
                        }
                        head.next = flat_head.next;
                    }
                }

                ty->members = head.next;
                ty->align = max_align;
                if (is_struct) {
                    ty->size = (offset + max_align - 1) & ~(max_align - 1);
                } else {
                    ty->size = (offset + max_align - 1) & ~(max_align - 1);
                }
            } else {
                /* Forward reference */
                if (!tag) {
                    error_at(tag_loc, "expected struct/union tag or definition");
                    ty = is_struct ? type_struct(p->arena, NULL) : type_union(p->arena, NULL);
                } else {
                    Tag *existing = symtab_lookup_tag(p->symtab, tag);
                    if (existing) {
                        ty = existing->type;
                    } else {
                        ty = is_struct ? type_struct(p->arena, tag) : type_union(p->arena, tag);
                        symtab_define_tag(p->symtab, tag, ty, tag_loc);
                    }
                }
            }
            custom_type = ty;
            type_flags |= TF_OTHER;
            continue;
        }

        if (k == TK_ENUM) {
            SrcLoc tag_loc = LOC;
            NEXT();
            const char *tag = NULL;
            if (TOK.kind == TK_IDENT) {
                tag = TOK.str;
                NEXT();
            }

            Type *ty = NULL;
            if (TOK.kind == TK_LBRACE) {
                NEXT();
                if (tag) {
                    ty = type_enum(p->arena, tag);
                    symtab_define_tag(p->symtab, tag, ty, tag_loc);
                } else {
                    ty = type_enum(p->arena, NULL);
                }

                long long val = 0;
                while (TOK.kind != TK_RBRACE && TOK.kind != TK_EOF) {
                    if (TOK.kind != TK_IDENT) {
                        error_at(LOC, "expected identifier in enum");
                        break;
                    }
                    const char *ename = TOK.str;
                    SrcLoc eloc = LOC;
                    NEXT();
                    if (MATCH(TK_ASSIGN)) {
                        Node *e = parse_cond_expr(p);
                        if (e->kind == ND_INT_LIT)
                            val = (long long)e->ival;
                    }
                    Symbol *es = symtab_define(p->symtab, ename, SYM_ENUM_CONST, ty_int, eloc);
                    es->enum_val = val;
                    val++;
                    if (!MATCH(TK_COMMA)) break;
                }
                EXPECT(TK_RBRACE);
            } else {
                if (!tag) {
                    error_at(tag_loc, "expected enum tag or definition");
                    ty = type_enum(p->arena, NULL);
                } else {
                    Tag *existing = symtab_lookup_tag(p->symtab, tag);
                    if (existing)
                        ty = existing->type;
                    else {
                        ty = type_enum(p->arena, tag);
                        symtab_define_tag(p->symtab, tag, ty, tag_loc);
                    }
                }
            }
            custom_type = ty;
            type_flags |= TF_OTHER;
            continue;
        }

        /* Typedef name */
        if (k == TK_IDENT && symtab_is_typedef(p->symtab, TOK.str) && !(type_flags & ~(TF_SIGNED|TF_UNSIGNED))) {
            if (type_flags & (TF_VOID|TF_BOOL|TF_CHAR|TF_SHORT|TF_INT|TF_LONG|
                             TF_LLONG|TF_FLOAT|TF_DOUBLE|TF_OTHER))
                break; /* already have a type specifier */
            Symbol *s = symtab_lookup(p->symtab, TOK.str);
            custom_type = s->type;
            type_flags |= TF_OTHER;
            NEXT();
            continue;
        }

        break; /* no more specifiers */
    }

    /* Resolve type from flags */
    Type *result;
    if (type_flags & TF_OTHER) {
        result = custom_type;
    } else if (type_flags == 0 || type_flags == TF_SIGNED ||
               type_flags == TF_INT || type_flags == (TF_SIGNED|TF_INT)) {
        result = ty_int;
    } else if (type_flags & TF_VOID) {
        result = ty_void;
    } else if (type_flags & TF_BOOL) {
        result = ty_bool;
    } else if (type_flags & TF_FLOAT) {
        result = (type_flags & TF_COMPLEX) ? type_complex(p->arena, ty_float) : ty_float;
    } else if (type_flags & TF_DOUBLE) {
        if (type_flags & TF_LONG) {
            result = (type_flags & TF_COMPLEX) ? type_complex(p->arena, ty_ldouble) : ty_ldouble;
        } else {
            result = (type_flags & TF_COMPLEX) ? type_complex(p->arena, ty_double) : ty_double;
        }
    } else if (type_flags & TF_CHAR) {
        result = (type_flags & TF_UNSIGNED) ? ty_uchar : ty_char;
    } else if (type_flags & TF_SHORT) {
        result = (type_flags & TF_UNSIGNED) ? ty_ushort : ty_short;
    } else if (type_flags & TF_LLONG) {
        result = (type_flags & TF_UNSIGNED) ? ty_ullong : ty_llong;
    } else if (type_flags & TF_LONG) {
        result = (type_flags & TF_UNSIGNED) ? ty_ulong : ty_long;
    } else if (type_flags & TF_UNSIGNED) {
        result = ty_uint;
    } else {
        result = ty_int;
    }

    if (qual) {
        result = type_qualified(p->arena, result, qual);
    }
    if (is_inline) {
        result = type_copy(p->arena, result);
        result->is_inline = true;
    }

    return result;
}

/* Parse declarator: pointers, arrays, function params
 * Returns the final type. If name is non-NULL, stores the declared name. */
static Type *parse_declarator(Parser *p, Type *base, const char **name) {
    Type *grouped_dummy = NULL;
    Type *grouped_inner = NULL;

    /* Pointer(s) */
    while (TOK.kind == TK_STAR) {
        NEXT();
        int qual = 0;
        while (TOK.kind == TK_CONST || TOK.kind == TK_VOLATILE || TOK.kind == TK_RESTRICT) {
            if (TOK.kind == TK_CONST)    qual |= QUAL_CONST;
            if (TOK.kind == TK_VOLATILE) qual |= QUAL_VOLATILE;
            if (TOK.kind == TK_RESTRICT) qual |= QUAL_RESTRICT;
            NEXT();
        }
        base = type_ptr(p->arena, base);
        if (qual) base->qual = qual;
    }

    /* Check for parenthesized declarator */
    bool grouped = false;
    if (TOK.kind == TK_LPAREN && !is_type_spec_qual(p)) {
        /* Could be a grouped declarator like (*name) or function params
         * Need to check: if next token after ( is * or ident not a type, it's grouped */
        Token peeked = PEEK();
        if (peeked.kind == TK_STAR ||
            (peeked.kind == TK_IDENT && !symtab_is_typedef(p->symtab, peeked.str)) ||
            peeked.kind == TK_LPAREN) {
            /* Grouped declarator: save position, parse inner, then suffix */
            NEXT(); /* skip ( */

            /* We need a placeholder - parse the inner declarator with a dummy base,
             * then apply the outer suffix to get the real base for the inner */
            grouped_dummy = arena_calloc(p->arena, sizeof(Type));
            grouped_inner = parse_declarator(p, grouped_dummy, name);
            EXPECT(TK_RPAREN);
            grouped = true;
        }
    }

    if (!grouped) {
        /* Direct declarator: identifier */
        if (name) {
            if (TOK.kind == TK_IDENT) {
                *name = TOK.str;
                NEXT();
            } else {
                *name = NULL;
            }
        }
    }

    /* parse suffix: */
    /* Array / Function suffixes */
    for (;;) {
        if (TOK.kind == TK_LBRACKET) {
            NEXT();
            if (TOK.kind == TK_RBRACKET) {
                /* Incomplete array */
                NEXT();
                base = type_array(p->arena, base, -1);
            } else if (TOK.kind == TK_STAR && PEEK().kind == TK_RBRACKET) {
                /* VLA with * */
                NEXT(); NEXT();
                base = type_vla(p->arena, base, NULL);
            } else if (TOK.kind == TK_STATIC || is_type_spec_qual(p)) {
                /* static or qualifiers in array declarator (C99) - skip them */
                while (TOK.kind == TK_STATIC || TOK.kind == TK_CONST ||
                       TOK.kind == TK_VOLATILE || TOK.kind == TK_RESTRICT)
                    NEXT();
                if (TOK.kind == TK_RBRACKET) {
                    NEXT();
                    base = type_array(p->arena, base, -1);
                } else {
                    Node *size = parse_assign_expr(p);
                    EXPECT(TK_RBRACKET);
                    long long cv;
                    if (try_eval_const(size, &cv)) {
                        base = type_array(p->arena, base, (int)cv);
                    } else {
                        base = type_vla(p->arena, base, size);
                    }
                }
            } else {
                Node *size = parse_assign_expr(p);
                EXPECT(TK_RBRACKET);
                long long cv;
                if (try_eval_const(size, &cv)) {
                    base = type_array(p->arena, base, (int)cv);
                } else {
                    base = type_vla(p->arena, base, size);
                }
            }
        } else if (TOK.kind == TK_LPAREN) {
            NEXT();
            Type *func = type_func(p->arena, base);

            if (TOK.kind == TK_RPAREN) {
                /* f() - old-style, no params */
                func->is_oldstyle = true;
                NEXT();
            } else if (TOK.kind == TK_VOID && PEEK().kind == TK_RPAREN) {
                /* f(void) */
                NEXT(); NEXT();
            } else {
                /* Parse parameter list */
                Param head = {0};
                Param *cur = &head;
                do {
                    if (TOK.kind == TK_ELLIPSIS) {
                        func->is_variadic = true;
                        NEXT();
                        break;
                    }
                    Type *pbase = parse_type_specifier(p, NULL);
                    const char *pname = NULL;
                    Type *ptype;
                    if (TOK.kind == TK_COMMA || TOK.kind == TK_RPAREN) {
                        ptype = pbase; /* abstract */
                    } else {
                        ptype = parse_declarator(p, pbase, &pname);
                    }
                    /* Array → pointer, function → pointer */
                    if (ptype->kind == TY_ARRAY || ptype->kind == TY_VLA)
                        ptype = type_ptr(p->arena, ptype->base);
                    if (ptype->kind == TY_FUNC)
                        ptype = type_ptr(p->arena, ptype);

                    Param *pm = arena_calloc(p->arena, sizeof(Param));
                    pm->name = pname;
                    pm->type = ptype;
                    cur->next = pm;
                    cur = pm;
                } while (MATCH(TK_COMMA));
                func->params = head.next;
                EXPECT(TK_RPAREN);
            }
            base = func;
        } else {
            break;
        }
    }

    /* Grouped declarator fixup: graft the suffixed base into the dummy placeholder */
    if (grouped_dummy) {
        *grouped_dummy = *base;
        return grouped_inner;
    }
    return base;
}

static Type *parse_abstract_declarator(Parser *p, Type *base) {
    return parse_declarator(p, base, NULL);
}

/* Parse type-name (for sizeof, casts, compound literals) */
static Type *parse_type_name(Parser *p) {
    Type *base = parse_type_specifier(p, NULL);
    return parse_abstract_declarator(p, base);
}

/* ---- Expression parsing (Pratt-style precedence climbing) ---- */

static Node *parse_primary_expr(Parser *p) {
    SrcLoc loc = LOC;

    switch (TOK.kind) {
    case TK_INT_LIT: {
        unsigned long long val = TOK.num.ival;
        int suffix = TOK.lit_suffix;
        Type *ty;
        if (suffix & LIT_UNSIGNED) {
            if (suffix & LIT_LONGLONG) ty = ty_ullong;
            else if (suffix & LIT_LONG) ty = ty_ulong;
            else ty = ty_uint;
        } else {
            if (suffix & LIT_LONGLONG) ty = ty_llong;
            else if (suffix & LIT_LONG) ty = ty_long;
            else ty = ty_int;
        }
        NEXT();
        return node_int_lit(p->arena, val, ty, loc);
    }
    case TK_FLOAT_LIT: {
        double val = TOK.num.fval;
        Type *ty = (TOK.lit_suffix & LIT_LONG) ? ty_ldouble : ty_double;
        NEXT();
        return node_float_lit(p->arena, val, ty, loc);
    }
    case TK_CHAR_LIT: {
        int val = (int)TOK.num.ival;
        NEXT();
        Node *n = node_new(p->arena, ND_CHAR_LIT, loc);
        n->cval = val;
        n->type = ty_int;
        return n;
    }
    case TK_STRING_LIT: {
        const char *s = TOK.str;
        int len = TOK.str_len;
        NEXT();
        /* Concatenate adjacent string literals */
        while (TOK.kind == TK_STRING_LIT) {
            Buf buf;
            buf_init(&buf);
            buf_append(&buf, s, (size_t)len);
            buf_append(&buf, TOK.str, (size_t)TOK.str_len);
            len += TOK.str_len;
            s = str_intern_range(buf.data, buf.data + buf.len);
            buf_free(&buf);
            NEXT();
        }
        return node_string_lit(p->arena, s, len, loc);
    }
    case TK_IDENT: {
        const char *name = TOK.str;
        NEXT();
        Node *n = node_ident(p->arena, name, loc);
        /* Look up symbol for type info */
        Symbol *sym = symtab_lookup(p->symtab, name);
        if (sym) {
            if (sym->kind == SYM_ENUM_CONST) {
                return node_int_lit(p->arena, (unsigned long long)sym->enum_val, ty_int, loc);
            }
            n->type = sym->type;
        }
        return n;
    }
    case TK_LPAREN: {
        NEXT();
        /* Check for compound literal or cast */
        if (is_type_name(p)) {
            Type *ty = parse_type_name(p);
            EXPECT(TK_RPAREN);
            if (TOK.kind == TK_LBRACE) {
                /* Compound literal */
                Node *init = parse_initializer(p);
                Node *n = node_new(p->arena, ND_COMPOUND_LIT, loc);
                n->cast_type = ty;
                n->cast_expr = init;
                n->type = ty;
                return n;
            }
            /* Cast expression */
            Node *operand = parse_cast_expr(p);
            Node *n = node_new(p->arena, ND_CAST, loc);
            n->cast_type = ty;
            n->cast_expr = operand;
            n->type = ty;
            return n;
        }
        Node *n = parse_expr(p);
        EXPECT(TK_RPAREN);
        return n;
    }
    default:
        error_at(loc, "expected expression, got '%s'", token_kind_str(TOK.kind));
        NEXT();
        return node_int_lit(p->arena, 0, ty_int, loc);
    }
}

static Node *parse_postfix_expr(Parser *p) {
    Node *n = parse_primary_expr(p);

    for (;;) {
        SrcLoc loc = LOC;

        if (MATCH(TK_LBRACKET)) {
            Node *idx = parse_expr(p);
            EXPECT(TK_RBRACKET);
            n = node_binary(p->arena, ND_SUBSCRIPT, n, idx, loc);
        } else if (TOK.kind == TK_LPAREN && n->kind != ND_CAST) {
            NEXT();
            Node *call = node_new(p->arena, ND_CALL, loc);
            call->callee = n;
            Node arg_head = {0};
            Node *cur = &arg_head;
            if (TOK.kind != TK_RPAREN) {
                do {
                    Node *arg = parse_assign_expr(p);
                    cur->next = arg;
                    cur = arg;
                } while (MATCH(TK_COMMA));
            }
            call->args = arg_head.next;
            EXPECT(TK_RPAREN);
            /* Determine return type */
            if (n->type) {
                Type *ft = n->type;
                if (ft->kind == TY_PTR) ft = ft->base;
                if (ft->kind == TY_FUNC) call->type = ft->return_type;
                else call->type = ty_int;
            } else {
                call->type = ty_int;
            }
            n = call;
        } else if (MATCH(TK_DOT)) {
            const char *name = TOK.str;
            EXPECT(TK_IDENT);
            Node *m = node_new(p->arena, ND_MEMBER, loc);
            m->lhs = n;
            m->name = name;
            if (n->type) {
                Member *mem = type_find_member(n->type, name);
                if (mem) m->type = mem->type;
            }
            n = m;
        } else if (MATCH(TK_ARROW)) {
            const char *name = TOK.str;
            EXPECT(TK_IDENT);
            Node *m = node_new(p->arena, ND_MEMBER_PTR, loc);
            m->lhs = n;
            m->name = name;
            if (n->type && n->type->kind == TY_PTR) {
                Member *mem = type_find_member(n->type->base, name);
                if (mem) m->type = mem->type;
            }
            n = m;
        } else if (MATCH(TK_INC)) {
            n = node_unary(p->arena, ND_POST_INC, n, loc);
            n->type = n->lhs->type;
        } else if (MATCH(TK_DEC)) {
            n = node_unary(p->arena, ND_POST_DEC, n, loc);
            n->type = n->lhs->type;
        } else {
            break;
        }
    }
    return n;
}

static Node *parse_unary_expr(Parser *p) {
    SrcLoc loc = LOC;

    if (MATCH(TK_INC)) {
        Node *n = node_unary(p->arena, ND_PRE_INC, parse_unary_expr(p), loc);
        n->type = n->lhs->type;
        return n;
    }
    if (MATCH(TK_DEC)) {
        Node *n = node_unary(p->arena, ND_PRE_DEC, parse_unary_expr(p), loc);
        n->type = n->lhs->type;
        return n;
    }
    if (MATCH(TK_AMP)) {
        Node *operand = parse_cast_expr(p);
        Node *n = node_unary(p->arena, ND_ADDR, operand, loc);
        if (operand->type) n->type = type_ptr(p->arena, operand->type);
        return n;
    }
    if (MATCH(TK_STAR)) {
        Node *operand = parse_cast_expr(p);
        Node *n = node_unary(p->arena, ND_DEREF, operand, loc);
        if (operand->type && operand->type->kind == TY_PTR)
            n->type = operand->type->base;
        return n;
    }
    if (MATCH(TK_PLUS)) {
        Node *n = node_unary(p->arena, ND_POS, parse_cast_expr(p), loc);
        n->type = n->lhs->type;
        return n;
    }
    if (MATCH(TK_MINUS)) {
        Node *n = node_unary(p->arena, ND_NEG, parse_cast_expr(p), loc);
        n->type = n->lhs->type;
        return n;
    }
    if (MATCH(TK_TILDE)) {
        Node *n = node_unary(p->arena, ND_BITNOT, parse_cast_expr(p), loc);
        n->type = n->lhs->type;
        return n;
    }
    if (MATCH(TK_BANG)) {
        Node *n = node_unary(p->arena, ND_NOT, parse_cast_expr(p), loc);
        n->type = ty_int;
        return n;
    }
    if (TOK.kind == TK_SIZEOF) {
        NEXT();
        if (TOK.kind == TK_LPAREN) {
            Token peeked = PEEK();
            /* Check if it's sizeof(type) or sizeof(expr) */
            if (token_is_type_keyword(peeked.kind) ||
                (peeked.kind == TK_IDENT && symtab_is_typedef(p->symtab, peeked.str))) {
                NEXT(); /* skip ( */
                Type *ty = parse_type_name(p);
                EXPECT(TK_RPAREN);
                Node *n = node_new(p->arena, ND_SIZEOF_TYPE, loc);
                n->cast_type = ty;
                n->type = ty_uint;
                return n;
            }
        }
        Node *operand = parse_unary_expr(p);
        Node *n = node_unary(p->arena, ND_SIZEOF, operand, loc);
        n->type = ty_uint;
        return n;
    }
    return parse_postfix_expr(p);
}

static Node *parse_cast_expr(Parser *p) {
    /* Cast is handled in primary via parenthesized type check */
    return parse_unary_expr(p);
}

/* Binary expression with precedence climbing */
static int get_precedence(TokenKind k) {
    switch (k) {
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 13;
    case TK_PLUS: case TK_MINUS: return 12;
    case TK_LSHIFT: case TK_RSHIFT: return 11;
    case TK_LT: case TK_LE: case TK_GT: case TK_GE: return 10;
    case TK_EQ: case TK_NE: return 9;
    case TK_AMP: return 8;
    case TK_CARET: return 7;
    case TK_PIPE: return 6;
    case TK_AND: return 5;
    case TK_OR: return 4;
    /* Ternary handled separately at prec 3 */
    default: return -1;
    }
}

static NodeKind binop_kind(TokenKind k) {
    switch (k) {
    case TK_STAR: return ND_MUL; case TK_SLASH: return ND_DIV;
    case TK_PERCENT: return ND_MOD;
    case TK_PLUS: return ND_ADD; case TK_MINUS: return ND_SUB;
    case TK_LSHIFT: return ND_LSHIFT; case TK_RSHIFT: return ND_RSHIFT;
    case TK_LT: return ND_LT; case TK_LE: return ND_LE;
    case TK_GT: return ND_GT; case TK_GE: return ND_GE;
    case TK_EQ: return ND_EQ; case TK_NE: return ND_NE;
    case TK_AMP: return ND_BITAND; case TK_PIPE: return ND_BITOR;
    case TK_CARET: return ND_BITXOR;
    case TK_AND: return ND_AND; case TK_OR: return ND_OR;
    default: return ND_ADD; /* unreachable */
    }
}

static Node *parse_binary_expr(Parser *p, int min_prec) {
    Node *lhs = parse_cast_expr(p);

    for (;;) {
        int prec = get_precedence(TOK.kind);
        if (prec < min_prec) break;

        SrcLoc loc = LOC;
        TokenKind op = TOK.kind;
        NEXT();

        Node *rhs = parse_binary_expr(p, prec + 1);
        lhs = node_binary(p->arena, binop_kind(op), lhs, rhs, loc);

        /* Determine result type (simplified) */
        if (lhs->lhs->type && lhs->rhs->type) {
            NodeKind nk = lhs->kind;
            if (nk >= ND_LT && nk <= ND_NE) {
                lhs->type = ty_int;
            } else if (nk == ND_AND || nk == ND_OR) {
                lhs->type = ty_int;
            } else if (type_is_ptr(lhs->lhs->type)) {
                lhs->type = lhs->lhs->type;
            } else if (type_is_ptr(lhs->rhs->type)) {
                lhs->type = lhs->rhs->type;
            } else {
                lhs->type = type_usual_arith(p->arena, lhs->lhs->type, lhs->rhs->type);
            }
        }
    }
    return lhs;
}

static Node *parse_cond_expr(Parser *p) {
    Node *cond = parse_binary_expr(p, 4); /* above || */

    /* Also handle || and && here to ensure correct precedence */
    for (;;) {
        if (TOK.kind == TK_OR) {
            SrcLoc loc = LOC; NEXT();
            Node *rhs = parse_binary_expr(p, 4);
            cond = node_binary(p->arena, ND_OR, cond, rhs, loc);
            cond->type = ty_int;
            continue;
        }
        break;
    }

    if (TOK.kind != TK_QUESTION) return cond;

    SrcLoc loc = LOC;
    NEXT();
    Node *then_expr = parse_expr(p);
    EXPECT(TK_COLON);
    Node *else_expr = parse_cond_expr(p);

    Node *n = node_new(p->arena, ND_TERNARY, loc);
    n->lhs = cond;
    n->rhs = then_expr;
    n->third = else_expr;
    n->type = then_expr->type;
    return n;
}

static NodeKind assign_op_kind(TokenKind k) {
    switch (k) {
    case TK_ASSIGN: return ND_ASSIGN;
    case TK_ADD_ASSIGN: return ND_ADD_ASSIGN;
    case TK_SUB_ASSIGN: return ND_SUB_ASSIGN;
    case TK_MUL_ASSIGN: return ND_MUL_ASSIGN;
    case TK_DIV_ASSIGN: return ND_DIV_ASSIGN;
    case TK_MOD_ASSIGN: return ND_MOD_ASSIGN;
    case TK_LSHIFT_ASSIGN: return ND_LSHIFT_ASSIGN;
    case TK_RSHIFT_ASSIGN: return ND_RSHIFT_ASSIGN;
    case TK_AND_ASSIGN: return ND_AND_ASSIGN;
    case TK_OR_ASSIGN: return ND_OR_ASSIGN;
    case TK_XOR_ASSIGN: return ND_XOR_ASSIGN;
    default: return ND_ASSIGN;
    }
}

static bool is_assign_op(TokenKind k) {
    return k == TK_ASSIGN || k == TK_ADD_ASSIGN || k == TK_SUB_ASSIGN ||
           k == TK_MUL_ASSIGN || k == TK_DIV_ASSIGN || k == TK_MOD_ASSIGN ||
           k == TK_LSHIFT_ASSIGN || k == TK_RSHIFT_ASSIGN ||
           k == TK_AND_ASSIGN || k == TK_OR_ASSIGN || k == TK_XOR_ASSIGN;
}

static Node *parse_assign_expr(Parser *p) {
    Node *lhs = parse_cond_expr(p);

    if (is_assign_op(TOK.kind)) {
        SrcLoc loc = LOC;
        NodeKind nk = assign_op_kind(TOK.kind);
        NEXT();
        Node *rhs = parse_assign_expr(p);
        Node *n = node_binary(p->arena, nk, lhs, rhs, loc);
        n->type = lhs->type;
        return n;
    }
    return lhs;
}

static Node *parse_expr(Parser *p) {
    Node *n = parse_assign_expr(p);
    while (MATCH(TK_COMMA)) {
        SrcLoc loc = LOC;
        Node *rhs = parse_assign_expr(p);
        n = node_binary(p->arena, ND_COMMA, n, rhs, loc);
        n->type = rhs->type;
    }
    return n;
}

/* ---- Initializer ---- */
static Node *parse_initializer(Parser *p) {
    if (TOK.kind == TK_LBRACE) {
        SrcLoc loc = LOC;
        NEXT();
        Node *n = node_new(p->arena, ND_INIT_LIST, loc);
        Node head = {0};
        Node *cur = &head;

        while (TOK.kind != TK_RBRACE && TOK.kind != TK_EOF) {
            Node *init;

            /* Designator */
            if (TOK.kind == TK_DOT || TOK.kind == TK_LBRACKET) {
                SrcLoc dloc = LOC;
                Node *d = node_new(p->arena, ND_DESIGNATOR, dloc);
                if (MATCH(TK_DOT)) {
                    d->desig_name = TOK.str;
                    EXPECT(TK_IDENT);
                } else {
                    NEXT(); /* [ */
                    d->desig_index = parse_cond_expr(p);
                    EXPECT(TK_RBRACKET);
                }
                EXPECT(TK_ASSIGN);
                d->desig_init = parse_initializer(p);
                init = d;
            } else {
                init = parse_initializer(p);
            }

            cur->next = init;
            cur = init;
            if (!MATCH(TK_COMMA)) break;
        }
        n->body = head.next;
        EXPECT(TK_RBRACE);
        return n;
    }
    return parse_assign_expr(p);
}

/* ---- Statement parsing ---- */

static Node *parse_stmt(Parser *p) {
    SrcLoc loc = LOC;

    /* Label: identifier followed by colon */
    if (TOK.kind == TK_IDENT && PEEK().kind == TK_COLON) {
        const char *name = TOK.str;
        NEXT(); NEXT(); /* skip ident and colon */
        symtab_define_label(p->symtab, name, loc);
        Node *n = node_new(p->arena, ND_LABEL, loc);
        n->name = name;
        n->lhs = parse_stmt(p);
        return n;
    }

    if (MATCH(TK_LBRACE)) {
        return parse_compound_stmt(p);
    }

    if (TOK.kind == TK_IF) {
        NEXT();
        EXPECT(TK_LPAREN);
        Node *cond = parse_expr(p);
        EXPECT(TK_RPAREN);
        Node *then_s = parse_stmt(p);
        Node *else_s = NULL;
        if (MATCH(TK_ELSE))
            else_s = parse_stmt(p);
        Node *n = node_new(p->arena, ND_IF, loc);
        n->lhs = cond;
        n->rhs = then_s;
        n->third = else_s;
        return n;
    }

    if (TOK.kind == TK_WHILE) {
        NEXT();
        EXPECT(TK_LPAREN);
        Node *cond = parse_expr(p);
        EXPECT(TK_RPAREN);
        p->loop_depth++;
        Node *body = parse_stmt(p);
        p->loop_depth--;
        Node *n = node_new(p->arena, ND_WHILE, loc);
        n->lhs = cond;
        n->rhs = body;
        return n;
    }

    if (TOK.kind == TK_DO) {
        NEXT();
        p->loop_depth++;
        Node *body = parse_stmt(p);
        p->loop_depth--;
        EXPECT(TK_WHILE);
        EXPECT(TK_LPAREN);
        Node *cond = parse_expr(p);
        EXPECT(TK_RPAREN);
        EXPECT(TK_SEMICOLON);
        Node *n = node_new(p->arena, ND_DO_WHILE, loc);
        n->lhs = cond;
        n->rhs = body;
        return n;
    }

    if (TOK.kind == TK_FOR) {
        NEXT();
        EXPECT(TK_LPAREN);
        symtab_enter_scope(p->symtab);

        Node *n = node_new(p->arena, ND_FOR, loc);

        /* Init: could be declaration or expression */
        if (TOK.kind == TK_SEMICOLON) {
            n->for_init = NULL;
            NEXT();
        } else if (is_type_name(p)) {
            n->for_init = parse_declaration(p);
        } else {
            n->for_init = parse_expr(p);
            EXPECT(TK_SEMICOLON);
        }

        n->for_cond = (TOK.kind != TK_SEMICOLON) ? parse_expr(p) : NULL;
        EXPECT(TK_SEMICOLON);
        n->for_inc = (TOK.kind != TK_RPAREN) ? parse_expr(p) : NULL;
        EXPECT(TK_RPAREN);

        p->loop_depth++;
        n->for_body = parse_stmt(p);
        p->loop_depth--;
        symtab_leave_scope(p->symtab);
        return n;
    }

    if (TOK.kind == TK_SWITCH) {
        NEXT();
        EXPECT(TK_LPAREN);
        Node *expr = parse_expr(p);
        EXPECT(TK_RPAREN);
        Node *n = node_new(p->arena, ND_SWITCH, loc);
        n->switch_expr = expr;
        p->switch_depth++;
        n->switch_body = parse_stmt(p);
        p->switch_depth--;
        return n;
    }

    if (TOK.kind == TK_CASE) {
        NEXT();
        Node *expr = parse_cond_expr(p);
        EXPECT(TK_COLON);
        Node *n = node_new(p->arena, ND_CASE, loc);
        n->case_expr = expr;
        if (expr->kind == ND_INT_LIT)
            n->case_val = (long long)expr->ival;
        n->case_body = parse_stmt(p);
        return n;
    }

    if (TOK.kind == TK_DEFAULT) {
        NEXT();
        EXPECT(TK_COLON);
        Node *n = node_new(p->arena, ND_DEFAULT, loc);
        n->lhs = parse_stmt(p);
        return n;
    }

    if (MATCH(TK_BREAK)) {
        EXPECT(TK_SEMICOLON);
        return node_new(p->arena, ND_BREAK, loc);
    }

    if (MATCH(TK_CONTINUE)) {
        EXPECT(TK_SEMICOLON);
        return node_new(p->arena, ND_CONTINUE, loc);
    }

    if (TOK.kind == TK_RETURN) {
        NEXT();
        Node *n = node_new(p->arena, ND_RETURN, loc);
        if (TOK.kind != TK_SEMICOLON)
            n->lhs = parse_expr(p);
        EXPECT(TK_SEMICOLON);
        return n;
    }

    if (TOK.kind == TK_GOTO) {
        NEXT();
        Node *n = node_new(p->arena, ND_GOTO, loc);
        n->name = TOK.str;
        EXPECT(TK_IDENT);
        EXPECT(TK_SEMICOLON);
        return n;
    }

    if (MATCH(TK_SEMICOLON)) {
        return node_new(p->arena, ND_NULL_STMT, loc);
    }

    /* Declaration in block scope */
    if (is_type_name(p)) {
        return parse_declaration(p);
    }

    /* Expression statement */
    Node *expr = parse_expr(p);
    EXPECT(TK_SEMICOLON);
    Node *n = node_new(p->arena, ND_EXPR_STMT, loc);
    n->lhs = expr;
    return n;
}

static Node *parse_compound_stmt(Parser *p) {
    SrcLoc loc = LOC;
    symtab_enter_scope(p->symtab);
    Node *block = node_new(p->arena, ND_BLOCK, loc);
    Node head = {0};
    Node *cur = &head;

    while (TOK.kind != TK_RBRACE && TOK.kind != TK_EOF) {
        Node *s = parse_stmt(p);
        cur->next = s;
        /* Find the end of the chain (declarations can produce multiple nodes) */
        while (cur->next) cur = cur->next;
    }
    EXPECT(TK_RBRACE);
    block->body = head.next;
    symtab_leave_scope(p->symtab);
    return block;
}

/* ---- Declaration parsing ---- */

static Node *parse_declaration(Parser *p) {
    SrcLoc loc = LOC;
    StorageClass sc;
    Type *base = parse_type_specifier(p, &sc);

    /* Typedef */
    if (sc == SC_TYPEDEF) {
        do {
            const char *name = NULL;
            Type *ty = parse_declarator(p, base, &name);
            if (name) {
                Symbol *s = symtab_define(p->symtab, name, SYM_TYPEDEF, ty, loc);
                s->sc = SC_TYPEDEF;
            }
        } while (MATCH(TK_COMMA));
        EXPECT(TK_SEMICOLON);
        return node_new(p->arena, ND_TYPEDEF, loc);
    }

    /* Bare struct/union/enum declaration */
    if (TOK.kind == TK_SEMICOLON) {
        NEXT();
        return node_new(p->arena, ND_NULL_STMT, loc);
    }

    Node head = {0};
    Node *cur = &head;

    do {
        const char *name = NULL;
        Type *ty = parse_declarator(p, base, &name);

        if (!name) {
            error_at(loc, "expected declarator name");
            break;
        }

        /* Function definition */
        if (ty->kind == TY_FUNC && TOK.kind == TK_LBRACE) {
            Symbol *sym = symtab_define(p->symtab, name, SYM_FUNC, ty, loc);
            sym->sc = sc;
            sym->is_defined = true;

            symtab_enter_func_scope(p->symtab);

            /* Define parameters as local variables */
            Node *params_head = NULL;
            Node **params_tail = &params_head;
            for (Param *pm = ty->params; pm; pm = pm->next) {
                if (pm->name) {
                    Symbol *ps = symtab_define(p->symtab, pm->name, SYM_PARAM, pm->type, loc);
                    (void)ps;
                    Node *pn = node_new(p->arena, ND_VAR_DECL, loc);
                    pn->var_name = pm->name;
                    pn->type = pm->type;
                    *params_tail = pn;
                    params_tail = &pn->next;
                }
            }

            NEXT(); /* skip { */
            Node *body = parse_compound_stmt(p);
            symtab_leave_scope(p->symtab);

            Node *func = node_new(p->arena, ND_FUNC_DEF, loc);
            func->func_name = name;
            func->type = ty;
            func->func_params = params_head;
            func->func_body = body;
            func->func_sc = sc;
            func->func_is_inline = base->is_inline;
            cur->next = func;
            cur = func;
            return head.next;
        }

        /* Variable or function-prototype declaration */
        SymKind sk = (ty->kind == TY_FUNC) ? SYM_FUNC : SYM_VAR;
        Symbol *sym = symtab_define(p->symtab, name, sk, ty, loc);
        sym->sc = sc;

        /* Function prototypes don't need an AST node (no codegen) */
        if (ty->kind == TY_FUNC) {
            /* still allow comma-separated declarations */
        } else {
            Node *decl = node_new(p->arena, ND_VAR_DECL, loc);
            decl->var_name = name;
            decl->var_sc = sc;
            decl->type = ty;

            if (MATCH(TK_ASSIGN)) {
                decl->var_init = parse_initializer(p);
                /* Fix incomplete array size from init list */
                if (ty->kind == TY_ARRAY && ty->array_len < 0 &&
                    decl->var_init->kind == ND_INIT_LIST) {
                    int count = 0;
                    for (Node *item = decl->var_init->body; item; item = item->next)
                        count++;
                    decl->type = type_array(p->arena, ty->base, count);
                    ty = decl->type;
                } else if (ty->kind == TY_ARRAY && ty->array_len < 0 &&
                           decl->var_init->kind == ND_STRING_LIT) {
                    decl->type = type_array(p->arena, ty->base,
                                            decl->var_init->slen + 1);
                    ty = decl->type;
                }
            }

            cur->next = decl;
            cur = decl;
        }
    } while (MATCH(TK_COMMA));

    EXPECT(TK_SEMICOLON);
    return head.next;
}

/* ---- Top-level parsing ---- */
Node *parser_parse(Parser *p) {
    SrcLoc loc = LOC;
    Node *prog = node_new(p->arena, ND_PROGRAM, loc);
    Node head = {0};
    Node *cur = &head;

    while (TOK.kind != TK_EOF) {
        Node *decl = parse_declaration(p);
        if (decl) {
            cur->next = decl;
            while (cur->next) cur = cur->next;
        }
    }
    prog->body = head.next;
    return prog;
}
