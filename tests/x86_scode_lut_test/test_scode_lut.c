#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#if UINTPTR_MAX > UINT32_MAX
#define _Static_assert(condition,message)
#endif
#include "../../runtime/common/storm_sdraw_arm.c"
#if UINTPTR_MAX > UINT32_MAX
#undef _Static_assert
#endif

volatile uint32_t g_vm_generation;
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define ARENA_SIZE (2u*1024u*1024u)
#define CODE1_SIZE 0x4Au
#define CODE2_SIZE 0x2BC5u
#define CODE3_SIZE 0x4Au
#define STUB 0x15030D30u

typedef struct { uint8_t *mem; uint32_t base; } Arena;
static Arena arena_new(void) {
    Arena a; int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_32BIT
    flags|=MAP_32BIT;
#endif
    a.mem=mmap(NULL,ARENA_SIZE,PROT_READ|PROT_WRITE,flags,-1,0);
    a.base=(a.mem==MAP_FAILED)?0:(uint32_t)(uintptr_t)a.mem;
    return a;
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put_rel(uint8_t *at,uint32_t after,uint32_t target) { put32(at,target-after); }
static int read_dump(uint8_t *one,uint8_t *two) {
    FILE *f=fopen("build/qa/b1-hover/scode-0x4a-0x2bc5.hex","r");
    char *line=malloc(40000u); unsigned row=0,n=0;
    if(!f || !line) return 0;
    while(fgets(line,40000,f)) {
        char *p=line;
        if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F'))) continue;
        while(*p) {
            unsigned v; int used=0;
            while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p;
            if(!*p) break;
            if(sscanf(p,"%2x%n",&v,&used)!=1||used!=2) { fclose(f); free(line); return 0; }
            if(row==0) { if(n>=CODE1_SIZE) break; one[n++]=(uint8_t)v; }
            else { if(n>=CODE2_SIZE) break; two[n++]=(uint8_t)v; }
            p+=used;
        }
        if(row==0) { if(n!=CODE1_SIZE) { fclose(f); free(line); return 0; } row=1; n=0; }
        else if(n==CODE2_SIZE) break;
    }
    fclose(f); free(line); return row==1 && n==CODE2_SIZE;
}
static void make_codes(uint8_t *c1,uint8_t *c2,uint8_t *c3,const uint8_t *dump1,
                       const uint8_t *dump2,uint32_t prefix,uint32_t q,uint32_t r,
                       uint32_t *entry,uint32_t *t1,uint32_t *t2,uint32_t *t3,
                       uint32_t a1,uint32_t a2,uint32_t a3) {
    uint32_t i;
    memcpy(c1,dump1,CODE1_SIZE); memcpy(c2,dump2,CODE2_SIZE);
    for(i=0;i<3u;i++) memcpy(c3+i*23u,dump1+i*23u,23u);
    c1[69]=c2[11200]=c3[69]=0xE9;
    *entry=a1+(3u-prefix)*23u;
    *t1=a2+(160u-q)*70u;
    *t2=a3+(3u-r)*23u; *t3=STUB;
    put_rel(c1+70u,a1+CODE1_SIZE,*t1);
    put_rel(c2+11201u,a2+CODE2_SIZE,*t2);
    put_rel(c3+70u,a3+CODE3_SIZE,*t3);
}
static void setup_desc(uint8_t *p,uint32_t rows,uint32_t width,uint32_t dd,uint32_t sd,
                       uint32_t dst,uint32_t src,uint32_t lut,uint32_t dd_alt,uint32_t sd_alt) {
    memset(p,0,0x40); put32(p+12,rows); put32(p+16,dd); put32(p+20,sd);
    put32(p+24,dst); put32(p+28,src); put32(p+32,lut);
    put32(p+48,dd_alt); put32(p+52,sd_alt); (void)width;
}
static uint64_t ticks_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
}
int main(void) {
    uint8_t d1[CODE1_SIZE],d2[CODE2_SIZE];
    Arena a=arena_new(); uint8_t *p,*c1,*c2,*c3,*src0,*dst0,*src1,*dst1,*lut;
    uint32_t code1,code2,code3,table; unsigned width,al,di,si;
    static const int32_t deltas[]={-7,0,13};
    CHECK(a.mem!=MAP_FAILED && a.base && a.base+ARENA_SIZE> a.base);
    CHECK(read_dump(d1,d2));
    p=a.mem+0x1000; c1=a.mem+0x2000; c2=a.mem+0x3000; c3=a.mem+0x6000;
    src0=a.mem+0x10000; dst0=a.mem+0x20000; src1=a.mem+0x30000; dst1=a.mem+0x40000; lut=a.mem+0x50000;
    code1=a.base+0x2000; code2=a.base+0x3000; code3=a.base+0x6000;
    table=a.base+0x50000;
    for(width=1;width<=364;width++) for(al=0;al<4;al++) for(di=0;di<3;di++) for(si=0;si<3;si++) {
        uint32_t rows=3,dd=(uint32_t)deltas[di],sd=(uint32_t)deltas[si];
        uint32_t prefix=(4u-al)&3u,q,r,entry,t1,t2,t3,src_addr,dst_addr;
        ScSpan ss,ds,bs; _cpu x,y; uint8_t *px=p,*py=a.mem+0x1100;
        if(prefix>width) prefix=width;
        q=(width-prefix)>>2; r=(width-prefix)&3u;
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        /* Force requested low-address alignments while retaining room for either stride sign. */
        dst_addr=a.base+(uint32_t)(dst0-a.mem)+0x8000u+al;
        src_addr=a.base+(uint32_t)(src0-a.mem)+0x8000u;
        memset(src0,0xA5,0x10000); memset(dst0,0x5A,0x10000);
        memset(src1,0xA5,0x10000); memset(dst1,0x5A,0x10000);
        for(unsigned i=0;i<65536u;i++) lut[i]=(uint8_t)((i*73u+(i>>8)*19u+11u)&255u);
        for(unsigned yrow=0;yrow<rows;yrow++) {
            int64_t soff=(int64_t)0x8000+(int64_t)yrow*((int64_t)width+(int32_t)sd);
            int64_t doff=(int64_t)0x8000+al+(int64_t)yrow*((int64_t)width+(int32_t)dd);
            for(unsigned xpix=0;xpix<width;xpix++) {
                src0[soff+xpix]=(uint8_t)(xpix*13u+yrow*47u+width);
                dst0[doff+xpix]=(uint8_t)(xpix*29u+yrow*31u+al);
                src1[soff+xpix]=src0[soff+xpix]; dst1[doff+xpix]=dst0[doff+xpix];
            }
        }
        memcpy(src1,src0,0x10000); memcpy(dst1,dst0,0x10000);
        setup_desc(px,rows,width,dd,sd,dst_addr,src_addr,table,0,0);
        setup_desc(py,rows,width,dd,sd,dst_addr+(uint32_t)(dst1-dst0),src_addr+(uint32_t)(src1-src0),table,0,0);
        memset(&x,0,sizeof x); memset(&y,0,sizeof y); x._edx=y._edx=0x13579BDFu;
        CHECK(validate_rows(src_addr,rows,width,sd,0,0,0,&ss));
        CHECK(validate_rows(dst_addr,rows,width,dd,0,0,1,&ds));
        CHECK(proc_range(table,65536,0)); bs.lo=table;bs.hi=table+65536u;bs.valid=1;
        CHECK(match_1x_lut_kernel(0x40u,0x40000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                  code3,CODE3_SIZE,entry,t1,t2,t3,width,dst_addr));
        run_scode(&x,entry,code1,CODE1_SIZE,code2,CODE2_SIZE,code3,CODE3_SIZE, a.base+0x1000,
                  dd,sd,&ss,&ds,&bs);
        run_1x_lut(&y,entry,a.base+0x1100,rows,width,dd,sd,prefix);
        CHECK(memcmp(src0,src1,0x10000)==0);
        CHECK(memcmp(dst0,dst1,0x10000)==0);
        CHECK(x._eax==y._eax && x._ecx==y._ecx && x._edx==y._edx && x._ebx==y._ebx);
        CHECK(get32(px+36)==get32(py+36) && get32(px+40)==get32(py+40) && get32(px+44)==get32(py+44));
        CHECK(!match_1x_lut_kernel(0x40u,0x04040000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                   code3,CODE3_SIZE,entry,t1,t2,t3,width,dst_addr));
    }
    puts("x86_scode_lut_test: 1x LUT interpreter parity PASS (width=1..364, align=0..3, signed strides)");
    {
        uint32_t w=364,rows=4,prefix=0,q=(w-prefix)>>2,r=(w-prefix)&3u,entry,t1,t2,t3;
        uint32_t saddr=a.base+0x18000u,daddr=a.base+0x28000u;
        ScSpan ss,ds,bs; _cpu x,y; uint64_t t0,t1ns,t2ns; unsigned iter;
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        memset(src0,0x33,0x10000); memset(dst0,0x55,0x10000);
        memset(src1,0x33,0x10000); memset(dst1,0x55,0x10000);
        setup_desc(p,rows,w,0,0,daddr,saddr,table,0,0);
        setup_desc(a.mem+0x1100,rows,w,0,0,daddr+0x20000u,saddr+0x20000u,table,0,0);
        CHECK(validate_rows(saddr,rows,w,0,0,0,0,&ss));
        CHECK(validate_rows(daddr,rows,w,0,0,0,1,&ds));
        bs.lo=table;bs.hi=table+65536u;bs.valid=1;
        memset(&x,0,sizeof x);memset(&y,0,sizeof y);
        t0=ticks_ns();
        for(iter=0;iter<100u;iter++) { x._edx=0x12345678u;
            run_scode(&x,entry,code1,CODE1_SIZE,code2,CODE2_SIZE,code3,CODE3_SIZE,a.base+0x1000,0,0,&ss,&ds,&bs); }
        t1ns=ticks_ns()-t0;
        t0=ticks_ns();
        for(iter=0;iter<100u;iter++) { y._edx=0x12345678u;
            run_1x_lut(&y,entry,a.base+0x1100,rows,w,0,0,prefix); }
        t2ns=ticks_ns()-t0;
        printf("A/B (100x %ux%u): interpreter=%llu ns C-fast=%llu ns speedup=%.2fx\n",
               w,rows,(unsigned long long)t1ns,(unsigned long long)t2ns,
               t2ns?(double)t1ns/(double)t2ns:0.0);
    }
    /* Exact-byte deviations and mismatched variants must fail closed. */
    {
        uint32_t w=65,prefix=0,q=w>>2,r=w&3u,entry,t1,t2,t3,daddr=a.base+0x28000u;
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        CHECK(match_1x_lut_kernel(0x40u,0x40000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                  code3,CODE3_SIZE,entry,t1,t2,t3,w,daddr));
        c1[7]=0x0E; c1[9]=0x17;
        CHECK(!match_1x_lut_kernel(0x40u,0x40000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                   code3,CODE3_SIZE,entry,t1,t2,t3,w,daddr));
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        c2[7]=0x0E; c2[9]=0x17;
        CHECK(!match_1x_lut_kernel(0x40u,0x40000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                   code3,CODE3_SIZE,entry,t1,t2,t3,w,daddr));
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        c3[6]^=1u;
        CHECK(!match_1x_lut_kernel(0x40u,0x40000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                   code3,CODE3_SIZE,entry,t1,t2,t3,w,daddr));
    }

    /* Interlaced flag is deliberately rejected by the fast-path matcher; its alternate
       row deltas remain covered by the interpreter and are checked against a byte reference. */
    {
        uint32_t w=37,rows=5,prefix=0,q=w>>2,r=w&3u,entry,t1,t2,t3;
        uint32_t dd=3,sd=5,dda=11,sda=-9;
        ScSpan ss,ds,bs; _cpu z; uint32_t saddr=a.base+0x18000,daddr=a.base+0x28000;
        make_codes(c1,c2,c3,d1,d2,prefix,q,r,&entry,&t1,&t2,&t3,code1,code2,code3);
        memset(src0,0x73,0x10000); memset(dst0,0x22,0x10000);
        memcpy(src1,src0,0x10000); memcpy(dst1,dst0,0x10000);
        saddr=a.base+0x18000; daddr=a.base+0x28000;
        for(unsigned yy=0;yy<rows;yy++) {
            uint32_t step_s=w+((rows-yy)&1u?(uint32_t)sda:sd);
            uint32_t step_d=w+((rows-yy)&1u?dda:dd);
            uint32_t rd=daddr+0x20000u;
            for(unsigned xx=0;xx<w;xx++) {
                uint8_t sv=*(uint8_t *)(unsigned long)(saddr+xx),dv=*(uint8_t *)(unsigned long)(daddr+xx);
                *(uint8_t *)(unsigned long)(rd+xx)=lut[((uint32_t)sv<<8)|dv];
            }
            saddr+=step_s; daddr+=step_d;
        }
        CHECK(!match_1x_lut_kernel(0x40u,0x04040000u,table,code1,CODE1_SIZE,code2,CODE2_SIZE,
                                   code3,CODE3_SIZE,entry,t1,t2,t3,w,daddr));
        CHECK(validate_rows(a.base+0x18000,rows,w,sd,sda,1,0,&ss));
        CHECK(validate_rows(a.base+0x28000,rows,w,dd,dda,1,1,&ds));
        CHECK(proc_range(table,65536,0)); bs.lo=table;bs.hi=table+65536u;bs.valid=1;
        setup_desc(p,rows,w,dd,sd,a.base+0x28000,a.base+0x18000,table,dda,sda);
        memset(&z,0,sizeof z); z._edx=0xABCDEF01u;
        /* D50 continuation is encoded by code3's final rel32 target. */ put_rel(c3+70u,code3+CODE3_SIZE,0x15030D50u);
        t3=0x15030D50u;
        /* Use the general interpreter with exact interlaced dispatch. */
        run_scode(&z,entry,code1,CODE1_SIZE,code2,CODE2_SIZE,code3,CODE3_SIZE,a.base+0x1000,
                  dd,sd,&ss,&ds,&bs);
        CHECK(z._eax==1);
        CHECK(memcmp(dst0,dst1,0x10000)==0);
    }
    puts("x86_scode_lut_test: interlaced fallback PASS");
    munmap(a.mem,ARENA_SIZE);
    return 0;
}
