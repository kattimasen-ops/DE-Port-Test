#!/bin/bash
# ============================================================
# Dead Effect Port - Unity-IL2CPP Loader
# Basis: Phigros NX (MIT) -> Linux ARM64 Portierung
#
# Ersetzte Switch-APIs:
#   svcMapProcessCodeMemory  -> mprotect(PROT_READ|PROT_EXEC)
#   armDCacheFlush/ICacheInvalidate -> __builtin___clear_cache()
#   libnx fatal_error        -> fprintf(stderr) + exit(1)
#   sdmc:/switch/...         -> /roms/ports/DeadEffect/...
# ============================================================
set -e

export DEBIAN_FRONTEND=noninteractive

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
  libasound2-dev:arm64 libpulse-dev:arm64 \
  libegl1-mesa-dev:arm64 libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 libgbm-dev:arm64 \
  libfreetype6-dev:arm64 libsdl2-dev:arm64 \
  zlib1g-dev:arm64

aarch64-linux-gnu-gcc --version | head -1

# ------------------------------------------------------------
# 3. Linux-Kompatibilitaets-Header fuer Switch-APIs
# ------------------------------------------------------------
echo "==> Erstelle Linux-Kompatibilitaets-Header"

# switch.h Stub: libnx -> Linux Mapping
mkdir -p /usr/include/switch
cat > /usr/include/switch.h <<'EOF'
#ifndef COMPAT_SWITCH_H
#define COMPAT_SWITCH_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

typedef int Result;
typedef uint64_t Handle;
typedef uint32_t u32;
typedef uint64_t u64;

#define R_FAILED(rc)  ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)

static inline void armDCacheFlush(void *addr, size_t size) {
    __builtin___clear_cache((char *)addr, (char *)addr + size);
}
static inline void armICacheInvalidate(void *addr, size_t size) {
    __builtin___clear_cache((char *)addr, (char *)addr + size);
}
static inline Result svcMapProcessCodeMemory(Handle h, u64 dst, u64 src, u64 sz) {
    return (mprotect((void *)dst, sz, PROT_READ | PROT_EXEC) == 0) ? 0 : 1;
}
static inline Result svcSetProcessMemoryPermission(Handle h, u64 addr, u64 sz, int perm) {
    return (mprotect((void *)addr, sz, perm) == 0) ? 0 : 1;
}
static inline Handle envGetOwnProcessHandle(void) { return 0; }
static inline void svcSleepThread(u64 ns) { usleep(ns / 1000); }
static inline int appletGetOperationMode(void) { return 0; }
#define AppletOperationMode_Console 0
#define AppletOperationMode_Handheld 1

static inline void fatal_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "FATAL: "); vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n"); va_end(ap); exit(1);
}
#endif
EOF

# android/log.h
mkdir -p /usr/include/android
cat > /usr/include/android/log.h <<'EOF'
#ifndef COMPAT_ANDROID_LOG_H
#define COMPAT_ANDROID_LOG_H
#include <stdio.h>
#include <stdarg.h>
#define ANDROID_LOG_INFO  4
#define ANDROID_LOG_ERROR 6
static inline int __android_log_print(int p, const char *t, const char *f, ...) {
    va_list ap; va_start(ap, f);
    fprintf(stderr, "[%s] ", t ? t : "?"); int r = vfprintf(stderr, f, ap);
    fprintf(stderr, "\n"); va_end(ap); return r;
}
static inline int __android_log_vprint(int p, const char *t, const char *f, va_list ap) {
    fprintf(stderr, "[%s] ", t ? t : "?"); int r = vfprintf(stderr, f, ap);
    fprintf(stderr, "\n"); return r;
}
static inline int __android_log_write(int p, const char *t, const char *s) {
    fprintf(stderr, "[%s] %s\n", t ? t : "?", s ? s : ""); return 0;
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
# 5. Phigros NX klonen (Unity-IL2CPP-Wrapper, MIT)
# ------------------------------------------------------------
echo "==> Klone Phigros NX"
git clone --depth=1 https://github.com/ChanseyIsTheBest/phigros_nx.git phigros-src
cp -r phigros-src/source phigros_source
cd phigros_source

# ------------------------------------------------------------
# 6. Phigros-spezifische Konstanten -> Dead Effect
# ------------------------------------------------------------
echo "==> Passe Konstanten fuer Dead Effect an"

# Phigros -> Dead Effect
grep -rl "phigros\|Phigros\|PHIGROS" . | while read f; do
    sed -i 's/phigros/deadeffect/g; s/Phigros/DeadEffect/g; s/PHIGROS/DEAD_EFFECT/g' "$f"
done

# Package-Name
grep -rl "com.PigeonGames.Phigros\|com.pigeongames.phigros" . | while read f; do
    sed -i 's/com\.PigeonGames\.Phigros/com.bulkypix.deadeffect/g' "$f"
    sed -i 's/com\.pigeongames\.phigros/com.bulkypix.deadeffect/g' "$f"
done

# Pfade
sed -i 's|sdmc:/switch/phigros|/roms/ports/DeadEffect|g' *.c *.h 2>/dev/null || true
sed -i 's|"/switch/phigros"|"/roms/ports/DeadEffect"|g' *.c *.h 2>/dev/null || true

echo "[OK] Konstanten angepasst"

# ------------------------------------------------------------
# 7. Switch-spezifische Includes und Funktionen patchen
# ------------------------------------------------------------
echo "==> Patche Switch-spezifische Teile"

# 7a. switch.h-Includes entfernen (unser compat-Header wird global eingebunden)
grep -rl '#include <switch' . | while read f; do sed -i '/#include <switch/d' "$f"; done
grep -rl '#include <switch/' . | while read f; do sed -i '/#include <switch\//d' "$f"; done

# 7b. Pad/HID-Funktionen neutralisieren
sed -i 's/padInitializeDefault(.*);/\/\* [COMPAT] pad disabled \*\//g' *.c 2>/dev/null || true
sed -i 's/hidScanInput();/\/\* [COMPAT] hid disabled \*\//g' *.c 2>/dev/null || true
sed -i 's/padUpdate(.*);/\/\* [COMPAT] pad disabled \*\//g' *.c 2>/dev/null || true

# 7c. newlib devoptab neutralisieren
sed -i 's/devoptab_[a-z_]*/NULL/g' *.c 2>/dev/null || true

echo "[OK] Switch-Teile gepatcht"

# ------------------------------------------------------------
# 8. Kompilieren
# ------------------------------------------------------------
echo "==> Kompiliere Unity-IL2CPP-Loader"

CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations -Wno-error -Wno-format"
CFLAGS="$CFLAGS -Wno-unused-variable -Wno-unused-function"
CFLAGS="$CFLAGS -I . -I /usr/include"

for inc in \
    "/usr/aarch64-linux-gnu/include" \
    "/usr/aarch64-linux-gnu/include/SDL2" \
    "/usr/include/SDL2" \
    "/usr/include/freetype2" \
    "/usr/aarch64-linux-gnu/include/freetype2"; do
    [ -d "$inc" ] && CFLAGS="$CFLAGS -I $inc"
done

LDFLAGS="-L/usr/aarch64-linux-gnu/lib"
LDFLAGS="$LDFLAGS -lSDL2 -lGLESv2 -lEGL -lfreetype -ldl -lm -lpthread -lstdc++ -lgcc_s"

SRCS=$(ls *.c 2>/dev/null)
echo "Quelldateien:"; echo "$SRCS"

OBJS=""
for src in $SRCS; do
    OBJ="/tmp/$(basename "$src" .c).o"
    echo "--- Kompiliere: $src ---"
    set +e
    aarch64-linux-gnu-gcc $CFLAGS -c "$src" -o "$OBJ" 2>&1 | head -10
    STATUS=${PIPESTATUS[0]}
    set -e
    [ "$STATUS" -ne 0 ] && echo "[WARN] $src: Kompilierung fehlgeschlagen"
    OBJS="$OBJS $OBJ"
done

# Linken
echo "==> Linke Loader"
set +e
aarch64-linux-gnu-gcc -o deadeffect-loader $OBJS $LDFLAGS 2>&1 | tail -20
LINK_STATUS=${PIPESTATUS[0]}
set -e

if [ ! -f deadeffect-loader ]; then
    echo "[FEHLER] Linken fehlgeschlagen (Status: $LINK_STATUS)"
    exit 1
fi

echo "[OK] Loader gebaut:"
file deadeffect-loader
ls -la deadeffect-loader

cd ..

# ------------------------------------------------------------
# 9. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/assets

cp phigros_source/deadeffect-loader port/DeadEffect/
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
Technik: Phigros-NX Loader (Unity-IL2CPP)
Ziel: M9 Pro / R36S (RK3326)
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo "==> Build erfolgreich."
