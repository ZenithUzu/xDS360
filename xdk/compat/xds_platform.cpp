// SPDX-License-Identifier: GPL-3.0-or-later
// Platform layer for the std:: replacements: Xbox 360 XDK, or POSIX for PC tests.
#include "xds_platform.h"

#if defined(_XBOX)
#include <xtl.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace xds {
namespace {

// Hardware threads for the threads NooDS starts. A new thread takes the first
// free one and gives it back when joined, so threads that come and go (the 3D
// workers are recreated when settings change) always land on the same ones.
int processors[6] = { 2, 3, 4, 5 };
int processorCount = 4;
volatile long slotInUse[6];

struct Start {
    ThreadEntry entry;
    void *argument;
};

int takeSlot() {
    for (int i = 0; i < processorCount; i++)
        if (atomicExchange(&slotInUse[i], 1) == 0) return i;
    return -1; // more threads than listed: let the system place it
}

void freeSlot(int slot) {
    if (slot >= 0) atomicExchange(&slotInUse[slot], 0);
}

} // namespace

void setThreadProcessors(const int *list, int count) {
    if (count < 1) return;
    if (count > 6) count = 6;
    for (int i = 0; i < count; i++)
        processors[i] = list[i];
    processorCount = count;
}

#if defined(_XBOX)

namespace {
DWORD WINAPI threadMain(LPVOID parameter) {
    Start start = *static_cast<Start*>(parameter);
    delete static_cast<Start*>(parameter);
    MemoryBarrier();
    start.entry(start.argument);
    MemoryBarrier();
    return 0;
}
} // namespace

struct Thread {
    HANDLE handle;
    int slot;
};

void *threadStart(ThreadEntry entry, void *argument) {
    Start *start = new Start;
    start->entry = entry;
    start->argument = argument;
    MemoryBarrier(); // the thread must see the task fully built
    HANDLE handle = CreateThread(NULL, 256 * 1024, threadMain, start, CREATE_SUSPENDED, NULL);
    if (!handle) {
        delete start;
        entry(argument); // no thread available: run it here so nothing is lost
        return 0;
    }
    Thread *thread = new Thread;
    thread->handle = handle;
    thread->slot = takeSlot();
    if (thread->slot >= 0) XSetThreadProcessor(handle, processors[thread->slot]);
    ResumeThread(handle);
    return thread;
}

void threadJoin(void *handle) {
    if (!handle) return;
    Thread *thread = static_cast<Thread*>(handle);
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    MemoryBarrier();
    freeSlot(thread->slot);
    delete thread;
}

void *mutexCreate() {
    CRITICAL_SECTION *section = new CRITICAL_SECTION;
    InitializeCriticalSection(section);
    return section;
}

void mutexDestroy(void *mutex) {
    DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(mutex));
    delete static_cast<CRITICAL_SECTION*>(mutex);
}

void mutexLock(void *mutex) { EnterCriticalSection(static_cast<CRITICAL_SECTION*>(mutex)); }
bool mutexTryLock(void *mutex) { return TryEnterCriticalSection(static_cast<CRITICAL_SECTION*>(mutex)) != FALSE; }
void mutexUnlock(void *mutex) { LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(mutex)); }

long long clockMicroseconds() {
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (long long)(now.QuadPart / frequency.QuadPart) * 1000000 +
        (long long)(now.QuadPart % frequency.QuadPart) * 1000000 / frequency.QuadPart;
}

void sleepMilliseconds(int milliseconds) { Sleep(milliseconds); }

long atomicExchange(volatile long *target, long value) {
    MemoryBarrier();
    long old = InterlockedExchange(target, value);
    MemoryBarrier();
    return old;
}

} // namespace xds

#else // POSIX, for tests on a PC

namespace {
void *threadMain(void *parameter) {
    Start start = *static_cast<Start*>(parameter);
    delete static_cast<Start*>(parameter);
    start.entry(start.argument);
    return 0;
}
} // namespace

struct Thread {
    pthread_t handle;
    int slot;
};

void *threadStart(ThreadEntry entry, void *argument) {
    Start *start = new Start;
    start->entry = entry;
    start->argument = argument;
    Thread *thread = new Thread;
    if (pthread_create(&thread->handle, 0, threadMain, start) != 0) {
        delete start;
        delete thread;
        entry(argument);
        return 0;
    }
    thread->slot = takeSlot(); // only bookkeeping on a PC
    return thread;
}

void threadYield() { sched_yield(); }

void threadJoin(void *handle) {
    if (!handle) return;
    Thread *thread = static_cast<Thread*>(handle);
    pthread_join(thread->handle, 0);
    freeSlot(thread->slot);
    delete thread;
}

void *mutexCreate() {
    pthread_mutex_t *mutex = new pthread_mutex_t;
    pthread_mutex_init(mutex, 0);
    return mutex;
}

void mutexDestroy(void *mutex) {
    pthread_mutex_destroy(static_cast<pthread_mutex_t*>(mutex));
    delete static_cast<pthread_mutex_t*>(mutex);
}

void mutexLock(void *mutex) { pthread_mutex_lock(static_cast<pthread_mutex_t*>(mutex)); }
bool mutexTryLock(void *mutex) { return pthread_mutex_trylock(static_cast<pthread_mutex_t*>(mutex)) == 0; }
void mutexUnlock(void *mutex) { pthread_mutex_unlock(static_cast<pthread_mutex_t*>(mutex)); }

long long clockMicroseconds() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

void sleepMilliseconds(int milliseconds) {
    timespec wait = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
    nanosleep(&wait, 0);
}

long atomicExchange(volatile long *target, long value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

} // namespace xds

#endif
