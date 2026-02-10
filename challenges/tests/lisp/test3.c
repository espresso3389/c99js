#include <stdint.h>

#define T(x) (*(uint64_t*)&x >> 48)

int main() {
  double x = 3.14;
  unsigned int tag = T(x);
  return tag;
}
