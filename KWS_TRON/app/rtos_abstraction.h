#pragma once
#include <tk/tkernel.h>

/* Event flags */
static inline void rtos_set_flag(ID flgid, UINT ptn) {
    tk_set_flg(flgid, ptn);
}
static inline void rtos_wait_flag(ID flgid, UINT ptn, UINT *out) {
    tk_wai_flg(flgid, ptn, TWF_ORW | TWF_BITCLR, out, TMO_FEVR);
}
static inline BOOL rtos_peek_flag(ID flgid, UINT ptn) {
    T_RFLG rflag;
    if (tk_ref_flg(flgid, &rflag) != E_OK) return FALSE;
    return (rflag.flgptn & ptn) ? TRUE : FALSE;
}

/* Mailbox */
static inline void rtos_send_mbx(ID mbxid, T_MSG *msg) {
    tk_snd_mbx(mbxid, msg);
}
static inline T_MSG* rtos_recv_mbx(ID mbxid) {
    T_MSG *msg;
    tk_rcv_mbx(mbxid, &msg, TMO_FEVR);
    return msg;
}

/* Task priority */
static inline void rtos_change_priority(ID tskid, PRI pri) {
    tk_chg_pri(tskid, pri);
}

/* Memory pool */
static inline void* rtos_get_mpl(ID mplid, UINT sz) {
    VP blk;
    tk_get_mpl(mplid, sz, &blk, TMO_FEVR);
    return blk;
}
static inline void rtos_rel_mpl(ID mplid, void *blk) {
    tk_rel_mpl(mplid, blk);
}
