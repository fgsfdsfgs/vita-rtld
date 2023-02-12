#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "lib.h"
#include "main.h"

static char bsstest[1024];

float bruh(float x) {
  const float y = sinf(x);
  fprintf(stderr, "lib: sinf(%f) = %f\n", x, y);
  return y;
}

void arse(const char *msg) {
  fprintf(stderr, "lib: arse msg `%s`\n", msg);
  fprintf(stderr, "lib: test is %d\n", test);
  fprintf(stderr, "lib: calling fuck(1337)\n");
  fuck(1337);
  snprintf(bsstest, sizeof(bsstest), "bsstest! %s", msg);
  fprintf(stderr, "lib: %s\n", bsstest);
}
