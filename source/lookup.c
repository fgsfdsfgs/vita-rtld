#include <string.h>
#include <stdint.h>
#include <vitasdk.h>
#include <taihen.h>

#include "common.h"
#include "vrtld.h"
#include "util.h"
#include "exports.h"
#include "lookup.h"

// sce exports stuff shamelessly stolen from vita-rss-libdl

typedef struct sce_module_exports {
  uint16_t size;           // size of this structure; 0x20 for Vita 1.x
  uint8_t  lib_version[2]; //
  uint16_t attribute;      // ?
  uint16_t num_functions;  // number of exported functions
  uint16_t num_vars;       // number of exported variables
  uint16_t unk;
  uint32_t num_tls_vars;   // number of exported TLS variables?  <-- pretty sure wrong // yifanlu
  uint32_t lib_nid;        // NID of this specific export list; one PRX can export several names
  char     *lib_name;      // name of the export module
  uint32_t *nid_table;     // array of 32-bit NIDs for the exports, first functions then vars
  void     **entry_table;  // array of pointers to exported functions and then variables
} sce_module_exports_t;

static void *sce_module_get_export(const char *module_name, const uint32_t func_nid) {
  tai_module_info_t tai_info = { 0 };
  tai_info.size = sizeof(tai_info);

  const int ret = taiGetModuleInfo(module_name, &tai_info);
  if (ret < 0) {
    DEBUG_PRINTF("sce_module_get_export() failed: taiGetModuleInfo() returned %d\n", ret);
    return NULL;
  }

  uintptr_t i = tai_info.exports_start;
  while (i < tai_info.exports_end) {
    const sce_module_exports_t *imp_info = (sce_module_exports_t *)(i);
    for (int j = 0; j < imp_info->num_functions + imp_info->num_vars; j++) {
      if (imp_info->nid_table[j] == func_nid)
        return imp_info->entry_table[j];
    }
    i += imp_info->size;
  }

  return NULL;
}

static uint32_t sce_nid_for_name(const char *name) {
  SceSblDmac5HashTransformParam param = { 0 };
  uint8_t src[0x40 + 0x3F];
  uint8_t dst[0x20];
  const size_t len = strlen(name);

  void *p = (void*)((uintptr_t)(src + 0x3F) & ~0x3F);
  snprintf((char*)p, 64, name);

  param.src = p;
  param.dst = dst;
  param.length = len;

  const int ret = sceSblDmac5HashTransform(&param, 0x13, 0);
  if (ret < 0) {
    DEBUG_PRINTF("sce_nid_for_name(%s) failed: %d\n", name, ret);
    return 0;
  }

  return (dst[0] << 24) | (dst[1] << 16) | (dst[2] << 8) | dst[3];
}

void *vrtld_lookup_sce_export(const char *symname) {
  if ((vrtld_init_flags() & VRTLD_NO_SCE_EXPORTS) == 0) {
    const uint32_t nid = sce_nid_for_name(symname);
    if (nid > 0)
      return sce_module_get_export(TAI_MAIN_MODULE, nid);
  }
  return NULL;
}

const Elf32_Sym *vrtld_elf_hashtab_lookup(
  const char *strtab,
  const Elf32_Sym *symtab,
  const uint32_t *hashtab,
  const char *symname
) {
    const uint32_t hash = vrtld_elf_hash((const uint8_t *)symname);
    const uint32_t nbucket = hashtab[0];
    const uint32_t *bucket = &hashtab[2];
    const uint32_t *chain = &bucket[nbucket];
    const uint32_t bucketidx = hash % nbucket;
    for (uint32_t i = bucket[bucketidx]; i; i = chain[i]) {
      if (!strcmp(symname, strtab + symtab[i].st_name))
        return symtab + i;
    }
    return NULL;
}

const Elf32_Sym *vrtld_lookup_sym(const dso_t *mod, const char *symname) {
  if (!mod || !mod->dynsym || !mod->dynstrtab)
    return NULL;
  // if hashtab is available, use that for lookup, otherwise do linear search
  if (mod->hashtab)
    return vrtld_elf_hashtab_lookup(mod->dynstrtab, mod->dynsym, mod->hashtab, symname);
  // sym 0 is always UNDEF
  for (size_t i = 1; i < mod->num_dynsym; ++i) {
    if (!strcmp(symname, mod->dynstrtab + mod->dynsym[i].st_name))
      return mod->dynsym + i;
  }
  return NULL;
}

void *vrtld_lookup(const dso_t *mod, const char *symname) {
  // try normal elf lookup first
  const Elf32_Sym *sym = vrtld_lookup_sym(mod, symname);
  if (sym && sym->st_shndx != SHN_UNDEF)
    return (uint8_t *)mod->base + sym->st_value;
  // if this is the main module, try SCE exports table as a last resort
  if (mod == &vrtld_dsolist)
    return vrtld_lookup_sce_export(symname);
  // didn't find anything
  return NULL;
}

const Elf32_Sym *vrtld_reverse_lookup_sym(const dso_t *mod, const void *addr) {
  if (!(mod->flags & MOD_RELOCATED) || !mod->dynsym || mod->num_dynsym <= 1)
    return NULL;
  // skip mandatory UNDEF
  for (size_t i = 1; i < mod->num_dynsym; ++i) {
    if (mod->dynsym[i].st_shndx != SHN_UNDEF && mod->dynsym[i].st_value) {
      const uintptr_t symaddr = mod->dynsym[i].st_value + (uintptr_t)mod->base;
      if (symaddr == (uintptr_t)addr)
        return mod->dynsym + i;
    }
  }
  return NULL;
}

void *vrtld_lookup_global(const char *symname) {
  if (!symname || !*symname)
    return NULL;

  // try the override exports table if it exists
  if (&__vrtld_override_exports && &__vrtld_num_override_exports && __vrtld_override_exports) {
    for (size_t i = 0; i < __vrtld_num_override_exports; ++i)
      if (!strcmp(symname, __vrtld_override_exports[i].name))
        return __vrtld_override_exports[i].addr_rx;
  }

  // try SCE exports table of the main module
  void *exp = vrtld_lookup_sce_export(symname);
  if (exp) return exp;

  // try actual modules
  const dso_t *mod = &vrtld_dsolist;
  while (mod) {
    const Elf32_Sym *sym = vrtld_lookup_sym(mod, symname);
    if (sym && sym->st_shndx != SHN_UNDEF)
      return (void *)((uintptr_t)mod->base + sym->st_value);
    mod = mod->next;
  }

  return NULL;
}
