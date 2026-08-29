#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "app_config.h"

/* The front end of the pipeline, behind one interface so the rest of the
   system does not know or care where samples come from.

   Two implementations exist. The replay source streams a flash resident corpus
   through GPDMA, which drives the same circular buffer, the same half and full
   transfer interrupts, and the same event flags a live sensor would, so the
   real time path under test is unchanged. The I2S source reads an INMP441 when
   one is attached. Selected by KWS_SIGNAL_SOURCE in app_config.h and explained
   in docs/signal_source.md. */

ER signal_source_init(void);

/* Begins capture with the given block size in samples. The block is the unit
   the pipeline is woken on, and it is what a window resize message changes. */
ER signal_source_start(uint32_t window_samples);

/* Re stages capture at a new block size. Safe to call from a task, not from an
   interrupt. Returns E_OK when the new size is in effect. */
ER signal_source_set_window(uint32_t window_samples);

/* Resolves the flag pattern the interrupt set into the block that just filled.
   Returns a pointer into the capture buffer, so the consumer reads the half
   the DMA is not writing and no copy is made on this path. */
void signal_source_completed_block(UINT pattern, const int16_t **block,
                                   uint32_t *count);

/* Total blocks captured and blocks the consumer failed to service in time.
   An overrun means the pipeline did not keep up, which the benchmark reports
   rather than hides. */
uint32_t signal_source_block_count(void);

/* Stop and restart the producer. The benchmark suspends the consumer tasks
   while it measures the core in isolation; without pausing the source too, the
   DMA keeps filling buffers nobody drains and every one of those blocks is
   counted as a consumer overrun, which reads as a pipeline failure when it is
   only an artefact of the measurement. */
void signal_source_pause(void);
void signal_source_resume(void);
uint32_t signal_source_overruns(void);

/* Marks the block handed out by signal_source_completed_block as consumed. The
   overrun counter depends on this being called after the copy, not before. */
void signal_source_release_block(void);

/* Corpus index of the block that filled most recently, so a consumer can say
   which part of the corpus a derived result came from. */
uint32_t signal_source_completed_corpus(void);

/* Ground truth for a span of the corpus ending at end_offset. Returns -1 when
   the span crosses a clip boundary or the corpus wrap, because such a span has
   no single correct label and guessing would corrupt the reported accuracy. */
int signal_source_label_for_span(uint32_t end_offset, uint32_t span);

/* Raw capture buffer, exposed so the interrupt setup in the MSP can target it
   without the board layer knowing how the pipeline uses it. */
const int16_t *signal_source_capture_buffer(void);

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
const int32_t *signal_source_i2s_buffer(void);
uint32_t signal_source_i2s_words(void);
#endif
