#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#define VRTLD_LIBDL_COMPAT
#include <vrtld.h>

#include "lib.h"
#include "main.h"

int test = 5643;

static void *lib = NULL;

__attribute__((noreturn)) void die(void) {
  vrtld_quit();
  exit(1);
}

int fuck(int x) {
  fprintf(stderr, "app: fuck called with %d\n", x);
  return x;
}

int main(int argc, const char **argv) {
  if (vrtld_init(0) < 0) {
    fprintf(stderr, "app: vrtld_init() failed: %s\n", dlerror());
    return 1;
  }

  fuck(69);

  lib = dlopen("app0:/libtestlib.so", RTLD_GLOBAL);
  if (!lib) {
    fprintf(stderr, "app: dlopen() failed: %s\n", dlerror());
    die();
  }

  float (*bruh_fn)(float) = dlsym(lib, "bruh");
  if (!bruh_fn) {
    fprintf(stderr, "app: dlsym(bruh) failed: %s\n", dlerror());
    die();
  }

  void (*arse_fn)(const char *) = dlsym(lib, "arse");
  if (!arse_fn) {
    fprintf(stderr, "app: dlsym(arse) failed: %s\n", dlerror());
    die();
  }

  fprintf(stderr, "app: calling bruh(3.14f)\n");
  const float result = bruh_fn(3.14f);
  fprintf(stderr, "app:   -> %f\n", result);

  fprintf(stderr, "app: calling arse(wew lad)\n");
  arse_fn("wew lad");

  fprintf(stderr, "app: terminating in 3 sec\n");

  sleep(3);

  vrtld_quit();
  return 0;
}
