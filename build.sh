#!/bin/bash
# ============================================================
# Dead Effect Port - Unity-IL2CPP Loader
# Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)
#
# Kombiniert: Chrono (so_util) + Phigros NX (Unity JNI)
#             + FalsoJNI (Fake-JVM)
#
# ROBUSTHEITS-REGELN (lehren aus dem letzten Build):
#   1. NIEMALS `set -e` / `set +e` – auch nicht temporär.
#   2. Platzhalter für ALLE finalen Artefakte ganz oben anlegen.
#   3. Compile/Link laufen in Subshells; Fehler propagieren nicht.
#   4. WORK liegt INNERHALB von $GITHUB_WORKSPACE.
# ============================================================

export DEBIAN_FRONTEND=noninteractive

# ---- Output-Verzeichnisse --------------------------------
if [ -n "${GITHUB_WORKSPACE:-}" ]; then
    OUT="$GITHUB_WORKSPACE"
else
    OUT="$PWD"
fi
WORK="${WORK:-$OUT/work}"
mkdir -p "$OUT" "$WORK"

BUILD_LOG="$WORK/build.log"
SYMS_FILE="$WORK/undefined_symbols.txt"
JNI_META="$WORK/jni_meta"
SRC="$WORK/loader_src"
ZIP_FILE="$OUT/DeadEffect-Port.zip"

# ---- PLATZHALTER: immer vorhanden, egal wo das Skript stirbt ----
: > "$BUILD_LOG"
: > "$SYMS_FILE"
mkdir -p "$JNI_META"
echo "Build gestartet: $(date -u)" > "$SYMS_FILE"

# ---- Log-Duplikation (kein exec-redirect in Subshell nötig) ----
# Wir schreiben in eine Funktion, die wir überall aufrufen.
log() { echo "$@" | tee -a "$BUILD_LOG" >&2; }

log "==> GITHUB_WORKSPACE = $OUT"
log "==> WORK             = $WORK"
log "==> PWD              = $PWD"
log "==> Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)"

# ------------------------------------------------------------
# 1. Multiarch + apt-Quellen
# ------------------------------------------------------------
dpkg --add-architecture arm64 2>/dev/null || true

cat > /etc/apt/sources.list.d/amd64.list <<'EOF'
deb [arch=amd64] http://archive.ubuntu.com/ubuntu focal main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu focal-updates main restricted universe multiverse
deb [arch=amd64] http://security.ubuntu.com/ubuntu focal-security main restricted universe multiverse
EOF

cat > /etc/apt/sources.list.d/arm64.list <<'EOF'
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-security main restricted universe multiverse
EOF

rm -f /etc/apt/sources.list
apt-get update 2>&1 | tee -a "$BUILD_LOG" || log "[WARN] apt-get update teilweise fehlgeschlagen"

# ------------------------------------------------------------
# 2. Cross-Toolchain + Bibliotheken
# ------------------------------------------------------------
log "==> Installiere Cross-Toolchain"
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git pkg-config ca-certificates \
  wget curl file zip unzip python3 patchelf \
  crossbuild-essential-arm64 \
  libasound2-dev:arm64 \
  libegl1-mesa-dev:arm64 libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 libgbm-dev:arm64 \
  libfreetype6-dev:arm64 libsdl2-dev:arm64 \
  libsdl2-image-dev:arm64 \
  zlib1g-dev:arm64 \
  2>&1 | tee -a "$BUILD_LOG" || log "[WARN] Einige Pakete konnten nicht installiert werden"

aarch64-linux-gnu-gcc --version 2>&1 | head -1 | tee -a "$BUILD_LOG" || log "[WARN] Cross-GCC fehlt"

# ------------------------------------------------------------
# 3. Compat-Header
# ------------------------------------------------------------
log "==> Erstelle Compat-Header"
mkdir -p /usr/include/android

cat > /usr/include/android/log.h <<'EOF'
#ifndef COMPAT_ANDROID_LOG_H
#define COMPAT_ANDROID_LOG_H
#include <stdio.h>
#include <stdarg.h>
#define ANDROID_LOG_UNKNOWN 0
#define ANDROID_LOG_DEFAULT 1
#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6
#define ANDROID_LOG_FATAL   7
#define ANDROID_LOG_SILENT  8
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap);
int __android_log_write(int prio, const char *tag, const char *text);
#endif
EOF

cat > /usr/include/android/native_window.h <<'EOF'
#ifndef COMPAT_ANATIVE_WINDOW_H
#define COMPAT_ANATIVE_WINDOW_H
#include <stdint.h>
typedef struct ANativeWindow ANativeWindow;
struct ANativeWindow {
    int32_t width; int32_t height; int32_t format; void *user_data;
};
void ANativeWindow_acquire(ANativeWindow *w);
void ANativeWindow_release(ANativeWindow *w);
int32_t ANativeWindow_getWidth(ANativeWindow *w);
int32_t ANativeWindow_getHeight(ANativeWindow *w);
int32_t ANativeWindow_getFormat(ANativeWindow *w);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format);
#endif
EOF

cat > /usr/include/android/looper.h <<'EOF'
#ifndef COMPAT_ALOOPER_H
#define COMPAT_ALOOPER_H
#include <stdint.h>
typedef struct ALooper ALooper;
typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);
enum { ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1 };
enum { ALOOPER_POLL_WAKE = -1, ALOOPER_POLL_CALLBACK = -2, ALOOPER_POLL_TIMEOUT = -3, ALOOPER_POLL_ERROR = -4 };
enum { ALOOPER_EVENT_INPUT = 1, ALOOPER_EVENT_OUTPUT = 2, ALOOPER_EVENT_ERROR = 4 };
ALooper *ALooper_prepare(int opts);
ALooper *ALooper_forThread(void);
void ALooper_acquire(ALooper *l);
void ALooper_release(ALooper *l);
int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData);
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData);
void ALooper_wake(ALooper *l);
int ALooper_addFd(ALooper *l, int fd, int ident, int events, ALooper_callbackFunc cb, void *data);
int ALooper_removeFd(ALooper *l, int fd);
#endif
EOF

cat > /usr/include/android/sensor.h <<'EOF'
#ifndef COMPAT_ASENSOR_H
#define COMPAT_ASENSOR_H
#include <stdint.h>
#include <android/looper.h>
typedef struct ASensorManager ASensorManager;
typedef struct ASensorEventQueue ASensorEventQueue;
typedef struct ASensor ASensor;
typedef struct ASensorEvent {
    int32_t version; int32_t sensor; int32_t type; int32_t reserved0;
    int64_t timestamp; union { float data[16]; };
    uint32_t flags; int32_t reserved1[3];
} ASensorEvent;
enum { ASENSOR_TYPE_ACCELEROMETER = 1, ASENSOR_TYPE_MAGNETIC_FIELD = 2, ASENSOR_TYPE_GYROSCOPE = 4 };
ASensorManager *ASensorManager_getInstance(void);
ASensorManager *ASensorManager_getInstanceForPackage(const char *pkg);
int ASensorManager_getSensorList(ASensorManager *m, ASensor const **list);
ASensor const *ASensorManager_getDefaultSensor(ASensorManager *m, int type);
ASensorEventQueue *ASensorManager_createEventQueue(ASensorManager *m, ALooper *l, int ident, int (*cb)(int, int, void*), void *data);
int ASensorManager_destroyEventQueue(ASensorManager *m, ASensorEventQueue *q);
int ASensorEventQueue_getEvents(ASensorEventQueue *q, ASensorEvent *events, size_t count);
int ASensorEventQueue_hasEvents(ASensorEventQueue *q);
int ASensorEventQueue_enableSensor(ASensorEventQueue *q, ASensor const *s);
int ASensorEventQueue_disableSensor(ASensorEventQueue *q, ASensor const *s);
int ASensorEventQueue_setEventRate(ASensorEventQueue *q, ASensor const *s, int32_t usec);
int ASensor_getMinDelay(ASensor const *s);
int ASensor_getType(ASensor const *s);
const char *ASensor_getName(ASensor const *s);
const char *ASensor_getVendor(ASensor const *s);
float ASensor_getResolution(ASensor const *s);
#endif
EOF

mkdir -p /usr/include/switch_compat
cat > /usr/include/switch_compat/switch.h <<'EOF'
#ifndef COMPAT_SWITCH_H
#define COMPAT_SWITCH_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
typedef int Result;
typedef uint64_t Handle;
#define R_FAILED(rc)  ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
static inline void armDCacheFlush(void *a, size_t s) { __builtin___clear_cache((char*)a,(char*)a+s); }
static inline void armICacheInvalidate(void *a, size_t s) { __builtin___clear_cache((char*)a,(char*)a+s); }
static inline Result svcMapProcessCodeMemory(Handle h, uint64_t d, uint64_t s, uint64_t sz) {
    return mprotect((void*)d, sz, PROT_READ|PROT_EXEC) == 0 ? 0 : 1;
}
static inline Result svcSetProcessMemoryPermission(Handle h, uint64_t a, uint64_t sz, int p) {
    return mprotect((void*)a, sz, p) == 0 ? 0 : 1;
}
static inline Handle envGetOwnProcessHandle(void) { return 0; }
static inline void svcSleepThread(uint64_t ns) { usleep(ns/1000); }
static inline int appletGetOperationMode(void) { return 0; }
#define AppletOperationMode_Console 0
#define AppletOperationMode_Handheld 1
static inline void fatal_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(stderr, "FATAL: ");
    vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap); exit(1);
}
#endif
EOF

log "[OK] Compat-Header geschrieben"

# ------------------------------------------------------------
# 4. Dead Effect .so-Dateien
# ------------------------------------------------------------
log "==> Lade Dead Effect native libs"
mkdir -p "$WORK/de_libs"
if [ -n "${DE_LIBS_URL:-}" ]; then
    wget -q -O "$WORK/arm64-v8a.zip" "$DE_LIBS_URL" 2>>"$BUILD_LOG" || log "[WARN] wget fehlgeschlagen"
    unzip -o "$WORK/arm64-v8a.zip" -d "$WORK/de_libs/" >/dev/null 2>&1 || log "[WARN] unzip fehlgeschlagen"
else
    log "[WARN] DE_LIBS_URL nicht gesetzt – erwarte .so in de_libs/arm64-v8a/"
fi

DE_MAIN="$WORK/de_libs/arm64-v8a/libmain.so"
DE_UNITY="$WORK/de_libs/arm64-v8a/libunity.so"
DE_IL2CPP="$WORK/de_libs/arm64-v8a/libil2cpp.so"

for f in "$DE_MAIN" "$DE_UNITY" "$DE_IL2CPP"; do
    if [ -f "$f" ]; then
        log "  $(basename "$f"): $(stat -c%s "$f") bytes"
    else
        log "  [WARN] $f fehlt"
    fi
done

# ------------------------------------------------------------
# 5. JNI-Metadaten
# ------------------------------------------------------------
log "==> Extrahiere JNI-Metadaten aus Dead Effect"
mkdir -p "$JNI_META"

if [ -f "$DE_MAIN" ]; then
    strings -a "$DE_MAIN" 2>/dev/null | grep -E '^(com|android|java|org)/' | sort -u > "$JNI_META/classes_main.txt" || true
    strings -a "$DE_MAIN" 2>/dev/null | grep -iE 'NativeLoader|bulkypix|deadeffect' | sort -u > "$JNI_META/nativeloader_hints.txt" || true
    strings -a "$DE_MAIN" 2>/dev/null | grep -E '^\(.*\)' | sort -u > "$JNI_META/signatures_main.txt" || true
fi
if [ -f "$DE_UNITY" ]; then
    strings -a "$DE_UNITY" 2>/dev/null | grep -E '^(com|android|java|org)/' | sort -u > "$JNI_META/classes_unity.txt" || true
    strings -a "$DE_UNITY" 2>/dev/null | grep -E '^com/unity3d/player' | sort -u > "$JNI_META/unity_classes.txt" || true
fi

log "--- NativeLoader-Kandidaten ---"
cat "$JNI_META/nativeloader_hints.txt" 2>/dev/null | tee -a "$BUILD_LOG" || true
log "--- Unity-Player-Klassen ---"
cat "$JNI_META/unity_classes.txt" 2>/dev/null | tee -a "$BUILD_LOG" || true

PKG_NAME=$(strings -a "$DE_MAIN" 2>/dev/null | grep -oE 'com/[a-z]+/[a-z]+' | head -1 | tr '/' '.' || true)
[ -z "$PKG_NAME" ] && PKG_NAME="com.bulkypix.deadeffect"
log "==> Erkannter Package-Name: $PKG_NAME"

# ------------------------------------------------------------
# 6. Quellen klonen
# ------------------------------------------------------------
log "==> Klone Basis-Quellen"
cd "$WORK"
[ -d chrono-src ]   || git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src   2>>"$BUILD_LOG" || log "[WARN] Chrono"
[ -d falsjni-src ]  || git clone --depth=1 https://github.com/Rinnegatamante/FalsoJNI.git falsjni-src      2>>"$BUILD_LOG" || log "[WARN] FalsoJNI"
[ -d phigros-src ]  || git clone --depth=1 https://github.com/ChanseyIsTheBest/phigros_nx.git phigros-src  2>>"$BUILD_LOG" || log "[WARN] Phigros"
[ -d soloader-src ] || git clone --depth=1 https://github.com/v-atamanenko/soloader-boilerplate.git soloader-src 2>>"$BUILD_LOG" || log "[WARN] Soloader"

# ------------------------------------------------------------
# 7. Loader-Quellen zusammenstellen
# ------------------------------------------------------------
log "==> Stelle Loader-Quellen zusammen"
rm -rf "$SRC"
mkdir -p "$SRC"

# so_util
if [ -f "$WORK/chrono-src/so_util.c" ]; then
    cp "$WORK/chrono-src/so_util.c" "$WORK/chrono-src/so_util.h" "$SRC/" 2>/dev/null || true
    cp "$WORK/chrono-src/imports.c" "$WORK/chrono-src/imports.h" "$SRC/" 2>/dev/null || true
    log "  [OK] so_util aus Chrono"
elif [ -f "$WORK/soloader-src/source/so_util.c" ]; then
    cp "$WORK/soloader-src/source/so_util."* "$SRC/" 2>/dev/null || true
    log "  [OK] so_util aus soloader-boilerplate"
else
    log "  [WARN] Keine so_util-Quelle gefunden"
fi

# FalsoJNI
if [ -d "$WORK/falsjni-src/src" ]; then
    cp "$WORK/falsjni-src/src/"*.c "$WORK/falsjni-src/src/"*.h "$SRC/" 2>/dev/null || true
    log "  [OK] FalsoJNI kopiert"
fi

# OpenSL
if [ -f "$WORK/chrono-src/opensles_shim.c" ]; then
    cp "$WORK/chrono-src/opensles_shim.c" "$WORK/chrono-src/opensles_shim.h" "$SRC/" 2>/dev/null || true
    log "  [OK] OpenSL-Shim aus Chrono"
fi

# Phigros Unity-NDK
if [ -f "$WORK/phigros-src/source/android_native_unity.c" ]; then
    cp "$WORK/phigros-src/source/android_native_unity.c" "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i '/#include <switch/d' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i '/#include <switch\//d' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i 's/padInitializeDefault(.*);/\/* compat *\//g' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i 's/hidScanInput();/\/* compat *\//g' "$SRC/unity_ndk.c" 2>/dev/null || true
    log "  [OK] Unity-NDK aus Phigros portiert"
fi

if [ -f "$WORK/phigros-src/source/unity_jni.c" ]; then
    cp "$WORK/phigros-src/source/unity_jni.c" "$SRC/" 2>/dev/null || true
    sed -i '/#include <switch/d' "$SRC/unity_jni.c" 2>/dev/null || true
    log "  [OK] unity_jni.c kopiert"
fi

# ---- android_shim.c ----
cat > "$SRC/android_shim.c" <<'SHIMEOF'
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
int32_t ANativeWindow_getWidth(ANativeWindow *w)  { return w ? w->width  : 0; }
int32_t ANativeWindow_getHeight(ANativeWindow *w) { return w ? w->height : 0; }
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
SHIMEOF

# ---- loader_main.c ----
cat > "$SRC/loader_main.c" <<'MAINEOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include "so_util.h"
#include "jni_shim.h"

#define TAG "deadeffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef DEAD_EFFECT_LIBDIR
#define DEAD_EFFECT_LIBDIR "/roms/ports/DeadEffect/lib"
#endif
#ifndef DEAD_EFFECT_ASSETS
#define DEAD_EFFECT_ASSETS "/roms/ports/DeadEffect/assets"
#endif

ANativeWindow *g_android_window = NULL;

static void *libmain_h  = NULL;
static void *libunity_h = NULL;
static void *libil2cpp_h = NULL;

typedef unsigned int (*JNI_OnLoad_t)(void *, void *);
typedef void  (*UnityPla
