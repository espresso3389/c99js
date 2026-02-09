int printf(const char *fmt, ...);

struct Foo {
    int kind;
    int *type;
    union {
        int ival;
        struct { const char *name; int len; };
        struct { int *body; };
    };
};

int main(void) {
    struct Foo f;
    f.kind = 1;
    f.ival = 42;
    printf("kind=%d ival=%d\n", f.kind, f.ival);
    printf("sizeof=%d\n", (int)sizeof(struct Foo));
    return 0;
}
