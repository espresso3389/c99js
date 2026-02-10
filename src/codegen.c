#include "codegen.h"
#include <string.h>
#include <stdlib.h>

/* ---- Helpers ---- */
static void emit(CodeGen *cg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(&cg->out, fmt, ap);
    va_end(ap);
}

static void emit_indent(CodeGen *cg) {
    for (int i = 0; i < cg->indent; i++)
        buf_append(&cg->out, "  ", 2);
}

static void emitln(CodeGen *cg, const char *fmt, ...) {
    emit_indent(cg);
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(&cg->out, fmt, ap);
    va_end(ap);
    buf_push(&cg->out, '\n');
}

static int new_tmp(CodeGen *cg) {
    return cg->tmp_count++;
}

/* ---- Variable table ---- */
static unsigned int var_hash(const char *name) {
    unsigned int h = 0;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h % CG_VAR_TABLE_SIZE;
}

static void var_clear_locals(CodeGen *cg) {
    memset(cg->locals, 0, sizeof(cg->locals));
}

static void var_set_local(CodeGen *cg, const char *name, int addr, Type *type, bool is_param) {
    unsigned int h = var_hash(name);
    CGVar *v = arena_calloc(cg->arena, sizeof(CGVar));
    v->name = name;
    v->addr = addr;
    v->is_local = true;
    v->is_param = is_param;
    v->type = type;
    v->next = cg->locals[h];
    cg->locals[h] = v;
}

static CGVar *var_find_local(CodeGen *cg, const char *name) {
    unsigned int h = var_hash(name);
    for (CGVar *v = cg->locals[h]; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

static void var_set_global(CodeGen *cg, const char *name, int addr, Type *type) {
    unsigned int h = var_hash(name);
    CGVar *v = arena_calloc(cg->arena, sizeof(CGVar));
    v->name = name;
    v->addr = addr;
    v->is_local = false;
    v->type = type;
    v->next = cg->globals[h];
    cg->globals[h] = v;
}

static CGVar *var_find_global(CodeGen *cg, const char *name) {
    unsigned int h = var_hash(name);
    for (CGVar *v = cg->globals[h]; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

static CGVar *var_find(CodeGen *cg, const char *name) {
    CGVar *v = var_find_local(cg, name);
    if (v) return v;
    return var_find_global(cg, name);
}

/* ---- Type helpers ---- */

/* True if type is long long (signed or unsigned) -- needs BigInt in JS */
static bool type_is_u64(Type *t) {
    return t && t->kind == TY_LLONG;
}

/* True if an expression's result type is uint64_t (BigInt in JS) */
static bool expr_is_u64(Node *n) {
    return n && type_is_u64(n->type);
}

/* True if type is double or long double -- stored as BigInt raw bits in JS */
static bool type_is_double(Type *t) {
    return t && (t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE);
}

/* True if an expression produces a double (BigInt raw bits in JS) */
static bool expr_is_double(Node *n) {
    return n && type_is_double(n->type);
}

/* Memory class uses readXxx/writeXxx (always little-endian) */
static const char *js_getter(Type *t) {
    if (!t) return "readInt32";
    switch (t->kind) {
    case TY_BOOL:  return "readUint8";
    case TY_CHAR:  return t->is_unsigned ? "readUint8" : "readInt8";
    case TY_SHORT: return t->is_unsigned ? "readUint16" : "readInt16";
    case TY_INT: case TY_ENUM:
        return t->is_unsigned ? "readUint32" : "readInt32";
    case TY_LONG:  return t->is_unsigned ? "readUint32" : "readInt32";
    case TY_LLONG: return t->is_unsigned ? "readBigUint64" : "readBigInt64";
    case TY_FLOAT: return "readFloat32";
    case TY_DOUBLE: case TY_LDOUBLE: return "readBigUint64";
    case TY_PTR:   return "readUint32";
    default:       return "readInt32";
    }
}

static const char *js_setter(Type *t) {
    if (!t) return "writeInt32";
    switch (t->kind) {
    case TY_BOOL:  return "writeUint8";
    case TY_CHAR:  return t->is_unsigned ? "writeUint8" : "writeInt8";
    case TY_SHORT: return t->is_unsigned ? "writeUint16" : "writeInt16";
    case TY_INT: case TY_ENUM:
        return t->is_unsigned ? "writeUint32" : "writeInt32";
    case TY_LONG:  return t->is_unsigned ? "writeUint32" : "writeInt32";
    case TY_LLONG: return t->is_unsigned ? "writeBigUint64" : "writeBigInt64";
    case TY_FLOAT: return "writeFloat32";
    case TY_DOUBLE: case TY_LDOUBLE: return "writeBigUint64";
    case TY_PTR:   return "writeUint32";
    default:       return "writeInt32";
    }
}

static int type_sz(Type *t) {
    if (!t) return 4;
    return t->size > 0 ? t->size : 4;
}

static bool is_aggregate(Type *t) {
    return t && (t->kind == TY_STRUCT || t->kind == TY_UNION);
}

/* Forward declarations */
static void gen_expr(CodeGen *cg, Node *n);
static void gen_addr(CodeGen *cg, Node *n);
static void gen_stmt(CodeGen *cg, Node *n);
static int alloc_local(CodeGen *cg, Type *ty);
static void gen_global_init(CodeGen *cg, int addr, Type *ty, Node *init);

static int global_offset = 4096;

void codegen_init(CodeGen *cg, Arena *a, SymTab *st) {
    cg->arena = a;
    buf_init(&cg->out);
    buf_init(&cg->data_section);
    buf_init(&cg->decl_section);
    buf_init(&cg->goto_labels);
    cg->indent = 0;
    cg->label_count = 0;
    cg->str_count = 0;
    cg->tmp_count = 0;
    cg->stack_offset = 0;
    cg->in_func = false;
    cg->has_goto = false;
    cg->goto_state = 0;
    cg->symtab = st;
    memset(cg->locals, 0, sizeof(cg->locals));
    memset(cg->globals, 0, sizeof(cg->globals));
}

/* ---- Address generation ---- */
static void gen_addr(CodeGen *cg, Node *n) {
    switch (n->kind) {
    case ND_IDENT: {
        CGVar *v = var_find(cg, n->name);
        if (v && v->is_local) {
            emit(cg, "(bp + (%d))", v->addr);
        } else if (v) {
            emit(cg, "%d", v->addr);
        } else {
            /* Could be &func → function pointer ID */
            Symbol *sym = symtab_lookup(cg->symtab, n->name);
            if (sym && sym->kind == SYM_FUNC) {
                emit(cg, "__fp_%s", n->name);
            } else {
                emit(cg, "0 /* unknown: %s */", n->name);
            }
        }
        break;
    }
    case ND_DEREF:
        gen_expr(cg, n->lhs);
        break;
    case ND_MEMBER: {
        gen_addr(cg, n->lhs);
        if (n->lhs->type) {
            Member *m = type_find_member(n->lhs->type, n->name);
            if (m && m->offset > 0) emit(cg, " + %d", m->offset);
        }
        break;
    }
    case ND_MEMBER_PTR: {
        gen_expr(cg, n->lhs);
        if (n->lhs->type && n->lhs->type->kind == TY_PTR) {
            Member *m = type_find_member(n->lhs->type->base, n->name);
            if (m && m->offset > 0) emit(cg, " + %d", m->offset);
        }
        break;
    }
    case ND_SUBSCRIPT: {
        emit(cg, "(");
        gen_expr(cg, n->lhs);
        emit(cg, " + (");
        gen_expr(cg, n->rhs);
        emit(cg, ")");
        if (n->type && type_sz(n->type) > 1)
            emit(cg, " * %d", type_sz(n->type));
        emit(cg, ")");
        break;
    }
    default:
        emit(cg, "0 /* cannot take addr */");
        break;
    }
}

/* Check if a function call is a known C library function */
static bool is_stdlib_func(const char *name) {
    static const char *names[] = {
        "printf","fprintf","sprintf","snprintf","scanf","sscanf",
        "malloc","calloc","realloc","free",
        "strlen","strcpy","strncpy","strcmp","strncmp","strcat","strncat",
        "strchr","strrchr","strstr","memcpy","memmove","memset","memcmp","memchr",
        "atoi","atof","strtol","strtoul","strtod","abs","labs",
        "rand","srand","exit","abort","qsort","bsearch",
        "isalpha","isdigit","isalnum","isspace","isupper","islower",
        "ispunct","isprint","iscntrl","isxdigit","toupper","tolower",
        "fopen","fclose","fread","fwrite","fgets","fputs","feof",
        "fgetc","fputc","fseek","ftell","rewind","fflush",
        "puts","putchar","getchar","assert","perror",
        "clock","time","difftime","localtime","strftime",
        "strdup","strtoll","strtoul","strtoull","vsnprintf","vfprintf",
        "__errno_ptr",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        if (strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

/* Map C math function name to JavaScript Math.xxx name.
 * Returns the JS name if it's a math function, NULL otherwise. */
static const char *math_func_js_name(const char *name) {
    /* Table of {c_name, js_name} pairs */
    static const struct { const char *c; const char *js; } map[] = {
        {"sin","sin"},{"cos","cos"},{"tan","tan"},
        {"asin","asin"},{"acos","acos"},{"atan","atan"},{"atan2","atan2"},
        {"sqrt","sqrt"},{"pow","pow"},{"fabs","abs"},
        {"ceil","ceil"},{"floor","floor"},{"fmod","fmod"},
        {"log","log"},{"log10","log10"},{"exp","exp"},
        {"tanh","tanh"},{"fmin","min"},{"fmax","max"},{"round","round"},
        /* float variants -> same JS function */
        {"sinf","sin"},{"cosf","cos"},{"tanf","tan"},
        {"asinf","asin"},{"acosf","acos"},{"atanf","atan"},{"atan2f","atan2"},
        {"sqrtf","sqrt"},{"powf","pow"},{"fabsf","abs"},
        {"ceilf","ceil"},{"floorf","floor"},{"fmodf","fmod"},
        {"logf","log"},{"log10f","log10"},{"expf","exp"},
        {"tanhf","tanh"},{"fminf","min"},{"fmaxf","max"},{"roundf","round"},
        {NULL, NULL}
    };
    for (int i = 0; map[i].c; i++) {
        if (strcmp(name, map[i].c) == 0) return map[i].js;
    }
    return NULL;
}

static bool is_math_func(const char *name) {
    return math_func_js_name(name) != NULL;
}

/* ---- Expression generation ---- */
static void gen_expr(CodeGen *cg, Node *n);

/* Emit expression as a JS float64 number (not BigInt).
 * Doubles (BigInt raw bits) are converted via rt.f64(),
 * uint64_t (BigInt integer) via Number().
 * Other types (int, float) are already JS numbers. */
static void gen_f64_val(CodeGen *cg, Node *n) {
    if (expr_is_double(n)) {
        emit(cg, "rt.f64("); gen_expr(cg, n); emit(cg, ")");
    } else if (expr_is_u64(n)) {
        emit(cg, "Number("); gen_expr(cg, n); emit(cg, ")");
    } else {
        gen_expr(cg, n);
    }
}

static void gen_expr(CodeGen *cg, Node *n) {
    if (!n) { emit(cg, "0"); return; }

    switch (n->kind) {
    case ND_INT_LIT:
        emit(cg, "%lld", (long long)n->ival);
        break;
    case ND_FLOAT_LIT:
        if (n->type && n->type->kind == TY_FLOAT)
            emit(cg, "%.17g", n->fval);
        else
            emit(cg, "rt.f64bits(%.17g)", n->fval);
        break;
    case ND_CHAR_LIT:
        emit(cg, "%d", n->cval);
        break;

    case ND_STRING_LIT: {
        int idx = cg->str_count++;
        buf_printf(&cg->data_section, "const __str%d = rt.mem.allocString(\"", idx);
        for (int i = 0; i < n->slen; i++) {
            char c = n->sval[i];
            switch (c) {
            case '\\': buf_append(&cg->data_section, "\\\\", 2); break;
            case '"':  buf_append(&cg->data_section, "\\\"", 2); break;
            case '\n': buf_append(&cg->data_section, "\\n", 2); break;
            case '\r': buf_append(&cg->data_section, "\\r", 2); break;
            case '\t': buf_append(&cg->data_section, "\\t", 2); break;
            case '\0': buf_append(&cg->data_section, "\\0", 2); break;
            default: buf_push(&cg->data_section, c); break;
            }
        }
        buf_append(&cg->data_section, "\");\n", 3);
        emit(cg, "__str%d", idx);
        break;
    }

    case ND_IDENT: {
        CGVar *v = var_find(cg, n->name);
        if (!v) {
            /* Could be a function name */
            Symbol *sym = symtab_lookup(cg->symtab, n->name);
            if (sym && sym->kind == SYM_FUNC) {
                /* Function used as a value → function pointer ID */
                emit(cg, "__fp_%s", n->name);
            } else if (sym && sym->kind == SYM_ENUM_CONST) {
                emit(cg, "%lld", sym->enum_val);
            } else if (sym && sym->kind == SYM_VAR && sym->sc == SC_EXTERN) {
                /* Extern variables: stdin/stdout/stderr */
                if (strcmp(n->name, "stdin") == 0)
                    emit(cg, "rt.stdin");
                else if (strcmp(n->name, "stdout") == 0)
                    emit(cg, "rt.stdout");
                else if (strcmp(n->name, "stderr") == 0)
                    emit(cg, "rt.stderr");
                else
                    emit(cg, "0 /* extern: %s */", n->name);
            } else {
                emit(cg, "0 /* undef: %s */", n->name);
            }
            break;
        }
        /* Array, struct/union types evaluate to their address */
        if (v->type && (v->type->kind == TY_ARRAY || v->type->kind == TY_VLA ||
                        v->type->kind == TY_STRUCT || v->type->kind == TY_UNION)) {
            gen_addr(cg, n);
            break;
        }
        if (v->type && v->type->kind == TY_FUNC) {
            emit(cg, "_%s", n->name);
            break;
        }
        /* Load value from memory */
        emit(cg, "rt.mem.%s(", js_getter(v->type));
        gen_addr(cg, n);
        emit(cg, ")");
        break;
    }

    case ND_NEG:
        if (expr_is_double(n)) {
            emit(cg, "rt.f64bits(-rt.f64("); gen_expr(cg, n->lhs); emit(cg, "))");
        } else {
            emit(cg, "(-("); gen_expr(cg, n->lhs); emit(cg, "))");
        }
        break;
    case ND_POS:
        emit(cg, "(+("); gen_expr(cg, n->lhs); emit(cg, "))");
        break;
    case ND_NOT:
        emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, ") ? 0 : 1)");
        break;
    case ND_BITNOT:
        emit(cg, "(~("); gen_expr(cg, n->lhs); emit(cg, "))");
        break;
    case ND_DEREF:
        if (n->type && (n->type->kind == TY_STRUCT || n->type->kind == TY_UNION ||
                        n->type->kind == TY_ARRAY)) {
            /* Aggregate deref: just return the address (pointer value) */
            gen_expr(cg, n->lhs);
        } else {
            emit(cg, "rt.mem.%s(", js_getter(n->type));
            gen_expr(cg, n->lhs);
            emit(cg, ")");
        }
        break;
    case ND_ADDR:
        gen_addr(cg, n->lhs);
        break;

    case ND_PRE_INC: case ND_PRE_DEC: {
        const char *op = (n->kind == ND_PRE_INC) ? "+" : "-";
        int step = 1;
        if (n->lhs->type && n->lhs->type->kind == TY_PTR)
            step = type_sz(n->lhs->type->base);
        Type *lt = n->lhs->type;
        if (type_is_double(lt)) {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var v = rt.f64bits(rt.f64(rt.mem.readBigUint64(a)) %s %d); rt.mem.writeBigUint64(a, v); return v; })())",
                   op, step);
        } else {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var v = rt.mem.%s(a) %s %d; rt.mem.%s(a, v); return v; })())",
                   js_getter(lt), op, step, js_setter(lt));
        }
        break;
    }
    case ND_POST_INC: case ND_POST_DEC: {
        const char *op = (n->kind == ND_POST_INC) ? "+" : "-";
        int step = 1;
        if (n->lhs->type && n->lhs->type->kind == TY_PTR)
            step = type_sz(n->lhs->type->base);
        Type *lt = n->lhs->type;
        if (type_is_double(lt)) {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var old = rt.mem.readBigUint64(a); rt.mem.writeBigUint64(a, rt.f64bits(rt.f64(old) %s %d)); return old; })())",
                   op, step);
        } else {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var old = rt.mem.%s(a); rt.mem.%s(a, old %s %d); return old; })())",
                   js_getter(lt), js_setter(lt), op, step);
        }
        break;
    }

    case ND_SIZEOF:
        emit(cg, "%d", n->lhs && n->lhs->type ? type_sz(n->lhs->type) : 4);
        break;
    case ND_SIZEOF_TYPE:
        emit(cg, "%d", n->cast_type ? type_sz(n->cast_type) : 4);
        break;

    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_LSHIFT: case ND_RSHIFT:
    case ND_LT: case ND_LE: case ND_GT: case ND_GE:
    case ND_EQ: case ND_NE:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: {
        const char *op;
        switch (n->kind) {
        case ND_ADD: op = "+"; break;  case ND_SUB: op = "-"; break;
        case ND_MUL: op = "*"; break;  case ND_DIV: op = "/"; break;
        case ND_MOD: op = "%"; break;
        case ND_LSHIFT: op = "<<"; break; case ND_RSHIFT: op = ">>"; break;
        case ND_LT: op = "<"; break;   case ND_LE: op = "<="; break;
        case ND_GT: op = ">"; break;   case ND_GE: op = ">="; break;
        case ND_EQ: op = "==="; break; case ND_NE: op = "!=="; break;
        case ND_BITAND: op = "&"; break; case ND_BITOR: op = "|"; break;
        case ND_BITXOR: op = "^"; break;
        default: op = "+"; break;
        }

        /* Float64 mode: when result or either operand is double, convert
         * operands to JS numbers via gen_f64_val and wrap result in rt.f64bits.
         * Must be checked BEFORE u64mode since double + uint64_t → double. */
        bool f64mode = expr_is_double(n->lhs) || expr_is_double(n->rhs) || type_is_double(n->type);
        if (f64mode) {
            bool is_cmp = (n->kind >= ND_LT && n->kind <= ND_NE);
            if (is_cmp) {
                emit(cg, "((");
                gen_f64_val(cg, n->lhs);
                emit(cg, " %s ", op);
                gen_f64_val(cg, n->rhs);
                emit(cg, ") ? 1 : 0)");
            } else {
                emit(cg, "rt.f64bits(");
                gen_f64_val(cg, n->lhs);
                emit(cg, " %s ", op);
                gen_f64_val(cg, n->rhs);
                emit(cg, ")");
            }
            break;
        }

        /* BigInt mode: when either operand is uint64_t, all arithmetic
         * must use BigInt. Non-BigInt operands are wrapped with BigInt(). */
        bool u64mode = expr_is_u64(n->lhs) || expr_is_u64(n->rhs) || type_is_u64(n->type);
        if (u64mode) {
            bool is_cmp = (n->kind >= ND_LT && n->kind <= ND_NE);
            emit(cg, "(");
            if (!expr_is_u64(n->lhs)) emit(cg, "BigInt(");
            gen_expr(cg, n->lhs);
            if (!expr_is_u64(n->lhs)) emit(cg, ")");
            emit(cg, " %s ", op);
            if (!expr_is_u64(n->rhs)) emit(cg, "BigInt(");
            gen_expr(cg, n->rhs);
            if (!expr_is_u64(n->rhs)) emit(cg, ")");
            emit(cg, ")");
            break;
        }

        bool lp = n->lhs->type && (type_is_ptr(n->lhs->type) || type_is_array(n->lhs->type));
        bool rp = n->rhs->type && (type_is_ptr(n->rhs->type) || type_is_array(n->rhs->type));

        if ((n->kind == ND_ADD || n->kind == ND_SUB) && lp && !rp) {
            int esz = n->lhs->type->base ? type_sz(n->lhs->type->base) : 1;
            emit(cg, "("); gen_expr(cg, n->lhs); emit(cg, " %s ", op);
            gen_expr(cg, n->rhs);
            if (esz > 1) emit(cg, " * %d", esz);
            emit(cg, ")");
        } else if (n->kind == ND_ADD && rp && !lp) {
            int esz = n->rhs->type->base ? type_sz(n->rhs->type->base) : 1;
            emit(cg, "("); gen_expr(cg, n->lhs);
            if (esz > 1) emit(cg, " * %d", esz);
            emit(cg, " + "); gen_expr(cg, n->rhs); emit(cg, ")");
        } else if (n->kind == ND_SUB && lp && rp) {
            int esz = n->lhs->type->base ? type_sz(n->lhs->type->base) : 1;
            emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, " - "); gen_expr(cg, n->rhs);
            emit(cg, ")");
            if (esz > 1) emit(cg, " / %d", esz);
            emit(cg, " | 0)");
        } else if (n->kind == ND_DIV && n->type && type_is_integer(n->type)) {
            emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, " / ");
            gen_expr(cg, n->rhs); emit(cg, ") | 0)");
        } else if (n->kind >= ND_LT && n->kind <= ND_NE) {
            emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, " %s ", op);
            gen_expr(cg, n->rhs); emit(cg, ") ? 1 : 0)");
        } else {
            emit(cg, "("); gen_expr(cg, n->lhs); emit(cg, " %s ", op);
            gen_expr(cg, n->rhs); emit(cg, ")");
        }
        break;
    }

    case ND_AND:
        emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, " && ");
        gen_expr(cg, n->rhs); emit(cg, ") ? 1 : 0)");
        break;
    case ND_OR:
        emit(cg, "(("); gen_expr(cg, n->lhs); emit(cg, " || ");
        gen_expr(cg, n->rhs); emit(cg, ") ? 1 : 0)");
        break;

    case ND_TERNARY: {
        /* When ternary result is double but a branch is i64, wrap with f64bits(Number())
         * When ternary result is double but a branch is int, wrap with f64bits() */
        bool res_double = type_is_double(n->type);
        bool rhs_u64 = expr_is_u64(n->rhs);
        bool third_u64 = expr_is_u64(n->third);
        bool rhs_double = expr_is_double(n->rhs);
        bool third_double = expr_is_double(n->third);

        emit(cg, "("); gen_expr(cg, n->lhs); emit(cg, " ? ");

        if (res_double && rhs_u64 && !rhs_double)
            { emit(cg, "rt.f64bits(Number("); gen_expr(cg, n->rhs); emit(cg, "))"); }
        else if (res_double && !rhs_double && !rhs_u64)
            { emit(cg, "rt.f64bits("); gen_expr(cg, n->rhs); emit(cg, ")"); }
        else
            gen_expr(cg, n->rhs);

        emit(cg, " : ");

        if (res_double && third_u64 && !third_double)
            { emit(cg, "rt.f64bits(Number("); gen_expr(cg, n->third); emit(cg, "))"); }
        else if (res_double && !third_double && !third_u64)
            { emit(cg, "rt.f64bits("); gen_expr(cg, n->third); emit(cg, ")"); }
        else
            gen_expr(cg, n->third);

        emit(cg, ")");
        break;
    }

    case ND_COMMA:
        emit(cg, "("); gen_expr(cg, n->lhs); emit(cg, ", ");
        gen_expr(cg, n->rhs); emit(cg, ")");
        break;

    case ND_ASSIGN: {
        Type *lt = n->lhs->type;
        if (lt && (lt->kind == TY_STRUCT || lt->kind == TY_UNION)) {
            /* Struct copy via memcpy; gen_expr returns address for structs */
            emit(cg, "(rt.memcpy(");
            gen_addr(cg, n->lhs);
            emit(cg, ", ");
            gen_expr(cg, n->rhs);
            emit(cg, ", %d), ", type_sz(lt));
            gen_addr(cg, n->lhs);
            emit(cg, ")");
        } else {
            emit(cg, "((function(){ var v = ");
            gen_expr(cg, n->rhs);
            emit(cg, "; rt.mem.%s(", js_setter(lt));
            gen_addr(cg, n->lhs);
            emit(cg, ", v); return v; })())");
        }
        break;
    }

    case ND_ADD_ASSIGN: case ND_SUB_ASSIGN: case ND_MUL_ASSIGN:
    case ND_DIV_ASSIGN: case ND_MOD_ASSIGN:
    case ND_LSHIFT_ASSIGN: case ND_RSHIFT_ASSIGN:
    case ND_AND_ASSIGN: case ND_OR_ASSIGN: case ND_XOR_ASSIGN: {
        const char *op;
        switch (n->kind) {
        case ND_ADD_ASSIGN: op = "+"; break; case ND_SUB_ASSIGN: op = "-"; break;
        case ND_MUL_ASSIGN: op = "*"; break; case ND_DIV_ASSIGN: op = "/"; break;
        case ND_MOD_ASSIGN: op = "%"; break;
        case ND_LSHIFT_ASSIGN: op = "<<"; break; case ND_RSHIFT_ASSIGN: op = ">>"; break;
        case ND_AND_ASSIGN: op = "&"; break; case ND_OR_ASSIGN: op = "|"; break;
        case ND_XOR_ASSIGN: op = "^"; break;
        default: op = "+"; break;
        }
        Type *lt = n->lhs->type;
        if (type_is_double(lt)) {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var v = rt.f64bits(rt.f64(rt.mem.readBigUint64(a)) %s ", op);
            gen_f64_val(cg, n->rhs);
            emit(cg, "); rt.mem.writeBigUint64(a, v); return v; })())");
        } else {
            emit(cg, "((function(){ var a = ");
            gen_addr(cg, n->lhs);
            emit(cg, "; var v = rt.mem.%s(a) %s (", js_getter(lt), op);
            gen_expr(cg, n->rhs);
            emit(cg, "); rt.mem.%s(a, v); return v; })())", js_setter(lt));
        }
        break;
    }

    case ND_CALL: {
        const char *fname = NULL;
        if (n->callee->kind == ND_IDENT)
            fname = n->callee->name;

        /* Special handling for va_start/va_end/va_copy */
        if (fname && strcmp(fname, "va_start") == 0) {
            /* va_start(ap, last) → write va_list ID to ap's address */
            emit(cg, "rt.mem.writeUint32(");
            gen_addr(cg, n->args);
            emit(cg, ", rt.vaStart(p___va))");
            break;
        }
        if (fname && strcmp(fname, "va_end") == 0) {
            /* va_end(ap) → free va_list entry */
            emit(cg, "rt.vaEnd(rt.mem.readUint32(");
            gen_addr(cg, n->args);
            emit(cg, "))");
            break;
        }
        if (fname && strcmp(fname, "va_copy") == 0) {
            /* va_copy(dest, src) → copy va_list entry */
            emit(cg, "rt.mem.writeUint32(");
            gen_addr(cg, n->args);
            emit(cg, ", rt.vaCopy(rt.mem.readUint32(");
            gen_addr(cg, n->args->next);
            emit(cg, ")))");
            break;
        }

        bool is_direct = false;
        bool sret = is_aggregate(n->type);
        if (fname) {
            /* Check if it's a known function (not a local/global variable) */
            CGVar *v = var_find(cg, fname);
            if (!v) {
                Symbol *sym = symtab_lookup(cg->symtab, fname);
                if (sym && sym->kind == SYM_FUNC) is_direct = true;
            }
        }

        /* For struct-returning functions, allocate temp space and use
         * comma expression: (call(retptr, args...), retptr) */
        int sret_off = 0;
        if (sret) {
            sret_off = alloc_local(cg, n->type);
            emit(cg, "(");
        }

        bool is_math = is_direct && is_math_func(fname);
        bool is_stdlib = is_direct && is_stdlib_func(fname);
        bool ret_f64 = !sret && type_is_double(n->type);
        /* Math/stdlib functions return JS numbers; wrap result in f64bits */
        bool wrap_ret = (is_math || is_stdlib) && ret_f64;
        /* Math/stdlib functions expect JS numbers; unwrap double args */
        bool unwrap_args = is_math || is_stdlib;

        if (wrap_ret) emit(cg, "rt.f64bits(");

        if (is_math) {
            emit(cg, "Math.%s(", math_func_js_name(fname));
        } else if (is_stdlib) {
            emit(cg, "rt.%s(", fname);
        } else if (is_direct) {
            emit(cg, "_%s(", fname);
        } else {
            /* Indirect call through function pointer */
            emit(cg, "rt.callFunction(");
            gen_expr(cg, n->callee);
            emit(cg, ", ");
        }

        /* Hidden return pointer as first argument */
        if (sret) {
            emit(cg, "(bp + (%d))", sret_off);
            if (n->args) emit(cg, ", ");
        }

        for (Node *a = n->args; a; a = a->next) {
            if (unwrap_args && expr_is_double(a)) {
                emit(cg, "rt.f64(");
                gen_expr(cg, a);
                emit(cg, ")");
            } else {
                gen_expr(cg, a);
            }
            if (a->next) emit(cg, ", ");
        }
        emit(cg, ")");

        if (wrap_ret) emit(cg, ")");

        if (sret) {
            emit(cg, ", (bp + (%d)))", sret_off);
        }
        break;
    }

    case ND_MEMBER:
    case ND_MEMBER_PTR: {
        /* Check if the original member type (before decay) is an array.
         * sema's decay_array() mutates n->type from TY_ARRAY to TY_PTR,
         * so we look up the member in the struct definition. */
        bool member_is_array = false;
        if (n->type && n->type->kind == TY_ARRAY) {
            member_is_array = true;
        } else if (n->name) {
            Type *stype = NULL;
            if (n->kind == ND_MEMBER && n->lhs && n->lhs->type)
                stype = n->lhs->type;
            else if (n->kind == ND_MEMBER_PTR && n->lhs && n->lhs->type &&
                     n->lhs->type->kind == TY_PTR)
                stype = n->lhs->type->base;
            if (stype) {
                Member *m = type_find_member(stype, n->name);
                if (m && m->type && m->type->kind == TY_ARRAY)
                    member_is_array = true;
            }
        }
        if (is_aggregate(n->type) || member_is_array) {
            /* Aggregate or array member → return address */
            gen_addr(cg, n);
        } else {
            emit(cg, "rt.mem.%s(", js_getter(n->type));
            gen_addr(cg, n);
            emit(cg, ")");
        }
        break;
    }

    case ND_SUBSCRIPT:
        if (n->type && (n->type->kind == TY_ARRAY || n->type->kind == TY_VLA ||
                        n->type->kind == TY_STRUCT || n->type->kind == TY_UNION)) {
            gen_addr(cg, n);
        } else {
            emit(cg, "rt.mem.%s(", js_getter(n->type));
            gen_addr(cg, n);
            emit(cg, ")");
        }
        break;

    case ND_CAST: {
        bool to_double = n->cast_type && type_is_double(n->cast_type);
        bool from_double = expr_is_double(n->cast_expr);
        bool to_u64 = n->cast_type && type_is_u64(n->cast_type);
        bool from_u64 = expr_is_u64(n->cast_expr);
        bool to_float32 = n->cast_type && n->cast_type->kind == TY_FLOAT;
        bool to_int = n->cast_type && type_is_integer(n->cast_type) && n->cast_type->size <= 4;

        if (to_double) {
            /* Cast TO double: result must be BigInt raw bits */
            if (from_double) {
                gen_expr(cg, n->cast_expr);
            } else if (from_u64) {
                /* uint64_t→double: numeric conversion (BigInt int → float → bits) */
                emit(cg, "rt.f64bits(Number("); gen_expr(cg, n->cast_expr); emit(cg, "))");
            } else {
                /* int/float→double: JS number → BigInt raw float64 bits */
                emit(cg, "rt.f64bits("); gen_expr(cg, n->cast_expr); emit(cg, ")");
            }
        } else if (to_u64) {
            if (from_double) {
                /* double→uint64_t: numeric conversion (bits → float → truncate → BigInt) */
                emit(cg, "BigInt(Math.trunc(rt.f64("); gen_expr(cg, n->cast_expr); emit(cg, ")))");
            } else {
                /* int/float→uint64_t: wrap in BigInt() */
                emit(cg, "BigInt("); gen_expr(cg, n->cast_expr); emit(cg, ")");
            }
        } else if (to_float32 && from_double) {
            /* double→float32: BigInt raw bits → JS number (narrowed to float32) */
            emit(cg, "Math.fround(rt.f64("); gen_expr(cg, n->cast_expr); emit(cg, "))");
        } else if (to_float32 && from_u64) {
            /* uint64_t→float32: BigInt integer → JS number */
            emit(cg, "Number("); gen_expr(cg, n->cast_expr); emit(cg, ")");
        } else if (to_int) {
            /* Cast to int/short/char: may need to unwrap BigInt/double first.
             * For from_u64: mask BigInt to 32 bits BEFORE Number() to avoid
             * precision loss for values > 2^53. */
            const char *pre = "", *suf = "";
            if (from_double)   { pre = "rt.f64("; suf = ")"; }
            else if (from_u64) { pre = "Number("; suf = " & 0xFFFFFFFFn)"; }

            if (n->cast_type->kind == TY_CHAR && !n->cast_type->is_unsigned) {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) << 24 >> 24)", suf);
            } else if (n->cast_type->kind == TY_SHORT && !n->cast_type->is_unsigned) {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) << 16 >> 16)", suf);
            } else if (n->cast_type->kind == TY_CHAR && n->cast_type->is_unsigned) {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) & 0xFF)", suf);
            } else if (n->cast_type->kind == TY_SHORT && n->cast_type->is_unsigned) {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) & 0xFFFF)", suf);
            } else if (n->cast_type->is_unsigned) {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) >>> 0)", suf);
            } else {
                emit(cg, "((%s", pre); gen_expr(cg, n->cast_expr); emit(cg, "%s) | 0)", suf);
            }
        } else {
            gen_expr(cg, n->cast_expr);
        }
        break;
    }

    case ND_COMPOUND_LIT:
        emit(cg, "0 /* compound_lit */");
        break;

    default:
        emit(cg, "0 /* expr_%d */", n->kind);
        break;
    }
}

/* ---- Statement generation ---- */
static int alloc_local(CodeGen *cg, Type *ty) {
    int size = type_sz(ty);
    int align = ty->align > 0 ? ty->align : 1;
    cg->stack_offset = (cg->stack_offset + align - 1) & ~(align - 1);
    cg->stack_offset += size;
    return -(cg->stack_offset);
}

static void gen_init(CodeGen *cg, const char *bp_expr, int base_offset, Type *ty, Node *init) {
    if (!init) return;

    if (init->kind == ND_INIT_LIST) {
        if (ty->kind == TY_ARRAY) {
            int idx = 0;
            for (Node *item = init->body; item; item = item->next, idx++) {
                int off = idx * type_sz(ty->base);
                if (item->kind == ND_DESIGNATOR && item->desig_index) {
                    if (item->desig_index->kind == ND_INT_LIT) {
                        idx = (int)item->desig_index->ival;
                        off = idx * type_sz(ty->base);
                    }
                    gen_init(cg, bp_expr, base_offset + off, ty->base, item->desig_init);
                } else {
                    gen_init(cg, bp_expr, base_offset + off, ty->base, item);
                }
            }
        } else if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
            Member *m = ty->members;
            for (Node *item = init->body; item; item = item->next) {
                if (item->kind == ND_DESIGNATOR && item->desig_name) {
                    m = type_find_member(ty, item->desig_name);
                    if (m) gen_init(cg, bp_expr, base_offset + m->offset, m->type, item->desig_init);
                    if (m) m = m->next;
                } else if (m) {
                    gen_init(cg, bp_expr, base_offset + m->offset, m->type, item);
                    m = m->next;
                }
            }
        } else {
            if (init->body) gen_init(cg, bp_expr, base_offset, ty, init->body);
        }
    } else if (is_aggregate(ty)) {
        /* Aggregate leaf: memcpy from source address */
        emit_indent(cg);
        emit(cg, "rt.memcpy(%s + (%d), ", bp_expr, base_offset);
        gen_expr(cg, init);
        emit(cg, ", %d);\n", type_sz(ty));
    } else {
        emit_indent(cg);
        emit(cg, "rt.mem.%s(%s + (%d), ", js_setter(ty), bp_expr, base_offset);
        gen_expr(cg, init);
        emit(cg, ");\n");
    }
}

static void gen_stmt(CodeGen *cg, Node *n) {
    if (!n) return;

    switch (n->kind) {
    case ND_BLOCK:
        for (Node *s = n->body; s; s = s->next)
            gen_stmt(cg, s);
        break;

    case ND_VAR_DECL: {
        if (n->var_sc == SC_STATIC) {
            /* Static local → allocate in global memory, init in data section */
            int size = type_sz(n->type);
            int align = n->type->align > 0 ? n->type->align : 1;
            global_offset = (global_offset + align - 1) & ~(align - 1);
            var_set_global(cg, n->var_name, global_offset, n->type);
            if (n->var_init) {
                gen_global_init(cg, global_offset, n->type, n->var_init);
            }
            global_offset += size;
            break;
        }

        int off = alloc_local(cg, n->type);
        var_set_local(cg, n->var_name, off, n->type, false);

        if (n->var_init) {
            /* Check if init is a string literal (possibly wrapped in ND_CAST) */
            Node *real_init = n->var_init;
            if (real_init->kind == ND_CAST && real_init->cast_expr)
                real_init = real_init->cast_expr;
            bool is_char_array = n->type &&
                (n->type->kind == TY_ARRAY || n->type->kind == TY_VLA) &&
                n->type->base && n->type->base->kind == TY_CHAR;

            if (n->var_init->kind == ND_INIT_LIST) {
                emitln(cg, "rt.memset(bp + (%d), 0, %d);", off, type_sz(n->type));
                gen_init(cg, "bp", off, n->type, n->var_init);
            } else if (real_init->kind == ND_STRING_LIT && is_char_array) {
                /* char arr[] = "string" → memset + strcpy */
                emitln(cg, "rt.memset(bp + (%d), 0, %d);", off, type_sz(n->type));
                emit_indent(cg);
                emit(cg, "rt.strcpy(bp + (%d), ", off);
                gen_expr(cg, real_init);
                emit(cg, ");\n");
            } else if (is_aggregate(n->type)) {
                /* Struct/union init from expression: memcpy */
                emitln(cg, "rt.memset(bp + (%d), 0, %d);", off, type_sz(n->type));
                emit_indent(cg);
                emit(cg, "rt.memcpy(bp + (%d), ", off);
                gen_expr(cg, n->var_init);
                emit(cg, ", %d);\n", type_sz(n->type));
            } else {
                emit_indent(cg);
                emit(cg, "rt.mem.%s(bp + (%d), ", js_setter(n->type), off);
                gen_expr(cg, n->var_init);
                emit(cg, ");\n");
            }
        }
        break;
    }

    case ND_EXPR_STMT:
        emit_indent(cg);
        gen_expr(cg, n->lhs);
        emit(cg, ";\n");
        break;

    case ND_IF:
        emit_indent(cg); emit(cg, "if ("); gen_expr(cg, n->lhs); emit(cg, ") {\n");
        cg->indent++;
        gen_stmt(cg, n->rhs);
        cg->indent--;
        if (n->third) {
            emitln(cg, "} else {");
            cg->indent++;
            gen_stmt(cg, n->third);
            cg->indent--;
        }
        emitln(cg, "}");
        break;

    case ND_WHILE:
        emit_indent(cg); emit(cg, "while ("); gen_expr(cg, n->lhs); emit(cg, ") {\n");
        cg->indent++;
        gen_stmt(cg, n->rhs);
        cg->indent--;
        emitln(cg, "}");
        break;

    case ND_DO_WHILE:
        emitln(cg, "do {");
        cg->indent++;
        gen_stmt(cg, n->rhs);
        cg->indent--;
        emit_indent(cg); emit(cg, "} while ("); gen_expr(cg, n->lhs); emit(cg, ");\n");
        break;

    case ND_FOR: {
        emit_indent(cg); emit(cg, "for (");
        if (n->for_init) {
            if (n->for_init->kind == ND_VAR_DECL) {
                int off = alloc_local(cg, n->for_init->type);
                var_set_local(cg, n->for_init->var_name, off, n->for_init->type, false);
                if (n->for_init->var_init) {
                    emit(cg, "rt.mem.%s(bp + (%d), ", js_setter(n->for_init->type), off);
                    gen_expr(cg, n->for_init->var_init);
                    emit(cg, ")");
                }
            } else {
                gen_expr(cg, n->for_init);
            }
        }
        emit(cg, "; ");
        if (n->for_cond) gen_expr(cg, n->for_cond);
        emit(cg, "; ");
        if (n->for_inc) gen_expr(cg, n->for_inc);
        emit(cg, ") {\n");
        cg->indent++;
        gen_stmt(cg, n->for_body);
        cg->indent--;
        emitln(cg, "}");
        break;
    }

    case ND_SWITCH:
        emit_indent(cg); emit(cg, "switch ("); gen_expr(cg, n->switch_expr); emit(cg, ") {\n");
        cg->indent++;
        gen_stmt(cg, n->switch_body);
        cg->indent--;
        emitln(cg, "}");
        break;

    case ND_CASE:
        cg->indent--;
        emit_indent(cg); emit(cg, "case "); gen_expr(cg, n->case_expr); emit(cg, ":\n");
        cg->indent++;
        gen_stmt(cg, n->case_body);
        break;

    case ND_DEFAULT:
        cg->indent--;
        emitln(cg, "default:");
        cg->indent++;
        gen_stmt(cg, n->lhs);
        break;

    case ND_BREAK:    emitln(cg, "break;"); break;
    case ND_CONTINUE: emitln(cg, "continue;"); break;

    case ND_RETURN:
        if (is_aggregate(cg->current_func_ret_type) && n->lhs) {
            /* Struct return: memcpy result to hidden __retptr, return the ptr */
            emit_indent(cg);
            emit(cg, "rt.memcpy(p___retptr, ");
            gen_expr(cg, n->lhs);
            emit(cg, ", %d);\n", type_sz(cg->current_func_ret_type));
            emitln(cg, "rt.mem.sp = saved_sp; return p___retptr;");
        } else if (n->lhs) {
            /* Evaluate return expression BEFORE restoring sp, because the
             * expression may reference stack-frame addresses (e.g. passing
             * &local to a callee). If sp is restored first, the callee's
             * frame can overlap and corrupt those locals. */
            emit_indent(cg);
            emit(cg, "var __ret = ");
            gen_expr(cg, n->lhs);
            emit(cg, "; rt.mem.sp = saved_sp; return __ret;\n");
        } else {
            emitln(cg, "rt.mem.sp = saved_sp; return;");
        }
        break;

    case ND_GOTO:
        emitln(cg, "/* goto %s */", n->name);
        break;
    case ND_LABEL:
        emitln(cg, "/* label %s: */", n->name);
        gen_stmt(cg, n->lhs);
        break;

    case ND_NULL_STMT:
    case ND_TYPEDEF:
        break;

    default:
        emit_indent(cg);
        gen_expr(cg, n);
        emit(cg, ";\n");
        break;
    }
}

/* ---- Global variable ---- */

static void gen_global_init(CodeGen *cg, int addr, Type *ty, Node *init) {
    /* Write initialization code for a global variable.
     * Use a temp buffer for init code so that string literal declarations
     * (written to data_section by gen_expr) appear before the init code
     * that references them. */
    Buf saved_out = cg->out;
    int saved_indent = cg->indent;
    Buf tmp;
    buf_init(&tmp);
    cg->out = tmp;
    cg->indent = 0;

    if (init->kind == ND_INIT_LIST) {
        gen_init(cg, "0", addr, ty, init);
    } else if (init->kind == ND_STRING_LIT &&
               ty->kind == TY_ARRAY && ty->base && ty->base->kind == TY_CHAR) {
        emit(cg, "rt.strcpy(%d, ", addr);
        gen_expr(cg, init);
        emit(cg, ");\n");
    } else if (is_aggregate(ty)) {
        emit(cg, "rt.memcpy(%d, ", addr);
        gen_expr(cg, init);
        emit(cg, ", %d);\n", type_sz(ty));
    } else {
        emit(cg, "rt.mem.%s(%d, ", js_setter(ty), addr);
        gen_expr(cg, init);
        emit(cg, ");\n");
    }

    tmp = cg->out;
    cg->out = saved_out;
    cg->indent = saved_indent;

    /* Append the init code to data_section (after any string decls) */
    if (tmp.len > 0)
        buf_append(&cg->data_section, tmp.data, tmp.len);
    buf_free(&tmp);
}

static void gen_global_var(CodeGen *cg, Node *n) {
    int size = type_sz(n->type);
    int align = n->type->align > 0 ? n->type->align : 1;
    global_offset = (global_offset + align - 1) & ~(align - 1);

    var_set_global(cg, n->var_name, global_offset, n->type);

    if (n->var_init) {
        gen_global_init(cg, global_offset, n->type, n->var_init);
    }

    global_offset += size;
}

/* ---- Function generation ---- */
static void gen_func(CodeGen *cg, Node *n) {
    cg->in_func = true;
    cg->stack_offset = 0;
    cg->tmp_count = 0;
    var_clear_locals(cg);
    cg->current_func_ret_type = n->type->return_type;

    bool sret = is_aggregate(n->type->return_type);
    emit(cg, "function _%s(", n->func_name);
    if (sret) {
        emit(cg, "p___retptr");
        if (n->type->params) emit(cg, ", ");
    }
    int pi = 0;
    for (Param *p = n->type->params; p; p = p->next) {
        if (pi > 0) emit(cg, ", ");
        emit(cg, "p_%s", p->name ? p->name : "arg");
        pi++;
    }
    if (n->type->is_variadic) {
        if (pi > 0 || sret) emit(cg, ", ");
        emit(cg, "...p___va");
    }
    emit(cg, ") {\n");
    cg->indent = 1;

    emitln(cg, "const saved_sp = rt.mem.sp;");
    emitln(cg, "const bp = rt.mem.sp;");

    /* Allocate and store parameters on stack */
    for (Param *p = n->type->params; p; p = p->next) {
        if (!p->name) continue;
        int off = alloc_local(cg, p->type);
        var_set_local(cg, p->name, off, p->type, true);
        if (is_aggregate(p->type)) {
            /* Struct/union params: caller passes address, copy full data */
            emitln(cg, "rt.memcpy(bp + (%d), p_%s, %d);",
                   off, p->name, type_sz(p->type));
        } else {
            emitln(cg, "rt.mem.%s(bp + (%d), p_%s);",
                   js_setter(p->type), off, p->name);
        }
    }

    /* Placeholder for stack allocation */
    size_t sp_pos = cg->out.len;
    emitln(cg, "rt.mem.sp -= %-10d;  /* frame */", 0);

    /* Body */
    if (n->func_body) gen_stmt(cg, n->func_body);

    /* Implicit return */
    if (strcmp(n->func_name, "main") == 0)
        emitln(cg, "rt.mem.sp = saved_sp; return 0;");
    else
        emitln(cg, "rt.mem.sp = saved_sp;");

    cg->indent = 0;
    emit(cg, "}\n\n");

    /* Patch stack frame size */
    int frame = (cg->stack_offset + 15) & ~15;
    char patch[64];
    snprintf(patch, sizeof(patch), "rt.mem.sp -= %-10d;  /* frame */", frame);
    char *target = strstr(cg->out.data + sp_pos, "rt.mem.sp -= ");
    if (target) memcpy(target, patch, strlen(patch));

    cg->in_func = false;
}

/* ---- Top-level ---- */
void codegen_generate(CodeGen *cg, Node *program) {
    if (!program || program->kind != ND_PROGRAM) return;

    emit(cg, "\"use strict\";\n");
    emit(cg, "const { Runtime } = require(\"./runtime/runtime.js\");\n");
    emit(cg, "const rt = new Runtime(16 * 1024 * 1024);\n\n");

    /* Collect globals */
    for (Node *n = program->body; n; n = n->next) {
        if (n->kind == ND_VAR_DECL && n->var_sc != SC_TYPEDEF)
            gen_global_var(cg, n);
    }

    /* Generate functions (this populates data_section with string literals,
     * and may add static locals which increase global_offset) */
    Buf func_buf;
    buf_init(&func_buf);
    Buf saved = cg->out;
    cg->out = func_buf;

    for (Node *n = program->body; n; n = n->next) {
        if (n->kind == ND_FUNC_DEF)
            gen_func(cg, n);
    }

    func_buf = cg->out;
    cg->out = saved;

    /* Now emit data section. reserveGlobals must come first so that
     * heap allocations (allocString etc.) don't overlap globals. */
    emit(cg, "// === Data ===\n");
    emit(cg, "rt.mem.reserveGlobals(%d);\n", global_offset);

    /* Emit functions first, then register function pointers BEFORE
     * global data initializers, because global data may reference
     * function pointer constants (__fp_xxx). */
    emit(cg, "\n// === Functions ===\n");
    if (func_buf.len > 0) {
        buf_push(&func_buf, '\0');
        emit(cg, "%s", func_buf.data);
    }
    buf_free(&func_buf);

    /* Register function pointers for user-defined functions */
    bool has_fp = false;
    for (Node *n = program->body; n; n = n->next) {
        if (n->kind == ND_FUNC_DEF) {
            if (!has_fp) {
                emit(cg, "// === Function Pointers ===\n");
                has_fp = true;
            }
            emit(cg, "const __fp_%s = rt.registerFunction(_%s);\n",
                 n->func_name, n->func_name);
        }
    }
    if (has_fp) emit(cg, "\n");

    /* Emit global data initializers (may reference __fp_xxx) */
    emit(cg, "// === Global Data ===\n");
    if (cg->data_section.len > 0) {
        buf_push(&cg->data_section, '\0');
        emit(cg, "%s", cg->data_section.data);
    }

    /* Check if main takes argc/argv */
    bool main_has_args = false;
    for (Node *fn = program->body; fn; fn = fn->next) {
        if (fn->kind == ND_FUNC_DEF && strcmp(fn->func_name, "main") == 0) {
            if (fn->type->params) main_has_args = true;
            break;
        }
    }

    emit(cg, "// === Entry ===\n");
    if (main_has_args) {
        emit(cg, "const __argv_ptrs = [];\n");
        emit(cg, "const __argv_strs = process.argv.slice(1);\n");
        emit(cg, "for (let i = 0; i < __argv_strs.length; i++) __argv_ptrs.push(rt.mem.allocString(__argv_strs[i]));\n");
        emit(cg, "const __argv = rt.malloc((__argv_ptrs.length + 1) * 4);\n");
        emit(cg, "for (let i = 0; i < __argv_ptrs.length; i++) rt.mem.writeUint32(__argv + i * 4, __argv_ptrs[i]);\n");
        emit(cg, "rt.mem.writeUint32(__argv + __argv_ptrs.length * 4, 0);\n");
        emit(cg, "try {\n  process.exit(_main(__argv_ptrs.length, __argv) | 0);\n");
    } else {
        emit(cg, "try {\n  process.exit(_main() | 0);\n");
    }
    emit(cg, "} catch (e) {\n");
    emit(cg, "  if (e.name === 'ExitException') process.exit(e.code);\n");
    emit(cg, "  throw e;\n}\n");
}

char *codegen_get_output(CodeGen *cg) {
    buf_push(&cg->out, '\0');
    return cg->out.data;
}
