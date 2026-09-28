#pragma once

#include <stddef.h>
#include <stdint.h>

/* Host side choices for the capture front end, invisible above signal_source.h. */

/* The six clip labelled corpus the firmware carries, the default because it scores itself. */
void host_source_use_corpus(void);

/* A 16 kHz mono WAV, classified but not scored. */
int  host_source_use_wav(const char *path, char *err, size_t errlen);

/* Wall clock pacing multiplier, one is real time. */
void host_source_set_speed(double speed);

void host_source_start_producer(void);
void host_source_stop_producer(void);

uint32_t    host_source_total_samples(void);
uint32_t    host_source_clip_count(void);
const char *host_source_clip_name(uint32_t index);
int         host_source_is_labelled(void);
const char *host_source_description(void);
