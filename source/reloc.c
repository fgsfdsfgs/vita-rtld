#include <string.h>
#include <kubridge.h>

#include "common.h"
#include "vrtld.h"
#include "util.h"
#include "lookup.h"
#include "reloc.h"

static int process_relocs(dso_t *mod, const Elf32_Rel *rels, const size_t num_rels, const int imports_only, const int ignore_undef) {
  int num_failed = 0;

  for (size_t j = 0; j < num_rels; j++) {
    uintptr_t *ptr = (uintptr_t *)((uintptr_t)mod->base + rels[j].r_offset);
    const uintptr_t symno = ELF32_R_SYM(rels[j].r_info);
    const int type = ELF32_R_TYPE(rels[j].r_info);
    uintptr_t symval = 0;
    uintptr_t symbase = (uintptr_t)mod->base;
    const char *symname = NULL;

    if (symno) {
      // if the reloc refers to a symbol, get the symbol value in there
      const Elf32_Sym *sym = &mod->dynsym[symno];
      if (sym->st_shndx == SHN_UNDEF) {
        symname = mod->dynstrtab + sym->st_name;
        symval = (uintptr_t)vrtld_lookup_global(symname);
        symbase = 0; // symbol is somewhere else
        if (!symval) {
          const int weak = (ELF32_ST_BIND(sym->st_info) == STB_WEAK);
          if (weak || ignore_undef) {
            // ignore resolution failure for weak syms or if we don't care
            DEBUG_PRINTF("`%s`: ignoring resolution failure for `%s`%s\n", mod->name, symname, weak ? " (weak)" : "");
            continue;
          } else {
            vrtld_set_error("`%s`: Could not resolve symbol: `%s`", mod->name, symname);
            ++num_failed;
          }
        }
      } else {
        if (imports_only) continue;
        symval = sym->st_value;
      }
    } else if (imports_only) {
      continue;
    }

    switch (type) {
      case R_ARM_RELATIVE:
        *ptr += symbase;
        break;
      case R_ARM_ABS32:
        *ptr += symbase + symval;
        break;
      case R_ARM_GLOB_DAT:
      case R_ARM_JUMP_SLOT:
        *ptr = symbase + symval;
        break;
      case R_ARM_NONE:
        break; // sorry nothing
      default:
        vrtld_set_error("`%s`: Unknown relocation type: %d", mod->name, type);
        return -1;
    }
  }

  return num_failed;
}

static int process_target2_relocs(dso_t *mod, const Elf32_Rel *rels, const size_t num_rels) {
  uint32_t target2_type = R_ARM_REL32; // vita native
  if (vrtld_init_flags() & VRTLD_TARGET2_IS_ABS)
    target2_type = R_ARM_ABS32;
  else if (vrtld_init_flags() & VRTLD_TARGET2_IS_GOT)
    target2_type = R_ARM_GOT_PREL;

  if (target2_type == R_ARM_REL32)
    return 0; // nothing to do

  for (size_t j = 0; j < num_rels; j++) {
    if (ELF32_R_TYPE(rels[j].r_info) != R_ARM_TARGET2)
      continue;

    // on the Vita TARGET2 relocs are treated as REL32, so we need to turn these into REL32
    intptr_t *ptr = (intptr_t *)((uintptr_t)mod->base + rels[j].r_offset);
    uint8_t *target = NULL;
    switch (target2_type) {
      case R_ARM_ABS32:
        // ptr points to an absolute address, turn it into a relative
        target = *(uint8_t **)ptr;
        break;
      case R_ARM_GOT_PREL:
        // ptr points to an offset to a GOT slot, which has already been filled in by process_relocs()
        if (*ptr) {
          target = *(uint8_t **)((uint8_t *)ptr + *ptr);
        }
        break;
      default:
        break;
    }

    if (target) {
      const intptr_t result = target - (uint8_t *)ptr;
      // these usually point to rodata, so we need to resort to this to bypass memory protection
      kuKernelCpuUnrestrictedMemcpy(ptr, &result, sizeof(result));
    }
  }

  return 0;
}

int vrtld_relocate(dso_t *mod, const int ignore_undef, const int imports_only) {
  Elf32_Rel *rel = NULL;
  Elf32_Rel *jmprel = NULL;
  uint32_t pltrel = 0;
  size_t relsz = 0;
  size_t pltrelsz = 0;

  // find REL and JMPREL
  for (Elf32_Dyn *dyn = mod->dynamic; dyn->d_tag != DT_NULL; dyn++) {
    switch (dyn->d_tag) {
      case DT_REL:
        rel = (Elf32_Rel *)(mod->base + dyn->d_un.d_ptr);
        break;
      case DT_RELSZ:
        relsz = dyn->d_un.d_val;
        break;
      case DT_JMPREL:
        // TODO: don't assume REL
        jmprel = (Elf32_Rel *)(mod->base + dyn->d_un.d_ptr);
        break;
      case DT_PLTREL:
        pltrel = dyn->d_un.d_val;
        break;
      case DT_PLTRELSZ:
        pltrelsz = dyn->d_un.d_val;
        break;
      default:
        break;
    }
  }

  if (rel && relsz) {
    DEBUG_PRINTF("`%s`: processing REL@%p size %u\n", mod->name, rel, relsz);
    // if there are any unresolved imports, bail unless it's the final relocation pass
    if (process_relocs(mod, rel, relsz / sizeof(Elf32_Rel), imports_only, ignore_undef))
      return -1;
  }

  if (jmprel && pltrelsz && pltrel) {
    // TODO: support DT_RELA?
    if (pltrel == DT_REL) {
      DEBUG_PRINTF("`%s`: processing JMPREL@%p size %u\n", mod->name, jmprel, pltrelsz);
      // if there are any unresolved imports, bail unless it's the final relocation pass
      if (process_relocs(mod, jmprel, pltrelsz / sizeof(Elf32_Rel), imports_only, ignore_undef))
        return -1;
    } else {
      DEBUG_PRINTF("`%s`: DT_JMPREL has unsupported type %08x\n", mod->name, pltrel);
    }
  }

  if(mod->extab_rel) {
    // fixup target2 relocs
    DEBUG_PRINTF("`%s`: processing .rel.ARM.extab@%p count %u\n", mod->name, mod->extab_rel, mod->num_extab_rel);
    process_target2_relocs(mod, mod->extab_rel, mod->num_extab_rel);
    // don't need this anymore
    free(mod->extab_rel);
    mod->extab_rel = NULL;
    mod->num_extab_rel = 0;
  }

  mod->flags |= MOD_RELOCATED;

  return 0;
}
