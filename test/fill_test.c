#undef NDEBUG
#include "impl.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main()
{
  unsigned char src[3];
  src[0] = 1;
  src[1] = 2;
  src[2] = 3;

  unsigned char dst[10];

  memset(dst, 0, sizeof(dst));
  fill(dst, 0, src, sizeof(src));
  assert(dst[0] == 0);
  assert(dst[1] == 0);
  assert(dst[2] == 0);
  assert(dst[3] == 0);
  assert(dst[4] == 0);
  assert(dst[5] == 0);
  assert(dst[6] == 0);
  assert(dst[7] == 0);
  assert(dst[8] == 0);
  assert(dst[9] == 0);

  memset(dst, 0, sizeof(dst));
  fill(dst, 1, src, sizeof(src));
  assert(dst[0] == 1);
  assert(dst[1] == 0);
  assert(dst[2] == 0);
  assert(dst[3] == 0);
  assert(dst[4] == 0);
  assert(dst[5] == 0);
  assert(dst[6] == 0);
  assert(dst[7] == 0);
  assert(dst[8] == 0);
  assert(dst[9] == 0);

  memset(dst, 0, sizeof(dst));
  fill(dst, 3, src, sizeof(src));
  assert(dst[0] == 1);
  assert(dst[1] == 2);
  assert(dst[2] == 3);
  assert(dst[3] == 0);
  assert(dst[4] == 0);
  assert(dst[5] == 0);
  assert(dst[6] == 0);
  assert(dst[7] == 0);
  assert(dst[8] == 0);
  assert(dst[9] == 0);

  memset(dst, 0, sizeof(dst));
  fill(dst, 4, src, sizeof(src));
  assert(dst[0] == 1);
  assert(dst[1] == 2);
  assert(dst[2] == 3);
  assert(dst[3] == 1);
  assert(dst[4] == 0);
  assert(dst[5] == 0);
  assert(dst[6] == 0);
  assert(dst[7] == 0);
  assert(dst[8] == 0);
  assert(dst[9] == 0);

  memset(dst, 0, sizeof(dst));
  fill(dst, sizeof(dst), src, sizeof(src));
  assert(dst[0] == 1);
  assert(dst[1] == 2);
  assert(dst[2] == 3);
  assert(dst[3] == 1);
  assert(dst[4] == 2);
  assert(dst[5] == 3);
  assert(dst[6] == 1);
  assert(dst[7] == 2);
  assert(dst[8] == 3);
  assert(dst[9] == 1);

  puts("fill() test passed.");
  return 0;
}
