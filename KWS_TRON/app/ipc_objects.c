#include "ipc_objects.h"

#include <tm/tmonitor.h>

#include "app_config.h"

ID flgid_capture;
ID flgid_features;
ID flgid_inference;
ID flgid_control;
ID flgid_gate;
ID mbxid_window;
ID mplid_layer;

adapt_state_t adapt_state = {
    .window_samples  = T1_WINDOW_DEFAULT,
    .active_frames   = T3_ACTIVE_FRAMES_MAX,
    .precision_mask  = 0,
    .deadline_cycles = T4_DEADLINE_CYCLES,
    .vad_threshold   = 0,
};

/* Backing store for the layer streaming pool, supplied by the application so
   the kernel heap is not involved. */
static uint8_t layer_pool_buffer[KWS_LAYER_POOL_BYTES] __attribute__((aligned(8)));

static ER create_flag(ID *slot, const char *name)
{
    /* TA_WMUL so a flag can have more than one waiter, which the pipeline
       needs even though each object serves a single consumer today. */
    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };
    ID id = tk_cre_flg(&cflg);
    if (id < E_OK) {
        tm_printf((UB *)"ipc: flag %s failed %d\n", name, (int)id);
        return (ER)id;
    }
    *slot = id;
    return E_OK;
}

ER ipc_objects_init(void)
{
    ER err;

    if ((err = create_flag(&flgid_capture,   "capture"))   != E_OK) return err;
    if ((err = create_flag(&flgid_features,  "features"))  != E_OK) return err;
    if ((err = create_flag(&flgid_inference, "inference")) != E_OK) return err;
    if ((err = create_flag(&flgid_control,   "control"))   != E_OK) return err;
    if ((err = create_flag(&flgid_gate,      "gate"))      != E_OK) return err;

    T_CMBX cmbx = {
        .exinf  = NULL,
        .mbxatr = TA_TFIFO | TA_MFIFO,
    };
    mbxid_window = tk_cre_mbx(&cmbx);
    if (mbxid_window < E_OK) {
        tm_printf((UB *)"ipc: mailbox failed %d\n", (int)mbxid_window);
        return (ER)mbxid_window;
    }

    T_CMPL cmpl = {
        .exinf  = NULL,
        .mplatr = TA_TFIFO | TA_USERBUF,
        .mplsz  = sizeof(layer_pool_buffer),
        .bufptr = layer_pool_buffer,
    };
    mplid_layer = tk_cre_mpl(&cmpl);
    if (mplid_layer < E_OK) {
        tm_printf((UB *)"ipc: memory pool failed %d\n", (int)mplid_layer);
        return (ER)mplid_layer;
    }

    return E_OK;
}
