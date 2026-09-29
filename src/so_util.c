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

#ifndef DT_RELR
#define DT_RELR 36
#define DT_RELRSZ 35
#define DT_RELRENT 37
#endif
#define DT_ANDROID_RELR 0x6fffe000
#define DT_ANDROID_RELRSZ 0x6fffe001
#define DT_ANDROID_RELRENT 0x6fffe003
#ifndef R_AARCH64_TLSDESC
#define R_AARCH64_TLSDESC 1031
#endif
#ifndef R_AARCH64_COPY
#define R_AARCH64_COPY 1024
#endif
#ifndef R_AARCH64_IRELATIVE
#define R_AARCH64_IRELATIVE 1032
#endif

#define TAG "so_util"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct {
    void *base; size_t size; uint64_t min_vaddr;
    Elf64_Dyn *dyn;
    Elf64_Sym *symtab; const char *strtab; size_t symcount;
    Elf64_Rela *rela; size_t relacount;
    Elf64_Rela *jmprel; size_t jmprelcount;
    void (**init_array)(void); size_t init_count;
    void (*init_fn)(void);
} so_module;

#define MAX_MODULES 8
static so_module g_modules[MAX_MODULES];
static int g_nmods = 0;

#define SYM_CACHE_SIZE 512
static struct { const char *name; void *addr; } g_sym_cache[SYM_CACHE_SIZE];
static int g_sym_cache_next = 0;

/* Entfernt Android-Versionssuffixe wie "@LIBC" aus Symbolnamen. */
static const char *strip_version(const char *name, char *buf, size_t sz) {
    strncpy(buf, name, sz - 1);
    buf[sz - 1] = '\0';
    char *at = strchr(buf, '@');
    if (at) *at = '\0';
    return buf;
}

static void *resolve_symbol_full(const char *name) {
    char clean[256];
    const char *lookup = strip_version(name, clean, sizeof(clean));

    for (int i = 0; i < g_nmods; i++) {
        so_module *m = &g_modules[i];
        if (!m->symtab || !m->strtab) continue;
        for (size_t j = 0; j < m->symcount; j++) {
            Elf64_Sym *s = &m->symtab[j];
            if (s->st_shndx == SHN_UNDEF) continue;
            if (!s->st_name) continue;
            if (strcmp(m->strtab + s->st_name, lookup) == 0)
                return (char *)m->base + s->st_value;
        }
    }
    /* RTLD_DEFAULT findet zuerst unsere eigenen -rdynamic-Exporte
     * (u.a. dlopen/dlsym-Hook, __android_log_print, stat, ...). */
    void *p = dlsym(RTLD_DEFAULT, lookup);
    if (p) return p;

    static void *sys_libs[12] = {0};
    static const char *sys_names[] = {
        "libc++_shared.so",
        "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
        "libEGL.so.1", "libGLESv2.so.2", "libGLESv1_CM.so.1",
        "libSDL2-2.0.so.0", "libz.so.1", "libstdc++.so.6", NULL
    };
    for (size_t i = 0; sys_names[i]; i++) {
        if (!sys_libs[i]) sys_libs[i] = dlopen(sys_names[i], RTLD_NOW | RTLD_GLOBAL);
        if (sys_libs[i]) {
            p = dlsym(sys_libs[i], lookup);
            if (p) return p;
        }
    }
    return NULL;
}

static void *resolve_symbol_cached(const char *name) {
    for (int i = 0; i < SYM_CACHE_SIZE; i++)
        if (g_sym_cache[i].name && strcmp(g_sym_cache[i].name, name) == 0)
            return g_sym_cache[i].addr;
    void *addr = resolve_symbol_full(name);
    g_sym_cache[g_sym_cache_next].name = name;
    g_sym_cache[g_sym_cache_next].addr = addr;
    g_sym_cache_next = (g_sym_cache_next + 1) % SYM_CACHE_SIZE;
    return addr;
}

/* IRELATIVE wird in einem zweiten Pass behandelt. */
static void relocate(so_module *m, Elf64_Rela *rel, size_t count) {
    for (size_t i = 0; i < count; i++) {
        Elf64_Rela *r = &rel[i];
        uint32_t type = ELF64_R_TYPE(r->r_info);
        uint32_t sym  = ELF64_R_SYM(r->r_info);
        if (r->r_offset + 16 > m->size) continue;
        uint64_t *ptr = (uint64_t *)((uintptr_t)m->base + r->r_offset);
        switch (type) {
            case R_AARCH64_NONE: break;
            case R_AARCH64_RELATIVE:
                *ptr = (uint64_t)m->base + r->r_addend; break;
            case R_AARCH64_ABS64:
            case R_AARCH64_GLOB_DAT:
            case R_AARCH64_JUMP_SLOT: {
                const char *name = (sym < m->symcount)
                    ? m->strtab + m->symtab[sym].st_name : NULL;
                if (!name || !*name) { *ptr = 0; break; }
                void *res = resolve_symbol_cached(name);
                if (!res) { LOGE("Unresolved: %s", name); *ptr = 0; }
                else *ptr = (uint64_t)res;
                break;
            }
            case R_AARCH64_COPY: {
                if (sym >= m->symcount) { *ptr = 0; break; }
                Elf64_Sym *s = &m->symtab[sym];
                void *src = resolve_symbol_cached(m->strtab + s->st_name);
                if (!src) { *ptr = 0; break; }
                size_t sz = s->st_size;
                if (sz > 0) {
                    if (r->r_offset + sz > m->size) sz = m->size - r->r_offset;
                    memcpy(ptr, src, sz);
                }
                break;
            }
            case R_AARCH64_TLSDESC: {
                static int warned = 0;
                if (!warned) {
                    LOGI("TLSDESC @ 0x%lx — setze Descriptor=0",
                         (unsigned long)r->r_offset);
                    warned = 1;
                }
                ptr[0] = 0; ptr[1] = 0; break;
            }
            case R_AARCH64_IRELATIVE:
                /* Bewusst übersprungen — zweiter Pass. */
                break;
            default:
                LOGI("Unknown reloc type %u at 0x%lx", type,
                     (unsigned long)r->r_offset);
                break;
        }
    }
}

/* Zweiter Pass: IRELATIVE-Resolvers aufrufen.
 * Muss NACH den normalen Relocations laufen, weil der Resolver
 * bereits aufgelöste Symbole (malloc, dlsym, ...) verwenden kann. */
static void apply_irelative(so_module *m, Elf64_Rela *rel, size_t count) {
    for (size_t i = 0; i < count; i++) {
        Elf64_Rela *r = &rel[i];
        uint32_t type = ELF64_R_TYPE(r->r_info);
        if (type != R_AARCH64_IRELATIVE) continue;
        if (r->r_offset + 8 > m->size) continue;
        uint64_t *ptr = (uint64_t *)((uintptr_t)m->base + r->r_offset);
        typedef void *(*resolver_t)(void);
        resolver_t res = (resolver_t)((char *)m->base + r->r_addend);
        *ptr = (uint64_t) res();
    }
}

/* Android RELR: kompakte Relocation-Kodierung. */
static void apply_relr(so_module *m, uint64_t *relr, size_t relr_sz) {
    if (!relr || !relr_sz) return;
    size_t count = relr_sz / sizeof(uint64_t);
    uintptr_t where = (uintptr_t)m->base;
    for (size_t i = 0; i < count; i++) {
        uint64_t entry = relr[i];
        if ((entry & 1) == 0) {
            where = (uintptr_t)m->base + entry;
            *(uint64_t *)where = (uint64_t)m->base + *(uint64_t *)where;
            where += sizeof(uint64_t);
        } else {
            uint64_t bitmap = entry >> 1;
            for (int j = 0; j < 63; j++) {
                if (bitmap & ((uint64_t)1 << j)) {
                    uintptr_t p = where + j * sizeof(uint64_t);
                    if ((p - (uintptr_t)m->base) + 8 <= m->size) {
                        *(uint64_t *)p = (uint64_t)m->base + *(uint64_t *)p;
                    }
                }
            }
            where += 63 * sizeof(uint64_t);
        }
    }
}

static const char *find_nearest_symbol(so_module *m, uint64_t target_off) {
    static char buf[320];
    const char *best = NULL; uint64_t best_dist = (uint64_t)-1;
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF || !s->st_name || !s->st_value) continue;
        unsigned type = ELF64_ST_TYPE(s->st_info);
        if (type != STT_FUNC && type != STT_OBJECT && type != STT_NOTYPE) continue;
        if (s->st_value <= target_off) {
            uint64_t d = target_off - s->st_value;
            if (d < best_dist) { best_dist = d; best = m->strtab + s->st_name; }
        }
    }
    if (best) { snprintf(buf, sizeof(buf), "%s+0x%lx", best,
                        (unsigned long)best_dist); return buf; }
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
        LOGE("%s: not aarch64 ELF", path); munmap(fdata, fsize); return NULL;
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
    if (base == MAP_FAILED) { munmap(fdata, fsize); return NULL; }
    LOGI("Loaded %s at %p, span=%zu", path, base, span);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *p = (void *)((uintptr_t)fdata + ehdr->e_phoff + i*sizeof(*p));
        if (p->p_type != PT_LOAD) continue;
        void *dst = (char *)base + (p->p_vaddr - min_vaddr);
        memcpy(dst, (char *)fdata + p->p_offset, p->p_filesz);
        if (p->p_memsz > p->p_filesz)
            memset((char *)dst + p->p_filesz, 0, p->p_memsz - p->p_filesz);
    }
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *p = (void *)((uintptr_t)fdata + ehdr->e_phoff + i*sizeof(*p));
        if (p->p_type == PT_DYNAMIC) {
            dyn = (void *)((char *)base + (p->p_vaddr - min_vaddr)); break;
        }
    }
    if (!dyn) { munmap(fdata, fsize); return NULL; }
    so_module *m = &g_modules[g_nmods++];
    memset(m, 0, sizeof(*m));
    m->base = base; m->size = span; m->min_vaddr = min_vaddr; m->dyn = dyn;

    size_t rela_sz = 0, rela_ent = 0, jmprel_sz = 0;
    uint64_t *relr = NULL; size_t relr_sz = 0;
    uint32_t *gnu_hash = NULL; uint32_t *sysv_hash = NULL;
    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB: m->symtab = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_STRTAB: m->strtab = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELA:   m->rela   = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELASZ: rela_sz   = d->d_un.d_val; break;
            case DT_RELAENT: rela_ent = d->d_un.d_val; break;
            case DT_JMPREL: m->jmprel = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_PLTRELSZ: jmprel_sz = d->d_un.d_val; break;
            case DT_INIT_ARRAY: m->init_array = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_INIT_ARRAYSZ: m->init_count = d->d_un.d_val / sizeof(void *); break;
            case DT_INIT: m->init_fn = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_HASH: sysv_hash = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_GNU_HASH: gnu_hash = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELR:
            case DT_ANDROID_RELR:
                relr = (void *)((char *)base + (d->d_un.d_ptr - min_vaddr)); break;
            case DT_RELRSZ:
            case DT_ANDROID_RELRSZ:
                relr_sz = d->d_un.d_val; break;
        }
    }
    if (rela_ent == 0) rela_ent = sizeof(Elf64_Rela);
    m->relacount = rela_sz / rela_ent;
    m->jmprelcount = jmprel_sz / sizeof(Elf64_Rela);

    if (sysv_hash) m->symcount = sysv_hash[1];
    else if (gnu_hash) {
        uint32_t nbuckets = gnu_hash[0], symoffset = gnu_hash[1], bloom_size = gnu_hash[2];
        uint64_t *bloom = (void *)&gnu_hash[4];
        uint32_t *buckets = (void *)&bloom[bloom_size];
        uint32_t *chain = &buckets[nbuckets];
        uint32_t max_idx = symoffset;
        for (uint32_t i = 0; i < nbuckets; i++) {
            uint32_t idx = buckets[i];
            if (idx < symoffset) continue;
            /* FIX: max_idx VOR dem Ketten-Abbruch aktualisieren,
             * sonst geht der letzte Eintrag verloren. */
            while (1) {
                if (idx > max_idx) max_idx = idx;
                uint32_t h = chain[idx - symoffset];
                if (h & 1) break;
                idx++;
            }
        }
        m->symcount = max_idx + 1;
    } else m->symcount = 65536;

    LOGI("  symcount=%zu rela=%zu jmprel=%zu init=%zu relr=%zu",
         m->symcount, m->relacount, m->jmprelcount, m->init_count, relr_sz);

    for (int i = 0; i < SYM_CACHE_SIZE; i++) {
        g_sym_cache[i].name = NULL; g_sym_cache[i].addr = NULL;
    }
    g_sym_cache_next = 0;

    if (relr && relr_sz) apply_relr(m, relr, relr_sz);
    if (m->rela && m->relacount) relocate(m, m->rela, m->relacount);
    if (m->jmprel && m->jmprelcount) relocate(m, m->jmprel, m->jmprelcount);

    /* Zweiter Pass: IRELATIVE */
    if (m->rela && m->relacount) apply_irelative(m, m->rela, m->relacount);
    if (m->jmprel && m->jmprelcount) apply_irelative(m, m->jmprel, m->jmprelcount);

    if (m->init_fn) { LOGI("=== DT_INIT ==="); m->init_fn(); }

    if (m->init_array && m->init_count) {
        LOGI("=== init_array: %zu ===", m->init_count);
        for (size_t i = 0; i < m->init_count; i++) {
            void (*fn)(void) = m->init_array[i];
            uintptr_t off = (uintptr_t)fn - (uintptr_t)m->base;
            LOGI("  [%zu/%zu] %p (off 0x%lx, %s)",
                 i, m->init_count, fn, (unsigned long)off,
                 find_nearest_symbol(m, off));
            if (fn) fn();
        }
    }
    munmap(fdata, fsize);
    return m;
}

void *so_find_addr(void *handle, const char *name) {
    so_module *m = handle;
    if (!m || !m->symtab || !m->strtab) return NULL;
    char clean[256];
    const char *lookup = strip_version(name, clean, sizeof(clean));
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF || !s->st_name) continue;
        if (strcmp(m->strtab + s->st_name, lookup) == 0)
            return (char *)m->base + s->st_value;
    }
    return NULL;
}

/* FIX: so_load() gibt &g_modules[i] zurück (den so_module*),
 * NICHT g_modules[i].base. Daher hier gegen den Modul-Zeiger
 * vergleichen, nicht gegen die mmap-Adresse. */
int so_is_our_handle(void *handle) {
    if (!handle) return 0;
    for (int i = 0; i < g_nmods; i++) {
        if ((void *)&g_modules[i] == handle) return 1;
    }
    return 0;
}

void so_flush_caches(void) { }
void so_set_imports(void)  { }

void so_dump_symbols(void *handle) {
    so_module *m = handle;
    if (!m || !m->symtab || !m->strtab) return;
    LOGI("--- Symbols @ %p (base=%p, count=%zu) ---", m, m->base, m->symcount);
    int shown = 0;
    for (size_t i = 0; i < m->symcount; i++) {
        Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx == SHN_UNDEF || !s->st_name) continue;
        const char *name = m->strtab + s->st_name;
        if (strstr(name, "JNI_OnLoad") || strstr(name, "UnityPlayer") ||
            strstr(name, "NativeLoader") || strstr(name, "il2cpp_") ||
            strstr(name, "UnityMain")) {
            LOGI("  %s", name); shown++;
        }
    }
    LOGI("--- %d interessante Symbole ---", shown);
}
