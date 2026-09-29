#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <dirent.h>
#include <android/log.h>

#undef stat
#undef lstat
#undef fstat

#define TAG "deadeffect-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

void (*g_de_crash_handler)(int, siginfo_t *, void *) = NULL;

/* ------------------------------------------------------------ */
/* Android-Logger                                                */
/* ------------------------------------------------------------ */
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    char buf[1024];
    va_list ap; int n; (void)prio;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", buf);
    fflush(stderr);
    return n;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    char buf[1024]; int n; (void)prio;
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", buf);
    fflush(stderr);
    return n;
}
int __android_log_write(int prio, const char *tag, const char *text) {
    (void)prio;
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", text ? text : "");
    fflush(stderr);
    return text ? (int)strlen(text) : 0;
}

/* ------------------------------------------------------------ */
/* sigaction / signal Intercept (mit Caller-Logging)             */
/* ------------------------------------------------------------ */
static int is_protected_signal(int sig) {
    return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
           sig == SIGFPE  || sig == SIGABRT;
}

__attribute__((visibility("default")))
int de_sigaction(int signum, const struct sigaction *act,
                 struct sigaction *oldact)
{
    void *caller = __builtin_return_address(0);
    if (is_protected_signal(signum) && act) {
        LOGI("[sigaction] BLOCKIERT signum=%d caller=%p (behalte %p)",
             signum, caller, (void *)g_de_crash_handler);
        if (oldact) {
            memset(oldact, 0, sizeof(*oldact));
            oldact->sa_sigaction = g_de_crash_handler;
            oldact->sa_flags     = SA_SIGINFO | SA_ONSTACK;
        }
        return 0;
    }
    LOGI("[sigaction] durchreichen signum=%d caller=%p", signum, caller);
    return (int)syscall(SYS_rt_sigaction, signum, act, oldact,
                        (long)sizeof(sigset_t));
}

typedef void (*sighandler_t)(int);

__attribute__((visibility("default")))
sighandler_t de_signal(int signum, sighandler_t handler) {
    void *caller = __builtin_return_address(0);
    if (is_protected_signal(signum)) {
        LOGI("[signal] BLOCKIERT signum=%d caller=%p", signum, caller);
        return SIG_DFL;
    }
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    memset(&old, 0, sizeof(old));
    sa.sa_handler = handler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (de_sigaction(signum, &sa, &old) == 0)
        return old.sa_handler;
    return SIG_ERR;
}

/* ------------------------------------------------------------ */
/* dlsym / dlopen Intercept (mit Caller)                         */
/* ------------------------------------------------------------ */
extern void *__libc_dlsym(void *, const char *);
extern void *__libc_dlopen_mode(const char *, int);

__attribute__((visibility("default")))
void *de_dlsym(void *handle, const char *symbol) {
    void *caller = __builtin_return_address(0);
    void *ret = __libc_dlsym(handle, symbol);
    LOGI("[dlsym] handle=%p sym=%s -> %p (caller=%p)",
         handle, symbol ? symbol : "?", ret, caller);
    return ret;
}

__attribute__((visibility("default")))
void *de_dlopen(const char *path, int flags) {
    void *caller = __builtin_return_address(0);
    void *ret = __libc_dlopen_mode(path, flags);
    LOGI("[dlopen] %s (flags=0x%x) -> %p (caller=%p)",
         path ? path : "?", flags, ret, caller);
    return ret;
}

/* ------------------------------------------------------------ */
/* eglGetProcAddress Intercept MIT FALLBACK-STUB                 */
/*                                                               */
/* Wenn eine GL-Funktion auf NextOS fehlt, liefert die echte     */
/* libEGL NULL. Wird dieser NULL-Zeiger später ungeprüft         */
/* aufgerufen, crasht Unity mit PC=0.                            */
/*                                                               */
/* Loesung: statt NULL wird ein generischer Stub zurueckgegeben. */
/* Der Stub loggt den Aufruf und liefert 0 in x0/v0 zurueck.     */
/* Auf ARM64 ist x0 == v0 (Rueckgaberegister fuer Integer und    */
/* Float). Alle Argumente in x0-x7 werden ignoriert, was fuer    */
/* das Register-/Stack-Layout safe ist.                          */
/* ------------------------------------------------------------ */
typedef void *(*egl_get_proc_t)(const char *);
static egl_get_proc_t g_real_egl_get_proc = NULL;
static int g_real_egl_tried = 0;

/* Ringpuffer fuer Log-Namen, damit der Stub sie mitloggen kann. */
#define EGL_STUB_SLOTS 64
static const char *g_egl_stub_name[EGL_STUB_SLOTS];
static int         g_egl_stub_slot = 0;
static int         g_egl_stub_wrap = 0;

/* Der generische Stub. Wird beim ersten Aufruf geloggt. */
static int g_stub_logged = 0;
__attribute__((noinline))
static void *de_gl_missing_stub(void) {
    if (!g_stub_logged) {
        g_stub_logged = 1;
        LOGI("[egl-stub] FEHLENDE GL-FUNKTION AUFGERUFEN — "
             "siehe vorherige eglGetProcAddress-Logs fuer den Namen");
    }
    return NULL;
}

__attribute__((visibility("default")))
void *eglGetProcAddress(const char *procname) {
    if (!g_real_egl_tried) {
        g_real_egl_tried = 1;
        g_real_egl_get_proc = (egl_get_proc_t)dlsym(RTLD_NEXT, "eglGetProcAddress");
        if (!g_real_egl_get_proc) {
            LOGE("[eglGetProcAddress] RTLD_NEXT fehlgeschlagen");
        }
    }

    void *ret = NULL;
    if (g_real_egl_get_proc) ret = g_real_egl_get_proc(procname);

    if (ret) {
        LOGI("[eglGetProcAddress] %s -> %p", procname ? procname : "?", ret);
        return ret;
    }

    /* Fallback: Namen merken und Stub zurueckgeben. */
    int slot = g_egl_stub_slot;
    g_egl_stub_name[slot] = procname ? procname : "?";
    g_egl_stub_slot = (g_egl_stub_slot + 1) % EGL_STUB_SLOTS;
    if (g_egl_stub_slot == 0) g_egl_stub_wrap = 1;

    LOGI("[eglGetProcAddress] FEHLT: %s -> STUB %p (statt NULL)",
         procname ? procname : "?", (void *)de_gl_missing_stub);
    return (void *)de_gl_missing_stub;
}

/* ------------------------------------------------------------ */
/* __sF                                                          */
/* ------------------------------------------------------------ */
void *__sF[3] = { NULL, NULL, NULL };

/* ------------------------------------------------------------ */
/* ANativeWindow                                                 */
/* ------------------------------------------------------------ */
typedef struct ANativeWindow {
    int width, height, format, stride;
} ANativeWindow;

static ANativeWindow g_fake_window = { 640, 480, 1, 640 };

ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
    (void)env; (void)surface;
    return &g_fake_window;
}
void ANativeWindow_acquire(ANativeWindow *w) { (void)w; }
void ANativeWindow_release(ANativeWindow *w) { (void)w; }
int ANativeWindow_getWidth(ANativeWindow *w)  { return w ? w->width  : 640; }
int ANativeWindow_getHeight(ANativeWindow *w) { return w ? w->height : 480; }
int ANativeWindow_setBuffersGeometry(ANativeWindow *w, int width, int height, int format) {
    (void)format;
    if (w) { w->width = width; w->height = height; w->stride = width; }
    return 0;
}

/* ------------------------------------------------------------ */
/* ALooper                                                       */
/* ------------------------------------------------------------ */
void *ALooper_forThread(void)     { return (void *)0x1; }
void *ALooper_prepare(int opts)   { (void)opts; return (void *)0x1; }
void  ALooper_acquire(void *l) { (void)l; }
void  ALooper_release(void *l) { (void)l; }
int   ALooper_pollAll(int t, int *f, int *e, void **d) {
    (void)t; (void)f; (void)e; (void)d; return -1;
}
int   ALooper_pollOnce(int t, int *f, int *e, void **d) {
    (void)t; (void)f; (void)e; (void)d; return -1;
}
int   ALooper_addFd(void *l, int fd, int i, int ev, void *cb, void *d) {
    (void)l; (void)fd; (void)i; (void)ev; (void)cb; (void)d; return 0;
}
int   ALooper_removeFd(void *l, int fd) { (void)l; (void)fd; return 0; }
void  ALooper_wake(void *l) { (void)l; }

/* ------------------------------------------------------------ */
/* ASensorManager                                                */
/* ------------------------------------------------------------ */
void *ASensorManager_getInstance(void) { return (void *)0x1; }
void *ASensorManager_getInstanceForPackage(const char *p) { (void)p; return (void *)0x1; }
void *ASensorManager_getDefaultSensor(void *m, int t) { (void)m; (void)t; return (void *)0x2; }
int   ASensorManager_getSensorList(void *m, const void **l) {
    (void)m; if (l) *l = NULL; return 0;
}
void *ASensorManager_createEventQueue(void *m, void *l, int i, void *cb, void *d) {
    (void)m; (void)l; (void)i; (void)cb; (void)d; return (void *)0x3;
}
int   ASensorManager_destroyEventQueue(void *m, void *q) { (void)m; (void)q; return 0; }
int   ASensorEventQueue_enableSensor(void *q, void *s)   { (void)q; (void)s; return 0; }
int   ASensorEventQueue_disableSensor(void *q, void *s)  { (void)q; (void)s; return 0; }
int   ASensorEventQueue_setEventRate(void *q, void *s, int r) {
    (void)q; (void)s; (void)r; return 0;
}
int   ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
int   ASensorEventQueue_getEvents(void *q, void *e, int c) {
    (void)q; (void)e; (void)c; return 0;
}
const char *ASensor_getName(void *s)       { (void)s; return "FakeSensor"; }
const char *ASensor_getVendor(void *s)     { (void)s; return "Fake"; }
int         ASensor_getType(void *s)       { (void)s; return 1; }
float       ASensor_getResolution(void *s) { (void)s; return 1.0f; }
int         ASensor_getMinDelay(void *s)   { (void)s; return 10000; }

/* ------------------------------------------------------------ */
/* System-Properties (Bionic)                                    */
/* ------------------------------------------------------------ */
typedef struct prop_info prop_info;

int __system_property_get(const char *name, char *value) {
    (void)name;
    if (value) { value[0] = '0'; value[1] = 0; }
    return 1;
}
prop_info *__system_property_find(const char *name) { (void)name; return NULL; }
int __system_property_read(const prop_info *pi, char *name, char *value) {
    (void)pi;
    if (name)  name[0]  = '\0';
    if (value) value[0] = '\0';
    return 0;
}

/* ------------------------------------------------------------ */
/* Bionic-Fortify-Wrapper                                        */
/* ------------------------------------------------------------ */
void __FD_SET_chk(int fd, void *set, size_t set_size) {
    if (!set || fd < 0 || (size_t)(fd / 8 + 1) > set_size) return;
    ((unsigned char *)set)[fd / 8] |= (unsigned char)(1u << (fd % 8));
}
int __FD_ISSET_chk(int fd, const void *set, size_t set_size) {
    if (!set || fd < 0 || (size_t)(fd / 8 + 1) > set_size) return 0;
    return (((const unsigned char *)set)[fd / 8] >> (fd % 8)) & 1;
}
void __FD_CLR_chk(int fd, void *set, size_t set_size) {
    if (!set || fd < 0 || (size_t)(fd / 8 + 1) > set_size) return;
    ((unsigned char *)set)[fd / 8] &= (unsigned char)~(1u << (fd % 8));
}
long int __fdelt_chk(long int fd) {
    if (fd < 0 || fd >= 1024) return fd % 1024;
    return fd / 64;
}

/* ------------------------------------------------------------ */
/* Bionic: __errno / strlcpy / android_set_abort_message         */
/* ------------------------------------------------------------ */
int *__errno(void) { return __errno_location(); }

size_t strlcpy(char *dst, const char *src, size_t size) {
    if (!dst) return 0;
    if (!src) { if (size) dst[0] = 0; return 0; }
    size_t n = strlen(src);
    if (size > 0) {
        size_t k = (n < size - 1) ? n : size - 1;
        memcpy(dst, src, k);
        dst[k] = '\0';
    }
    return n;
}

void android_set_abort_message(const char *msg) {
    if (msg) LOGE("android_set_abort_message: %s", msg);
}

/* ------------------------------------------------------------ */
/* glibc: stat/fstat/lstat direkter Export                       */
/* ------------------------------------------------------------ */
int stat(const char *path, struct stat *buf) {
    return (int)syscall(SYS_newfstatat, AT_FDCWD, path, buf, 0);
}
int lstat(const char *path, struct stat *buf) {
    return (int)syscall(SYS_newfstatat, AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}
int fstat(int fd, struct stat *buf) {
    return (int)syscall(SYS_fstat, fd, buf);
}

/* ------------------------------------------------------------ */
/* pthread_atfork                                                */
/* ------------------------------------------------------------ */
int pthread_atfork(void (*p)(void), void (*q)(void), void (*c)(void)) {
    (void)p; (void)q; (void)c; return 0;
}

/* ------------------------------------------------------------ */
/* pthread_create passthrough (KEIN Intercept)                   */
/* ------------------------------------------------------------ */
__attribute__((visibility("default")))
int de_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                      void *(*start_routine)(void *), void *arg)
{
    void *caller = __builtin_return_address(0);
    LOGI("[pthread_create] start_routine=%p arg=%p caller=%p",
         (void *)start_routine, arg, caller);
    if (!start_routine) {
        LOGE("[pthread_create] *** NULL start_routine — BLOCKIERT ***");
        return EINVAL;
    }
    typedef int (*real_t)(pthread_t *, const pthread_attr_t *,
                          void *(*)(void *), void *);
    real_t real = (real_t)dlsym(RTLD_NEXT, "pthread_create");
    if (!real) return EAGAIN;
    return real(thread, attr, start_routine, arg);
}

/* ------------------------------------------------------------ */
/* pthread_cond_timedwait (CLOCK_MONOTONIC -> CLOCK_REALTIME)    */
/* ------------------------------------------------------------ */
typedef int (*real_cond_timedwait_t)(pthread_cond_t *, pthread_mutex_t *,
                                     const struct timespec *);
static real_cond_timedwait_t g_real_cond_timedwait = NULL;

static void resolve_cond_timedwait(void) {
    if (g_real_cond_timedwait) return;
    g_real_cond_timedwait = (real_cond_timedwait_t)dlsym(RTLD_NEXT,
                                                          "pthread_cond_timedwait");
}
__attribute__((visibility("default")))
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    resolve_cond_timedwait();
    if (!g_real_cond_timedwait) return ETIMEDOUT;
    if (!abstime) return g_real_cond_timedwait(cond, mutex, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (abstime->tv_sec - now.tv_sec < 0) return ETIMEDOUT;
    struct timespec rel;
    rel.tv_sec  = abstime->tv_sec  - now.tv_sec;
    rel.tv_nsec = abstime->tv_nsec - now.tv_nsec;
    if (rel.tv_nsec < 0) { rel.tv_sec -= 1; rel.tv_nsec += 1000000000L; }
    struct timespec rn;
    clock_gettime(CLOCK_REALTIME, &rn);
    struct timespec ra;
    ra.tv_sec  = rn.tv_sec  + rel.tv_sec;
    ra.tv_nsec = rn.tv_nsec + rel.tv_nsec;
    if (ra.tv_nsec >= 1000000000L) { ra.tv_sec += 1; ra.tv_nsec -= 1000000000L; }
    return g_real_cond_timedwait(cond, mutex, &ra);
}
