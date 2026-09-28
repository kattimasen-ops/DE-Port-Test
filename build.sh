#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Chrono-Trigger-Loader-Architektur
# GPU-Ziel: Mali-G31 (Bifrost) statt Mali-450 (Utgard)
#
# NEU: JNI-Shim wird von Cocos2d-x auf Unity umgestellt
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

echo "=== GCC ==="
aarch64-linux-gnu-gcc --version | head -1

# ------------------------------------------------------------
# 3. Native Bibliotheken herunterladen
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
unzip -o arm64-v8a.zip -d de_libs/
echo "=== Enthaltene Bibliotheken ==="
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 4. Chrono-Trigger-Loader-Quellen klonen
# ------------------------------------------------------------
echo "==> Klone Chrono-Trigger-Loader-Quellen"
if ! git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null; then
    echo "[WARN] Gitee-Klon fehlgeschlagen - versuche GitHub-Spiegel"
    git clone --depth=1 https://github.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null || {
        echo "[FEHLER] Chrono-Trigger-Repo nicht erreichbar"
        exit 1
    }
fi

cd chrono-src/chrono
echo "=== Chrono-Struktur ==="
ls -la
echo "=== src/ ==="
ls -la src/ 2>/dev/null || { echo "[FEHLER] src/ nicht gefunden"; exit 1; }

# ------------------------------------------------------------
# 5. JNI-Shim von Cocos2d-x auf Unity umstellen
# ------------------------------------------------------------
echo "==> Passe JNI-Shim fuer Unity an"

# 5a. main.c: Entry-Points anpassen
python3 - <<'PYEOF'
import os, re

path = "src/main.c"
if os.path.exists(path):
    with open(path) as f:
        content = f.read()

    orig = content
    # Cocos2d-Entry-Points durch Unity-Entry-Points ersetzen
    replacements = {
        'Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext': 'JNI_OnLoad',
        'Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath': 'JNI_OnLoad_placeholder',
        'Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit': 'UnityPlayer_initJNI',
        'Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender': 'UnityPlayer_nativeRender',
        'Java_org_cocos2dx_lib_GameControllerAdapter_nativeControllerButtonEvent': 'UnityPlayer_dispatchKeyEvent',
        'libchrono.so': 'libmain.so',
        'libc++_shared.so': 'libunity.so',
        'libencrypt.so': 'libil2cpp.so',
        'com.square_enix.android_googleplay.chrono_trigger': 'com.bulkypix.deadeffect',
    }
    for old, new in replacements.items():
        content = content.replace(old, new)

    # Deaktivere Cocos2d-App-Init, falls vorhanden
    content = re.sub(
        r'(\bcocos_android_app_init\s*\([^)]*\)\s*;)',
        r'/* [DISABLED-COCOS2D] \1 */',
        content
    )
    content = re.sub(
        r'(\bcocos2d::[A-Za-z_:]+)',
        r'/* [DISABLED-COCOS2D] \1 */',
        content
    )

    with open(path, "w") as f:
        f.write(content)

    if content != orig:
        print(f"[PATCH] main.c angepasst ({len(orig)} -> {len(content)} bytes)")
    else:
        print("[INFO] main.c: Keine Cocos2d-Referenzen gefunden")
else:
    print("[WARN] main.c nicht gefunden")
PYEOF

# 5b. jni_shim.c: Cocos2d-Funktionen deaktivieren, Unity-Stubs hinzufuegen
python3 - <<'PYEOF'
import os, re

path = "src/jni_shim.c"
if not os.path.exists(path):
    print("[WARN] jni_shim.c nicht gefunden")
else:
    with open(path) as f:
        content = f.read()

    orig = content

    # Cocos2d-Funktionen deaktivieren (in Kommentar setzen)
    cocos_funcs = [
        'Cocos2dxHelper_nativeSetContext',
        'Cocos2dxHelper_nativeSetApkPath',
        'Cocos2dxHelper_nativeSetAudioDeviceInfo',
        'Cocos2dxRenderer_nativeInit',
        'Cocos2dxRenderer_nativeRender',
        'Cocos2dxRenderer_nativeOnPause',
        'Cocos2dxRenderer_nativeOnResume',
        'Cocos2dxRenderer_nativeOnSurfaceChanged',
    ]
    for func in cocos_funcs:
        # Deaktiviere den Funktionskoerper
        pattern = rf'(JNIEXPORT[^;{{]*\b{func}\s*\([^)]*\)\s*\{{)'
        content = re.sub(
            pattern,
            lambda m: f'/* [DISABLED-COCOS2D] {m.group(1)}',
            content
        )

    # Unity-JNI-Stubs anhaengen
    unity_stubs = r'''

/* ============================================================
 * [PATCHED] Unity-spezifische JNI-Stubs fuer Dead Effect
 * ============================================================ */

#include <android/log.h>
#include <jni.h>

#ifndef JNI_VERSION_1_6
#define JNI_VERSION_1_6 0x00010006
#endif

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    __android_log_print(4, "DE-Loader", "JNI_OnLoad aufgerufen (vm=%p)", vm);
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL UnityPlayer_initJNI(JNIEnv* env, jobject thiz) {
    __android_log_print(4, "DE-Loader", "UnityPlayer.initJNI aufgerufen");
}

JNIEXPORT void JNICALL UnityPlayer_nativeRender(JNIEnv* env, jobject thiz) {
    static int frame = 0;
    if (frame++ % 60 == 0) {
        __android_log_print(4, "DE-Loader", "nativeRender Frame %d", frame);
    }
}

JNIEXPORT void JNICALL UnityPlayer_nativeResume(JNIEnv* env, jobject thiz) {
    __android_log_print(4, "DE-Loader", "UnityPlayer.nativeResume aufgerufen");
}

JNIEXPORT void JNICALL UnityPlayer_nativePause(JNIEnv* env, jobject thiz) {
    __android_log_print(4, "DE-Loader", "UnityPlayer.nativePause aufgerufen");
}

JNIEXPORT void JNICALL UnityPlayer_dispatchKeyEvent(JNIEnv* env, jobject thiz, jobject event) {
    __android_log_print(4, "DE-Loader", "UnityPlayer.dispatchKeyEvent");
}

JNIEXPORT void JNICALL UnityPlayer_nativeSetInputString(JNIEnv* env, jobject thiz, jstring str) {
    __android_log_print(4, "DE-Loader", "UnityPlayer.nativeSetInputString");
}
'''
    content = content + unity_stubs

    with open(path, "w") as f:
        f.write(content)
    print(f"[PATCH] jni_shim.c: Unity-Stubs hinzugefuegt ({len(orig)} -> {len(content)} bytes)")
PYEOF

# 5c. imports.c: Android-Sensor/Looper/Window-Stubs hinzufuegen
python3 - <<'PYEOF'
import os

path = "src/imports.c"
if not os.path.exists(path):
    print("[WARN] imports.c nicht gefunden")
else:
    with open(path) as f:
        content = f.read()

    orig = content
    android_stubs = r'''

/* ============================================================
 * [PATCHED] Android-Sensor/Looper/Window/Property-Stubs
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>

/* ASensor-Stubs (Sensoren - fuer Dead Effect nicht relevant) */
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

/* ALooper-Stubs (Event-Loop) */
void* ALooper_prepare(int opts) { return NULL; }
void* ALooper_forThread(void) { return NULL; }
int   ALooper_pollAll(int timeout, int* fd, int* events, void** data) { return -1; }
void  ALooper_acquire(void* looper) {}
void  ALooper_release(void* looper) {}
void  ALooper_wake(void* looper) {}

/* ANativeWindow-Stubs */
void ANativeWindow_acquire(void* window) {}
void ANativeWindow_release(void* window) {}
int  ANativeWindow_getWidth(void* window) { return 640; }
int  ANativeWindow_getHeight(void* window) { return 480; }

/* System-Property-Stubs */
int   __system_property_read(void* pi, char* name, char* value) { return 0; }
void* __system_property_find(const char* name) { return NULL; }
'''
    content = content + android_stubs

    with open(path, "w") as f:
        f.write(content)
    print(f"[PATCH] imports.c: Android-Stubs hinzugefuegt ({len(orig)} -> {len(content)} bytes)")
PYEOF

# ------------------------------------------------------------
# 6. Loader bauen
# ------------------------------------------------------------
echo "==> Baue Loader mit aarch64-linux-gnu-gcc"

SRCS=$(ls src/*.c 2>/dev/null)
if [ -z "$SRCS" ]; then
    echo "[FEHLER] Keine Quelldateien in src/ gefunden"
    exit 1
fi
echo "Gefundene Quelldateien:"
echo "$SRCS"

# Compiler-Flags
CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations -Wno-error"
CFLAGS="$CFLAGS -I src"

# Header-Pfade dynamisch suchen
for inc in \
    "/usr/aarch64-linux-gnu/include" \
    "/usr/aarch64-linux-gnu/include/SDL2" \
    "/usr/include/SDL2" \
    "/usr/include/freetype2" \
    "/usr/aarch64-linux-gnu/include/freetype2" \
    "/usr/include/aarch64-linux-gnu/freetype2"; do
    if [ -d "$inc" ]; then
        CFLAGS="$CFLAGS -I $inc"
    fi
done

# Linker-Flags
LDFLAGS="-L/usr/aarch64-linux-gnu/lib"
LDFLAGS="$LDFLAGS -lSDL2 -lGLESv2 -lEGL -lfreetype -ldl -lm -lpthread -lstdc++ -lgcc_s"

# Build
echo "==> Kompiliere $SRCS"
set +e
aarch64-linux-gnu-gcc $CFLAGS -o chrono $SRCS $LDFLAGS 2>&1 | tail -40
BUILD_STATUS=${PIPESTATUS[0]}
set -e

if [ ! -f chrono ] || [ "$BUILD_STATUS" -ne 0 ]; then
    echo "[FEHLER] Loader-Binary wurde nicht erstellt (Status: $BUILD_STATUS)"
    echo "=== Diagnose ==="
    for src in $SRCS; do
        echo "--- Kompiliere: $src ---"
        aarch64-linux-gnu-gcc $CFLAGS -c "$src" -o /tmp/test.o 2>&1 | head -5
    done
    exit 1
fi

echo "[OK] Loader gebaut:"
file chrono
ls -la chrono

cd ../..

# ------------------------------------------------------------
# 7. Port-Paket schnueren
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
================================

Technik: Chrono-Trigger-Loader (so_util + jni_shim + imports)
GPU: Mali-G31 (Bifrost) via Panfrost/Mesa

Installation:
1. Kopiere den Ordner "DeadEffect" nach /roms/ports/
2. Kopiere deine Dead Effect .so-Dateien nach:
   /roms/ports/DeadEffect/lib/
3. Kopiere deine OBB-Assets nach:
   /roms/ports/DeadEffect/assets/bin/Data/
4. Starte ueber EmulationStation > Ports > Dead Effect

Log: /roms/ports/DeadEffect/log.txt
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo ""
echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo ""
echo "==> Cross-Compile erfolgreich."
