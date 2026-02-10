// SPDX-License-Identifier: Zlib
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015-2020 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* COMPILE TIME OPTIONS */

/* Exponentiation associativity:
For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

#include "tinyexpr.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif

#ifndef ULONG_MAX
#define ULONG_MAX 4294967295UL
#endif


typedef double (*te_fun2)(double, double);

enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};


enum {TE_CONSTANT = 1};


typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
    int lookup_len;
} state;


#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
/* Non-variadic helper functions for c99js compatibility */
static te_expr *new_expr1(const int type, const te_expr *p0) {
    const te_expr *params[1];
    params[0] = p0;
    return new_expr(type, params);
}
static te_expr *new_expr2(const int type, const te_expr *p0, const te_expr *p1) {
    const te_expr *params[2];
    params[0] = p0;
    params[1] = p1;
    return new_expr(type, params);
}


static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = malloc(size);
    if (ret == NULL) { return NULL; }

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}


void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);     /* Falls through. */
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);     /* Falls through. */
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);     /* Falls through. */
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);     /* Falls through. */
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);     /* Falls through. */
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);     /* Falls through. */
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    }
}


void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi(void) {return 3.14159265358979323846;}
static double e(void) {return 2.71828182845904523536;}
static double fac(double a) {
    if (a < 0.0)
        return NAN;
    if (a > 170.0)
        return INFINITY;
    double result = 1.0;
    int i;
    int n = (int)a;
    for (i = 2; i <= n; i++) {
        result = result * (double)i;
    }
    return result;
}
static double ncr(double n, double r) {
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > 170.0 || r > 170.0) return INFINITY;
    int un = (int)n;
    int ur = (int)r;
    int i;
    double result = 1.0;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        result = result * (double)(un - ur + i);
        result = result / (double)i;
    }
    return result;
}
static double npr(double n, double r) {return ncr(n, r) * fac(r);}

#ifdef _MSC_VER
#pragma function (ceil)
#pragma function (floor)
#endif


/* Wrapper functions for standard library math functions.
   c99js cannot create function pointers for built-in functions,
   so we wrap them in user-defined functions. */
static double te_fabs(double x) { return fabs(x); }
static double te_acos(double x) { return acos(x); }
static double te_asin(double x) { return asin(x); }
static double te_atan(double x) { return atan(x); }
static double te_atan2(double y, double x) { return atan2(y, x); }
static double te_ceil(double x) { return ceil(x); }
static double te_cos(double x) { return cos(x); }
static double te_exp(double x) { return exp(x); }
static double te_floor(double x) { return floor(x); }
static double te_log(double x) { return log(x); }
static double te_log10(double x) { return log10(x); }
static double te_pow(double x, double y) { return pow(x, y); }
static double te_sin(double x) { return sin(x); }
static double te_sqrt(double x) { return sqrt(x); }
static double te_tan(double x) { return tan(x); }
static double te_fmod(double x, double y) { return fmod(x, y); }

/* Runtime-initialized function table to avoid static function pointer init */
#define TE_NUM_BUILTINS 23
static te_variable functions[TE_NUM_BUILTINS];
static int functions_initialized = 0;

static void te_init_functions(void) {
    if (functions_initialized) return;
    functions_initialized = 1;
    int idx = 0;
    functions[idx].name = "abs";
    functions[idx].address = (const void*)te_fabs;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "acos";
    functions[idx].address = (const void*)te_acos;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "asin";
    functions[idx].address = (const void*)te_asin;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "atan";
    functions[idx].address = (const void*)te_atan;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "atan2";
    functions[idx].address = (const void*)te_atan2;
    functions[idx].type = TE_FUNCTION2 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "ceil";
    functions[idx].address = (const void*)te_ceil;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "cos";
    functions[idx].address = (const void*)te_cos;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "e";
    functions[idx].address = (const void*)e;
    functions[idx].type = TE_FUNCTION0 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "exp";
    functions[idx].address = (const void*)te_exp;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "fac";
    functions[idx].address = (const void*)fac;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "floor";
    functions[idx].address = (const void*)te_floor;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "ln";
    functions[idx].address = (const void*)te_log;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "log";
    functions[idx].address = (const void*)te_log;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "log";
    functions[idx].address = (const void*)te_log10;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "log10";
    functions[idx].address = (const void*)te_log10;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "ncr";
    functions[idx].address = (const void*)ncr;
    functions[idx].type = TE_FUNCTION2 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "npr";
    functions[idx].address = (const void*)npr;
    functions[idx].type = TE_FUNCTION2 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "pi";
    functions[idx].address = (const void*)pi;
    functions[idx].type = TE_FUNCTION0 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "pow";
    functions[idx].address = (const void*)te_pow;
    functions[idx].type = TE_FUNCTION2 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "sin";
    functions[idx].address = (const void*)te_sin;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "sqrt";
    functions[idx].address = (const void*)te_sqrt;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = "tan";
    functions[idx].address = (const void*)te_tan;
    functions[idx].type = TE_FUNCTION1 | TE_FLAG_PURE;
    functions[idx].context = 0;
    idx++;
    functions[idx].name = 0;
    functions[idx].address = 0;
    functions[idx].type = 0;
    functions[idx].context = 0;
}


static const te_variable *find_builtin(const char *name, int len) {
    int imin = 0;
    int imax = TE_NUM_BUILTINS - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double comma(double a, double b) {(void)a; return b;}


void next_token(state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (isalpha(s->next[0])) {
                const char *start;
                start = s->next;
                while (isalpha(s->next[0]) || isdigit(s->next[0]) || (s->next[0] == '_')) s->next++;
                
                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->function = var->address;
                            break;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->function = add; break;
                    case '-': s->type = TOK_INFIX; s->function = sub; break;
                    case '*': s->type = TOK_INFIX; s->function = mul; break;
                    case '/': s->type = TOK_INFIX; s->function = divide; break;
                    case '^': s->type = TOK_INFIX; s->function = te_pow; break;
                    case '%': s->type = TOK_INFIX; s->function = te_fmod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            if (ret == NULL) { return NULL; }

            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            if (ret == NULL) { return NULL; }

            ret->bound = s->bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            if (ret == NULL) { return NULL; }

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[0] = s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            if (ret == NULL) { return NULL; }

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[1] = s->context;
            next_token(s);
            ret->parameters[0] = power(s);
            if (ret->parameters[0] == NULL) { te_free(ret); return NULL; }
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            if (ret == NULL) { return NULL; }

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
                    if (ret->parameters[i] == NULL) { te_free(ret); return NULL; }

                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            if (ret == NULL) { return NULL; }

            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            if (ret == NULL) { return NULL; }

            s->type = TOK_ERROR;
            ret->value = NAN;
            break;
    }

    return ret;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        if (s->function == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        te_expr *b = base(s);
        if (b == NULL) { return NULL; }

        ret = new_expr1(TE_FUNCTION1 | TE_FLAG_PURE, b);
        if (ret == NULL) { te_free(b); return NULL; }

        ret->function = negate;
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);
    if (ret == NULL) { return NULL; }

    int neg = 0;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) && ret->function == negate) {
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
        neg = 1;
    }

    te_expr *insertion = 0;

    while (s->type == TOK_INFIX && (s->function == te_pow)) {
        te_fun2 t = s->function;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr *p = power(s);
            if (p == NULL) { te_free(ret); return NULL; }

            te_expr *insert = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], p);
            if (insert == NULL) { te_free(p); te_free(ret); return NULL; }

            insert->function = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        } else {
            te_expr *p = power(s);
            if (p == NULL) { te_free(ret); return NULL; }

            te_expr *prev = ret;
            ret = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
            if (ret == NULL) { te_free(p); te_free(prev); return NULL; }

            ret->function = t;
            insertion = ret;
        }
    }

    if (neg) {
        te_expr *prev = ret;
        ret = new_expr1(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        if (ret == NULL) { te_free(prev); return NULL; }

        ret->function = negate;
    }

    return ret;
}
#else
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);
    if (ret == NULL) { return NULL; }

    while (s->type == TOK_INFIX && (s->function == te_pow)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *p = power(s);
        if (p == NULL) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
        if (ret == NULL) { te_free(p); te_free(prev); return NULL; }

        ret->function = t;
    }

    return ret;
}
#endif



static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);
    if (ret == NULL) { return NULL; }

    while (s->type == TOK_INFIX && (s->function == mul || s->function == divide || s->function == te_fmod)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *f = factor(s);
        if (f == NULL) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, ret, f);
        if (ret == NULL) { te_free(f); te_free(prev); return NULL; }

        ret->function = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);
    if (ret == NULL) { return NULL; }

    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        te_fun2 t = s->function;
        next_token(s);
        te_expr *te = term(s);
        if (te == NULL) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, ret, te);
        if (ret == NULL) { te_free(te); te_free(prev); return NULL; }

        ret->function = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);
    if (ret == NULL) { return NULL; }

    while (s->type == TOK_SEP) {
        next_token(s);
        te_expr *e = expr(s);
        if (e == NULL) { te_free(ret); return NULL; }

        te_expr *prev = ret;
        ret = new_expr2(TE_FUNCTION2 | TE_FLAG_PURE, ret, e);
        if (ret == NULL) { te_free(e); te_free(prev); return NULL; }

        ret->function = comma;
    }

    return ret;
}


/* Typedef-based function pointer types for c99js compatibility */
typedef double (*te_fn0)(void);
typedef double (*te_fn1)(double);
typedef double (*te_fn2_eval)(double, double);
typedef double (*te_fn3)(double, double, double);
typedef double (*te_fn4)(double, double, double, double);
typedef double (*te_fn5)(double, double, double, double, double);
typedef double (*te_fn6)(double, double, double, double, double, double);
typedef double (*te_fn7)(double, double, double, double, double, double, double);
typedef double (*te_cfn0)(void*);
typedef double (*te_cfn1)(void*, double);
typedef double (*te_cfn2)(void*, double, double);
typedef double (*te_cfn3)(void*, double, double, double);
typedef double (*te_cfn4)(void*, double, double, double, double);
typedef double (*te_cfn5)(void*, double, double, double, double, double);
typedef double (*te_cfn6)(void*, double, double, double, double, double, double);
typedef double (*te_cfn7)(void*, double, double, double, double, double, double, double);

double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: { te_fn0 f = (te_fn0)n->function; return f(); }
                case 1: { te_fn1 f = (te_fn1)n->function; return f(te_eval(n->parameters[0])); }
                case 2: { te_fn2_eval f = (te_fn2_eval)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1])); }
                case 3: { te_fn3 f = (te_fn3)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2])); }
                case 4: { te_fn4 f = (te_fn4)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3])); }
                case 5: { te_fn5 f = (te_fn5)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4])); }
                case 6: { te_fn6 f = (te_fn6)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4]), te_eval(n->parameters[5])); }
                case 7: { te_fn7 f = (te_fn7)n->function; return f(te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4]), te_eval(n->parameters[5]), te_eval(n->parameters[6])); }
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: { te_cfn0 f = (te_cfn0)n->function; return f(n->parameters[0]); }
                case 1: { te_cfn1 f = (te_cfn1)n->function; return f(n->parameters[1], te_eval(n->parameters[0])); }
                case 2: { te_cfn2 f = (te_cfn2)n->function; return f(n->parameters[2], te_eval(n->parameters[0]), te_eval(n->parameters[1])); }
                case 3: { te_cfn3 f = (te_cfn3)n->function; return f(n->parameters[3], te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2])); }
                case 4: { te_cfn4 f = (te_cfn4)n->function; return f(n->parameters[4], te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3])); }
                case 5: { te_cfn5 f = (te_cfn5)n->function; return f(n->parameters[5], te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4])); }
                case 6: { te_cfn6 f = (te_cfn6)n->function; return f(n->parameters[6], te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4]), te_eval(n->parameters[5])); }
                case 7: { te_cfn7 f = (te_cfn7)n->function; return f(n->parameters[7], te_eval(n->parameters[0]), te_eval(n->parameters[1]), te_eval(n->parameters[2]), te_eval(n->parameters[3]), te_eval(n->parameters[4]), te_eval(n->parameters[5]), te_eval(n->parameters[6])); }
                default: return NAN;
            }

        default: return NAN;
    }

}

static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->value = value;
        }
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    te_init_functions();
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);
    if (root == NULL) {
        if (error) *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}


double te_interp(const char *expression, int *error) {
    te_init_functions();
    te_expr *n = te_compile(expression, 0, 0, error);

    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %d
", (int)(long)n->bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %d", (int)(long)n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
