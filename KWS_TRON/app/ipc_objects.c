#include "ipc_objects.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

ID flgid_audio;

void ipc_objects_init(void)
{
    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };
    flgid_audio = tk_cre_flg(&cflg);
    if (flgid_audio < 0) {
        tm_printf((UB*)"ipc_objects_init: tk_cre_flg failed (%d)\n",
                  (int)flgid_audio);
    }
}
