#pragma once

#include <stdint.h>

// approximate borders of the free virtual address space we can use
#define VRTLD_VMA_START 0x98000000
#define VRTLD_VMA_END   0xA2000000

void vma_init(void);
void *vma_alloc(size_t size);
void vma_free(void *vptr);
