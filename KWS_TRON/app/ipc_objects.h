#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* Every link between tasks is a native kernel primitive. Remove the kernel and
   the adaptive loop stops working, which is the point of the design. The full
   map is in docs/architecture.md. */

/* flgid_capture, set from the DMA interrupt, waited on by T2. */
#define FLG_HALF_READY      (1u << 0)
#define FLG_FULL_READY      (1u << 1)
#define FLG_CAPTURE_ANY     (FLG_HALF_READY | FLG_FULL_READY)

/* flgid_pipeline, the stage handoffs and the control signals T5 reacts to. */
#define FLG_VOICE_ACTIVE    (1u << 0)   /* T2 to T3, block held speech      */
#define FLG_QUIESCENT       (1u << 1)   /* T2 to T5, block was silence      */
#define FLG_SKIP_FEATURES   (1u << 2)   /* T5 to T3, gate peeked with ref   */
#define FLG_FEATURES_READY  (1u << 3)   /* T3 to T4, grid ready to classify */
#define FLG_INFERENCE_DONE  (1u << 4)   /* T4 to T5, result available       */
#define FLG_BUDGET_EXCEEDED (1u << 5)   /* T4 to T5, a layer overran        */

extern ID flgid_capture;
extern ID flgid_pipeline;

/* T5 to T1, window resize. A mailbox message must begin with T_MSG. */
typedef struct {
    T_MSG    header;
    uint16_t window_samples;
    uint16_t active_frames;
} window_msg_t;

extern ID mbxid_window;

/* T4 per layer weight streaming, so only the working layer occupies SRAM. */
extern ID mplid_layer;

/* Shared adaptation state. Written only by T5, read by the others. Each field
   is a single aligned word, so a reader never sees a torn value and no lock is
   needed on this core. */
typedef struct {
    volatile uint32_t window_samples;
    volatile uint32_t active_frames;
    volatile uint32_t precision_mask;   /* bit n set means layer n runs FP32 */
    volatile uint32_t deadline_cycles;
    volatile uint32_t vad_threshold;    /* energy units, tracked by T5 */
} adapt_state_t;

extern adapt_state_t adapt_state;

ER ipc_objects_init(void);
