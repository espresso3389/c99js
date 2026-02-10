#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

#define BREAK_ON  (void)0
#define BREAK_OFF (void)0

void using_history() { }

#define FLOAT "%.17lg"

#define ALWAYS_GC 0

#define I uint32_t
#define L double

#define T(x) (*(uint64_t*)&x >> 48)

enum { PRIM = 0x7ff9, ATOM = 0x7ffa, STRG = 0x7ffb, CONS = 0x7ffc, CLOS = 0x7ffe, MACR = 0x7fff, NIL = 0xffff };

L box(I t, I i) { L x; *(uint64_t*)&x = (uint64_t)t << 48 | i; return x; }
I ord(L x)      { return *(uint64_t*)&x; }
L num(L n)      { return n; }
I equ(L x, L y) { return *(uint64_t*)&x == *(uint64_t*)&y; }

int main() {
  L x = box(ATOM, 42);
  printf("tag = %u\n", T(x));
  return 0;
}
