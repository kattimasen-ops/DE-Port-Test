#ifndef SO_UTIL_H
#define SO_UTIL_H
#include <stdint.h>
#include <stddef.h>

void  *so_load(const char *path);
void  *so_find_addr(void *handle, const char *name);
void   so_flush_caches(void);
void   so_set_imports(void);
void   so_dump_symbols(void *handle);

/* NEU: für dlopen/dlsym-Hook */
int    so_is_our_handle(void *handle);

#endif /* SO_UTIL_H */
