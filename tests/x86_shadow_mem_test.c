#define _GNU_SOURCE
#include "game_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

volatile uint32_t g_vm_generation;
extern int proc_range(uint32_t, uint32_t, int);
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
static const char ro[] = "ELF read-only literal";
static char rw[64];

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    long page=sysconf(_SC_PAGESIZE);
    unsigned char *two;
    CHECK(page==4096);
    CHECK(arm_register_region(0x12000000u,4096u,"unit-page")==0);
    CHECK(arm_va_is_mapped(0x12000000u,4096u));
    CHECK(!arm_va_is_mapped(0x12000fffu,2u));
    CHECK(!arm_va_is_mapped(0x7fffffffu,2u));
    CHECK(!arm_va_is_mapped(0x80000000u,1u));
    CHECK(!arm_va_is_mapped(0x12000000u,0xffffffffu));
    arm_shadow_set_range(0x13000000u,4096u,ARM_SHADOW_R|ARM_SHADOW_W);
    CHECK(!arm_va_is_mapped(0x13000000u,4u));

    two=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(two!=MAP_FAILED);
    CHECK(mprotect(two+4096,4096,PROT_NONE)==0);
    CHECK(proc_range((uint32_t)(uintptr_t)ro,sizeof ro,0));
    CHECK(!proc_range((uint32_t)(uintptr_t)ro,sizeof ro,1));
    CHECK(proc_range((uint32_t)(uintptr_t)rw,sizeof rw,1));
    CHECK(!proc_range((uint32_t)(uintptr_t)two,8192,0));
    CHECK(!proc_range(0xfffffff0u,64u,0));

    /* Model committed guest RW pages as VirtualAlloc does when it updates the shadow metadata. */
    arm_shadow_set_range(0x14000000u,8192u,ARM_SHADOW_GUEST|ARM_SHADOW_R|ARM_SHADOW_W);
    CHECK(arm_va_is_mapped(0x14000fffu,2u));
    arm_shadow_set_range(0x14001000u,4096u,0u); /* MEM_DECOMMIT / VirtualFree */
    CHECK(!arm_va_is_mapped(0x14000000u,8192u));
    CHECK(munmap(two,8192)==0);
    puts("x86_shadow_mem_test: PASS");
    return 0;
}
