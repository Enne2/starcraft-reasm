/* T3: translation only. Delivery reuses win32_ui.c's Post/Peek/Dispatch. */
#include "input_elf.h"
#include "reasm_elf.h"
extern void *memset(void *,int,unsigned long);
extern int snprintf(char *,unsigned long,const char *,...);
extern char *getenv(const char *);
extern void *win32_shim_lookup_dll(const char *,const char *);
extern void *win32_shim_lookup(const char *);
unsigned int input_elf_virtual_key(int k) {
    unsigned int scan=(unsigned int)k&~0x40000000u;
    if(k>='a' && k<='z') return (unsigned int)(k-'a'+'A');
    if(k==127) return 46; /* SDLK_DELETE -> VK_DELETE */
    if(!((unsigned int)k&0x40000000u)) return (unsigned int)k;
    switch(scan) {
    case 79: return 39; case 80: return 37; case 81: return 40; case 82: return 38;
    case 73: return 45; case 74: return 36; case 75: return 33; case 76: return 46;
    case 77: return 35; case 78: return 34;
    case 224: case 228: return 17; case 225: case 229: return 16;
    case 226: case 230: return 18;
    case 40: case 88: return 13; case 41: return 27;
    default: if(scan>=58 && scan<=69) return 112+scan-58; return 0;
    }
}
static unsigned int mouse_wp(const struct host_input_event *e) {
    return ((e->buttons&1)?1:0)|((e->buttons&4)?2:0)|((e->buttons&2)?0x10:0)|
           ((e->modifiers&3)?4:0)|((e->modifiers&0xC0)?8:0);
}
static void msg(input_elf_message *m,unsigned int id,unsigned int wp,unsigned int lp) {
    m->message=id; m->wparam=wp; m->lparam=lp;
}
/* SDL UTF-8 -> WM_CHAR for ANSI windows (the shim ACP is Windows-1252).
 * Invalid/unsupported scalars become '?', not independent UTF-8 bytes. */
static unsigned int ansi(unsigned int cp) {
    static const unsigned short map[32]={0x20AC,0,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,
        0x2C6,0x2030,0x160,0x2039,0x152,0,0x17D,0,0,0x2018,0x2019,0x201C,0x201D,0x2022,
        0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0,0x17E,0x178};
    int i; if(cp<128 || (cp>=160 && cp<=255)) return cp;
    for(i=0;i<32;++i) if(map[i]==cp && cp) return 128+i;
    return '?';
}
int input_elf_translate(const struct host_input_event *e,input_elf_message out[32]) {
    unsigned int lp,vk; int n=0,i;
    if(!e || !out) return 0;
    lp=((unsigned int)e->x&0xFFFFu)|((unsigned int)e->y<<16);
    switch(e->type) {
    case HOST_INPUT_MOVE: msg(out,0x200,mouse_wp(e),lp); return 1;
    case HOST_INPUT_DOWN: case HOST_INPUT_UP:
        if(e->key<1 || e->key>3) return 0;
        vk=e->key==1?0x201:e->key==2?0x207:0x204;
        msg(out,vk+(e->type==HOST_INPUT_UP),mouse_wp(e),lp);
        if(getenv("REASM_TRACE")) {
            char b[160];
            snprintf(b,sizeof b,"[input-route] t=%u translate host=%s xy=%d,%d buttons=0x%X -> msg=0x%X wp=0x%X lp=0x%X\n",
                (unsigned)(host_elf_ticks ? host_elf_ticks() : 0),
                e->type==HOST_INPUT_DOWN?"DOWN":"UP",e->x,e->y,e->buttons,out[0].message,out[0].wparam,out[0].lparam);
            reasm_elf_log(b);
        }
        return 1;
    case HOST_INPUT_KEYDOWN: case HOST_INPUT_KEYUP:
        vk=input_elf_virtual_key(e->key); if(!vk) return 0;
        lp=1u|((e->modifiers&0x300)?0x20000000u:0)|
           ((e->type==HOST_INPUT_KEYUP)?0xC0000000u:(e->buttons?0x40000000u:0));
        msg(out,(e->modifiers&0x300)?(e->type==HOST_INPUT_KEYDOWN?0x104:0x105):
            (e->type==HOST_INPUT_KEYDOWN?0x100:0x101),vk,lp);
        if(e->type==HOST_INPUT_KEYDOWN && (vk==8 || vk==9 || vk==13)) {
            msg(out+1,0x102,vk,lp); return 2;
        }
        return 1;
    case HOST_INPUT_TEXT:
        for(i=0;i<32 && e->text[i];) {
            unsigned int cp=(unsigned char)e->text[i++], min=0; int extra=0,j;
            if(cp>=0xC2 && cp<=0xDF) { cp&=31; extra=1; min=0x80; }
            else if(cp>=0xE0 && cp<=0xEF) { cp&=15; extra=2; min=0x800; }
            else if(cp>=0xF0 && cp<=0xF4) { cp&=7; extra=3; min=0x10000; }
            else if(cp>=128) cp='?';
            for(j=0;j<extra;++j) {
                if(i>=32 || ((unsigned char)e->text[i]&0xC0)!=0x80) break;
                cp=(cp<<6)|((unsigned char)e->text[i++]&63);
            }
            if(j!=extra || (extra && cp<min) || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) cp='?';
            msg(out+n++,((e->modifiers&0x300) && !(e->modifiers&0xC0))?0x106:0x102,ansi(cp),1);
        }
        return n;
    case HOST_INPUT_FOCUS:
        msg(out,0x1C,e->key!=0,0); msg(out+1,6,e->key?1:0,0);
        msg(out+2,e->key?7:8,0,0); return 3;
    case HOST_INPUT_QUIT: msg(out,0x10,0,0); return 1;
    default: return 0; /* expose handled by deferred/gated paint */
    }
}

/* Opt-in integrated regression. No changes to dialog/GDI/COM-owned tests. */
#define S __attribute__((stdcall))
extern S unsigned short RegisterClassA(const void *);
extern S unsigned int CreateWindowExA(unsigned int,const char *,const char *,unsigned int,
    int,int,int,int,unsigned int,unsigned int,void *,void *);
extern S int ShowWindow(unsigned int,int);
extern S unsigned int SetFocus(unsigned int);
extern S int PeekMessageA(void *,unsigned int,unsigned int,unsigned int,unsigned int);
extern S int DispatchMessageA(const void *);
extern S int DestroyWindow(unsigned int);
extern S int GetKeyState(int);
extern S int DefWindowProcA(unsigned int,unsigned int,unsigned int,unsigned int);
static int tests,failed,seen_move,seen_down,seen_up;
static void check(const char *what,int pass) {
    char b[160]; ++tests; if(!pass) ++failed;
    snprintf(b,sizeof b,"[input-selftest] %s %s\n",pass?"PASS":"FAIL",what); reasm_elf_log(b);
}
static S int test_proc(unsigned int h,unsigned int m,unsigned int wp,unsigned int lp) {
    if(m==0x200) ++seen_move;
    if(m==0x201) { ++seen_down; check("button down state at dispatch",GetKeyState(1)<0 && (GetKeyState(1)&0x8000)); }
    if(m==0x202) { ++seen_up; check("button up state at dispatch",GetKeyState(1)==0); }
    return DefWindowProcA(h,m,wp,lp);
}
int input_elf_selftest(void) {
    struct host_input_event e; input_elf_message out[32]; unsigned int wc[10]={0},h,m[7];
    int n,i; char b[100];
    if(getenv("SC_INPUT_EXTENSION_PROBE")) {
        void *fn=win32_shim_lookup_dll("USER32.dll","T3Probe");
        check("extension lookup",fn!=0);
        check("extension case insensitive",fn && win32_shim_lookup_dll("user32.DLL","t3probe")==fn);
        check("extension DLL isolation",win32_shim_lookup_dll("KERNEL32.dll","T3Probe")==0);
        check("extension legacy name lookup",fn && win32_shim_lookup("T3Probe")==fn);
        check("extension stdcall callback",fn && ((int (S *)(void))fn)()==123);
    }
    memset(&e,0,sizeof e); e.type=HOST_INPUT_MOVE; e.x=320; e.y=190; e.buttons=5; e.modifiers=3;
    n=input_elf_translate(&e,out); check("mousemove packing",n==1 && out[0].message==0x200 && out[0].lparam==0x00BE0140 && out[0].wparam==7);
    e.type=HOST_INPUT_DOWN; e.key=1; check("left down",input_elf_translate(&e,out)==1 && out[0].message==0x201);
    e.type=HOST_INPUT_UP; check("left up",input_elf_translate(&e,out)==1 && out[0].message==0x202);
    e.key=3; check("right up",input_elf_translate(&e,out)==1 && out[0].message==0x205);
    e.key=2; check("middle up",input_elf_translate(&e,out)==1 && out[0].message==0x208);
    e.type=HOST_INPUT_KEYDOWN; e.key='a'; e.buttons=1;
    check("key/repeat",input_elf_translate(&e,out)==1 && out[0].wparam=='A' && (out[0].lparam&0x40000000));
    e.type=HOST_INPUT_KEYUP; check("keyup flags",input_elf_translate(&e,out)==1 && (out[0].lparam&0xC0000000)==0xC0000000);
    e.type=HOST_INPUT_KEYDOWN; e.key=9;
    check("shift tab and control char",input_elf_translate(&e,out)==2 && out[0].wparam==9 && out[1].message==0x102);
    e.modifiers=0x300; e.key='a'; check("alt system key",input_elf_translate(&e,out)==1 && out[0].message==0x104);
    e.type=HOST_INPUT_TEXT; e.text[0]=(char)0xC3; e.text[1]=(char)0xA9; e.text[2]=0;
    check("UTF8 composed e acute ANSI",input_elf_translate(&e,out)==1 && out[0].wparam==0xE9);
    e.text[0]=(char)0xE2; e.text[1]=(char)0x82; e.text[2]=(char)0xAC; e.text[3]=0;
    check("UTF8 euro CP1252",input_elf_translate(&e,out)==1 && out[0].wparam==0x80);
    e.type=HOST_INPUT_FOCUS; e.key=0; check("focus lost messages",input_elf_translate(&e,out)==3 && out[0].message==0x1C && out[2].message==8);
    check("SDL arrow VK",input_elf_virtual_key(0x40000050)==37);
    check("SDL ctrl VK",input_elf_virtual_key(0x400000E0)==17);
    check("host init",host_elf_init("T3 input selftest")==0);
    check("host window",host_elf_display_mode(640,480,8)==0);
    wc[1]=(unsigned int)test_proc; wc[9]=(unsigned int)"T3InputTest";
    check("register class",RegisterClassA(wc)!=0);
    h=CreateWindowExA(0,"T3InputTest","T3 input test",0x10000000,0,0,640,480,0,0,0,0);
    check("create logical window",h!=0); ShowWindow(h,5); SetFocus(h);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    memset(&e,0,sizeof e); e.type=HOST_INPUT_KEYDOWN; e.key='a';
    check("SDL push key",host_elf_test_event(&e)==1); win32_input_pump();
    check("host API held state unchanged",host_elf_key_state('a')==0x80);
    check("Win32 held key before dispatch",GetKeyState('a')<0 && (GetKeyState('a')&0x8000));
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    host_elf_delay(30); check("held key remains",GetKeyState('a')<0 && GetKeyState('A')<0);
    e.type=HOST_INPUT_KEYUP; check("SDL push keyup",host_elf_test_event(&e)==1);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    check("released key",GetKeyState('a')==0);
    e.type=HOST_INPUT_KEYDOWN; e.key=0x400000E1; e.modifiers=1; host_elf_test_event(&e);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    check("held SHIFT",GetKeyState(16)<0);
    e.type=HOST_INPUT_FOCUS; e.key=0; host_elf_test_event(&e);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    check("focus lost clears keys",GetKeyState(16)==0 && GetKeyState('a')==0);
    /* SHIFT+Tab and release in the same poll batch: Peek must expose each mask. */
    e.type=HOST_INPUT_KEYDOWN; e.key=0x400000E1; e.modifiers=1; host_elf_test_event(&e);
    e.key=9; host_elf_test_event(&e);
    e.type=HOST_INPUT_KEYUP; e.key=0x400000E1; e.modifiers=0; host_elf_test_event(&e);
    i=0;
    while(PeekMessageA(m,0,0,0,1)) {
        if(m[1]==0x100 && m[2]==9) { check("queued SHIFT Tab state",GetKeyState(16)<0); ++i; }
        DispatchMessageA(m);
    }
    check("SHIFT Tab seen once",i==1 && GetKeyState(16)==0);
    e.type=HOST_INPUT_FOCUS; e.key=1; host_elf_test_event(&e);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    e.type=HOST_INPUT_MOVE; e.x=100; e.y=120; e.buttons=0; e.modifiers=0; host_elf_test_event(&e);
    e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
    e.type=HOST_INPUT_UP; host_elf_test_event(&e);
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    check("complete click FIFO",seen_down==1 && seen_up==1 && seen_move>=1);
    e.type=HOST_INPUT_MOVE; e.x=-1; host_elf_test_event(&e); i=seen_move;
    while(PeekMessageA(m,0,0,0,1)) DispatchMessageA(m);
    check("outside window dropped",seen_move==i);
    /* A burst exceeds host FIFO capacity: backpressure must retain all events. */
    memset(&e,0,sizeof e); e.type=HOST_INPUT_KEYDOWN; e.key='x'; e.buttons=1;
    for(i=0;i<600;++i) if(host_elf_test_event(&e)!=1) break;
    check("SDL burst accepted",i==600); n=0;
    while(PeekMessageA(m,0,0,0,1)) { if(m[1]==0x100 && m[2]=='X') ++n; DispatchMessageA(m); }
    check("burst FIFO no lost repeated keys",n==600 && host_elf_input_dropped()==0);
    DestroyWindow(h); host_elf_shutdown();
    snprintf(b,sizeof b,"[input-selftest] %d/%d passed\n",tests-failed,tests); reasm_elf_log(b);
    return failed?1:0;
}
