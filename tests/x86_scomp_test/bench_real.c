/* Five-run throughput benchmark for the native #552 codecs over captured real sectors.
 * PKWARE (mask 0x08) and the Huffman chains (masks 0x41/0x81, Huffman -> ADPCM) are timed
 * separately; input files are loaded before timing and every run verifies the return value,
 * the output length and the output bytes against the capture produced by the original routine.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int storm_scomp_decompress(void *dst, uint32_t *dst_len, const void *src, uint32_t src_len);

typedef struct { uint8_t *src, *out, *expect; uint32_t src_len, cap, expected; } sample;
static sample *samples;
static size_t count, total_out;

static int load_file(const char *path, uint8_t **data, size_t *len) {
    FILE *f = fopen(path, "rb"); long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    *data = malloc(n ? (size_t)n : 1);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*data); return -1; }
    fclose(f); *len = (size_t)n; return 0;
}

static int load_samples(const char *dir, unsigned want_mask, int huffman_masks) {
    DIR *d = opendir(dir); struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        size_t name_len = strlen(e->d_name), meta_len, src_len;
        char base[1024], path[1200], meta[512]; uint8_t *meta_bytes, *src;
        unsigned mask = 0; long cap = -1, expected = -1; size_t ignored;
        if (name_len < 6 || strcmp(e->d_name + name_len - 5, ".meta")) continue;
        if (name_len >= sizeof base) { closedir(d); return -1; }
        memcpy(base, e->d_name, name_len - 5); base[name_len - 5] = 0;
        snprintf(path, sizeof path, "%s/%s.meta", dir, base);
        if (load_file(path, &meta_bytes, &meta_len) || meta_len >= sizeof meta) continue;
        memcpy(meta, meta_bytes, meta_len); meta[meta_len] = 0; free(meta_bytes);
        if (sscanf(meta, "mask=0x%x", &mask) != 1) continue;
        if (huffman_masks ? (mask != 0x41 && mask != 0x81) : (mask != want_mask)) continue;
        { char *p = strstr(meta, "dst_cap="); if (p) cap = atol(p + 8); }
        { char *p = strstr(meta, "dst_len="); if (p) expected = atol(p + 8); }
        if (cap < 0 || expected < 0 || expected > cap) { closedir(d); return -1; }
        snprintf(path, sizeof path, "%s/%s.src", dir, base);
        if (load_file(path, &src, &src_len) || src_len > UINT32_MAX) { closedir(d); return -1; }
        sample *next = realloc(samples, (count + 1) * sizeof *samples);
        if (!next) { free(src); closedir(d); return -1; }
        samples = next;
        samples[count].src = src; samples[count].src_len = (uint32_t)src_len;
        samples[count].cap = (uint32_t)cap; samples[count].expected = (uint32_t)expected;
        samples[count].out = malloc(cap ? (size_t)cap : 1);
        if (!samples[count].out) { closedir(d); return -1; }
        snprintf(path, sizeof path, "%s/%s.dst", dir, base);
        if (load_file(path, &samples[count].expect, &ignored) || ignored != (size_t)expected) {
            closedir(d); return -1;
        }
        count++; total_out += (size_t)expected; (void)ignored;
    }
    closedir(d); return 0;
}

static double elapsed(struct timespec a, struct timespec b) {
    return (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9;
}

/* Runs the benchmark on the currently loaded samples; verifies bytes, not just lengths. */
static int run_bench(const char *label) {
    double runs[5];
    for (unsigned r = 0; r < 5; ++r) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (size_t i = 0; i < count; ++i) {
            uint32_t n = samples[i].cap;
            int ret = storm_scomp_decompress(samples[i].out, &n, samples[i].src, samples[i].src_len);
            if (ret != 1 || n != samples[i].expected) {
                fprintf(stderr, "bench_real: correctness failure at sample %zu run %u (ret=%d len=%u expected=%u)\n",
                        i, r + 1, ret, n, samples[i].expected); return 1;
            }
            if (memcmp(samples[i].out, samples[i].expect, (size_t)n) != 0) {
                fprintf(stderr, "bench_real: byte mismatch at sample %zu run %u\n", i, r + 1); return 1;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &b); runs[r] = elapsed(a, b);
    }
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = i + 1; j < 5; ++j) if (runs[j] < runs[i]) { double t = runs[i]; runs[i] = runs[j]; runs[j] = t; }
    printf("bench_real: codec=%s sectors=%zu output=%.3f MiB; 5 runs (seconds): median=%.6f min=%.6f max=%.6f span=%.6f; median throughput=%.2f MiB/s\n",
           label, count, (double)total_out / (1024.0 * 1024.0), runs[2], runs[0], runs[4], runs[4] - runs[0],
           ((double)total_out / (1024.0 * 1024.0)) / runs[2]);
    return 0;
}

static void free_samples(void) {
    for (size_t i = 0; i < count; ++i) { free(samples[i].src); free(samples[i].out); free(samples[i].expect); }
    free(samples); samples = NULL; count = 0; total_out = 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/tmp/scomp-real-now";
    if (load_samples(dir, 0x08, 0) || !count) { fprintf(stderr, "bench_real: cannot load PKWARE samples from %s\n", dir); return 2; }
    if (run_bench("PKWARE mask=0x08")) return 1;
    free_samples();
    if (load_samples(dir, 0, 1) || !count) { fprintf(stderr, "bench_real: cannot load Huffman samples from %s\n", dir); return 2; }
    if (run_bench("Huffman+ADPCM mask=0x41/0x81")) return 1;
    free_samples();
    return 0;
}
