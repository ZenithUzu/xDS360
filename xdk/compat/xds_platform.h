// SPDX-License-Identifier: GPL-3.0-or-later
// Small platform layer behind the std:: replacements in this folder.
// Implemented in xds_platform.cpp for the Xbox 360 XDK and, for PC tests, POSIX.
// Kept free of <xtl.h> so NooDS never sees the Windows min/max macros.
#pragma once

#if defined(_XBOX)
#include <PPCIntrinsics.h>
#endif

#if defined(_MSC_VER)
extern "C" void _ReadWriteBarrier(void);
#pragma intrinsic(_ReadWriteBarrier)
#endif

namespace xds {

typedef void (*ThreadEntry)(void *argument);

// Starts a thread on the next hardware thread from the configured list.
void *threadStart(ThreadEntry entry, void *argument);
void threadJoin(void *handle);
// Hardware threads used, in creation order, by threads NooDS starts.
void setThreadProcessors(const int *processors, int count);
#if !defined(_XBOX)
void threadYield();
#endif

void *mutexCreate();
void mutexDestroy(void *mutex);
void mutexLock(void *mutex);
bool mutexTryLock(void *mutex);
void mutexUnlock(void *mutex);

long long clockMicroseconds();
void sleepMilliseconds(int milliseconds);

// Atomic exchange on an aligned 32-bit value, with full ordering.
long atomicExchange(volatile long *target, long value);

// Keeps the compiler from moving memory accesses across this point.
inline void compilerBarrier() {
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

// Orders memory between hardware threads. On the Xbox 360 a lightweight sync
// is needed: the CPU may reorder stores, and Interlocked calls are not barriers.
inline void barrier() {
#if defined(_XBOX)
    _ReadWriteBarrier();
    __lwsync();
    _ReadWriteBarrier();
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

} // namespace xds
