#pragma once

#include <stddef.h>
#include <stdint.h>

/* Host side selection and control for the capture front end. The pipeline above
   signal_source.h is unchanged and cannot tell the difference; only these
   choices are new. */

/* The stratified corpus that travels in the firmware, six clips alternating
   keyword and silence. This is the default because it carries labels, so a run
   can score itself the way the board does. */
void host_source_use_corpus(void);

/* An arbitrary 16 kHz mono WAV. Classifications are still produced but nothing
   is scored, because a file supplied at run time carries no ground truth. */
int  host_source_use_wav(const char *path, char *err, size_t errlen);

/* Wall clock pacing multiplier. One is real time, which is the honest default
   because the capture cadence is part of what the controller regulates. */
void host_source_set_speed(double speed);

void host_source_start_producer(void);
void host_source_stop_producer(void);

uint32_t    host_source_total_samples(void);
uint32_t    host_source_clip_count(void);
const char *host_source_clip_name(uint32_t index);
int         host_source_is_labelled(void);
const char *host_source_description(void);
