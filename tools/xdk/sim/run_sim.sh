#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds xdk/frontend/main.cpp with the PC simulator (sim_xdk.cpp), sets up a
# fake console folder with the synthetic test ROM, walks the menu with a button
# script and saves screenshots (PNG) to the folder given as $1.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
shots="${1:-${TMPDIR:-/tmp}/xds360-sim-shots}"
build="${TMPDIR:-/tmp}/xds360-sim-build"
console="$build/console"
cxx="${CXX:-clang++}"
mkdir -p "$shots" "$build"
rm -rf "$console"
mkdir -p "$console/roms/DS Games" "$console/hdd/Emulators" "$console/usb0"

python3 "$repo_root/tests/make_noods_smoke_rom.py" "$build/test.nds"
cp "$build/test.nds" "$console/roms/Render Test.nds"
cp "$build/test.nds" "$console/roms/DS Games/Another Test Game (USA).nds"
echo "not a rom" > "$console/roms/readme.txt"

mapfile -t core < <(cd "$repo_root/xdk/noods" && find . -name '*.cpp' | sort | sed "s#^\./#$repo_root/xdk/noods/#")
common=(-w -std=c++11 -O2 -pthread -DLOG_LEVEL=0 -DNOODS_XENON=1 -DNO_FDOPEN=1)

"$cxx" "${common[@]}" -fms-extensions -c "$repo_root/xdk/frontend/main.cpp" -o "$build/main.o" \
    -Dmain=xdkMain \
    -include "$repo_root/tools/xdk/fake_xdk/fake_crt.h" -I"$repo_root/tools/xdk/fake_xdk" \
    -I"$repo_root/xdk/compat" -I"$repo_root/xdk/frontend"
"$cxx" "${common[@]}" -fms-extensions -c "$repo_root/tools/xdk/sim/sim_xdk.cpp" -o "$build/sim.o" \
    -I"$repo_root/tools/xdk/fake_xdk"
cc -O2 -c "$repo_root/tools/xdk/sim/sim_files.c" -o "$build/sim_files.o"
"$cxx" "${common[@]}" "$build/main.o" "$build/sim.o" "$build/sim_files.o" -ldl "$repo_root/xdk/frontend/emu.cpp" "${core[@]}" \
    "$repo_root/xdk/compat/xds_platform.cpp" -I"$repo_root/xdk/compat" -I"$repo_root/xdk/noods" \
    -I"$repo_root/xdk/frontend" -o "$build/xds360-sim"

script="wait:3 shot:01-roms-folder
press:B wait:2 shot:02-emulator-folder
press:B wait:2 shot:03-drives
press:Y wait:2 press:A wait:2 shot:04-subfolder
press:B wait:2 press:DOWN wait:2 shot:05-rom-selected
press:A wait:150 shot:06-game
hold:BACK+START:120 wait:5 shot:07-back-to-menu
press:BACK"

SIM_ROOT="$console" SIM_SHOTS="$build" SIM_SCRIPT="$(echo $script)" "$build/xds360-sim"

for ppm in "$build"/*.ppm; do
    python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
        "$ppm" "$shots/$(basename "${ppm%.ppm}").png"
done
echo "--- console folder after the run:"
(cd "$console" && find . -maxdepth 2 | sort)
echo "--- xds360.log:"
cat "$console/xds360.log"
echo "--- xds360.cfg:"
cat "$console/xds360.cfg"
