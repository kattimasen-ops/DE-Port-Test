/* KEIN #define _GNU_SOURCE mehr — kommt aus CFLAGS (-D_GNU_SOURCE).
 * Sonst Re-Define-Warnung. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <time.h>
#include <pthread.h>
#include <link.h>
#include <elf.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <android/sensor.h>

#ifndef DEAD_EFFECT_LIBDIR
#define DEAD_EFFECT_LIBDIR "/roms/ports/DeadEffect/lib"
#endif

/* Aus so_util.h */
extern void *so_load(const char *path);
extern void *so_find_addr(void *handle, const char *name);
extern int   so_is_our_handle(void *handle);

/* ============================================================
 * Logging
 * ============================================================ */
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap); return r;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); return r;
}
int __android_log_write(int prio, const char *tag, const char *s) {
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", s ? s : ""); return 0;
}
void __android_log_assert(const char *cond, const char *tag,
                          const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[ASSERT] %s: ", tag ? tag : "?");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, " (cond: %s)\n", cond ? cond : "?");
    va_end(ap);
}
void android_set_abort_message(const char *msg) {
    fprintf(stderr, "[android_abort] %s\n", msg ? msg : "(null)");
}

/* ============================================================
 * ANativeWindow
 * ============================================================ */
extern ANativeWindow *g_android_window;
void ANativeWindow_acquire(ANativeWindow *w) { (void)w; }
void ANativeWindow_release(ANativeWindow *w) { (void)w; }
int32_t ANativeWindow_getWidth(ANativeWindow *w)  { return w ? w->width  : 640; }
int32_t ANativeWindow_getHeight(ANativeWindow *w) { return w ? w->height : 480; }
int32_t ANativeWindow_getFormat(ANativeWindow *w) { return w ? w->format : 1; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format) {
    if (w) { w->width = width; w->height = height; w->format = format; }
    return 0;
}
ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
    (void)env; (void)surface; return g_android_window;
}

/* ============================================================
 * ALooper
 * ============================================================ */
static int g_looper_dummy;
static ALooper *g_looper = (ALooper *)&g_looper_dummy;
ALooper *ALooper_prepare(int opts) { (void)opts; return g_looper; }
ALooper *ALooper_forThread(void)   { return g_looper; }
void ALooper_acquire(ALooper *l)   { (void)l; }
void ALooper_release(ALooper *l)   { (void)l; }
int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
    if (timeoutMillis > 0) usleep(timeoutMillis * 1000);
    if (outFd) *outFd = -1;
    if (outEvents) *outEvents = 0;
    if (outData) *outData = NULL;
    return ALOOPER_POLL_TIMEOUT;
}
int ALooper_pollOnce(int t, int *f, int *e, void **d) { return ALooper_pollAll(t,f,e,d); }
void ALooper_wake(ALooper *l) { (void)l; }
int ALooper_addFd(ALooper *l, int fd, int ident, int events, ALooper_callbackFunc cb, void *data) { return 1; }
int ALooper_removeFd(ALooper *l, int fd) { return 1; }

/* ============================================================
 * ASensor
 * ============================================================ */
static int g_sensor_mgr_dummy;
static ASensorManager *g_sensor_mgr = (ASensorManager *)&g_sensor_mgr_dummy;
ASensorManager *ASensorManager_getInstance(void) { return g_sensor_mgr; }
ASensorManager *ASensorManager_getInstanceForPackage(const char *p) { (void)p; return g_sensor_mgr; }
int ASensorManager_getSensorList(ASensorManager *m, ASensor const **list) { if (list) *list = NULL; return 0; }
ASensor const *ASensorManager_getDefaultSensor(ASensorManager *m, int t) { (void)m; (void)t; return NULL; }
ASensorEventQueue *ASensorManager_createEventQueue(ASensorManager *m, ALooper *l, int i, int (*cb)(int,int,void*), void *d) {
    return (ASensorEventQueue *)calloc(1, 64);
}
int ASensorManager_destroyEventQueue(ASensorManager *m, ASensorEventQueue *q) { free(q); return 0; }
int ASensorEventQueue_getEvents(ASensorEventQueue *q, ASensorEvent *e, size_t n) { return 0; }
int ASensorEventQueue_hasEvents(ASensorEventQueue *q) { return 0; }
int ASensorEventQueue_enableSensor(ASensorEventQueue *q, ASensor const *s) { return 0; }
int ASensorEventQueue_disableSensor(ASensorEventQueue *q, ASensor const *s) { return 0; }
int ASensorEventQueue_setEventRate(ASensorEventQueue *q, ASensor const *s, int32_t u) { return 0; }
int ASensor_getMinDelay(ASensor const *s) { return 0; }
int ASensor_getType(ASensor const *s) { return 0; }
const char *ASensor_getName(ASensor const *s) { return "stub"; }
const char *ASensor_getVendor(ASensor const *s) { return "linux"; }
float ASensor_getResolution(ASensor const *s) { return 1.0f; }

/* ============================================================
 * System Properties
 * ============================================================ */
static const char *prop_get(const char *name) {
    if (!name) return "";
    if (!strcmp(name, "ro.build.version.sdk"))       return "30";
    if (!strcmp(name, "ro.build.version.release"))   return "11";
    if (!strcmp(name, "ro.product.model"))           return "M9Pro";
    if (!strcmp(name, "ro.product.brand"))           return "NextOS";
    if (!strcmp(name, "ro.product.name"))            return "R36S";
    if (!strcmp(name, "ro.product.device"))          return "rk3326";
    if (!strcmp(name, "ro.product.manufacturer"))    return "Rockchip";
    if (!strcmp(name, "ro.hardware"))                return "rk3326";
    if (!strcmp(name, "ro.board.platform"))          return "rk3326";
    if (!strcmp(name, "ro.arch"))                    return "aarch64";
    if (!strcmp(name, "ro.kernel.qemu"))             return "0";
    return "";
}
int __system_property_get(const char *name, char *value) {
    const char *v = prop_get(name);
    if (value) strcpy(value, v);
    return strlen(v);
}
int __system_property_read(const void *pi, char *name, char *value) {
    if (name) name[0] = 0;
    if (value) value[0] = 0;
    return 0;
}
const void *__system_property_find(const char *name) { (void)name; return NULL; }
int __system_property_set(const char *name, const char *value) { return 0; }

/* ============================================================
 * Unity
 * ============================================================ */
int UnitySendMessage(const char *obj, const char *method, const char *msg) {
    (void)obj; (void)method; (void)msg; return 0;
}

/* FIX: __sF muss die echten stdio-Handles enthalten.
 * Bionic-Code greift z.B. über __sF[1] auf stdout zu. */
FILE *__sF[3];

__attribute__((constructor))
static void init_sF(void) {
    __sF[0] = stdin;
    __sF[1] = stdout;
    __sF[2] = stderr;
}

unsigned char _binary_classes_dex_start[1] = {0};
unsigned char _binary_classes_dex_end[1]   = {0};

/* ============================================================
 * Libc-Kompatibilitaet (Bionic != glibc)
 * ============================================================ */
#define AARCH64_SYS_newfstatat 79
#define AARCH64_SYS_fstat      80
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

int stat(const char *path, struct stat *buf) {
    return (int)syscall(AARCH64_SYS_newfstatat, AT_FDCWD, path, buf, 0);
}
int lstat(const char *path, struct stat *buf) {
    return (int)syscall(AARCH64_SYS_newfstatat, AT_FDCWD, path, buf,
                        AT_SYMLINK_NOFOLLOW);
}
int fstat(int fd, struct stat *buf) {
    return (int)syscall(AARCH64_SYS_fstat, fd, buf);
}

extern int *__errno_location(void);
int *__errno(void) { return __errno_location(); }

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t srclen = strlen(src);
    if (size > 0) {
        size_t copylen = (srclen >= size) ? size - 1 : srclen;
        memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}

void __FD_SET_chk(int fd, fd_set *set, size_t set_size) {
    (void)set_size;
    if (fd >= 0 && fd < FD_SETSIZE && set) FD_SET(fd, set);
}
int __FD_ISSET_chk(int fd, const fd_set *set, size_t set_size) {
    (void)set_size;
    if (fd >= 0 && fd < FD_SETSIZE && set) return FD_ISSET(fd, set);
    return 0;
}

/* FIX: glibc deklariert
 *   extern long int __fdelt_chk (long int __d);
 * in bits/select2.h. Rückgabetyp MUSS long int sein, sonst
 * "conflicting types"-Fehler. */
long int __fdelt_chk(long int d) {
    if (d < 0 || d >= FD_SETSIZE) return 0;
    return (long int)(d / __NFDBITS);
}

extern int __register_atfork(void (*prepare)(void),
                             void (*parent)(void),
                             void (*child)(void),
                             void *dso_handle);
int pthread_atfork(void (*prepare)(void),
                   void (*parent)(void),
                   void (*child)(void)) {
    return __register_atfork(prepare, parent, child, NULL);
}

/* ============================================================
 * dlopen/dlsym-Hook
 *
 * NIEMALS die echten dlopen/dlsym über dlopen()/dlsym() holen —
 * weder mit RTLD_NEXT noch mit dlopen("libdl.so.2") — das landet
 * wegen Symbol-Interposition bei UNS SELBST und läuft in eine
 * Endlosrekursion, bis der 8-MB-Stack voll ist (SIGSEGV vor oder
 * in main(), LR bleibt konstant in jeder Frame, FP-Schritt 0x50).
 *
 * Lösung: dl_iterate_phdr() benutzen (hooken wir nicht) und in
 * den PT_DYNAMIC-Dynsym-Tabellen der geladenen Module nach
 * "dlopen" / "dlsym" suchen. Liefert die echten glibc-Adressen.
 * ============================================================ */

static const char *g_lookup_module;
static const char *g_lookup_symbol;
static void       *g_lookup_result;

static int phdr_symbol_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (g_lookup_result) return 1;

    const char *name = info->dlpi_name;
    if (!name || !*name) return 0;              /* Hauptprogramm überspringen */
    if (!strstr(name, g_lookup_module)) return 0;

    /* PT_DYNAMIC finden */
    Elf64_Dyn *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const Elf64_Phdr *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn *)(uintptr_t)(info->dlpi_addr + ph->p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    Elf64_Sym *symtab = NULL;
    const char *strtab = NULL;
    uint32_t *sysv_hash = NULL;
    uint32_t *gnu_hash  = NULL;

    for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch ((uint32_t)d->d_tag) {
            case DT_SYMTAB:   symtab    = (void *)(uintptr_t)(info->dlpi_addr + d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab    = (void *)(uintptr_t)(info->dlpi_addr + d->d_un.d_ptr); break;
            case DT_HASH:     sysv_hash = (void *)(uintptr_t)(info->dlpi_addr + d->d_un.d_ptr); break;
            case DT_GNU_HASH: gnu_hash  = (void *)(uintptr_t)(info->dlpi_addr + d->d_un.d_ptr); break;
        }
    }
    if (!symtab || !strtab) return 0;

    size_t nsym = 0;
    if (sysv_hash) {
        nsym = sysv_hash[1];
    } else if (gnu_hash) {
        uint32_t nbuckets   = gnu_hash[0];
        uint32_t symoffset  = gnu_hash[1];
        uint32_t bloom_size = gnu_hash[2];
        uint64_t *bloom     = (void *)&gnu_hash[4];
        uint32_t *buckets   = (void *)&bloom[bloom_size];
        uint32_t *chain     = &buckets[nbuckets];
        uint32_t max_idx    = symoffset;
        for (uint32_t i = 0; i < nbuckets; i++) {
            uint32_t idx = buckets[i];
            if (idx < symoffset) continue;
            while (1) {
                if (idx > max_idx) max_idx = idx;
                uint32_t h = chain[idx - symoffset];
                if (h & 1) break;
                idx++;
            }
        }
        nsym = max_idx + 1;
    }
    if (nsym == 0) return 0;

    for (size_t i = 0; i < nsym; i++) {
        const Elf64_Sym *s = &symtab[i];
        if (s->st_shndx == SHN_UNDEF || !s->st_name) continue;
        if (strcmp(strtab + s->st_name, g_lookup_symbol) == 0) {
            g_lookup_result = (void *)(uintptr_t)(info->dlpi_addr + s->st_value);
            return 1;
        }
    }
    return 0;
}

static void *find_symbol_in_module(const char *module_substr, const char *symbol) {
    g_lookup_module = module_substr;
    g_lookup_symbol = symbol;
    g_lookup_result = NULL;
    dl_iterate_phdr(phdr_symbol_cb, NULL);
    return g_lookup_result;
}

static void *g_real_dlopen_raw = NULL;
static void *g_real_dlsym_raw   = NULL;

static void resolve_real_dl_functions(void) {
    if (g_real_dlopen_raw && g_real_dlsym_raw) return;

    if (!g_real_dlopen_raw)
        g_real_dlopen_raw = find_symbol_in_module("libc.so.6",  "dlopen");
    if (!g_real_dlopen_raw)
        g_real_dlopen_raw = find_symbol_in_module("libdl.so.2", "dlopen");

    if (!g_real_dlsym_raw)
        g_real_dlsym_raw = find_symbol_in_module("libc.so.6",  "dlsym");
    if (!g_real_dlsym_raw)
        g_real_dlsym_raw = find_symbol_in_module("libdl.so.2", "dlsym");

    /* Nur einmal pro Prozess ausgeben — reines Debug, kein Risiko. */
    static int logged = 0;
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[dl-hook] real_dlopen=%p real_dlsym=%p\n",
                g_real_dlopen_raw, g_real_dlsym_raw);
    }
}

void *dlopen(const char *name, int flags) {
    resolve_real_dl_functions();

    /* Unity ruft dlopen("libil2cpp.so") und dlopen("libunity.so")
     * selbst auf, nachdem NativeLoader.load gelaufen ist. Wir
     * fangen das ab und liefern die von so_load erzeugten Handles
     * zurück (damit unser ELF-Loader die Kontrolle behält). */
    if (name && strstr(name, "libil2cpp.so")) {
        static void *cached = NULL;
        if (!cached) {
            cached = so_load(name);
            if (!cached) {
                char full[512];
                snprintf(full, sizeof(full), "%s/libil2cpp.so",
                         DEAD_EFFECT_LIBDIR);
                cached = so_load(full);
            }
            fprintf(stderr, "[dlopen-hook] libil2cpp.so -> %p\n", cached);
        }
        return cached;
    }
    if (name && strstr(name, "libunity.so")) {
        static void *cached = NULL;
        if (!cached) {
            cached = so_load(name);
            if (!cached) {
                char full[512];
                snprintf(full, sizeof(full), "%s/libunity.so",
                         DEAD_EFFECT_LIBDIR);
                cached = so_load(full);
            }
            fprintf(stderr, "[dlopen-hook] libunity.so -> %p\n", cached);
        }
        return cached;
    }

    if (!g_real_dlopen_raw) return NULL;
    return ((void *(*)(const char *, int))g_real_dlopen_raw)(name, flags);
}

void *dlsym(void *handle, const char *name) {
    resolve_real_dl_functions();

    if (so_is_our_handle(handle)) {
        return so_find_addr(handle, name);
    }
    if (!g_real_dlsym_raw) return NULL;
    return ((void *(*)(void *, const char *))g_real_dlsym_raw)(handle, name);
}

/* ============================================================
 * Fortify (_chk) Wrapper
 * ============================================================ */
void *__memcpy_chk(void *dst, const void *src, size_t n, size_t dstlen) {
    (void)dstlen; return memcpy(dst, src, n);
}
void *__memmove_chk(void *dst, const void *src, size_t n, size_t dstlen) {
    (void)dstlen; return memmove(dst, src, n);
}
void *__memset_chk(void *dst, int c, size_t n, size_t dstlen) {
    (void)dstlen; return memset(dst, c, n);
}
char *__strcpy_chk(char *dst, const char *src, size_t dstlen) {
    (void)dstlen; return strcpy(dst, src);
}
char *__strncpy_chk(char *dst, const char *src, size_t n, size_t dstlen) {
    (void)dstlen; return strncpy(dst, src, n);
}
char *__strncpy_chk2(char *dst, const char *src, size_t n,
                     size_t dstlen, size_t srclen) {
    (void)dstlen; (void)srclen; return strncpy(dst, src, n);
}
char *__strcat_chk(char *dst, const char *src, size_t dstlen) {
    (void)dstlen; return strcat(dst, src);
}
char *__strncat_chk(char *dst, const char *src, size_t n, size_t dstlen) {
    (void)dstlen; return strncat(dst, src, n);
}
size_t __strlen_chk(const char *s, size_t slen) {
    (void)slen; return strlen(s);
}
char *__strchr_chk(const char *s, int c, size_t slen) {
    (void)slen; return strchr(s, c);
}
char *__strrchr_chk(const char *s, int c, size_t slen) {
    (void)slen; return strrchr(s, c);
}
int __snprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                   const char *fmt, ...) {
    (void)flag; (void)slen;
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(s, maxlen, fmt, ap);
    va_end(ap); return r;
}
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                    const char *fmt, va_list ap) {
    (void)flag; (void)slen;
    return vsnprintf(s, maxlen, fmt, ap);
}
int __sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...) {
    (void)flag; (void)slen;
    va_list ap; va_start(ap, fmt);
    int r = vsprintf(s, fmt, ap);
    va_end(ap); return r;
}
int __vsprintf_chk(char *s, int flag, size_t slen,
                   const char *fmt, va_list ap) {
    (void)flag; (void)slen;
    return vsprintf(s, fmt, ap);
}
int __printf_chk(int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap); return r;
}
int __fprintf_chk(FILE *stream, int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stream, fmt, ap);
    va_end(ap); return r;
}
int __vfprintf_chk(FILE *stream, int flag, const char *fmt, va_list ap) {
    (void)flag;
    return vfprintf(stream, fmt, ap);
}