int printf(const char *fmt, ...);

typedef struct { const char *name; int val; } Entry;

static const Entry table[] = {
    {"hello", 10},
    {"world", 20},
    {0, 0}
};

int is_known(const char *name) {
    static const char *names[] = {"foo", "bar", 0};
    int i;
    for (i = 0; names[i]; i++) {
        /* just iterate to count */
    }
    return i;
}

int main(void) {
    printf("%s %d\n", table[0].name, table[0].val);
    printf("%s %d\n", table[1].name, table[1].val);
    printf("known count: %d\n", is_known("x"));
    return 0;
}
