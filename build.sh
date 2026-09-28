#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Chrono-Trigger-Loader-Architektur
# (so_util + jni_shim + imports + opensles_shim)
#
# WICHTIG:
#   - Der Chrono-Trigger-Port hat kein Makefile und verwendet
#     einen NextOS-libreelec-Toolchain, den wir nicht haben.
#   - Wir klonen nur die Quelldateien und kompilieren sie mit
#     unserem eigenen aarch64-linux-gnu-gcc.
#   - GPU-Ziel: Mali-G31 (Bifrost) statt Mali-450 (Utgard).
#   - FreeType-Header liegen unter /usr/include/freetype2/.
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

# Zeige wo freetype-Header liegen (für Diagnose)
echo "=== FreeType-Header-Suche ==="
find /usr -name "ft2build.h" 2>/dev/null | head -5
echo "=== SDL2-Header-Suche ==="
find /usr -name "SDL.h" -path "*SDL2*" 2>/dev/null | head -5

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
# 5. Loader mit unserem Cross-Compiler bauen
# ------------------------------------------------------------
echo "==> Baue Loader mit aarch64-linux-gnu-gcc"

SRCS=$(ls src/*.c 2>/dev/null)
if [ -z "$SRCS" ]; then
    echo "[FEHLER] Keine Quelldateien in src/ gefunden"
    exit 1
fi
echo "Gefundene Quelldateien:"
echo "$SRCS"

# ------------------------------------------------------------
# 5a. Compiler-Flags (mit korrekten Include-Pfaden)
# ------------------------------------------------------------
CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -I src"

# Header-Pfade dynamisch suchen (robust gegen Ubuntu-Versionen)
for inc in \
    "/usr/aarch64-linux-gnu/include" \
    "/usr/aarch64-linux-gnu/include/SDL2" \
    "/usr/include/SDL2" \
    "/usr/include/freetype2" \
    "/usr/aarch64-linux-gnu/include/freetype2" \
    "/usr/include/aarch64-linux-gnu/freetype2" \
    "/usr/include/libxml2"; do
    if [ -d "$inc" ]; then
        CFLAGS="$CFLAGS -I $inc"
        echo "[OK] Include: $inc"
    fi
done

# Bibliotheks-Pfade
LDFLAGS="-L/usr/aarch64-linux-gnu/lib"
LDFLAGS="$LDFLAGS -lSDL2 -lGLESv2 -lEGL -lfreetype -ldl -lm -lpthread -lstdc++ -lgcc_s"

# ------------------------------------------------------------
# 5b. Build
# ------------------------------------------------------------
echo "==> Kompiliere $SRCS"
set +e
aarch64-linux-gnu-gcc $CFLAGS -o chrono $SRCS $LDFLAGS 2>&1 | tail -40
BUILD_STATUS=${PIPESTATUS[0]}
set -e

if [ ! -f chrono ] || [ "$BUILD_STATUS" -ne 0 ]; then
    echo "[FEHLER] Loader-Binary wurde nicht erstellt (Status: $BUILD_STATUS)"
    exit 1
fi

echo "[OK] Loader gebaut:"
file chrono
ls -la chrono

# ------------------------------------------------------------
# 6. Loader für Dead Effect anpassen
# ------------------------------------------------------------
echo "==> Passe Loader für Dead Effect an"

sed -i 's/libchrono\.so/libmain.so/g' src/main.c 2>/dev/null || true
sed -i 's/libc++_shared\.so/libunity.so/g' src/main.c 2>/dev/null || true
sed -i 's/libencrypt\.so/libil2cpp.so/g' src/main.c 2>/dev/null || true
sed -i 's/com\.square_enix\.android_googleplay\.chrono_trigger/com.bulkypix.deadeffect/g' \
    src/*.c src/*.h 2>/dev/null || true

echo "==> Baue Loader mit Dead-Effect-Anpassungen neu"
set +e
aarch64-linux-gnu-gcc $CFLAGS -o chrono $SRCS $LDFLAGS 2>&1 | tail -20
set -e

if [ ! -f chrono ]; then
    echo "[FEHLER] Neu-Bau fehlgeschlagen"
    exit 1
fi

echo "[OK] Loader fuer Dead Effect angepasst:"
file chrono

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
else
    cat > port/DeadEffect/DeadEffect.sh << 'LAUNCHER'
#!/bin/bash
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="./lib:$LD_LIBRARY_PATH"
export SDL_AUDIODRIVER=alsa
chmod +x deadeffect-loader
exec ./deadeffect-loader "$(pwd)"
LAUNCHER
    chmod +x port/DeadEffect/DeadEffect.sh
fi

if [ -f /work/de_wrapper.gptk ]; then
    cp /work/de_wrapper.gptk port/DeadEffect/
fi

cat > port/DeadEffect/README.txt << 'READMEEOF'
Dead Effect - PortMaster-Port
================================

Technik: so_util + jni_shim + imports + opensles_shim
Framework: Chrono-Trigger-Loader-Architektur (Linux ARM64)
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
