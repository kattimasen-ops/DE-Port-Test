#!/bin/bash
# ============================================================
# Dead Effect Port - ATL-basierter Build
# (android_translation_layer + bionic_translation + libjnivm)
#
# Neue Technik: ATL ist das Fundament. Es baut alle
# Abhängigkeiten in der richtigen Reihenfolge:
#   wolfSSL -> libunwind -> bionic_translation -> art_standalone
#   -> android_translation_layer
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
# 2. Cross-Toolchain + Clang + ATL-Abhängigkeiten
# ------------------------------------------------------------
echo "==> Installiere Cross-Toolchain (GCC + Clang)"
apt-get install -y --no-install-recommends \
  build-essential cmake git pkg-config ca-certificates wget file zip unzip python3 \
  crossbuild-essential-arm64 \
  clang-12 lld-12 llvm-12 \
  libc6-dev-arm64-cross \
  libzip-dev:arm64 \
  libasound2-dev:arm64 \
  libpulse-dev:arm64 \
  libegl1-mesa-dev:arm64 \
  libgles2-mesa-dev:arm64 \
  libdrm-dev:arm64 \
  libgbm-dev:arm64 \
  libssl-dev:arm64 \
  libunwind-dev:arm64 \
  zlib1g-dev:arm64 \
  meson ninja-build

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
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 4. bionic_translation klonen und bauen
#    (wird von ATL für das Laden bionic-gelinkter .so benötigt)
# ------------------------------------------------------------
echo "==> Klone bionic_translation"
git clone --depth=1 https://gitlab.com/android_translation_layer/bionic_translation.git
cd bionic_translation

# Cross-Compile-Toolchain für Meson
cat > /tmp/aarch64-meson.ini <<'EOF'
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-gcc-ar'
strip = 'aarch64-linux-gnu-strip'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

mkdir -p build && cd build
meson setup .. --cross-file /tmp/aarch64-meson.ini
ninja
cd ../..
echo "[OK] bionic_translation gebaut"

# ------------------------------------------------------------
# 5. libjnivm klonen und bauen
#    (JNI-VM, wird von ATL für die Java-Seite benötigt)
# ------------------------------------------------------------
echo "==> Klone libjnivm"
git clone --depth=1 https://github.com/ChristopherHX/libjnivm.git
cd libjnivm

mkdir -p build && cd build
cmake .. \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DJNIVM_ENABLE_TRACE=ON \
  -DJNIVM_USE_FAKE_JNI_CODEGEN=ON
make -j$(nproc)
cd ../..
echo "[OK] libjnivm gebaut"

# ------------------------------------------------------------
# 6. mcpelauncher-linker klonen und bauen
#    (AArch64-ELF-Loader, wird von ATL als Alternative zu
#    bionic_translation verwendet)
# ------------------------------------------------------------
echo "==> Klone mcpelauncher-linker"
git clone --depth=1 --recursive https://github.com/minecraft-linux/mcpelauncher-linker.git
cd mcpelauncher-linker

# GCC-C++-Include-Pfad für Clang ermitteln
GCC_CXX_INCLUDE=""
for ver in 9 10 11 12; do
    if [ -d "/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu" ]; then
        GCC_CXX_INCLUDE="/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu"
        break
    fi
done

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
echo "[OK] mcpelauncher-linker gebaut"

# ------------------------------------------------------------
# 7. ATL klonen und bauen
#    Das einheitliche CMake-Build-System baut alle
#    Abhängigkeiten in der richtigen Reihenfolge.
# ------------------------------------------------------------
echo "==> Klone android_translation_layer"
git clone --depth=1 https://gitlab.com/android_translation_layer/android_translation_layer.git
cd android_translation_layer

# ATL verwendet Meson für den Haupt-Build
mkdir -p build && cd build
meson setup .. --cross-file /tmp/aarch64-meson.ini
ninja
cd ../..
echo "[OK] android_translation_layer gebaut"

# ------------------------------------------------------------
# 8. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/atl

# ATL-Binaries und Bibliotheken
find android_translation_layer/build -name "*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true
find android_translation_layer/build -name "atl-*" -executable -exec cp {} port/DeadEffect/atl/ \; 2>/dev/null || true

# bionic_translation
find bionic_translation/build -name "*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true

# libjnivm
find libjnivm/build -name "*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true

# mcpelauncher-linker
find mcpelauncher-linker/build -name "*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true

# Dead Effect native libs
cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

# Startscript + Config
cp /work/start.sh port/DeadEffect/DeadEffect.sh 2>/dev/null || true
chmod +x port/DeadEffect/DeadEffect.sh 2>/dev/null || true

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port (ATL-basiert)
=============================================

Technik: android_translation_layer + bionic_translation + libjnivm
Framework: mcpelauncher-linker (AArch64-ELF-Loader)

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
