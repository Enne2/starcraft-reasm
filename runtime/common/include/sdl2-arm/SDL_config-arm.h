/*
 * SDL2 configuration for ARMv6 hard-float Linux (Raspberry Pi OS armhf).
 *
 * The Raspberry Pi OS image ships libSDL2-2.0.so.0 (runtime only, no -dev
 * package), so the SDL2 headers come from the host SDL2-devel package.  The
 * host's SDL_config.h wrapper dispatches on __arm__ to this file, which simply
 * selects the generic POSIX/POSIX-threads configuration: SDL_config_unix.h is
 * itself architecture-neutral (it derives SIZEOF_VOIDP from __SIZEOF_POINTER__
 * and gates the only x86-specific stanza on __i386__/__x86_64__).
 *
 * No ABI definition is invented here: only the configure-time feature macros
 * that the shim sources need for compilation are selected.
 */
#ifndef SDL_config_arm_h_
#define SDL_config_arm_h_
#include <SDL2/SDL_config_unix.h>
#endif /* SDL_config_arm_h_ */
