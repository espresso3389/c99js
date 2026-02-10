/*
 * c99js test for tiny-bignum-c
 * Computes factorial(100) and verifies the hex result.
 *
 * Uses WORD_SIZE=1 to avoid uint64_t (DTYPE_TMP is uint32_t).
 */

#include <stdio.h>
#include <string.h>

/* Force WORD_SIZE=1 so DTYPE=uint8_t, DTYPE_TMP=uint32_t (no 64-bit needed) */
#define WORD_SIZE 1

/* Include implementation directly (single compilation unit) */
#include "bn.c"


void factorial(struct bn* n, struct bn* res)
{
  struct bn tmp;

  /* Copy n -> tmp */
  bignum_assign(&tmp, n);

  /* Decrement n by one */
  bignum_dec(n);

  /* Begin summing products: */
  while (!bignum_is_zero(n))
  {
    /* res = tmp * n */
    bignum_mul(&tmp, n, res);

    /* n -= 1 */
    bignum_dec(n);

    /* tmp = res */
    bignum_assign(&tmp, res);
  }

  /* res = tmp */
  bignum_assign(res, &tmp);
}


int main(void)
{
  struct bn num;
  struct bn result;
  char buf[8192];

  char expected[] = "1b30964ec395dc24069528d54bbda40d16e966ef9a70eb21b5b2943a321cdf10391745570cca9420c6ecb3b72ed2ee8b02ea2735c61a000000000000000000000000";

  bignum_from_int(&num, 100);
  factorial(&num, &result);
  bignum_to_string(&result, buf, sizeof(buf));

  printf("factorial(100) = %s\n", buf);
  printf("expected       = %s\n", expected);

  if (strcmp(buf, expected) == 0)
  {
    printf("PASS: factorial(100) matches expected value!\n");
    return 0;
  }
  else
  {
    printf("FAIL: result does not match expected value.\n");
    return 1;
  }
}
