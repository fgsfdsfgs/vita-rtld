#pragma once

#include <stdint.h>
#include <elf.h>
#include "vrtld.h"

// optional user-defined global exports
extern __attribute__((weak)) const vrtld_export_t *__vrtld_exports;
extern __attribute__((weak)) const size_t __vrtld_num_exports;

// optional user-defined global exports that will override everything
extern __attribute__((weak)) const vrtld_export_t *__vrtld_override_exports;
extern __attribute__((weak)) const size_t __vrtld_num_override_exports;

int vrtld_symtab_from_exports(
  const vrtld_export_t *exp,
  const int numexp,
  Elf32_Sym **out_symtab,
  char **out_strtab,
  uint32_t **out_hashtab
);

int vrtld_set_main_exports(const vrtld_export_t *exp, const int numexp);
