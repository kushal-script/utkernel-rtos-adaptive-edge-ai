#pragma once

#include <tk/tkernel.h>

/* Task identifiers, published so the controller can change a task's priority
   at runtime. Created once in usermain, never destroyed. */

extern ID tskid_t1;
extern ID tskid_t2;
extern ID tskid_t3;
extern ID tskid_t4;
extern ID tskid_t5;
extern ID tskid_heartbeat;
extern ID tskid_bench;
