#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vma.h"
#include "util.h"

// a simple stack allocator for the virtual address space

#define VMA_ALIGNMENT ALIGN_PAGE
#define VMA_MAX_ALLOCS 256

static uintptr_t vma_base;
static uintptr_t vma_ptr;
static uintptr_t vma_lastptr;
static uint32_t vma_size;
static uint32_t vma_left;

static struct {
  uintptr_t ptr; // 0 means this has been marked as free
  uint32_t size;
} vma_allocs[VMA_MAX_ALLOCS];

static uint32_t vma_numallocs;

void vma_init(void) {
  vma_base = VRTLD_VMA_START;
  vma_ptr = vma_lastptr = vma_base;
  vma_size = vma_left = VRTLD_VMA_END - vma_base;
  vma_numallocs = 0;
  memset(vma_allocs, 0, sizeof(vma_allocs));
  DEBUG_PRINTF("vma_init(): vma_base=0x%08x vma_size=0x%08x\n", vma_base, vma_size);
}

void *vma_alloc(size_t size) {
  size = ALIGN_UP(size, VMA_ALIGNMENT);

  if (size == 0) {
    DEBUG_PRINTF("vma_alloc(): size == 0\n");
    return 0;
  }

  if (vma_left < size) {
    DEBUG_PRINTF("vma_alloc(): failed to alloc %u bytes\n", size);
    return 0;
  }

  if (vma_numallocs == VMA_MAX_ALLOCS) {
    DEBUG_PRINTF("vma_alloc(): MAX_ALLOCS reached\n");
    return 0;
  }

  vma_lastptr = vma_ptr;
  vma_ptr += size;
  vma_left -= size;

  const uint32_t i = vma_numallocs++;
  vma_allocs[i].size = size;
  vma_allocs[i].ptr = vma_lastptr;

  DEBUG_PRINTF("vma_alloc(): allocated %u bytes at 0x%08x, %u free\n", size, vma_lastptr, vma_left);

  return (void *)vma_lastptr;
}

void vma_free(void *vptr) {
  const uintptr_t ptr = (uintptr_t)vptr;

  if (!ptr)
    return; // no-op

  if (vma_numallocs == 0) {
    DEBUG_PRINTF("vma_free(): nothing to free\n");
    return;
  }

  if (ptr == vma_lastptr) {
    // this was the top of the stack; dealloc it and everything that's marked as free below it
    const int top = vma_numallocs - 1;
    vma_allocs[top].ptr = 0; // mark top as free
    for (int i = top; i >= 0; --i) {
      if (vma_allocs[i].ptr) {
        // found a live alloc, restore to that point
        vma_lastptr = vma_allocs[i].ptr;
        vma_ptr = vma_lastptr + vma_allocs[i].size;
        DEBUG_PRINTF("vma_free(): resetting to last alloc of %u bytes at 0x%08x, %u free\n", vma_allocs[i].size, vma_lastptr, vma_left);
        return;
      } else {
        DEBUG_PRINTF("vma_free(): chain-freeing %u bytes\n", vma_allocs[i].size);
        vma_left += vma_allocs[i].size;
        vma_allocs[i].size = 0;
      }
    }
    // didn't find any live allocs, clear the state
    DEBUG_PRINTF("vma_free(): resetting to base state\n");
    vma_lastptr = vma_ptr = vma_base;
    vma_left = vma_size;
    return;
  }

  // not the top of the stack; just find it and mark as free
  for (uint32_t i = 0; i < vma_numallocs; ++i) {
    if (vma_allocs[i].ptr == ptr) {
      DEBUG_PRINTF("vma_free(): marking %u bytes at 0x%08x as free\n", vma_allocs[i].size, vma_allocs[i].ptr);
      vma_allocs[i].ptr = 0;
      return;
    }
  }

  DEBUG_PRINTF("vma_free(): tried to free unknown pointer 0x%08x\n", ptr);
}
