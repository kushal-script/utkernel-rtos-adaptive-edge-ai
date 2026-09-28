#include "uthread.h"

#include <stdlib.h>

#if defined(_WIN32)

#include <process.h>

void umutex_init(umutex_t *m)    { InitializeCriticalSection(m); }
void umutex_destroy(umutex_t *m) { DeleteCriticalSection(m); }
void umutex_lock(umutex_t *m)    { EnterCriticalSection(m); }
void umutex_unlock(umutex_t *m)  { LeaveCriticalSection(m); }

void ucond_init(ucond_t *c)      { InitializeConditionVariable(c); }
void ucond_destroy(ucond_t *c)   { (void)c; }
void ucond_wait(ucond_t *c, umutex_t *m)
{
    SleepConditionVariableCS(c, m, INFINITE);
}

int ucond_wait_ms(ucond_t *c, umutex_t *m, uint32_t ms)
{
    if (SleepConditionVariableCS(c, m, (DWORD)ms)) {
        return 0;
    }
    return GetLastError() == ERROR_TIMEOUT ? 1 : 0;
}

void ucond_signal(ucond_t *c)    { WakeConditionVariable(c); }
void ucond_broadcast(ucond_t *c) { WakeAllConditionVariable(c); }

typedef struct {
    void (*entry)(void *);
    void  *arg;
} trampoline_t;

static unsigned __stdcall win_trampoline(void *raw)
{
    trampoline_t *t = (trampoline_t *)raw;
    void (*entry)(void *) = t->entry;
    void *arg = t->arg;
    free(t);
    entry(arg);
    return 0;
}

int uthread_start(uthread_t *t, void (*entry)(void *), void *arg)
{
    trampoline_t *tr = (trampoline_t *)malloc(sizeof(*tr));
    if (tr == NULL) {
        return -1;
    }
    tr->entry = entry;
    tr->arg   = arg;
    /* Stack size zero takes the image default, larger than any stksz the firmware asks for. */
    uintptr_t h = _beginthreadex(NULL, 0, win_trampoline, tr, 0, NULL);
    if (h == 0) {
        free(tr);
        return -1;
    }
    *t = (HANDLE)h;
    return 0;
}

void uthread_join(uthread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

uint64_t umonotonic_us(void)
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0) {
        return 0;
    }
    /* Divide before scaling, the product overflows int64 after about ten days. */
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000u
         + (uint64_t)(((now.QuadPart % freq.QuadPart) * 1000000LL) / freq.QuadPart);
}

void usleep_us(uint64_t us)
{
    /* Sleep rounds sub millisecond waits up rather than busy spinning. */
    DWORD ms = (DWORD)((us + 999u) / 1000u);
    Sleep(ms);
}

#else

#include <errno.h>
#include <time.h>
#include <unistd.h>

void umutex_init(umutex_t *m)    { pthread_mutex_init(m, NULL); }
void umutex_destroy(umutex_t *m) { pthread_mutex_destroy(m); }
void umutex_lock(umutex_t *m)    { pthread_mutex_lock(m); }
void umutex_unlock(umutex_t *m)  { pthread_mutex_unlock(m); }

void ucond_init(ucond_t *c)      { pthread_cond_init(c, NULL); }
void ucond_destroy(ucond_t *c)   { pthread_cond_destroy(c); }
void ucond_wait(ucond_t *c, umutex_t *m) { pthread_cond_wait(c, m); }

int ucond_wait_ms(ucond_t *c, umutex_t *m, uint32_t ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec  += 1;
    }
    return pthread_cond_timedwait(c, m, &ts) == ETIMEDOUT ? 1 : 0;
}

void ucond_signal(ucond_t *c)    { pthread_cond_signal(c); }
void ucond_broadcast(ucond_t *c) { pthread_cond_broadcast(c); }

typedef struct {
    void (*entry)(void *);
    void  *arg;
} trampoline_t;

static void *posix_trampoline(void *raw)
{
    trampoline_t *t = (trampoline_t *)raw;
    void (*entry)(void *) = t->entry;
    void *arg = t->arg;
    free(t);
    entry(arg);
    return NULL;
}

int uthread_start(uthread_t *t, void (*entry)(void *), void *arg)
{
    trampoline_t *tr = (trampoline_t *)malloc(sizeof(*tr));
    if (tr == NULL) {
        return -1;
    }
    tr->entry = entry;
    tr->arg   = arg;
    if (pthread_create(t, NULL, posix_trampoline, tr) != 0) {
        free(tr);
        return -1;
    }
    return 0;
}

void uthread_join(uthread_t t)
{
    pthread_join(t, NULL);
}

uint64_t umonotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
}

void usleep_us(uint64_t us)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

#endif
