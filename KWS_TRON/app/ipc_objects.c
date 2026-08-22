#include "ipc_objects.h"

#include <tm/tmonitor.h>

#include "app_config.h"

ID flgid_capture;
ID flgid_pipeline;
ID mbxid_window;
ID mplid_layer;

adapt_state_t adapt_state = {
    .window_samples  = T1_WINDOW_DEFAULT,
    .active_frames   = T3_ACTIVE_FRAMES_MAX,
    .precision_mask  = 0,
    .deadline_cycles = T4_DEADLINE_CYCLES,
    .vad_threshold   = 0,
};

/* Backing store for the layer streaming pool. Sized for the largest layer's
   weights in either precision plus the pool's own block headers. */
static uint8_t layer_pool_buffer[KWS_LAYER_POOL_BYTES] __attribute__((aligned(8)));

ER ipc_objects_init(void)
{
    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };

    flgid_capture = tk_cre_flg(&cflg);
    if (flgid_capture < E_OK) {
        tm_printf((UB *)"ipc: cre_flg capture failed %d\n", (int)flgid_capture);
        return (ER)flgid_capture;
    }

    flgid_pipeline = tk_cre_flg(&cflg);
    if (flgid_pipeline < E_OK) {
        tm_printf((UB *)"ipc: cre_flg pipeline failed %d\n", (int)flgid_pipeline);
        return (ER)flgid_pipeline;
    }

    T_CMBX cmbx = {
        .exinf  = NULL,
        .mbxatr = TA_TFIFO | TA_MFIFO,
    };
    mbxid_window = tk_cre_mbx(&cmbx);
    if (mbxid_window < E_OK) {
        tm_printf((UB *)"ipc: cre_mbx window failed %d\n", (int)mbxid_window);
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
        tm_printf((UB *)"ipc: cre_mpl layer failed %d\n", (int)mplid_layer);
        return (ER)mplid_layer;
    }

    return E_OK;
}
