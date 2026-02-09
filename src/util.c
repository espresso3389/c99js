#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

int error_count = 0;
int warn_count = 0;

/* ---- Arena allocator ---- */
static ArenaBlock *arena_new_block(size_t size) {
    ArenaBlock *b = malloc(sizeof(ArenaBlock) + size);
    b->next = NULL;
    b->size = size;
    b->used = 0;
    return b;
}

void arena_init(Arena *a, size_t default_block_size) {
    a->default_block_size = default_block_size;
    a->head = arena_new_block(default_block_size);
    a->current = a->head;
}

void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7) & ~(size_t)7; /* align to 8 bytes */
    if (a->current->used + size > a->current->size) {
        size_t bsz = a->default_block_size;
        if (size > bsz) bsz = size;
        ArenaBlock *nb = arena_new_block(bsz);
        a->current->next = nb;
        a->current = nb;
    }
    void *p = a->current->data + a->current->used;
    a->current->used += size;
    return p;
}

void *arena_calloc(Arena *a, size_t size) {
    void *p = arena_alloc(a, size);
    memset(p, 0, size);
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s);
    char *p = arena_alloc(a, len + 1);
    memcpy(p, s, len + 1);
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = a->current = NULL;
}

/* ---- Dynamic buffer ---- */
void buf_init(Buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(Buf *b, size_t need) {
    if (b->len + need <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 64;
    while (ncap < b->len + need) ncap *= 2;
    b->data = realloc(b->data, ncap);
    b->cap = ncap;
}

void buf_push(Buf *b, char c) {
    buf_grow(b, 1);
    b->data[b->len++] = c;
}

void buf_append(Buf *b, const char *s, size_t len) {
    buf_grow(b, len);
    memcpy(b->data + b->len, s, len);
    b->len += len;
}

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(b, fmt, ap);
    va_end(ap);
}

void buf_vprintf(Buf *b, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return;
    buf_grow(b, (size_t)n + 1);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
}

char *buf_detach(Buf *b) {
    buf_push(b, '\0');
    char *s = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return s;
}

void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---- Error reporting ---- */
void error_at(SrcLoc loc, const char *fmt, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", loc.filename ? loc.filename : "<unknown>", loc.line, loc.col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    error_count++;
}

void warn_at(SrcLoc loc, const char *fmt, ...) {
    fprintf(stderr, "%s:%d:%d: warning: ", loc.filename ? loc.filename : "<unknown>", loc.line, loc.col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    warn_count++;
}

void error_noloc(const char *fmt, ...) {
    fprintf(stderr, "error: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    error_count++;
}

/* ---- File I/O ---- */
char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = rd;
    return buf;
}

/* ---- String interning ---- */
#define INTERN_TABLE_SIZE 4096

typedef struct InternEntry {
    struct InternEntry *next;
    size_t len;
    char str[];
} InternEntry;

static InternEntry *intern_table[INTERN_TABLE_SIZE];

static unsigned int intern_hash(const char *s, size_t len) {
    unsigned int h = 0;
    for (size_t i = 0; i < len; i++) {
        h = h * 31 + (unsigned char)s[i];
    }
    return h;
}

const char *str_intern_range(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    unsigned int idx = intern_hash(start, len) % INTERN_TABLE_SIZE;
    for (InternEntry *e = intern_table[idx]; e; e = e->next) {
        if (e->len == len && memcmp(e->str, start, len) == 0)
            return e->str;
    }
    InternEntry *e = malloc(sizeof(InternEntry) + len + 1);
    e->len = len;
    memcpy(e->str, start, len);
    e->str[len] = '\0';
    e->next = intern_table[idx];
    intern_table[idx] = e;
    return e->str;
}

const char *str_intern(const char *s) {
    return str_intern_range(s, s + strlen(s));
}
