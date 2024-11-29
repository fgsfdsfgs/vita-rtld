#include <string.h>

#include "common.h"
#include "vrtld.h"
#include "util.h"

// own exidx section
extern uintptr_t __exidx_start;
extern uintptr_t __exidx_end;

void *__gnu_Unwind_Find_exidx(void *pc, uint32_t *pcount) __attribute__((used));

void *__gnu_Unwind_Find_exidx(void *pc, uint32_t *pcount) {
  // find which loaded module this belongs to
  const dso_t *mod = NULL;
  for (mod = vrtld_dsolist.next; mod; mod = mod->next) {
    if (pc >= mod->base && pc < mod->base + mod->size)
      break;
  }

  if (mod && mod->exidx) {
    void *start = mod->exidx;
    *pcount = mod->num_exidx;
    return start;
  }

  // if this is not from a DSO, default to main exidx
  const uintptr_t start = (uintptr_t)&__exidx_start;
  const uintptr_t end = (uintptr_t)&__exidx_end;
  *pcount = (end - start) / 8;
  return (void *)start;
}
