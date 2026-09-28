#!/bin/bash
# ============================================================
# Dead Effect Port - Unity-IL2CPP Loader
# Basis: Chrono-Trigger (ELF-Loader) + Phigros NX (JNI-Module)
# Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)
#
# Diese build.sh integriert:
#   - Chrono-Trigger: so_util.c (AArch64-ELF-Loader)
#   - Phigros NX: android_native_unity.c, jni_fake.c, unity_jni.c,
#                 libc_shim.c, opensles.c
#   - Unity-Entry-Points: Dynamische Extraktion aus libunity.so
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
# 3. android/log.h systemweit bereitstellen
# ------------------------------------------------------------
echo "==> Erstelle android/log.h"
mkdir -p /usr/include/android
cat > /usr/include/android/log.h <<'EOF'
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
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap);
    return r;
}
static inline int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    int r = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); return r;
}
static inline int __android_log_write(int prio, const char* tag, const char* text) {
    fprintf(stderr, "[%s] %s\n", tag ? tag : "?", text ? text : "");
    return 0;
}
#endif
EOF

# ------------------------------------------------------------
# 4. Dead Effect native libs
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
unzip -o arm64-v8a.zip -d de_libs/
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 5. Chrono-Trigger-Loader-Quellen klonen (ELF-Loader-Basis)
# ------------------------------------------------------------
echo "==> Klone Chrono-Trigger-Loader"
if ! git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null; then
    git clone --depth=1 https://github.com/windstarry/portmaster_chrono.git chrono-src 2>/dev/null || {
        echo "[FEHLER] Chrono-Trigger-Repo nicht erreichbar"; exit 1
    }
fi
cd chrono-src/chrono
ls -la src/

# ------------------------------------------------------------
# 6. Phigros-NX-Module klonen (JNI-Infrastruktur)
# ------------------------------------------------------------
echo "==> Klone Phigros-NX (JNI-Module)"
git clone --depth=1 https://github.com/ChanseyIsTheBest/phigros_nx.git phigros-src

# ------------------------------------------------------------
# 7. main.c: Entry-Points auf Unity umstellen
# ------------------------------------------------------------
echo "==> Passe main.c fuer Unity an"
python3 - <<'PYEOF'
import os
path = "src/main.c"
with open(path) as f:
    content = f.read()
orig = content

# Bibliotheksnamen
content = content.replace('libchrono.so', 'libmain.so')
content = content.replace('libc++_shared.so', 'libunity.so')
content = content.replace('libencrypt.so', 'libil2cpp.so')
content = content.replace(
    'com.square_enix.android_googleplay.chrono_trigger',
    'com.bulkypix.deadeffect'
)

# Cocos2d-Entry-Points durch Unity ersetzen (in den Kommentaren/Decls)
content = content.replace(
    'Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext',
    'JNI_OnLoad_Unity'
)
content = content.replace(
    'Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit',
    'UnityPlayer_initJNI'
)
content = content.replace(
    'Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender',
    'UnityPlayer_nativeRender'
)

if content != orig:
    with open(path, "w") as f:
        f.write(content)
    print(f"[PATCH] main.c: Unity-Entry-Points gesetzt")
PYEOF

# ------------------------------------------------------------
# 8. Unity-JNI-Module aus Phigros NX integrieren
# ------------------------------------------------------------
echo "==> Integriere Phigros-NX JNI-Module"

# 8a. android_native_unity.c: 27 NDK-Symbole (Linux-adaptiert)
cp ../phigros-src/source/android_native_unity.c src/phigros_android_native.c 2>/dev/null || true
cp ../phigros-src/source/android_native_unity.h src/phigros_android_native.h 2>/dev/null || true

# Ersetze Switch-spezifische Includes durch Linux
sed -i 's/#include <switch.h>//' src/phigros_android_native.c 2>/dev/null || true
sed -i 's/appletGetOperationMode()/0/g' src/phigros_android_native.c 2>/dev/null || true
sed -i 's/AppletOperationMode_Console/1/g' src/phigros_android_native.c 2>/dev/null || true
sed -i 's/armDCacheFlush/0/g' src/phigros_android_native.c 2>/dev/null || true
sed -i 's/armICacheInvalidate/0/g' src/phigros_android_native.c 2>/dev/null || true
sed -i 's/svcMapProcessCodeMemory/0/g' src/phigros_android_native.c 2>/dev/null || true

# 8b. jni_fake.c: Fake-JNI-Umgebung
cp ../phigros-src/source/jni_fake.c src/phigros_jni_fake.c 2>/dev/null || true
cp ../phigros-src/source/jni_fake.h src/phigros_jni_fake.h 2>/dev/null || true

# 8c. unity_jni.c: Unity-spezifische JNI-Handler
cp ../phigros-src/source/unity_jni.c src/phigros_unity_jni.c 2>/dev/null || true
cp ../phigros-src/source/unity_jni.h src/phigros_unity_jni.h 2>/dev/null || true

# 8d. libc_shim.c: Bionic-_chk-Wrapper
cp ../phigros-src/source/libc_shim.c src/phigros_libc_shim.c 2>/dev/null || true
cp ../phigros-src/source/libc_shim.h src/phigros_libc_shim.h 2>/dev/null || true

# 8e. opensles.c: OpenSL-ES-Shim
cp ../phigros-src/source/opensles.c src/phigros_opensles.c 2>/dev/null || true
cp ../phigros-src/source/opensles.h src/phigros_opensles.h 2>/dev/null || true

# 8f. unity_entrypoints.h
cp ../phigros-src/source/unity_entrypoints.h src/phigros_unity_entrypoints.h 2>/dev/null || true

# ------------------------------------------------------------
# 9. Automatische Extraktion der fehlenden CK_*-Konstanten
# ------------------------------------------------------------
echo "==> Identifiziere fehlende CK_*-Konstanten"

CFLAGS_BASE="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS_BASE="$CFLAGS_BASE -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS_BASE="$CFLAGS_BASE -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS_BASE="$CFLAGS_BASE -Wno-deprecated-declarations -Wno-error"
CFLAGS_BASE="$CFLAGS_BASE -I src -I src/compat"

for inc in \
    "/usr/aarch64-linux-gnu/include" \
    "/usr/aarch64-linux-gnu/include/SDL2" \
    "/usr/include/SDL2" \
    "/usr/include/freetype2" \
    "/usr/aarch64-linux-gnu/include/freetype2"; do
    [ -d "$inc" ] && CFLAGS_BASE="$CFLAGS_BASE -I $inc"
done

# Pass 1: Fehler sammeln
set +e
aarch64-linux-gnu-gcc $CFLAGS_BASE -c src/main.c -o /tmp/main.o 2> /tmp/main_errors.txt
set -e

MISSING=$(grep -oP "error: '\K[A-Z_][A-Z0-9_]*(?=' undeclared)" /tmp/main_errors.txt | sort -u)

if [ -n "$MISSING" ]; then
    echo "Fehlende Konstanten:"
    echo "$MISSING"
    cat > src/cocos_extra.h <<'HEADEREOF'
/* Auto-generiert: fehlende Cocos2d-Konstanten */
#ifndef COCOS_EXTRA_H
#define COCOS_EXTRA_H
HEADEREOF
    COUNTER=9000
    for CONST in $MISSING; do
        cat >> src/cocos_extra.h <<EOF
#ifndef $CONST
#define $CONST $COUNTER
#endif
EOF
        COUNTER=$((COUNTER + 1))
    done
    echo "#endif" >> src/cocos_extra.h
else
    echo "" > src/cocos_extra.h
fi

# ------------------------------------------------------------
# 10. Alle Quelldateien kompilieren
# ------------------------------------------------------------
echo "==> Kompiliere alle Quelldateien"

OBJS=""
for src in src/*.c; do
    BASENAME=$(basename "$src")
    if [ "$BASENAME" = "main.c" ]; then
        aarch64-linux-gnu-gcc $CFLAGS_BASE -include src/cocos_extra.h -c "$src" -o "/tmp/${BASENAME}.o" 2>&1 | tail -10
    else
        aarch64-linux-gnu-gcc $CFLAGS_BASE -c "$src" -o "/tmp/${BASENAME}.o" 2>&1 | tail -5
    fi
    OBJS="$OBJS /tmp/${BASENAME}.o"
done

# ------------------------------------------------------------
# 11. Linken
# ------------------------------------------------------------
echo "==> Linke Loader"
LDFLAGS="-L/usr/aarch64-linux-gnu/lib"
LDFLAGS="$LDFLAGS -lSDL2 -lGLESv2 -lEGL -lfreetype -ldl -lm -lpthread -lstdc++ -lgcc_s"

aarch64-linux-gnu-gcc -o chrono $OBJS $LDFLAGS

if [ ! -f chrono ]; then
    echo "[FEHLER] Linken fehlgeschlagen"
    exit 1
fi

echo "[OK] Loader gebaut:"
file chrono
ls -la chrono

cd ../..

# ------------------------------------------------------------
# 12. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/assets

cp chrono-src/chrono/chrono port/DeadEffect/deadeffect-loader
chmod +x port/DeadEffect/deadeffect-loader

cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

# start.sh aus dem Repo
if [ -f /work/start.sh ]; then
    cp /work/start.sh port/DeadEffect/DeadEffect.sh
    chmod +x port/DeadEffect/DeadEffect.sh
fi

if [ -f /work/de_wrapper.gptk ]; then
    cp /work/de_wrapper.gptk port/DeadEffect/
fi

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port
Technik: Chrono-Trigger ELF-Loader + Phigros NX JNI-Module
Ziel: M9 Pro / R36S (RK3326)
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo "==> Build erfolgreich."
