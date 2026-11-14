#!/bin/bash
# ECHOFIELD - experimental granular sampler (R36S / ArkOS / PortMaster)
# PortMaster launch script. Lives in the ports folder next to the echofield/ directory.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/echofield"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

ARCH="${DEVICE_ARCH:-aarch64}"
BIN="echofield.${ARCH}"

say() {   # message on screen when PortMaster provides pm_message, always in log.txt
  echo "ECHOFIELD: $*"
  if type pm_message >/dev/null 2>&1; then pm_message "ECHOFIELD: $*"; fi
}

# ---- dependency checks -------------------------------------------------------
if [ ! -f "$GAMEDIR/$BIN" ]; then
  say "no binary for architecture '$ARCH' (only aarch64 is provided)."
  sleep 6; exit 1
fi
chmod +x "$GAMEDIR/$BIN" 2>/dev/null
if command -v ldd >/dev/null 2>&1; then
  missing=$(ldd "$GAMEDIR/$BIN" 2>&1 | grep "not found")
  if [ -n "$missing" ]; then
    say "missing system libraries: $missing"
    sleep 8; exit 1
  fi
fi

# ---- user data: kept outside the shipped files so updates never overwrite it ---
DATADIR="$GAMEDIR/userdata"
mkdir -p "$DATADIR" 2>/dev/null
if ! ( touch "$DATADIR/.write_test" && rm -f "$DATADIR/.write_test" ) 2>/dev/null; then
  DATADIR="$XDG_DATA_HOME/echofield"
  mkdir -p "$DATADIR"
  echo "ports folder not writable, using $DATADIR"
fi
export ECHOFIELD_DATA="$DATADIR"
export ECHOFIELD_APPDIR="$GAMEDIR"

export LD_LIBRARY_PATH="$GAMEDIR/libs.${ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

echo "ECHOFIELD launch: CFW=$CFW_NAME device=$DEVICE_NAME arch=$ARCH data=$DATADIR"
uname -a

# ---- USB audio: make the kernel driver take the device, record what happened ----
# USB class-compliant audio needs snd-usb-audio; some firmwares ship it as a module
# that is never loaded, or load it after the device was plugged in (left unbound).
# The report is shown in-app (DIAGNOSTICS) so no SSH is needed to read it.
REPORT="$DATADIR/kernel_audio.txt"
export ECHOFIELD_KERNEL_REPORT="$REPORT"
{
  echo "kernel: $(uname -r)"
  if [ -d /sys/module/snd_usb_audio ]; then
    echo "snd-usb-audio: already active"
  elif out=$($ESUDO modprobe snd-usb-audio 2>&1); then
    echo "snd-usb-audio: loaded now by the launcher"; sleep 1
  else
    echo "snd-usb-audio: COULD NOT LOAD (${out:-no message})"
    if ls /lib/modules/"$(uname -r)"/kernel/sound/usb/snd-usb-audio.ko* >/dev/null 2>&1; then
      echo "  module file exists but failed to load"
    else
      echo "  module not shipped with this kernel/firmware"
    fi
  fi
  # bind audio-control interfaces that no driver claimed (device plugged in before load)
  if [ -d /sys/bus/usb/drivers/snd-usb-audio ]; then
    for i in /sys/bus/usb/devices/*:*; do
      [ "$(cat "$i/bInterfaceClass" 2>/dev/null)" = "01" ] || continue
      [ "$(cat "$i/bInterfaceSubClass" 2>/dev/null)" = "01" ] || continue
      [ -e "$i/driver" ] && continue
      n=$(basename "$i")
      if echo "$n" | $ESUDO tee /sys/bus/usb/drivers/snd-usb-audio/bind >/dev/null 2>&1; then
        echo "bound $n to snd-usb-audio"; sleep 1
      else
        echo "binding $n to snd-usb-audio FAILED"
      fi
    done
  fi
  echo "--- sound cards"
  cat /proc/asound/cards 2>&1
  echo "--- usb audio interfaces"
  for i in /sys/bus/usb/devices/*:*; do
    [ "$(cat "$i/bInterfaceClass" 2>/dev/null)" = "01" ] || continue
    d=$(dirname "$(readlink -f "$i")")
    echo "$(basename "$i") $(cat "$d/product" 2>/dev/null) [$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)] subclass $(cat "$i/bInterfaceSubClass" 2>/dev/null) driver: $(basename "$(readlink "$i/driver" 2>/dev/null)" 2>/dev/null)"
  done
  echo "--- kernel messages (usb / audio)"
  ($ESUDO dmesg 2>/dev/null || dmesg 2>&1) | grep -iE "usb|snd|audio|uac" | tail -40
} > "$REPORT" 2>&1
cat "$REPORT"
ls -l /dev/snd 2>&1

# gptokeyb without -c only provides the hotkey exit (no key emulation).
# ECHOFIELD reads the controller itself and saves its session continuously.
$GPTOKEYB "$BIN" &
if type pm_platform_helper >/dev/null 2>&1; then pm_platform_helper "$GAMEDIR/$BIN"; fi

./"$BIN"
status=$?
echo "ECHOFIELD exited with status $status"

if type pm_finish >/dev/null 2>&1; then
  pm_finish
else
  $ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null
  $ESUDO systemctl restart oga_events 2>/dev/null &
  printf "\033c" > /dev/tty0 2>/dev/null
fi
