/* Differential check of storm_scomp_decompress against sectors captured from the real game
 * (SC_SCOMP_DUMP: "<id>.meta" + "<id>.src" + "<id>.dst", where .dst is exactly what the lifted
 * orig_storm_552 produced). Usable in the repository without the capture directory. */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int storm_scomp_decompress(void *dst, uint32_t *dst_len, const void *src, uint32_t src_len);

static int read_file(const char *path, uint8_t **buf, size_t *len) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!*buf || fread(*buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*buf); return -1; }
    fclose(f);
    *len = (size_t)n;
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/x86_scomp_test/samples";
    DIR *d;
    struct dirent *e;
    unsigned files = 0, ok = 0, skipped = 0, bad = 0, fallback = 0;
    unsigned by_mask[256];
    memset(by_mask, 0, sizeof by_mask);
    d = opendir(dir);
    if (!d) { fprintf(stderr, "diff_real: %s non leggibile\n", dir); return 1; }
    while ((e = readdir(d)) != NULL) {
        char base[1024], path[1200], meta[512];
        size_t n = strlen(e->d_name);
        uint8_t *msrc, *msrmeta, *dst;
        size_t lsrc, lmeta, ldst;
        long src_len = -1, cap = -1, dst_len = -1;
        unsigned mask = 0;
        uint32_t got;
        int r;
        if (n < 9 || strcmp(e->d_name + n - 5, ".meta") != 0) continue;
        memset(base, 0, sizeof base);
        snprintf(base, sizeof base, "%s", e->d_name);
        base[n - 5] = 0;
        snprintf(path, sizeof path, "%s/%s.meta", dir, base);
        if (read_file(path, &msrmeta, &lmeta) < 0) continue;
        meta[0] = 0;
        if (lmeta < sizeof meta) { memcpy(meta, msrmeta, lmeta); meta[lmeta] = 0; }
        free(msrmeta);
        if (sscanf(meta, "mask=0x%x", &mask) != 1) { skipped++; continue; }
        { char *p;
          if ((p = strstr(meta, "src_len=")) != NULL) src_len = atol(p + 8);
          if ((p = strstr(meta, "dst_cap=")) != NULL) cap = atol(p + 8);
          if ((p = strstr(meta, "dst_len=")) != NULL) dst_len = atol(p + 8); }
        if (src_len < 0 || cap < 0 || dst_len < 0) { skipped++; continue; }
        snprintf(path, sizeof path, "%s/%s.src", dir, base);
        if (read_file(path, &msrc, &lsrc) < 0) { skipped++; continue; }
        snprintf(path, sizeof path, "%s/%s.dst", dir, base);
        if (read_file(path, &dst, &ldst) < 0) { free(msrc); skipped++; continue; }
        got = (uint32_t)cap;
        {
            uint8_t *out = calloc(1, (size_t)cap + 1);
            r = storm_scomp_decompress(out, &got, msrc, (uint32_t)src_len);
            files++;
            by_mask[mask & 0xFF]++;
            if (r < 0) {
                fallback++;
            } else if (r == 1 && got == (uint32_t)dst_len && ldst == (size_t)dst_len &&
                       memcmp(out, dst, (size_t)dst_len) == 0) {
                ok++;
            } else {
                if (bad < 5)
                    fprintf(stderr, "diff_real: %s mask=0x%02x ret=%d len=%u/%ld differisce\n",
                            base, mask, r, got, dst_len);
                bad++;
            }
            free(out);
        }
        free(msrc); free(dst);
    }
    closedir(d);
    printf("diff_real: %u settori, %u identici, %u differenze, %u fallback, %u salti\n",
           files, ok, bad, fallback, skipped);
    { unsigned m;
      for (m = 0; m < 256; ++m)
          if (by_mask[m]) printf("  mask 0x%02x: %u\n", m, by_mask[m]); }
    return (bad == 0) ? 0 : 1;
}
