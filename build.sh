#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Chrono-Trigger-Loader-Architektur
# (so_util + jni_shim + imports + main + opensles_shim)
#
# Der Chrono-Trigger-Port ist Linux ARM64 und verwendet die
# gleiche Technik wie NextOS. Er ist Open Source auf Gitee.
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
#    (Linux ARM64, so_util + jni_shim + imports + main + opensles_shim)
# ------------------------------------------------------------
echo "==> Klone Chrono-Trigger-Loader"
git clone --depth=1 https://gitee.com/windstarry/portmaster_chrono.git chrono-src
cd chrono-src

# ------------------------------------------------------------
# 5. Loader für Dead Effect anpassen
# ------------------------------------------------------------
echo "==> Passe Loader für Dead Effect an"

# main.c: Dead Effect-spezifische Bibliotheken
sed -i 's/libchrono\.so/libmain.so/g' chrono/src/main.c 2>/dev/null || true
sed -i 's/libc++_shared\.so/libunity.so/g' chrono/src/main.c 2>/dev/null || true
sed -i 's/libencrypt\.so/libil2cpp.so/g' chrono/src/main.c 2>/dev/null || true

# Package-Name
sed -i 's/com\.square_enix\.android_googleplay\.chrono_trigger/com.bulkypix.deadeffect/g' \
    chrono/src/*.c chrono/src/*.h 2>/dev/null || true

# ------------------------------------------------------------
# 6. Loader bauen
# ------------------------------------------------------------
echo "==> Baue Loader"
cd chrono
make clean 2>/dev/null || true
make 2>&1 | tail -20

# Binary finden
LOADER_BIN=$(find . -name "chrono" -type f -executable | head -n1)
if [ -z "$LOADER_BIN" ]; then
    echo "[FEHLER] Loader-Binary nicht gefunden"
    exit 1
fi
echo "[OK] Loader: $LOADER_BIN"
file "$LOADER_BIN"
cd ..

# ------------------------------------------------------------
# 7. Port-Paket schnueren
# ------------------------------------------------------------
echo "==> Erstelle Port-Paket"
cd /work
rm -rf port
mkdir -p port/DeadEffect/lib
mkdir -p port/DeadEffect/assets

# Loader umbenennen
cp "chrono-src/chrono/$LOADER_BIN" port/DeadEffect/deadeffect-loader
chmod +x port/DeadEffect/deadeffect-loader

# Dead Effect native libs
cp de_libs/arm64-v8a/*.so port/DeadEffect/lib/ 2>/dev/null || true

# Startscript
cp /work/start.sh port/DeadEffect/DeadEffect.sh 2>/dev/null || true
chmod +x port/DeadEffect/DeadEffect.sh 2>/dev/null || true

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port
================================

Technik: so_util + jni_shim + imports + opensles_shim
Framework: Chrono-Trigger-Loader-Architektur (Linux ARM64)

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

echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo "==> Cross-Compile erfolgreich."
