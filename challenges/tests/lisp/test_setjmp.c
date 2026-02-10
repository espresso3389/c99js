#include <stdio.h>
#include <setjmp.h>
#include <string.h>

jmp_buf jb;

void thrower(int n) {
    printf("About to longjmp with %d\n", n);
    longjmp(jb, n);
}

int main() {
    int i;

    /* Test 1: basic setjmp/longjmp */
    i = setjmp(jb);
    if (i) {
        printf("Caught: %d\n", i);
    } else {
        printf("setjmp returned 0, calling thrower\n");
        thrower(42);
    }

    /* Test 2: setjmp in if condition */
    if (setjmp(jb)) {
        printf("Second setjmp caught\n");
    } else {
        printf("Second setjmp returned 0\n");
        thrower(1);
    }

    /* Test 3: nested setjmp with save/restore (like f_catch) */
    jmp_buf saved;
    memcpy(saved, jb, sizeof(jb));
    i = setjmp(jb);
    if (i) {
        printf("Inner catch: %d\n", i);
    } else {
        thrower(99);
    }
    memcpy(jb, saved, sizeof(jb));

    printf("All tests passed!\n");
    return 0;
}
