// MSVC C runtime names used by xDS360, mapped for PC syntax checks only.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _stricmp strcasecmp
inline unsigned short _byteswap_ushort(unsigned short v) { return __builtin_bswap16(v); }
inline unsigned long _byteswap_ulong(unsigned long v) { return __builtin_bswap32((unsigned int)v); }
inline unsigned long long _byteswap_uint64(unsigned long long v) { return __builtin_bswap64(v); }
#define _strtoi64 strtoll
#define _strtoui64 strtoull
