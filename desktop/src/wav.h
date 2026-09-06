#pragma once

#include <stddef.h>
#include <stdint.h>

/* Minimal reader for the one format this project needs: 16 bit PCM WAV.
   Multi channel files are downmixed to mono by averaging. Returns 0 on
   success and a negative value on failure, with a human readable reason
   written into err. The sample rate is reported rather than enforced, so a
   caller that needs 16 kHz checks *rate itself. */

int  wav_read_mono16(const char *path, int16_t **samples, uint32_t *count,
                     uint32_t *rate, char *err, size_t errlen);
void wav_free(int16_t *samples);
