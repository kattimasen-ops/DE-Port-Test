#!/bin/bash
# ============================================================
# Dead Effect Wrapper - Cross-Compile fuer RK3326 / ARM64
# Laeuft im ubuntu:20.04 Container (GLIBC 2.31)
# Optimiert fuer Cortex-A35 mit LTO
# ============================================================
set -e

export DEBIAN_FRONTEND=noninteractive

echo "==> Host arch: $(uname -m)"
echo "==> Ziel: M9 Pro / R36S (RK3326, aarch64, GLIBC 2.31)"
echo "==> Optimierung: Cortex-A35 + LTO + O3"

# ------------------------------------------------------------
# 1. Multiarch + apt-Quellen
#    WICHTIG: arm64 kommt von ports.ubuntu.com
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
# 2. Cross-Toolchain + ARM64-Zielbibliotheken
#    unzip ist jetzt enthalten!
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
  zlib1g-dev:arm64

which aarch64-linux-gnu-gcc
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
# 4. mcpelauncher-linker bauen (Bionic-ELF-Loader)
# ------------------------------------------------------------
echo "==> Baue mcpelauncher-linker"
LINKER_BUILT=0
if git clone --depth=1 --recursive https://github.com/minecraft-linux/mcpelauncher-linker.git 2>/dev/null; then
    cd mcpelauncher-linker
    mkdir -p build && cd build
    cmake .. \
      -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
      -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
      -DCMAKE_SYSTEM_NAME=Linux \
      -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON
    make -j$(nproc)
    cd ../..
    echo "[OK] mcpelauncher-linker gebaut"
    LINKER_BUILT=1
else
    echo "[WARN] mcpelauncher-linker konnte nicht geklont werden"
fi

# ------------------------------------------------------------
# 5. Wrapper bauen mit LTO + Cortex-A35
# ------------------------------------------------------------
echo "==> Baue DE Wrapper mit LTO und Cortex-A35-Optimierung"
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
# 6. Port-Paket schnueren
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

# start.sh mit Performance-Tuning (systemseitig bereits gesetzt, hier nur Mesa/Umgebung)
cat > port/DeadEffect/start.sh << 'LAUNCHER'
#!/bin/bash
cd "$(dirname "$0")"

# --- Mesa/Panfrost (Mali-G31) ---
export MESA_GL_VERSION_OVERRIDE=3.1
export MESA_GLES_VERSION_OVERRIDE=3.2
export PAN_MESA_DEBUG=noaff,deqp
export MESA_GLSL_CACHE_DISABLE=0
export MESA_GLSL_CACHE_DIR="./.mesa_cache"

# --- VSync aus, vblank aus ---
export vblank_mode=0
export SDL_RENDER_VSYNC=0

# --- Audio (RK3326) ---
export SDL_AUDIODRIVER=alsa

# --- Bibliothekspfad ---
export LD_LIBRARY_PATH="./lib:$LD_LIBRARY_PATH"

chmod +x de_wrapper
exec ./de_wrapper "$@"
LAUNCHER
chmod +x port/DeadEffect/start.sh

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - Native Wrapper fuer M9 Pro / R36S
================================================

Build: Cortex-A35 + LTO + O3 optimiert
GLIBC: 2.31 (kompatibel mit ArkOS)

Installation:
1. Kopiere den Ordner "DeadEffect" nach /roms/ports/
2. Kopiere deine Dead Effect OBB-Daten nach:
   /roms/ports/DeadEffect/assets/
3. Starte ueber EmulationStation > Ports > Dead Effect
READMEEOF

cd port && zip -r ../DeadEffect-Wrapper.zip . > /dev/null
cd ..

echo ""
echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Wrapper.zip

echo ""
echo "==> Cross-Compile erfolgreich."
