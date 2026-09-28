#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Chrono-Trigger-Loader-Architektur
# GPU-Ziel: Mali-G31 (Bifrost)
#
# WICHTIG: Chrono-Quellcode stammt aus Cocos2d-x-Umgebung.
# Wir muessen Android-NDK-Header und Cocos2d-Konstanten stubben.
# ============================================================
set -e

export DEBIAN_FRONTEND=noninteractive

echo "==> Host arch: $(uname -m)"
echo "==> Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)"

# ------------------------------------------------------------
# 1. Multiarch + apt-Quellen
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
  build-essential cmake git pkg-config ca-certificates wget file zip unzip python3 \
  crossbuild-essential-arm64 \
  libasound2-dev:arm64 \
  libpulse-dev:arm64 \
  libegl1-mesa-dev:arm64 \
  libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 \
  libgbm-dev:arm64 \
  libfreetype6-dev:arm64 \
  libsdl2-dev:arm64 \
  zlib1g-dev:arm64

aarch64-linux-gnu-gcc --version | head -1

# ------------------------------------------------------------
# 3. Native Bibliotheken herunterladen
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
unzip -o arm64-v8a.zip -d de_libs/
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 4. Chrono-Trigger-Loader-Quellen klonen
# ------------------------------------------------------------
echo "==> Klone Chrono-Trigger-Loader-Quellen"
if ! git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null; then
    git clone --depth=1 https://github.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null || {
        echo "[FEHLER] Chrono-Trigger-Repo nicht erreichbar"
        exit 1
    }
fi

cd chrono-src/chrono
ls -la src/

# ------------------------------------------------------------
# 5. Kompatibilitaets-Header erstellen
#    (Android-NDK und Cocos2d-Konstanten stubben)
# ------------------------------------------------------------
echo "==> Erstelle Kompatibilitaets-Header"

mkdir -p src/compat/android

# android/log.h Stub
cat > src/compat/android/log.h <<'EOF'
#ifndef COMPAT_ANDROID_LOG_H
#define COMPAT_ANDROID_LOG_H

#include <stdio.h>
#include <stdarg.h>

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO  4
#endif
#ifndef ANDROID_LOG_ERROR
#define ANDROID_LOG_ERROR 6
#endif

static inline int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    return r;
}

static inline int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    return r;
}

static inline int __android_log_write(int prio, const char* tag, const char* text) {
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", text ? text : "");
    return 0;
}

#endif
EOF

# Cocos2d-Konstanten-Stub
cat > src/compat/cocos_stubs.h <<'EOF'
#ifndef COMPAT_COCOS_STUBS_H
#define COMPAT_COCOS_STUBS_H

/* Cocos2d-Controller-Konstanten (Dummies, werden nicht verwendet) */
#define CK_BUTTON_A                  1000
#define CK_BUTTON_B                  1001
#define CK_BUTTON_X                  1002
#define CK_BUTTON_Y                  1003
#define CK_BUTTON_DPAD_LEFT          1004
#define CK_BUTTON_DPAD_RIGHT         1005
#define CK_BUTTON_DPAD_UP            1006
#define CK_BUTTON_DPAD_DOWN          1007
#define CK_BUTTON_LEFT_THUMBSTICK    1008
#define CK_BUTTON_RIGHT_THUMBSTICK   1009
#define CK_BUTTON_START              1010
#define CK_BUTTON_SELECT             1011
#define CK_BUTTON_L1                 1012
#define CK_BUTTON_L2                 1013
#define CK_BUTTON_R1                 1014
#define CK_BUTTON_R2                 1015

#define CK_JOYSTICK_LEFT_X           2000
#define CK_JOYSTICK_LEFT_Y           2001
#define CK_JOYSTICK_RIGHT_X          2002
#define CK_JOYSTICK_RIGHT_Y          2003

#endif
EOF

# ------------------------------------------------------------
# 6. main.c chirurgisch patchen (NICHT aggressiv!)
# ------------------------------------------------------------
echo "==> Patche main.c"
python3 - <<'PYEOF'
import os
path = "src/main.c"
if not os.path.exists(path):
    print("[WARN] main.c nicht gefunden")
else:
    with open(path) as f:
        content = f.read()

    orig = content

    # 6a. Cocos-Stubs-Header ganz oben einbinden (nach den System-Includes)
    if '#include "cocos_stubs.h"' not in content and 'compat/cocos_stubs.h' not in content:
        # Nach den letzten #include-Zeilen einfuegen
        lines = content.split('\n')
        last_include = 0
        for i, line in enumerate(lines):
            if line.strip().startswith('#include'):
                last_include = i
        lines.insert(last_include + 1, '#include "cocos_stubs.h"')
        content = '\n'.join(lines)

    # 6b. Bibliotheksnamen ersetzen (funktioniert)
    content = content.replace('libchrono.so', 'libmain.so')
    content = content.replace('libc++_shared.so', 'libunity.so')
    content = content.replace('libencrypt.so', 'libil2cpp.so')

    # 6c. Package-Name ersetzen
    content = content.replace(
        'com.square_enix.android_googleplay.chrono_trigger',
        'com.bulkypix.deadeffect'
    )

    # 6d. KEINE Kommentar-Manipulation!
    # Wir lassen alle cocos2d:: Referenzen in Ruhe.
    # Sie sind nur in Kommentaren/Deklarationen und schaden nicht.

    if content != orig:
        with open(path, "w") as f:
            f.write(content)
        print(f"[PATCH] main.c angepasst ({len(orig)} -> {len(content)} bytes)")
    else:
        print("[INFO] main.c: Keine Aenderungen noetig")
PYEOF

# ------------------------------------------------------------
# 7. jni_shim.c: android/log.h durch compat ersetzen
# ------------------------------------------------------------
echo "==> Patche jni_shim.c"
python3 - <<'PYEOF'
import os
path = "src/jni_shim.c"
if not os.path.exists(path):
    print("[WARN] jni_shim.c nicht gefunden")
else:
    with open(path) as f:
        content = f.read()

    orig = content

    # android/log.h durch compat-Pfad ersetzen (funktioniert mit -I)
    # Eigentlich reicht es, wenn -I src/compat gesetzt ist.

    # Cocos2d-JNI-Funktionen finden und durch Log-Meldungen ergaenzen
    # (NICHT die Funktionskoerper entfernen!)
    if 'Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext' in content:
        print("[INFO] Cocos2d-JNI-Funktionen noch vorhanden - werden bleiben")

    if content != orig:
        with open(path, "w") as f:
            f.write(content)
        print(f"[PATCH] jni_shim.c angepasst")
    else:
        print("[INFO] jni_shim.c: Keine Aenderungen noetig")
PYEOF

# ------------------------------------------------------------
# 8. imports.c: Android-Stubs hinzufuegen (unveraendert von vorher)
# ------------------------------------------------------------
echo "==> Erweitere imports.c"
python3 - <<'PYEOF'
import os
path = "src/imports.c"
if not os.path.exists(path):
    print("[WARN] imports.c nicht gefunden")
else:
    with open(path) as f:
        content = f.read()

    if 'ASensorManager_createEventQueue' in content:
        print("[SKIP] imports.c: Android-Stubs bereits vorhanden")
    else:
        android_stubs = r'''

/* ============================================================
 * [PATCHED] Android-Sensor/Looper/Window/Property-Stubs
 * ============================================================ */
#include <stdint.h>
#include <stddef.h>

void* ASensorManager_createEventQueue(void* m, void* l, int id, void* cb, void* data) { return NULL; }
void* ASensorManager_getInstance(void) { return NULL; }
void* ASensorManager_getSensorList(void* m, int* count) { if(count) *count = 0; return NULL; }
void* ASensorManager_getDefaultSensor(void* m, int type) { return NULL; }
void  ASensorManager_destroyEventQueue(void* m, void* q) {}
int   ASensor_getMinDelay(void* s) { return 0; }
int   ASensor_getType(void* s) { return 0; }
const char* ASensor_getName(void* s) { return "stub"; }
const char* ASensor_getVendor(void* s) { return "stub"; }
float ASensor_getResolution(void* s) { return 1.0f; }
int   ASensorEventQueue_getEvents(void* q, void* events, int count) { return 0; }
int   ASensorEventQueue_hasEvents(void* q) { return 0; }
int   ASensorEventQueue_enableSensor(void* q, void* s) { return 0; }
int   ASensorEventQueue_disableSensor(void* q, void* s) { return 0; }
int   ASensorEventQueue_setEventRate(void* q, void* s, int rate) { return 0; }

void* ALooper_prepare(int opts) { return NULL; }
void* ALooper_forThread(void) { return NULL; }
int   ALooper_pollAll(int timeout, int* fd, int* events, void** data) { return -1; }
void  ALooper_acquire(void* looper) {}
void  ALooper_release(void* looper) {}
void  ALooper_wake(void* looper) {}

void ANativeWindow_acquire(void* window) {}
void ANativeWindow_release(void* window) {}
int  ANativeWindow_getWidth(void* window) { return 640; }
int  ANativeWindow_getHeight(void* window) { return 480; }

int   __system_property_read(void* pi, char* name, char* value) { return 0; }
void* __system_property_find(const char* name) { return NULL; }
'''
        content = content + android_stubs
        with open(path, "w") as f:
            f.write(content)
        print("[PATCH] imports.c: Android-Stubs hinzugefuegt")
PYEOF

# ------------------------------------------------------------
# 9. Loader bauen
# ------------------------------------------------------------
echo "==> Baue Loader mit aarch64-linux-gnu-gcc"

SRCS=$(ls src/*.c 2>/dev/null)
echo "Quelldateien: $SRCS"

# Compiler-Flags MIT compat-Pfad
CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations -Wno-error"
CFLAGS="$CFLAGS -I src -I src/compat"

for inc in \
    "/usr/aarch64-linux-gnu/include" \
    "/usr/aarch64-linux-gnu/include/SDL2" \
    "/usr/include/SDL2" \
    "/usr/include/freetype2" \
    "/usr/aarch64-linux-gnu/include/freetype2"; do
    if [ -d "$inc" ]; then
        CFLAGS="$CFLAGS -I $inc"
    fi
done

LDFLAGS="-L/usr/aarch64-linux-gnu/lib"
LDFLAGS="$LDFLAGS -lSDL2 -lGLESv2 -lEGL -lfreetype -ldl -lm -lpthread -lstdc++ -lgcc_s"

echo "==> Kompiliere..."
set +e
aarch64-linux-gnu-gcc $CFLAGS -o chrono $SRCS $LDFLAGS 2>&1 | tail -30
BUILD_STATUS=${PIPESTATUS[0]}
set -e

if [ ! -f chrono ] || [ "$BUILD_STATUS" -ne 0 ]; then
    echo "[FEHLER] Loader-Binary wurde nicht erstellt (Status: $BUILD_STATUS)"
    for src in $SRCS; do
        echo "--- Diagnose: $src ---"
        aarch64-linux-gnu-gcc $CFLAGS -c "$src" -o /tmp/test.o 2>&1 | head -10
    done
    exit 1
fi

echo "[OK] Loader gebaut:"
file chrono

cd ../..

# ------------------------------------------------------------
# 10. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
cd /work
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/assets

cp chrono-src/chrono/chrono port/DeadEffect/deadeffect-loader
chmod +x port/DeadEffect/deadeffect-loader

cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

if [ -f /work/start.sh ]; then
    cp /work/start.sh port/DeadEffect/DeadEffect.sh
    chmod +x port/DeadEffect/DeadEffect.sh
fi

if [ -f /work/de_wrapper.gptk ]; then
    cp /work/de_wrapper.gptk port/DeadEffect/
fi

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port
Technik: Chrono-Trigger-Loader + Unity-JNI-Stubs
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo "==> Build erfolgreich."
