#!/bin/bash
# analyze_apk.sh – Analysiert die nativen IL2CPP-Bibliotheken von Dead Effect
# Erwartet: arm64-v8a.zip im aktuellen Verzeichnis
# Ausgabe:  analysis_output/ mit mehreren Textdateien
set -e

# --- Ausgabe-Ordner für Artefakte ---
OUT_DIR="analysis_output"
mkdir -p "$OUT_DIR"

# --- 1. ZIP-Archiv entpacken ---
echo "=== Entpacke arm64-v8a.zip ==="
if [ ! -f "arm64-v8a.zip" ]; then
    echo "FEHLER: arm64-v8a.zip nicht gefunden!"
    exit 1
fi

unzip -o arm64-v8a.zip -d libs_extracted

echo ""
echo "=== Entpackte Dateien ==="
find libs_extracted -type f -name "*.so" -exec ls -la {} \;

# --- 2. Abhängigkeiten der Hauptbibliotheken prüfen (NEEDED) ---
echo ""
echo "=== Abhängigkeiten (NEEDED) ==="
NEEDED_FILE="$OUT_DIR/needed_libs.txt"
: > "$NEEDED_FILE"

for LIB in libil2cpp.so libunity.so libmain.so; do
    LIB_PATH=$(find libs_extracted -name "$LIB" -type f | head -n1)
    if [ -n "$LIB_PATH" ]; then
        echo "--- $LIB ($LIB_PATH) ---" | tee -a "$NEEDED_FILE"
        readelf -d "$LIB_PATH" 2>/dev/null | grep NEEDED | tee -a "$NEEDED_FILE" || true
        echo "" | tee -a "$NEEDED_FILE"
    fi
done

# --- 3. Alle exportierten Symbole von libunity.so sammeln ---
echo ""
echo "=== Exportierte Symbole (libunity.so) – Auszug ==="
UNITY_PATH=$(find libs_extracted -name "libunity.so" -type f | head -n1)
if [ -n "$UNITY_PATH" ]; then
    readelf --dyn-syms "$UNITY_PATH" 2>/dev/null | grep -E "FUNC|OBJECT" | awk '{print $8}' | grep -v '^$' | sort -u > "$OUT_DIR/libunity_exports.txt"
    echo "Anzahl exportierter Symbole: $(wc -l < "$OUT_DIR/libunity_exports.txt")"
    head -30 "$OUT_DIR/libunity_exports.txt"
fi

# --- 4. JNI-Funktionen in libil2cpp.so suchen ---
echo ""
echo "=== JNI-Funktionen (libil2cpp.so) ==="
IL2CPP_PATH=$(find libs_extracted -name "libil2cpp.so" -type f | head -n1)
if [ -n "$IL2CPP_PATH" ]; then
    readelf --dyn-syms "$IL2CPP_PATH" 2>/dev/null | grep -i "jni" | head -30 | tee "$OUT_DIR/jni_symbols.txt" || true
fi

# --- 5. Unity-Version ermitteln ---
echo ""
echo "=== Unity-Version ==="
if [ -n "$UNITY_PATH" ]; then
    strings "$UNITY_PATH" 2>/dev/null | grep -iE "unity version|20[0-9]{2}\.[0-9]+\.[0-9]+" | head -10 | tee "$OUT_DIR/unity_version.txt" || true
fi

# --- 6. Architektur und ELF-Header prüfen ---
echo ""
echo "=== Architektur und ELF-Header ==="
for LIB in libs_extracted/*.so; do
    echo "--- $(basename "$LIB") ---"
    file "$LIB"
done | tee "$OUT_DIR/architecture.txt"

# --- 7. Backend-Erkennung ---
echo ""
echo "=== Backend-Erkennung ==="
{
    if [ -n "$IL2CPP_PATH" ]; then
        echo "-> IL2CPP-Backend gefunden (Standard, komplexer)"
    fi
    if find libs_extracted -name "libmono.so" -type f | grep -q .; then
        echo "-> Mono-Backend ebenfalls vorhanden"
    fi
} | tee "$OUT_DIR/backend.txt"

# --- 8. Alle benötigten Android-Systembibliotheken sammeln ---
echo ""
echo "=== Zusammenfassung: Alle benötigten Android-System-Libs ==="
grep NEEDED "$NEEDED_FILE" | grep -oP '\[\K[^\]]+' | sort -u | tee "$OUT_DIR/system_libs_required.txt"

echo ""
echo "=== Analyse abgeschlossen. Ergebnisse in $OUT_DIR/ ==="
ls -la "$OUT_DIR/"
