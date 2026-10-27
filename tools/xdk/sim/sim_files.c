/* SPDX-License-Identifier: GPL-3.0-or-later
 * PC simulator: makes fopen/remove/rename understand Xbox paths such as
 * "game:\xds360.log" by mapping them into $SIM_ROOT, then calls the real ones. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *hostPath(const char *path, char *out, size_t size) {
    const char *colon = strchr(path, ':');
    const char *root = getenv("SIM_ROOT");
    if (colon && colon - path < 10 && root) {
        size_t drive = (size_t)(colon - path);
        if (drive == 4 && strncmp(path, "game", 4) == 0)
            snprintf(out, size, "%s/%s", root, colon + 1);
        else
            snprintf(out, size, "%s/%.*s/%s", root, (int)drive, path, colon + 1);
    } else {
        snprintf(out, size, "%s", path);
    }
    for (char *p = out; *p; p++)
        if (*p == '\\') *p = '/';
    return out;
}

FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    char host[1024], m[16];
    size_t j = 0;
    for (size_t i = 0; mode[i] && j < sizeof(m) - 1; i++)
        if (mode[i] != 'c') m[j++] = mode[i]; /* "c" (commit) is Xbox-only */
    m[j] = 0;
    return real(hostPath(path, host, sizeof(host)), m);
}

int remove(const char *path) {
    static int (*real)(const char *);
    if (!real) real = (int (*)(const char *))dlsym(RTLD_NEXT, "remove");
    char host[1024];
    return real(hostPath(path, host, sizeof(host)));
}

int rename(const char *from, const char *to) {
    static int (*real)(const char *, const char *);
    if (!real) real = (int (*)(const char *, const char *))dlsym(RTLD_NEXT, "rename");
    char a[1024], b[1024];
    return real(hostPath(from, a, sizeof(a)), hostPath(to, b, sizeof(b)));
}
