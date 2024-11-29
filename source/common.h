#pragma once

#include <vitasdk.h>
#include <stdint.h>
#include <elf.h>

enum dso_flags_internal {
  // states
  MOD_RELOCATED   = 1 << 17,
  MOD_MAPPED      = 1 << 18,
  MOD_INITIALIZED = 1 << 19,
  // additional flags
  MOD_OWN_SYMTAB  = 1 << 24,
};

typedef struct dso_seg {
  SceUID blkid;
  void *base;
  void *page;
  void *end;
  uint32_t size;
  uint32_t align;
  uint32_t pflags;
} dso_seg_t;

typedef struct dso {
  char *name;
  uint32_t flags;
  uint32_t refcount;

  void *base;
  uint32_t size;

  dso_seg_t *segs;
  uint32_t num_segs;

  Elf32_Dyn *dynamic;
  Elf32_Sym *dynsym;
  uint32_t num_dynsym;

  char *dynstrtab;
  uint32_t *hashtab;

  int (**init_array)(void);
  uint32_t num_init;

  int (**fini_array)(void);
  uint32_t num_fini;

  void *exidx;
  uint32_t num_exidx;

  struct dso *next;
  struct dso *prev;
} dso_t;

// some libc stuff we're going to need
extern int _start;

// module list; head is always main module
extern dso_t vrtld_dsolist;
