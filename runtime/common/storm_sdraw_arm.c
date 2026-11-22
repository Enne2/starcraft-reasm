/* ARMv6 replacements for Storm's generated SCode blitters.
 * Pixel semantics are grounded in the runtime JIT dump at 0x20170A9C:
 * PATCOPY bytes repeat `mov [edi],ecx; add edi,4` (see REPORT-arm-blt.md).
 * Storm 313's frame/register setup and row-stride deltas are at IDA 0x15036310-0x15036339.
 */
#include "cpu_llasm.h"
#include "game_memory_arm.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern int close(int);
extern int pthread_mutex_lock(void *);
extern int pthread_mutex_unlock(void *);
extern volatile uint32_t g_vm_generation;

/* #454 skip is opt-in and scoped to the intro/stinger asset namespace. The original
 * ordinal's first stdcall argument is the guest path at [ESP+4]. Lifted x86 code can pass
 * pointers to string literals in the host ELF image, not only guest VAs; validate both through
 * the guest-region table and the readable process-map cache before dereferencing. Never disable
 * libsmacker: menu assets (glue\\mainmenu\\*.smk) still need to open and animate. */
int proc_range(uint32_t p, uint32_t n, int write_access);
static int video_path_readable(uint32_t p, uint32_t n) {
    return arm_va_is_mapped(p, n) || proc_range(p, n, 0);
}
int storm_video_skip_enabled(void) {
    const char *v = getenv("SC_SKIP_INTRO");
    _cpu *cpu = cpu_llasm_current();
    uint32_t path, i;
    if (!v || strcmp(v, "1") != 0 || !cpu) return 0;
    path = cpu_llasm_read32(cpu->_esp + 4u);
    if (!video_path_readable(path, 1)) return 0;
    for (i = 0; i < 512u && video_path_readable(path + i, 1); ++i) {
        unsigned char c = *(const unsigned char *)(unsigned long)(path + i);
        if (!c) break;
        if ((c == 's' || c == 'S') && video_path_readable(path + i, 4)) {
            const unsigned char *p = (const unsigned char *)(unsigned long)(path + i);
            unsigned char m = p[1], k = p[2], sep = p[3];
            if ((m == 'm' || m == 'M') && (k == 'k' || k == 'K') &&
                (sep == '\\' || sep == '/')) return 1;
        }
    }
    return 0;
}
#include <stdio.h>
#include <string.h>

extern long write(int fd, const void *buf, unsigned long n);
extern void _exit(int);
extern char *getenv(const char *);

static void log_s(const char *s) { (void)write(2, s, strlen(s)); }
static void log_hex(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    char b[11]; int i;
    b[0]='0'; b[1]='x';
    for (i=0;i<8;i++) b[i+2]=h[(v>>(28-4*i))&15];
    b[10]='\n'; (void)write(2,b,sizeof b);
}
static void fail73(const char *why, uint32_t rop, uint32_t flags) {
    log_s("[storm-blt] ROP/flag NON IMPLEMENTATO: "); log_s(why);
    log_s(" ROP="); log_hex(rop); log_s(" flags="); log_hex(flags);
    _exit(73);
}

/* Fast range validation for Storm SCode. The old path opened and sscanf-parsed
 * /proc/self/maps on every pixel-buffer check. Cache the map snapshot; refresh only
 * on an address miss or when VirtualAlloc/Free/Protect advances g_vm_generation. */
typedef union { char bytes[40]; long long align; } MapMutex;
static MapMutex g_map_mutex;
#define MAP_CACHE_MAX 4096u
typedef struct { uint64_t lo, hi; uint8_t perm; } MapEntry;
static MapEntry g_map_cache[MAP_CACHE_MAX];
static unsigned g_map_count;
static uint32_t g_map_generation;
static int g_map_valid;

static int map_hex(const char **pp, uint64_t *out) {
    const char *p=*pp; uint64_t v=0; unsigned digits=0;
    for (;;) {
        unsigned d;
        if (*p>='0'&&*p<='9') d=(unsigned)(*p-'0');
        else if (*p>='a'&&*p<='f') d=(unsigned)(*p-'a'+10);
        else if (*p>='A'&&*p<='F') d=(unsigned)(*p-'A'+10);
        else break;
        if (v>(UINT64_MAX-d)/16u) return 0;
        v=(v<<4)|d; ++p; ++digits;
    }
    if (!digits) return 0;
    *pp=p; *out=v; return 1;
}
static int map_parse_line(const char *s, MapEntry *e) {
    uint64_t lo,hi; const char *p=s;
    if (!map_hex(&p,&lo) || *p++!='-' || !map_hex(&p,&hi) || hi<=lo) return 0;
    while (*p==' ') ++p;
    e->lo=lo; e->hi=hi; e->perm=0;
    if (p[0]=='r') e->perm|=1u;
    if (p[1]=='w') e->perm|=2u;
    if (p[2]=='x') e->perm|=4u;
    return 1;
}
static int map_refresh_locked(void) {
    MapEntry fresh[MAP_CACHE_MAX]; char chunk[2048], line[512];
    unsigned count=0; int fd=open("/proc/self/maps",0), used=0, overflow=0; long nr;
    if(fd<0) { g_map_valid=0; return 0; }
    while((nr=read(fd,chunk,sizeof chunk))>0) {
        long i;
        for(i=0;i<nr;i++) {
            char c=chunk[i];
            if(c!='\n') { if(used<(int)sizeof line-1) line[used++]=c; else overflow=1; continue; }
            line[used]=0;
            if(!overflow) { MapEntry e; if(map_parse_line(line,&e)) { if(count==MAP_CACHE_MAX) { close(fd); g_map_valid=0; return 0; } fresh[count++]=e; } }
            used=0; overflow=0;
        }
    }
    close(fd);
    if(nr<0 || overflow || used) { g_map_valid=0; return 0; }
    memcpy(g_map_cache,fresh,count*sizeof fresh[0]);
    g_map_count=count; g_map_generation=g_vm_generation; g_map_valid=1;
    arm_shadow_clear_host();
    { unsigned i; for(i=0;i<count;i++) arm_shadow_add_range((uint32_t)g_map_cache[i].lo,
        g_map_cache[i].hi-g_map_cache[i].lo,
        (uint8_t)(((g_map_cache[i].perm&1u)?ARM_SHADOW_R:0u) |
                  ((g_map_cache[i].perm&2u)?ARM_SHADOW_W:0u) |
                  ((g_map_cache[i].perm&4u)?ARM_SHADOW_X:0u))); }
    return 1;
}
static int map_contains(uint64_t start,uint64_t end,int write_access) {
    uint64_t cursor=start; unsigned i;
    for(i=0;i<g_map_count && cursor<end;i++) {
        MapEntry *e=&g_map_cache[i];
        if(e->hi<=cursor) continue;
        if(e->lo>cursor || !(e->perm&1u) || (write_access && !(e->perm&2u))) return 0;
        cursor=e->hi<end?e->hi:end;
    }
    return cursor==end;
}
int proc_range(uint32_t p,uint32_t n,int write_access) {
    uint64_t start=p,end=start+(uint64_t)n;
    uint8_t need=(uint8_t)(ARM_SHADOW_R|(write_access?ARM_SHADOW_W:0u));
    int ok,refreshed=0;
    if(!n || end>0x100000000ull) return 0;
    if(arm_shadow_check_range(p,n,need)) return 1;
    pthread_mutex_lock(&g_map_mutex);
    if(!g_map_valid || g_map_generation!=g_vm_generation) {
        (void)map_refresh_locked(); refreshed=1;
    }
    ok=arm_shadow_check_range(p,n,need);
    if(!ok && !refreshed) { (void)map_refresh_locked(); ok=arm_shadow_check_range(p,n,need); }
    if(!ok) ok=g_map_valid && map_contains(start,end,write_access);
    if(!ok && getenv("REASM_TRACE")) {
        static unsigned warned;
        if(warned++<4) {
            log_s("[storm-map] range reject p="); log_hex(p); log_s(" n="); log_hex(n);
            log_s(" write="); log_hex((uint32_t)write_access); log_s(" entries="); log_hex(g_map_count);
            log_s(" generation="); log_hex(g_map_generation); log_s(" valid="); log_hex((uint32_t)g_map_valid);
        }
    }
    pthread_mutex_unlock(&g_map_mutex);
    return ok;
}
/* stdcall return stack is [ret][arg1..argN]. reasm_extern pops the return itself,
 * so relocate it to the slot exposed after removing the arguments. */
static uint32_t args_and_relocate_return(_cpu *cpu, unsigned n, uint32_t *args) {
    uint32_t sp=cpu->_esp, ret=cpu_llasm_read32(sp), i;
    for (i=0;i<n;i++) args[i]=cpu_llasm_read32(sp+4u*(i+1u));
    cpu_llasm_write32(sp+4u*n,ret);
    cpu->_esp=sp+4u*n;
    return ret;
}

void storm_313_arm_body(_cpu *cpu) {
    uint32_t a[8], dst, src, width, rows, dst_pitch, src_pitch, pattern, rop;
    uint32_t row, col;
    (void)args_and_relocate_return(cpu,8,a);
    dst=a[0]; src=a[1]; width=a[2]; rows=a[3];
    dst_pitch=a[4]; src_pitch=a[5]; pattern=a[6]; rop=a[7];
    if ((int32_t)width<=0 || (int32_t)rows<=0) { cpu->_eax=1; return; }
    if (rop!=0x00CC0020u && rop!=0x00F00021u) fail73("ROP",rop,0);
    {
        int fast=(((dst|width)&3u)==0 && width<=0x280u);
        for (row=0;row<rows;row++) {
            uint32_t d=dst, s=src;
            if (!proc_range(d,width,1)) {
                log_s("[storm-blt] dst/width/row="); log_hex(d); log_hex(width); log_hex(row);
                fail73("destination surface non scrivibile",rop,0);
            }
            if (rop==0x00CC0020u && !proc_range(s,width,0)) {
                log_s("[storm-blt] src/width/row="); log_hex(s); log_hex(width); log_hex(row);
                fail73("source surface non leggibile",rop,0);
            }
            if (fast) {
                for (col=0;col<width;col+=4) {
                    if (rop==0x00F00021u) *(volatile uint32_t *)(unsigned long)d=pattern;
                    else {
                        uint32_t pixel;
                        memcpy(&pixel,(const void *)(unsigned long)s,4);
                        memcpy((void *)(unsigned long)d,&pixel,4);
                        s+=4;
                    }
                    d+=4;
                }
            } else {
                for (col=0;col<width;col++) {
                    if (rop==0x00F00021u)
                        *(volatile uint8_t *)(unsigned long)(d+col)=(uint8_t)(pattern>>((col&3u)*8u));
                    else
                        *(volatile uint8_t *)(unsigned long)(d+col)=
                            *(const volatile uint8_t *)(unsigned long)(s+col);
                }
            }
            if (row+1u<rows) {
                int64_t nd=(int64_t)(uint64_t)dst+(int32_t)dst_pitch;
                if (nd<0 || nd>0xFFFFFFFFLL) fail73("destination row stride overflow",rop,dst);
                dst=(uint32_t)nd;
                if (rop==0x00CC0020u) {
                    int64_t ns=(int64_t)(uint64_t)src+(int32_t)src_pitch;
                    if (ns<0 || ns>0xFFFFFFFFLL) fail73("source row stride overflow",rop,src);
                    src=(uint32_t)ns;
                }
            }
        }
    }
    cpu->_eax=1;
}

/* Minimal interpreter for the x86 SCode emitted by Storm. All memory spans are
 * validated once before execution; unsupported encodings fail closed with PC+bytes. */
static uint8_t rd8(uint32_t p);
static void scode_fail(uint32_t pc, const uint8_t *b) {
    log_s("[storm-scode] unsupported x86 at "); log_hex(pc);
    log_s("[storm-scode] bytes=");
    { unsigned i; for(i=0;i<8;i++) log_hex(b[i]); }
    log_s("[storm-scode] dump256=");
    { unsigned i; for(i=0;i<256u && proc_range(pc+i,1,0);i++) log_hex(rd8(pc+i)); }
    _exit(73);
}
static uint8_t rd8(uint32_t p) { return *(volatile uint8_t *)(unsigned long)p; }
static uint8_t reg8_get(uint32_t eax,uint32_t ecx,uint32_t edx,uint32_t ebx,unsigned r) {
    uint32_t v;
    switch (r&7u) {
    case 0: v=eax; break; case 1: v=ecx; break; case 2: v=edx; break; case 3: v=ebx; break;
    case 4: v=eax>>8; break; case 5: v=ecx>>8; break; case 6: v=edx>>8; break; default: v=ebx>>8; break;
    }
    return (uint8_t)v;
}
static void reg8_set(uint32_t *eax,uint32_t *ecx,uint32_t *edx,uint32_t *ebx,unsigned r,uint8_t v) {
    uint32_t *p,shift,mask;
    switch (r&7u) {
    case 0: p=eax; shift=0; break; case 1: p=ecx; shift=0; break;
    case 2: p=edx; shift=0; break; case 3: p=ebx; shift=0; break;
    case 4: p=eax; shift=8; break; case 5: p=ecx; shift=8; break;
    case 6: p=edx; shift=8; break; default: p=ebx; shift=8; break;
    }
    mask=0xFFu<<shift; *p=(*p&~mask)|((uint32_t)v<<shift);
}
static uint32_t rd32(uint32_t p) { uint32_t v; memcpy(&v,(const void *)(unsigned long)p,4); return v; }
static void wr16(uint32_t p,uint16_t v) { memcpy((void *)(unsigned long)p,&v,2); }
static void wr32(uint32_t p,uint32_t v) { memcpy((void *)(unsigned long)p,&v,4); }
/* Validate SCode data operands at use: a direct-copy kernel need not provide a LUT, while an
 * actual table read with a null/unmapped base must still fail closed. */
static uint8_t scode_rd8(uint32_t p) {
    if (!proc_range(p,1,0)) fail73("SCode byte read range",0,p);
    return rd8(p);
}
static uint32_t scode_rd32(uint32_t p) {
    uint32_t v;
    if (!proc_range(p,4,0)) fail73("SCode dword read range",0,p);
    memcpy(&v,(const void *)(unsigned long)p,4); return v;
}
static void scode_wr8(uint32_t p,uint8_t v) {
    if (!proc_range(p,1,1)) fail73("SCode byte write range",0,p);
    *(volatile uint8_t *)(unsigned long)p=v;
}
static void scode_wr16(uint32_t p,uint16_t v) {
    if (!proc_range(p,2,1)) fail73("SCode word write range",0,p);
    wr16(p,v);
}
static void scode_wr32(uint32_t p,uint32_t v) {
    if (!proc_range(p,4,1)) fail73("SCode dword write range",0,p);
    wr32(p,v);
}
static void run_scode(_cpu *cpu,uint32_t entry,uint32_t code1,uint32_t size1,uint32_t code2,uint32_t size2,
                      uint32_t code3,uint32_t size3,uint32_t a2,uint32_t dst_stride,uint32_t src_stride) {
    uint32_t eax=0,ebx=entry,ecx=rd32(a2+12),edx=cpu->_edx;
    uint32_t esi=rd32(a2+28),edi=rd32(a2+24),ebp=rd32(a2+32),pc=entry;
    uint32_t rows=ecx, steps=0;
    /* Host stack template: [entry][rows][a2][src stride][dst stride]. */
    (void)size1; (void)size2; (void)size3;
    while (rows) {
        unsigned budget=1000000;
        while (budget--) {
            uint8_t op; uint32_t target;
            if (!((pc>=code1 && pc<code1+size1)||(pc>=code2 && pc<code2+size2)||
                  (pc>=code3 && pc<code3+size3))) {
                log_s("[storm-scode] PC fuori buffer="); log_hex(pc);
                fail73("SCode PC escaped validated buffers",0,pc);
            }
            op=rd8(pc);
            if (op==0xC7 && rd8(pc+1)==0xC0) { eax=rd32(pc+2); pc+=6; }
            else if (op==0x8A && rd8(pc+1)==0xC4) { eax=(eax&0xFFFFFF00u)|((eax>>8)&0xFFu); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xE0) { reg8_set(&eax,&ecx,&edx,&ebx,4,reg8_get(eax,ecx,edx,ebx,0)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x26) { eax=(eax&0xFFFF00FFu)|((uint32_t)scode_rd8(esi)<<8); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x06) { eax=(eax&0xFFFFFF00u)|scode_rd8(esi); pc+=2; }
            else if (op==0xFF && rd8(pc+1)==0xC6) { ++esi; pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x04 && rd8(pc+2)==0x28) { eax=(eax&0xFFFFFF00u)|scode_rd8(eax+ebp); pc+=3; }
            else if (op==0x66 && rd8(pc+1)==0x89 && rd8(pc+2)==0x07) { scode_wr16(edi,(uint16_t)eax); pc+=3; }
            else if (op==0x81 && rd8(pc+1)==0xC7) { edi+=rd32(pc+2); pc+=6; }
            else if (op==0x89 && rd8(pc+1)==0x0F) { scode_wr32(edi,ecx); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x0F) { ecx=scode_rd32(edi); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x16) { edx=scode_rd32(esi); pc+=2; }
            /* Speculari a 8B 0F / 8B 16: caricamento a 32 bit dai due puntatori (8B /r, mod=00).
             * Misurati il 21/11/2026 sul kernel LUT a 4 byte per passo generato da Storm a
             * 0x20176A5A (StarCraft x86, dopo la conferma del profilo): il blocco apre con
             * 8B 0E 8B 17 8A C1 8A E2 81 C6 04000000 ... e chiude con 89 1F 81 C7 04000000. */
            else if (op==0x8B && rd8(pc+1)==0x0E) { ecx=scode_rd32(esi); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x17) { edx=scode_rd32(edi); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x0F) { reg8_set(&eax,&ecx,&edx,&ebx,1,scode_rd8(edi)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x16) { reg8_set(&eax,&ecx,&edx,&ebx,2,scode_rd8(esi)); pc+=2; }
            /* Variante speculare dello stesso kernel LUT generato da Storm: legge il byte
             * sorgente in CL e quello di destinazione in DL (x86: 8A /r, mod=00). Misurata il
             * 21/11/2026 su StarCraft x86: SCode a 0x2016011A generato per il blit 320x320
             * (size1=0x4A, tre blocchi da 23 byte) iniziato con 8A 0E 8A 17 8A C1 8A E2
             * FF C6 8A 04 28 88 07 FF C7 C7 C0 00000000: senza 8A 0E / 8A 17 l'interprete
             * usciva fail-closed su un kernel perfettamente legale. */
            else if (op==0x8A && rd8(pc+1)==0x0E) { reg8_set(&eax,&ecx,&edx,&ebx,1,scode_rd8(esi)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x17) { reg8_set(&eax,&ecx,&edx,&ebx,2,scode_rd8(edi)); pc+=2; }
            else if (op==0x88 && rd8(pc+1)==0x07) { scode_wr8(edi,reg8_get(eax,ecx,edx,ebx,0)); pc+=2; }
            else if (op==0xFF && rd8(pc+1)==0xC7) { ++edi; pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xC1) { reg8_set(&eax,&ecx,&edx,&ebx,0,reg8_get(eax,ecx,edx,ebx,1)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xE2) { reg8_set(&eax,&ecx,&edx,&ebx,4,reg8_get(eax,ecx,edx,ebx,2)); pc+=2; }
            else if (op==0x81 && rd8(pc+1)==0xC6) { esi+=rd32(pc+2); pc+=6; }
            else if (op==0x8A && rd8(pc+1)==0x1C && rd8(pc+2)==0x28) { reg8_set(&eax,&ecx,&edx,&ebx,3,scode_rd8(eax+ebp)); pc+=3; }
            else if (op==0x8A && rd8(pc+1)==0xC5) { reg8_set(&eax,&ecx,&edx,&ebx,0,reg8_get(eax,ecx,edx,ebx,5)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xE6) { reg8_set(&eax,&ecx,&edx,&ebx,4,reg8_get(eax,ecx,edx,ebx,6)); pc+=2; }
            else if (op==0xC1 && (rd8(pc+1)==0xC9 || rd8(pc+1)==0xCA || rd8(pc+1)==0xCB) && rd8(pc+2)==0x10) {
                uint32_t *r=rd8(pc+1)==0xC9?&ecx:(rd8(pc+1)==0xCA?&edx:&ebx);
                *r=(*r>>16)|(*r<<16); pc+=3;
            }
            else if (op==0x8A && rd8(pc+1)==0x3C && rd8(pc+2)==0x28) { reg8_set(&eax,&ecx,&edx,&ebx,7,scode_rd8(eax+ebp)); pc+=3; }
            else if (op==0x89 && rd8(pc+1)==0x1F) { scode_wr32(edi,ebx); pc+=2; }
            else if (op==0xE9) {
                int32_t rel=(int32_t)rd32(pc+1); target=pc+5u+(uint32_t)rel;
                if (target==0x15030D30u || target==0x15030D50u) {
                    if (target==0x15030D30u) { --rows; if (!rows) break; esi+=src_stride; edi+=dst_stride; }
                    else { if (rows&1u) { --rows; if (!rows) break; esi+=rd32(a2+52); edi+=rd32(a2+48); }
                           else { --rows; esi+=src_stride; edi+=dst_stride; } }
                    pc=entry; ++steps; break;
                }
                if ((target>=code1 && target<code1+size1) || (target>=code2 && target<code2+size2) ||
                    (target>=code3 && target<code3+size3)) pc=target;
                else scode_fail(pc,(const uint8_t *)(unsigned long)pc);
            } else scode_fail(pc,(const uint8_t *)(unsigned long)pc);
        }
        if (!budget && rows) fail73("SCode instruction budget",0,steps);
    }
    cpu->_eax=1; cpu->_ecx=ecx; cpu->_edx=edx; cpu->_ebx=ebx;
    wr32(a2+36,ecx); wr32(a2+40,edx); wr32(a2+44,ebx);
}

static int scode_trace_on(void) {
    static int enabled=-1;
    if (enabled<0) { const char *v=getenv("REASM_TRACE"); enabled=(v && v[0]=='1' && v[1]==0); }
    return enabled;
}
static int range_2d(uint32_t base,uint32_t rows,uint32_t rowbytes,uint32_t delta,int write_access) {
    int64_t stride=(int64_t)rowbytes+(int32_t)delta;
    int64_t last=(int64_t)(rows-1u)*stride;
    int64_t low=last<0?last:0, high=(last>0?last:0)+(int64_t)rowbytes;
    int64_t first=(int64_t)(uint64_t)base+low;
    uint64_t length=(uint64_t)(high-low);
    if (!rows || first<0 || first>0xFFFFFFFFLL || length>0xFFFFFFFFu || first+ (int64_t)length>0x100000000LL) return 0;
    return proc_range((uint32_t)first,(uint32_t)length,write_access);
}
static int match_2x_lut_kernel(uint32_t code1,uint32_t size1,uint32_t code2,uint32_t size2,
                               uint32_t width,uint32_t entry,uint32_t target,uint32_t loop_target) {
    static const uint8_t prefix[]={
        0xC7,0xC0,0,0,0,0, 0x8A,0x26, 0xFF,0xC6, 0x8A,0xC4,
        0x66,0x89,0x07, 0x81,0xC7,2,0,0,0, 0xE9
    };
    static const uint8_t op[]={0x8A,0xC4,0x8A,0x26,0xFF,0xC6,0x8A,0x04,0x28,
                               0x66,0x89,0x07,0x81,0xC7,2,0,0,0};
    uint32_t i;
    if (width!=319u || entry!=code1 || size1!=0x1Au || size2!=0x1685u ||
        target!=code2+18u || loop_target!=0x15030D30u) return 0;
    if (memcmp((const void *)(unsigned long)code1,prefix,sizeof prefix)!=0) return 0;
    if (rd8(code2+0x1680u)!=0xE9) return 0;
    for(i=0;i<320u;i++) if (memcmp((const void *)(unsigned long)(code2+i*18u),op,sizeof op)!=0) return 0;
    return 1;
}
/* C translation of the fully fingerprinted 2x byte->word LUT SCode above.
 * It is gated by every generated operation, both lengths, width, and patched continuation. */
static void run_2x_lut(_cpu *cpu,uint32_t entry,uint32_t a2,uint32_t rows,uint32_t dst_delta,uint32_t src_delta) {
    uint32_t row,ebx=entry,ecx=rows,edx=cpu->_edx;
    uint32_t dst=rd32(a2+24),src=rd32(a2+28),lutbase=rd32(a2+32);
    const uint8_t *lut=(const uint8_t *)(unsigned long)lutbase;
    for(row=0;row<rows;row++) {
        const uint8_t *s=(const uint8_t *)(unsigned long)src;
        uint8_t *d=(uint8_t *)(unsigned long)dst;
        uint8_t prev=s[0]; uint32_t i;
        d[0]=prev; d[1]=prev;
        for(i=1;i<320u;i++) {
            uint8_t cur=s[i];
            uint16_t ax=(uint16_t)(((uint16_t)cur<<8)|lut[((uint32_t)cur<<8)|prev]);
            d[2u*i]=(uint8_t)ax; d[2u*i+1u]=(uint8_t)(ax>>8);
            prev=cur;
        }
        src+=320u+src_delta; dst+=640u+dst_delta;
    }
    cpu->_eax=1; cpu->_ecx=ecx; cpu->_edx=edx; cpu->_ebx=ebx;
    wr32(a2+36,ecx); wr32(a2+40,edx); wr32(a2+44,ebx);
}

void storm_334_arm_body(_cpu *cpu) {
    uint32_t a[2],p,code1,code2,code3,size1,size2,size3,width,rows,dst,src,dst_delta,src_delta,base,flags,ret;
    ret=args_and_relocate_return(cpu,2,a); (void)ret;
    p=a[1];
    if (!proc_range(a[0],0x4c,0) || !proc_range(p,0x38,1)) fail73("storm_334 descriptors",0,0);
    flags=rd32(p); width=rd32(p+8); rows=rd32(p+12);
    dst_delta=rd32(p+16); src_delta=rd32(p+20); dst=rd32(p+24); src=rd32(p+28);
    code1=rd32(a[0]+20); size1=rd32(a[0]+24); code3=rd32(a[0]+44); size3=rd32(a[0]+48);
    code2=rd32(a[0]+68); size2=rd32(a[0]+72);
    if (!width || !rows) { cpu->_eax=1; return; }
    if (flags!=0x40u) fail73("storm_334 flags",0,flags);
    if (!size1 || !size2 || !proc_range(code1,size1,1) || !proc_range(code2,size2,1)) {
        log_s("[storm-scode] code1/size1="); log_hex(code1); log_hex(size1);
        log_s("[storm-scode] code2/size2="); log_hex(code2); log_hex(size2);
        fail73("SCode buffers",0,flags);
    }
    if (!proc_range(dst,(rows-1u)*(width+dst_delta)+width,1) ||
        !proc_range(src,(rows-1u)*(width+src_delta)+width,0)) fail73("surface range",0,flags);
    base=rd32(p+32);
    /* Opt-in diagnostic capture for the reported hover kernel; preserve exact bytes
     * before storm_334 patches its generated jump slots. */
    if (getenv("SC_SCODE_DUMP") && size1==0x4Au && size2==0x2BC5u) {
        uint32_t off;
        log_s("[storm-scode-dump] width/rows/flags="); log_hex(width); log_hex(rows); log_hex(flags);
        log_s("[storm-scode-dump] src/dst/base="); log_hex(src); log_hex(dst); log_hex(base);
        log_s("[storm-scode-dump] code1=");
        for (off=0;off<size1;off++) log_hex(rd8(code1+off));
        log_s("[storm-scode-dump] code2=");
        for (off=0;off<size2;off++) log_hex(rd8(code2+off));
        log_s("\n");
    }
    /* Not every generated SCode kernel uses a LUT. Direct byte-to-word pixel-copy kernels
     * (including the observed 60x56 boot-camp blit) have a null base; table loads are checked
     * by scode_rd8 at their actual operand address. The fingerprinted 2x-LUT path validates
     * its complete 64 KiB table immediately before executing. */
    /* Patch the selected generated kernel exactly as storm_334 does (IDA
     * 0x15030AD4-0x15030CEA), including its alignment/prefix tables. */
    {
      uint32_t descriptor_flags=rd32(a[0]+8), old_width=rd32(a[0]+12);
      uint32_t stub=(descriptor_flags&0x04000000u)?0x15030D50u:0x15030D30u;
      uint32_t selected=old_width;
      if (descriptor_flags&0x00040000u) {
        uint32_t align=rd32(p+24)&3u, prefix=(4u-align)&3u, remain,q,r;
        uint64_t max_index, bytes;
        selected=(4u*width)^align;
        if (old_width!=selected) {
          uint32_t target1=0, target2;
          if (prefix>width) prefix=width;
          remain=width-prefix; q=remain>>2; r=remain&3u;
          max_index=q+19u;
          if (prefix+7u>max_index) max_index=prefix+7u;
          if (r+13u>max_index) max_index=r+13u;
          bytes=((uint64_t)max_index+1u)*4u;
          if (bytes>0xFFFFFFFFu || !proc_range(a[0],(uint32_t)bytes,1))
            fail73("aligned SCode entry tables",0,(uint32_t)max_index);
          if (size1<4u || size2<4u) fail73("aligned SCode jump slots",0,descriptor_flags);
          if (prefix || r) {
            uint32_t entry1=rd32(a[0]+(prefix+7u)*4u);
            target1=rd32(a[0]+(q+19u)*4u);
            target2=rd32(a[0]+(r+13u)*4u);
            if (size3<4u || !proc_range(code3,size3,1)) fail73("aligned SCode prefix buffer",0,size3);
            wr32(a[0]+16u,entry1);
            wr32(code1+size1-4u,target1-(code1+size1));
            wr32(code2+size2-4u,target2-(code2+size2));
            wr32(code3+size3-4u,stub-(code3+size3));
          } else {
            uint32_t entry2=rd32(a[0]+(q+19u)*4u);
            wr32(a[0]+16u,entry2);
            wr32(code2+size2-4u,stub-(code2+size2));
          }
          wr32(a[0]+12u,selected);
        }
      } else if (old_width!=width) {
        uint32_t target;
        wr32(a[0]+12u,width); wr32(a[0]+16u,code1);
        if (!proc_range(a[0]+(width+19u)*4u,4,0)) fail73("SCode width entry",0,width);
        target=rd32(a[0]+(width+19u)*4u);
        wr32(code1+size1-4u,target-(code1+size1));
        wr32(code2+size2-4u,stub-(code2+size2));
      }
    }
    /* a1[4] is the patched entry selected by storm_334; it is captured in a1+16. */
    { uint32_t entry=rd32(a[0]+16), target=code1+size1+(int32_t)rd32(code1+size1-4u);
      if (!((entry>=code1 && entry<code1+size1)||(entry>=code2 && entry<code2+size2)))
          fail73("SCode entry outside code buffers",0,entry);
      if (scode_trace_on()) { log_s("[storm-scode] entry="); log_hex(entry); }
      { uint32_t target2=code2+size2+(int32_t)rd32(code2+size2-4u);
      if (match_2x_lut_kernel(code1,size1,code2,size2,width,entry,target,target2)) {
          static int announced;
          if (!proc_range(base,65536,0)) fail73("fingerprinted 2x SCode LUT",0,base);
          if (!announced) { announced=1; log_s("[storm-scode] fast-path C 2x LUT attivo\n"); }
          if (!range_2d(dst,rows,640u,dst_delta,1) || !range_2d(src,rows,320u,src_delta,0))
              fail73("2x LUT surface span",0,flags);
          run_2x_lut(cpu,entry,p,rows,dst_delta,src_delta);
      } else {
          static int fallback_announced;
          if (!fallback_announced) {
              fallback_announced=1;
              log_s("[storm-scode] kernel fuori fingerprint 2x: interpreter (size1/size2/width/flags)=");
              log_hex(size1); log_hex(size2); log_hex(width); log_hex(rd32(a[0]+8));
          }
          run_scode(cpu,entry,code1,size1,code2,size2,code3,size3,p,dst_delta,src_delta);
      }
      }
    }
}
