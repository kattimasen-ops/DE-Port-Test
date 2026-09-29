#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <android/log.h>

#define TAG "deadeffect-android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ================================================================
 * ANativeWindow Fake
 * ================================================================ */
typedef struct ANativeWindow {
    int width;
    int height;
    int format;
    int stride;
} ANativeWindow;

static ANativeWindow g_fake_window = { 640, 480, 1, 640 };

ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
    (void)env; (void)surface;
    LOGI("ANativeWindow_fromSurface -> fake %p (%dx%d)",
         &g_fake_window, g_fake_window.width, g_fake_window.height);
    return &g_fake_window;
}

void ANativeWindow_acquire(ANativeWindow *w) { (void)w; }
void ANativeWindow_release(ANativeWindow *w) { (void)w; }

int ANativeWindow_getWidth(ANativeWindow *w) {
    if (!w) return 640;
    return w->width;
}
int ANativeWindow_getHeight(ANativeWindow *w) {
    if (!w) return 480;
    return w->height;
}
int ANativeWindow_setBuffersGeometry(ANativeWindow *w, int width, int height, int format) {
    (void)format;
    if (w) { w->width = width; w->height = height; w->stride = width; }
    LOGI("ANativeWindow_setBuffersGeometry(%d, %d)", width, height);
    return 0;
}

/* ================================================================
 * ALooper Fake
 * ================================================================ */
void *ALooper_forThread(void)     { return (void *)0x1; }
void *ALooper_prepare(int opts)   { (void)opts; return (void *)0x1; }
void  ALooper_acquire(void *looper) { (void)looper; }
void  ALooper_release(void *looper) { (void)looper; }
int   ALooper_pollAll(int timeout, int *fd, int *events, void **data) {
    (void)timeout; (void)fd; (void)events; (void)data;
    return -1; /* ALOOPER_POLL_TIMEOUT */
}
void  ALooper_wake(void *looper) { (void)looper; }

/* ================================================================
 * ASensor Fake
 * ================================================================ */
void *ASensorManager_getInstance(void) { return (void *)0x1; }
void *ASensorManager_getDefaultSensor(void *mgr, int type) {
    (void)mgr; (void)type;
    return (void *)0x2;
}
void *ASensorManager_createEventQueue(void *mgr, void *looper,
                                      int ident, void *cb, void *data) {
    (void)mgr; (void)looper; (void)ident; (void)cb; (void)data;
    return (void *)0x3;
}
int   ASensorManager_destroyEventQueue(void *mgr, void *queue) {
    (void)mgr; (void)queue; return 0;
}
int   ASensorEventQueue_enableSensor(void *q, void *sensor) {
    (void)q; (void)sensor; return 0;
}
int   ASensorEventQueue_disableSensor(void *q, void *sensor) {
    (void)q; (void)sensor; return 0;
}
int   ASensorEventQueue_setEventRate(void *q, void *sensor, int rate) {
    (void)q; (void)sensor; (void)rate; return 0;
}
int   ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
int   ASensorEventQueue_getEvents(void *q, void *events, int count) {
    (void)q; (void)events; (void)count; return 0;
}
const char *ASensor_getName(void *sensor)       { (void)sensor; return "FakeSensor"; }
const char *ASensor_getVendor(void *sensor)     { (void)sensor; return "Fake"; }
int         ASensor_getType(void *sensor)       { (void)sensor; return 1; }
float       ASensor_getResolution(void *sensor) { (void)sensor; return 1.0f; }
int         ASensor_getMinDelay(void *sensor)   { (void)sensor; return 10000; }

/* ================================================================
 * System Properties Fake
 * ================================================================ */
int __system_property_get(const char *name, char *value) {
    (void)name;
    if (value) { value[0] = '0'; value[1] = 0; }
    return 1;
}

/* ================================================================
 * pthread_cond_timedwait Normalisierung
 *
 * Unity uebergibt absolute Zeitstempel auf Basis von CLOCK_MONOTONIC.
 * glibc erwartet standardmaessig CLOCK_REALTIME.
 * Diese Wrapper-Funktion rechnet um.
 * ================================================================ */
typedef int (*real_cond_timedwait_t)(pthread_cond_t *, pthread_mutex_t *,
                                     const struct timespec *);
static real_cond_timedwait_t g_real_cond_timedwait = NULL;

static void resolve_cond_timedwait(void) {
    if (g_real_cond_timedwait) return;
    g_real_cond_timedwait = (real_cond_timedwait_t)dlsym(RTLD_NEXT,
                                                          "pthread_cond_timedwait");
    if (!g_real_cond_timedwait) {
        LOGE("pthread_cond_timedwait: dlsym fehlgeschlagen");
    }
}

__attribute__((visibility("default")))
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    resolve_cond_timedwait();
    if (!g_real_cond_timedwait)
        return ETIMEDOUT;

    if (!abstime)
        return g_real_cond_timedwait(cond, mutex, NULL);

    /* Absolute Zeit in relative Zeit umrechnen */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Wenn abstime weit in der Zukunft liegt (> 1 Jahr),
     * behandeln wir es als CLOCK_REALTIME-Interpretation
     * und korrigieren. Andernfalls nutzen wir es direkt. */
    time_t sec_diff = abstime->tv_sec - now.tv_sec;
    if (sec_diff < 0) {
        /* Timeout bereits abgelaufen */
        return ETIMEDOUT;
    }

    struct timespec rel;
    rel.tv_sec  = abstime->tv_sec  - now.tv_sec;
    rel.tv_nsec = abstime->tv_nsec - now.tv_nsec;
    if (rel.tv_nsec < 0) {
        rel.tv_sec  -= 1;
        rel.tv_nsec += 1000000000L;
    }

    /* Umrechnung auf absolute CLOCK_REALTIME-Zeit fuer glibc */
    struct timespec real_now;
    clock_gettime(CLOCK_REALTIME, &real_now);
    struct timespec real_abs;
    real_abs.tv_sec  = real_now.tv_sec  + rel.tv_sec;
    real_abs.tv_nsec = real_now.tv_nsec + rel.tv_nsec;
    if (real_abs.tv_nsec >= 1000000000L) {
        real_abs.tv_sec  += 1;
        real_abs.tv_nsec -= 1000000000L;
    }

    return g_real_cond_timedwait(cond, mutex, &real_abs);
}

/* ================================================================
 * Fortify-Wrapper (verhindern Endlosrekursion)
 * ================================================================ */
int __fdelt_chk(int fd) {
    if (fd < 0 || fd >= 1024) return fd % 1024;
    return fd / 64;
}

/* ================================================================
 * __android_log_print ist in liblog, wir linken direkt
 * ================================================================ */
