/* runtime/arm/game_memory_arm.h — guest address space for the ARMv6 runtime.
 *
 * Requirement that drives everything here: Win32 32-bit code (storm.dll in particular) encodes
 * "empty container" as `~ptr`, which is negative only when the pointer is < 0x80000000. On an
 * ARMv6 Linux process the whole user space is below 0xC0000000 and the default stack sits at
 * ~0xBEFxxxxx, so *nothing* is guaranteed low automatically. Hence:
 *   - the PE images are restored at their original VAs (0x00400000 exe, 0x15000000 storm),
 *   - every guest heap/TEB/CPU structure is carved from an arena reserved below 0x80000000,
 *   - `arm_va_is_low()` / `arm_selftest_low()` make the invariant testable at startup.
 * No MAP_32BIT (x86_64-only flag) and no `personality(ADDR_LIMIT_32BIT)` are used.
 */
#ifndef ARM_GAME_MEMORY_H
#define ARM_GAME_MEMORY_H

#include <stdint.h>

#define ARM_LOW_LIMIT        0x80000000u   /* exclusive: guest pointers must be below this */
#define ARM_ARENA_BASE       0x0E000000u   /* heap arena for runtime structures (low);
                                             * 0x50C000..0x6DD694 belongs to the translated
                                             * exe .data at its original VA */
#define ARM_ARENA_SIZE       0x01000000u   /* 16 MiB, contiguous and fixed */
#define ARM_STACK_BASE       0x02000000u   /* guest CPU stack (low) */
#define ARM_STACK_MAX        0x00400000u   /* 4 MiB max per stack */

#define ARM_SHADOW_R         0x01u
#define ARM_SHADOW_W         0x02u
#define ARM_SHADOW_X         0x04u
#define ARM_SHADOW_GUEST     0x08u

/* Map [va, va+size) exactly (MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, RW). Returns 0 on success.
 * Refuses VAs above ARM_LOW_LIMIT-1 unless `allow_high` is set (image VAs are always low). */
int arm_map_at(uint32_t va, uint32_t size);

/* Reserve the low arena once (idempotent). Returns 0 on success. */
int arm_arena_init(void);

/* Bump allocator inside the low arena. `align` must be a power of two (8 recommended for _cpu,
 * which holds doubles). Returns 0 on exhaustion. All results satisfy < 0x80000000. */
void *arm_alloc_low(uint32_t size, uint32_t align);

uint32_t arm_arena_used(void);
uint32_t arm_arena_free(void);

/* True when `va` is a plausible guest pointer: non-null, below ARM_LOW_LIMIT and inside a
 * region this runtime mapped (arena or a mapped image). Used to turn a wild guest pointer into
 * a diagnosable stop instead of a SIGSEGV inside translated code. */
int arm_va_is_low(uint32_t va);
int arm_va_is_mapped(uint32_t va, uint32_t len);
/* Flat 4 GiB / 4 KiB page permission map. Range operations include partial edge pages. */
void arm_shadow_set_range(uint32_t base, uint32_t size, uint8_t perm);
void arm_shadow_add_range(uint32_t base, uint64_t size, uint8_t perm);
void arm_shadow_clear_host(void);
int arm_shadow_check_range(uint32_t base, uint32_t size, uint8_t required);

/* Register a region the runtime did not map itself (image data placed by the linker at its
 * original VA, PE header pages). Returns 0 on success. */
int arm_register_region(uint32_t base, uint32_t size, const char *what);

/* ~ptr semantics check that Win32 code relies on (documented in sc-reasm main_elf.c). Returns 0
 * when the invariant holds, -1 with a log line otherwise. */
int arm_selftest_low(void);

void arm_mem_log(const char *s);
void arm_mem_log_hex(const char *prefix, uint32_t v);

#endif /* ARM_GAME_MEMORY_H */
