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

/* Vom Loader gesetzt: Adresse unseres echten Crash-Handlers */
void (*g_de_crash_handler)(int, siginfo_t *, void *) = NULL;

/* ------------------------------------------------------------ */
/* Android-Logger                                                */
/* ------------------------------------------------------------ */
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    int n;
    (void)prio;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", buf);
    fflush(stderr);
    return n;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    char buf[1024];
    int n; (void)prio;
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
/* Intercept: sigaction / signal                                 */
/*                                                               */
/* libunity installiert beim ersten Engine-Zugriff einen eigenen */
/* SIGSEGV-Handler und killt damit unseren. Wir fangen das ab.   */
/* ------------------------------------------------------------ */
static int is_protected_signal(int sig) {
    return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
           sig == SIGFPE  || sig == SIGABRT;
}

__attribute__((visibility("default")))
int de_sigaction(int signum, const struct sigaction *act,
                 struct sigaction *oldact)
{
    if (is_protected_signal(signum) && act) {
        void *want = (act->sa_flags & SA_SIGINFO)
                     ? (void *)act->sa_sigaction
                     : (void *)act->sa_handler;

        if (want == (void *)g_de_crash_handler ||
            want == (void *)SIG_DFL ||
            want == (void *)SIG_IGN)
        {
            /* OK, durchlassen — das ist unser eigener Aufruf */
        } else {
            /* libunity will unseren Handler ersetzen -> blockieren */
            LOGI("[sigaction] BLOCKIERT signum=%d handler=%p (wir behalten %p)",
                 signum, want, (void *)g_de_crash_handler);
            if (oldact) {
                memset(oldact, 0, sizeof(*oldact));
                oldact->sa_sigaction = g_de_crash_handler;
                oldact->sa_flags = SA_SIGINFO | SA_ONSTACK;
            }
            return 0;
        }
    }
    /* an libc durchreichen — direkt per Syscall, damit wir uns nicht selbst rufen */
    return (int)syscall(SYS_rt_sigaction, signum, act, oldact,
                        (long)sizeof(sigset_t));
}

typedef void (*sighandler_t)(int);

__attribute__((visibility("default")))
sighandler_t de_signal(int signum, sighandler_t handler) {
    if (is_protected_signal(signum)) {
        if (handler == (sighandler_t)g_de_crash_handler ||
            handler == SIG_DFL || handler == SIG_IGN)
        {
            /* durchlassen */
        } else {
            LOGI("[signal] BLOCKIERT signum=%d handler=%p",
                 signum, (void *)handler);
            return SIG_DFL;
        }
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
