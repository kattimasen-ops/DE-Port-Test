#!/bin/bash
# ============================================================
# Dead Effect Port - Unity-IL2CPP Loader
# Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)
#
# Kombiniert:
#   - Chrono Trigger Port (so_util, Linux-nativ)
#   - Phigros NX (Unity JNI + NDK-Shims)
#   - FalsoJNI (Fake-JVM)
#
# NEU ggue. alter Version:
#   - JNI-Metadaten-Extraktion aus DE-.so-Dateien
#   - Android-Property-Shim (__system_property_read/find)
#   - OpenSL-ES-Shim aus Phigros
#   - OBB-Handling im Launcher
#   - Capability-Report (nxcompat-Äquivalent)
#   - Symbol-Resolution-Check vor dem Linken
# ============================================================
set -e

export DEBIAN_FRONTEND=noninteractive
WORK=/work
OUT=/out
mkdir -p "$OUT" "$WORK"
cd "$WORK"

echo "==> Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)"

# ------------------------------------------------------------
# 1. Multiarch + apt-Quellen (unverändert)
# ------------------------------------------------------------
dpkg --add-architecture arm64

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
apt-get update

# ------------------------------------------------------------
# 2. Cross-Toolchain + Bibliotheken
# ------------------------------------------------------------
echo "==> Installiere Cross-Toolchain"
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git pkg-config ca-certificates \
  wget curl file zip unzip python3 patchelf \
  crossbuild-essential-arm64 \
  libasound2-dev:arm64 \
  libegl1-mesa-dev:arm64 libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 libgbm-dev:arm64 \
  libfreetype6-dev:arm64 libsdl2-dev:arm64 \
  libsdl2-image-dev:arm64 \
  zlib1g-dev:arm64

aarch64-linux-gnu-gcc --version | head -1

# ------------------------------------------------------------
# 3. Compat-Header (Linux <-> Android <-> Switch)
# ------------------------------------------------------------
echo "==> Erstelle Compat-Header"

mkdir -p /usr/include/android

# android/log.h
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

# android/native_window.h, looper.h, sensor.h – werden im Shim
# implementiert, hier nur Typdefinitionen
cat > /usr/include/android/native_window.h <<'EOF'
#ifndef COMPAT_ANATIVE_WINDOW_H
#define COMPAT_ANATIVE_WINDOW_H
#include <stdint.h>
typedef struct ANativeWindow ANativeWindow;
struct ANativeWindow {
    int32_t width;
    int32_t height;
    int32_t format;
    void *user_data;
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
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union { float data[16]; };
    uint32_t flags;
    int32_t reserved1[3];
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

# Switch-Compat (nur für Phigros-Code)
mkdir -p /usr/include/switch_compat
cat > /usr/include/switch_compat/switch.h <<'EOF'
#ifndef COMPAT_SWITCH_H
#define COMPAT_SWITCH_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

# ------------------------------------------------------------
# 4. Dead Effect .so-Dateien holen
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
mkdir -p de_libs
if [ -n "$DE_LIBS_URL" ]; then
    wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
    unzip -o arm64-v8a.zip -d de_libs/
else
    echo "[WARN] DE_LIBS_URL nicht gesetzt – erwarte .so in de_libs/arm64-v8a/"
fi

DE_MAIN=de_libs/arm64-v8a/libmain.so
DE_UNITY=de_libs/arm64-v8a/libunity.so
DE_IL2CPP=de_libs/arm64-v8a/libil2cpp.so

for f in "$DE_MAIN" "$DE_UNITY" "$DE_IL2CPP"; do
    [ -f "$f" ] || { echo "[FEHLER] $f fehlt"; exit 1; }
    echo "  $(basename $f): $(stat -c%s $f) bytes"
done

# ------------------------------------------------------------
# 5. JNI-Metadaten aus Dead Effect extrahieren
# ------------------------------------------------------------
echo "==> Extrahiere JNI-Metadaten aus Dead Effect"

mkdir -p jni_meta

# Alle JNI-Klassennamen (Java-style: com/... oder android/...)
strings -a "$DE_MAIN" | grep -E '^(com|android|java|org)/' | sort -u > jni_meta/classes_main.txt
strings -a "$DE_UNITY" | grep -E '^(com|android|java|org)/' | sort -u > jni_meta/classes_unity.txt

# Speziell: NativeLoader-Klasse
strings -a "$DE_MAIN" | grep -iE 'NativeLoader|bulkypix|deadeffect' | sort -u > jni_meta/nativeloader_hints.txt

# Unity-spezifische Klassen in libunity
strings -a "$DE_UNITY" | grep -E '^com/unity3d/player' | sort -u > jni_meta/unity_classes.txt

# Methodensignaturen (RegisterNatives-Paare)
strings -a "$DE_MAIN" | grep -E '^\(.*\)' | sort -u > jni_meta/signatures_main.txt

echo "--- NativeLoader-Kandidaten ---"
cat jni_meta/nativeloader_hints.txt
echo "--- Unity-Player-Klassen ---"
cat jni_meta/unity_classes.txt

# Automatisch Package-Name ermitteln (muss ggf. manuell korrigiert werden)
PKG_NAME=$(strings -a "$DE_MAIN" | grep -oE 'com/[a-z]+/[a-z]+' | head -1 | tr '/' '.')
[ -z "$PKG_NAME" ] && PKG_NAME="com.bulkypix.deadeffect"
echo "==> Erkannter Package-Name: $PKG_NAME"

# ------------------------------------------------------------
# 6. Quellen klonen
# ------------------------------------------------------------
echo "==> Klone Basis-Quellen"

# 6a. Chrono Trigger Port (Linux-nativ, so_util + JNI-Shim)
if [ ! -d chrono-src ]; then
    git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src || \
    echo "[WARN] Chrono konnte nicht geklont werden – verwende Fallback"
fi

# 6b. FalsoJNI (Zero-dep Fake-JVM)
if [ ! -d falsjni-src ]; then
    git clone --depth=1 https://github.com/Rinnegatamante/FalsoJNI.git falsjni-src || \
    echo "[WARN] FalsoJNI konnte nicht geklont werden"
fi

# 6c. Phigros NX (Unity JNI + NDK-Shims) – nur bestimmte Dateien
if [ ! -d phigros-src ]; then
    git clone --depth=1 https://github.com/ChanseyIsTheBest/phigros_nx.git phigros-src || \
    echo "[WARN] Phigros NX konnte nicht geklont werden"
fi

# 6d. Soloader-Boilerplate (TheFloW's so_util, FalsoJNI)
if [ ! -d soloader-src ]; then
    git clone --depth=1 https://github.com/v-atamanenko/soloader-boilerplate.git soloader-src || \
    echo "[WARN] Soloader-Boilerplate konnte nicht geklont werden"
fi

# ------------------------------------------------------------
# 7. Loader-Quellen zusammenstellen
# ------------------------------------------------------------
echo "==> Stelle Loader-Quellen zusammen"

SRC=$WORK/loader_src
rm -rf "$SRC"
mkdir -p "$SRC"

# 7a. so_util aus Chrono (oder Fallback soloader)
if [ -f chrono-src/so_util.c ]; then
    cp chrono-src/so_util.c chrono-src/so_util.h "$SRC/"
    cp chrono-src/imports.c chrono-src/imports.h "$SRC/" 2>/dev/null || true
    echo "  [OK] so_util aus Chrono"
elif [ -f soloader-src/source/so_util.c ]; then
    cp soloader-src/source/so_util.* "$SRC/"
    echo "  [OK] so_util aus soloader-boilerplate"
fi

# 7b. JNI-Shim aus FalsoJNI (sauberer als Phigros)
if [ -d falsjni-src/src ]; then
    cp falsjni-src/src/*.c falsjni-src/src/*.h "$SRC/" 2>/dev/null || true
    echo "  [OK] FalsoJNI kopiert"
fi

# 7c. OpenSL-ES-Shim aus Chrono (Linux-nativ)
if [ -f chrono-src/opensles_shim.c ]; then
    cp chrono-src/opensles_shim.c chrono-src/opensles_shim.h "$SRC/"
    echo "  [OK] OpenSL-Shim aus Chrono"
fi

# 7d. Unity-spezifische NDK-Funktionen aus Phigros (anpassen)
if [ -f phigros-src/source/android_native_unity.c ]; then
    cp phigros-src/source/android_native_unity.c "$SRC/unity_ndk.c"
    # Switch-spezifische Includes entfernen
    sed -i '/#include <switch/d' "$SRC/unity_ndk.c"
    sed -i '/#include <switch\//d' "$SRC/unity_ndk.c"
    # libnx-spezifische Calls neutralisieren
    sed -i 's/padInitializeDefault(.*);/\/* compat *\//g' "$SRC/unity_ndk.c"
    sed -i 's/hidScanInput();/\/* compat *\//g' "$SRC/unity_ndk.c"
    echo "  [OK] Unity-NDK aus Phigros portiert"
fi

# 7e. Unity-JNI aus Phigros
if [ -f phigros-src/source/unity_jni.c ]; then
    cp phigros-src/source/unity_jni.c "$SRC/"
    sed -i '/#include <switch/d' "$SRC/unity_jni.c"
    echo "  [OK] unity_jni.c kopiert"
fi

# 7f. Eigene Shim-Dateien, die wir neu schreiben
cat > "$SRC/android_shim.c" <<'SHIMEOF'
/* android_shim.c – Implementierung der Android-NDK-Symbole,
 * die von libunity.so / libil2cpp.so importiert werden.
 * Basiert auf den Dead-Trigger-Logs (194 importierte ABI-Funktionen). */
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

/* ---------- Logging ---------- */
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

/* ---------- ANativeWindow ---------- */
/* Wird an SDL2-Fenster gekoppelt (siehe loader_main.c) */
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

/* ---------- ALooper (Single-Threaded Stub) ---------- */
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

/* ---------- ASensorManager (No-Op) ---------- */
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

/* ---------- System Properties ---------- */
/* Wichtig: Unity liest ro.build.version.sdk, ro.product.model etc. */
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

# 7g. Loader-Hauptdatei (ersetzt generisches main.c)
cat > "$SRC/loader_main.c" <<'MAINEOF'
/* loader_main.c – Dead Effect Unity-IL2CPP Loader
 *
 * Ablauf (aus Dead-Trigger-Logs abgeleitet):
 *   1. libmain.so laden
 *   2. JNI_OnLoad aufrufen → liefert JNI-Version
 *   3. NativeLoader.load(libdir) über JNI aufrufen
 *   4. libunity.so laden → init_array → JNI_OnLoad
 *   5. UnityPlayer.initJni(Context) aufrufen
 *   6. libil2cpp.so laden
 *   7. Render-Loop: nativeRender, nativePause/Resume via SDL2-Events
 */
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
#include "opensles_shim.h"

#define TAG "deadeffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* Konfiguration */
#ifndef DEAD_EFFECT_L
