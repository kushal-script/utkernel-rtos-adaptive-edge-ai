#include "t1_dma_ingest.h"
#include "ipc_objects.h"

#include "main.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

extern I2S_HandleTypeDef hi2s2;

static int32_t audio_buffer[T1_AUDIO_BUFFER_LEN] __attribute__((aligned(4)));

const int32_t *t1_get_buffer(void) { return audio_buffer; }

void t1_dma_ingest_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    HAL_StatusTypeDef st =
        HAL_I2S_Receive_DMA(&hi2s2, (uint16_t *)audio_buffer,
                            T1_AUDIO_BUFFER_LEN);
    if (st != HAL_OK) {
        tm_printf((UB*)"T1: HAL_I2S_Receive_DMA failed (%d)\n", (int)st);
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    tm_printf((UB*)"T1: I2S DMA capture started @ 16 kHz\n");
    tk_slp_tsk(TMO_FEVR);
}

/* Runs in GPDMA1_Channel0 ISR context. Only tk_set_flg is ISR safe here. */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) {
        tk_set_flg(flgid_audio, FLG_HALF_READY);
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) {
        tk_set_flg(flgid_audio, FLG_FULL_READY);
        HAL_I2S_Receive_DMA(hi2s, (uint16_t *)audio_buffer,
                            T1_AUDIO_BUFFER_LEN);
    }
}
