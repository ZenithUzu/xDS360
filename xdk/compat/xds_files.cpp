// SPDX-License-Identifier: GPL-3.0-or-later
// File functions for NooDS on the Xbox 360 (see xds_unistd.h). Only uses the
// Win32-style API, so the PC simulator can run it too.
#define NOMINMAX
#include <xtl.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include "xds_unistd.h"

#undef stat

namespace {

// Drives shown for the root path "" (see the console UI's file browser).
const char *const kDrives[] = { "game:", "hdd:", "usb0:", "usb1:", "usb2:" };
const int kDriveCount = sizeof(kDrives) / sizeof(kDrives[0]);

// "/hdd:/Games/x.nds" -> "hdd:\Games\x.nds"; "/hdd:" -> "hdd:\".
std::string xboxPath(const char *path) {
    std::string out = path;
    while (!out.empty() && (out[0] == '/' || out[0] == '\\')) out.erase(0, 1);
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] == '/') out[i] = '\\';
    if (!out.empty() && out[out.size() - 1] == ':') out += '\\';
    return out;
}

bool isFolder(const std::string &path) {
    DWORD attributes = GetFileAttributes(path.c_str());
    if (attributes != (DWORD)-1) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    // A drive root may not report attributes; it is a folder if it can be listed.
    if (path.size() > 1 && path[path.size() - 2] == ':') {
        WIN32_FIND_DATA data;
        HANDLE find = FindFirstFile((path + "*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            FindClose(find);
            return true;
        }
    }
    return false;
}

} // namespace

struct XdsDir {
    HANDLE find;
    bool pending; // `data` holds an entry not yet returned
    int drive;    // next drive to check when listing the root (-1 otherwise)
    WIN32_FIND_DATA data;
    dirent entry;
};

FILE *xdsFopen(const char *path, const char *mode) {
    return fopen(xboxPath(path).c_str(), mode);
}

int xdsMakeDirectory(const char *path) {
    return CreateDirectory(xboxPath(path).c_str(), NULL) ? 0 : -1;
}

DIR *opendir(const char *path) {
    XdsDir *dir = new XdsDir;
    dir->find = INVALID_HANDLE_VALUE;
    dir->pending = false;
    dir->drive = -1;
    std::string folder = xboxPath(path);
    if (folder.empty()) {
        dir->drive = 0;
        return dir;
    }
    if (folder[folder.size() - 1] != '\\') folder += '\\';
    dir->find = FindFirstFile((folder + "*").c_str(), &dir->data);
    dir->pending = dir->find != INVALID_HANDLE_VALUE;
    return dir; // an empty folder still opens (NooDS does not check for null)
}

dirent *readdir(DIR *dir) {
    if (dir->drive >= 0) {
        while (dir->drive < kDriveCount) {
            const char *drive = kDrives[dir->drive++];
            if (isFolder(std::string(drive) + "\\")) {
                strcpy(dir->entry.d_name, drive);
                return &dir->entry;
            }
        }
        return 0;
    }
    while (dir->pending) {
        strncpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name) - 1);
        dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = 0;
        dir->pending = FindNextFile(dir->find, &dir->data) != FALSE;
        if (strcmp(dir->entry.d_name, ".") && strcmp(dir->entry.d_name, ".."))
            return &dir->entry;
    }
    return 0;
}

int closedir(DIR *dir) {
    if (dir->find != INVALID_HANDLE_VALUE) FindClose(dir->find);
    delete dir;
    return 0;
}

int XdsStat(const char *path, struct XdsStat *info) {
    std::string xbox = xboxPath(path);
    const bool folder = isFolder(xbox);
    if (!folder && GetFileAttributes(xbox.c_str()) == (DWORD)-1) return -1;
    info->st_mode = folder ? S_IFDIR : 0;
    return 0;
}
