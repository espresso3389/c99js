#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * C-Simple-JSON-Parser (as a challenge project).
 *
 * This test was previously part of the primitive test suite as `test/test_json.c`,
 * but the parser repository is now tracked under `challenges/`.
 */

#define JSON_SKIP_WHITESPACE
#include "../../C-Simple-JSON-Parser/json.c"

int main(void) {
    /* Test 1: Simple object */
    const char *json1 = "{\"name\": \"Alice\", \"age\": 30, \"active\": true}";
    printf("=== Test 1: Simple Object ===\n");
    result(json_element) r1 = json_parse(json1);
    if (result_is_err(json_element)(&r1)) { printf("FAIL\n"); return 1; }
    typed(json_element) e1 = result_unwrap(json_element)(&r1);
    json_print(&e1, 2);
    printf("\n");
    json_free(&e1);

    /* Test 2: Nested object */
    const char *json2 = "{\"person\": {\"name\": \"Bob\", \"age\": 25}, \"valid\": true}";
    printf("=== Test 2: Nested Object ===\n");
    result(json_element) r2 = json_parse(json2);
    if (result_is_err(json_element)(&r2)) { printf("FAIL\n"); return 1; }
    typed(json_element) e2 = result_unwrap(json_element)(&r2);
    json_print(&e2, 2);
    printf("\n");
    json_free(&e2);

    /* Test 3: Array */
    const char *json3 = "[1, 2, 3, 4, 5]";
    printf("=== Test 3: Array ===\n");
    result(json_element) r3 = json_parse(json3);
    if (result_is_err(json_element)(&r3)) { printf("FAIL\n"); return 1; }
    typed(json_element) e3 = result_unwrap(json_element)(&r3);
    json_print(&e3, 2);
    printf("\n");
    json_free(&e3);

    /* Test 4: String */
    const char *json4 = "\"hello world\"";
    printf("=== Test 4: String ===\n");
    result(json_element) r4 = json_parse(json4);
    if (result_is_err(json_element)(&r4)) { printf("FAIL\n"); return 1; }
    typed(json_element) e4 = result_unwrap(json_element)(&r4);
    json_print(&e4, 2);
    printf("\n");
    json_free(&e4);

    /* Test 5: Boolean and number */
    const char *json5 = "{\"pi\": 3.14, \"flag\": false}";
    printf("=== Test 5: Float and Boolean ===\n");
    result(json_element) r5 = json_parse(json5);
    if (result_is_err(json_element)(&r5)) { printf("FAIL\n"); return 1; }
    typed(json_element) e5 = result_unwrap(json_element)(&r5);
    json_print(&e5, 2);
    printf("\n");
    json_free(&e5);

    printf("All tests passed!\n");
    return 0;
}

