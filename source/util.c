#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "util.h"

#define MAX_ERROR 2048

static char errbuf[MAX_ERROR];
static const char *err = NULL;

void vrtld_set_error(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(errbuf, sizeof(errbuf), fmt, args);
  va_end(args);
  if (!err) err = errbuf;
  DEBUG_PRINTF("vrtld error: %s\n", err);
}

const char *vrtld_dlerror(void) {
  const char *ret = err;
  err = NULL;
  return ret;
}

char *vrtld_strdup(const char *s) {
  const size_t len = strlen(s);
  char *ns = malloc(len + 1);
  if (ns) memcpy(ns, s, len + 1);
  return ns;
}

void *vrtld_memdup(const void *src, const size_t size) {
  void *dst = malloc(size);
  if (dst) memcpy(dst, src, size);
  return dst;
}

uint32_t vrtld_elf_hash(const uint8_t *name) {
  uint32_t h = 0, g;
  while (*name) {
    h = (h << 4) + *name++;
    if ((g = (h & 0xf0000000)) != 0)
      h ^= g >> 24;
    h &= 0x0fffffff;
  }
  return h;
}
