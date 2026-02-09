#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    /* Basic arithmetic */
    int x = 10;
    int y = 20;
    int z = add(x, y);
    printf("add(%d, %d) = %d\n", x, y, z);

    /* Control flow */
    for (int i = 0; i < 5; i++) {
        printf("i = %d\n", i);
    }

    /* Recursion */
    printf("factorial(10) = %d\n", factorial(10));

    /* Pointers */
    int a = 42;
    int *p = &a;
    printf("a = %d, *p = %d\n", a, *p);
    *p = 100;
    printf("after *p = 100: a = %d\n", a);

    /* Arrays */
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += arr[i];
    }
    printf("sum of array = %d\n", sum);

    /* While loop */
    int count = 0;
    while (count < 3) {
        printf("count = %d\n", count);
        count++;
    }

    /* Switch */
    int val = 2;
    switch (val) {
    case 1: printf("one\n"); break;
    case 2: printf("two\n"); break;
    case 3: printf("three\n"); break;
    default: printf("other\n"); break;
    }

    /* Ternary */
    int max = (x > y) ? x : y;
    printf("max(%d, %d) = %d\n", x, y, max);

    return 0;
}
