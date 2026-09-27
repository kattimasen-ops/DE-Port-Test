#!/bin/bash
# analyze_apk.sh
set -e

echo "=== Entpacke XAPK ==="
unzip -o dead-effect.xapk -d xapk_extracted

# Finde die Haupt-APK (die größte .apk-Datei)
MAIN_APK=$(find xapk_extracted -name "*.apk" -type f -printf '%s %p\n' | sort -rn | head -n1 | awk '{print $2}')
echo "Haupt-APK: $MAIN_APK"

echo "=== Entpacke Haupt-APK ==="
unzip -o "$MAIN_APK" -d apk_extracted

echo "=== Analysiere native Bibliotheken ==="
for ABI in arm64-v8a armeabi-v7a; do
    LIB_DIR="apk_extracted/lib/$ABI"
    if [ -d "$LIB_DIR" ]; then
        echo "--- $ABI ---"
        ls -la "$LIB_DIR"
        
        # Prüfe auf Unity-Hauptbibliothek
        if [ -f "$LIB_DIR/libunity.so" ]; then
            echo "Unity-Bibliothek gefunden. Prüfe Abhängigkeiten:"
            readelf -d "$LIB_DIR/libunity.so" | grep NEEDED || true
            
            echo "Suche nach Unity-Version:"
            strings "$LIB_DIR/libunity.so" | grep -i "unity version" | head -5 || true
        fi
        
        # Prüfe auf Mono oder IL2CPP
        if [ -f "$LIB_DIR/libmono.so" ]; then
            echo "-> Mono-Backend gefunden (einfacher zu portieren)."
        elif [ -f "$LIB_DIR/libil2cpp.so" ]; then
            echo "-> IL2CPP-Backend gefunden (komplexer, aber Standard)."
        fi
    fi
done
