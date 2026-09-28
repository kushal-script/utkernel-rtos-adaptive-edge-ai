#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "ipc_objects.h"
#include "kws_features.h"
#include "signal_source.h"
#include "t1_ingest.h"
#include "t2_variance.h"
#include "t3_features.h"
#include "t4_inference.h"
#include "t5_controller.h"

#if BENCH_ENABLE
#include "bench_harness.h"
#endif

ID tskid_t1;
ID tskid_t2;
ID tskid_t3;
ID tskid_t4;
ID tskid_t5;
ID tskid_heartbeat;
ID tskid_bench;

/* LD2 on PA5, a system alive indicator. */
LOCAL void task_heartbeat(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    while (1) {
        out_w(GPIO_ODR(A), in_w(GPIO_ODR(A)) ^ (1 << 5));
        tk_dly_tsk(500);
    }
}

typedef struct {
    ID       *slot;
    FP        entry;
    PRI       priority;
    SZ        stack;
    const char *name;
} task_spec_t;

LOCAL const task_spec_t task_table[] = {
    { &tskid_t1,        (FP)t1_ingest_task,      PRI_T1_INGEST,     STACK_MEDIUM, "T1 ingest"     },
    { &tskid_t2,        (FP)t2_variance_task,    PRI_T2_VARIANCE,   STACK_MEDIUM, "T2 variance"   },
    { &tskid_t3,        (FP)t3_features_task,    PRI_T3_FEATURES,   STACK_LARGE,  "T3 features"   },
    { &tskid_t4,        (FP)t4_inference_task,   PRI_T4_INFERENCE,  STACK_LARGE,  "T4 inference"  },
    { &tskid_t5,        (FP)t5_controller_task,  PRI_T5_CONTROLLER, STACK_MEDIUM, "T5 controller" },
    { &tskid_heartbeat, (FP)task_heartbeat,      PRI_HEARTBEAT,     STACK_SMALL,  "heartbeat"     },
#if BENCH_ENABLE
    { &tskid_bench,     (FP)bench_task,          PRI_BENCH,         STACK_LARGE,  "benchmark"     },
#endif
};

#define TASK_COUNT (sizeof(task_table) / sizeof(task_table[0]))

EXPORT INT usermain(void)
{
    tm_putstring((UB *)"RTOS coupled adaptive keyword spotting\n");

    out_w(GPIO_ODR(A), in_w(GPIO_ODR(A)) & ~(1 << 5));

    kws_features_init();

    if (ipc_objects_init() != E_OK) {
        tm_putstring((UB *)"usermain: kernel objects failed, halting\n");
        tk_slp_tsk(TMO_FEVR);
        return -1;
    }

    for (unsigned i = 0; i < TASK_COUNT; i++) {
        T_CTSK ctsk = {
            .exinf   = NULL,
            .tskatr  = TA_HLNG | TA_RNG3,
            .task    = task_table[i].entry,
            .itskpri = task_table[i].priority,
            .stksz   = task_table[i].stack,
        };
        ID id = tk_cre_tsk(&ctsk);
        if (id < E_OK) {
            /* Fatal: an uncreated identifier is zero, which would retarget priority changes to the caller. */
            tm_printf((UB *)"usermain: create %s failed %d, halting\n",
                      task_table[i].name, (int)id);
            tk_slp_tsk(TMO_FEVR);
            return -1;
        }
        *task_table[i].slot = id;
        if (tk_sta_tsk(id, 0) < E_OK) {
            tm_printf((UB *)"usermain: start %s failed, halting\n",
                      task_table[i].name);
            tk_slp_tsk(TMO_FEVR);
            return -1;
        }
    }

    tk_slp_tsk(TMO_FEVR);
    return 0;
}
