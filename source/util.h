#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#ifdef DEBUG
#define DEBUG_PRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINTF(...)
#endif

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DN(x, align) (((x) / (align)) * (align))
#define ALIGN_PAGE 0x1000

void vrtld_set_error(const char *fmt, ...);

char *vrtld_strdup(const char *s);
void *vrtld_memdup(const void *src, const size_t size);

uint32_t vrtld_elf_hash(const uint8_t *name);
