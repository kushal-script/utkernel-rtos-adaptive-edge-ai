#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* Every wakeup is a kernel primitive, map in docs/architecture.md. */
/* One flag object per consumer edge, two waiters on a TWF_BITCLR flag can lose a wakeup. */

/* T1 capture interrupt to T2. */
#define FLG_HALF_READY      (1u << 0)
#define FLG_FULL_READY      (1u << 1)
#define FLG_CAPTURE_ANY     (FLG_HALF_READY | FLG_FULL_READY)
extern ID flgid_capture;

/* T2 to T3, a block held speech and is worth turning into features. */
#define FLG_VOICE_ACTIVE    (1u << 0)
extern ID flgid_features;

/* T3 to T4, the feature grid is ready to classify. */
#define FLG_FEATURES_READY  (1u << 0)
extern ID flgid_inference;

/* T2 and T4 to T5; FLG_ACTIVE is what reopens the gate when speech returns. */
#define FLG_QUIESCENT       (1u << 0)
#define FLG_INFERENCE_DONE  (1u << 1)
#define FLG_BUDGET_EXCEEDED (1u << 2)
#define FLG_ACTIVE          (1u << 3)
#define FLG_CONTROL_ANY     (FLG_QUIESCENT | FLG_INFERENCE_DONE | \
                             FLG_BUDGET_EXCEEDED | FLG_ACTIVE)
extern ID flgid_control;

/* T5 to T3, the feature gate, only peeked with tk_ref_flg so T3 never blocks on it. */
#define FLG_SKIP_FEATURES   (1u << 0)
extern ID flgid_gate;

/* T5 to T1, window resize; a message starts with T_MSG and must outlive the send. */
typedef struct {
    T_MSG    header;
    uint16_t window_samples;
    uint16_t active_frames;
} window_msg_t;

extern ID mbxid_window;

/* T4 per layer weight streaming, so only the working layer occupies SRAM. */
extern ID mplid_layer;

/* Shared adaptation state; each field is one aligned word, so no lock is needed on this core. */
typedef struct {
    volatile uint32_t window_samples;
    volatile uint32_t active_frames;
    volatile uint32_t precision_mask;   /* bit n set means layer n runs FP32 */
    volatile uint32_t deadline_cycles;
    volatile uint32_t vad_threshold;    /* energy units, tracked by T5 */
    /* Set while the benchmark pins a configuration, the controller leaves the mask alone. */
    volatile uint32_t pin_precision;
} adapt_state_t;

extern adapt_state_t adapt_state;

ER ipc_objects_init(void);
