#include <stdio.h>
#include <string.h>
#include <vitasdk.h>

#include "common.h"
#include "util.h"
#include "loader.h"
#include "exports.h"
#include "vma.h"
#include "vrtld.h"

static int init_flags = 0;

// the main module is the head and is never unloaded
dso_t vrtld_dsolist = {
  "$main",
  .base = (void *)&_start,
  // we're already all done
  .flags = MOD_MAPPED | MOD_RELOCATED | MOD_INITIALIZED,
};

static int check_kubridge(void) {
  int search_unk[2];
  return _vshKernelSearchModuleByName("kubridge", search_unk);
}

int vrtld_init(const unsigned int flags) {
  if (check_kubridge() < 0) {
    vrtld_set_error("kubridge not detected");
    return -1;
  }

  init_flags = VRTLD_INITIALIZED | flags;

  // initialize virtual memory "allocator"
  vma_init();

  // check if there's any user-defined exports
  vrtld_set_main_exports(NULL, 0);

  // clear error flag
  vrtld_dlerror();

  return 0;
}

unsigned int vrtld_init_flags(void) {
  return init_flags;
}

void vrtld_quit(void) {
  if (!init_flags) {
    vrtld_set_error("vrtld is not initialized");
    return;
  }

  vrtld_unload_all();

  init_flags = 0;

  vrtld_dlerror(); // clear error flag
}
