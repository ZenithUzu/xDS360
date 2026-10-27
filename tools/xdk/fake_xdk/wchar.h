// Fake XDK <wchar.h>: the system one, plus the "struct stat" the XDK's
// version declares (this caught nothing until VS2010 failed on it).
#pragma once
#include_next <wchar.h>
#if defined(_XBOX) && !defined(_STAT_DEFINED)
struct stat {
    unsigned st_mode;
    long st_size;
};
#define _STAT_DEFINED
#endif
