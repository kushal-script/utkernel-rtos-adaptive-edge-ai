#pragma once

#include <stdint.h>

/* Threads, mutexes and condition variables across the three hosts this tool
   supports. C11 threads.h is not available on Apple's toolchain, so this maps
   to pthreads on macOS and Linux and to the Win32 primitives elsewhere. */

#if defined(_WIN32)

#include <windows.h>

typedef HANDLE              uthread_t;
typedef CRITICAL_SECTION    umutex_t;
typedef CONDITION_VARIABLE  ucond_t;

#else

#include <pthread.h>

typedef pthread_t           uthread_t;
typedef pthread_mutex_t     umutex_t;
typedef pthread_cond_t      ucond_t;

#endif

void umutex_init(umutex_t *m);
void umutex_destroy(umutex_t *m);
void umutex_lock(umutex_t *m);
void umutex_unlock(umutex_t *m);

void ucond_init(ucond_t *c);
void ucond_destroy(ucond_t *c);
void ucond_wait(ucond_t *c, umutex_t *m);
/* Returns 1 when the wait timed out, 0 when it was signalled. */
int  ucond_wait_ms(ucond_t *c, umutex_t *m, uint32_t ms);
void ucond_signal(ucond_t *c);
void ucond_broadcast(ucond_t *c);

int  uthread_start(uthread_t *t, void (*entry)(void *), void *arg);
void uthread_join(uthread_t t);

/* Monotonic microseconds since an arbitrary origin, the base for both the
   virtual device cycle clock and the capture pacing. */
uint64_t umonotonic_us(void);
void     usleep_us(uint64_t us);
