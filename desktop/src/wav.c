#include "wav.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAV_FORMAT_PCM        0x0001u
#define WAV_FORMAT_EXTENSIBLE 0xfffeu

#define WAV_MAX_DATA_BYTES (512u * 1024u * 1024u)
#define WAV_MAX_CHANNELS   64u
#define WAV_STAGE_BYTES    4096u

/* fseek takes a long, which is 32 bit on Windows, so a skip is stepped. */
#define WAV_SEEK_STEP 0x40000000L

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void wav_fail(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || errlen == 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* Header fields are assembled from bytes so the result does not depend on the
   host byte order or on the alignment of the buffer they were read into. */
static uint16_t wav_le16(const uint8_t *b)
{
    return (uint16_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
}

static uint32_t wav_le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* Two's complement is spelled out rather than cast, which is implementation
   defined for values above 32767 until C23. */
static int32_t wav_sample16(const uint8_t *b)
{
    uint32_t raw = wav_le16(b);
    return (raw & 0x8000u) ? (int32_t)raw - 65536 : (int32_t)raw;
}

static int16_t wav_clamp(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static int wav_read_exact(FILE *f, uint8_t *dst, size_t count)
{
    return fread(dst, 1, count, f) == count;
}

static int wav_skip(FILE *f, uint32_t count)
{
    while (count > 0) {
        long step = (count > (uint32_t)WAV_SEEK_STEP) ? WAV_SEEK_STEP : (long)count;
        if (fseek(f, step, SEEK_CUR) != 0) {
            return 0;
        }
        count -= (uint32_t)step;
    }
    return 1;
}

/* Four character chunk ids reach the error strings, so anything unprintable is
   replaced rather than passed through to a terminal. */
static void wav_tag(const uint8_t *b, char *out)
{
    for (int i = 0; i < 4; i++) {
        out[i] = (b[i] >= 0x20 && b[i] < 0x7f) ? (char)b[i] : '?';
    }
    out[4] = '\0';
}

static int wav_parse_fmt(FILE *f, uint32_t size, uint32_t *channels,
                         uint32_t *rate, char *err, size_t errlen)
{
    uint8_t base[16];
    uint8_t ext[24];
    const char *field = "audio format";
    uint32_t format;
    uint32_t bits;
    uint32_t consumed = 16;

    if (size < 16) {
        wav_fail(err, errlen, "fmt chunk is %lu bytes, at least 16 are required",
                 (unsigned long)size);
        return -1;
    }
    if (!wav_read_exact(f, base, sizeof base)) {
        wav_fail(err, errlen, "file ends inside the fmt chunk");
        return -1;
    }

    format    = wav_le16(base);
    *channels = wav_le16(base + 2);
    *rate     = wav_le32(base + 4);
    bits      = wav_le16(base + 14);

    if (format == WAV_FORMAT_EXTENSIBLE) {
        if (size < 40) {
            wav_fail(err, errlen,
                     "extensible fmt chunk is %lu bytes, 40 are required",
                     (unsigned long)size);
            return -1;
        }
        if (!wav_read_exact(f, ext, sizeof ext)) {
            wav_fail(err, errlen, "file ends inside the extensible fmt chunk");
            return -1;
        }
        /* The subformat GUID opens with the format code the extension stands in for. */
        format = wav_le16(ext + 8);
        field = "extensible subformat";
        consumed = 40;
    }

    if (format != WAV_FORMAT_PCM) {
        wav_fail(err, errlen,
                 "%s 0x%04lx is not supported, 16 bit PCM is required",
                 field, (unsigned long)format);
        return -1;
    }
    if (bits != 16) {
        wav_fail(err, errlen,
                 "samples are %lu bit, 16 bit PCM is required",
                 (unsigned long)bits);
        return -1;
    }
    if (*channels == 0 || *channels > WAV_MAX_CHANNELS) {
        wav_fail(err, errlen, "file declares %lu channels, the limit is %lu",
                 (unsigned long)*channels, (unsigned long)WAV_MAX_CHANNELS);
        return -1;
    }

    if (size > consumed && !wav_skip(f, size - consumed)) {
        wav_fail(err, errlen, "file ends inside the fmt chunk");
        return -1;
    }
    return 0;
}

static int wav_decode(FILE *f, uint32_t channels, uint32_t frames, int16_t *dst,
                      char *err, size_t errlen)
{
    uint8_t stage[WAV_STAGE_BYTES];
    size_t block = (size_t)channels * 2u;
    size_t batch = sizeof stage / block;
    uint32_t done = 0;

    while (done < frames) {
        size_t want = ((size_t)(frames - done) < batch) ? (size_t)(frames - done) : batch;

        if (!wav_read_exact(f, stage, want * block)) {
            wav_fail(err, errlen,
                     "data chunk is truncated, %lu of %lu frames were read",
                     (unsigned long)done, (unsigned long)frames);
            return -1;
        }

        for (size_t i = 0; i < want; i++) {
            const uint8_t *frame = stage + i * block;
            int32_t sum = 0;
            int32_t half = (int32_t)(channels / 2u);
            int32_t mean;

            for (uint32_t c = 0; c < channels; c++) {
                sum += wav_sample16(frame + c * 2u);
            }
            /* Rounded away from zero, since C truncates toward it. */
            mean = (sum >= 0) ? (sum + half) / (int32_t)channels
                              : (sum - half) / (int32_t)channels;
            dst[done + i] = wav_clamp(mean);
        }
        done += (uint32_t)want;
    }
    return 0;
}

static int wav_parse(FILE *f, int16_t **samples, uint32_t *count, uint32_t *rate,
                     char *err, size_t errlen)
{
    uint8_t header[12];
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t data_bytes = 0;
    uint32_t frames;
    long data_pos = 0;
    long file_bytes = 0;
    int have_fmt = 0;
    int have_data = 0;
    int16_t *out;

    if (fseek(f, 0, SEEK_END) != 0) {
        wav_fail(err, errlen, "cannot determine the file length");
        return -1;
    }
    file_bytes = ftell(f);
    if (file_bytes < 0 || fseek(f, 0, SEEK_SET) != 0) {
        wav_fail(err, errlen, "cannot determine the file length");
        return -1;
    }

    if (!wav_read_exact(f, header, sizeof header)) {
        wav_fail(err, errlen, "file is too short to hold a RIFF header");
        return -1;
    }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        char riff[5];
        char wave[5];
        wav_tag(header, riff);
        wav_tag(header + 8, wave);
        wav_fail(err, errlen, "not a RIFF WAVE file, header reads \"%s\" and \"%s\"",
                 riff, wave);
        return -1;
    }

    /* Chunks are walked rather than assumed, and each one is followed by a pad
       byte when its size is odd. */
    while (!have_fmt || !have_data) {
        uint8_t chunk[8];
        uint32_t size;

        if (!wav_read_exact(f, chunk, sizeof chunk)) {
            break;
        }
        size = wav_le32(chunk + 4);

        if (memcmp(chunk, "fmt ", 4) == 0 && !have_fmt) {
            if (wav_parse_fmt(f, size, &channels, &sample_rate, err, errlen) < 0) {
                return -1;
            }
            have_fmt = 1;
        } else if (memcmp(chunk, "data", 4) == 0 && !have_data) {
            data_pos = ftell(f);
            if (data_pos < 0) {
                wav_fail(err, errlen, "cannot locate the data chunk");
                return -1;
            }
            data_bytes = size;
            have_data = 1;
            if (!wav_skip(f, size)) {
                break;
            }
        } else if (!wav_skip(f, size)) {
            break;
        }

        if ((size & 1u) != 0 && !wav_skip(f, 1)) {
            break;
        }
    }

    if (!have_fmt) {
        wav_fail(err, errlen, "file has no fmt chunk");
        return -1;
    }
    if (!have_data) {
        wav_fail(err, errlen, "file has no data chunk");
        return -1;
    }
    if (data_bytes > WAV_MAX_DATA_BYTES) {
        wav_fail(err, errlen, "data chunk declares %lu bytes, the limit is %lu",
                 (unsigned long)data_bytes, (unsigned long)WAV_MAX_DATA_BYTES);
        return -1;
    }
    if ((unsigned long)(file_bytes - data_pos) < (unsigned long)data_bytes) {
        wav_fail(err, errlen,
                 "data chunk declares %lu bytes but only %lu are present",
                 (unsigned long)data_bytes, (unsigned long)(file_bytes - data_pos));
        return -1;
    }

    frames = data_bytes / (channels * 2u);
    if (frames == 0) {
        wav_fail(err, errlen, "data chunk holds no complete %lu channel frames",
                 (unsigned long)channels);
        return -1;
    }

    out = malloc((size_t)frames * sizeof(int16_t));
    if (out == NULL) {
        wav_fail(err, errlen, "cannot allocate %lu samples",
                 (unsigned long)frames);
        return -1;
    }

    if (fseek(f, data_pos, SEEK_SET) != 0) {
        free(out);
        wav_fail(err, errlen, "cannot seek back to the data chunk");
        return -1;
    }
    if (wav_decode(f, channels, frames, out, err, errlen) < 0) {
        free(out);
        return -1;
    }

    *samples = out;
    *count = frames;
    *rate = sample_rate;
    return 0;
}

int wav_read_mono16(const char *path, int16_t **samples, uint32_t *count,
                    uint32_t *rate, char *err, size_t errlen)
{
    FILE *f;
    int rc;

    if (samples == NULL || count == NULL || rate == NULL) {
        wav_fail(err, errlen, "samples, count and rate outputs are all required");
        return -1;
    }

    *samples = NULL;
    *count = 0;
    *rate = 0;

    if (path == NULL) {
        wav_fail(err, errlen, "no path given");
        return -1;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        wav_fail(err, errlen, "cannot open the file, %s", strerror(errno));
        return -1;
    }

    rc = wav_parse(f, samples, count, rate, err, errlen);
    fclose(f);

    if (rc < 0) {
        *samples = NULL;
        *count = 0;
        *rate = 0;
    }
    return rc;
}

void wav_free(int16_t *samples)
{
    free(samples);
}
