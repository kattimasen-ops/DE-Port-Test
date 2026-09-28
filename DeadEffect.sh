#!/bin/bash
GAMEDIR="/roms/ports/DeadEffect"
cd "$GAMEDIR" || exit 1

echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true

# WICHTIG: privates lib-Verzeichnis in den Suchpfad aufnehmen.
# libc++_shared.so und andere private Libs liegen in $GAMEDIR/lib/.
export LD_LIBRARY_PATH="$GAMEDIR/lib:$GAMEDIR:$LD_LIBRARY_PATH"

export SDL_VIDEODRIVER=kmsdrm
export SDL_AUDIODRIVER=alsa
export SDL_ASSERT=always_ignore
export MESA_GL_VERSION_OVERRIDE=3.2
export MESA_GLSL_VERSION_OVERRIDE=320
export PAN_MESA_DEBUG=gl3
export MESA_NO_ERROR=1
export MESA_LOADER_DRIVER_OVERRIDE=panfrost

export HOME="$GAMEDIR/userdata"
mkdir -p "$HOME" "$GAMEDIR/assets" "$GAMEDIR/lib"

OBB=$(ls "$GAMEDIR"/main.*.com.bulkypix.deadeffect.obb 2>/dev/null | head -1)
if [ -n "$OBB" ] && [ ! -d "$GAMEDIR/assets/bin/Data" ]; then
    mkdir -p "$GAMEDIR/assets"
    unzip -o -q "$OBB" -d "$GAMEDIR/assets" || true
fi

if command -v gptokeyb >/dev/null 2>&1; then
    gptokeyb -k "deadeffect" -c "$GAMEDIR/de_wrapper.gptk" & GPID=$!
    trap "kill $GPID 2>/dev/null" EXIT
fi

stdbuf -oL -eL ./deadeffect-loader "$GAMEDIR" 2>&1 | tee "$GAMEDIR/log.txt"
STATUS=$?
[ -n "${GPID:-}" ] && kill "$GPID" 2>/dev/null || true
exit $STATUS
