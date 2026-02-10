#include <stdint.h>

#define I uint32_t
#define L double
#define T(x) (*(uint64_t*)&x >> 48)

enum { PRIM = 0x7ff9, ATOM = 0x7ffa, STRG = 0x7ffb, CONS = 0x7ffc, CLOS = 0x7ffe, MACR = 0x7fff, NIL = 0xffff };

L box(I t, I i) { L x; *(uint64_t*)&x = (uint64_t)t << 48 | i; return x; }
I ord(L x)      { return *(uint64_t*)&x; }
L num(L n)      { return n; }
I equ(L x, L y) { return *(uint64_t*)&x == *(uint64_t*)&y; }

L car(L p);
L cdr(L p);

L f_string(L t, L *e) {
  L s;
  I i = 0;
  char buf[40];
  for (s = t; T(s) != NIL; s = cdr(s)) {
    L x = car(s);
    if ((T(x) & ~(ATOM^STRG)) == ATOM)
      i += 1;
    else if (T(x) == CONS)
      for (; T(x) == CONS; x = cdr(x))
        ++i;
    else if (x == x)
      i += 1;
  }
  return box(0, i);
}

int main() {
  return 0;
}
