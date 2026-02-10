/*
 * c99js test for benhoyt/ht hash table.
 *
 * We #include ht.c directly to get a single compilation unit.
 * Before that, we provide a strdup shim (not in C99 standard)
 * and fix the UL suffix constants to ULL for proper 64-bit values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provide strdup since it is not part of C99 */
char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* d = (char*)malloc(len);
    if (d != NULL) {
        memcpy(d, s, len);
    }
    return d;
}

/* Include the implementation directly */
#include "ht.c"

/* ---- Test helpers ---- */

static int tests_run    = 0;
static int tests_passed = 0;

static void check(int cond, const char* msg) {
    tests_run++;
    if (cond) {
        tests_passed++;
    } else {
        printf("  FAIL: %s\n", msg);
    }
}

/* ---- Tests ---- */

static void test_create_destroy(void) {
    printf("test_create_destroy\n");
    ht* t = ht_create();
    check(t != NULL, "ht_create returns non-NULL");
    check(ht_length(t) == 0, "new table has length 0");
    ht_destroy(t);
}

static void test_set_get(void) {
    printf("test_set_get\n");
    ht* t = ht_create();

    /* Use static ints as values (ht stores void*, value must not be NULL) */
    int v1 = 10, v2 = 20, v3 = 30;

    check(ht_set(t, "alpha", &v1) != NULL, "set alpha");
    check(ht_set(t, "beta",  &v2) != NULL, "set beta");
    check(ht_set(t, "gamma", &v3) != NULL, "set gamma");

    check(ht_length(t) == 3, "length is 3 after 3 inserts");

    check(ht_get(t, "alpha") == &v1, "get alpha == &v1");
    check(ht_get(t, "beta")  == &v2, "get beta  == &v2");
    check(ht_get(t, "gamma") == &v3, "get gamma == &v3");

    /* Verify actual values */
    check(*(int*)ht_get(t, "alpha") == 10, "alpha value == 10");
    check(*(int*)ht_get(t, "beta")  == 20, "beta value  == 20");
    check(*(int*)ht_get(t, "gamma") == 30, "gamma value == 30");

    ht_destroy(t);
}

static void test_overwrite(void) {
    printf("test_overwrite\n");
    ht* t = ht_create();

    int v1 = 1, v2 = 2;
    ht_set(t, "key", &v1);
    check(*(int*)ht_get(t, "key") == 1, "initial value is 1");

    ht_set(t, "key", &v2);
    check(*(int*)ht_get(t, "key") == 2, "overwritten value is 2");
    check(ht_length(t) == 1, "length still 1 after overwrite");

    ht_destroy(t);
}

static void test_missing_key(void) {
    printf("test_missing_key\n");
    ht* t = ht_create();

    check(ht_get(t, "nonexistent") == NULL, "missing key returns NULL (empty table)");

    int v = 42;
    ht_set(t, "exists", &v);
    check(ht_get(t, "nope") == NULL, "missing key returns NULL (non-empty table)");

    ht_destroy(t);
}

static void test_iteration(void) {
    printf("test_iteration\n");
    ht* t = ht_create();

    int vals[5];
    vals[0] = 100; vals[1] = 200; vals[2] = 300; vals[3] = 400; vals[4] = 500;
    ht_set(t, "one",   &vals[0]);
    ht_set(t, "two",   &vals[1]);
    ht_set(t, "three", &vals[2]);
    ht_set(t, "four",  &vals[3]);
    ht_set(t, "five",  &vals[4]);

    /* Iterate and sum up all values */
    hti it = ht_iterator(t);
    int sum = 0;
    int count = 0;
    while (ht_next(&it)) {
        sum += *(int*)it.value;
        count++;
    }
    check(count == 5, "iterator visits 5 items");
    check(sum == 1500, "sum of iterated values == 1500");

    ht_destroy(t);
}

static void test_growth(void) {
    printf("test_growth\n");
    ht* t = ht_create();

    /* Insert enough items to force table expansion (initial capacity 16,
       expansion at length >= capacity/2 = 8). Insert 50 items. */
    int values[50];
    char keybuf[32];
    int i;
    for (i = 0; i < 50; i++) {
        values[i] = i * 7;
        sprintf(keybuf, "item_%d", i);
        check(ht_set(t, keybuf, &values[i]) != NULL, "set during growth");
    }

    check(ht_length(t) == 50, "length is 50 after 50 inserts");

    /* Verify all items are retrievable */
    int all_ok = 1;
    for (i = 0; i < 50; i++) {
        sprintf(keybuf, "item_%d", i);
        int* got = (int*)ht_get(t, keybuf);
        if (got == NULL || *got != i * 7) {
            all_ok = 0;
            printf("  FAIL: growth retrieval failed for %s\n", keybuf);
        }
    }
    check(all_ok, "all 50 items retrievable after growth");

    ht_destroy(t);
}

int main(void) {
    printf("=== benhoyt/ht hash table test via c99js ===\n\n");

    test_create_destroy();
    test_set_get();
    test_overwrite();
    test_missing_key();
    test_iteration();
    test_growth();

    printf("\n=== Results: %d / %d passed ===\n", tests_passed, tests_run);
    if (tests_passed == tests_run) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }
    return (tests_passed == tests_run) ? 0 : 1;
}
