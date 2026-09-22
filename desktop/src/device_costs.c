#include "device_costs.h"

#include <stdio.h>
#include <string.h>

#define BUILTIN_SOURCE \
    "experiments/2026-09-22_094445_hardware-stem-border-and-vcvtr (built in)"

#define COST_PREFIX     "BENCH_COST "
#define COST_PREFIX_LEN (sizeof(COST_PREFIX) - 1u)

#define LINE_CAPACITY 512

/* The caller may want no message at all, so every write goes through here. */
#define SET_ERR(...)                                \
    do {                                            \
        if (err != NULL && errlen > 0u) {           \
            snprintf(err, errlen, __VA_ARGS__);     \
        }                                           \
    } while (0)

static const char *const layer_names[DEVICE_COST_LAYERS] = {
    "stem", "dw0", "pw0", "dw1", "pw1", "dw2", "pw2", "dw3", "pw3", "fc"
};

static const uint32_t builtin_int8[DEVICE_COST_LAYERS] = {
    3918368u, 2309309u, 2536052u, 2251476u, 2533377u,
    2259300u, 2608027u, 2254886u, 2528005u, 3434u
};

static const uint32_t builtin_fp32[DEVICE_COST_LAYERS] = {
    7461455u, 1690319u, 4372440u, 1554669u, 4811547u,
    1554517u, 4351029u, 1690260u, 4376615u, 4883u
};

/* Captures taken over serial carry CRLF, so a carriage return counts as a
   separator and can never end up inside a layer name. */
static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *skip_blanks(const char *p)
{
    while (*p != '\0' && is_blank(*p)) {
        p++;
    }
    return p;
}

/* Reads digits only, so a sign is rejected, and stops the moment the value
   would exceed uint32 rather than letting a corrupt capture wrap silently. */
static int read_u32(const char **cursor, uint32_t *value)
{
    const char *p = *cursor;
    uint64_t acc = 0u;

    if (*p < '0' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10u + (uint64_t)(*p - '0');
        if (acc > 0xFFFFFFFFu) {
            return -1;
        }
        p++;
    }

    *cursor = p;
    *value = (uint32_t)acc;
    return 0;
}

static int read_literal(const char **cursor, const char *literal)
{
    size_t len = strlen(literal);

    if (strncmp(*cursor, literal, len) != 0) {
        return -1;
    }
    *cursor += len;
    return 0;
}

static int read_name(const char **cursor, char *dst, size_t dstlen)
{
    const char *p = *cursor;
    size_t n = 0u;

    while (*p != '\0' && !is_blank(*p)) {
        if (n + 1u >= dstlen) {
            return -1;
        }
        dst[n++] = *p++;
    }
    if (n == 0u) {
        return -1;
    }

    dst[n] = '\0';
    *cursor = p;
    return 0;
}

static void drain_line(FILE *f)
{
    int ch;

    while ((ch = fgetc(f)) != EOF && ch != '\n') {
    }
}

int device_costs_load(const char *report_path, device_costs_t *out,
                      char *err, size_t errlen)
{
    char line[LINE_CAPACITY];
    int seen[DEVICE_COST_LAYERS];
    unsigned long lineno = 0u;
    int found = 0;
    int i;
    FILE *f;

    if (err != NULL && errlen > 0u) {
        err[0] = '\0';
    }
    if (report_path == NULL || out == NULL) {
        SET_ERR("null argument");
        return -1;
    }

    /* layers stays zero until all ten indices have been seen, so a table from
       a failed load totals to nothing instead of to a partial figure. */
    memset(out, 0, sizeof(*out));
    memset(seen, 0, sizeof(seen));

    f = fopen(report_path, "r");
    if (f == NULL) {
        SET_ERR("cannot open '%s'", report_path);
        return -1;
    }

    while (fgets(line, (int)sizeof(line), f) != NULL) {
        const char *cursor;
        char name[16];
        uint32_t index;
        uint32_t int8_cycles;
        uint32_t fp32_cycles;

        lineno++;
        if (strchr(line, '\n') == NULL && feof(f) == 0) {
            drain_line(f);
        }

        cursor = skip_blanks(line);
        if (strncmp(cursor, COST_PREFIX, COST_PREFIX_LEN) != 0) {
            continue;
        }
        cursor = skip_blanks(cursor + COST_PREFIX_LEN);

        if (read_u32(&cursor, &index) != 0) {
            SET_ERR("line %lu: layer index is not a plain number", lineno);
            fclose(f);
            return -1;
        }
        if (index >= (uint32_t)DEVICE_COST_LAYERS) {
            SET_ERR("line %lu: layer index %lu outside 0 to %d",
                    lineno, (unsigned long)index, DEVICE_COST_LAYERS - 1);
            fclose(f);
            return -1;
        }
        if (seen[index] != 0) {
            SET_ERR("line %lu: duplicate BENCH_COST index %lu in '%s'",
                    lineno, (unsigned long)index, report_path);
            fclose(f);
            return -1;
        }

        cursor = skip_blanks(cursor);
        if (read_name(&cursor, name, sizeof(name)) != 0) {
            SET_ERR("line %lu: layer name missing or longer than %d characters",
                    lineno, (int)sizeof(name) - 1);
            fclose(f);
            return -1;
        }

        cursor = skip_blanks(cursor);
        if (read_literal(&cursor, "int8=") != 0 ||
            read_u32(&cursor, &int8_cycles) != 0) {
            SET_ERR("line %lu: int8 field missing or does not fit uint32", lineno);
            fclose(f);
            return -1;
        }

        cursor = skip_blanks(cursor);
        if (read_literal(&cursor, "fp32=") != 0 ||
            read_u32(&cursor, &fp32_cycles) != 0) {
            SET_ERR("line %lu: fp32 field missing or does not fit uint32", lineno);
            fclose(f);
            return -1;
        }

        out->int8_cycles[index] = int8_cycles;
        out->fp32_cycles[index] = fp32_cycles;
        snprintf(out->name[index], sizeof(out->name[index]), "%s", name);
        seen[index] = 1;
        found++;
    }

    fclose(f);

    if (found == 0) {
        SET_ERR("no BENCH_COST lines in '%s'", report_path);
        return -1;
    }
    for (i = 0; i < DEVICE_COST_LAYERS; i++) {
        if (seen[i] == 0) {
            SET_ERR("missing BENCH_COST index %d (%s) in '%s'",
                    i, layer_names[i], report_path);
            return -1;
        }
    }

    out->layers = DEVICE_COST_LAYERS;
    snprintf(out->source, sizeof(out->source), "%s", report_path);
    return 0;
}

void device_costs_builtin(device_costs_t *out)
{
    int i;

    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    for (i = 0; i < DEVICE_COST_LAYERS; i++) {
        out->int8_cycles[i] = builtin_int8[i];
        out->fp32_cycles[i] = builtin_fp32[i];
        snprintf(out->name[i], sizeof(out->name[i]), "%s", layer_names[i]);
    }
    out->layers = DEVICE_COST_LAYERS;
    snprintf(out->source, sizeof(out->source), "%s", BUILTIN_SOURCE);
}

uint32_t device_costs_total(const device_costs_t *c, uint32_t precision_mask)
{
    uint32_t total = 0u;
    int layers;
    int i;

    if (c == NULL) {
        return 0u;
    }

    layers = c->layers;
    if (layers > DEVICE_COST_LAYERS) {
        layers = DEVICE_COST_LAYERS;
    }
    for (i = 0; i < layers; i++) {
        total += ((precision_mask >> i) & 1u) ? c->fp32_cycles[i] : c->int8_cycles[i];
    }
    return total;
}
