#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum vrtld_init_flags {
  VRTLD_INITIALIZED    = 1,  /* library is operational */
  VRTLD_NO_SCE_EXPORTS = 2,  /* don't search main module's exports table */
};

enum vrtld_dlopen_flags {
  VRTLD_LOCAL  = 0,  /* don't use this module's symbols when resolving others */
  VRTLD_GLOBAL = 1,  /* use this module's symbols when resolving others */
  VRTLD_NOW    = 0,  /* finalize loading before dlopen() returns */
  VRTLD_LAZY   = 2,  /* finalize loading only after dlsym() is called */
};

typedef struct vrtld_export {
  const char *name;  /* symbol name */
  void *addr_rx;     /* executable address */
} vrtld_export_t;

/* this is just Dl_info */
typedef struct vrtld_dl_info {
  const char *dli_fname;  /* pathname of shared object that contains address */
  void *dli_fbase;        /* base address at which shared object is loaded */
  const char *dli_sname;  /* name of symbol whose definition overlaps addr */
  void *dli_saddr;        /* exact address of symbol named in dli_sname */
} vrtld_dl_info_t;

#define VRTLD_EXPORT_SYMBOL(sym) { #sym, (void *)&sym }
#define VRTLD_EXPORT(name, addr) { name, addr }

/* special handle meaning "this module" */
#define VRTLD_DEFAULT (NULL)

/* initialize loader */
int vrtld_init(const unsigned int flags);
/* deinit loader and free all libraries */
void vrtld_quit(void);
/* returns the `flags` value with which library was initialized, or 0 if it wasn't */
unsigned int vrtld_init_flags(void);
/* set the aux exports table */
int vrtld_set_main_exports(const vrtld_export_t *exp, const int numexp);

/* these function mostly the same as the equivalent dlfcn stuff */
void *vrtld_dlopen(const char *fname, int flags);
int vrtld_dlclose(void *handle);
void *vrtld_dlsym(void *__restrict handle, const char *__restrict symname);
/* return current error and reset the error flag */
const char *vrtld_dlerror(void);
/* reverse lookup symbol name by its address */
int vrtld_dladdr(void *addr, vrtld_dl_info_t *info);

/* get module handle from module base */
void *vrtld_get_handle(void *base);
/* get module base from module handle */
void *vrtld_get_base(void *handle);
/* get module size from module handle */
unsigned int vrtld_get_size(void *handle);
/* get module's exidx table, if any */
void *vrtld_get_exidx(void *handle, unsigned int *out_count);

#ifdef VRTLD_LIBDL_COMPAT

/* provide "compatibility layer" with libdl */

#undef dlopen
#undef dlclose
#undef dlsym
#undef dlerror
#undef dladdr
#undef RTLD_LOCAL
#undef RTLD_GLOBAL
#undef RTLD_NOW
#undef RTLD_LAZY
#undef RTLD_DEFAULT

#define dlopen(x, y) vrtld_dlopen((x), (y))
#define dlclose(x)   vrtld_dlclose((x))
#define dlsym(x, y)  vrtld_dlsym((x), (y))
#define dladdr(x, y) vrtld_dladdr((x), (y))
#define dlerror()    vrtld_dlerror()

#define RTLD_LOCAL   VRTLD_LOCAL
#define RTLD_GLOBAL  VRTLD_GLOBAL
#define RTLD_NOW     VRTLD_NOW
#define RTLD_LAZY    VRTLD_LAZY
#define RTLD_DEFAULT VRTLD_DEFAULT

typedef vrtld_dl_info_t Dl_info;

#endif

#ifdef __cplusplus
}
#endif
