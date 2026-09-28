#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <android/log.h>
#include "so_util.h"

#define TAG "so_util"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef R_AARCH64_COPY
#define R_AARCH64_COPY 1024
#endif
#ifndef R_AARCH64_TLSDESC
#define R_AARCH64_TLSDESC 1031
#endif

typedef struct {
    void *base;
    size_t size;
    uint64_t min_vaddr;
    Elf64_Dyn *dyn;
    Elf64_Sym *symtab;
    const char *strtab;
    size_t symcount;
    Elf64_Rela *rela;   size_t relacount;
    Elf64_Rela *jmprel; size_t jmprelcount;
    void (**init_array)(void);
    size_t init_count;
    void (*init_fn)(void);
} so_module;

#define MAX_MODULES 8
static so_module g_modules[MAX_MODULES];
static int g_nmods = 0;

#define SYM_CACHE_SIZE 512
static struct {
    const char *name;
    void *addr;
} g_sym_cache[SYM_CACHE_SIZE];
static int g_sym_cache_next = 0;

/* Heap-Konsistenzpruefung auf Top-Chunk-Ebene.
 * Die 128-KB-Allokation kommt aus dem Top-Chunk und zwingt glibc,
 * die Metadaten zu validieren. Kleine Allokationen nutzen tcache
 * und wuerden eine Top-Chunk-Korruption nicht bemerken. */
static int heap_sane_check(const char *when) {
    void *a = malloc(16);
    if (!a) { LOGE("  Heap-Check(%s): malloc(16) fehlgeschlagen", when); return 0; }
    memset(a, 0xAA, 16);
    free(a);

    void *big = malloc(128 * 1024);
    if (!big) { LOGE("  Heap-Check(%s): malloc(128K) fehlgeschlagen", when); return 0; }
    memset(big, 0xCC, 128 * 1024);
    free(big);

    return 1;
}

static void *resolve_symbol_full(const char *name) {
    for (int i = 0; i < g_nmods; i++) {
        so_module *m = &g_modules[i];
        if (!m->symtab || !m->strtab) continue;
        for (size_t j = 0; j < m->symcount; j++) {
            Elf64_Sym *s = &m->symtab[j];
            if (s->st_shndx == SHN_UNDEF) continue;
            if (!s->st_name) continue;
            if (strcmp(m->strtab + s->st_name, name) == 0) {
                return (char *)m->base + s->st_value;
            }
        }
    }

    void *p = dlsym(RTLD_DEFAULT, name);
    if (p) return p;

    static void *sys_libs[12] = {0};
    static const char *sys_names[] = {
        "libc++_shared.so",
        "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
        "libEGL.so.1", "libGLESv2.so.2", "libGLESv1_CM.so.1",
        "libSDL2-2.0.so.0", "libz.so.1", "libstdc++.so.6",
        NULL
    };
    for (size_t i = 0; sys_names[i]; i++) {
        if (!sys_libs[i]) {
            sys_libs[i] = dlopen(sys_names[i], RTLD_NOW | RTLD_GLOBAL);
        }
        if (sys_libs[i]) {
            p = dlsym(sys_libs[i], name);
            if (p) return p;
        }
    }
    return NULL;
}

static void *resolve_symbol_cached(const char *name) {
    for (int i = 0; i < SYM_CACHE_SIZE; i++) {
        if (g_sym_cache[i].name && strcmp(g_sym_cache[i].name, name) == 0) {
            return g_sym_cache[i].addr;
        }
    }
    void *addr = resolve_symbol_full(name);
    g_sym_cache[g_sym_cache_next].name = name;
    g_sym_cache[g_sym_cache_next].addr = addr;
    g_sym_cache_next = (g_sym_cache_next + 1) % SYM_CACHE_SIZE;
    return addr;
}

static void relocate(so_module *m, Elf64_Rela *rel, size_t count) {
    for (size_t i = 0; i < count; i++) {
        Elf64_Rela *r = &rel[i];
        uint32_t type = ELF64_R_TYPE(r->r_info);
        uint32_t sym  = ELF64_R_SYM(r->r_info);

        if (r->r_offset + 16 > m->size) {
            LOGE("Ungueltiger Reloc-Offset 0x%lx — ueberspringe",
                 (unsigned long)r->r_offset);
            continue;
        }

        uint64_t *ptr = (uint64_t *)((uintptr_t)m->base + r->r_offset);
        switch (type) {
            case R_AARCH64_NONE: break;

            case R_AARCH64_RELATIVE:
                *ptr = (uint64_t)m->base + r->r_addend;
                break;

            case R_AARCH64_ABS64:
            case R_AARCH64_GLOB_DAT:
            case R_AARCH64_JUMP_SLOT: {
                const char *name = (sym < m->symcount)
                                   ? m->strtab + m->symtab[sym].st_name
                                   : NULL;
                if (!name || !*name) { *ptr = 0; break; }
                void *res = resolve_symbol_cached(name);
                if (!res) {
                    LOGE("Unresolved: %s", name);
                    *ptr = 0;
                } else {
                    *ptr = (uint64_t)res;
                }
                break;
            }

            case R_AARCH64_COPY: {
                if (sym >= m->symcount) { *ptr = 0; break; }
                Elf64_Sym *s = &m->symtab[sym];
                const char *name = m->strtab + s->st_name;
                void *src = resolve_symbol_cached(name);
                if (!src) {
                    LOGE("COPY unresolved: %s", name);
                    *ptr = 0;
                    break;
                }
                size_t sz = s->st_size;
                if (sz > 0) {
                    if (r->r_offset + sz > m->size) sz = m->size - r->r_offset;
                    memcpy(ptr, src, sz);
                    LOGI("COPY: %s -> %p (%zu Bytes)", name, ptr, sz);
                }
                break;
            }

            case R_AARCH64_TLSDESC: {
                static int warned = 0;
                if (!warned) {
                    const char *name = (sym < m->symcount)
                        ? m->strtab + m->symtab[sym].st_name : "?";
                    LOGI("TLSDESC @ 0x%lx (sym=%s) — setze Descriptor=0",
                         (unsigned long)r->r_offset, name);
                    warned = 1;
                }
                ptr[0] = 0;
                ptr[1] = 0;
                break;
            }

            default:
                LOGI("Unknown reloc type %u at 0x%lx", type,
                     (unsigned long)r->r_offset);
                break;
        }
    }
}

static const char *find_nearest_symbol(so_module *m, uint64_t target_off) {
    static char buf[320];
    const char *best = NULL;
    uint64_t best_dist = (uint64_t)-1;
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF) continue;
        if (!s->st_name) continue;
        if (s->st_value == 0) continue;
        unsigned type = ELF64_ST_TYPE(s->st_info);
        if (type != STT_FUNC && type != STT_OBJECT && type != STT_NOTYPE) continue;
        if (s->st_value <= target_off) {
            uint64_t dist = target_off - s->st_value;
            if (dist < best_dist) {
                best_dist = dist;
                best = m->strtab + s->st_name;
            }
        }
    }
    if (best) {
        snprintf(buf, sizeof(buf), "%s+0x%lx", best, (unsigned long)best_dist);
        return buf;
    }
    return "(unbekannt)";
}

void *so_load(const char *path) {
    if (g_nmods >= MAX_MODULES) { LOGE("too many modules"); return NULL; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { LOGE("open(%s): %s", path, strerror(errno)); return NULL; }
    struct stat st; fstat(fd, &st);
    size_t fsize = st.st_size;
    void *fdata = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (fdata == MAP_FAILED) { LOGE("mmap fdata failed"); return NULL; }

    Elf64_Ehdr *ehdr = fdata;
    if (memcmp(ehdr->e_ident, ELFMAG, 4) || ehdr->e_machine != EM_AARCH64) {
        LOGE("%s: not aarch64 ELF", path);
        munmap(fdata, fsize);
        return NULL;
    }

    uint64_t min_vaddr = UINT64_MAX, max_vaddr = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *p = (void *)((uintptr_t)fdata + ehdr->e_phoff + i*sizeof(*p));
        if (p->p_type != PT_LOAD) continue;
        if (p->p_vaddr < min_vaddr) min_vaddr = p->p_vaddr;
        uint64_t end = p->p_vaddr + p->p_memsz;
        if (end > max_vaddr) max_vaddr = end;
    }
    size_t span = (max_vaddr - min_vaddr + 0xFFF) & ~0xFFFULL;

    void *base = mmap(NULL, span, PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        LOGE("mmap span failed"); munmap(fdata, fsize); return NULL;
    }
    LOGI("Loaded %s at %p, span=%zu", path, base, span);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *p = (void *)((uintptr_t)fdata + ehdr->e_phoff + i*sizeof(*p));
        if (p->p_type != PT_LOAD) continue;
        void *dst = (char *)base + (p->p_vaddr - min_vaddr);
        memcpy(dst, (char *)fdata + p->p_offset, p->p_filesz);
        if (p->p_memsz > p->p_filesz)
            memset((char *)dst + p->p_filesz, 0, p->p_memsz - p->p_filesz);
        mprotect(dst, p->p_memsz, PROT_READ|PROT_WRITE|PROT_EXEC);
    }

    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *p = (void *)((uintptr_t)fdata + ehdr->e_phoff + i*sizeof(*p));
        if (p->p_type == PT_DYNAMIC) {
            dyn = (void *)((char *)base + (p->p_vaddr - min_vaddr));
            break;
        }
    }
    if (!dyn) { LOGE("no PT_DYNAMIC"); munmap(fdata, fsize); return NULL; }

    so_module *m = &g_modules[g_nmods++];
    memset(m, 0, sizeof(*m));
    m->base = base; m->size = span; m->min_vaddr = min_vaddr; m->dyn = dyn;

    size_t rela_sz = 0, rela_ent = 0, jmprel_sz = 0;
    uint32_t *gnu_hash = NULL;
    uint32_t *sysv_hash = NULL;
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB: m->symtab = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_STRTAB: m->strtab = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELA:   m->rela   = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELASZ: rela_sz   = d->d_un.d_val; break;
            case DT_RELAENT: rela_ent = d->d_un.d_val; break;
            case DT_JMPREL: m->jmprel = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_PLTRELSZ: jmprel_sz = d->d_un.d_val; break;
            case DT_INIT_ARRAY:   m->init_array = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_INIT_ARRAYSZ: m->init_count = d->d_un.d_val / sizeof(void *); break;
            case DT_INIT:         m->init_fn    = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_HASH:    sysv_hash = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_GNU_HASH: gnu_hash  = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
        }
    }

    if (rela_ent == 0) rela_ent = sizeof(Elf64_Rela);
    m->relacount = rela_sz / rela_ent;
    m->jmprelcount = jmprel_sz / sizeof(Elf64_Rela);

    if (sysv_hash) {
        m->symcount = sysv_hash[1];
    } else if (gnu_hash) {
        uint32_t nbuckets = gnu_hash[0];
        uint32_t symoffset = gnu_hash[1];
        uint32_t bloom_size = gnu_hash[2];
        uint64_t *bloom = (void *)&gnu_hash[4];
        uint32_t *buckets = (void *)&bloom[bloom_size];
        uint32_t *chain = &buckets[nbuckets];
        uint32_t max_idx = symoffset;
        for (uint32_t i = 0; i < nbuckets; i++) {
            uint32_t idx = buckets[i];
            if (idx < symoffset) continue;
            while (1) {
                uint32_t h = chain[idx - symoffset];
                if (h & 1) break;
                if (idx > max_idx) max_idx = idx;
                idx++;
            }
        }
        m->symcount = max_idx + 1;
    } else {
        m->symcount = 65536;
    }
    LOGI("  symcount=%zu rela=%zu (ent=%zu) jmprel=%zu init_array=%zu dt_init=%p",
         m->symcount, m->relacount, rela_ent, m->jmprelcount,
         m->init_count, m->init_fn);

    for (int i = 0; i < SYM_CACHE_SIZE; i++) {
        g_sym_cache[i].name = NULL;
        g_sym_cache[i].addr = NULL;
    }
    g_sym_cache_next = 0;

    if (m->rela && m->relacount)     relocate(m, m->rela, m->relacount);
    if (m->jmprel && m->jmprelcount) relocate(m, m->jmprel, m->jmprelcount);

    if (m->init_fn) {
        LOGI("=== DT_INIT @ %p ===", m->init_fn);
        m->init_fn();
        LOGI("=== DT_INIT fertig ===");
    }

    if (m->init_array && m->init_count) {
        LOGI("=== init_array: %zu Eintraege (base=%p) ===",
             m->init_count, m->base);
        for (size_t i = 0; i < m->init_count; i++) {
            void (*fn)(void) = m->init_array[i];
            uintptr_t off = (uintptr_t)fn - (uintptr_t)m->base;
            LOGI("  init_array[%zu/%zu] = %p (off 0x%lx, %s)",
                 i, m->init_count, fn, (unsigned long)off,
                 find_nearest_symbol(m, off));

            /* Top-Chunk-Heap-Check vor jedem Aufruf */
            if (!heap_sane_check("vor init_array-Aufruf")) {
                LOGE("  !!! Heap BROKEN vor init_array[%zu/%zu] !!!",
                     i, m->init_count);
                break;
            }

            if (fn) fn();
        }
        LOGI("=== init_array fertig ===");
    }

    munmap(fdata, fsize);
    return m;
}

void *so_find_addr(void *handle, const char *name) {
    so_module *m = handle;
    if (!m || !m->symtab || !m->strtab) return NULL;
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF) continue;
        if (!s->st_name) continue;
        if (strcmp(m->strtab + s->st_name, name) == 0) {
            return (char *)m->base + s->st_value;
        }
    }
    return NULL;
}

void so_flush_caches(void) { /* no-op */ }
void so_set_imports(void)  { /* no-op */ }

void so_dump_symbols(void *handle) {
    so_module *m = handle;
    if (!m || !m->symtab || !m->strtab) return;
    LOGI("--- Symbols in module @ %p (base=%p, count=%zu) ---",
         m, m->base, m->symcount);
    int shown = 0;
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF) continue;
        if (!s->st_name) continue;
        const char *name = m->strtab + s->st_name;
        if (strstr(name, "JNI_OnLoad") ||
            strstr(name, "UnityPlayer") ||
            strstr(name, "NativeLoader") ||
            strstr(name, "il2cpp_")) {
            LOGI("  %s", name);
            shown++;
        }
    }
    LOGI("--- %d interessante Symbole ---", shown);
}
