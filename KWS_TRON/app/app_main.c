#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "app_config.h"
#include "ipc_objects.h"
#include "t1_dma_ingest.h"
#include "audio_probe.h"

/* LD2 (PA5) toggles every 500 ms as a system alive indicator. */
LOCAL void task_heartbeat(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    UW tick = 0;
    while (1) {
        out_w(GPIO_ODR(A), in_w(GPIO_ODR(A)) ^ (1 << 5));
#if !KWS_AUDIO_PROBE
        tm_printf((UB*)"hb %u\n", (unsigned)tick++);
#endif
        (void)tick;
        tk_dly_tsk(500);
    }
}

LOCAL ID tskid_hb;
LOCAL ID tskid_t1;

LOCAL T_CTSK ctsk_hb = {
    .itskpri = 10,
    .stksz   = 1024,
    .task    = task_heartbeat,
    .tskatr  = TA_HLNG | TA_RNG3,
};

LOCAL T_CTSK ctsk_t1 = {
    .itskpri = 5,
    .stksz   = 2048,
    .task    = t1_dma_ingest_task,
    .tskatr  = TA_HLNG | TA_RNG3,
};

#if KWS_AUDIO_PROBE
LOCAL ID tskid_probe;
LOCAL T_CTSK ctsk_probe = {
    .itskpri = 8,
    .stksz   = 2048,
    .task    = audio_probe_task,
    .tskatr  = TA_HLNG | TA_RNG3,
};
#endif

EXPORT INT usermain(void)
{
    tm_putstring((UB*)"Start User-main program.\n");

    out_w(GPIO_ODR(A), in_w(GPIO_ODR(A)) & ~(1 << 5));

    ipc_objects_init();

    tskid_hb = tk_cre_tsk(&ctsk_hb);
    tk_sta_tsk(tskid_hb, 0);

    tskid_t1 = tk_cre_tsk(&ctsk_t1);
    tk_sta_tsk(tskid_t1, 0);

#if KWS_AUDIO_PROBE
    tskid_probe = tk_cre_tsk(&ctsk_probe);
    tk_sta_tsk(tskid_probe, 0);
#endif

    tk_slp_tsk(TMO_FEVR);
    return 0;
}
