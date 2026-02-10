/* Include the adapted printf implementation directly (c99js takes single file) */
#include "printf_adapted.c"

/* Simple string compare */
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

/* Test helper: returns 1 on pass, 0 on fail */
static int check(const char *test_name, const char *got, const char *expected) {
    if (my_strcmp(got, expected) == 0) {
        printf_ss("PASS: %s => \"%s\"\n", test_name, got);
        return 1;
    } else {
        printf_sss("FAIL: %s => got \"%s\", expected \"%s\"\n", test_name, got, expected);
        return 0;
    }
}

int main(void) {
    char buf[256];
    int pass = 0;
    int total = 0;

    /* Test 1: Simple string */
    snprintf_s(buf, sizeof(buf), "Hello %s!", "world");
    total++; pass += check("%%s basic", buf, "Hello world!");

    /* Test 2: Integer %d */
    snprintf_i(buf, sizeof(buf), "%d", 42);
    total++; pass += check("%%d positive", buf, "42");

    /* Test 3: Negative integer */
    snprintf_i(buf, sizeof(buf), "%d", -123);
    total++; pass += check("%%d negative", buf, "-123");

    /* Test 4: Zero */
    snprintf_i(buf, sizeof(buf), "%d", 0);
    total++; pass += check("%%d zero", buf, "0");

    /* Test 5: Hex lowercase */
    snprintf_i(buf, sizeof(buf), "%x", 255);
    total++; pass += check("%%x lowercase", buf, "ff");

    /* Test 6: Hex uppercase */
    snprintf_i(buf, sizeof(buf), "%X", 255);
    total++; pass += check("%%X uppercase", buf, "FF");

    /* Test 7: Hex with prefix */
    snprintf_i(buf, sizeof(buf), "%#x", 255);
    total++; pass += check("%%#x prefix", buf, "0xff");

    /* Test 8: Octal */
    snprintf_i(buf, sizeof(buf), "%o", 8);
    total++; pass += check("%%o octal", buf, "10");

    /* Test 9: Unsigned */
    snprintf_u(buf, sizeof(buf), "%u", 4294967295U);
    total++; pass += check("%%u max", buf, "4294967295");

    /* Test 10: Char */
    snprintf_i(buf, sizeof(buf), "%c", 'A');
    total++; pass += check("%%c char", buf, "A");

    /* Test 11: Width right-aligned */
    snprintf_i(buf, sizeof(buf), "%10d", 42);
    total++; pass += check("%%10d right-align", buf, "        42");

    /* Test 12: Width left-aligned */
    snprintf_i(buf, sizeof(buf), "%-10d|", 42);
    total++; pass += check("%%-10d left-align", buf, "42        |");

    /* Test 13: Zero-padded */
    snprintf_i(buf, sizeof(buf), "%05d", 42);
    total++; pass += check("%%05d zero-pad", buf, "00042");

    /* Test 14: Plus flag */
    snprintf_i(buf, sizeof(buf), "%+d", 42);
    total++; pass += check("%%+d plus flag", buf, "+42");

    /* Test 15: Space flag */
    snprintf_i(buf, sizeof(buf), "% d", 42);
    total++; pass += check("%% d space flag", buf, " 42");

    /* Test 16: String precision */
    snprintf_s(buf, sizeof(buf), "%.3s", "abcdef");
    total++; pass += check("%%.3s precision", buf, "abc");

    /* Test 17: Float basic */
    snprintf_f(buf, sizeof(buf), "%f", 3.14);
    total++; pass += check("%%f basic", buf, "3.140000");

    /* Test 18: Float precision */
    snprintf_f(buf, sizeof(buf), "%.2f", 3.14159);
    total++; pass += check("%%.2f precision", buf, "3.14");

    /* Test 19: Float negative -- use neg_double() to avoid c99js negation bug */
    snprintf_f(buf, sizeof(buf), "%f", neg_double(2.5));
    total++; pass += check("%%f negative", buf, "-2.500000");

    /* Test 20: Float zero precision */
    snprintf_f(buf, sizeof(buf), "%.0f", 3.7);
    total++; pass += check("%%.0f zero prec", buf, "4");

    /* Test 21: Percent literal */
    snprintf_0(buf, sizeof(buf), "100%%");
    total++; pass += check("%%%% literal", buf, "100%");

    /* Test 22: Multiple args */
    snprintf_iii(buf, sizeof(buf), "%d + %d = %d", 1, 2, 3);
    total++; pass += check("multiple args", buf, "1 + 2 = 3");

    /* Test 23: String width */
    snprintf_s(buf, sizeof(buf), "%10s", "hi");
    total++; pass += check("%%10s width", buf, "        hi");

    /* Test 24: Precision with integer */
    snprintf_i(buf, sizeof(buf), "%.5d", 42);
    total++; pass += check("%%.5d int prec", buf, "00042");

    /* Test 25: snprintf truncation */
    snprintf_0(buf, 5, "Hello world");
    total++; pass += check("snprintf trunc", buf, "Hell");

    /* Test 26: Large integer */
    snprintf_i(buf, sizeof(buf), "%d", 1000000);
    total++; pass += check("%%d large", buf, "1000000");

    /* Test 27: Hex zero-padded width */
    snprintf_i(buf, sizeof(buf), "%08x", 0xABCD);
    total++; pass += check("%%08x zero-pad", buf, "0000abcd");

    /* Summary */
    printf_ii("\n=== Results: %d/%d passed ===\n", pass, total);

    return (pass == total) ? 0 : 1;
}
