/*
    Copyright 2019-2026 Hydr8gon

    This file is part of NooDS.

    NooDS is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdint>
#include <cstdio>
#include "xds_compat.h"

// Control how many cycles of 3D GPU commands are batched
#define GPU3D_BATCH 32

// Print critical logs in red if enabled
#if LOG_LEVEL > 0
#define LOG_CRIT(...) printf("\x1b[31m" __VA_ARGS__)
#else
#define LOG_CRIT(...) (0)
#endif

// Print warning logs in yellow if enabled
#if LOG_LEVEL > 1
#define LOG_WARN(...) printf("\x1b[33m" __VA_ARGS__)
#else
#define LOG_WARN(...) (0)
#endif

// Print info logs normally if enabled
#if LOG_LEVEL > 2
#define LOG_INFO(...) printf("\x1b[0m" __VA_ARGS__)
#else
#define LOG_INFO(...) (0)
#endif

// Compatibility toggle for systems that don't have fdopen
#ifdef NO_FDOPEN
#define fdopen(...) (0)
#define ftruncate(...) (0)
#else
#include "xds_unistd.h"
#endif

// Macro to handle differing mkdir arguments on Windows
#ifdef WINDOWS
#define MKDIR_ARGS
#else
#define MKDIR_ARGS , 0777
#endif

// Macro to force inlining
#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

// Simple bit macros
#define BIT(i) (1U << (i))
#define BITL(i) (1ULL << (i))

// Macro to swap two values
#define SWAP(a, b) { \
    auto c = a; \
    a = b; \
    b = c; \
}

// Read and write little-endian values without violating alignment or aliasing
// rules. The original pointer casts happened to work on the upstream targets,
// but are undefined in C++ and are unsafe on Xenon's 64-bit PowerPC toolchain.
// Constant-size memcpy calls inline to native loads/stores at optimization.
FORCE_INLINE uint16_t readLe16(const void *data) {
    uint16_t value;
    memcpy(&value, data, sizeof(value));
#ifdef ENDIAN_BIG
    value = XDS_BSWAP16(value);
#endif
    return value;
}

FORCE_INLINE uint32_t readLe32(const void *data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));
#ifdef ENDIAN_BIG
    value = XDS_BSWAP32(value);
#endif
    return value;
}

// The interpreter and mapped-memory paths always align 16/32-bit addresses
// before reaching these helpers. Tell the compiler that explicitly so Xenon
// uses one native-width load plus a byte swap instead of rebuilding every
// value from four byte loads. The may_alias typedefs keep the access valid
// when the underlying storage is a byte array.
typedef uint16_t AliasUint16;
typedef uint32_t AliasUint32;

FORCE_INLINE uint16_t readLe16Aligned(const void *data) {
    uint16_t value = *static_cast<const AliasUint16*>(data);
#ifdef ENDIAN_BIG
    value = XDS_BSWAP16(value);
#endif
    return value;
}

FORCE_INLINE uint32_t readLe32Aligned(const void *data) {
    uint32_t value = *static_cast<const AliasUint32*>(data);
#ifdef ENDIAN_BIG
    value = XDS_BSWAP32(value);
#endif
    return value;
}

FORCE_INLINE uint64_t readLe64(const void *data) {
    uint64_t value;
    memcpy(&value, data, sizeof(value));
#ifdef ENDIAN_BIG
    value = XDS_BSWAP64(value);
#endif
    return value;
}

FORCE_INLINE void writeLe32(void *data, uint32_t value) {
#ifdef ENDIAN_BIG
    value = XDS_BSWAP32(value);
#endif
    memcpy(data, &value, sizeof(value));
}

FORCE_INLINE void writeLe16Aligned(void *data, uint16_t value) {
#ifdef ENDIAN_BIG
    value = XDS_BSWAP16(value);
#endif
    *static_cast<AliasUint16*>(data) = value;
}

FORCE_INLINE void writeLe32Aligned(void *data, uint32_t value) {
#ifdef ENDIAN_BIG
    value = XDS_BSWAP32(value);
#endif
    *static_cast<AliasUint32*>(data) = value;
}

#define U8TO16(data, index) readLe16(&(data)[index])
#define U8TO32(data, index) readLe32(&(data)[index])
#define U8TO64(data, index) readLe64(&(data)[index])
#define U32TO8(data, index, value) writeLe32(&(data)[index], (value))

FORCE_INLINE uint32_t rotateRight32(uint32_t value, unsigned int shift) {
    shift &= 31u;
    return shift ? ((value >> shift) | (value << (32u - shift))) : value;
}

// Macros that perform bit rotations
#define ROL8(v, s) ((v << s) | (v >> (8 - s)))
#define ROL32(v, s) ((v << s) | (v >> (32 - s)))
#define ROR32(v, s) rotateRight32((v), (s))
