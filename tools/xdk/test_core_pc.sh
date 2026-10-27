#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the converted core (xdk/noods) on a PC with the xdk/compat std::
# replacements instead of the real <thread>, <atomic>, <chrono>... and runs the
# upstream smoke test. Frame hashes must match the original NooDS build.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${TMPDIR:-/tmp}/xds360-core-pc"
rom_path="$build_dir/render-test.nds"
binary="$build_dir/xds-core-smoke"
cxx="${CXX:-g++}"

mkdir -p "$build_dir"
python3 "$repo_root/tests/make_noods_smoke_rom.py" "$rom_path"

mapfile -t sources < <(cd "$repo_root/xdk/noods" && find . -name '*.cpp' | sort | sed "s#^\./#$repo_root/xdk/noods/#")

# -I xdk/compat comes first, so <thread>, <mutex>, <atomic>, <chrono> and
# <condition_variable> resolve to our versions, as they do in VS2010.
"$cxx" -w -std=gnu++11 -O2 -fstrict-aliasing -pthread \
    -DLOG_LEVEL=0 -DNOODS_XENON=1 -DNO_FDOPEN=1 \
    -I"$repo_root/xdk/compat" -I"$repo_root/xdk/noods" \
    "$repo_root/tests/noods_core_smoke.cpp" "${sources[@]}" \
    "$repo_root/xdk/compat/xds_platform.cpp" -o "$binary"

"$binary" "$rom_path"
"$binary" "$rom_path" --framepath-regression
"$binary" "$rom_path" --capture-regression
"$binary" "$rom_path" --capture-bank-matrix
"$binary" "$rom_path" --cadence-stress "${NOODS_CADENCE_STRESS_FRAMES:-2000}"
"$binary" "$rom_path" --benchmark "${NOODS_BENCHMARK_FRAMES:-600}"
"$binary" "$rom_path" --renderer-benchmark "${NOODS_RENDERER_BENCHMARK_FRAMES:-120}"

# The frontend's wrapper around the core (xdk/frontend/emu.cpp).
"$cxx" -w -std=gnu++11 -O2 -pthread -DLOG_LEVEL=0 -DNOODS_XENON=1 -DNO_FDOPEN=1 \
    -I"$repo_root/xdk/compat" -I"$repo_root/xdk/noods" -I"$repo_root/xdk/frontend" \
    "$repo_root/tools/xdk/pc_emu_test.cpp" "$repo_root/xdk/frontend/emu.cpp" "${sources[@]}" \
    "$repo_root/xdk/compat/xds_platform.cpp" -o "$build_dir/xds-emu-test"
"$build_dir/xds-emu-test" "$rom_path"
