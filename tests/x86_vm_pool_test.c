#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#define CLOCK_MONOTONIC 1

/* Standalone i386 unit harness: compile the production shim with section GC. */
#include "../runtime/shim-port/elf/win32_shim.c"
struct timespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int,void *);

static unsigned char shadow[1u << 20];
void arm_shadow_set_range(uint32_t a,uint32_t n,uint8_t flags) {
    uint32_t first=a>>12,last=(a+n-1u)>>12,i;
    for(i=first;i<=last;i++)shadow[i]=flags;
}
int arm_shadow_check_range(uint32_t a,uint32_t n,uint8_t flags) {
    uint32_t first=a>>12,last=(a+n-1u)>>12,i;
    if(!n)return 0;
    for(i=first;i<=last;i++)if((shadow[i]&flags)!=flags)return 0;
    return 1;
}
int proc_range(uint32_t a,uint32_t n,int write_access) {
    return arm_shadow_check_range(a,n,(uint8_t)(ARM_SHADOW_GUEST|ARM_SHADOW_R|(write_access?ARM_SHADOW_W:0)));
}

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;} } while(0)
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
int main(void) {
    const uint32_t alloc=(uint32_t)(MEM_RESERVE|MEM_COMMIT),rw=64u,release=MEM_RELEASE;
    void *p,*q,*oldest=0,*blocks[8]; unsigned i,j; double runs[5],before,after,tmp;
    p=VirtualAlloc(0,65536,alloc,rw);CHECK(p && (uintptr_t)p<0x80000000u);
    memset(p,0xa5,65536);CHECK(VirtualFree(p,0,release));
    q=VirtualAlloc(0,65536,alloc,rw);CHECK(q==p);CHECK((uintptr_t)q<0x80000000u);
    for(i=0;i<65536;i++)CHECK(((unsigned char*)q)[i]==0);
    CHECK(arm_shadow_check_range((uint32_t)(uintptr_t)q,65536,ARM_SHADOW_GUEST|ARM_SHADOW_R|ARM_SHADOW_W));
    CHECK(VirtualFree(q,0,release));

    before=now_s();
    for(i=0;i<10000;i++) { p=VirtualAlloc(0,65536,alloc,rw);CHECK(p);CHECK(VirtualFree(p,0,release)); }
    after=now_s();printf("pooled_10000_s=%.6f ",(after-before));
    /* Uncached MEM_DECOMMIT path supplies a within-process comparison for A/B. */
    before=now_s();
    for(i=0;i<10000;i++) { p=VirtualAlloc(0,65536,alloc,rw);CHECK(p);CHECK(VirtualFree(p,65536,MEM_DECOMMIT));CHECK(VirtualFree(p,0,release)); }
    after=now_s();printf("decommit_10000_s=%.6f\n",after-before);

    for(j=0;j<5;j++) {
        before=now_s();
        for(i=0;i<10000;i++) { p=VirtualAlloc(0,65536,alloc,rw);CHECK(p);CHECK(VirtualFree(p,0,release)); }
        runs[j]=now_s()-before;
    }
    for(i=0;i<5;i++)for(j=i+1;j<5;j++)if(runs[j]<runs[i]){tmp=runs[i];runs[i]=runs[j];runs[j]=tmp;}
    printf("pool_runs_s min=%.6f median=%.6f max=%.6f span=%.6f\n",runs[0],runs[2],runs[4],runs[4]-runs[0]);

    /* Fill beyond the 24 MiB cache cap; the oldest mapping must become PROT_NONE/unmapped. */
    for(i=0;i<8;i++) { blocks[i]=VirtualAlloc(0,4u*1024u*1024u,alloc,rw);CHECK(blocks[i]); }
    oldest=blocks[0];
    for(i=0;i<8;i++)CHECK(VirtualFree(blocks[i],0,release));
    CHECK(!arm_shadow_check_range((uint32_t)(uintptr_t)oldest,4096,ARM_SHADOW_GUEST|ARM_SHADOW_R));
    CHECK(!proc_range((uint32_t)(uintptr_t)oldest,4096,0));
    puts("x86_vm_pool_test: PASS");
    return 0;
}
