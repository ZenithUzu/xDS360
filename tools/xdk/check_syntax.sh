#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Syntax check of every file in the VS2010 project, on a PC, pretending to be
# the Xbox 360 compiler (_XBOX, _MSC_VER=1600) with the fake XDK headers in
# tools/xdk/fake_xdk. Also flags C++11 that VS2010 does not support.
# This is a quick pre-check; the real test is building in Visual Studio 2010.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

flags=(-std=c++11 -fsyntax-only -fms-extensions -Wc++98-compat -Wno-c++98-compat-local-type-template-args
       -D_XBOX -D_MSC_VER=1600 -Dmain=xdkMain -DNDEBUG -DLOG_LEVEL=0 -DNOODS_XENON=1 -DENDIAN_BIG=1 -DNO_FDOPEN=1
       -include tools/xdk/fake_xdk/fake_crt.h -Itools/xdk/fake_xdk -Ixdk/compat -Ixdk/noods/core)

# The XDK entry point is "void main()", which clang rejects; it is renamed
# only for this check.
# C++11 features VS2010 accepts. Anything else reported is a problem.
allowed="'nullptr' is incompatible|'auto' type specifier is incompatible|lambda expressions are incompatible|'>>' is incompatible|rvalue references are incompatible|'override' keyword"

status=0
mapfile -t files < <(find xdk/noods xdk/frontend xdk/compat -name '*.cpp' | sort)
for file in "${files[@]}"; do
    output="$(clang++ "${flags[@]}" "$file" 2>&1 || true)"
    problems="$(printf '%s\n' "$output" | grep -E 'error|c\+\+98-compat' | grep -Ev "$allowed" || true)"
    if [[ -n "$problems" ]]; then
        printf '%s\n' "$problems"
        status=1
    fi
done
# C library functions the VS2010/XDK runtime does not have (strtoll and
# strtoull are mapped in xdk/compat/xds_compat.h) (glibc on the PC
# has them, so clang alone does not notice), and math calls on integers, which
# are ambiguous there.
missing='\b(llabs|lround|llround|round|trunc|fmin|fmax|cbrt|log2|exp2|nearbyint|copysign|snprintf|vsnprintf|strcasecmp|strncasecmp|va_copy|isnan|isinf|atoll|usleep)\s*\('
ambiguous='\b(sqrt|pow|floor|ceil|log|exp|fabs)\s*\(\s*(\*|\(u?int)'
found="$(grep -rnE "$missing|$ambiguous" xdk/noods xdk/frontend xdk/compat --include='*.cpp' --include='*.h' |
         grep -vE '^\S+:[0-9]+:\s*(//|#define)' | grep -v 'PC tests' || true)"
if [[ -n "$found" ]]; then
    printf 'Not available or ambiguous in the VS2010 runtime:\n%s\n' "$found"
    status=1
fi

if [[ $status -eq 0 ]]; then
    printf 'OK: %d files, no syntax errors and no C++11 that VS2010 rejects\n' "${#files[@]}"
fi
exit $status
