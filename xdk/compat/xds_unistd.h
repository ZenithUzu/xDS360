// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-in for <unistd.h> and <sys/stat.h>, which the XDK does not have.
// NooDS only needs mkdir() here; NO_FDOPEN already removes fdopen/ftruncate.
#pragma once

#if defined(_MSC_VER)
int xdsMakeDirectory(const char *path); // xds_platform.cpp
inline int xdsMkdir(const char *path, int = 0) { return xdsMakeDirectory(path); }
#define mkdir(...) xdsMkdir(__VA_ARGS__)
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
