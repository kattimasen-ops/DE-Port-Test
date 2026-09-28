#!/bin/bash
# ============================================================
# Dead Effect - Launcher (PortMaster-konform)
# ============================================================

# ---- PortMaster control.txt finden -------------------------------------
controlfolder=""
for d in \
    "/opt/system/Tools/PortMaster" \
    "/opt/tools/PortMaster" \
    "$HOME/.local/share/PortMaster" \
    "$XDG_DATA_HOME/PortMaster" \
    "/storage/.config/PortMaster" \
    "/roms/tools/PortMaster" \
    "/roms/ports/PortMaster" \
    "/roms2/tools/PortMaster" \
    ; do
    [ -f "$d/control.txt" ] && controlfolder="$d" && break
done

if [ -n "$controlfolder" ] && [ -f "$controlfolder/control.txt" ]; then
    # shellcheck disable=SC1091
    . "$controlfolder/control.txt"
    # shellcheck disable=SC1091
    [ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && . "$controlfolder/mod_${CFW_NAME}.txt"
    get_controls 2>/dev/null || true
fi

# ---- GAMEDIR ermitteln -------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR=""
for cand in "$SCRIPT_DIR" \
            "$SCRIPT_DIR/DeadEffect" \
            "$SCRIPT_DIR/deadeffect" \
            "$SCRIPT_DIR/dead_effect" \
            "$SCRIPT_DIR/Dead_Effect" \
            "/roms/ports/DeadEffect"; do
    if [ -f "$cand/deadeffect-loader" ]; then
        GAMEDIR="$cand"
        break
    fi
done

if [ -z "$GAMEDIR" ]; then
    echo "[DE] FEHLER: DeadEffect-Verzeichnis nicht gefunden."
    exit 1
fi

cd "$GAMEDIR" || exit 1

# ---- Log-Rotation ------------------------------------------------------
LOG_FILE="$GAMEDIR/log.txt"
[ -f "$LOG_FILE" ] && mv -f "$LOG_FILE" "$GAMEDIR/log.prev.txt" 2>/dev/null

# ---- Logging -----------------------------------------------------------
exec > >(tee "$LOG_FILE") 2>&1

echo "============================================================"
echo "=== Dead Effect gestartet: $(date) ==="
echo "=== SCRIPT_DIR: $SCRIPT_DIR ==="
echo "=== GAMEDIR:    $GAMEDIR ==="
echo "=== CWD:        $PWD ==="
echo "=== Log-Datei:  $LOG_FILE ==="
echo "============================================================"

# ---- Symlink-Fallback --------------------------------------------------
if [ "$GAMEDIR" != "/roms/ports/DeadEffect" ] && [ ! -e /roms/ports/DeadEffect ]; then
    ln -sfn "$GAMEDIR" /roms/ports/DeadEffect 2>/dev/null || true
fi

# ---- gptokeyb ----------------------------------------------------------
GPID=""
cleanup() {
    [ -n "$GPID" ] && kill "$GPID" 2>/dev/null || true
    pkill -f deadeffect-loader 2>/dev/null || true
    [ -n "$controlfolder" ] && [ -x "$controlfolder/tools/reset_tty1.sh" ] && \
        "$controlfolder/tools/reset_tty1.sh" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

GPTK=""
for cand in /opt/inttools/gptokeyb /usr/local/bin/gptokeyb /usr/bin/gptokeyb; do
    [ -x "$cand" ] && GPTK="$cand" && break
done
if [ -z "$GPTK" ] && [ -n "$controlfolder" ] && [ -x "$controlfolder/gptokeyb" ]; then
    GPTK="$controlfolder/gptokeyb"
fi

if [ -n "$GPTK" ] && [ -f "$GAMEDIR/de_wrapper.gptk" ]; then
    echo "[DE] Starte gptokeyb: $GPTK"
    "$GPTK" -k "deadeffect" -c "$GAMEDIR/de_wrapper.gptk" &
    GPID=$!
else
    echo "[DE] gptokeyb nicht gefunden — ueberspringe"
fi

# ---- Datei-Check -------------------------------------------------------
echo "[DE] === Datei-Check ==="
check_file() {
    if [ -f "$1" ]; then
        echo "[DE]   OK    $2 ($(stat -c%s "$1") Bytes)"
    else
        echo "[DE]   FEHLT $2"
    fi
}
check_file "$GAMEDIR/deadeffect-loader" "deadeffect-loader"
check_file "$GAMEDIR/lib/libmain.so"    "lib/libmain.so"
check_file "$GAMEDIR/lib/libunity.so"   "lib/libunity.so"
check_file "$GAMEDIR/lib/libil2cpp.so"  "lib/libil2cpp.so"
[ -d "$GAMEDIR/assets/bin/Data" ] && echo "[DE]   OK    assets/bin/Data/" || echo "[DE]   FEHLT assets/bin/Data/"

# ---- libc++_shared.so an glibc anpassen (idempotent) -------------------
if [ -f "$GAMEDIR/fix_libcxx.sh" ] && [ -f "$GAMEDIR/lib/libc++_shared.so" ]; then
    bash "$GAMEDIR/fix_libcxx.sh" "$GAMEDIR/lib/libc++_shared.so" "$LOG_FILE" || true
elif [ -f "$GAMEDIR/lib/libc++_shared.so" ]; then
    echo "[DE] [WARN] fix_libcxx.sh fehlt — libc++ bleibt ungepatcht"
fi

# ---- Umgebung ----------------------------------------------------------
echo "============================================================"
echo "[DE] GPU/Mesa/Panfrost"
echo "============================================================"

export PAN_MESA_DEBUG="noaff,deqp"
export MESA_GL_VERSION_OVERRIDE="3.1"
export MESA_GLES_VERSION_OVERRIDE="3.2"
export MESA_GLSL_CACHE_DISABLE="0"
export MESA_GLSL_CACHE_DIR="$GAMEDIR/.mesa_cache"
mkdir -p "$MESA_GLSL_CACHE_DIR"

export vblank_mode=0
export SDL_RENDER_VSYNC=0
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_KMSDRM_DOUBLE_BUFFER=1
export SDL_VIDEO_KMSDRM_CARD_INDEX=0

export SDL_AUDIODRIVER=alsa
export AUDIODEV=default

export LD_LIBRARY_PATH="$GAMEDIR/lib:$GAMEDIR:${LD_LIBRARY_PATH:-}"

if [ -n "${sdl_controllerconfig:-}" ]; then
    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

# ---- OBB-Suche ---------------------------------------------------------
if [ ! -d "$GAMEDIR/assets/bin/Data" ]; then
    OBB=""
    for cand in "$GAMEDIR"/main.*.obb "$GAMEDIR"/assets/main.*.obb; do
        [ -f "$cand" ] && OBB="$cand" && break
    done
    if [ -n "$OBB" ]; then
        echo "[DE] Extrahiere OBB: $OBB"
        mkdir -p "$GAMEDIR/assets"
        unzip -o -q "$OBB" -d "$GAMEDIR/assets" || true
    fi
fi

# ---- Start -------------------------------------------------------------
echo "============================================================"
echo "[DE] Starte deadeffect-loader..."
echo "[DE] CWD:    $PWD"
echo "[DE] GAMEDIR: $GAMEDIR"
echo "============================================================"

"$GAMEDIR/deadeffect-loader" "$GAMEDIR"
EXIT_CODE=$?

echo "============================================================"
echo "[DE] Loader beendet mit Code: $EXIT_CODE"
echo "[DE] Log wurde gespeichert unter: $LOG_FILE"
echo "============================================================"

# pm_finish (PortMaster), falls verfügbar
type pm_finish >/dev/null 2>&1 && pm_finish

exit $EXIT_CODE
