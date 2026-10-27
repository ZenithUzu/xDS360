// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-in for <unistd.h>, <sys/stat.h> and <dirent.h>, which the XDK lacks.
// On the Xbox (and in the PC simulator, XDS_SIM) they go through
// xds_files.cpp, which turns NooDS's "/" paths into Xbox paths ("game:\x")
// and lists the drives (game:, hdd:, usb0:...) for the root path "".
#pragma once

#if defined(_XBOX) || defined(XDS_SIM)

int xdsMakeDirectory(const char *path);
inline int xdsMkdir(const char *path, int = 0) { return xdsMakeDirectory(path); }
#define mkdir(...) xdsMkdir(__VA_ARGS__)

struct dirent {
    char d_name[260];
};
struct XdsDir;
typedef XdsDir DIR;
DIR *opendir(const char *path);
dirent *readdir(DIR *dir);
int closedir(DIR *dir);

// Only st_mode is used (folder or file). "struct stat x; stat(path, &x)"
// becomes a struct and a function both named XdsStat.
struct XdsStat {
    unsigned st_mode;
};
int XdsStat(const char *path, struct XdsStat *info);
#define stat XdsStat
#ifndef S_IFDIR
#define S_IFDIR 0x4000
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFDIR) != 0)
#endif

#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
