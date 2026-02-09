#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

typedef int (*binop)(int, int);

void apply(binop fn, int a, int b) {
    printf("result = %d\n", fn(a, b));
}

/* Callback pattern */
void for_each(int *arr, int n, void (*callback)(int)) {
    for (int i = 0; i < n; i++) {
        callback(arr[i]);
    }
}

void print_int(int x) {
    printf("%d ", x);
}

int main(void) {
    /* Function pointers */
    apply(add, 10, 20);
    apply(sub, 10, 20);
    apply(mul, 10, 20);

    /* Array of function pointers */
    binop ops[3] = {add, sub, mul};
    const char *names[3] = {"add", "sub", "mul"};
    for (int i = 0; i < 3; i++) {
        printf("%s(5, 3) = %d\n", names[i], ops[i](5, 3));
    }

    /* Callback */
    int arr[] = {1, 2, 3, 4, 5};
    for_each(arr, 5, print_int);
    printf("\n");

    return 0;
}
