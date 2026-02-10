#include "preprocess.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* Portable strdup (not available in strict C99) */
static char *pp_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

/* ---- Macro definition ---- */
typedef struct MacroParam {
    const char *name;
    struct MacroParam *next;
} MacroParam;

typedef struct Macro {
    const char   *name;
    const char   *body;       /* replacement text */
    MacroParam   *params;     /* NULL for object-like macros */
    bool          is_func;    /* function-like macro */
    bool          is_variadic;
    bool          is_builtin; /* __LINE__ etc. */
    struct Macro *next;
} Macro;

#define MACRO_TABLE_SIZE 1024
static Macro *macro_table[MACRO_TABLE_SIZE];

static unsigned int macro_hash(const char *name) {
    unsigned int h = 0;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h % MACRO_TABLE_SIZE;
}

static Macro *find_macro(const char *name) {
    unsigned int h = macro_hash(name);
    for (Macro *m = macro_table[h]; m; m = m->next) {
        if (strcmp(m->name, name) == 0)
            return m;
    }
    return NULL;
}

static void define_macro(const char *name, const char *body, bool is_func,
                         MacroParam *params, bool is_variadic) {
    unsigned int h = macro_hash(name);
    /* Check for existing */
    for (Macro *m = macro_table[h]; m; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            m->body = body;
            m->params = params;
            m->is_func = is_func;
            m->is_variadic = is_variadic;
            return;
        }
    }
    Macro *m = calloc(1, sizeof(Macro));
    m->name = pp_strdup(name);
    m->body = body ? pp_strdup(body) : "";
    m->is_func = is_func;
    m->params = params;
    m->is_variadic = is_variadic;
    m->next = macro_table[h];
    macro_table[h] = m;
}

static void undef_macro(const char *name) {
    unsigned int h = macro_hash(name);
    Macro **pp = &macro_table[h];
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            *pp = (*pp)->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

void preprocess_define(const char *name, const char *value) {
    define_macro(name, value, false, NULL, false);
}

/* ---- Preprocessor state ---- */
typedef struct {
    const char *src;
    const char *p;
    const char *filename;
    int line;
    Buf out;
    const char **include_paths;
    Arena *arena;
    int if_depth;
    int skip_depth;  /* > 0 means skipping */
    bool in_block_comment; /* persistent across lines */
} PPState;

static void pp_init(PPState *pp, const char *src, const char *filename,
                    const char **include_paths, Arena *arena) {
    pp->src = src;
    pp->p = src;
    pp->filename = filename;
    pp->line = 1;
    buf_init(&pp->out);
    pp->include_paths = include_paths;
    pp->arena = arena;
    pp->if_depth = 0;
    pp->skip_depth = 0;
    pp->in_block_comment = false;
}

static char pp_advance(PPState *pp) {
    char c = *pp->p++;
    if (c == '\r' && *pp->p == '\n') { pp->p++; c = '\n'; }
    if (c == '\n') pp->line++;
    return c;
}

static void pp_skip_line(PPState *pp) {
    while (*pp->p && *pp->p != '\n' && *pp->p != '\r') pp->p++;
}

static void pp_skip_whitespace_inline(PPState *pp) {
    while (*pp->p == ' ' || *pp->p == '\t') pp->p++;
}

/* Read an identifier at current position */
static const char *pp_read_ident(PPState *pp) {
    const char *start = pp->p;
    while (isalnum((unsigned char)*pp->p) || *pp->p == '_') pp->p++;
    if (pp->p == start) return NULL;
    size_t len = (size_t)(pp->p - start);
    char *s = malloc(len + 1);
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/* Check for line continuation: backslash followed by newline (handles \r\n too) */
static bool is_line_continuation(const char *p) {
    if (*p != '\\') return false;
    if (p[1] == '\n') return true;
    if (p[1] == '\r' && p[2] == '\n') return true;
    return false;
}

static int skip_line_continuation(const char *p) {
    /* Returns number of chars to skip */
    if (p[1] == '\n') return 2;
    if (p[1] == '\r' && p[2] == '\n') return 3;
    return 0;
}

/* Read rest of line as macro body, stripping backslash-newline continuations */
static const char *pp_read_line(PPState *pp) {
    pp_skip_whitespace_inline(pp);
    Buf body;
    buf_init(&body);
    while (*pp->p && *pp->p != '\n' && *pp->p != '\r') {
        /* Handle line continuation */
        if (is_line_continuation(pp->p)) {
            pp->p += skip_line_continuation(pp->p);
            pp->line++;
            /* Skip leading whitespace on continued line */
            while (*pp->p == ' ' || *pp->p == '\t') pp->p++;
            /* Ensure single space separator */
            if (body.len > 0) buf_push(&body, ' ');
            continue;
        }
        /* Strip C-style block comments */
        if (pp->p[0] == '/' && pp->p[1] == '*') {
            pp->p += 2;
            while (*pp->p && !(pp->p[0] == '*' && pp->p[1] == '/')) {
                if (*pp->p == '\n' || *pp->p == '\r') pp->line++;
                pp->p++;
            }
            if (*pp->p) pp->p += 2; /* skip close */
            /* Replace comment with a single space */
            if (body.len > 0) buf_push(&body, ' ');
            continue;
        }
        /* Strip C++ line comments - rest of line is comment */
        if (pp->p[0] == '/' && pp->p[1] == '/') {
            while (*pp->p && *pp->p != '\n' && *pp->p != '\r') pp->p++;
            break;
        }
        buf_push(&body, *pp->p++);
    }
    /* Trim trailing whitespace */
    while (body.len > 0 && (body.data[body.len-1] == ' ' || body.data[body.len-1] == '\t'))
        body.len--;
    return buf_detach(&body);
}

/* Evaluate simple preprocessor constant expression */
static long long pp_eval_expr(const char *expr);

static long long pp_eval_primary(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;

    if (**p == '(') {
        (*p)++;
        long long val = pp_eval_expr(*p);
        /* Simplified: skip to matching ) */
        int depth = 1;
        while (**p && depth > 0) {
            if (**p == '(') depth++;
            if (**p == ')') depth--;
            (*p)++;
        }
        return val;
    }

    if (**p == '!') {
        (*p)++;
        return !pp_eval_primary(p);
    }

    if (**p == '-') {
        (*p)++;
        return -pp_eval_primary(p);
    }

    if (**p == '+') {
        (*p)++;
        return pp_eval_primary(p);
    }

    /* defined(X) or defined X */
    if (strncmp(*p, "defined", 7) == 0 && !isalnum((unsigned char)(*p)[7]) && (*p)[7] != '_') {
        *p += 7;
        while (**p == ' ' || **p == '\t') (*p)++;
        bool paren = false;
        if (**p == '(') { paren = true; (*p)++; }
        while (**p == ' ' || **p == '\t') (*p)++;
        const char *start = *p;
        while (isalnum((unsigned char)**p) || **p == '_') (*p)++;
        size_t len = (size_t)(*p - start);
        char name[256];
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, start, len);
        name[len] = '\0';
        if (paren) {
            while (**p == ' ' || **p == '\t') (*p)++;
            if (**p == ')') (*p)++;
        }
        return find_macro(name) ? 1 : 0;
    }

    /* Number */
    if (isdigit((unsigned char)**p)) {
        long long val = strtoll(*p, (char **)p, 0);
        /* Skip suffix */
        while (**p == 'u' || **p == 'U' || **p == 'l' || **p == 'L') (*p)++;
        return val;
    }

    /* Character literal */
    if (**p == '\'') {
        (*p)++;
        long long val = **p;
        (*p)++;
        if (**p == '\'') (*p)++;
        return val;
    }

    /* Unknown identifier evaluates to 0 in preprocessor */
    if (isalpha((unsigned char)**p) || **p == '_') {
        while (isalnum((unsigned char)**p) || **p == '_') (*p)++;
        return 0;
    }

    return 0;
}

static long long pp_eval_expr(const char *expr) {
    const char *p = expr;
    long long lhs = pp_eval_primary(&p);

    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '+' && p[1] != '+') { p++; lhs = lhs + pp_eval_primary(&p); }
        else if (*p == '-' && p[1] != '-') { p++; lhs = lhs - pp_eval_primary(&p); }
        else if (*p == '*') { p++; lhs = lhs * pp_eval_primary(&p); }
        else if (*p == '/') { p++; long long r = pp_eval_primary(&p); lhs = r ? lhs / r : 0; }
        else if (*p == '%') { p++; long long r = pp_eval_primary(&p); lhs = r ? lhs % r : 0; }
        else if (*p == '<' && p[1] == '<') { p+=2; lhs = lhs << pp_eval_primary(&p); }
        else if (*p == '>' && p[1] == '>') { p+=2; lhs = lhs >> pp_eval_primary(&p); }
        else if (*p == '<' && p[1] == '=') { p+=2; lhs = lhs <= pp_eval_primary(&p); }
        else if (*p == '>' && p[1] == '=') { p+=2; lhs = lhs >= pp_eval_primary(&p); }
        else if (*p == '<') { p++; lhs = lhs < pp_eval_primary(&p); }
        else if (*p == '>') { p++; lhs = lhs > pp_eval_primary(&p); }
        else if (*p == '=' && p[1] == '=') { p+=2; lhs = lhs == pp_eval_primary(&p); }
        else if (*p == '!' && p[1] == '=') { p+=2; lhs = lhs != pp_eval_primary(&p); }
        else if (*p == '&' && p[1] == '&') { p+=2; lhs = lhs && pp_eval_primary(&p); }
        else if (*p == '|' && p[1] == '|') { p+=2; lhs = lhs || pp_eval_primary(&p); }
        else if (*p == '&' && p[1] != '&') { p++; lhs = lhs & pp_eval_primary(&p); }
        else if (*p == '|' && p[1] != '|') { p++; lhs = lhs | pp_eval_primary(&p); }
        else if (*p == '^') { p++; lhs = lhs ^ pp_eval_primary(&p); }
        else break;
    }
    return lhs;
}

/* Replace defined(X) / defined X with 1 or 0 before macro expansion.
 * Per C99 6.10.1, the defined operator must be processed before
 * macro expansion to prevent the operand from being expanded. */
static char *pp_replace_defined(const char *expr) {
    Buf out;
    buf_init(&out);
    const char *p = expr;
    while (*p) {
        if (strncmp(p, "defined", 7) == 0 &&
            !isalnum((unsigned char)p[7]) && p[7] != '_') {
            const char *start = p;
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            bool paren = false;
            if (*p == '(') { paren = true; p++; }
            while (*p == ' ' || *p == '\t') p++;
            const char *nstart = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            size_t len = (size_t)(p - nstart);
            char name[256];
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, nstart, len);
            name[len] = '\0';
            if (paren) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ')') p++;
            }
            buf_push(&out, find_macro(name) ? '1' : '0');
        } else {
            buf_push(&out, *p);
            p++;
        }
    }
    buf_push(&out, '\0');
    return out.data;
}

/* Macro expansion with rescanning */
static void expand_macros_r(const char *input, Buf *out, const char *filename, int line, int depth);

static void expand_macros(const char *input, Buf *out, const char *filename, int line) {
    expand_macros_r(input, out, filename, line, 0);
}

static void expand_macros_r(const char *input, Buf *out, const char *filename, int line, int depth) {
    if (depth > 32) {
        /* Prevent infinite recursion */
        buf_append(out, input, strlen(input));
        return;
    }
    const char *p = input;
    while (*p) {
        /* Skip string literals */
        if (*p == '"') {
            buf_push(out, *p++);
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) { buf_push(out, *p++); }
                buf_push(out, *p++);
            }
            if (*p) buf_push(out, *p++);
            continue;
        }
        if (*p == '\'') {
            buf_push(out, *p++);
            while (*p && *p != '\'') {
                if (*p == '\\' && p[1]) { buf_push(out, *p++); }
                buf_push(out, *p++);
            }
            if (*p) buf_push(out, *p++);
            continue;
        }

        /* Identifier - check for macro */
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            size_t len = (size_t)(p - start);
            char name[256];
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, start, len);
            name[len] = '\0';

            /* Built-in macros */
            if (strcmp(name, "__LINE__") == 0) {
                buf_printf(out, "%d", line);
                continue;
            }
            if (strcmp(name, "__FILE__") == 0) {
                buf_printf(out, "\"%s\"", filename);
                continue;
            }
            if (strcmp(name, "__DATE__") == 0) {
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                char date[20];
                strftime(date, sizeof(date), "\"%b %d %Y\"", tm);
                buf_append(out, date, strlen(date));
                continue;
            }
            if (strcmp(name, "__TIME__") == 0) {
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                char tstr[20];
                strftime(tstr, sizeof(tstr), "\"%H:%M:%S\"", tm);
                buf_append(out, tstr, strlen(tstr));
                continue;
            }

            Macro *m = find_macro(name);
            if (m && !m->is_func) {
                /* Object-like macro: expand body (rescan for nested macros) */
                expand_macros_r(m->body, out, filename, line, depth + 1);
                continue;
            }
            if (m && m->is_func && *p == '(') {
                /* Function-like macro */
                p++; /* skip ( */

                /* Collect arguments */
                char *args[64];
                int nargs = 0;
                int pdepth = 1;

                while (pdepth > 0 && *p && nargs < 64) {
                    while (*p == ' ' || *p == '\t') p++;
                    const char *astart = p;
                    Buf abuf;
                    buf_init(&abuf);

                    while (*p && pdepth > 0) {
                        if (*p == '(') { pdepth++; buf_push(&abuf, *p++); }
                        else if (*p == ')') {
                            pdepth--;
                            if (pdepth > 0) buf_push(&abuf, *p++);
                            else p++;
                        } else if (*p == ',' && pdepth == 1) {
                            p++;
                            break;
                        } else if (*p == '"') {
                            buf_push(&abuf, *p++);
                            while (*p && *p != '"') {
                                if (*p == '\\' && p[1]) buf_push(&abuf, *p++);
                                buf_push(&abuf, *p++);
                            }
                            if (*p) buf_push(&abuf, *p++);
                        } else {
                            buf_push(&abuf, *p++);
                        }
                    }
                    (void)astart;
                    args[nargs++] = buf_detach(&abuf);
                }

                /* Substitute parameters in macro body into temp buffer */
                Buf tmpbuf;
                buf_init(&tmpbuf);
                const char *bp = m->body;
                while (*bp) {
                    if (isalpha((unsigned char)*bp) || *bp == '_') {
                        const char *bs = bp;
                        while (isalnum((unsigned char)*bp) || *bp == '_') bp++;
                        size_t blen = (size_t)(bp - bs);

                        /* Check if it's __VA_ARGS__ */
                        if (blen == 12 && memcmp(bs, "__VA_ARGS__", 11) == 0) {
                            int param_count = 0;
                            for (MacroParam *mp = m->params; mp; mp = mp->next) param_count++;
                            for (int i = param_count; i < nargs; i++) {
                                if (i > param_count) buf_push(&tmpbuf, ',');
                                buf_append(&tmpbuf, args[i], strlen(args[i]));
                            }
                            continue;
                        }

                        /* Check if matches a parameter */
                        int pi = 0;
                        bool found = false;
                        for (MacroParam *mp = m->params; mp; mp = mp->next, pi++) {
                            if (strlen(mp->name) == blen && memcmp(mp->name, bs, blen) == 0) {
                                if (pi < nargs) {
                                    buf_append(&tmpbuf, args[pi], strlen(args[pi]));
                                }
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            buf_append(&tmpbuf, bs, blen);
                        }
                    } else if (*bp == '#' && bp[1] == '#') {
                        /* Token pasting: remove whitespace around ## */
                        while (tmpbuf.len > 0 && (tmpbuf.data[tmpbuf.len-1] == ' ' || tmpbuf.data[tmpbuf.len-1] == '\t'))
                            tmpbuf.len--;
                        bp += 2;
                        while (*bp == ' ' || *bp == '\t') bp++;
                    } else if (*bp == '#') {
                        /* Stringification */
                        bp++;
                        while (*bp == ' ' || *bp == '\t') bp++;
                        const char *ns = bp;
                        while (isalnum((unsigned char)*bp) || *bp == '_') bp++;
                        size_t nlen = (size_t)(bp - ns);

                        int pi = 0;
                        for (MacroParam *mp = m->params; mp; mp = mp->next, pi++) {
                            if (strlen(mp->name) == nlen && memcmp(mp->name, ns, nlen) == 0) {
                                buf_push(&tmpbuf, '"');
                                if (pi < nargs) {
                                    for (const char *a = args[pi]; *a; a++) {
                                        if (*a == '"' || *a == '\\') buf_push(&tmpbuf, '\\');
                                        buf_push(&tmpbuf, *a);
                                    }
                                }
                                buf_push(&tmpbuf, '"');
                                break;
                            }
                        }
                    } else {
                        buf_push(&tmpbuf, *bp++);
                    }
                }

                /* Rescan expanded text for nested macros */
                char *expanded = buf_detach(&tmpbuf);
                expand_macros_r(expanded, out, filename, line, depth + 1);
                free(expanded);

                /* Free args */
                for (int i = 0; i < nargs; i++) free(args[i]);
                continue;
            }

            /* Not a macro - output as is */
            buf_append(out, start, len);
            continue;
        }

        buf_push(out, *p++);
    }
}

/* ---- Process a single #include ---- */
static void pp_include(PPState *pp, const char *path, bool is_system) {
    char fullpath[1024];
    char *content = NULL;

    if (!is_system) {
        /* Try relative to current file first */
        const char *dir = pp->filename;
        const char *slash = strrchr(dir, '/');
        const char *bslash = strrchr(dir, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        if (slash) {
            int dirlen = (int)(slash - dir + 1);
            snprintf(fullpath, sizeof(fullpath), "%.*s%s", dirlen, dir, path);
            content = read_file(fullpath, NULL);
        }
    }

    /* Try path directly (relative to cwd) */
    if (!content) {
        snprintf(fullpath, sizeof(fullpath), "%s", path);
        content = read_file(fullpath, NULL);
    }

    if (!content && pp->include_paths) {
        for (const char **ip = pp->include_paths; *ip; ip++) {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", *ip, path);
            content = read_file(fullpath, NULL);
            if (content) break;
        }
    }

    if (!content) {
        /* For standard headers, provide minimal built-in definitions */
        if (strcmp(path, "stdio.h") == 0 || strcmp(path, "stdlib.h") == 0 ||
            strcmp(path, "string.h") == 0 || strcmp(path, "math.h") == 0 ||
            strcmp(path, "ctype.h") == 0 || strcmp(path, "assert.h") == 0 ||
            strcmp(path, "stdarg.h") == 0 || strcmp(path, "stddef.h") == 0 ||
            strcmp(path, "stdbool.h") == 0 || strcmp(path, "stdint.h") == 0 ||
            strcmp(path, "limits.h") == 0 || strcmp(path, "float.h") == 0 ||
            strcmp(path, "errno.h") == 0 || strcmp(path, "time.h") == 0 ||
            strcmp(path, "signal.h") == 0 || strcmp(path, "setjmp.h") == 0) {
            /* Silently skip - runtime.js provides these */
            buf_printf(&pp->out, "\n/* #include <%s> provided by runtime */\n", path);
            return;
        }
        error_noloc("cannot find include file '%s'", path);
        return;
    }

    /* Recursively preprocess the included file */
    char *result = preprocess(content, fullpath, pp->include_paths, pp->arena);
    if (result) {
        buf_append(&pp->out, result, strlen(result));
        buf_push(&pp->out, '\n');
    }
    free(content);
}

/* ---- Main preprocessor loop ---- */
char *preprocess(const char *src, const char *filename,
                 const char **include_paths, Arena *arena) {
    PPState pp;
    pp_init(&pp, src, filename, include_paths, arena);

    /* Setup predefined macros */
    static bool initialized = false;
    if (!initialized) {
        define_macro("__STDC__", "1", false, NULL, false);
        define_macro("__STDC_VERSION__", "199901L", false, NULL, false);
        define_macro("__STDC_HOSTED__", "1", false, NULL, false);
        define_macro("NULL", "((void*)0)", false, NULL, false);
        define_macro("true", "1", false, NULL, false);
        define_macro("false", "0", false, NULL, false);
        define_macro("bool", "_Bool", false, NULL, false);
        define_macro("EOF", "(-1)", false, NULL, false);

        /* stdint types */
        define_macro("int8_t", "signed char", false, NULL, false);
        define_macro("uint8_t", "unsigned char", false, NULL, false);
        define_macro("int16_t", "short", false, NULL, false);
        define_macro("uint16_t", "unsigned short", false, NULL, false);
        define_macro("int32_t", "int", false, NULL, false);
        define_macro("uint32_t", "unsigned int", false, NULL, false);
        define_macro("int64_t", "long long", false, NULL, false);
        define_macro("uint64_t", "unsigned long long", false, NULL, false);
        define_macro("size_t", "unsigned int", false, NULL, false);
        define_macro("ptrdiff_t", "int", false, NULL, false);
        define_macro("intptr_t", "int", false, NULL, false);
        define_macro("uintptr_t", "unsigned int", false, NULL, false);

        /* limits */
        define_macro("INT_MIN", "(-2147483647-1)", false, NULL, false);
        define_macro("INT_MAX", "2147483647", false, NULL, false);
        define_macro("UINT_MAX", "4294967295u", false, NULL, false);
        define_macro("LONG_MIN", "(-2147483647L-1)", false, NULL, false);
        define_macro("LONG_MAX", "2147483647L", false, NULL, false);
        define_macro("CHAR_BIT", "8", false, NULL, false);
        define_macro("SCHAR_MIN", "(-128)", false, NULL, false);
        define_macro("SCHAR_MAX", "127", false, NULL, false);
        define_macro("UCHAR_MAX", "255", false, NULL, false);
        define_macro("SHRT_MIN", "(-32768)", false, NULL, false);
        define_macro("SHRT_MAX", "32767", false, NULL, false);
        define_macro("USHRT_MAX", "65535", false, NULL, false);

        /* errno */
        define_macro("errno", "(*__errno_ptr())", false, NULL, false);
        define_macro("EINVAL", "22", false, NULL, false);
        define_macro("ERANGE", "34", false, NULL, false);

        /* stdio constants */
        define_macro("SEEK_SET", "0", false, NULL, false);
        define_macro("SEEK_CUR", "1", false, NULL, false);
        define_macro("SEEK_END", "2", false, NULL, false);
        define_macro("CLOCKS_PER_SEC", "1000", false, NULL, false);

        /* time.h types */
        define_macro("time_t", "long", false, NULL, false);
        define_macro("clock_t", "long", false, NULL, false);

        /* signal.h types and constants */
        define_macro("sig_atomic_t", "int", false, NULL, false);
        define_macro("SIGINT", "2", false, NULL, false);
        define_macro("SIGTERM", "15", false, NULL, false);
        define_macro("SIG_DFL", "((void(*)(int))0)", false, NULL, false);
        define_macro("SIG_IGN", "((void(*)(int))1)", false, NULL, false);

        /* BUFSIZ */
        define_macro("BUFSIZ", "8192", false, NULL, false);

        /* EXIT_SUCCESS / EXIT_FAILURE */
        define_macro("EXIT_SUCCESS", "0", false, NULL, false);
        define_macro("EXIT_FAILURE", "1", false, NULL, false);

        /* __cplusplus guard - not defined (we're C) */

        initialized = true;
    }

    /* Add line marker */
    buf_printf(&pp.out, "# 1 \"%s\"\n", filename);

    while (*pp.p) {
        /* Skip leading whitespace to find potential # directive */
        const char *line_ws_start = pp.p;
        while (*pp.p == ' ' || *pp.p == '\t') pp.p++;

        /* Check for # at beginning of line (possibly after whitespace) */
        if (*pp.p == '#') {
            pp.p++;
            pp_skip_whitespace_inline(&pp);

            /* Read directive name */
            const char *dir = pp_read_ident(&pp);
            if (!dir) {
                pp_skip_line(&pp);
                continue;
            }

            if (strcmp(dir, "if") == 0) {
                pp.if_depth++;
                if (pp.skip_depth > 0) {
                    pp.skip_depth++;
                    pp_skip_line(&pp);
                } else {
                    const char *expr = pp_read_line(&pp);
                    /* Process defined() before macro expansion (C99 6.10.1) */
                    char *dexpr = pp_replace_defined(expr);
                    Buf expanded;
                    buf_init(&expanded);
                    expand_macros(dexpr, &expanded, pp.filename, pp.line);
                    free(dexpr);
                    char *estr = buf_detach(&expanded);
                    long long val = pp_eval_expr(estr);
                    free(estr);
                    if (!val) pp.skip_depth = 1;
                }
            } else if (strcmp(dir, "ifdef") == 0) {
                pp.if_depth++;
                pp_skip_whitespace_inline(&pp);
                const char *name = pp_read_ident(&pp);
                pp_skip_line(&pp);
                if (pp.skip_depth > 0) {
                    pp.skip_depth++;
                } else if (!name || !find_macro(name)) {
                    pp.skip_depth = 1;
                }
            } else if (strcmp(dir, "ifndef") == 0) {
                pp.if_depth++;
                pp_skip_whitespace_inline(&pp);
                const char *name = pp_read_ident(&pp);
                pp_skip_line(&pp);
                if (pp.skip_depth > 0) {
                    pp.skip_depth++;
                } else if (name && find_macro(name)) {
                    pp.skip_depth = 1;
                }
            } else if (strcmp(dir, "elif") == 0) {
                if (pp.skip_depth == 1) {
                    const char *expr = pp_read_line(&pp);
                    char *dexpr = pp_replace_defined(expr);
                    Buf expanded;
                    buf_init(&expanded);
                    expand_macros(dexpr, &expanded, pp.filename, pp.line);
                    free(dexpr);
                    char *estr = buf_detach(&expanded);
                    long long val = pp_eval_expr(estr);
                    free(estr);
                    if (val) pp.skip_depth = 0;
                } else if (pp.skip_depth == 0) {
                    pp.skip_depth = 1;
                    pp_skip_line(&pp);
                } else {
                    pp_skip_line(&pp);
                }
            } else if (strcmp(dir, "else") == 0) {
                pp_skip_line(&pp);
                if (pp.skip_depth == 1) pp.skip_depth = 0;
                else if (pp.skip_depth == 0) pp.skip_depth = 1;
            } else if (strcmp(dir, "endif") == 0) {
                pp_skip_line(&pp);
                if (pp.skip_depth > 0) pp.skip_depth--;
                pp.if_depth--;
            } else if (pp.skip_depth > 0) {
                pp_skip_line(&pp);
            } else if (strcmp(dir, "define") == 0) {
                pp_skip_whitespace_inline(&pp);
                const char *name = pp_read_ident(&pp);
                if (!name) { pp_skip_line(&pp); continue; }

                bool is_func = false;
                bool is_variadic = false;
                MacroParam *params = NULL;

                /* Function-like macro: ( immediately after name (no space) */
                if (*pp.p == '(') {
                    is_func = true;
                    pp.p++;
                    MacroParam head = {0};
                    MacroParam *cur = &head;
                    pp_skip_whitespace_inline(&pp);
                    while (*pp.p && *pp.p != ')') {
                        pp_skip_whitespace_inline(&pp);
                        if (*pp.p == '.' && pp.p[1] == '.' && pp.p[2] == '.') {
                            is_variadic = true;
                            pp.p += 3;
                            break;
                        }
                        const char *pname = pp_read_ident(&pp);
                        if (pname) {
                            MacroParam *mp = calloc(1, sizeof(MacroParam));
                            mp->name = pname;
                            cur->next = mp;
                            cur = mp;
                        }
                        pp_skip_whitespace_inline(&pp);
                        if (*pp.p == ',') pp.p++;
                    }
                    if (*pp.p == ')') pp.p++;
                    params = head.next;
                }

                const char *body = pp_read_line(&pp);
                define_macro(name, body, is_func, params, is_variadic);
            } else if (strcmp(dir, "undef") == 0) {
                pp_skip_whitespace_inline(&pp);
                const char *name = pp_read_ident(&pp);
                if (name) undef_macro(name);
                pp_skip_line(&pp);
            } else if (strcmp(dir, "include") == 0) {
                pp_skip_whitespace_inline(&pp);
                bool is_system = false;
                char path[256];
                if (*pp.p == '<') {
                    is_system = true;
                    pp.p++;
                    int i = 0;
                    while (*pp.p && *pp.p != '>' && i < 255)
                        path[i++] = *pp.p++;
                    path[i] = '\0';
                    if (*pp.p == '>') pp.p++;
                } else if (*pp.p == '"') {
                    pp.p++;
                    int i = 0;
                    while (*pp.p && *pp.p != '"' && i < 255)
                        path[i++] = *pp.p++;
                    path[i] = '\0';
                    if (*pp.p == '"') pp.p++;
                } else {
                    error_noloc("expected filename after #include");
                    pp_skip_line(&pp);
                    continue;
                }
                pp_skip_line(&pp);
                pp_include(&pp, path, is_system);
                buf_printf(&pp.out, "# %d \"%s\"\n", pp.line, pp.filename);
            } else if (strcmp(dir, "pragma") == 0) {
                pp_skip_line(&pp); /* ignore */
            } else if (strcmp(dir, "error") == 0) {
                pp_skip_whitespace_inline(&pp);
                const char *msg = pp_read_line(&pp);
                error_noloc("#error %s", msg);
            } else if (strcmp(dir, "line") == 0) {
                pp_skip_whitespace_inline(&pp);
                pp.line = (int)strtol(pp.p, (char **)&pp.p, 10);
                pp_skip_line(&pp);
            } else {
                pp_skip_line(&pp); /* unknown directive */
            }
            free((void *)dir);
            continue;
        }

        /* Not a directive: restore position to include leading whitespace */
        pp.p = line_ws_start;

        /* Skipping */
        if (pp.skip_depth > 0) {
            if (*pp.p == '\n' || *pp.p == '\r') {
                buf_push(&pp.out, pp_advance(&pp));
            } else {
                pp.p++;
            }
            continue;
        }

        /* Regular line: expand macros and output */
        if (*pp.p == '\n' || *pp.p == '\r') {
            buf_push(&pp.out, pp_advance(&pp));
            continue;
        }

        /* Read the full line and expand macros.
         * If we detect unbalanced parentheses (multi-line macro invocation),
         * keep reading additional lines until balanced. */
        Buf linebuf;
        buf_init(&linebuf);
        int paren_depth = 0;
        bool in_string = false;
        bool in_char = false;
        do {
            while (*pp.p && *pp.p != '\n' && *pp.p != '\r') {
                /* Handle line continuation */
                if (is_line_continuation(pp.p)) {
                    pp.p += skip_line_continuation(pp.p);
                    pp.line++;
                    continue;
                }
                char ch = *pp.p;
                /* Inside block comment: just look for end */
                if (pp.in_block_comment) {
                    if (ch == '*' && pp.p[1] == '/') {
                        buf_push(&linebuf, *pp.p++);
                        buf_push(&linebuf, *pp.p++);
                        pp.in_block_comment = false;
                    } else {
                        buf_push(&linebuf, *pp.p++);
                    }
                    continue;
                }
                /* Track parenthesis depth outside string/char/comment */
                if (!in_string && !in_char) {
                    if (ch == '/' && pp.p[1] == '*') {
                        pp.in_block_comment = true;
                        buf_push(&linebuf, *pp.p++);
                        buf_push(&linebuf, *pp.p++);
                        continue;
                    }
                    if (ch == '/' && pp.p[1] == '/') {
                        /* Line comment: output rest of line */
                        while (*pp.p && *pp.p != '\n' && *pp.p != '\r')
                            buf_push(&linebuf, *pp.p++);
                        break;
                    }
                    if (ch == '"') in_string = true;
                    else if (ch == '\'') in_char = true;
                    else if (ch == '(') paren_depth++;
                    else if (ch == ')') { if (paren_depth > 0) paren_depth--; }
                }
                else if (in_string) {
                    if (ch == '\\' && pp.p[1]) { buf_push(&linebuf, *pp.p++); }
                    else if (ch == '"') in_string = false;
                }
                else if (in_char) {
                    if (ch == '\\' && pp.p[1]) { buf_push(&linebuf, *pp.p++); }
                    else if (ch == '\'') in_char = false;
                }
                buf_push(&linebuf, *pp.p++);
            }
            /* If parens are balanced and not in a block comment, we're done */
            if (paren_depth <= 0 && !pp.in_block_comment) break;
            /* Unbalanced: consume the newline and keep reading */
            if (*pp.p == '\r' && pp.p[1] == '\n') {
                buf_push(&linebuf, ' '); /* replace newline with space */
                pp.p += 2;
            } else if (*pp.p == '\n' || *pp.p == '\r') {
                buf_push(&linebuf, ' ');
                pp.p++;
            } else {
                break; /* EOF */
            }
            pp.line++;
        } while (*pp.p);

        char *line = buf_detach(&linebuf);
        Buf expanded;
        buf_init(&expanded);
        expand_macros(line, &expanded, pp.filename, pp.line);
        buf_append(&pp.out, expanded.data, expanded.len);
        buf_free(&expanded);
        free(line);
    }

    return buf_detach(&pp.out);
}
