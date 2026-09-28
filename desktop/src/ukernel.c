#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "uthread.h"

/* uT-Kernel 3.0 work alike with a single running token, so dispatch is deterministic, see desktop/README.md. */

#define MAX_TASKS   16
#define MAX_FLAGS   16
#define MAX_MBX     8
#define MAX_MPL     4

enum {
    TS_UNUSED = 0,
    TS_DORMANT,
    TS_READY,
    TS_RUNNING,
    TS_WAIT,
    TS_WAITSUS,
    TS_SUSPEND,
    TS_ENDED
};

typedef struct task_s {
    int         state;
    PRI         pri;
    PRI         ipri;
    FP          entry;
    INT         stacd;
    uint64_t    ready_seq;
    ucond_t     cv;
    uthread_t   thread;
    int         started;
    ER          wait_result;
    UINT       *wait_flgptn;
    UINT        wait_ptn;
    UINT        wait_mode;
    T_MSG      *rcv_msg;
    struct task_s *qnext;
} task_t;

typedef struct {
    int      used;
    void    *exinf;
    ATR      flgatr;
    UINT     flgptn;
    task_t  *whead;
    task_t  *wtail;
} flg_t;

typedef struct {
    int      used;
    void    *exinf;
    ATR      mbxatr;
    T_MSG   *mhead;
    T_MSG   *mtail;
    task_t  *whead;
    task_t  *wtail;
} mbx_t;

typedef struct blkhdr_s {
    SZ               size;
    int              free;
    struct blkhdr_s *next;
} blkhdr_t;

typedef struct {
    int       used;
    void     *exinf;
    ATR       mplatr;
    SZ        size;
    uint8_t  *buf;
    blkhdr_t *head;
} mpl_t;

static umutex_t   kmutex;
static task_t     tasks[MAX_TASKS];
static flg_t      flags[MAX_FLAGS];
static mbx_t      mailboxes[MAX_MBX];
static mpl_t      pools[MAX_MPL];
static task_t    *current;
static uint64_t   ready_counter;
static int        kernel_up;
static int        frozen;
static volatile int stop_requested;

static _Thread_local task_t *self_task;

static umutex_t   printmutex;

/* Priority ordering, lower value is more urgent, FIFO within a level. */
static int better(const task_t *a, const task_t *b)
{
    if (b == NULL) {
        return 1;
    }
    if (a->pri != b->pri) {
        return a->pri < b->pri;
    }
    return a->ready_seq < b->ready_seq;
}

static void dispatch_locked(void)
{
    /* Shutdown stops handing out the token, so tasks park inside their kernel calls. */
    if (frozen) {
        if (current != NULL && current->state == TS_RUNNING) {
            current->state = TS_READY;
        }
        current = NULL;
        return;
    }

    task_t *best = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->state == TS_READY || t->state == TS_RUNNING) {
            if (better(t, best)) {
                best = t;
            }
        }
    }
    if (best == current) {
        return;
    }

    /* Never take the token from a task running application code, only from one inside a kernel call. */
    if (current != NULL && current->state == TS_RUNNING && current != self_task) {
        return;
    }

    if (current != NULL && current->state == TS_RUNNING) {
        current->state     = TS_READY;
        current->ready_seq = ++ready_counter;
    }
    current = best;
    if (best != NULL) {
        best->state = TS_RUNNING;
        ucond_signal(&best->cv);
    }
}

/* After every kernel call from task context, a task that lost the token waits here. */
static void resume_self_locked(task_t *self)
{
    if (self == NULL) {
        return;
    }
    while (self->state != TS_RUNNING && self->state != TS_ENDED) {
        ucond_wait(&self->cv, &kmutex);
    }
}

static void make_ready_locked(task_t *t)
{
    t->state     = TS_READY;
    t->ready_seq = ++ready_counter;
}

static void block_self_locked(task_t *self, int newstate)
{
    self->state = newstate;
    if (current == self) {
        current = NULL;
    }
    dispatch_locked();
    while (self->state != TS_RUNNING && self->state != TS_ENDED) {
        ucond_wait(&self->cv, &kmutex);
    }
}

static void queue_push(task_t **head, task_t **tail, task_t *t)
{
    t->qnext = NULL;
    if (*tail == NULL) {
        *head = t;
        *tail = t;
    } else {
        (*tail)->qnext = t;
        *tail = t;
    }
}

static void queue_remove(task_t **head, task_t **tail, task_t *t)
{
    task_t *prev = NULL;
    task_t *cur  = *head;
    while (cur != NULL) {
        if (cur == t) {
            if (prev == NULL) {
                *head = cur->qnext;
            } else {
                prev->qnext = cur->qnext;
            }
            if (*tail == cur) {
                *tail = prev;
            }
            cur->qnext = NULL;
            return;
        }
        prev = cur;
        cur  = cur->qnext;
    }
}

/* A released waiter goes READY unless it was suspended while waiting. */
static void release_waiter_locked(task_t *t, ER result)
{
    t->wait_result = result;
    if (t->state == TS_WAITSUS) {
        t->state = TS_SUSPEND;
    } else {
        make_ready_locked(t);
        ucond_signal(&t->cv);
    }
}

void ukernel_init(void)
{
    umutex_init(&kmutex);
    umutex_init(&printmutex);
    memset(tasks, 0, sizeof(tasks));
    memset(flags, 0, sizeof(flags));
    memset(mailboxes, 0, sizeof(mailboxes));
    memset(pools, 0, sizeof(pools));
    for (int i = 0; i < MAX_TASKS; i++) {
        ucond_init(&tasks[i].cv);
    }
    current        = NULL;
    ready_counter  = 0;
    stop_requested = 0;
    kernel_up      = 1;
}

void ukernel_request_stop(void)
{
    stop_requested = 1;
}

int ukernel_stopping(void)
{
    return stop_requested;
}

void ukernel_run_ms(uint32_t ms)
{
    uint64_t deadline = umonotonic_us() + (uint64_t)ms * 1000u;
    while (!stop_requested && umonotonic_us() < deadline) {
        usleep_us(2000);
    }
    stop_requested = 1;
}

void ukernel_shutdown(void)
{
    stop_requested = 1;
    umutex_lock(&kmutex);
    frozen = 1;
    if (current != NULL && current->state == TS_RUNNING) {
        current->state = TS_READY;
    }
    current = NULL;
    umutex_unlock(&kmutex);

    /* Running tasks stop at their next kernel call. */
    usleep_us(60000);
    kernel_up = 0;
}

ID ukernel_self_id(void)
{
    return (self_task != NULL) ? (ID)((self_task - tasks) + 1) : 0;
}

ID tk_cre_flg(const T_CFLG *pk_cflg)
{
    if (pk_cflg == NULL) {
        return E_PAR;
    }
    if ((pk_cflg->flgatr & ~(TA_TPRI | TA_WMUL)) != 0) {
        return E_RSATR;
    }
    umutex_lock(&kmutex);
    for (int i = 0; i < MAX_FLAGS; i++) {
        if (!flags[i].used) {
            flags[i].used   = 1;
            flags[i].exinf  = pk_cflg->exinf;
            flags[i].flgatr = pk_cflg->flgatr;
            flags[i].flgptn = pk_cflg->iflgptn;
            flags[i].whead  = NULL;
            flags[i].wtail  = NULL;
            umutex_unlock(&kmutex);
            return (ID)(i + 1);
        }
    }
    umutex_unlock(&kmutex);
    return E_LIMIT;
}

static int flag_cond(UINT flgptn, UINT waiptn, UINT wfmode)
{
    if ((wfmode & TWF_ORW) != 0) {
        return (flgptn & waiptn) != 0;
    }
    return (flgptn & waiptn) == waiptn;
}

ER tk_set_flg(ID flgid, UINT setptn)
{
    if (flgid < 1 || flgid > MAX_FLAGS) {
        return E_ID;
    }
    umutex_lock(&kmutex);
    flg_t *f = &flags[flgid - 1];
    if (!f->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    f->flgptn |= setptn;

    /* Release waiters in queue order, a TWF_BITCLR waiter consumes its bits and ends the walk. */
    task_t *t = f->whead;
    while (t != NULL) {
        task_t *next = t->qnext;
        if (flag_cond(f->flgptn, t->wait_ptn, t->wait_mode)) {
            if (t->wait_flgptn != NULL) {
                *t->wait_flgptn = f->flgptn;
            }
            queue_remove(&f->whead, &f->wtail, t);
            release_waiter_locked(t, E_OK);
            if ((t->wait_mode & TWF_CLR) != 0) {
                f->flgptn = 0;
                break;
            }
            if ((t->wait_mode & TWF_BITCLR) != 0) {
                f->flgptn &= ~t->wait_ptn;
                if (f->flgptn == 0) {
                    break;
                }
            }
        }
        t = next;
    }

    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_clr_flg(ID flgid, UINT clrptn)
{
    if (flgid < 1 || flgid > MAX_FLAGS) {
        return E_ID;
    }
    umutex_lock(&kmutex);
    flg_t *f = &flags[flgid - 1];
    if (!f->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    /* The argument is the pattern to keep, not the pattern to clear. */
    f->flgptn &= clrptn;
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout)
{
    if (flgid < 1 || flgid > MAX_FLAGS) {
        return E_ID;
    }
    if (waiptn == 0) {
        return E_PAR;
    }
    if ((wfmode & ~(TWF_ORW | TWF_CLR | TWF_BITCLR)) != 0) {
        return E_PAR;
    }
    if (tmout < TMO_FEVR) {
        return E_PAR;
    }
    task_t *me = self_task;
    if (me == NULL) {
        return E_CTX;
    }

    umutex_lock(&kmutex);
    flg_t *f = &flags[flgid - 1];
    if (!f->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    if ((f->flgatr & TA_WMUL) == 0 && f->whead != NULL) {
        umutex_unlock(&kmutex);
        return E_OBJ;
    }

    if (flag_cond(f->flgptn, waiptn, wfmode)) {
        if (p_flgptn != NULL) {
            *p_flgptn = f->flgptn;
        }
        if ((wfmode & TWF_CLR) != 0) {
            f->flgptn = 0;
        } else if ((wfmode & TWF_BITCLR) != 0) {
            f->flgptn &= ~waiptn;
        }
        umutex_unlock(&kmutex);
        return E_OK;
    }

    if (tmout == TMO_POL) {
        umutex_unlock(&kmutex);
        return E_TMOUT;
    }

    me->wait_ptn    = waiptn;
    me->wait_mode   = wfmode;
    me->wait_flgptn = p_flgptn;
    me->wait_result = E_TMOUT;
    queue_push(&f->whead, &f->wtail, me);

    if (tmout == TMO_FEVR) {
        block_self_locked(me, TS_WAIT);
    } else {
        me->state = TS_WAIT;
        if (current == me) {
            current = NULL;
        }
        dispatch_locked();
        uint32_t remain = (uint32_t)tmout;
        while (me->state == TS_WAIT || me->state == TS_WAITSUS) {
            if (ucond_wait_ms(&me->cv, &kmutex, remain)) {
                if (me->state == TS_WAIT || me->state == TS_WAITSUS) {
                    queue_remove(&f->whead, &f->wtail, me);
                    me->wait_result = E_TMOUT;
                    make_ready_locked(me);
                }
                break;
            }
        }
        while (me->state != TS_RUNNING && me->state != TS_ENDED) {
            dispatch_locked();
            if (me->state == TS_RUNNING || me->state == TS_ENDED) {
                break;
            }
            ucond_wait(&me->cv, &kmutex);
        }
    }

    ER result = me->wait_result;
    umutex_unlock(&kmutex);
    return result;
}

ER tk_ref_flg(ID flgid, T_RFLG *pk_rflg)
{
    if (flgid < 1 || flgid > MAX_FLAGS) {
        return E_ID;
    }
    if (pk_rflg == NULL) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    flg_t *f = &flags[flgid - 1];
    if (!f->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    pk_rflg->exinf  = f->exinf;
    pk_rflg->flgptn = f->flgptn;
    pk_rflg->wtsk   = (f->whead != NULL) ? (ID)((f->whead - tasks) + 1) : 0;
    umutex_unlock(&kmutex);
    return E_OK;
}

ID tk_cre_mbx(const T_CMBX *pk_cmbx)
{
    if (pk_cmbx == NULL) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    for (int i = 0; i < MAX_MBX; i++) {
        if (!mailboxes[i].used) {
            mailboxes[i].used   = 1;
            mailboxes[i].exinf  = pk_cmbx->exinf;
            mailboxes[i].mbxatr = pk_cmbx->mbxatr;
            mailboxes[i].mhead  = NULL;
            mailboxes[i].mtail  = NULL;
            mailboxes[i].whead  = NULL;
            mailboxes[i].wtail  = NULL;
            umutex_unlock(&kmutex);
            return (ID)(i + 1);
        }
    }
    umutex_unlock(&kmutex);
    return E_LIMIT;
}

ER tk_snd_mbx(ID mbxid, T_MSG *pk_msg)
{
    if (mbxid < 1 || mbxid > MAX_MBX) {
        return E_ID;
    }
    if (pk_msg == NULL) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    mbx_t *m = &mailboxes[mbxid - 1];
    if (!m->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    pk_msg->msgque[0] = NULL;

    if (m->whead != NULL) {
        task_t *t = m->whead;
        queue_remove(&m->whead, &m->wtail, t);
        t->rcv_msg = pk_msg;
        release_waiter_locked(t, E_OK);
    } else {
        if (m->mtail == NULL) {
            m->mhead = pk_msg;
        } else {
            m->mtail->msgque[0] = pk_msg;
        }
        m->mtail = pk_msg;
    }

    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout)
{
    if (mbxid < 1 || mbxid > MAX_MBX) {
        return E_ID;
    }
    if (ppk_msg == NULL) {
        return E_PAR;
    }
    task_t *me = self_task;
    if (me == NULL) {
        return E_CTX;
    }

    umutex_lock(&kmutex);
    mbx_t *m = &mailboxes[mbxid - 1];
    if (!m->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }

    if (m->mhead != NULL) {
        T_MSG *msg = m->mhead;
        m->mhead = (T_MSG *)msg->msgque[0];
        if (m->mhead == NULL) {
            m->mtail = NULL;
        }
        msg->msgque[0] = NULL;
        *ppk_msg = msg;
        umutex_unlock(&kmutex);
        return E_OK;
    }

    if (tmout == TMO_POL) {
        umutex_unlock(&kmutex);
        return E_TMOUT;
    }

    me->rcv_msg     = NULL;
    me->wait_result = E_TMOUT;
    queue_push(&m->whead, &m->wtail, me);
    block_self_locked(me, TS_WAIT);

    ER result = me->wait_result;
    if (result == E_OK) {
        *ppk_msg = me->rcv_msg;
    }
    umutex_unlock(&kmutex);
    return result;
}

#define MPL_ALIGN 8u

static SZ mpl_round(SZ n)
{
    return (n + (MPL_ALIGN - 1u)) & ~(MPL_ALIGN - 1u);
}

ID tk_cre_mpl(const T_CMPL *pk_cmpl)
{
    if (pk_cmpl == NULL || pk_cmpl->bufptr == NULL || pk_cmpl->mplsz == 0) {
        return E_PAR;
    }
    if ((pk_cmpl->mplatr & TA_USERBUF) == 0) {
        return E_RSATR;
    }
    umutex_lock(&kmutex);
    for (int i = 0; i < MAX_MPL; i++) {
        if (!pools[i].used) {
            pools[i].used   = 1;
            pools[i].exinf  = pk_cmpl->exinf;
            pools[i].mplatr = pk_cmpl->mplatr;
            pools[i].size   = pk_cmpl->mplsz;
            pools[i].buf    = (uint8_t *)pk_cmpl->bufptr;
            blkhdr_t *h = (blkhdr_t *)pools[i].buf;
            h->size = pk_cmpl->mplsz - (SZ)sizeof(blkhdr_t);
            h->free = 1;
            h->next = NULL;
            pools[i].head = h;
            umutex_unlock(&kmutex);
            return (ID)(i + 1);
        }
    }
    umutex_unlock(&kmutex);
    return E_LIMIT;
}

ER tk_get_mpl(ID mplid, SZ blksz, void **p_blk, TMO tmout)
{
    if (mplid < 1 || mplid > MAX_MPL) {
        return E_ID;
    }
    if (p_blk == NULL || blksz == 0) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    mpl_t *p = &pools[mplid - 1];
    if (!p->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }

    SZ want = mpl_round(blksz);
    for (blkhdr_t *h = p->head; h != NULL; h = h->next) {
        if (!h->free || h->size < want) {
            continue;
        }
        SZ leftover = h->size - want;
        if (leftover > sizeof(blkhdr_t) + MPL_ALIGN) {
            blkhdr_t *split = (blkhdr_t *)((uint8_t *)h + sizeof(blkhdr_t) + want);
            split->size = leftover - (SZ)sizeof(blkhdr_t);
            split->free = 1;
            split->next = h->next;
            h->next = split;
            h->size = want;
        }
        h->free = 0;
        *p_blk = (uint8_t *)h + sizeof(blkhdr_t);
        umutex_unlock(&kmutex);
        return E_OK;
    }

    /* Callers fall back to reading weights in place, so an exhausted pool never blocks. */
    umutex_unlock(&kmutex);
    (void)tmout;
    return E_TMOUT;
}

ER tk_rel_mpl(ID mplid, void *blk)
{
    if (mplid < 1 || mplid > MAX_MPL) {
        return E_ID;
    }
    if (blk == NULL) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    mpl_t *p = &pools[mplid - 1];
    if (!p->used) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    blkhdr_t *target = (blkhdr_t *)((uint8_t *)blk - sizeof(blkhdr_t));
    target->free = 1;
    for (blkhdr_t *h = p->head; h != NULL && h->next != NULL; ) {
        if (h->free && h->next->free) {
            h->size += (SZ)sizeof(blkhdr_t) + h->next->size;
            h->next  = h->next->next;
        } else {
            h = h->next;
        }
    }
    umutex_unlock(&kmutex);
    return E_OK;
}

static void task_trampoline(void *arg)
{
    task_t *t = (task_t *)arg;
    self_task = t;

    umutex_lock(&kmutex);
    while (t->state != TS_RUNNING && t->state != TS_ENDED) {
        ucond_wait(&t->cv, &kmutex);
    }
    int ended = (t->state == TS_ENDED);
    umutex_unlock(&kmutex);

    if (!ended) {
        void (*entry)(INT, void *) = (void (*)(INT, void *))t->entry;
        entry(t->stacd, NULL);
    }

    umutex_lock(&kmutex);
    t->state = TS_ENDED;
    if (current == t) {
        current = NULL;
    }
    dispatch_locked();
    umutex_unlock(&kmutex);
}

ID tk_cre_tsk(const T_CTSK *pk_ctsk)
{
    if (pk_ctsk == NULL || pk_ctsk->task == NULL) {
        return E_PAR;
    }
    umutex_lock(&kmutex);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TS_UNUSED) {
            task_t *t = &tasks[i];
            t->state   = TS_DORMANT;
            t->pri     = pk_ctsk->itskpri;
            t->ipri    = pk_ctsk->itskpri;
            t->entry   = pk_ctsk->task;
            t->started = 0;
            t->qnext   = NULL;
            umutex_unlock(&kmutex);
            return (ID)(i + 1);
        }
    }
    umutex_unlock(&kmutex);
    return E_LIMIT;
}

ER tk_sta_tsk(ID tskid, INT stacd)
{
    if (tskid < 1 || tskid > MAX_TASKS) {
        return E_ID;
    }
    umutex_lock(&kmutex);
    task_t *t = &tasks[tskid - 1];
    if (t->state != TS_DORMANT) {
        umutex_unlock(&kmutex);
        return E_OBJ;
    }
    t->stacd = stacd;
    make_ready_locked(t);
    if (uthread_start(&t->thread, task_trampoline, t) != 0) {
        t->state = TS_DORMANT;
        umutex_unlock(&kmutex);
        return E_SYS;
    }
    t->started = 1;
    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_chg_pri(ID tskid, PRI tskpri)
{
    umutex_lock(&kmutex);
    task_t *t;
    if (tskid == 0) {
        t = self_task;
        if (t == NULL) {
            umutex_unlock(&kmutex);
            return E_CTX;
        }
    } else {
        if (tskid < 1 || tskid > MAX_TASKS) {
            umutex_unlock(&kmutex);
            return E_ID;
        }
        t = &tasks[tskid - 1];
    }
    if (t->state == TS_UNUSED) {
        umutex_unlock(&kmutex);
        return E_NOEXS;
    }
    t->pri = (tskpri == 0) ? t->ipri : tskpri;
    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_dly_tsk(int dlytim)
{
    task_t *me = self_task;
    if (me == NULL) {
        usleep_us((uint64_t)dlytim * 1000u);
        return E_OK;
    }
    if (dlytim <= 0) {
        return E_OK;
    }

    umutex_lock(&kmutex);
    me->state = TS_WAIT;
    if (current == me) {
        current = NULL;
    }
    dispatch_locked();
    ucond_wait_ms(&me->cv, &kmutex, (uint32_t)dlytim);
    if (me->state == TS_WAIT) {
        make_ready_locked(me);
    } else if (me->state == TS_WAITSUS) {
        me->state = TS_SUSPEND;
    }
    while (me->state != TS_RUNNING && me->state != TS_ENDED) {
        dispatch_locked();
        if (me->state == TS_RUNNING || me->state == TS_ENDED) {
            break;
        }
        ucond_wait(&me->cv, &kmutex);
    }
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_slp_tsk(TMO tmout)
{
    task_t *me = self_task;
    if (me == NULL) {
        /* The startup thread parks here, where usermain stops on the board. */
        if (tmout == TMO_FEVR) {
            while (!stop_requested) {
                usleep_us(2000);
            }
        }
        return E_OK;
    }
    umutex_lock(&kmutex);
    if (tmout == TMO_FEVR) {
        block_self_locked(me, TS_WAIT);
        umutex_unlock(&kmutex);
        return E_OK;
    }
    me->state = TS_WAIT;
    if (current == me) {
        current = NULL;
    }
    dispatch_locked();
    ucond_wait_ms(&me->cv, &kmutex, (uint32_t)tmout);
    if (me->state == TS_WAIT) {
        make_ready_locked(me);
    }
    while (me->state != TS_RUNNING && me->state != TS_ENDED) {
        dispatch_locked();
        if (me->state == TS_RUNNING || me->state == TS_ENDED) {
            break;
        }
        ucond_wait(&me->cv, &kmutex);
    }
    umutex_unlock(&kmutex);
    return E_TMOUT;
}

ER tk_sus_tsk(ID tskid)
{
    if (tskid < 1 || tskid > MAX_TASKS) {
        return E_ID;
    }
    umutex_lock(&kmutex);
    task_t *t = &tasks[tskid - 1];
    if (t->state == TS_UNUSED || t->state == TS_DORMANT) {
        umutex_unlock(&kmutex);
        return E_OBJ;
    }
    if (t->state == TS_WAIT) {
        t->state = TS_WAITSUS;
    } else if (t->state == TS_READY || t->state == TS_RUNNING) {
        if (current == t) {
            current = NULL;
        }
        t->state = TS_SUSPEND;
    }
    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

ER tk_rsm_tsk(ID tskid)
{
    if (tskid < 1 || tskid > MAX_TASKS) {
        return E_ID;
    }
    umutex_lock(&kmutex);
    task_t *t = &tasks[tskid - 1];
    if (t->state == TS_WAITSUS) {
        t->state = TS_WAIT;
    } else if (t->state == TS_SUSPEND) {
        make_ready_locked(t);
        ucond_signal(&t->cv);
    }
    task_t *me = self_task;
    dispatch_locked();
    resume_self_locked(me);
    umutex_unlock(&kmutex);
    return E_OK;
}

void tm_putstring(UB *str)
{
    umutex_lock(&printmutex);
    fputs((const char *)str, stdout);
    fflush(stdout);
    umutex_unlock(&printmutex);
}

void tm_printf(UB *fmt, ...)
{
    va_list ap;
    umutex_lock(&printmutex);
    va_start(ap, fmt);
    vfprintf(stdout, (const char *)fmt, ap);
    va_end(ap);
    fflush(stdout);
    umutex_unlock(&printmutex);
}
