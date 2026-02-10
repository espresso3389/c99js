/* Test file for tinyexpr with c99js compiler */
#include <stdio.h>
#include <math.h>

/* Include tinyexpr implementation directly */
#include "tinyexpr_patched.c"

int main() {
    int error;
    double result;

    printf("=== TinyExpr c99js Test ===\n");

    /* Test 1: 2+3 = 5 */
    result = te_interp("2+3", &error);
    printf("Test 1: te_interp(\"2+3\") = %g (expected 5, error=%d)\n", result, error);

    /* Test 2: sqrt(9) = 3 */
    result = te_interp("sqrt(9)", &error);
    printf("Test 2: te_interp(\"sqrt(9)\") = %g (expected 3, error=%d)\n", result, error);

    /* Test 3: sin(0) = 0 */
    result = te_interp("sin(0)", &error);
    printf("Test 3: te_interp(\"sin(0)\") = %g (expected 0, error=%d)\n", result, error);

    /* Test 4: 2^10 = 1024 */
    result = te_interp("2^10", &error);
    printf("Test 4: te_interp(\"2^10\") = %g (expected 1024, error=%d)\n", result, error);

    /* Bonus tests */
    result = te_interp("3*4+2", &error);
    printf("Test 5: te_interp(\"3*4+2\") = %g (expected 14, error=%d)\n", result, error);

    result = te_interp("pi()", &error);
    printf("Test 6: te_interp(\"pi()\") = %g (expected 3.14159, error=%d)\n", result, error);

    result = te_interp("fac(5)", &error);
    printf("Test 7: te_interp(\"fac(5)\") = %g (expected 120, error=%d)\n", result, error);

    result = te_interp("pow(2,8)", &error);
    printf("Test 8: te_interp(\"pow(2,8)\") = %g (expected 256, error=%d)\n", result, error);

    printf("=== All tests complete ===\n");
    return 0;
}
