#pragma once

#include "common.h"

const Elf32_Sym *vrtld_lookup_sym(const dso_t *mod, const char *symname);
const Elf32_Sym *vrtld_reverse_lookup_sym(const dso_t *mod, const void *addr);

void *vrtld_lookup(const dso_t *mod, const char *symname);
void *vrtld_lookup_global(const char *symname);
void *vrtld_lookup_sce_export(const char *symname);
