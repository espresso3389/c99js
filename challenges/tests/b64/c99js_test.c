/**
 * c99js_test.c - Test driver for b64 library under c99js transpiler
 *
 * Tests base64 encode and decode with known test vectors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the b64 library source files directly */
#include "b64.h"
#include "buffer.c"
#include "encode.c"
#include "decode.c"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void test_encode(const char *input, const char *expected, const char *label) {
    tests_run++;
    char *result = b64_encode((const unsigned char *)input, strlen(input));
    if (result == NULL) {
        printf("FAIL [encode] %s: got NULL\n", label);
        tests_failed++;
        return;
    }
    if (strcmp(result, expected) == 0) {
        printf("PASS [encode] %s\n", label);
        tests_passed++;
    } else {
        printf("FAIL [encode] %s: expected \"%s\", got \"%s\"\n", label, expected, result);
        tests_failed++;
    }
    free(result);
}

static void test_decode(const char *input, const char *expected, const char *label) {
    tests_run++;
    unsigned char *result = b64_decode(input, strlen(input));
    if (result == NULL) {
        printf("FAIL [decode] %s: got NULL\n", label);
        tests_failed++;
        return;
    }
    if (strcmp(expected, (const char *)result) == 0) {
        printf("PASS [decode] %s\n", label);
        tests_passed++;
    } else {
        printf("FAIL [decode] %s: expected \"%s\", got \"%s\"\n", label, expected, (const char *)result);
        tests_failed++;
    }
    free(result);
}

static void test_roundtrip(const char *input, const char *label) {
    tests_run++;
    char *encoded = b64_encode((const unsigned char *)input, strlen(input));
    if (encoded == NULL) {
        printf("FAIL [roundtrip] %s: encode returned NULL\n", label);
        tests_failed++;
        return;
    }
    unsigned char *decoded = b64_decode(encoded, strlen(encoded));
    if (decoded == NULL) {
        printf("FAIL [roundtrip] %s: decode returned NULL\n", label);
        tests_failed++;
        free(encoded);
        return;
    }
    if (strcmp(input, (const char *)decoded) == 0) {
        printf("PASS [roundtrip] %s\n", label);
        tests_passed++;
    } else {
        printf("FAIL [roundtrip] %s: input \"%s\", after roundtrip \"%s\"\n", label, input, (const char *)decoded);
        tests_failed++;
    }
    free(encoded);
    free(decoded);
}

int main(void) {
    printf("=== b64 base64 library test suite (c99js) ===\n");

    /* Encode tests - RFC 4648 test vectors and others */
    test_encode("", "", "empty string");
    test_encode("f", "Zg==", "single char 'f'");
    test_encode("fo", "Zm8=", "two chars 'fo'");
    test_encode("foo", "Zm9v", "three chars 'foo'");
    test_encode("foob", "Zm9vYg==", "four chars 'foob'");
    test_encode("fooba", "Zm9vYmE=", "five chars 'fooba'");
    test_encode("foobar", "Zm9vYmFy", "six chars 'foobar'");
    test_encode("Hello, World!", "SGVsbG8sIFdvcmxkIQ==", "Hello, World!");
    test_encode("bradley", "YnJhZGxleQ==", "bradley");
    test_encode("kinkajou", "a2lua2Fqb3U=", "kinkajou");

    /* Decode tests */
    test_decode("Zm9v", "foo", "decode 'foo'");
    test_decode("Zm9vYmFy", "foobar", "decode 'foobar'");
    test_decode("SGVsbG8sIFdvcmxkIQ==", "Hello, World!", "decode Hello, World!");
    test_decode("Y2FzaWxsZXJv", "casillero", "decode casillero");
    test_decode("aGF4", "hax", "decode hax");
    test_decode("bW9ua2V5cyBhbmQgZG9ncw==", "monkeys and dogs", "decode monkeys and dogs");
    test_decode("YnJhZGxleQ==", "bradley", "decode bradley");

    /* Roundtrip tests */
    test_roundtrip("The quick brown fox jumps over the lazy dog", "pangram roundtrip");
    test_roundtrip("0123456789", "digits roundtrip");
    test_roundtrip("a", "single char roundtrip");
    test_roundtrip("ab", "two char roundtrip");
    test_roundtrip("abc", "three char roundtrip");

    /* Summary */
    printf("\n=== Results: %d/%d passed, %d failed ===\n", tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }

    return tests_failed;
}
