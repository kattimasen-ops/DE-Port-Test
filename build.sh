#!/bin/bash
# build.sh – Cross-Compile des Wrappers fuer aarch64
set -e

echo "=== DE-Port-Test Wrapper Build ==="

# --- Verzeichnisse ---
BUILD_DIR="build_aarch64"
OUTPUT_DIR="output"
mkdir -p "$BUILD_DIR" "$OUTPUT_DIR"

# --- CMake konfigurieren ---
echo "Konfiguriere CMake..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_BUILD_TYPE=Release

# --- Bauen ---
echo "Baue Wrapper..."
cmake --build "$BUILD_DIR" -j$(nproc)

# --- Kopiere Ergebnis ---
cp "$BUILD_DIR/de_wrapper" "$OUTPUT_DIR/"
echo "Wrapper: $OUTPUT_DIR/de_wrapper"
file "$OUTPUT_DIR/de_wrapper"

# --- Verifiziere Architektur ---
if file "$OUTPUT_DIR/de_wrapper" | grep -q "ARM aarch64"; then
    echo "[OK] Wrapper ist fuer aarch64 kompiliert."
else
    echo "[FEHLER] Wrapper ist NICHT fuer aarch64!"
    exit 1
fi

echo "=== Build abgeschlossen ==="
