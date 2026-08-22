#include "t2_variance.h"

#include <tm/tmonitor.h>

#include "app_config.h"
#include "ipc_objects.h"
#include "signal_source.h"

t2_stats_t   t2_stats;
sample_ring_t t2_ring;

uint32_t t2_block_energy(const int16_t *samples, uint32_t count)
{
    if (count == 0) {
        return 0;
    }

    /* Mean is removed so a constant bias is not read as signal. Accumulating
       the sum of squares in 64 bits keeps a full scale block from wrapping. */
    int32_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    int32_t mean = sum / (int32_t)count;

    uint64_t square = 0;
    for (uint32_t i = 0; i < count; i++) {
        int32_t centred = (int32_t)samples[i] - mean;
        square += (uint64_t)(centred * centred);
    }
    return (uint32_t)(square / count);
}

void t2_variance_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    sample_ring_reset(&t2_ring);
    t2_stats.noise_floor = 0;

    for (;;) {
        UINT pattern = 0;
        ER err = tk_wai_flg(flgid_capture, FLG_CAPTURE_ANY,
                            TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        if (err < E_OK) {
            continue;
        }

        const int16_t *block = NULL;
        uint32_t count = 0;
        signal_source_completed_block(pattern, &block, &count);
        if (block == NULL || count == 0) {
            continue;
        }

        uint32_t energy = t2_block_energy(block, count);
        t2_stats.energy = energy;
        t2_stats.blocks_seen++;

        sample_ring_push(&t2_ring, block, count);

        uint32_t threshold = adapt_state.vad_threshold;
        bool active = (threshold == 0) || (energy > threshold);

        if (active) {
            t2_stats.hangover = T2_HANGOVER_BLOCKS;
        } else if (t2_stats.hangover > 0) {
            t2_stats.hangover--;
            active = true;
        } else {
            /* Only a genuinely quiet block updates the floor, so speech never
               drags the threshold up behind itself. */
            if (t2_stats.noise_floor == 0) {
                t2_stats.noise_floor = energy;
            } else {
                t2_stats.noise_floor +=
                    (int32_t)(energy - t2_stats.noise_floor) / 32;
            }
        }

        if (active) {
            t2_stats.blocks_active++;
            tk_set_flg(flgid_features, FLG_VOICE_ACTIVE);
        } else {
            tk_set_flg(flgid_control, FLG_QUIESCENT);
        }
    }
}
