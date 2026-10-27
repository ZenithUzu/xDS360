#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and runs tools/xdk/test_text.cpp (letters, translations, font picture).
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${TMPDIR:-/tmp}/xds360-test-text"
"${CXX:-g++}" -std=c++03 -Wall -O1 -fsanitize=address,undefined -I"$repo_root/xdk/frontend" \
    "$repo_root/tools/xdk/test_text.cpp" "$repo_root/xdk/frontend/xds_text.cpp" \
    "$repo_root/xdk/frontend/xds_text_data.cpp" -o "$out"
"$out"
