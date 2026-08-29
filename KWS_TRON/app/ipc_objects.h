#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* Every link between tasks is a native kernel primitive. Remove the kernel and
   the adaptive loop stops working, which is the point of the design. The full
   map is in docs/architecture.md.

   One event flag object per consumer edge, never shared. With TA_WMUL and a
   TWF_BITCLR wait, the kernel stops releasing waiters as soon as one of them
   clears the pattern, so two tasks waiting on the same object can lose a
   wakeup. Splitting the objects removes the hazard entirely and costs nothing,
   the kernel allows sixteen. */

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

/* T2 and T4 to T5, everything the controller reacts to.

   FLG_ACTIVE matters as much as FLG_QUIESCENT. Without it the feature gate is a
   one way latch: the gate closes on silence, T3 then skips every frame, so no
   grid is ever completed, so no inference finishes, so the only events reaching
   the controller are further quiescent ones, which keep the gate shut. The
   pipeline would never restart when speech returned. */
#define FLG_QUIESCENT       (1u << 0)
#define FLG_INFERENCE_DONE  (1u << 1)
#define FLG_BUDGET_EXCEEDED (1u << 2)
#define FLG_ACTIVE          (1u << 3)
#define FLG_CONTROL_ANY     (FLG_QUIESCENT | FLG_INFERENCE_DONE | \
                             FLG_BUDGET_EXCEEDED | FLG_ACTIVE)
extern ID flgid_control;

/* T5 to T3, the feature gate. Only ever peeked with tk_ref_flg, never waited
   on, so the feature stage is never blocked by the controller. */
#define FLG_SKIP_FEATURES   (1u << 0)
extern ID flgid_gate;

/* T5 to T1, window resize. A mailbox message must begin with T_MSG, which the
   kernel uses as its queue link, and must live in storage that outlives the
   send. */
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
    /* Set while the benchmark holds a configuration fixed so it can measure one
       precision end to end. The controller leaves the mask alone when set. */
    volatile uint32_t pin_precision;
} adapt_state_t;

extern adapt_state_t adapt_state;

ER ipc_objects_init(void);
