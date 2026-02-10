/*
 * c99js test driver for MD5 implementation.
 * Computes MD5 of known strings and compares against expected hashes.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Include the implementation directly since c99js compiles a single TU */
#include "md5.c"

void print_hash(uint8_t *p) {
    for (unsigned int i = 0; i < 16; ++i) {
        printf("%02x", p[i]);
    }
}

int check_md5(char *input, char *expected) {
    uint8_t result[16];
    char hex[33];

    md5String(input, result);

    /* Build hex string manually */
    for (unsigned int i = 0; i < 16; ++i) {
        unsigned int byte_val = result[i];
        unsigned int hi = byte_val / 16;
        unsigned int lo = byte_val % 16;
        if (hi < 10)
            hex[i * 2] = '0' + hi;
        else
            hex[i * 2] = 'a' + (hi - 10);
        if (lo < 10)
            hex[i * 2 + 1] = '0' + lo;
        else
            hex[i * 2 + 1] = 'a' + (lo - 10);
    }
    hex[32] = 0;

    printf("Input:    \"%s\"\n", input);
    printf("Expected: %s\n", expected);
    printf("Got:      %s\n", hex);

    int pass = 1;
    for (int i = 0; i < 32; ++i) {
        if (hex[i] != expected[i]) {
            pass = 0;
            break;
        }
    }

    if (pass) {
        printf("Result:   PASS\n\n");
    } else {
        printf("Result:   FAIL\n\n");
    }

    return pass;
}

int main(void) {
    int all_pass = 1;

    printf("=== MD5 Test Vectors ===\n\n");

    /* Test vector 1: empty string */
    if (!check_md5("", "d41d8cd98f00b204e9800998ecf8427e"))
        all_pass = 0;

    /* Test vector 2: "abc" */
    if (!check_md5("abc", "900150983cd24fb0d6963f7d28e17f72"))
        all_pass = 0;

    /* Test vector 3: "Hello, World!" */
    if (!check_md5("Hello, World!", "65a8e27d8879283831b664bd8b7f0ad4"))
        all_pass = 0;

    if (all_pass) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== SOME TESTS FAILED ===\n");
    }

    return all_pass ? 0 : 1;
}
