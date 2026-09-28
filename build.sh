
#!/bin/bash
# ============================================================
# Dead Effect Port - Build mit Chrono-Trigger-Loader-Architektur
# GPU-Ziel: Mali-G31 (Bifrost)
#
# WICHTIG:
#   - main.c definiert einige CK_* Konstanten selbst (Enum).
#   - Wir setzen NUR die fehlenden Konstanten, mit #ifndef-Guards.
#   - android/log.h wird systemweit bereitgestellt.
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
# 2. Cross-Toolchain
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
echo "==> Erstelle android/log.h systemweit"
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

# ------------------------------------------------------------
# 4. Dead Effect native libs
# ------------------------------------------------------------
echo "==> Lade Dead Effect native libs"
wget -q -O arm64-v8a.zip "$DE_LIBS_URL"
unzip -o arm64-v8a.zip -d de_libs/
find de_libs -name "*.so" -exec ls -la {} \;

# ------------------------------------------------------------
# 5. Chrono-Trigger-Loader-Quellen klonen
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
# 6. main.c minimal patchen (nur Bibliotheksnamen + Package)
# ------------------------------------------------------------
echo "==> Patche main.c minimal"
sed -i 's/libchrono\.so/libmain.so/g' src/main.c
sed -i 's/libc++_shared\.so/libunity.so/g' src/main.c
sed -i 's/libencrypt\.so/libil2cpp.so/g' src/main.c
sed -i 's/com\.square_enix\.android_googleplay\.chrono_trigger/com.bulkypix.deadeffect/g' src/main.c

# ------------------------------------------------------------
# 7. Compiler-Flags
# ------------------------------------------------------------
CFLAGS="-D_GNU_SOURCE -O2 -fPIC -fno-omit-frame-pointer -rdynamic"
CFLAGS="$CFLAGS -Wno-int-conversion -Wno-incompatible-pointer-types"
CFLAGS="$CFLAGS -Wno-implicit-function-declaration -Wno-pointer-sign"
CFLAGS="$CFLAGS -Wno-deprecated-declarations -Wno-error"
CFLAGS="$CFLAGS -I src"

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

# ------------------------------------------------------------
# 8. Automatische Extraktion der fehlenden Konstanten
# ------------------------------------------------------------
echo "==> Identifiziere fehlende CK_*-Konstanten"

# Kompiliere main.c ohne Extra-Header und sammle Fehler
set +e
aarch64-linux-gnu-gcc $CFLAGS -c src/main.c -o /tmp/main.o 2> /tmp/main_errors.txt
set -e

# Extrahiere undeclared identifiers aus den Fehlern
MISSING=$(grep -oP "error: '\K[A-Z_][A-Z0-9_]*(?=' undeclared)" /tmp/main_errors.txt | sort -u)

if [ -n "$MISSING" ]; then
    echo "Fehlende Konstanten:"
    echo "$MISSING"

    # Generiere cocos_extra.h mit #ifndef-Guards
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

    cat >> src/cocos_extra.h <<'HEADEREOF'

#endif
HEADEREOF

    echo "==> cocos_extra.h erstellt"
    cat src/cocos_extra.h
else
    echo "==> Keine fehlenden Konstanten gefunden"
    echo "" > src/cocos_extra.h
fi

# ------------------------------------------------------------
# 9. Alle Quelldateien kompilieren
# ------------------------------------------------------------
echo "==> Kompiliere alle Quelldateien"

# Objekt-Dateien sammeln
OBJS=""
for src in src/*.c; do
    # main.c bekommt den Extra-Header
    if [ "$(basename "$src")" = "main.c" ]; then
        aarch64-linux-gnu-gcc $CFLAGS -include src/cocos_extra.h -c "$src" -o "/tmp/$(basename "$src").o" 2>&1 | tail -20
    else
        aarch64-linux-gnu-gcc $CFLAGS -c "$src" -o "/tmp/$(basename "$src").o" 2>&1 | tail -5
    fi
    OBJS="$OBJS /tmp/$(basename "$src").o"
done

# ------------------------------------------------------------
# 10. Linken
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
# 11. Port-Paket schnueren
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
Technik: Chrono-Trigger-Loader
Installation: /roms/ports/DeadEffect/
READMEEOF

cd port && zip -r ../DeadEffect-Port.zip . > /dev/null
cd ..

echo "=== ZIP-Inhalt ==="
unzip -l DeadEffect-Port.zip
echo "==> Build erfolgreich."
