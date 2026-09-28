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
#   - OpenSL-ES-Shim aus Chrono
#   - OBB-Handling im Launcher
#   - Capability-Report (nxcompat-Äquivalent)
#   - Symbol-Resolution-Check vor dem Linken
#   - ROBUST: bricht NICHT mehr bei Fehlern ab
#   - Diagnose-Artefakte landen IMMER im ZIP
# ============================================================

# KEIN 'set -e' – wir wollen Diagnose-Artefakte auch bei Fehlern.
set -uo pipefail

export DEBIAN_FRONTEND=noninteractive

# ---- Output IMMER ins GitHub-Workspace -------------------
if [ -n "${GITHUB_WORKSPACE:-}" ]; then
    OUT="$GITHUB_WORKSPACE"
else
    OUT="$PWD"
fi
WORK="${WORK:-/work}"
mkdir -p "$OUT" "$WORK"

# ---- Diagnose-Falle --------------------------------------
diagnose() {
    local rc=$?
    echo ""
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "!! FEHLER (rc=$rc) in Zeile ${BASH_LINENO[0]:-?}"
    echo "!! Befehl: ${BASH_COMMAND:-?}"
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "--- letzte 40 Zeilen Build-Log ---"
    [ -f "$WORK/build.log" ] && tail -40 "$WORK/build.log" || true
    echo "---------------------------------------------------------"
}
trap diagnose ERR

# ---- Alles doppelt loggen --------------------------------
BUILD_LOG="$WORK/build.log"
: > "$BUILD_LOG"
exec > >(tee -a "$BUILD_LOG") 2>&1

echo "==> GITHUB_WORKSPACE = $OUT"
echo "==> WORK             = $WORK"
echo "==> PWD              = $PWD"
echo "==> Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)"

# ------------------------------------------------------------
# 1. Multiarch + apt-Quellen
# ------------------------------------------------------------
dpkg --add-architecture arm64 || true

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
apt-get update || echo "[WARN] apt-get update teilweise fehlgeschlagen"

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
  zlib1g-dev:arm64 \
  || echo "[WARN] Einige Pakete konnten nicht installiert werden"

aarch64-linux-gnu-gcc --version | head -1 || echo "[WARN] Cross-GCC fehlt"

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

# android/native_window.h
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

# android/looper.h
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

# android/sensor.h
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

echo "[OK] Compat-Header geschrieben"

# ------------------------------------------------------------
# 4. Dead Effect .so-Dateien holen
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
cd "$WORK"
mkdir -p de_libs
if [ -n "${DE_LIBS_URL:-}" ]; then
    wget -q -O arm64-v8a.zip "$DE_LIBS_URL" || echo "[WARN] wget fehlgeschlagen"
    unzip -o arm64-v8a.zip -d de_libs/ >/dev/null 2>&1 || echo "[WARN] unzip fehlgeschlagen"
else
    echo "[WARN] DE_LIBS_URL nicht gesetzt – erwarte .so in de_libs/arm64-v8a/"
fi

DE_MAIN=de_libs/arm64-v8a/libmain.so
DE_UNITY=de_libs/arm64-v8a/libunity.so
DE_IL2CPP=de_libs/arm64-v8a/libil2cpp.so

for f in "$DE_MAIN" "$DE_UNITY" "$DE_IL2CPP"; do
    if [ -f "$f" ]; then
        echo "  $(basename "$f"): $(stat -c%s "$f") bytes"
    else
        echo "  [WARN] $f fehlt"
    fi
done

# ------------------------------------------------------------
# 5. JNI-Metadaten aus Dead Effect extrahieren
# ------------------------------------------------------------
echo "==> Extrahiere JNI-Metadaten aus Dead Effect"
mkdir -p "$WORK/jni_meta"

if [ -f "$DE_MAIN" ]; then
    strings -a "$DE_MAIN" | grep -E '^(com|android|java|org)/' | sort -u > "$WORK/jni_meta/classes_main.txt" || true
    strings -a "$DE_MAIN" | grep -iE 'NativeLoader|bulkypix|deadeffect' | sort -u > "$WORK/jni_meta/nativeloader_hints.txt" || true
    strings -a "$DE_MAIN" | grep -E '^\(.*\)' | sort -u > "$WORK/jni_meta/signatures_main.txt" || true
fi

if [ -f "$DE_UNITY" ]; then
    strings -a "$DE_UNITY" | grep -E '^(com|android|java|org)/' | sort -u > "$WORK/jni_meta/classes_unity.txt" || true
    strings -a "$DE_UNITY" | grep -E '^com/unity3d/player' | sort -u > "$WORK/jni_meta/unity_classes.txt" || true
fi

echo "--- NativeLoader-Kandidaten ---"
cat "$WORK/jni_meta/nativeloader_hints.txt" 2>/dev/null || echo "  (keine)"
echo "--- Unity-Player-Klassen ---"
cat "$WORK/jni_meta/unity_classes.txt" 2>/dev/null || echo "  (keine)"

PKG_NAME=$(strings -a "$DE_MAIN" 2>/dev/null | grep -oE 'com/[a-z]+/[a-z]+' | head -1 | tr '/' '.' || true)
[ -z "$PKG_NAME" ] && PKG_NAME="com.bulkypix.deadeffect"
echo "==> Erkannter Package-Name: $PKG_NAME"

# ------------------------------------------------------------
# 6. Quellen klonen
# ------------------------------------------------------------
echo "==> Klone Basis-Quellen"
cd "$WORK"

if [ ! -d chrono-src ]; then
    git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null \
        || echo "[WARN] Chrono konnte nicht geklont werden"
fi

if [ ! -d falsjni-src ]; then
    git clone --depth=1 https://github.com/Rinnegatamante/FalsoJNI.git falsjni-src 2>/dev/null \
        || echo "[WARN] FalsoJNI konnte nicht geklont werden"
fi

if [ ! -d phigros-src ]; then
    git clone --depth=1 https://github.com/ChanseyIsTheBest/phigros_nx.git phigros-src 2>/dev/null \
        || echo "[WARN] Phigros NX konnte nicht geklont werden"
fi

if [ ! -d soloader-src ]; then
    git clone --depth=1 https://github.com/v-atamanenko/soloader-boilerplate.git soloader-src 2>/dev/null \
        || echo "[WARN] Soloader-Boilerplate konnte nicht geklont werden"
fi

# ------------------------------------------------------------
# 7. Loader-Quellen zusammenstellen
# ------------------------------------------------------------
echo "==> Stelle Loader-Quellen zusammen"
SRC="$WORK/loader_src"
rm -rf "$SRC"
mkdir -p "$SRC"

# 7a. so_util aus Chrono (oder Fallback soloader)
if [ -f chrono-src/so_util.c ]; then
    cp chrono-src/so_util.c chrono-src/so_util.h "$SRC/" 2>/dev/null || true
    cp chrono-src/imports.c chrono-src/imports.h "$SRC/" 2>/dev/null || true
    echo "  [OK] so_util aus Chrono"
elif [ -f soloader-src/source/so_util.c ]; then
    cp soloader-src/source/so_util.* "$SRC/" 2>/dev/null || true
    echo "  [OK] so_util aus soloader-boilerplate"
else
    echo "  [WARN] Keine so_util-Quelle gefunden"
fi

# 7b. JNI-Shim aus FalsoJNI
if [ -d falsjni-src/src ]; then
    cp falsjni-src/src/*.c falsjni-src/src/*.h "$SRC/" 2>/dev/null || true
    echo "  [OK] FalsoJNI kopiert"
fi

# 7c. OpenSL-ES-Shim aus Chrono
if [ -f chrono-src/opensles_shim.c ]; then
    cp chrono-src/opensles_shim.c chrono-src/opensles_shim.h "$SRC/" 2>/dev/null || true
    echo "  [OK] OpenSL-Shim aus Chrono"
fi

# 7d. Unity-spezifische NDK-Funktionen aus Phigros
if [ -f phigros-src/source/android_native_unity.c ]; then
    cp phigros-src/source/android_native_unity.c "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i '/#include <switch/d' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i '/#include <switch\//d' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i 's/padInitializeDefault(.*);/\/* compat *\//g' "$SRC/unity_ndk.c" 2>/dev/null || true
    sed -i 's/hidScanInput();/\/* compat *\//g' "$SRC/unity_ndk.c" 2>/dev/null || true
    echo "  [OK] Unity-NDK aus Phigros portiert"
fi

# 7e. Unity-JNI aus Phigros
if [ -f phigros-src/source/unity_jni.c ]; then
    cp phigros-src/source/unity_jni.c "$SRC/" 2>/dev/null || true
    sed -i '/#include <switch/d' "$SRC/unity_jni.c" 2>/dev/null || true
    echo "  [OK] unity_jni.c kopiert"
fi

# 7f. android_shim.c
cat > "$SRC/android_shim.c" <<'SHIMEOF'
/* android_shim.c – Implementierung der Android-NDK-Symbole,
 * die von libunity.so / libil2cpp.so importiert werden. */
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

# 7g. loader_main.c
cat > "$SRC/loader_main.c" <<'MAINEOF'
/* loader_main.c – Dead Effect Unity-IL2CPP Loader */
#define _
