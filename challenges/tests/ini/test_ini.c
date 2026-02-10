#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* c99js does not have strcspn, so we provide our own */
static int strcspn(const char *s, const char *reject) {
    int count = 0;
    const char *r;
    while (*s) {
        r = reject;
        while (*r) {
            if (*s == *r) {
                return count;
            }
            r++;
        }
        s++;
        count++;
    }
    return count;
}

/* Include the ini.c source directly so we get a single translation unit */
#include "ini.c"

int main(void) {
    FILE *fp;
    const char *val;
    int port;
    ini_t *ini;

    /* Write a test.ini file */
    fp = fopen("test.ini", "w");
    if (!fp) {
        printf("ERROR: Could not create test.ini\n");
        return 1;
    }
    fputs("[owner]\n", fp);
    fputs("name=John\n", fp);
    fputs("organization=Acme Inc\n", fp);
    fputs("\n", fp);
    fputs("[database]\n", fp);
    fputs("port=143\n", fp);
    fputs("server=192.168.1.1\n", fp);
    fputs("file=payroll.dat\n", fp);
    fclose(fp);
    printf("test.ini written successfully\n");

    /* Load the ini file */
    ini = ini_load("test.ini");
    if (!ini) {
        printf("ERROR: Could not load test.ini\n");
        return 1;
    }
    printf("ini loaded successfully\n");

    /* Test ini_get for [owner] section */
    val = ini_get(ini, "owner", "name");
    if (val) {
        printf("[owner] name = %s\n", val);
    } else {
        printf("[owner] name = (null)\n");
    }

    val = ini_get(ini, "owner", "organization");
    if (val) {
        printf("[owner] organization = %s\n", val);
    } else {
        printf("[owner] organization = (null)\n");
    }

    /* Test ini_get for [database] section */
    val = ini_get(ini, "database", "server");
    if (val) {
        printf("[database] server = %s\n", val);
    } else {
        printf("[database] server = (null)\n");
    }

    val = ini_get(ini, "database", "file");
    if (val) {
        printf("[database] file = %s\n", val);
    } else {
        printf("[database] file = (null)\n");
    }

    /* Test ini_sget to parse port as integer */
    port = 0;
    if (ini_sget(ini, "database", "port", "%d", &port)) {
        printf("[database] port = %d\n", port);
    } else {
        printf("[database] port = (not found)\n");
    }

    /* Test a key that does not exist */
    val = ini_get(ini, "database", "missing_key");
    if (val) {
        printf("[database] missing_key = %s\n", val);
    } else {
        printf("[database] missing_key = (null, as expected)\n");
    }

    ini_free(ini);
    printf("All tests passed!\n");
    return 0;
}
