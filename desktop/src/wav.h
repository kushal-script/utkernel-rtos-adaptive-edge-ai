#pragma once

#include <stddef.h>
#include <stdint.h>

/* Minimal 16 bit PCM WAV reader, downmixed to mono; returns 0, or negative with a reason in err. */

int  wav_read_mono16(const char *path, int16_t **samples, uint32_t *count,
                     uint32_t *rate, char *err, size_t errlen);
void wav_free(int16_t *samples);
