#include "app_config.h"

#if KWS_AUDIO_PROBE

#include "audio_probe.h"
#include "ipc_objects.h"
#include "t1_dma_ingest.h"

#include "main.h"
#include <tk/tkernel.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

#define PROBE_SNAP_WORDS  (PROBE_SNAP_FRAMES * 2)
#define PROBE_TX_CHUNK    T1_AUDIO_HALF_LEN

static int32_t snap[PROBE_SNAP_WORDS];

static void uart_tx(const void *p, uint32_t n)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)p, (uint16_t)n, HAL_MAX_DELAY);
}

/* Frame: "SNAP" | ver | channels | bytes/sample | 0 | rate u32 | frames u32
   | payload (frames * channels * int32, L then R) | sum u32. */
static void dump_snapshot(void)
{
    uint8_t hdr[16] = { 'S', 'N', 'A', 'P', 1, 2, 4, 0 };
    uint32_t rate = 16000, frames = PROBE_SNAP_FRAMES;
    memcpy(&hdr[8],  &rate,   4);
    memcpy(&hdr[12], &frames, 4);
    uart_tx(hdr, sizeof hdr);

    uint32_t sum = 0;
    for (uint32_t i = 0; i < PROBE_SNAP_WORDS; i += PROBE_TX_CHUNK) {
        uart_tx(&snap[i], PROBE_TX_CHUNK * sizeof(int32_t));
        for (uint32_t j = i; j < i + PROBE_TX_CHUNK; j++) {
            sum += (uint32_t)snap[j];
        }
    }
    uart_tx(&sum, sizeof sum);
}

void audio_probe_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    for (;;) {
        uint32_t idx = 0;
        while (idx < PROBE_SNAP_WORDS) {
            UINT ptn;
            tk_wai_flg(flgid_audio, FLG_HALF_READY | FLG_FULL_READY,
                       TWF_ORW | TWF_BITCLR, &ptn, TMO_FEVR);
            const int32_t *buf  = t1_get_buffer();
            const int32_t *half = (ptn & FLG_HALF_READY) ? buf
                                                         : buf + T1_AUDIO_HALF_LEN;
            memcpy(&snap[idx], half, T1_AUDIO_HALF_LEN * sizeof(int32_t));
            idx += T1_AUDIO_HALF_LEN;
        }
        dump_snapshot();
        tk_dly_tsk(500);
    }
}

#endif /* KWS_AUDIO_PROBE */
