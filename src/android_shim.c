#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <android/sensor.h>

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

static ALooper g_looper;
ALooper *ALooper_prepare(int opts) { (void)opts; return &g_looper; }
ALooper *ALooper_forThread(void)   { return &g_looper; }
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

static ASensorManager g_sensor_mgr;
ASensorManager *ASensorManager_getInstance(void) { return &g_sensor_mgr; }
ASensorManager *ASensorManager_getInstanceForPackage(const char *p) { (void)p; return &g_sensor_mgr; }
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
