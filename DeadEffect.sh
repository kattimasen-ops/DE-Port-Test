#!/bin/bash
# ============================================================
# Dead Effect - PortMaster-integriertes Startscript
# Target: M9 Pro / R36S (RK3326, ArkOS, aarch64)
#
# WICHTIG:
#   Der Loader hat folgende Pfade EINKOMPILIERT:
#     DEAD_EFFECT_LIBDIR = /roms/ports/DeadEffect/lib
#     DEAD_EFFECT_ASSETS = /roms/ports/DeadEffect/assets
#
#   Falls GAMEDIR woanders liegt, wird ein Symlink
#   /roms/ports/DeadEffect -> $GAMEDIR erzeugt (benoetigt root).
# ============================================================

# --- 1. PortMaster-Standardpfade ermitteln ---
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

controlfolder=""
for pm_candidate in \
    "/opt/system/Tools/PortMaster" \
    "/opt/tools/PortMaster" \
    "$XDG_DATA_HOME/PortMaster" \
    "/storage/.config/PortMaster" \
    "/roms/tools/PortMaster" \
    "/roms/ports/PortMaster" \
    "/roms2/tools/PortMaster"; do
    if [ -f "$pm_candidate/control.txt" ]; then
        controlfolder="$pm_candidate"
        break
    fi
done

# --- 2. PortMaster-Kontrollskripte laden ---
if [ -n "$controlfolder" ] && [ -f "$controlfolder/control.txt" ]; then
    source "$controlfolder/control.txt"
    [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
    command -v get_controls >/dev/null 2>&1 && get_controls
fi

# --- 3. GAMEDIR robust ermitteln ---
SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd -P)"

GAMEDIR=""
for sub in "DeadEffect" "deadeffect" "dead_effect" "Dead_Effect"; do
    if [ -d "$SCRIPT_DIR/$sub" ]; then
        GAMEDIR="$SCRIPT_DIR/$sub"
        break
    fi
done
[ -z "$GAMEDIR" ] && GAMEDIR="$SCRIPT_DIR"
cd "$GAMEDIR" || exit 1

# --- 4. Logging aufsetzen (VOR allen Aktionen) ---
LOG_FILE="$GAMEDIR/log.txt"
[ -s "$LOG_FILE" ] && mv -f "$LOG_FILE" "$GAMEDIR/log.prev.txt"

# stdout+stderr gleichzeitig auf Konsole und Datei
exec > >(tee "$LOG_FILE") 2>&1

echo "============================================================"
echo "=== Dead Effect gestartet: $(date) ==="
echo "=== SCRIPT_DIR: $SCRIPT_DIR ==="
echo "=== GAMEDIR:    $GAMEDIR ==="
echo "=== CWD:        $(pwd) ==="
echo "=== Log-Datei:  $LOG_FILE ==="
echo "============================================================"

# --- 5. gptokeyb suchen ---
if [ -z "$GPTOKEYB" ] || [ ! -x "$GPTOKEYB" ]; then
    GPTOKEYB=""
    for gptk_path in \
        "/opt/inttools/gptokeyb" \
        "/usr/local/bin/gptokeyb" \
        "/usr/bin/gptokeyb" \
        "$controlfolder/gptokeyb"; do
        if [ -x "$gptk_path" ]; then
            GPTOKEYB="$gptk_path"
            break
        fi
    done
fi

# --- 6. Cleanup / Kill-Switch ---
cleanup() {
    echo "[DE] Aufraeumen..."
    pkill -9 -f "deadeffect-loader" 2>/dev/null || true
    if [ -n "$ESUDO" ]; then
        $ESUDO kill -9 $(pidof gptokeyb)  2>/dev/null || true
        $ESUDO kill -9 $(pidof gptokeyb2) 2>/dev/null || true
    else
        kill -9 $(pidof gptokeyb)  2>/dev/null || true
        kill -9 $(pidof gptokeyb2) 2>/dev/null || true
    fi
    printf "\033c\e[?25h" > /dev/tty1 2>/dev/null || true
    echo "[DE] Beendet: $(date)"
}
trap cleanup EXIT INT TERM

# --- 7. gptokeyb starten ---
if [ -n "$GPTOKEYB" ] && [ -x "$GPTOKEYB" ]; then
    echo "[DE] Starte gptokeyb: $GPTOKEYB"
    if [ -f "$GAMEDIR/de_wrapper.gptk" ]; then
        "$GPTOKEYB" "deadeffect-loader" -c "$GAMEDIR/de_wrapper.gptk" &
    else
        "$GPTOKEYB" "deadeffect-loader" &
    fi
    sleep 0.5
else
    echo "[DE] WARNUNG: gptokeyb nicht gefunden - kein Select+Start-Kill!"
fi

# --- 8. GPU / Mesa / Panfrost (Mali-G31 MP2) ---
export PAN_MESA_DEBUG=noaff,deqp
export MESA_GL_VERSION_OVERRIDE=3.1
export MESA_GLES_VERSION_OVERRIDE=3.2
export MESA_GLSL_CACHE_DISABLE=0
export MESA_GLSL_CACHE_DIR="$GAMEDIR/.mesa_cache"
mkdir -p "$GAMEDIR/.mesa_cache" 2>/dev/null || true

# --- 9. Display / Vsync ---
export vblank_mode=0
export SDL_RENDER_VSYNC=0
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_KMSDRM_DOUBLE_BUFFER=1
export SDL_VIDEO_KMSDRM_CARD_INDEX=0

# --- 10. Audio (RK3326) ---
export SDL_AUDIODRIVER=alsa
export AUDIODEV="default"

# --- 11. Bibliothekspfad (HOME absichtlich NICHT ueberschreiben) ---
export LD_LIBRARY_PATH="$GAMEDIR/lib:$GAMEDIR:$LD_LIBRARY_PATH"
[ -n "$sdl_controllerconfig" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# --- 12. Pfad-Kompatibilitaet fuer den Loader ---
# Der Loader erwartet HART /roms/ports/DeadEffect/lib und .../assets.
# Falls GAMEDIR woanders liegt, Symlink setzen (nur als root moeglich).
LOADER_EXPECTED="/roms/ports/DeadEffect"
if [ "$GAMEDIR" != "$LOADER_EXPECTED" ]; then
    echo "[DE] GAMEDIR != $LOADER_EXPECTED - pruefe Symlink..."
    if [ -L "$LOADER_EXPECTED" ] || [ -d "$LOADER_EXPECTED" ]; then
        echo "[DE]   $LOADER_EXPECTED existiert bereits"
    elif [ -w "$(dirname "$LOADER_EXPECTED")" ]; then
        if ln -sfn "$GAMEDIR" "$LOADER_EXPECTED"; then
            echo "[DE]   Symlink erstellt: $LOADER_EXPECTED -> $GAMEDIR"
        else
            echo "[DE]   FEHLER: Symlink konnte nicht erstellt werden"
        fi
    else
        echo "[DE]   WARNUNG: Kein Schreibrecht auf $(dirname "$LOADER_EXPECTED")"
        echo "[DE]   Bitte manuell (als root): ln -sfn '$GAMEDIR' '$LOADER_EXPECTED'"
    fi
fi

# --- 13. Datei-Check (Diagnose) ---
echo "[DE] === Datei-Check ==="
for f in "deadeffect-loader" "lib/libmain.so" "lib/libunity.so" "lib/libil2cpp.so"; do
    if [ -f "$GAMEDIR/$f" ]; then
        SZ=$(stat -c%s "$GAMEDIR/$f" 2>/dev/null || echo '?')
        echo "[DE]   OK    $f ($SZ Bytes)"
    else
        echo "[DE]   FEHLT $f"
    fi
done

if [ -d "$GAMEDIR/assets/bin/Data" ]; then
    echo "[DE]   OK    assets/bin/Data/"
else
    echo "[DE]   WARN  assets/bin/Data/ fehlt (OBB nicht entpackt?)"
fi

# OBB-Suche (falls noch als .obb herumliegt)
OBB_FOUND=""
for obb in "$GAMEDIR/main."*.obb "$GAMEDIR/assets/main."*.obb; do
    [ -f "$obb" ] && OBB_FOUND="$obb" && break
done
if [ -n "$OBB_FOUND" ]; then
    echo "[DE]   INFO  OBB gefunden: $(basename "$OBB_FOUND")"
    echo "[DE]         (muss noch entpackt werden nach assets/)"
fi

# --- 14. Konsole vorbereiten ---
[ -c /dev/tty1 ] && printf '\033[?25l\033[2J\033[H' > /dev/tty1 2>/dev/null || true

# --- 15. Loader starten ---
echo "============================================================"
echo "[DE] Starte deadeffect-loader..."
echo "[DE] CWD:    $(pwd)"
echo "[DE] GAMEDIR: $GAMEDIR"
echo "============================================================"

chmod +x "$GAMEDIR/deadeffect-loader" 2>/dev/null || true

if command -v pm_platform_helper >/dev/null 2>&1; then
    pm_platform_helper "$GAMEDIR/deadeffect-loader" 2>/dev/null || true
fi

cd "$GAMEDIR"
"$GAMEDIR/deadeffect-loader" "$GAMEDIR"
EXIT_CODE=$?

echo "============================================================"
echo "[DE] Loader beendet mit Code: $EXIT_CODE"
echo "[DE] Log wurde gespeichert unter: $LOG_FILE"
echo "============================================================"

# --- 16. Aufraeumen ---
[ -c /dev/tty1 ] && printf '\033c' > /dev/tty1 2>/dev/null || true
if command -v pm_finish >/dev/null 2>&1; then
    pm_finish 2>/dev/null || true
fi

exit $EXIT_CODE