// SPDX-License-Identifier: GPL-3.0-or-later
// Replacements for GCC-only features used by NooDS, included from defines.h.
#pragma once

#include <new>
#include <string.h>

#if defined(_MSC_VER)
#include <stdlib.h>
#define XDS_BSWAP16(x) _byteswap_ushort(x)
#define XDS_BSWAP32(x) _byteswap_ulong(x)
#define XDS_BSWAP64(x) _byteswap_uint64(x)
// C99 names that the VS2010 C library spells differently.
#define strtoll _strtoi64
#define strtoull _strtoui64
#else
#define XDS_BSWAP16(x) __builtin_bswap16(x)
#define XDS_BSWAP32(x) __builtin_bswap32(x)
#define XDS_BSWAP64(x) __builtin_bswap64(x)
#endif

// Folder separator in file paths: the Xbox 360 only accepts "\\".
#if defined(_XBOX)
#define XDS_SLASH "\\"
#else
#define XDS_SLASH "/"
#endif

// A fixed array of objects that have no default constructor. VS2010 cannot
// build `Dma dma[2]` from `dma { Dma(this, 0), Dma(this, 1) }`, so each element
// is built in place, in order, exactly where the plain array would be.
// Elements are never copied (Interpreter keeps pointers into itself).
template <typename T, int N>
class XdsArray {
public:
    // Builds T(argument, 0), T(argument, 1), ...
    template <typename A>
    explicit XdsArray(A argument) {
        for (int i = 0; i < N; i++)
            new (slot(i)) T(argument, i);
    }

    // Builds three elements from copies (HleBios has no self-references).
    XdsArray(const T &a, const T &b, const T &c) {
        new (slot(0)) T(a);
        new (slot(1)) T(b);
        new (slot(2)) T(c);
    }

    ~XdsArray() {
        for (int i = N - 1; i >= 0; i--)
            slot(i)->~T();
    }

    T &operator[](int i) { return *slot(i); }
    const T &operator[](int i) const { return *slot(i); }

private:
    XdsArray(const XdsArray&);
    XdsArray &operator=(const XdsArray&);

    T *slot(int i) { return reinterpret_cast<T*>(storage_.bytes) + i; }
    const T *slot(int i) const { return reinterpret_cast<const T*>(storage_.bytes) + i; }

    union {
        unsigned char bytes[N * sizeof(T)];
        double alignDouble;
        long long alignLong;
        void *alignPointer;
    } storage_;
};
