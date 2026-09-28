#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Bogodroid-Framework
# (droidports + libjnivm + mcpelauncher-linker)
#
# Neue Technik: Wir bauen NICHT unseren eigenen Wrapper,
# sondern verwenden das Bogodroid-Framework, das einen
# fertigen unityloader fuer Unity-Spiele mitbringt.
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
  libzip-dev:arm64 \
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
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 4. Bogodroid-Framework klonen
#    Enthaelt: droidports + libjnivm + mcpelauncher-linker
# ------------------------------------------------------------
echo "==> Klone Bogodroid-Framework"
git clone --depth=1 --recursive https://github.com/binarycounter/bogodroid.git
cd bogodroid

# Submodule initialisieren (libjnivm, mcpelauncher-linker, etc.)
git submodule update --init --recursive

echo "=== Bogodroid-Struktur ==="
ls -la
echo "=== libjnivm ==="
ls -la libjnivm/ 2>/dev/null || echo "libjnivm nicht gefunden"

# ------------------------------------------------------------
# 5. libjnivm mit Clang bauen
#    (fuer Cross-Compile nach aarch64)
# ------------------------------------------------------------
echo "==> Baue libjnivm"

# GCC-C++-Include-Pfad fuer Clang ermitteln
GCC_CXX_INCLUDE=""
for ver in 9 10 11 12; do
    if [ -d "/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu" ]; then
        GCC_CXX_INCLUDE="/usr/aarch64-linux-gnu/include/c++/$ver/aarch64-linux-gnu"
        break
    fi
done

# Toolchain-File fuer Cross-Compile erstellen
cat > /tmp/aarch64-toolchain.cmake <<TOOLCHAIN_EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang-12)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER clang++-12)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TOOLCHAIN_EOF

# libjnivm bauen
mkdir -p libjnivm/build && cd libjnivm/build
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DJNIVM_ENABLE_TRACE=ON \
  -DJNIVM_ENABLE_GC=ON \
  -DJNIVM_ENABLE_DEBUG=ON \
  -DJNIVM_USE_FAKE_JNI_CODEGEN=ON \
  -DCMAKE_C_FLAGS="-isystem $GCC_CXX_INCLUDE" \
  -DCMAKE_CXX_FLAGS="-isystem $GCC_CXX_INCLUDE"
make -j$(nproc)
cd ../..
echo "[OK] libjnivm gebaut"

# ------------------------------------------------------------
# 6. Bogodroid-Wrapper (unityloader) bauen
#    WICHTIG: unityloader ist der fertige Loader fuer Unity-
#    Spiele. Er nutzt droidports (ELF-Loader) + libjnivm (JNI).
# ------------------------------------------------------------
echo "==> Baue unityloader"
mkdir -p build && cd build
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DPROJ=unityloader \
  -DCMAKE_C_FLAGS="-isystem $GCC_CXX_INCLUDE" \
  -DCMAKE_CXX_FLAGS="-isystem $GCC_CXX_INCLUDE"
make -j$(nproc)
cd ..
echo "[OK] unityloader gebaut"

# Suchen wo das Binary liegt
UNITYLOADER_BIN=$(find . -name "unityloader" -type f -executable | head -n1)
echo "[OK] unityloader: $UNITYLOADER_BIN"
file "$UNITYLOADER_BIN"

# ------------------------------------------------------------
# 7. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
cd /work
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/gamefiles/unity

# unityloader ins Port-Verzeichnis
cp "bogodroid/$UNITYLOADER_BIN" port/DeadEffect/unityloader
chmod +x port/DeadEffect/unityloader

# Dead Effect native libs
cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

# libjnivm + mcpelauncher-linker Libs
find bogodroid -name "libjnivm*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true
find bogodroid -name "libmcpelauncher-linker*.so*" -exec cp {} port/DeadEffect/lib/ \; 2>/dev/null || true

# Startscript + TOML-Config
cp /work/start.sh port/DeadEffect/DeadEffect.sh 2>/dev/null || true
cp /work/de_wrapper.gptk port/DeadEffect/ 2>/dev/null || true
cp /work/configs/unity.toml port/DeadEffect/gamefiles/unity/ 2>/dev/null || true

chmod +x port/DeadEffect/DeadEffect.sh 2>/dev/null || true

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port (Bogodroid-Framework)
====================================================

Technik: unityloader (droidports + libjnivm + mcpelauncher-linker)

Installation:
1. Kopiere den Ordner "DeadEffect" nach /roms/ports/
2. Kopiere deine Dead Effect .so-Dateien nach:
   /roms/ports/DeadEffect/lib/
3. Kopiere deine OBB-Assets nach:
   /roms/ports/DeadEffect/gamefiles/unity/assets/
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
