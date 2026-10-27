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
// workers are recreated every frame) always land on the same ones.
int processors[6] = { 2, 3, 4, 5 };
int processorCount = 4;
volatile long slotInUse[6];

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
typedef HANDLE Event;
Event eventCreate() { return CreateEvent(NULL, FALSE, FALSE, NULL); } // auto-reset
void eventSet(Event event) { SetEvent(event); }
void eventWait(Event event) { WaitForSingleObject(event, INFINITE); }

typedef DWORD (WINAPI *RawEntry)(LPVOID);
bool rawThreadStart(RawEntry entry, void *argument, int processor) {
    HANDLE handle = CreateThread(NULL, 256 * 1024, entry, argument, CREATE_SUSPENDED, NULL);
    if (!handle) return false;
    if (processor >= 0) XSetThreadProcessor(handle, processor);
    ResumeThread(handle);
    CloseHandle(handle); // the worker lives for the whole program
    return true;
}
#define XDS_RAW_ENTRY DWORD WINAPI
#define XDS_RAW_RETURN return 0
} // namespace

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
struct EventData {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool set;
};
typedef EventData *Event;
Event eventCreate() {
    Event event = new EventData;
    pthread_mutex_init(&event->mutex, 0);
    pthread_cond_init(&event->cond, 0);
    event->set = false;
    return event;
}
void eventSet(Event event) {
    pthread_mutex_lock(&event->mutex);
    event->set = true;
    pthread_cond_signal(&event->cond);
    pthread_mutex_unlock(&event->mutex);
}
void eventWait(Event event) {
    pthread_mutex_lock(&event->mutex);
    while (!event->set) pthread_cond_wait(&event->cond, &event->mutex);
    event->set = false; // auto-reset, like the Xbox event
    pthread_mutex_unlock(&event->mutex);
}

typedef void *(*RawEntry)(void *);
bool rawThreadStart(RawEntry entry, void *argument, int) {
    pthread_t handle;
    if (pthread_create(&handle, 0, entry, argument) != 0) return false;
    pthread_detach(handle); // the worker lives for the whole program
    return true;
}
#define XDS_RAW_ENTRY void *
#define XDS_RAW_RETURN return 0
} // namespace

void threadYield() { sched_yield(); }

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

// ---------------------------------------------------------------- thread pool
// NooDS starts its 2D and 3D threads every frame and joins them at the end of
// it. Creating a thread is slow on the Xbox, so each hardware thread gets one
// worker that sleeps between tasks and is reused. (melonDS also keeps its
// render threads alive between frames.)
namespace xds {
namespace {

struct Worker {
    Event start, done;
    ThreadEntry entry;
    void *argument;
    int slot;        // hardware-thread slot, or -1 for an extra worker
    Worker *next;    // in the list of free extra workers
};

Worker *pinned[6];   // one per slot, made on first use
Worker *spare;       // free extra workers (more threads than slots)
void *spareMutex;

XDS_RAW_ENTRY workerMain(void *parameter) {
    Worker *worker = static_cast<Worker*>(parameter);
    for (;;) {
        eventWait(worker->start);
        barrier(); // see the task written before start was set
        worker->entry(worker->argument);
        barrier(); // publish the task's writes before done is set
        eventSet(worker->done);
    }
    XDS_RAW_RETURN;
}

Worker *newWorker(int slot) {
    Worker *worker = new Worker;
    worker->start = eventCreate();
    worker->done = eventCreate();
    worker->slot = slot;
    worker->next = 0;
    if (!rawThreadStart(workerMain, worker, slot >= 0 ? processors[slot] : -1)) {
        delete worker; // shortcut: its two events leak, only if the system is out of threads
        return 0;
    }
    return worker;
}

} // namespace

void *threadStart(ThreadEntry entry, void *argument) {
    Worker *worker;
    int slot = takeSlot();
    if (slot >= 0) {
        if (!pinned[slot]) pinned[slot] = newWorker(slot);
        worker = pinned[slot];
    } else {
        if (!spareMutex) spareMutex = mutexCreate(); // shortcut: assumes the first extra start isn't concurrent
        mutexLock(spareMutex);
        worker = spare;
        if (worker) spare = worker->next;
        mutexUnlock(spareMutex);
        if (!worker) worker = newWorker(-1);
    }
    if (!worker) { // no thread available: run it here so nothing is lost
        freeSlot(slot);
        entry(argument);
        return 0;
    }
    worker->entry = entry;
    worker->argument = argument;
    barrier();
    eventSet(worker->start);
    return worker;
}

void threadJoin(void *handle) {
    if (!handle) return;
    Worker *worker = static_cast<Worker*>(handle);
    eventWait(worker->done);
    barrier();
    if (worker->slot >= 0) {
        freeSlot(worker->slot);
    } else {
        mutexLock(spareMutex);
        worker->next = spare;
        spare = worker;
        mutexUnlock(spareMutex);
    }
}

} // namespace xds
