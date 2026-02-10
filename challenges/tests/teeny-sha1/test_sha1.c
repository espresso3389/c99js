/*
 * Single-file test for teeny-sha1 under c99js.
 * Includes sha1digest() directly and tests known SHA-1 vectors.
 *
 * Workaround: c99js compiles C >> as JS >> (signed right shift).
 * For uint32_t rotation, we need unsigned right shift behavior.
 * We mask the right-shift result to strip sign-extended bits.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
sha1digest(uint8_t *digest, char *hexdigest, const uint8_t *data, size_t databytes)
{
  /*
   * Workaround for c99js: >> compiles to JS signed >>.
   * We mask the right-shifted part to only keep the valid low bits.
   * For rotate-left by N, the right-shift is by (32-N), producing N bits.
   * Mask those N bits with ((1u << N) - 1u).
   */
#define SHA1ROTATELEFT(value, bits) \
  (((value) << (bits)) | (((value) >> (32 - (bits))) & ((1u << (bits)) - 1u)))

  uint32_t W[80];
  uint32_t H[5];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t k;

  uint32_t idx;
  uint32_t lidx;
  uint32_t widx;
  uint32_t didx;

  int32_t wcount;
  uint32_t temp;
  uint64_t databits;
  uint32_t loopcount;
  uint32_t tailbytes;
  uint8_t datatail[128];
  uint32_t databits_hi;
  uint32_t databits_lo;

  H[0] = 0x67452301;
  H[1] = 0xEFCDAB89;
  H[2] = 0x98BADCFE;
  H[3] = 0x10325476;
  H[4] = 0xC3D2E1F0;

  f = 0;
  k = 0;
  didx = 0;

  /*
   * Workaround for c99js uint64_t handling:
   * c99js stores uint64_t as Float64. Shifting a float64 with >> in JS
   * first converts to Int32, losing the upper 32 bits.
   * Instead, compute the high and low 32-bit words of databits manually.
   */
  databits_lo = (uint32_t)(databytes << 3);
  databits_hi = (uint32_t)(databytes >> 29);

  loopcount = (databytes + 8) / 64 + 1;
  tailbytes = 64 * loopcount - databytes;

  memset(datatail, 0, 128);

  if (!digest && !hexdigest)
    return -1;

  if (!data)
    return -1;

  datatail[0] = 0x80;
  /* Big-endian 64-bit length in bits at end of tail */
  datatail[tailbytes - 8] = (uint8_t)((databits_hi >> 24) & 0xFF);
  datatail[tailbytes - 7] = (uint8_t)((databits_hi >> 16) & 0xFF);
  datatail[tailbytes - 6] = (uint8_t)((databits_hi >> 8) & 0xFF);
  datatail[tailbytes - 5] = (uint8_t)((databits_hi) & 0xFF);
  datatail[tailbytes - 4] = (uint8_t)((databits_lo >> 24) & 0xFF);
  datatail[tailbytes - 3] = (uint8_t)((databits_lo >> 16) & 0xFF);
  datatail[tailbytes - 2] = (uint8_t)((databits_lo >> 8) & 0xFF);
  datatail[tailbytes - 1] = (uint8_t)((databits_lo) & 0xFF);

  for (lidx = 0; lidx < loopcount; lidx++)
  {
    memset(W, 0, 80 * sizeof(uint32_t));

    for (widx = 0; widx <= 15; widx++)
    {
      wcount = 24;

      while (didx < databytes && wcount >= 0)
      {
        W[widx] += (((uint32_t)data[didx]) << wcount);
        didx++;
        wcount -= 8;
      }
      while (wcount >= 0)
      {
        W[widx] += (((uint32_t)datatail[didx - databytes]) << wcount);
        didx++;
        wcount -= 8;
      }
    }

    for (widx = 16; widx <= 31; widx++)
    {
      W[widx] = SHA1ROTATELEFT((W[widx - 3] ^ W[widx - 8] ^ W[widx - 14] ^ W[widx - 16]), 1);
    }
    for (widx = 32; widx <= 79; widx++)
    {
      W[widx] = SHA1ROTATELEFT((W[widx - 6] ^ W[widx - 16] ^ W[widx - 28] ^ W[widx - 32]), 2);
    }

    a = H[0];
    b = H[1];
    c = H[2];
    d = H[3];
    e = H[4];

    for (idx = 0; idx <= 79; idx++)
    {
      if (idx <= 19)
      {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      }
      else if (idx >= 20 && idx <= 39)
      {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      }
      else if (idx >= 40 && idx <= 59)
      {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      }
      else if (idx >= 60 && idx <= 79)
      {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      temp = SHA1ROTATELEFT(a, 5) + f + e + k + W[idx];
      e = d;
      d = c;
      c = SHA1ROTATELEFT(b, 30);
      b = a;
      a = temp;
    }

    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
  }

  if (digest)
  {
    for (idx = 0; idx < 5; idx++)
    {
      digest[idx * 4 + 0] = (uint8_t)(H[idx] >> 24);
      digest[idx * 4 + 1] = (uint8_t)(H[idx] >> 16);
      digest[idx * 4 + 2] = (uint8_t)(H[idx] >> 8);
      digest[idx * 4 + 3] = (uint8_t)(H[idx]);
    }
  }

  if (hexdigest)
  {
    snprintf(hexdigest, 41, "%08x%08x%08x%08x%08x",
             H[0], H[1], H[2], H[3], H[4]);
  }

  return 0;
}

int main(void)
{
  char hexdigest[41];
  uint8_t digest[20];
  int failures;
  const char *data;
  const char *expected;

  failures = 0;

  /* Test 1: SHA-1 of empty string "" */
  data = "";
  expected = "da39a3ee5e6b4b0d3255bfef95601890afd80709";

  sha1digest(digest, hexdigest, (const uint8_t *)data, 0);

  printf("Test 1: SHA-1 of empty string\n");
  printf("  Expected: %s\n", expected);
  printf("  Got:      %s\n", hexdigest);
  if (strcmp(hexdigest, expected) == 0) {
    printf("  PASS\n");
  } else {
    printf("  FAIL\n");
    failures++;
  }

  /* Test 2: SHA-1 of "abc" */
  data = "abc";
  expected = "a9993e364706816aba3e25717850c26c9cd0d89d";

  sha1digest(digest, hexdigest, (const uint8_t *)data, 3);

  printf("Test 2: SHA-1 of \"abc\"\n");
  printf("  Expected: %s\n", expected);
  printf("  Got:      %s\n", hexdigest);
  if (strcmp(hexdigest, expected) == 0) {
    printf("  PASS\n");
  } else {
    printf("  FAIL\n");
    failures++;
  }

  /* Test 3: SHA-1 of "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" */
  data = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  expected = "84983e441c3bd26ebaae4aa1f95129e5e54670f1";

  sha1digest(digest, hexdigest, (const uint8_t *)data, 56);

  printf("Test 3: SHA-1 of long test vector\n");
  printf("  Expected: %s\n", expected);
  printf("  Got:      %s\n", hexdigest);
  if (strcmp(hexdigest, expected) == 0) {
    printf("  PASS\n");
  } else {
    printf("  FAIL\n");
    failures++;
  }

  /* Test 4: SHA-1 of "The quick brown fox jumps over the lazy dog" */
  data = "The quick brown fox jumps over the lazy dog";
  expected = "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12";

  sha1digest(digest, hexdigest, (const uint8_t *)data, 43);

  printf("Test 4: SHA-1 of \"The quick brown fox jumps over the lazy dog\"\n");
  printf("  Expected: %s\n", expected);
  printf("  Got:      %s\n", hexdigest);
  if (strcmp(hexdigest, expected) == 0) {
    printf("  PASS\n");
  } else {
    printf("  FAIL\n");
    failures++;
  }

  printf("\nTotal failures: %d\n", failures);
  if (failures == 0) {
    printf("All tests passed!\n");
  }

  return failures;
}
