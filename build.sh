#!/bin/bash
# ============================================================
# Dead Effect Wrapper - Cross-Compile fuer RK3326 / ARM64
# Laeuft im ubuntu:20.04 Container (GLIBC 2.31)
#
# WICHTIG:
#   - mcpelauncher-linker MUSS mit Clang gebaut werden (AOSP
#     ist Clang-only, GCC scheitert an C11 _Atomic).
#   - Clang braucht den GCC-C++-Include-Pfad fuer bits/c++config.h.
#   - Der Wrapper wird mit GCC + LTO + Cortex-A35 gebaut.
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
# 2. Cross-Toolchain + Clang + ARM64-Zielbibliotheken
# ------------------------------------------------------------
echo "==> Installiere Cross-Toolchain (GCC + Clang)"
apt-get install -y --no-install-recommends \
  build-essential cmake git pkg-config ca-certificates wget file zip unzip python3 \
  crossbuild-essential-arm64 \
  clang-12 lld-12 llvm-12 \
  libc6-dev-arm64-cross \
  libasound2-dev:arm64 \
  libpulse-dev:arm64 \
  libegl1-mesa-dev:arm64 \
  libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 \
  libgbm-dev:arm64 \
  zlib1g-dev:arm64

echo "=== GCC ==="
aarch64-linux-gnu-gcc --version | head -1
echo "=== Clang ==="
clang-12 --version | head -1

# ------------------------------------------------------------
# 3. Native Bibliotheken herunterladen
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
unzip -o arm64-v8a.zip -d de_libs/
echo "=== Enthaltene Bibliotheken ==="
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 4. mcpelauncher-linker mit CLANG bauen
#    FIX: Clang braucht den GCC-C++-Include-Pfad fuer
#    bits/c++config.h. Dieser liegt unter
#    /usr/aarch64-linux-gnu/include/c++/9/aarch64-linux-gnu
# ------------------------------------------------------------
echo "==> Baue mcpelauncher-linker mit Clang"

# Include-Pfad ermitteln (verschiedene GCC-Versionen moeglich)
GCC_CXX_INCLUDE=""
for ver in 9 10 11 12; do
    if [ -d "/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu" ]; then
        GCC_CXX_INCLUDE="/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu"
        break
    fi
done

if [ -z "$GCC_CXX_INCLUDE" ]; then
    echo "[FEHLER] GCC-C++-Include-Pfad nicht gefunden!"
    echo "Verfuegbare Pfade:"
    ls -la /usr/aarch64-linux-gnu/include/c++/ 2>/dev/null || true
    exit 1
fi
echo "[OK] GCC-C++-Include: $GCC_CXX_INCLUDE"

LINKER_BUILT=0
if git clone --depth=1 --recursive https://github.com/minecraft-linux/mcpelauncher-linker.git 2>/dev/null; then
    cd mcpelauncher-linker
    mkdir -p build && cd build

    cmake .. \
      -DCMAKE_C_COMPILER=clang-12 \
      -DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu \
      -DCMAKE_CXX_COMPILER=clang++-12 \
      -DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu \
      -DCMAKE_SYSTEM_NAME=Linux \
      -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DCMAKE_C_FLAGS="-isystem $GCC_CXX_INCLUDE" \
      -DCMAKE_CXX_FLAGS="-isystem $GCC_CXX_INCLUDE" \
      -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld-12" \
      -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld-12"

    make -j$(nproc)
    cd ../..
    echo "[OK] mcpelauncher-linker mit Clang gebaut"
    LINKER_BUILT=1
else
    echo "[WARN] mcpelauncher-linker konnte nicht geklont werden"
fi

# ------------------------------------------------------------
# 5. Wrapper mit GCC + LTO + Cortex-A35 bauen
# ------------------------------------------------------------
echo "==> Baue DE Wrapper mit GCC + LTO + Cortex-A35"
rm -rf build_aarch64 output
mkdir -p build_aarch64 output

cmake -B build_aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_AR=aarch64-linux-gnu-gcc-ar \
  -DCMAKE_C_COMPILER_RANLIB=aarch64-linux-gnu-gcc-ranlib \
  -DCMAKE_CXX_COMPILER_AR=aarch64-linux-gnu-gcc-ar \
  -DCMAKE_CXX_COMPILER_RANLIB=aarch64-linux-gnu-gcc-ranlib

cmake --build build_aarch64 -j$(nproc)

cp build_aarch64/de_wrapper output/
echo "=== Wrapper-Info ==="
file output/de_wrapper
readelf -p .comment output/de_wrapper 2>/dev/null | head -5
ls -la output/

# ------------------------------------------------------------
# 6. Port-Paket schnueren (mit start.sh + gptk)
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
rm -rf port
mkdir -p port/DeadEffect/lib

cp output/de_wrapper port/DeadEffect/

if [ -d de_libs/arm64-v8a ]; then
    cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true
else
    cp de_libs/*.so port/DeadEffect/lib/ 2>/dev/null || true
fi

if [ "$LINKER_BUILT" = "1" ]; then
    find mcpelauncher-linker/build -name "*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true
fi

# start.sh aus dem Repo kopieren (PortMaster-integriert)
if [ -f /work/start.sh ]; then
    cp /work/start.sh port/DeadEffect/DeadEffect.sh
    chmod +x port/DeadEffect/DeadEffect.sh
else
    echo "[WARN] start.sh nicht gefunden – erstelle Fallback"
    cat > port/DeadEffect/DeadEffect.sh << 'LAUNCHER'
#!/bin/bash
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="./lib:$LD_LIBRARY_PATH"
export SDL_AUDIODRIVER=alsa
chmod +x de_wrapper
exec ./de_wrapper "$@"
LAUNCHER
    chmod +x port/DeadEffect/DeadEffect.sh
fi

# gptk-Datei kopieren
if [ -f /work/de_wrapper.gptk ]; then
    cp /work/de_wrapper.gptk port/DeadEffect/
fi

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - Native Wrapper fuer M9 Pro / R36S
================================================

Build: Cortex-A35 + LTO + O3 optimiert (Wrapper mit GCC)
Linker: mcpelauncher-linker (mit Clang-12 gebaut)
GLIBC: 2.31 (kompatibel mit ArkOS)

Installation:
1. Kopiere den Ordner "DeadEffect" nach /roms/ports/
2. Kopiere deine Dead Effect OBB-Daten nach:
   /roms/ports/DeadEffect/assets/
3. Starte ueber EmulationStation > Ports > Dead Effect
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo ""
echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip

echo ""
echo "==> Cross-Compile erfolgreich."
