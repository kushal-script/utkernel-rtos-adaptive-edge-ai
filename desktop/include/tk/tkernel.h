#pragma once

#include <stddef.h>
#include <stdint.h>

/* The uT-Kernel 3.0 subset the pipeline calls, mirroring KWS_TRON/mtk3, see desktop/README.md. */

typedef signed char         B;
typedef short               H;
typedef int                 W;
typedef unsigned char       UB;
typedef unsigned short      UH;
typedef unsigned int        UW;

typedef int                 INT;
typedef unsigned int        UINT;

typedef int                 BOOL;
typedef int                 ER;
typedef int                 ID;
typedef int                 PRI;
typedef unsigned int        SZ;
typedef int                 TMO;
typedef unsigned int        ATR;

typedef void               *VP;
typedef void              (*FP)(void);
typedef intptr_t            VP_INT;

#define LOCAL   static
#define EXPORT
#define IMPORT  extern
#define CONST   const

#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

#define E_OK        (0)
#define E_SYS       (-5)
#define E_RSATR     (-11)
#define E_PAR       (-17)
#define E_ID        (-18)
#define E_CTX       (-25)
#define E_LIMIT     (-34)
#define E_OBJ       (-41)
#define E_NOEXS     (-42)
#define E_TMOUT     (-50)

#define TMO_POL     (0)
#define TMO_FEVR    (-1)

#define TA_TFIFO    (0x00000000u)
#define TA_TPRI     (0x00000001u)
#define TA_WMUL     (0x00000008u)
#define TA_MFIFO    (0x00000000u)
#define TA_MPRI     (0x00000002u)
#define TA_USERBUF  (0x00000020u)
#define TA_HLNG     (0x00000001u)
#define TA_RNG0     (0x00000000u)
#define TA_RNG3     (0x00000300u)

#define TWF_ANDW    (0x00000000u)
#define TWF_ORW     (0x00000001u)
#define TWF_CLR     (0x00000010u)
#define TWF_BITCLR  (0x00000020u)

/* Message header the kernel links through, the sender's storage must outlive the send. */
typedef struct t_msg {
    void *msgque[1];
} T_MSG;

typedef struct t_cflg {
    void *exinf;
    ATR   flgatr;
    UINT  iflgptn;
} T_CFLG;

typedef struct t_rflg {
    void *exinf;
    ID    wtsk;
    UINT  flgptn;
} T_RFLG;

typedef struct t_cmbx {
    void *exinf;
    ATR   mbxatr;
} T_CMBX;

typedef struct t_cmpl {
    void *exinf;
    ATR   mplatr;
    SZ    mplsz;
    void *bufptr;
} T_CMPL;

typedef struct t_ctsk {
    void *exinf;
    ATR   tskatr;
    FP    task;
    PRI   itskpri;
    SZ    stksz;
} T_CTSK;

ID  tk_cre_flg(const T_CFLG *pk_cflg);
ER  tk_set_flg(ID flgid, UINT setptn);
ER  tk_clr_flg(ID flgid, UINT clrptn);
ER  tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout);
ER  tk_ref_flg(ID flgid, T_RFLG *pk_rflg);

ID  tk_cre_mbx(const T_CMBX *pk_cmbx);
ER  tk_snd_mbx(ID mbxid, T_MSG *pk_msg);
ER  tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout);

ID  tk_cre_mpl(const T_CMPL *pk_cmpl);
ER  tk_get_mpl(ID mplid, SZ blksz, void **p_blk, TMO tmout);
ER  tk_rel_mpl(ID mplid, void *blk);

ID  tk_cre_tsk(const T_CTSK *pk_ctsk);
ER  tk_sta_tsk(ID tskid, INT stacd);
ER  tk_chg_pri(ID tskid, PRI tskpri);
ER  tk_dly_tsk(int dlytim);
ER  tk_slp_tsk(TMO tmout);
ER  tk_sus_tsk(ID tskid);
ER  tk_rsm_tsk(ID tskid);

/* Host side control for the CLI, not part of the kernel API. */
void ukernel_init(void);
/* Identifier of the calling task, or zero when the caller is not one. */
ID   ukernel_self_id(void);
void ukernel_run_ms(uint32_t ms);
void ukernel_request_stop(void);
int  ukernel_stopping(void);
void ukernel_shutdown(void);
