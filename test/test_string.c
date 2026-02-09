#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    /* String operations */
    char s1[64] = "Hello";
    char s2[64] = "World";

    printf("strlen(\"%s\") = %d\n", s1, (int)strlen(s1));
    printf("strcmp(\"%s\", \"%s\") = %d\n", s1, s2, strcmp(s1, s2));

    strcat(s1, " ");
    strcat(s1, s2);
    printf("After strcat: \"%s\"\n", s1);

    /* Dynamic string */
    char *dyn = malloc(128);
    strcpy(dyn, "Dynamic");
    strcat(dyn, " string");
    printf("Dynamic: \"%s\", len=%d\n", dyn, (int)strlen(dyn));
    free(dyn);

    /* Character operations */
    char buf[10];
    strncpy(buf, "abcdefgh", 5);
    buf[5] = '\0';
    printf("strncpy: \"%s\"\n", buf);

    /* memset / memcpy */
    char data[16];
    memset(data, 'A', 8);
    data[8] = '\0';
    printf("memset: \"%s\"\n", data);

    char copy[16];
    memcpy(copy, data, 9);
    printf("memcpy: \"%s\"\n", copy);

    /* strchr / strstr */
    const char *haystack = "Hello, World!";
    printf("strchr 'W' = %d\n", strchr(haystack, 'W') != 0);
    printf("strstr \"World\" = %d\n", strstr(haystack, "World") != 0);

    return 0;
}
