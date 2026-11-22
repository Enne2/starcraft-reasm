#include <stdint.h>
#include <stdio.h>
#include <string.h>

int storm_scomp_decompress(void *dst, uint32_t *dst_len, const void *src, uint32_t src_len);

static int check(int ok, const char *what) {
    if (!ok) fprintf(stderr, "x86_scomp_test: FAIL: %s\n", what);
    return ok;
}

int main(void) {
    int ok = 1;
    uint8_t src1[] = {0x40, 1, 2, 3}, dst1[8] = {0};
    uint32_t n1 = sizeof src1;
    int r = storm_scomp_decompress(dst1, &n1, src1, sizeof src1);
    ok &= check(r == 1 && n1 == sizeof src1 && memcmp(dst1, src1, n1) == 0,
                "src_len == dst_cap copy");

    {
        uint8_t src[] = {0x00, 'M', 'P', 'Q'}, dst[8] = {0};
        uint32_t n = 8;
        r = storm_scomp_decompress(dst, &n, src, sizeof src);
        ok &= check(r == 1 && n == 3 && memcmp(dst, "MPQ", 3) == 0,
                    "stored block strips compression byte");
    }
    {
        /* ADPCM mono header: reserved, shift, initial signed sample 0x1234. */
        uint8_t src[] = {0x40, 0, 0, 0x34, 0x12}, dst[8] = {0};
        uint32_t n = sizeof dst;
        r = storm_scomp_decompress(dst, &n, src, sizeof src);
        ok &= check(r == 1 && n == 2 && dst[0] == 0x34 && dst[1] == 0x12,
                    "ADPCM mono emits its initial sample");
    }
    {
        uint8_t src[] = {0x08, 1, 2, 3}, dst[8] = {0};
        uint32_t n = 8;
        r = storm_scomp_decompress(dst, &n, src, sizeof src);
        ok &= check(r == -1 && n == 8, "unsupported codec requests exact guest fallback");
    }
    {
        uint8_t src[] = {0x00, 1, 2, 3}, dst[2] = {0xAA, 0xBB};
        uint32_t n = 2;
        r = storm_scomp_decompress(dst, &n, src, sizeof src);
        ok &= check(r == -1 && n == 2 && dst[0] == 0xAA && dst[1] == 0xBB,
                    "undersized destination is not modified");
    }
    if (!ok) return 1;
    puts("x86_scomp_test: PASS (copy paths + synthetic ADPCM; unsupported codecs use lifted fallback)");
    return 0;
}
