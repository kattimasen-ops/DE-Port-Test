#!/bin/bash
# ============================================================
# Dead Effect Port - Build-Skript
# ============================================================

set +euo pipefail

export DEBIAN_FRONTEND=noninteractive

GITHUB_WS="${GITHUB_WORKSPACE:-$PWD}"
OUT="$GITHUB_WS"
WORK="$GITHUB_WS/work"
mkdir -p "$WORK"

BUILD_LOG="$WORK/build.log"
SYMS_FILE="$WORK/undefined_symbols.txt"
JNI_META="$WORK/jni_meta"
ZIP_FILE="$OUT/DeadEffect-Port.zip"

: > "$BUILD_LOG"
: > "$SYMS_FILE"
mkdir -p "$JNI_META"
echo "Build gestartet: $(date -u)" > "$SYMS_FILE"

log() { echo "$@" | tee -a "$BUILD_LOG" >&2; }

log "==> GITHUB_WORKSPACE = $OUT"
log "==> WORK             = $WORK"
log "==> PWD              = $PWD"
log "==> Shell-Optionen   = $-"

# ------------------------------------------------------------
# 1. apt
# ------------------------------------------------------------
log "===== STEP 1: apt ====="
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

apt-get update 2>&1 | tee -a "$BUILD_LOG" || log "[WARN] apt update"

# ------------------------------------------------------------
# 2. Cross-Toolchain
# ------------------------------------------------------------
log "===== STEP 2: Cross-Toolchain ====="
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git pkg-config ca-certificates \
  wget curl file zip unzip python3 patchelf \
  crossbuild-essential-arm64 \
  libasound2-dev:arm64 \
  libegl1-mesa-dev:arm64 libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 libgbm-dev:arm64 \
  libfreetype6-dev:arm64 libsdl2-dev:arm64 \
  zlib1g-dev:arm64 \
  2>&1 | tee -a "$BUILD_LOG" || log "[WARN] Einige Pakete fehlen"
aarch64-linux-gnu-gcc --version 2>&1 | head -1 | tee -a "$BUILD_LOG"

# ------------------------------------------------------------
# 3. Compat-Header
# ------------------------------------------------------------
log "===== STEP 3: Compat-Header ====="
mkdir -p /usr/include/android

cat > /usr/include/android/log.h <<'EOF'
#ifndef COMPAT_ANDROID_LOG_H
#define COMPAT_ANDROID_LOG_H
#include <stdio.h>
#include <stdarg.h>
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_ERROR   6
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_WARN    5
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
struct ANativeWindow { int32_t width; int32_t height; int32_t format; void *user_data; };
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
enum { ALOOPER_POLL_WAKE = -1, ALOOPER_POLL_CALLBACK = -2, ALOOPER_POLL_TIMEOUT = -3, ALOOPER_POLL_ERROR = -4 };
ALooper *ALooper_prepare(int opts);
ALooper *ALooper_forThread(void);
void ALooper_acquire(ALooper *l);
void ALooper_release(ALooper *l);
int ALooper_pollAll(int t, int *f, int *e, void **d);
int ALooper_pollOnce(int t, int *f, int *e, void **d);
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
    int32_t version, sensor, type, reserved0;
    int64_t timestamp;
    union { float data[16]; };
    uint32_t flags; int32_t reserved1[3];
} ASensorEvent;
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

log "[OK] Compat-Header geschrieben"

# ------------------------------------------------------------
# 4. DE .so laden
# ------------------------------------------------------------
log "===== STEP 4: Dead Effect .so ====="
mkdir -p "$WORK/de_libs"
if [ -n "${DE_LIBS_URL:-}" ]; then
    wget -q -O "$WORK/arm64-v8a.zip" "$DE_LIBS_URL" 2>>"$BUILD_LOG" || log "[WARN] wget"
    unzip -o "$WORK/arm64-v8a.zip" -d "$WORK/de_libs/" >/dev/null 2>&1 || log "[WARN] unzip"
else
    log "[WARN] DE_LIBS_URL nicht gesetzt"
fi

DE_MAIN="$WORK/de_libs/arm64-v8a/libmain.so"
DE_UNITY="$WORK/de_libs/arm64-v8a/libunity.so"
DE_IL2CPP="$WORK/de_libs/arm64-v8a/libil2cpp.so"
for f in "$DE_MAIN" "$DE_UNITY" "$DE_IL2CPP"; do
    if [ -f "$f" ]; then log "  $(basename "$f"): $(stat -c%s "$f") bytes"
    else log "  [WARN] $f fehlt"; fi
done

# ---- libc++_shared.so (nicht mehr benoetigt, aber der Vollstaendigkeit
#      halber ins ZIP legen, falls eine externe .so sie doch anfordert) ----
LIBCXX_SRC=""
for cand in \
    "$GITHUB_WS/libc++_shared.so" \
    "$GITHUB_WS/libs/libc++_shared.so" \
    "$GITHUB_WS/lib/libc++_shared.so" \
    "$GITHUB_WS/src/libc++_shared.so" \
    "$WORK/de_libs/arm64-v8a/libc++_shared.so" \
    ; do
    if [ -f "$cand" ]; then
        LIBCXX_SRC="$cand"
        log "  libc++_shared.so: gefunden unter $cand"
        break
    fi
done

if [ -n "$LIBCXX_SRC" ]; then
    cp "$LIBCXX_SRC" "$WORK/de_libs/arm64-v8a/libc++_shared.so"
    LIBCXX_DST="$WORK/de_libs/arm64-v8a/libc++_shared.so"
    if command -v patchelf >/dev/null 2>&1; then
        HAS_ANDROID=0
        for need in $(readelf -d "$LIBCXX_DST" 2>/dev/null | grep NEEDED | awk '{print $NF}' | tr -d '[]'); do
            case "$need" in
                libc.so|libdl.so|libm.so|libstdc++.so|liblog.so) HAS_ANDROID=1 ;;
            esac
        done
        if [ "$HAS_ANDROID" = "1" ]; then
            log "  [PATCH] libc++ hat Android-Namen — patche fuer glibc"
            patchelf --replace-needed libc.so      libc.so.6      "$LIBCXX_DST" 2>&1 | tee -a "$BUILD_LOG" || true
            patchelf --replace-needed libdl.so     libdl.so.2     "$LIBCXX_DST" 2>&1 | tee -a "$BUILD_LOG" || true
            patchelf --replace-needed libm.so      libm.so.6      "$LIBCXX_DST" 2>&1 | tee -a "$BUILD_LOG" || true
            patchelf --replace-needed libstdc++.so libstdc++.so.6 "$LIBCXX_DST" 2>&1 | tee -a "$BUILD_LOG" || true
            patchelf --replace-needed liblog.so    libc.so.6      "$LIBCXX_DST" 2>&1 | tee -a "$BUILD_LOG" || true
        fi
    fi
fi

# ------------------------------------------------------------
# 4b. Relocation-Analyse fuer libil2cpp und libunity
# ------------------------------------------------------------
log "===== STEP 4b: Relocation-Analyse ====="

for lib in "$DE_IL2CPP" "$DE_UNITY"; do
    if [ ! -f "$lib" ]; then continue; fi
    log "--- $(basename "$lib"): Relocation-Typen-Haeufigkeit ---"
    aarch64-linux-gnu-readelf -r "$lib" 2>/dev/null | \
        awk '{print $3}' | grep -E '^R_AARCH64' | sort | uniq -c | sort -rn | \
        tee -a "$BUILD_LOG" || log "  (readelf fehlgeschlagen)"

    log "--- $(basename "$lib"): COPY/TLSDESC im Detail ---"
    aarch64-linux-gnu-readelf -r "$lib" 2>/dev/null | \
        grep -E 'R_AARCH64_(COPY|TLSDESC)' | head -20 | tee -a "$BUILD_LOG" || \
        log "  (keine COPY/TLSDESC gefunden)"
done

log "--- Disassembly libil2cpp @ 0x8015b0 - 0x8016a0 ---"
if [ -f "$DE_IL2CPP" ] && command -v aarch64-linux-gnu-objdump >/dev/null 2>&1; then
    aarch64-linux-gnu-objdump -d \
        --start-address=0x8015b0 \
        --stop-address=0x8016a0 \
        "$DE_IL2CPP" 2>&1 | tee -a "$BUILD_LOG"
fi

log "===== STEP 4b fertig ====="

# ------------------------------------------------------------
# 5. JNI-Metadaten
# ------------------------------------------------------------
log "===== STEP 5: JNI-Meta ====="
if [ -f "$DE_MAIN" ]; then
    strings -a "$DE_MAIN" 2>/dev/null | grep -iE 'NativeLoader|unity3d|player' | sort -u > "$JNI_META/nativeloader_hints.txt" || true
fi
if [ -f "$DE_UNITY" ]; then
    strings -a "$DE_UNITY" 2>/dev/null | grep -E '^com/unity3d/player' | sort -u > "$JNI_META/unity_classes.txt" || true
fi
log "--- NativeLoader-Hints ---"
cat "$JNI_META/nativeloader_hints.txt" 2>/dev/null | tee -a "$BUILD_LOG" || true

# ------------------------------------------------------------
# 6. Kompilieren
# ------------------------------------------------------------
log "===== STEP 6: Kompilieren ====="

SRC_IN="$GITHUB_WS/src"
if [ ! -d "$SRC_IN" ]; then
    log "[FEHLER] $SRC_IN fehlt!"
    exit 0
fi

BUILD_SRC="$WORK/build_src"
rm -rf "$BUILD_SRC"
mkdir -p "$BUILD_SRC"
cp "$SRC_IN"/*.c "$SRC_IN"/*.h "$BUILD_SRC/" 2>/dev/null || true

log "Quellen:"
ls -la "$BUILD_SRC" 2>&1 | tee -a "$BUILD_LOG"

cd "$BUILD_SRC"

CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic -Wl,-E"
CFLAGS="$CFLAGS -DDEAD_EFFECT_LIBDIR=\"/roms/ports/DeadEffect/lib\""
CFLAGS="$CFLAGS -DDEAD_EFFECT_ASSETS=\"/roms/ports/DeadEffect/assets\""
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations -Wno-error -Wno-format"
CFLAGS="$CFLAGS -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter"
CFLAGS="$CFLAGS -I. -I/usr/include -I/usr/aarch64-linux-gnu/include"
CFLAGS="$CFLAGS -I/usr/include/SDL2 -I/usr/aarch64-linux-gnu/include/SDL2"

LDFLAGS="-L/usr/aarch64-linux-gnu/lib -lSDL2 -lGLESv2 -lEGL -ldl -lm -lpthread -lstdc++ -lgcc_s -rdynamic -Wl,-E"

SRCS=$(ls *.c 2>/dev/null | grep -v '^main\.c$' || true)
log "Kompiliere: $SRCS"

OBJS=""
COMPILE_FAILED=0
for src in $SRCS; do
    OBJ="/tmp/$(basename "$src" .c).o"
    log "--- $src ---"
    (
        aarch64-linux-gnu-gcc $CFLAGS -c "$src" -o "$OBJ" 2>&1 | head -60
    ) | tee -a "$BUILD_LOG"
    if [ -f "$OBJ" ]; then
        OBJS="$OBJS $OBJ"
    else
        log "  [FEHLER] $src kompiliert nicht"
        COMPILE_FAILED=1
    fi
done

if [ "$COMPILE_FAILED" = "1" ]; then
    log "[FEHLER] Mindestens eine Quelldatei fehlgeschlagen — Linken uebersprungen."
else
    log "==> Linke Loader"
    (
        aarch64-linux-gnu-gcc -o deadeffect-loader $OBJS $LDFLAGS 2>&1 | head -100
    ) | tee -a "$BUILD_LOG"
fi

LOADER_OK=0
if [ -f deadeffect-loader ]; then
    SZ=$(stat -c%s deadeffect-loader)
    SYM_OK=1
    for want in slCreateEngine jni_find_native jni_dump_natives so_load; do
        if ! aarch64-linux-gnu-nm deadeffect-loader 2>/dev/null | grep -q " T $want\$"; then
            log "[WARN] Symbol '$want' fehlt im Loader"
            SYM_OK=0
        fi
    done

    if [ "$SZ" -lt 40000 ]; then
        log "[FEHLER] Loader nur $SZ Bytes gross (vermutlich Fallback)"
        LOADER_OK=0
    elif [ "$SYM_OK" != "1" ]; then
        log "[FEHLER] Loader fehlen Pflicht-Symbole"
        LOADER_OK=0
    else
        LOADER_OK=1
        log "[OK] Loader gebaut und geprueft ($SZ Bytes)"
        file deadeffect-loader | tee -a "$BUILD_LOG"
        ls -la deadeffect-loader | tee -a "$BUILD_LOG"
        readelf -d deadeffect-loader 2>/dev/null | grep NEEDED | tee -a "$BUILD_LOG" || true
    fi
else
    log "[FEHLER] Linken fehlgeschlagen — keine Loader-Datei vorhanden"
fi

{
    echo "=== Loader_ok: $LOADER_OK ==="
    echo "=== Loader-Groesse: $(stat -c%s deadeffect-loader 2>/dev/null) Bytes ==="
    echo "=== Undefined symbols (falls vorhanden) ==="
    [ -f deadeffect-loader ] && aarch64-linux-gnu-nm -D --undefined-only deadeffect-loader 2>/dev/null | head -200
} > "$SYMS_FILE" 2>&1

# ------------------------------------------------------------
# 7. Port-Paket
# ------------------------------------------------------------
log "===== STEP 7: Port-Paket ====="
cd "$WORK"
rm -rf port
mkdir -p port/DeadEffect/lib port/DeadEffect/assets

if [ -f "$BUILD_SRC/deadeffect-loader" ] && [ "$LOADER_OK" = "1" ]; then
    cp "$BUILD_SRC/deadeffect-loader" port/DeadEffect/
    log "[OK] Echter Loader ins Port-Paket kopiert ($(stat -c%s port/DeadEffect/deadeffect-loader) Bytes)"
else
    printf '#!/bin/bash\necho "Loader nicht erfolgreich gebaut"\nexit 1\n' > port/DeadEffect/deadeffect-loader
    log "[WARN] Fallback-Loader ins Port-Paket kopiert"
fi
chmod +x port/DeadEffect/deadeffect-loader

[ -d "$WORK/de_libs/arm64-v8a" ] && cp "$WORK"/de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

if [ -f "$GITHUB_WS/DeadEffect.sh" ]; then
    cp "$GITHUB_WS/DeadEffect.sh" port/DeadEffect/DeadEffect.sh
    chmod +x port/DeadEffect/DeadEffect.sh
    log "[OK] DeadEffect.sh aus Repo uebernommen"
fi

cat > port/DeadEffect/de_wrapper.gptk <<'GPTK'
back = esc
start = enter
a = x
b = z
x = c
y = v
up = up
down = down
left = left
right = right
left_analog_up = w
left_analog_down = s
left_analog_left = a
left_analog_right = d
right_analog_up = mouse_movement_up
right_analog_down = mouse_movement_down
right_analog_left = mouse_movement_left
right_analog_right = mouse_movement_right
l1 = q
r1 = e
l2 = tab
r2 = shift
GPTK

cat > port/DeadEffect/README.txt <<'READMEEOF'
Dead Effect - PortMaster-Port
Loader: deadeffect-loader (Unity-IL2CPP)
READMEEOF

mkdir -p port/DeadEffect/debug_analysis
cp "$BUILD_LOG" port/DeadEffect/debug_analysis/ 2>/dev/null || true
cp "$SYMS_FILE" port/DeadEffect/debug_analysis/ 2>/dev/null || true
cp -r "$JNI_META" port/DeadEffect/debug_analysis/ 2>/dev/null || true
ls -la "$BUILD_SRC" > port/DeadEffect/debug_analysis/build_src.txt 2>/dev/null || true
if [ -d "$BUILD_SRC" ]; then
    for f in "$BUILD_SRC"/*.c "$BUILD_SRC"/*.h; do
        [ -f "$f" ] && echo "===== $f =====" && cat "$f"
    done > port/DeadEffect/debug_analysis/sources_snapshot.txt 2>/dev/null || true
fi

cd port
rm -f "$ZIP_FILE"
zip -r -9 "$ZIP_FILE" . > /dev/null 2>&1 || zip -r "$ZIP_FILE" . > /dev/null 2>&1
cd "$WORK"

log "=== ZIP: $ZIP_FILE ==="
unzip -l "$ZIP_FILE" 2>/dev/null | tee -a "$BUILD_LOG" || true
log "==> Build fertig."

exit 0
