#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include patched fastlz implementation (LL suffix removed for c99js BigInt compat) */
#include "fastlz_patched.c"

/*
 * Test driver for FastLZ compression library via c99js transpiler.
 * Tests: compress with level 1, decompress, verify round-trip.
 *        compress with level 2, decompress, verify round-trip.
 *        compress auto-select, decompress, verify round-trip.
 */

static int test_roundtrip(const char *name, const unsigned char *data, int data_len, int level) {
    int comp_buf_size = (int)(data_len * 1.1) + 256;
    unsigned char *compressed = (unsigned char *)malloc(comp_buf_size);
    unsigned char *decompressed = (unsigned char *)malloc(data_len + 256);
    int comp_len, decomp_len;
    int pass = 1;
    int i;

    if (!compressed || !decompressed) {
        printf("  [FAIL] %s: malloc failed\n", name);
        free(compressed);
        free(decompressed);
        return 0;
    }

    /* Clear buffers */
    memset(compressed, 0, comp_buf_size);
    memset(decompressed, 0, data_len + 256);

    /* Compress */
    if (level == 0) {
        comp_len = fastlz_compress(data, data_len, compressed);
    } else {
        comp_len = fastlz_compress_level(level, data, data_len, compressed);
    }

    if (comp_len <= 0) {
        printf("  [FAIL] %s: compression returned %d\n", name, comp_len);
        free(compressed);
        free(decompressed);
        return 0;
    }

    printf("  %s: original=%d compressed=%d ratio=%.1f%%\n",
           name, data_len, comp_len, (double)comp_len * 100.0 / (double)data_len);

    /* Decompress */
    decomp_len = fastlz_decompress(compressed, comp_len, decompressed, data_len + 256);

    if (decomp_len != data_len) {
        printf("  [FAIL] %s: decompress length mismatch: expected %d got %d\n",
               name, data_len, decomp_len);
        pass = 0;
    } else {
        /* Verify data matches */
        for (i = 0; i < data_len; i++) {
            if (decompressed[i] != data[i]) {
                printf("  [FAIL] %s: data mismatch at byte %d: expected 0x%02x got 0x%02x\n",
                       name, i, data[i], decompressed[i]);
                pass = 0;
                break;
            }
        }
    }

    if (pass) {
        printf("  [PASS] %s: round-trip OK\n", name);
    }

    free(compressed);
    free(decompressed);
    return pass;
}

int main(void) {
    int total_pass = 0;
    int total_tests = 0;
    int i;

    printf("=== FastLZ c99js Round-Trip Test ===\n\n");

    /* Test 1: Repetitive text data (high compression) */
    {
        unsigned char buf[512];
        for (i = 0; i < 512; i++) {
            buf[i] = (unsigned char)('A' + (i % 4));
        }
        total_tests++;
        total_pass += test_roundtrip("Repetitive text L1", buf, 512, 1);
        total_tests++;
        total_pass += test_roundtrip("Repetitive text L2", buf, 512, 2);
    }

    /* Test 2: Lorem ipsum like text */
    {
        const char *text =
            "The quick brown fox jumps over the lazy dog. "
            "The quick brown fox jumps over the lazy dog. "
            "Pack my box with five dozen liquor jugs. "
            "Pack my box with five dozen liquor jugs. "
            "How vexingly quick daft zebras jump! "
            "How vexingly quick daft zebras jump! "
            "The five boxing wizards jump quickly. "
            "The five boxing wizards jump quickly. "
            "Sphinx of black quartz, judge my vow. "
            "Sphinx of black quartz, judge my vow. ";
        int text_len = (int)strlen(text);
        total_tests++;
        total_pass += test_roundtrip("English text L1", (const unsigned char *)text, text_len, 1);
        total_tests++;
        total_pass += test_roundtrip("English text L2", (const unsigned char *)text, text_len, 2);
    }

    /* Test 3: Sequential byte pattern */
    {
        unsigned char buf[256];
        for (i = 0; i < 256; i++) {
            buf[i] = (unsigned char)i;
        }
        total_tests++;
        total_pass += test_roundtrip("Sequential bytes L1", buf, 256, 1);
    }

    /* Test 4: All zeros */
    {
        unsigned char buf[300];
        memset(buf, 0, 300);
        total_tests++;
        total_pass += test_roundtrip("All zeros L1", buf, 300, 1);
    }

    /* Test 5: Mixed pattern with repetition */
    {
        unsigned char buf[1024];
        for (i = 0; i < 1024; i++) {
            buf[i] = (unsigned char)((i * 7 + 13) % 256);
        }
        /* Inject some repetitive regions */
        memcpy(buf + 200, buf, 100);
        memcpy(buf + 500, buf + 100, 100);
        total_tests++;
        total_pass += test_roundtrip("Mixed pattern L1", buf, 1024, 1);
        total_tests++;
        total_pass += test_roundtrip("Mixed pattern L2", buf, 1024, 2);
    }

    /* Test 6: Auto-select compression level */
    {
        unsigned char buf[400];
        for (i = 0; i < 400; i++) {
            buf[i] = (unsigned char)(i % 26 + 'a');
        }
        total_tests++;
        total_pass += test_roundtrip("Auto-level", buf, 400, 0);
    }

    /* Test 7: Minimum size input (16 bytes, the documented minimum) */
    {
        unsigned char buf[16];
        for (i = 0; i < 16; i++) {
            buf[i] = (unsigned char)(i * 3);
        }
        total_tests++;
        total_pass += test_roundtrip("Min size (16) L1", buf, 16, 1);
    }

    printf("\n=== Results: %d/%d tests passed ===\n", total_pass, total_tests);

    if (total_pass == total_tests) {
        printf("OVERALL: PASS\n");
        return 0;
    } else {
        printf("OVERALL: FAIL\n");
        return 1;
    }
}
