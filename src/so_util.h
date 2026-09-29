#ifndef SO_UTIL_H
#define SO_UTIL_H
#include <stdint.h>
#include <stddef.h>

void  *so_load(const char *path);
void  *so_find_addr(void *handle, const char *name);
void   so_flush_caches(void);
void   so_set_imports(void);
void   so_dump_symbols(void *handle);

int    so_is_our_handle(void *handle);

int    so_module_count(void);
const char *so_module_path(int index);

#endif
