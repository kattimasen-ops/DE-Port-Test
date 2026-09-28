#!/bin/bash
# ============================================================
# fix_libcxx.sh
# Passt libc++_shared.so von Android- auf glibc-Abhaengigkeiten an.
# Idempotent: laeuft beliebig oft ohne Schaden.
#
# Aufruf:  fix_libcxx.sh <pfad-zu-libc++_shared.so> [logfile]
#
# Hintergrund: Android-libc++ hat DT_NEEDED-Eintraege wie
# 'libc.so', 'libm.so', 'libdl.so'. Auf glibc-Systemen existieren
# diese Namen nur als Text-Stub (Linker-Skript) — dlopen scheitert
# mit "invalid ELF header". patchelf schreibt die Eintraege auf
# die glibc-Namen um.
# ============================================================

LIBCXX="${1:-}"
LOG="${2:-}"

_log() {
    if [ -n "$LOG" ] && [ -w "$LOG" ]; then
        echo "[fix_libcxx] $*" >> "$LOG"
    fi
    echo "[fix_libcxx] $*"
}

if [ -z "$LIBCXX" ] || [ ! -f "$LIBCXX" ]; then
    _log "libc++ nicht gefunden: '$LIBCXX' — nichts zu tun"
    exit 0
fi

if ! command -v patchelf >/dev/null 2>&1; then
    _log "WARN patchelf fehlt. Installiere mit:"
    _log "     sudo apt-get install -y patchelf"
    exit 0
fi

# Architektur bestaetigen
ARCH=$(file -b "$LIBCXX" | head -1)
case "$ARCH" in
    *aarch64*) : ;;
    *) _log "WARN libc++ ist nicht aarch64: $ARCH"
       exit 0 ;;
esac

# Android-Namen ermitteln
HAS_ANDROID=0
for need in $(readelf -d "$LIBCXX" 2>/dev/null | grep NEEDED | awk '{print $NF}' | tr -d '[]'); do
    case "$need" in
        libc.so|libdl.so|libm.so|libstdc++.so|liblog.so) HAS_ANDROID=1 ;;
    esac
done

if [ "$HAS_ANDROID" = "0" ]; then
    _log "libc++ hat bereits glibc-Namen — kein Patch noetig"
    exit 0
fi

# Backup nur einmal anlegen; von Backup arbeiten, damit mehrfache
# Aufrufe exakt dasselbe Ergebnis liefern (idempotent)
if [ ! -f "${LIBCXX}.orig" ]; then
    cp "$LIBCXX" "${LIBCXX}.orig"
    _log "Backup angelegt: ${LIBCXX}.orig"
fi
cp "${LIBCXX}.orig" "$LIBCXX"

patchelf --replace-needed libc.so      libc.so.6      "$LIBCXX" 2>>"$LOG" || true
patchelf --replace-needed libdl.so     libdl.so.2     "$LIBCXX" 2>>"$LOG" || true
patchelf --replace-needed libm.so      libm.so.6      "$LIBCXX" 2>>"$LOG" || true
patchelf --replace-needed libstdc++.so libstdc++.so.6 "$LIBCXX" 2>>"$LOG" || true
# liblog.so gibt es unter Linux nicht — auf libc.so.6 umleiten.
# Die __android_log_*-Symbole kommen aus unserem Loader (android_shim.c).
patchelf --replace-needed liblog.so    libc.so.6      "$LIBCXX" 2>>"$LOG" || true

_log "libc++ erfolgreich gepatcht. Neue NEEDED-Eintraege:"
readelf -d "$LIBCXX" 2>/dev/null | grep NEEDED | while read -r line; do
    _log "  $line"
done

exit 0
