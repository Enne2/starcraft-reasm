/* runtime/arm/tests/sysroot_probe.c — minimal C + SDL2 ARMv6 program used to validate the
 * extracted Raspberry Pi OS sysroot *independently* of the runtime (task step 1).
 *
 * It must be a hard-float ARMv6 binary (arm1176jzf-s, VFPv2, no NEON) that links dynamically
 * against the sysroot's glibc and libSDL2-2.0.so.0, and it must run under
 *     qemu-arm-static -cpu arm1176 -L build/sysroot
 * with SDL_VIDEODRIVER=dummy (no display in CI). Exit code 0 = sysroot usable.
 */
#include <SDL2/SDL.h>

#include <stdio.h>
#include <string.h>

/* Soft-float would show up here: __ARM_PCS_VFP is defined only for hard-float AAPCS. */
#if !defined(__ARM_PCS_VFP)
#error "hard-float ABI required (VFPv2): compile with -mfloat-abi=hard"
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#error "ARMv6 has no NEON: -march=armv6zk -mfpu=vfp must disable it"
#endif

int main(void) {
    SDL_version lv;
    SDL_Window *win;
    SDL_Surface *surf;
    int ok = 1;

    printf("[probe] ARMv6 hard-float C+SDL2 sysroot test\n");
    printf("[probe] sizeof(void*)=%u float-abi=hard\n", (unsigned) sizeof(void *));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("[probe] SDL_Init FAILED: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GetVersion(&lv);
    printf("[probe] SDL %d.%d.%d driver=%s\n", lv.major, lv.minor, lv.patch,
           SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");

    win = SDL_CreateWindow("probe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 64, 64, 0);
    if (!win) { printf("[probe] CreateWindow FAILED: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
    surf = SDL_GetWindowSurface(win);
    if (!surf || !surf->pixels) { printf("[probe] GetWindowSurface FAILED\n"); SDL_DestroyWindow(win); SDL_Quit(); return 4; }
    SDL_FillRect(surf, NULL, SDL_MapRGB(surf->format, 0x20, 0x40, 0x60));
    SDL_UpdateWindowSurface(win);
    printf("[probe] surface %ux%u bpp=%u ok\n", (unsigned) surf->w, (unsigned) surf->h,
           (unsigned) surf->format->BitsPerPixel);

    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("[probe] ESITO: PASS\n");
    return ok ? 0 : 1;
}
