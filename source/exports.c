#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <elf.h>

#include "common.h"
#include "vrtld.h"
#include "util.h"
#include "exports.h"

int vrtld_symtab_from_exports(
  const vrtld_export_t *exp,
  const int numexp,
  Elf32_Sym **out_symtab,
  char **out_strtab,
  uint32_t **out_hashtab
) {
  const uintptr_t base = (uintptr_t)&_start;
  const uint32_t nchain = numexp + 1; // + NULL symbol
  const uint32_t nbucket = nchain * 2 + 1; // FIXME: is this even right
  char *strtab = NULL;
  Elf32_Sym *symtab = NULL;
  uint32_t *hashtab = NULL;

  if (!exp || !numexp || !out_symtab || !out_strtab || !out_hashtab)
    return -1;

  // bucket array + chain array + two ints for lengths
  hashtab = calloc(nchain + nbucket + 2, sizeof(uint32_t));
  if (!hashtab) goto _error;

  symtab = calloc(nchain, sizeof(Elf32_Sym));
  if (!symtab) goto _error;

  // calculate string table size
  size_t strtabsz = 1; // for undefined symname, "\0"
  for (int i = 0; i < numexp; ++i)
    strtabsz += 1 + strlen(exp[i].name);

  strtab = malloc(strtabsz);
  if (!strtab) goto _error;

  // first entry is an empty string
  size_t strptr = 1;
  strtab[0] = '\0';
  symtab[0].st_name = strptr;
  // the rest are just symbol names packed together
  // fill symtab while we're at it
  for (int i = 0; i < numexp; ++i) {
    const size_t slen = strlen(exp[i].name) + 1;
    memcpy(strtab + strptr, exp[i].name, slen);
    symtab[i + 1].st_name = strptr;
    symtab[i + 1].st_shndx = SHN_ABS; // who fucking knows if this is correct
    symtab[i + 1].st_value = (uintptr_t)exp[i].addr_rx - base;
    strptr += slen;
  }
  // should be filled by now
  assert(strtabsz == strptr);

  hashtab[0] = nbucket;
  hashtab[1] = nchain;
  uint32_t *bucket = &hashtab[2];
  uint32_t *chain = &bucket[nbucket];
  for (unsigned int i = 0; i < nbucket; i++)
    bucket[i] = STN_UNDEF;
  for (unsigned int i = 0; i < nchain; i++)
    chain[i] = STN_UNDEF;

  // fill hash table
  for (Elf32_Word i = 0; i < nchain; ++i) {
    const char *symname = strtab + symtab[i].st_name;
    const uint32_t h = vrtld_elf_hash((const uint8_t *)symname);
    const uint32_t n = h % nbucket;
    if (bucket[n] == STN_UNDEF) {
      bucket[n] = i;
    } else {
      Elf32_Word y = bucket[n];
      while (chain[y] != STN_UNDEF)
        y = chain[y];
      chain[y] = i;
    }
  }

  *out_symtab = symtab;
  *out_hashtab = hashtab;
  *out_strtab = strtab;

  return 0;

_error:
  free(hashtab);
  free(symtab);
  free(strtab);
  return -1;
}

int vrtld_set_main_exports(const vrtld_export_t *exp, const int numexp) {
  Elf32_Sym *symtab = NULL;
  uint32_t *hashtab = NULL;
  char *strtab = NULL;

  if (exp != NULL) {
    // if we got a custom export table, turn it into a symtab and use it
    if (vrtld_symtab_from_exports(exp, numexp, &symtab, &strtab, &hashtab) == 0)
      vrtld_dsolist.flags |= MOD_OWN_SYMTAB; // to free it later
  }

  // didn't get a custom table, try the user-defined exports table
  if (symtab == NULL) {
    if (&__vrtld_exports && &__vrtld_num_exports && __vrtld_exports) {
      if (vrtld_symtab_from_exports(__vrtld_exports, __vrtld_num_exports, &symtab, &strtab, &hashtab) == 0)
        vrtld_dsolist.flags |= MOD_OWN_SYMTAB; // to free it later
    }
  }

  // didn't get anything -- bail
  if (symtab == NULL) return -1;

  vrtld_dsolist.num_dynsym = hashtab[1]; // nchain == number of symbols
  vrtld_dsolist.dynsym = symtab;
  vrtld_dsolist.hashtab = hashtab;
  vrtld_dsolist.dynstrtab = strtab;

  // we now have symbols for other libs to use, so we need to mark ourselves as GLOBAL
  vrtld_dsolist.flags |= VRTLD_GLOBAL;

  return 0;
}
