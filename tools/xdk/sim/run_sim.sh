#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Xbox frontend (xdk/frontend/main.cpp + NooDS's console UI) with the
# PC simulator (sim_xdk.cpp: software Direct3D, scripted buttons, folders for
# game:, hdd:, usb0:), walks the menus with a button script and saves
# screenshots (PNG) to the folder given as $1.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
shots="${1:-${TMPDIR:-/tmp}/xds360-sim-shots}"
build="${TMPDIR:-/tmp}/xds360-sim-build"
console="$build/console"
cxx="${CXX:-clang++}"
mkdir -p "$shots" "$build"
rm -rf "$console" "$build"/*.ppm
mkdir -p "$console/roms/DS Games" "$console/hdd/Emulators" "$console/usb0"

python3 "$repo_root/tests/make_noods_smoke_rom.py" "$build/test.nds"
# Real ROMs are far bigger; NooDS reads the first 20KB at start, so pad to 1MB.
python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[1],'wb').write(d+bytes(1048576-len(d)))" "$build/test.nds"
cp "$build/test.nds" "$console/roms/Render Test.nds"
cp "$build/test.nds" "$console/roms/DS Games/Another Test Game (USA).nds"
echo "not a rom" > "$console/roms/readme.txt"

mapfile -t noods < <(cd "$repo_root/xdk/noods" && find . -name '*.cpp' | sort | sed "s#^\./#$repo_root/xdk/noods/#")
flags=(-w -std=c++11 -O2 -pthread -fms-extensions -DXDS_SIM -DLOG_LEVEL=0 -DNOODS_XENON=1 -DNO_FDOPEN=1
       -Dmain=xdkMain -include "$repo_root/tools/xdk/fake_xdk/fake_crt.h"
       -I"$repo_root/tools/xdk/fake_xdk" -I"$repo_root/xdk/compat" -I"$repo_root/xdk/noods/core")

objects=()
for source in "$repo_root/xdk/frontend/main.cpp" "$repo_root/xdk/frontend/xbox_buttons.cpp" \
              "$repo_root/xdk/compat/xds_files.cpp" \
              "$repo_root/xdk/compat/xds_platform.cpp" "${noods[@]}"; do
    object="$build/obj/$(echo "${source#$repo_root/}" | tr '/' '_').o"
    mkdir -p "$build/obj"
    if [[ ! -f "$object" || "$source" -nt "$object" || -n "${SIM_REBUILD:-}" ]]; then
        "$cxx" "${flags[@]}" -c "$source" -o "$object"
    fi
    objects+=("$object")
done
"$cxx" -w -std=c++11 -O2 -fms-extensions -c "$repo_root/tools/xdk/sim/sim_xdk.cpp" -o "$build/sim.o" \
    -I"$repo_root/tools/xdk/fake_xdk"
cc -O2 -c "$repo_root/tools/xdk/sim/sim_files.c" -o "$build/sim_files.o"
"$cxx" -pthread "${objects[@]}" "$build/sim.o" "$build/sim_files.o" -ldl -o "$build/xds360-sim"

# One step per screen frame (60 per second).
script="wait:40 shot:01-roms-folder
press:B wait:20 shot:02-emulator-folder
press:B wait:20 shot:03-drives
press:START wait:20 shot:04-start-goes-to-roms
press:DOWN wait:5 press:A wait:240 shot:05-game
press:RS wait:30 shot:06-pause-menu
press:DOWN wait:3 press:DOWN wait:3 press:DOWN wait:3 press:DOWN wait:3 press:DOWN wait:3
press:A wait:30 shot:07-settings
press:B wait:20 press:DOWN wait:3 press:A wait:30 shot:08-back-to-browser
press:BACK wait:10"

SIM_ROOT="$console" SIM_SHOTS="$build" SIM_SCRIPT="$(echo $script)" "$build/xds360-sim"

for ppm in "$build"/*.ppm; do
    python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
        "$ppm" "$shots/$(basename "${ppm%.ppm}").png"
done
echo "--- console folder after the run:"
(cd "$console" && find . -maxdepth 2 | sort)
echo "--- xds360.log:"
cat "$console/xds360.log"
