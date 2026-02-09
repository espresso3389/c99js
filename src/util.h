#ifndef C99JS_UTIL_H
#define C99JS_UTIL_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

/* ---- Source location ---- */
typedef struct {
    const char *filename;
    int line;
    int col;
} SrcLoc;

/* ---- Arena allocator ---- */
typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t size;
    size_t used;
    char data[];
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
    ArenaBlock *current;
    size_t default_block_size;
} Arena;

void  arena_init(Arena *a, size_t default_block_size);
void *arena_alloc(Arena *a, size_t size);
void *arena_calloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
void  arena_free(Arena *a);

/* ---- Dynamic buffer (for building strings) ---- */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buf;

void buf_init(Buf *b);
void buf_push(Buf *b, char c);
void buf_append(Buf *b, const char *s, size_t len);
void buf_printf(Buf *b, const char *fmt, ...);
void buf_vprintf(Buf *b, const char *fmt, va_list ap);
char *buf_detach(Buf *b);
void buf_free(Buf *b);

/* ---- Dynamic array (type-generic via macros) ---- */
#define Vec(T) struct { T *data; int len; int cap; }

#define vec_init(v) do { (v).data = NULL; (v).len = 0; (v).cap = 0; } while(0)

#define vec_push(v, item) do { \
    if ((v).len >= (v).cap) { \
        (v).cap = (v).cap ? (v).cap * 2 : 8; \
        (v).data = realloc((v).data, sizeof(*(v).data) * (v).cap); \
    } \
    (v).data[(v).len++] = (item); \
} while(0)

#define vec_free(v) do { free((v).data); (v).data = NULL; (v).len = (v).cap = 0; } while(0)

/* ---- Error reporting ---- */
void error_at(SrcLoc loc, const char *fmt, ...);
void warn_at(SrcLoc loc, const char *fmt, ...);
void error_noloc(const char *fmt, ...);

extern int error_count;
extern int warn_count;

/* ---- File I/O ---- */
char *read_file(const char *path, size_t *out_len);

/* ---- String interning ---- */
const char *str_intern(const char *s);
const char *str_intern_range(const char *start, const char *end);

#endif /* C99JS_UTIL_H */
