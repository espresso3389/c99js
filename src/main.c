#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "lexer.h"
#include "preprocess.h"
#include "ast.h"
#include "type.h"
#include "symtab.h"
#include "parser.h"
#include "sema.h"
#include "codegen.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.c> [-o <output.js>]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>    Output file (default: stdout)\n");
    fprintf(stderr, "  -I <dir>     Add include search path\n");
    fprintf(stderr, "  -D <name>=<val>  Define preprocessor macro\n");
    fprintf(stderr, "  -E           Preprocess only\n");
    fprintf(stderr, "  --dump-ast   Print AST (for debugging)\n");
    fprintf(stderr, "  -h, --help   Show this help\n");
}

/* Declare built-in functions in symbol table */
static void register_builtins(SymTab *st, Arena *a) {
    SrcLoc loc = {"<builtin>", 0, 0};

    /* printf(const char *fmt, ...) -> int */
    Type *printf_ty = type_func(a, ty_int);
    Param *fmt_param = arena_calloc(a, sizeof(Param));
    fmt_param->name = "fmt";
    fmt_param->type = type_ptr(a, ty_char);
    printf_ty->params = fmt_param;
    printf_ty->is_variadic = true;
    Symbol *s;
    s = symtab_define(st, "printf", SYM_FUNC, printf_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fprintf", SYM_FUNC, printf_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "sprintf", SYM_FUNC, printf_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "snprintf", SYM_FUNC, printf_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "scanf", SYM_FUNC, printf_ty, loc); s->sc = SC_EXTERN;

    /* malloc(size_t) -> void* */
    Type *malloc_ty = type_func(a, type_ptr(a, ty_void));
    Param *size_p = arena_calloc(a, sizeof(Param));
    size_p->name = "size";
    size_p->type = ty_uint;
    malloc_ty->params = size_p;
    s = symtab_define(st, "malloc", SYM_FUNC, malloc_ty, loc); s->sc = SC_EXTERN;

    /* calloc(size_t, size_t) -> void* */
    Type *calloc_ty = type_func(a, type_ptr(a, ty_void));
    Param *cp1 = arena_calloc(a, sizeof(Param));
    cp1->name = "nmemb"; cp1->type = ty_uint;
    Param *cp2 = arena_calloc(a, sizeof(Param));
    cp2->name = "size"; cp2->type = ty_uint;
    cp1->next = cp2;
    calloc_ty->params = cp1;
    s = symtab_define(st, "calloc", SYM_FUNC, calloc_ty, loc); s->sc = SC_EXTERN;

    /* realloc(void*, size_t) -> void* */
    s = symtab_define(st, "realloc", SYM_FUNC, calloc_ty, loc); s->sc = SC_EXTERN;

    /* free(void*) -> void */
    Type *free_ty = type_func(a, ty_void);
    Param *fp = arena_calloc(a, sizeof(Param));
    fp->name = "ptr"; fp->type = type_ptr(a, ty_void);
    free_ty->params = fp;
    s = symtab_define(st, "free", SYM_FUNC, free_ty, loc); s->sc = SC_EXTERN;

    /* strlen, strcpy, etc -> various */
    Type *str_int_ty = type_func(a, ty_uint);
    Param *sp1 = arena_calloc(a, sizeof(Param));
    sp1->name = "s"; sp1->type = type_ptr(a, ty_char);
    str_int_ty->params = sp1;
    s = symtab_define(st, "strlen", SYM_FUNC, str_int_ty, loc); s->sc = SC_EXTERN;

    Type *str_ptr_ty = type_func(a, type_ptr(a, ty_char));
    str_ptr_ty->params = sp1;
    str_ptr_ty->is_variadic = true;
    s = symtab_define(st, "strcpy", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strncpy", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strcat", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strncat", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strchr", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strrchr", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strstr", SYM_FUNC, str_ptr_ty, loc); s->sc = SC_EXTERN;

    Type *cmp_ty = type_func(a, ty_int);
    cmp_ty->params = sp1;
    cmp_ty->is_variadic = true;
    s = symtab_define(st, "strcmp", SYM_FUNC, cmp_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "strncmp", SYM_FUNC, cmp_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "memcmp", SYM_FUNC, cmp_ty, loc); s->sc = SC_EXTERN;

    Type *memfn_ty = type_func(a, type_ptr(a, ty_void));
    memfn_ty->is_variadic = true;
    s = symtab_define(st, "memcpy", SYM_FUNC, memfn_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "memmove", SYM_FUNC, memfn_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "memset", SYM_FUNC, memfn_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "memchr", SYM_FUNC, memfn_ty, loc); s->sc = SC_EXTERN;

    /* stdlib */
    Type *atoi_ty = type_func(a, ty_int);
    atoi_ty->params = sp1;
    s = symtab_define(st, "atoi", SYM_FUNC, atoi_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "atof", SYM_FUNC, type_func(a, ty_double), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "abs", SYM_FUNC, atoi_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "labs", SYM_FUNC, type_func(a, ty_long), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "rand", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "srand", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "exit", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "abort", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "qsort", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;

    /* strtol(const char*, char**, int) -> long */
    Type *strtol_ty = type_func(a, ty_long);
    strtol_ty->is_variadic = true;
    s = symtab_define(st, "strtol", SYM_FUNC, strtol_ty, loc); s->sc = SC_EXTERN;

    /* strtod(const char*, char**) -> double */
    Type *strtod_ty = type_func(a, ty_double);
    strtod_ty->is_variadic = true;
    s = symtab_define(st, "strtod", SYM_FUNC, strtod_ty, loc); s->sc = SC_EXTERN;

    /* __errno_ptr() -> int* (used by errno macro) */
    Type *errno_ty = type_func(a, type_ptr(a, ty_int));
    s = symtab_define(st, "__errno_ptr", SYM_FUNC, errno_ty, loc); s->sc = SC_EXTERN;

    /* Math functions */
    Type *math_ty = type_func(a, ty_double);
    math_ty->is_variadic = true;
    const char *math_fns[] = {
        "sin","cos","tan","asin","acos","atan","atan2",
        "sqrt","pow","fabs","ceil","floor","fmod","log","log10","exp",
        "ldexp","frexp", NULL
    };
    for (int i = 0; math_fns[i]; i++) {
        s = symtab_define(st, math_fns[i], SYM_FUNC, math_ty, loc);
        s->sc = SC_EXTERN;
    }

    /* ctype */
    Type *ctype_ty = type_func(a, ty_int);
    ctype_ty->is_variadic = true;
    const char *ctype_fns[] = {
        "isalpha","isdigit","isalnum","isspace","isupper","islower",
        "ispunct","isprint","iscntrl","isxdigit","toupper","tolower", NULL
    };
    for (int i = 0; ctype_fns[i]; i++) {
        s = symtab_define(st, ctype_fns[i], SYM_FUNC, ctype_ty, loc);
        s->sc = SC_EXTERN;
    }

    /* I/O */
    s = symtab_define(st, "puts", SYM_FUNC, atoi_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "putchar", SYM_FUNC, atoi_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "getchar", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fopen", SYM_FUNC, type_func(a, type_ptr(a, ty_void)), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fclose", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fread", SYM_FUNC, type_func(a, ty_uint), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fwrite", SYM_FUNC, type_func(a, ty_uint), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fgets", SYM_FUNC, type_func(a, type_ptr(a, ty_char)), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fputs", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "feof", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fgetc", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fputc", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "fseek", SYM_FUNC, type_func(a, ty_int), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "ftell", SYM_FUNC, type_func(a, ty_long), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "rewind", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "assert", SYM_FUNC, type_func(a, ty_void), loc); s->sc = SC_EXTERN;

    /* Define FILE as void* for simplicity */
    Type *file_ty = type_ptr(a, ty_void);
    s = symtab_define(st, "stdin", SYM_VAR, file_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "stdout", SYM_VAR, file_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "stderr", SYM_VAR, file_ty, loc); s->sc = SC_EXTERN;

    /* typedef void* FILE */
    s = symtab_define(st, "FILE", SYM_TYPEDEF, file_ty, loc); s->sc = SC_TYPEDEF;

    /* typedef for va_list */
    s = symtab_define(st, "va_list", SYM_TYPEDEF, type_ptr(a, ty_void), loc);
    s->sc = SC_TYPEDEF;

    /* struct tm (opaque for time.h) */
    Type *tm_struct = type_struct(a, "tm");
    tm_struct->size = 36; tm_struct->align = 4;
    symtab_define_tag(st, "tm", tm_struct, loc);

    /* localtime(const long*) -> struct tm* */
    Type *localtime_ty = type_func(a, type_ptr(a, tm_struct));
    localtime_ty->is_variadic = true;
    s = symtab_define(st, "localtime", SYM_FUNC, localtime_ty, loc); s->sc = SC_EXTERN;

    /* strftime(char*, size_t, const char*, struct tm*) -> size_t */
    Type *strftime_ty = type_func(a, ty_uint);
    strftime_ty->is_variadic = true;
    s = symtab_define(st, "strftime", SYM_FUNC, strftime_ty, loc); s->sc = SC_EXTERN;

    /* difftime(time_t, time_t) -> double */
    Type *difftime_ty = type_func(a, ty_double);
    difftime_ty->is_variadic = true;
    s = symtab_define(st, "difftime", SYM_FUNC, difftime_ty, loc); s->sc = SC_EXTERN;

    /* strdup(const char*) -> char* */
    Type *strdup_ty = type_func(a, type_ptr(a, ty_char));
    strdup_ty->is_variadic = true;
    s = symtab_define(st, "strdup", SYM_FUNC, strdup_ty, loc); s->sc = SC_EXTERN;

    /* strtoll(const char*, char**, int) -> long long */
    Type *strtoll_ty = type_func(a, ty_llong);
    strtoll_ty->is_variadic = true;
    s = symtab_define(st, "strtoll", SYM_FUNC, strtoll_ty, loc); s->sc = SC_EXTERN;

    /* strtoul(const char*, char**, int) -> unsigned long */
    Type *strtoul_ty = type_func(a, ty_ulong);
    strtoul_ty->is_variadic = true;
    s = symtab_define(st, "strtoul", SYM_FUNC, strtoul_ty, loc); s->sc = SC_EXTERN;

    /* vsnprintf(char*, size_t, const char*, va_list) -> int */
    Type *vsnprintf_ty = type_func(a, ty_int);
    vsnprintf_ty->is_variadic = true;
    s = symtab_define(st, "vsnprintf", SYM_FUNC, vsnprintf_ty, loc); s->sc = SC_EXTERN;

    /* vfprintf(FILE*, const char*, va_list) -> int */
    Type *vfprintf_ty = type_func(a, ty_int);
    vfprintf_ty->is_variadic = true;
    s = symtab_define(st, "vfprintf", SYM_FUNC, vfprintf_ty, loc); s->sc = SC_EXTERN;

    /* va_start, va_end, va_copy (built-in, special codegen) */
    Type *va_builtin_ty = type_func(a, ty_void);
    va_builtin_ty->is_variadic = true;
    s = symtab_define(st, "va_start", SYM_FUNC, va_builtin_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "va_end", SYM_FUNC, va_builtin_ty, loc); s->sc = SC_EXTERN;
    s = symtab_define(st, "va_copy", SYM_FUNC, va_builtin_ty, loc); s->sc = SC_EXTERN;

    /* time(time_t*) -> time_t (long) */
    Type *time_ty = type_func(a, ty_long);
    time_ty->is_variadic = true;
    s = symtab_define(st, "time", SYM_FUNC, time_ty, loc); s->sc = SC_EXTERN;

    /* strtoull(const char*, char**, int) -> unsigned long long */
    Type *strtoull_ty = type_func(a, ty_ullong);
    strtoull_ty->is_variadic = true;
    s = symtab_define(st, "strtoull", SYM_FUNC, strtoull_ty, loc); s->sc = SC_EXTERN;

    /* clock() -> clock_t (long) */
    Type *clock_ty = type_func(a, ty_long);
    s = symtab_define(st, "clock", SYM_FUNC, clock_ty, loc); s->sc = SC_EXTERN;
}

int main(int argc, char **argv) {
    const char *input_file = NULL;
    const char *output_file = NULL;
    const char *include_paths[64];
    int include_count = 0;
    bool preprocess_only = false;
    bool dump_ast = false;

    /* Initialize include paths with NULL terminator */
    include_paths[0] = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            include_paths[include_count++] = argv[++i];
            include_paths[include_count] = NULL;
        } else if (strncmp(argv[i], "-I", 2) == 0) {
            include_paths[include_count++] = argv[i] + 2;
            include_paths[include_count] = NULL;
        } else if (strncmp(argv[i], "-D", 2) == 0) {
            char *def = argv[i] + 2;
            if (*def == '\0' && i + 1 < argc) def = argv[++i];
            char *eq = strchr(def, '=');
            if (eq) {
                *eq = '\0';
                preprocess_define(def, eq + 1);
            } else {
                preprocess_define(def, "1");
            }
        } else if (strcmp(argv[i], "-E") == 0) {
            preprocess_only = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            dump_ast = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!input_file) {
        fprintf(stderr, "error: no input file\n");
        usage(argv[0]);
        return 1;
    }

    /* Read source file */
    size_t src_len;
    char *src = read_file(input_file, &src_len);
    if (!src) {
        error_noloc("cannot open file '%s'", input_file);
        return 1;
    }

    /* Initialize arena */
    Arena arena;
    arena_init(&arena, 1024 * 1024); /* 1MB blocks */

    /* Preprocess */
    char *preprocessed = preprocess(src, input_file, include_paths, &arena);
    if (!preprocessed) {
        fprintf(stderr, "preprocessing failed\n");
        return 1;
    }

    if (preprocess_only) {
        FILE *out = output_file ? fopen(output_file, "w") : stdout;
        fputs(preprocessed, out);
        if (output_file) fclose(out);
        free(src);
        arena_free(&arena);
        return error_count > 0 ? 1 : 0;
    }

    /* Initialize type system */
    type_init(&arena);

    /* Initialize symbol table and register builtins */
    SymTab symtab;
    symtab_init(&symtab, &arena);
    register_builtins(&symtab, &arena);

    /* Lex and parse */
    Lexer lexer;
    lexer_init(&lexer, preprocessed, input_file);

    Parser parser;
    parser_init(&parser, &lexer, &arena, &symtab);
    Node *program = parser_parse(&parser);

    if (error_count > 0) {
        fprintf(stderr, "%d error(s) found\n", error_count);
        free(src);
        arena_free(&arena);
        return 1;
    }

    /* Semantic analysis */
    Sema sema;
    sema_init(&sema, &arena, &symtab);
    sema_check(&sema, program);

    if (error_count > 0) {
        fprintf(stderr, "%d error(s) found\n", error_count);
        free(src);
        arena_free(&arena);
        return 1;
    }

    (void)dump_ast; /* TODO: implement AST dump */

    /* Code generation */
    CodeGen codegen;
    codegen_init(&codegen, &arena, &symtab);
    codegen_generate(&codegen, program);
    char *output = codegen_get_output(&codegen);

    /* Write output */
    FILE *out = output_file ? fopen(output_file, "w") : stdout;
    if (!out) {
        fprintf(stderr, "error: cannot open output file '%s'\n", output_file);
        return 1;
    }
    fputs(output, out);
    if (output_file) fclose(out);

    /* Cleanup */
    free(src);
    arena_free(&arena);

    if (error_count > 0) {
        fprintf(stderr, "%d error(s), %d warning(s)\n", error_count, warn_count);
        return 1;
    }
    if (warn_count > 0) {
        fprintf(stderr, "%d warning(s)\n", warn_count);
    }

    return 0;
}
