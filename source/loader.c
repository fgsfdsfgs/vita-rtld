#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <limits.h>
#include <elf.h>
#include <assert.h>
#include <vitasdk.h>
#include <kubridge.h>

#include "vrtld.h"
#include "loader.h"
#include "common.h"
#include "util.h"
#include "reloc.h"
#include "lookup.h"
#include "vma.h"

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX 0x0C20D050
#endif

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_R
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_R SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_R
#endif

// total modules loaded
static int vrtld_num_modules = 0;

static inline uint32_t dso_convert_pflags(const uint32_t pflags) {
  switch (pflags) {
    case PF_R:        return SCE_KERNEL_MEMBLOCK_TYPE_USER_R;
    case PF_R | PF_X: return SCE_KERNEL_MEMBLOCK_TYPE_USER_RX;
    default:          return SCE_KERNEL_MEMBLOCK_TYPE_USER_RW; // assume rw
  }
}

static int dso_alloc_seg_memblock(dso_seg_t *seg) {
  SceKernelAllocMemBlockKernelOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.size = sizeof(opt);
  opt.attr = 0x1;
  opt.field_C = (uintptr_t)seg->page;
  const SceUID blkid = kuKernelAllocMemBlock("dso_seg", seg->pflags, seg->size, &opt);
  if (blkid >= 0) {
    // the segment should be where we expect it to be
    void *outptr = NULL;
    sceKernelGetMemBlockBase(blkid, &outptr);
    assert(outptr == seg->page);
    seg->blkid = blkid;
    // zero it out; unfortunately there's no kuKernelCpuUnrestrictedMemset
    uint8_t *zero = calloc(1, seg->size);
    if (zero) {
      kuKernelCpuUnrestrictedMemcpy(seg->page, zero, seg->size);
      free(zero);
    }
    return SCE_TRUE;
  }
  return SCE_FALSE;
}

static dso_t *dso_load(const char *filename, const char *modname) {
  size_t file_size = 0;
  Elf32_Ehdr *ehdr = NULL;
  Elf32_Phdr *phdr = NULL;
  Elf32_Shdr *shdr = NULL;
  char *shstrtab = NULL;

  FILE *fd = fopen(filename, "rb");
  if (!fd) {
    vrtld_set_error("Could not open `%s`", filename);
    return NULL;
  }

  fseek(fd, 0, SEEK_END);
  file_size = ftell(fd);
  fseek(fd, 0, SEEK_SET);

  DEBUG_PRINTF("`%s`: total elf size is %u\n", modname, file_size);

  ehdr = memalign(ALIGN_PAGE, file_size);
  if (!ehdr) {
    vrtld_set_error("Could not allocate %u bytes for `%s`", file_size, modname);
    fclose(fd);
    return NULL;
  }

  dso_t *mod = calloc(1, sizeof(dso_t));
  if (!mod) {
    vrtld_set_error("Could not allocate dynmod header");
    free(ehdr);
    fclose(fd);
    return NULL;
  }

  fread(ehdr, file_size, 1, fd);
  fclose(fd);

  if (memcmp(ehdr, ELFMAG, SELFMAG) != 0) {
    vrtld_set_error("`%s` is not a valid ELF file", modname);
    goto err_free_so;
  }

  if (ehdr->e_type != ET_DYN) {
    vrtld_set_error("`%s` is not a shared library", modname);
    goto err_free_so;
  }

  phdr = (Elf32_Phdr *)((uintptr_t)ehdr + ehdr->e_phoff);
  shdr = (Elf32_Shdr *)((uintptr_t)ehdr + ehdr->e_shoff);
  shstrtab = (char *)((uintptr_t)ehdr + shdr[ehdr->e_shstrndx].sh_offset);

  // calculate total size of the LOAD segments (overshoot it by a ton actually)
  // total size = size of last load segment + vaddr of last load segment
  size_t max_align = ALIGN_PAGE;
  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz) {
      const size_t this_size = phdr[i].p_vaddr + phdr[i].p_memsz;
      if (phdr[i].p_align > max_align)
        max_align = phdr[i].p_align;
      if (this_size > mod->size)
        mod->size = this_size;
      mod->num_segs++;
    }
  }

  // round up to max segment alignment
  mod->size = ALIGN_UP(mod->size, max_align);

  DEBUG_PRINTF("`%s`: reserving %u bytes; %u segs total\n", modname, mod->size, mod->num_segs);

  // allocate that much virtual address space
  mod->base = vma_alloc(mod->size);
  if (!mod->base) {
    vrtld_set_error("Could not allocate %u bytes of virtual address space for `%s`", mod->size, modname);
    goto err_free_load;
  }

  // collect segments
  mod->segs = calloc(mod->num_segs, sizeof(*mod->segs));
  if (!mod->segs) {
    vrtld_set_error("Could not allocate space for `%s`'s segment table", modname);
    goto err_free_load;
  }

  for (size_t i = 0, n = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz) {
      mod->segs[n].pflags = dso_convert_pflags(phdr[i].p_flags);
      mod->segs[n].align = (phdr[i].p_align < ALIGN_PAGE) ? ALIGN_PAGE : phdr[i].p_align;
      mod->segs[n].base = (void *)((Elf32_Addr)mod->base + phdr[i].p_vaddr);
      mod->segs[n].page = (void *)ALIGN_DN((Elf32_Addr)mod->segs[n].base, ALIGN_PAGE);
      mod->segs[n].end = (void *)ALIGN_UP((Elf32_Addr)mod->segs[n].base + phdr[i].p_memsz, ALIGN_PAGE);
      mod->segs[n].size = (Elf32_Addr)mod->segs[n].end - (Elf32_Addr)mod->segs[n].page;
      // allocate space for a copy of the segment and zero it out
      if (!dso_alloc_seg_memblock(&mod->segs[n])) {
        vrtld_set_error("Could not allocate %u bytes for segment %u\n", mod->segs[n].size, n);
        goto err_free_load;
      }
      const intptr_t diff = (Elf32_Addr)mod->segs[n].base - (Elf32_Addr)mod->segs[n].page;
      mod->segs[n].base = (void *)((Elf32_Addr)mod->segs[n].page + diff);
      mod->segs[n].end = mod->segs[n].page + mod->segs[n].size;
      // fill it in
      kuKernelCpuUnrestrictedMemcpy(mod->segs[n].base, (void *)((uintptr_t)ehdr + phdr[i].p_offset), phdr[i].p_filesz);
      phdr[i].p_vaddr = (Elf32_Addr)mod->segs[n].base;
      ++n;
    } else if (phdr[i].p_type == PT_DYNAMIC) {
      // remember the dynamic seg
      mod->dynamic = (Elf32_Dyn *)((Elf32_Addr)mod->base + phdr[i].p_vaddr);
    } else if (phdr[i].p_type == PT_ARM_EXIDX) {
      mod->exidx = (void *)((Elf32_Addr)mod->base + phdr[i].p_vaddr);
      mod->num_exidx = phdr->p_memsz / 8;
    }
  }

  DEBUG_PRINTF("`%s`: base = %p\n", modname, mod->base);

  if (!mod->dynamic) {
    vrtld_set_error("`%s` doesn't have a DYNAMIC segment", filename);
    goto err_free_load;
  }

  // find special sections
  for (int i = 0; i < ehdr->e_shnum; i++) {
    const char *sh_name = shstrtab + shdr[i].sh_name;
    if (!strcmp(sh_name, ".dynsym")) {
      mod->dynsym = (Elf32_Sym *)((Elf32_Addr)mod->base + shdr[i].sh_addr);
      mod->num_dynsym = shdr[i].sh_size / sizeof(Elf32_Sym);
    } else if (!strcmp(sh_name, ".dynstr")) {
      mod->dynstrtab = (char *)((Elf32_Addr)mod->base + shdr[i].sh_addr);
    } else if (!strcmp(sh_name, ".hash")) {
      // optional: if there's no hashtab, linear lookup will be used
      mod->hashtab = (uint32_t *)((Elf32_Addr)mod->base + shdr[i].sh_addr);
    } else if (!strcmp(sh_name, ".init_array")) {
      mod->init_array = (void *)((Elf32_Addr)mod->base + shdr[i].sh_addr);
      mod->num_init = shdr[i].sh_size / sizeof(void *);
    } else if (!strcmp(sh_name, ".fini_array")) {
      mod->fini_array = (void *)((Elf32_Addr)mod->base + shdr[i].sh_addr);
      mod->num_fini = shdr[i].sh_size / sizeof(void *);
    } else if (!strcmp(sh_name, ".text")) {
      // useful for gdb
      DEBUG_PRINTF("`%s`: text start = %p\n", modname, mod->base + shdr[i].sh_addr); 
    }
  }

  if (mod->dynsym == NULL || mod->dynstrtab == NULL) {
    vrtld_set_error("No symbol information in `%s`", filename);
    goto err_free_load;
  }

  mod->name = vrtld_strdup(modname);
  mod->flags = MOD_MAPPED;
  vrtld_num_modules++;

  free(ehdr); // don't need this no more

  return mod;

err_free_load:
  vma_free(mod->base);
  for (size_t i = 0; i < mod->num_segs; ++i)
    sceKernelFreeMemBlock(mod->segs[i].blkid);
err_free_so:
  free(mod->segs);
  free(ehdr);
  free(mod);

  return NULL;
}

static void dso_initialize(dso_t *mod) {
  if (mod->init_array) {
    DEBUG_PRINTF("`%s`: init array %p has %u entries\n", mod->name, mod->init_array, mod->num_init);
    for (size_t i = 0; i < mod->num_init; ++i) {
      if (mod->init_array[i])
        mod->init_array[i]();
    }
  }
  mod->flags |= MOD_INITIALIZED;
}

static void dso_finalize(dso_t *mod) {
  if (mod->fini_array) {
    DEBUG_PRINTF("`%s`: fini array %p has %u entries\n", mod->name, mod->fini_array, mod->num_fini);
    for (int i = (int)mod->num_fini - 1; i >= 0; --i) {
      if (mod->fini_array[i])
        mod->fini_array[i]();
    }
    mod->fini_array = NULL;
    mod->num_fini = 0;
  }
  mod->flags &= ~MOD_INITIALIZED;
}

static void dso_link(dso_t *mod) {
  mod->next = vrtld_dsolist.next;
  mod->prev = &vrtld_dsolist;
  if (vrtld_dsolist.next)
    vrtld_dsolist.next->prev = mod;
  vrtld_dsolist.next = mod;
}

static void dso_unlink(dso_t *mod) {
  if (mod->prev)
    mod->prev->next = mod->next;
  if (mod->next)
    mod->next->prev = mod->prev;
  mod->next = NULL;
  mod->prev = NULL;
}

static int dso_relocate_and_init(dso_t *mod, int ignore_undef) {
  if (!(mod->flags & MOD_RELOCATED) && vrtld_relocate(mod, ignore_undef, 0))
    return -1;
  if (!(mod->flags & MOD_INITIALIZED)) {
    // flush caches before trying to run any code
    DEBUG_PRINTF("`%s`: flushing cache range %p - %p\n", mod->name, mod->segs[0].base, (char *)mod->segs[0].base + mod->segs[0].size);
    kuKernelFlushCaches(mod->segs[0].base, mod->segs[0].size);
    dso_initialize(mod);
  }
  dso_link(mod);
  return 0;
}

static int dso_unload(dso_t *mod) {
  if (mod->base == NULL)
    return -1;

  DEBUG_PRINTF("`%s`: unloading\n", mod->name);

  // execute destructors, if any
  if (mod->flags & MOD_INITIALIZED)
    dso_finalize(mod);

  DEBUG_PRINTF("`%s`: unmapping\n", mod->name);

  // unmap and free all segs
  for (size_t i = 0; i < mod->num_segs; ++i)
    sceKernelFreeMemBlock(mod->segs[i].blkid);

  // release virtual address range
  vma_free(mod->base);

  // if we own the symtab, free it
  if (mod->flags & MOD_OWN_SYMTAB) {
    free(mod->dynsym);
    free(mod->dynstrtab);
    free(mod->hashtab);
  }

  vrtld_num_modules--;
  DEBUG_PRINTF("`%s`: unloaded\n", mod->name);

  // free everything else
  free(mod->segs);
  free(mod->name);
  free(mod);

  return 0;
}

static inline int dso_get_addr_info(void *addr, const dso_t *mod, vrtld_dl_info_t *info) {
  if (addr < mod->base || addr >= mod->base + mod->size)
    return 0;

  // fill in the symbol info if this is a symbol
  const Elf32_Sym *sym = vrtld_reverse_lookup_sym(mod, addr);
  if (sym) {
    info->dli_saddr = (void *)((uintptr_t)mod->base + sym->st_value);
    info->dli_sname = mod->dynstrtab + sym->st_name;
  } else {
    info->dli_saddr = NULL;
    info->dli_sname = NULL;
  }

  // at least fill in the module info
  info->dli_fname = mod->name;
  info->dli_fbase = mod->base;

  return 1;
}

void vrtld_unload_all(void) {
  dso_t *mod = vrtld_dsolist.next;
  vrtld_dsolist.next = NULL;

  while (mod) {
    dso_t *next = mod->next;
    dso_unload(mod);
    mod = next;
  }

  // clear main module's exports if needed
  if (vrtld_dsolist.flags & MOD_OWN_SYMTAB) {
    free(vrtld_dsolist.dynsym); vrtld_dsolist.dynsym = NULL;
    free(vrtld_dsolist.dynstrtab); vrtld_dsolist.dynstrtab = NULL;
    free(vrtld_dsolist.hashtab); vrtld_dsolist.hashtab = NULL;
    vrtld_dsolist.flags &= ~MOD_OWN_SYMTAB;
  }
}

/* vrtld API begins */

void *vrtld_dlopen(const char *fname, int flags) {
  dso_t *mod = NULL;

  // clear error flag since we're starting work on a new library
  vrtld_dlerror();

  if (!fname) {
    DEBUG_PRINTF("dlopen(): trying to open root module\n");
    return &vrtld_dsolist;
  }

  // see if the module is already loaded and just increase refcount if it is
  for (dso_t *p = vrtld_dsolist.next; p; p = p->next) {
    if (!strcmp(p->name, fname)) {
      mod = p;
      break;
    }
  }

  if (mod) {
    DEBUG_PRINTF("dlopen(): `%s` is already loaded, increasing refcount\n", fname);
    mod->refcount++;
    return mod;
  }

  // identify the module by absolute path if possible
  char pathbuf[1024] = { 0 };
  const char *modname = realpath(fname, pathbuf);
  if (!modname) modname = fname; // but fall back to the relative name

  // load the module
  mod = dso_load(fname, modname);
  if (!mod) return NULL;

  // relocate and init it right away if not lazy
  if (!(flags & VRTLD_LAZY)) {
    if (dso_relocate_and_init(mod, 0)) {
      dso_unload(mod);
      return NULL;
    }
  }

  mod->flags |= flags;
  mod->refcount = 1;

  return mod;
}

int vrtld_dlclose(void *handle) {
  if (!handle) {
    vrtld_set_error("dlclose(): NULL handle");
    return -1;
  }

  if (handle == &vrtld_dsolist) {
    DEBUG_PRINTF("dlclose(): tried to close main module\n");
    return 0;
  }

  dso_t *mod = handle;
  // free the module when reference count reaches zero
  if (--mod->refcount <= 0) {
    DEBUG_PRINTF("`%s`: refcount is 0, unloading\n", mod->name);
    dso_unlink(mod);
    return dso_unload(mod);
  }

  return 0;
}

void *vrtld_dlsym(void *__restrict handle, const char *__restrict symname) {
  if (!symname || symname[0] == '\0') {
    vrtld_set_error("dlsym(): empty symname");
    return NULL;
  }

  // passed in a handle to the main module
  if (handle == &vrtld_dsolist) {
    handle = NULL;
  }

  // NULL handle means search in order starting with the main module
  dso_t *mod = handle ? handle : &vrtld_dsolist;
  for (; mod; mod = mod->next) {
    if (!(mod->flags & MOD_RELOCATED)) {
      // module isn't ready yet; try to finalize it
      if (dso_relocate_and_init(mod, 0)) {
        dso_unload(mod);
        if (handle)
          return NULL;
        else
          continue;
      }
    }

    void *symaddr = vrtld_lookup(mod, symname);
    if (symaddr) return symaddr;

    // stop early if we're searching in a specific module
    if (handle) {
      vrtld_set_error("`%s`: symbol `%s` not found", mod->name, symname);
      return NULL;
    }
  }

  vrtld_set_error("symbol `%s` not found in any loaded modules", symname);
  return NULL;
}

int vrtld_dladdr(void *addr, vrtld_dl_info_t *info) {
  if (!addr || !info) {
    vrtld_set_error("vrtld_dladdr(): NULL args");
    return 0;
  }

  // by man description only these two fields need to be set to NULL
  info->dli_saddr = NULL;
  info->dli_sname = NULL;

  // ha-ha, time for linear lookup
  // start with the top module after main, since someone's unlikely to be looking for symbol names inside main
  const dso_t *mod;
  for (mod = vrtld_dsolist.next; mod; mod = mod->next) {
    if (dso_get_addr_info(addr, mod, info))
      return 1;
  }

  // do main module last
  return dso_get_addr_info(addr, &vrtld_dsolist, info);
}

void *vrtld_get_handle(void *base) {
  if (!base) {
    vrtld_set_error("vrtld_get_handle(): NULL arg");
    return NULL;
  }

  dso_t *mod;
  for (mod = &vrtld_dsolist; mod; mod = mod->next) {
    if (mod->base == base)
      return mod;
  }

  vrtld_set_error("vrtld_get_handle(): %p is not the base of any loaded module", base);
  return NULL;
}

void *vrtld_get_base(void *handle) {
  if (!handle) {
    vrtld_set_error("vrtld_get_base(): NULL arg");
    return NULL;
  }
  const dso_t *mod = handle;
  return mod->base;
}

unsigned int vrtld_get_size(void *handle) {
  if (!handle) {
    vrtld_set_error("vrtld_get_size(): NULL arg");
    return 0;
  }
  const dso_t *mod = handle;
  return mod->size;
}

void *vrtld_get_exidx(void *handle, unsigned int *out_count) {
  if (!handle) {
    vrtld_set_error("vrtld_get_exidx(): NULL arg");
    return 0;
  }
  const dso_t *mod = handle;
  if (out_count) *out_count = mod->num_exidx;
  return mod->exidx;
}
