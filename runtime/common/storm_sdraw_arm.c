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

static int scode_trace_on(void);

void storm_313_arm_body(_cpu *cpu) {
    uint32_t a[8], dst, src, width, rows, dst_pitch, src_pitch, pattern, rop;
    uint32_t row, col, guest_ret;
    guest_ret=args_and_relocate_return(cpu,8,a);
    dst=a[0]; src=a[1]; width=a[2]; rows=a[3];
    dst_pitch=a[4]; src_pitch=a[5]; pattern=a[6]; rop=a[7];
    if (scode_trace_on()) {
        extern unsigned char *host_elf_canvas(int *,int *,int *);
        int cw=0,ch=0,cp=0;
        unsigned char *canvas=host_elf_canvas(&cw,&ch,&cp);
        uint64_t cb=(uint32_t)(unsigned long)canvas;
        if (canvas && dst>=cb && dst<cb+(uint64_t)(unsigned int)cp*(unsigned int)ch) {
            static unsigned int n;
            if (n++<32u) {
                log_s("[storm-313-canvas] ret/dst/src="); log_hex(guest_ret); log_hex(dst); log_hex(src);
                log_s(" width/rows/dst_pitch/src_pitch="); log_hex(width); log_hex(rows);
                log_hex(dst_pitch); log_hex(src_pitch);
                log_s(" pattern/rop="); log_hex(pattern); log_hex(rop);
            }
        }
    }
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
/* A per-invocation, prevalidated contiguous span makes the common operand check
 * lock-free. An operand outside the span keeps the original proc_range fallback. */
typedef struct { uint32_t lo, hi; int valid; } ScSpan;
static int span_contains(const ScSpan *s,uint32_t p,uint32_t n) {
    uint64_t end=(uint64_t)p+n;
    return s && s->valid && p>=s->lo && end<=s->hi;
}
static int spans_overlap(const ScSpan *a,const ScSpan *b) {
    return a && b && a->valid && b->valid && a->lo<b->hi && b->lo<a->hi;
}
static uint8_t scode_rd8(uint32_t p,const ScSpan *s) {
    if (!span_contains(s,p,1) && !proc_range(p,1,0)) fail73("SCode byte read range",0,p);
    return rd8(p);
}
static uint32_t scode_rd32(uint32_t p,const ScSpan *s) {
    uint32_t v;
    if (!span_contains(s,p,4) && !proc_range(p,4,0)) fail73("SCode dword read range",0,p);
    memcpy(&v,(const void *)(unsigned long)p,4); return v;
}
static void scode_wr8(uint32_t p,uint8_t v,const ScSpan *s) {
    if (!span_contains(s,p,1) && !proc_range(p,1,1)) fail73("SCode byte write range",0,p);
    *(volatile uint8_t *)(unsigned long)p=v;
}
static void scode_wr16(uint32_t p,uint16_t v,const ScSpan *s) {
    if (!span_contains(s,p,2) && !proc_range(p,2,1)) fail73("SCode word write range",0,p);
    wr16(p,v);
}
static void scode_wr32(uint32_t p,uint32_t v,const ScSpan *s) {
    if (!span_contains(s,p,4) && !proc_range(p,4,1)) fail73("SCode dword write range",0,p);
    wr32(p,v);
}
static void run_scode(_cpu *cpu,uint32_t entry,uint32_t code1,uint32_t size1,uint32_t code2,uint32_t size2,
                      uint32_t code3,uint32_t size3,uint32_t a2,uint32_t dst_stride,uint32_t src_stride,
                      const ScSpan *src_span,const ScSpan *dst_span,const ScSpan *base_span) {
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
            else if (op==0x8A && rd8(pc+1)==0x26) { eax=(eax&0xFFFF00FFu)|((uint32_t)scode_rd8(esi,src_span)<<8); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x06) { eax=(eax&0xFFFFFF00u)|scode_rd8(esi,src_span); pc+=2; }
            else if (op==0xFF && rd8(pc+1)==0xC6) { ++esi; pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x04 && rd8(pc+2)==0x28) { eax=(eax&0xFFFFFF00u)|scode_rd8(eax+ebp,base_span); pc+=3; }
            else if (op==0x66 && rd8(pc+1)==0x89 && rd8(pc+2)==0x07) { scode_wr16(edi,(uint16_t)eax,dst_span); pc+=3; }
            else if (op==0x81 && rd8(pc+1)==0xC7) { edi+=rd32(pc+2); pc+=6; }
            else if (op==0x89 && rd8(pc+1)==0x0F) { scode_wr32(edi,ecx,dst_span); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x0F) { ecx=scode_rd32(edi,dst_span); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x16) { edx=scode_rd32(esi,src_span); pc+=2; }
            /* Speculari a 8B 0F / 8B 16: caricamento a 32 bit dai due puntatori (8B /r, mod=00).
             * Misurati il 21/11/2026 sul kernel LUT a 4 byte per passo generato da Storm a
             * 0x20176A5A (StarCraft x86, dopo la conferma del profilo): il blocco apre con
             * 8B 0E 8B 17 8A C1 8A E2 81 C6 04000000 ... e chiude con 89 1F 81 C7 04000000. */
            else if (op==0x8B && rd8(pc+1)==0x0E) { ecx=scode_rd32(esi,src_span); pc+=2; }
            else if (op==0x8B && rd8(pc+1)==0x17) { edx=scode_rd32(edi,dst_span); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x0F) { reg8_set(&eax,&ecx,&edx,&ebx,1,scode_rd8(edi,dst_span)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x16) { reg8_set(&eax,&ecx,&edx,&ebx,2,scode_rd8(esi,src_span)); pc+=2; }
            /* Variante speculare dello stesso kernel LUT generato da Storm: legge il byte
             * sorgente in CL e quello di destinazione in DL (x86: 8A /r, mod=00). Misurata il
             * 21/11/2026 su StarCraft x86: SCode a 0x2016011A generato per il blit 320x320
             * (size1=0x4A, tre blocchi da 23 byte) iniziato con 8A 0E 8A 17 8A C1 8A E2
             * FF C6 8A 04 28 88 07 FF C7 C7 C0 00000000: senza 8A 0E / 8A 17 l'interprete
             * usciva fail-closed su un kernel perfettamente legale. */
            else if (op==0x8A && rd8(pc+1)==0x0E) { reg8_set(&eax,&ecx,&edx,&ebx,1,scode_rd8(esi,src_span)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0x17) { reg8_set(&eax,&ecx,&edx,&ebx,2,scode_rd8(edi,dst_span)); pc+=2; }
            else if (op==0x88 && rd8(pc+1)==0x07) { scode_wr8(edi,reg8_get(eax,ecx,edx,ebx,0),dst_span); pc+=2; }
            else if (op==0xFF && rd8(pc+1)==0xC7) { ++edi; pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xC1) { reg8_set(&eax,&ecx,&edx,&ebx,0,reg8_get(eax,ecx,edx,ebx,1)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xE2) { reg8_set(&eax,&ecx,&edx,&ebx,4,reg8_get(eax,ecx,edx,ebx,2)); pc+=2; }
            else if (op==0x81 && rd8(pc+1)==0xC6) { esi+=rd32(pc+2); pc+=6; }
            else if (op==0x8A && rd8(pc+1)==0x1C && rd8(pc+2)==0x28) { reg8_set(&eax,&ecx,&edx,&ebx,3,scode_rd8(eax+ebp,base_span)); pc+=3; }
            else if (op==0x8A && rd8(pc+1)==0xC5) { reg8_set(&eax,&ecx,&edx,&ebx,0,reg8_get(eax,ecx,edx,ebx,5)); pc+=2; }
            else if (op==0x8A && rd8(pc+1)==0xE6) { reg8_set(&eax,&ecx,&edx,&ebx,4,reg8_get(eax,ecx,edx,ebx,6)); pc+=2; }
            else if (op==0xC1 && (rd8(pc+1)==0xC9 || rd8(pc+1)==0xCA || rd8(pc+1)==0xCB) && rd8(pc+2)==0x10) {
                uint32_t *r=rd8(pc+1)==0xC9?&ecx:(rd8(pc+1)==0xCA?&edx:&ebx);
                *r=(*r>>16)|(*r<<16); pc+=3;
            }
            else if (op==0x8A && rd8(pc+1)==0x3C && rd8(pc+2)==0x28) { reg8_set(&eax,&ecx,&edx,&ebx,7,scode_rd8(eax+ebp,base_span)); pc+=3; }
            else if (op==0x89 && rd8(pc+1)==0x1F) { scode_wr32(edi,ebx,dst_span); pc+=2; }
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
/* Validate the bounding span of every row touched by the selected Storm stub.
 * D50 alternates its ordinary delta and the absolute interlaced pitch fields. */
static int validate_rows(uint32_t base,uint32_t rows,uint32_t width,uint32_t delta,
                         uint32_t alternate_delta,int interlaced,int write_access,ScSpan *out) {
    uint32_t r,addr=base; uint64_t low=UINT64_MAX,high=0;
    if (!rows || !width) return 0;
    for (r=0;r<rows;r++) {
        uint64_t end=(uint64_t)addr+width;
        if (end>0x100000000ull) return 0;
        if (addr<low) low=addr;
        if (end>high) high=end;
        if (r+1u<rows) {
            int64_t step;
            if (interlaced && ((rows-r)&1u)) step=(int64_t)width+(int32_t)alternate_delta;
            else step=(int64_t)width+(int32_t)delta;
            { int64_t next=(int64_t)(uint64_t)addr+step;
              if(next<0 || next>0xFFFFFFFFLL) return 0;
              addr=(uint32_t)next; }
        }
    }
    if (high<=low || high-low>0xFFFFFFFFull || !proc_range((uint32_t)low,(uint32_t)(high-low),write_access)) return 0;
    if (out) { out->lo=(uint32_t)low; out->hi=(uint32_t)high; out->valid=1; }
    return 1;
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
/* Exact 1x byte-LUT kernel seen in scode-0x4a-0x2bc5.hex (plus its mirror
 * 8A 0E 8A 17 / 8B 0E 8B 17). Covers both unaligned (prefix||r) and aligned
 * (prefix==0 && r==0, where storm_334 enters code2 directly). */
static int match_1x_lut_kernel(uint32_t flags,uint32_t descriptor_flags,uint32_t base,
                               uint32_t code1,uint32_t size1,uint32_t code2,uint32_t size2,
                               uint32_t code3,uint32_t size3,uint32_t entry,
                               uint32_t target1,uint32_t target2,uint32_t target3,uint32_t width,uint32_t dst) {
    static const uint8_t pixel_a[23]={
        0xC7,0xC0,0,0,0,0, 0x8A,0x0F, 0x8A,0x16, 0x8A,0xC1, 0x8A,0xE2,
        0xFF,0xC6, 0x8A,0x04,0x28, 0x88,0x07, 0xFF,0xC7
    };
    static const uint8_t pixel_b[23]={
        0xC7,0xC0,0,0,0,0, 0x8A,0x0E, 0x8A,0x17, 0x8A,0xC1, 0x8A,0xE2,
        0xFF,0xC6, 0x8A,0x04,0x28, 0x88,0x07, 0xFF,0xC7
    };
    static const uint8_t block_a[70]={
        0xC7,0xC0,0,0,0,0, 0x8B,0x0F,0x8B,0x16,0x8A,0xC1,0x8A,0xE2,
        0x81,0xC6,4,0,0,0, 0x8A,0x1C,0x28,0x8A,0xC5,0x8A,0xE6,0xC1,0xC9,0x10,
        0x8A,0x3C,0x28,0x8A,0xC1,0xC1,0xCA,0x10,0xC1,0xCB,0x10,
        0x8A,0xE2,0x8A,0x1C,0x28,0x8A,0xC5,0xC1,0xC9,0x10,0x8A,0xE6,
        0xC1,0xCA,0x10,0x8A,0x3C,0x28,0xC1,0xCB,0x10,0x89,0x1F,
        0x81,0xC7,4,0,0,0
    };
    static const uint8_t block_b[70]={
        0xC7,0xC0,0,0,0,0, 0x8B,0x0E,0x8B,0x17,0x8A,0xC1,0x8A,0xE2,
        0x81,0xC6,4,0,0,0, 0x8A,0x1C,0x28,0x8A,0xC5,0x8A,0xE6,0xC1,0xC9,0x10,
        0x8A,0x3C,0x28,0x8A,0xC1,0xC1,0xCA,0x10,0xC1,0xCB,0x10,
        0x8A,0xE2,0x8A,0x1C,0x28,0x8A,0xC5,0xC1,0xC9,0x10,0x8A,0xE6,
        0xC1,0xCA,0x10,0x8A,0x3C,0x28,0xC1,0xCB,0x10,0x89,0x1F,
        0x81,0xC7,4,0,0,0
    };
    uint32_t i,prefix,q,r,expected_entry,expected_body,expected_tail;
    if (flags!=0x40u || !base || (descriptor_flags&0x04000000u) ||
        !(descriptor_flags&0x00040000u) || size1!=0x4Au || size2!=0x2BC5u ||
        !width || width>640u) return 0;
    prefix=(4u-(dst&3u))&3u;
    if(prefix>width) prefix=width;
    q=(width-prefix)>>2; r=(width-prefix)&3u;
    if(q>160u) return 0;
    expected_body=code2+(160u-q)*70u;
    if(!prefix && !r && entry==expected_body && target2==0x15030D30u) {
        if(!proc_range(code2,size2,0)) return 0;
    } else {
        if(size3!=0x4Au || target3!=0x15030D30u) return 0;
        expected_entry=code1+(3u-prefix)*23u;
        expected_tail=code3+(3u-r)*23u;
        if(entry!=expected_entry || target1!=expected_body || target2!=expected_tail) return 0;
        if(!proc_range(code1,size1,0) || !proc_range(code2,size2,0) || !proc_range(code3,size3,0)) return 0;
        if(rd8(code1+69u)!=0xE9 || rd8(code3+69u)!=0xE9) return 0;
        if(code1+size1+(int32_t)rd32(code1+70u)!=target1) return 0;
        if(code3+size3+(int32_t)rd32(code3+size3-4u)!=target3) return 0;
    }
    if(rd8(code2+11200u)!=0xE9) return 0;
    if(code2+size2+(int32_t)rd32(code2+size2-4u)!=target2) return 0;
    { const uint8_t *px_tmpl=0, *blk_tmpl=0;
      if(memcmp((const void *)(unsigned long)code2,block_a,sizeof block_a)==0) {
          blk_tmpl=block_a; px_tmpl=pixel_a;
      } else if(memcmp((const void *)(unsigned long)code2,block_b,sizeof block_b)==0) {
          blk_tmpl=block_b; px_tmpl=pixel_b;
      } else return 0;
      for(i=1u;i<160u;i++) {
          if(memcmp((const void *)(unsigned long)(code2+i*70u),blk_tmpl,70u)!=0) return 0;
      }
      if(entry!=expected_body) {
          for(i=0;i<3u;i++) {
              if(memcmp((const void *)(unsigned long)(code1+i*23u),px_tmpl,23u)!=0) return 0;
              if(memcmp((const void *)(unsigned long)(code3+i*23u),px_tmpl,23u)!=0) return 0;
          }
      }
      return 1;
    }
}
static uint32_t load_le32_bytes(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint8_t lut_reg_lookup(const uint8_t *lut,uint32_t eax) {
    return lut[((eax>>8)&0xFFu)*256u+(eax&0xFFu)];
}
static void run_1x_lut(_cpu *cpu,uint32_t entry,uint32_t a2,uint32_t rows,uint32_t width,
                       uint32_t dst_delta,uint32_t src_delta,uint32_t prefix) {
    uint32_t y,src=rd32(a2+28),dst=rd32(a2+24),lutbase=rd32(a2+32);
    uint32_t ebx=entry,ecx=rows,edx=cpu->_edx;
    const uint8_t *lut=(const uint8_t *)(unsigned long)lutbase;
    uint32_t q=(width-prefix)>>2;
    for(y=0;y<rows;y++) {
        const uint8_t *s=(const uint8_t *)(unsigned long)src;
        uint8_t *d=(uint8_t *)(unsigned long)dst;
        uint32_t x, last_row=(y+1u==rows);
        uint32_t last_dc=0, last_sc=0; uint8_t last_px_d=0, last_px_s=0, last_tail_d=0, last_tail_s=0;
        if(last_row) {
            if(prefix) { last_px_d=d[prefix-1u]; last_px_s=s[prefix-1u]; }
            if(q) { uint32_t qb=prefix+(q-1u)*4u; last_dc=load_le32_bytes(d+qb); last_sc=load_le32_bytes(s+qb); }
            if(prefix+q*4u<width) { last_tail_d=d[width-1u]; last_tail_s=s[width-1u]; }
        }
        for(x=0;x<prefix;x++) d[x]=lut[((uint32_t)s[x]<<8)|d[x]];
        for(;x<prefix+q*4u;x+=4u) {
            d[x+0u]=lut[((uint32_t)s[x+0u]<<8)|d[x+0u]];
            d[x+1u]=lut[((uint32_t)s[x+1u]<<8)|d[x+1u]];
            d[x+2u]=lut[((uint32_t)s[x+2u]<<8)|d[x+2u]];
            d[x+3u]=lut[((uint32_t)s[x+3u]<<8)|d[x+3u]];
        }
        for(;x<width;x++) d[x]=lut[((uint32_t)s[x]<<8)|d[x]];
        if(last_row) {
            if(prefix) { ecx=(ecx&0xFFFFFF00u)|last_px_d; edx=(edx&0xFFFFFF00u)|last_px_s; }
            if(q) {
                uint32_t qb=prefix+(q-1u)*4u;
                ebx=load_le32_bytes(d+qb); ecx=last_dc; edx=last_sc;
            }
            if(prefix+q*4u<width) { ecx=(ecx&0xFFFFFF00u)|last_tail_d; edx=(edx&0xFFFFFF00u)|last_tail_s; }
        } else { src+=width+src_delta; dst+=width+dst_delta; }
    }
    (void)lut_reg_lookup;
    cpu->_eax=1; cpu->_ecx=ecx; cpu->_edx=edx; cpu->_ebx=ebx;
    wr32(a2+36,ecx); wr32(a2+40,edx); wr32(a2+44,ebx);
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
    uint32_t descriptor_flags,stub;
    int writes_canvas=0,ticked=0;
    ScSpan src_span={0,0,0},dst_span={0,0,0},base_span={0,0,0};
    ret=args_and_relocate_return(cpu,2,a); (void)ret;
    p=a[1];
    if (!proc_range(a[0],0x4c,0) || !proc_range(p,0x38,1)) fail73("storm_334 descriptors",0,0);
    flags=rd32(p); width=rd32(p+8); rows=rd32(p+12);
    dst_delta=rd32(p+16); src_delta=rd32(p+20); dst=rd32(p+24); src=rd32(p+28);
    code1=rd32(a[0]+20); size1=rd32(a[0]+24); code3=rd32(a[0]+44); size3=rd32(a[0]+48);
    code2=rd32(a[0]+68); size2=rd32(a[0]+72);
    if (!width || !rows) { cpu->_eax=1; return; }
    /* Sequenza delle scritture sul canvas: serve a capire chi azzera il frame del filmato
     * introduttivo (su ARM il canvas ha contenuto subito dopo il blit 2x e zero al present). */
    if (scode_trace_on()) {
        extern unsigned char *host_elf_canvas(int *, int *, int *) __attribute__((weak));
        int cw=0,ch=0,cp=0; unsigned char *canv=host_elf_canvas?host_elf_canvas(&cw,&ch,&cp):0;
        uint32_t cb=(uint32_t)(unsigned long)canv;
        if (canv && cb && dst>=cb && dst<cb+(uint32_t)cp*(uint32_t)ch) {
            static uint32_t seq;
            if (seq++ < 48u) {
                log_s("[canvas-write] seq="); log_hex(seq);
                log_s(" off="); log_hex(dst-cb);
                log_s(" w/r/dd/sd="); log_hex(width); log_hex(rows); log_hex(dst_delta); log_hex(src_delta);
                log_s(" entry="); log_hex(a[0]);
                log_s((width>=640u && rows>=480u && dst==cb)?" FULL-fill\n":"\n");
            }
        }
    }
    if (flags!=0x40u) fail73("storm_334 flags",0,flags);
    if (!size1 || !size2 || !proc_range(code1,size1,1) || !proc_range(code2,size2,1)) {
        log_s("[storm-scode] code1/size1="); log_hex(code1); log_hex(size1);
        log_s("[storm-scode] code2/size2="); log_hex(code2); log_hex(size2);
        fail73("SCode buffers",0,flags);
    }
    descriptor_flags=rd32(a[0]+8);
    stub=(descriptor_flags&0x04000000u)?0x15030D50u:0x15030D30u;
    if (!validate_rows(dst,rows,width,dst_delta,rd32(p+48),
                       (descriptor_flags&0x04000000u)!=0,1,&dst_span) ||
        !validate_rows(src,rows,width,src_delta,rd32(p+52),
                       (descriptor_flags&0x04000000u)!=0,0,&src_span)) fail73("surface range",0,flags);
    {
        extern unsigned char *host_elf_canvas(int *, int *, int *);
        extern volatile unsigned int g_canvas_write_ticks;
        int cw=0,ch=0,cp=0;
        unsigned char *canvas=host_elf_canvas(&cw,&ch,&cp);
        uint64_t canvas_lo=(uint32_t)(unsigned long)canvas;
        uint64_t canvas_hi=canvas_lo+(uint64_t)(unsigned int)cp*(unsigned int)ch;
        writes_canvas=canvas && dst_span.valid && dst_span.lo<canvas_hi && dst_span.hi>canvas_lo;
    }
    base=rd32(p+32);
    if (base && proc_range(base,65536u,0)) {
        base_span.lo=base; base_span.hi=base+65536u; base_span.valid=(base_span.hi>base);
    }
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
      uint32_t old_width=rd32(a[0]+12);
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
        uint32_t target3=(size3>=4u && proc_range(code3,size3,0))?
                         code3+size3+(int32_t)rd32(code3+size3-4u):0;
      if (match_1x_lut_kernel(flags,descriptor_flags,base,code1,size1,code2,size2,code3,size3,
                              entry,target,target2,target3,width,dst) && base_span.valid) {
          if (!spans_overlap(&src_span,&dst_span) && !spans_overlap(&base_span,&dst_span) &&
              !spans_overlap(&base_span,&src_span)) {
              static int announced_1x;
              uint32_t pref=(4u-(dst&3u))&3u;
              if (pref>width) pref=width;
              if (!announced_1x) { announced_1x=1; log_s("[storm-scode] fast-path C 1x LUT attivo\n"); }
              if (scode_trace_on()) log_s("[storm-scode] fast-path C 1x LUT attivo\n");
              run_1x_lut(cpu,entry,p,rows,width,dst_delta,src_delta,pref);
          } else {
              run_scode(cpu,entry,code1,size1,code2,size2,code3,size3,p,dst_delta,src_delta,
                        &src_span,&dst_span,&base_span);
          }
      } else if (match_2x_lut_kernel(code1,size1,code2,size2,width,entry,target,target2)) {
          static int announced;
          if (!proc_range(base,65536,0)) fail73("fingerprinted 2x SCode LUT",0,base);
          if (!announced) { announced=1; log_s("[storm-scode] fast-path C 2x LUT attivo\n"); }
          if (!range_2d(dst,rows,640u,dst_delta,1) || !range_2d(src,rows,320u,src_delta,0))
              fail73("2x LUT surface span",0,flags);
          run_2x_lut(cpu,entry,p,rows,dst_delta,src_delta);
          if (writes_canvas) {
              extern volatile unsigned int g_canvas_write_ticks;
              __sync_add_and_fetch(&g_canvas_write_ticks,1u);
              ticked=1;
          }
          if (scode_trace_on()) {
              static unsigned int video_seq;
              uint8_t *canvas; int cw=0,ch=0,cp=0;
              uint32_t nz_src=0,nz_dst=0,nz_canvas=0,y,x;
              extern unsigned char *host_elf_canvas(int *,int *,int *);
              extern void host_elf_canvas_watch_arm(unsigned int) __attribute__((weak));
              extern volatile unsigned int g_canvas_write_ticks;
              for(y=0;y<rows;y++) for(x=0;x<320u;x++)
                  nz_src+=((uint8_t *)(unsigned long)src)[(int64_t)y*(320+(int32_t)src_delta)+x]!=0;
              canvas=host_elf_canvas(&cw,&ch,&cp);
              for(y=0;y<rows;y++) for(x=0;x<640u;x++)
                  nz_dst+=((uint8_t *)(unsigned long)dst)[(int64_t)y*(640+(int32_t)dst_delta)+x]!=0;
              if (canvas && cw>=640 && ch>=394 && cp>=640)
                  for(y=86u;y<394u;y++) for(x=0;x<640u;x++)
                      nz_canvas+=canvas[(unsigned long)y*cp+x]!=0;
              if (video_seq++ < 48u) {
                  log_s("[storm-scode-video] seq/tick="); log_hex(video_seq);
                  log_hex(__sync_fetch_and_add(&g_canvas_write_ticks,0u));
                  log_s(" src/dst/canvas="); log_hex(src); log_hex(dst);
                  log_hex((uint32_t)(unsigned long)canvas);
                  log_s(" nonzero src/dst/filmrect=");
                  log_hex(nz_src); log_hex(nz_dst); log_hex(nz_canvas);
              }
              if (nz_dst && host_elf_canvas_watch_arm) host_elf_canvas_watch_arm(entry);
          }
      } else {
          static int fallback_announced;
          if (!fallback_announced) {
              fallback_announced=1;
              log_s("[storm-scode] kernel fuori fingerprint 2x: interpreter (size1/size2/width/flags)=");
              log_hex(size1); log_hex(size2); log_hex(width); log_hex(rd32(a[0]+8));
          }
          run_scode(cpu,entry,code1,size1,code2,size2,code3,size3,p,dst_delta,src_delta,
                    &src_span,&dst_span,&base_span);
      }
      }
    }
    if (writes_canvas && !ticked) {
        extern volatile unsigned int g_canvas_write_ticks;
        __sync_add_and_fetch(&g_canvas_write_ticks,1u);
    }
}
