/* Dialog manager for native ELF32: no PE execution, Wine, or fake modal return.
 * Layouts: Microsoft DLGTEMPLATE(EX)/DLGITEMTEMPLATE(EX). Fixed limits fail
 * cleanly rather than reading past a resource. Host is one canvas, so modal
 * windows/children are composed on it; HWNDs remain in win32_ui's registry.
 */
#include "dlg_elf.h"
#include "host_elf.h"
#include "gdi_elf.h"
#include "reasm_elf.h"
#define S DLG_S
extern void *malloc(unsigned long);
extern void free(void *);
extern void *memset(void *, int, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern int memcmp(const void *, const void *, unsigned long);
extern int strcmp(const char *, const char *);
extern unsigned long strlen(const char *);
extern int strcasecmp(const char *, const char *);
extern char *getenv(const char *);
extern int atoi(const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern long write(int, const void *, unsigned long);
extern int getpid(void);
extern int usleep(unsigned int);
typedef struct { void *base; unsigned long len; } IOV;
extern int open(const char *, int, ...);
extern int close(int);
extern long read(int, void *, unsigned long);
extern S void *FindResourceA(void *, const char *, const char *);
extern S void *LoadResource(void *, void *);
extern S void *LockResource(void *);
extern S unsigned int SizeofResource(void *, void *);
extern S unsigned int CreateWindowExA(unsigned int, const char *, const char *, unsigned int,
    int, int, int, int, unsigned int, unsigned int, void *, void *);
extern S int DestroyWindow(unsigned int);
extern S int IsWindow(unsigned int);
extern S int IsWindowEnabled(unsigned int);
extern S int IsWindowVisible(unsigned int);
extern S int EnableWindow(unsigned int, int);
extern S int ShowWindow(unsigned int, int);
extern S unsigned int GetFocus(void);
extern S unsigned int GetCapture(void);
extern S int GetKeyState(int);
extern S unsigned int SetFocus(unsigned int);
extern S unsigned int GetActiveWindow(void);
extern S unsigned int SetActiveWindow(unsigned int);
extern S unsigned int GetParent(unsigned int);
extern S unsigned int GetTopWindow(unsigned int);
extern S unsigned int GetWindow(unsigned int, unsigned int);
extern S unsigned int GetDlgItem(unsigned int, int);
extern S int GetDlgCtrlID(unsigned int);
extern S int GetWindowTextA(unsigned int, char *, int);
extern S int GetClassNameA(unsigned int, char *, int);
extern S int GetWindowRect(unsigned int, int *);
extern S unsigned long GetWindowLongA(unsigned int, int);
extern S unsigned long SetWindowLongA(unsigned int, int, unsigned long);
extern S int SendMessageA(unsigned int, unsigned int, unsigned int, unsigned int);
extern S int PostMessageA(unsigned int, unsigned int, unsigned int, unsigned int);
extern S void PostQuitMessage(int);
extern S int PeekMessageA(void *, unsigned int, unsigned int, unsigned int, unsigned int);
extern S int DispatchMessageA(const void *);
extern S int TranslateMessage(const void *);
extern S void SetLastError(unsigned int);

/* A build without video keeps the parser/API but cannot render a modal window. */
#pragma weak host_elf_init
#pragma weak host_elf_shutdown
#pragma weak host_elf_display_mode
#pragma weak host_elf_canvas
#pragma weak host_elf_fill8
#pragma weak host_elf_set_title
#pragma weak host_elf_set_palette
#pragma weak host_elf_get_palette
#pragma weak host_elf_present
#pragma weak host_elf_input_state
#pragma weak host_elf_test_event
#pragma weak host_elf_test_text
#pragma weak gdi_elf_init
#pragma weak gdi_elf_window_dc
#pragma weak IntersectClipRect

#define NCTL 94
#define TEXT 4096
typedef struct { unsigned int style, ex, id, atom, title_id; int x,y,w,h;
                 char cls[64], title[TEXT]; const void *extra; } Item;
typedef struct { unsigned int style, ex, count; int x,y,w,h, extended, point;
                 char title[TEXT], face[64]; Item item[NCTL]; } Template;
typedef struct { const unsigned char *base; unsigned int size, at; int bad; } Reader;
static unsigned int rd(Reader *r, unsigned int n) {
    unsigned int v=0, i;
    if (r->bad || r->at > r->size || n > r->size-r->at) { r->bad=1; return 0; }
    for (i=0;i<n;++i) v |= (unsigned int)r->base[r->at++] << (8*i);
    return v;
}
static void skip(Reader *r, unsigned int n) {
    if (r->at > r->size || n > r->size-r->at) r->bad=1;
    else r->at+=n;
}
static unsigned int field(Reader *r, char *s, unsigned int cap) {
    unsigned int c=rd(r,2), n=0;
    if (s && cap) s[0]=0;
    if (c==0xffff) return rd(r,2);
    while (c && !r->bad) {
        /* A APIs use an ANSI code page; these game resources are English. */
        if (s && n+1<cap) s[n++]=(char)(c<=255?c:'?');
        c=rd(r,2);
    }
    if (s && cap) s[n]=0;
    return 0;
}
static int parse(const void *data, unsigned int size, Template *t) {
    Reader r; unsigned int i;
    memset(t,0,sizeof *t); r.base=data; r.size=size; r.at=0; r.bad=0;
    if (!data || size<18) return 0;
    t->extended = r.base[2]==255 && r.base[3]==255;
    if(t->extended && (r.base[0]!=1 || r.base[1]!=0)) return 0;
    if (t->extended) { skip(&r,8); t->ex=rd(&r,4); t->style=rd(&r,4); }
    else { t->style=rd(&r,4); t->ex=rd(&r,4); }
    t->count=rd(&r,2);
    t->x=(short)rd(&r,2); t->y=(short)rd(&r,2); t->w=(short)rd(&r,2); t->h=(short)rd(&r,2);
    field(&r,0,0); field(&r,0,0); field(&r,t->title,TEXT);
    if (t->style&0x40) {
        t->point=rd(&r,2);
        if (t->extended) skip(&r,4); /* weight WORD, italic BYTE, charset BYTE */
        field(&r,t->face,sizeof t->face);
    }
    if (t->count>NCTL || t->w<=0 || t->h<=0) return 0;
    for (i=0;i<t->count && !r.bad;++i) {
        Item *c=&t->item[i]; unsigned int n;
        skip(&r,(-r.at)&3);
        if (t->extended) { skip(&r,4); c->ex=rd(&r,4); c->style=rd(&r,4); }
        else { c->style=rd(&r,4); c->ex=rd(&r,4); }
        c->x=(short)rd(&r,2); c->y=(short)rd(&r,2); c->w=(short)rd(&r,2); c->h=(short)rd(&r,2);
        c->id=rd(&r,t->extended?4:2);
        c->atom=field(&r,c->cls,sizeof c->cls); c->title_id=field(&r,c->title,TEXT);
        if (c->atom) {
            const char *names[]={"BUTTON","EDIT","STATIC","LISTBOX","SCROLLBAR","COMBOBOX"};
            if (c->atom<0x80 || c->atom>0x85) return 0;
            snprintf(c->cls,sizeof c->cls,"%s",names[c->atom-0x80]);
        }
        if (!c->cls[0]) return 0;
        n=rd(&r,2); c->extra=n?r.base+r.at:0;
        /* Standard creation-data size includes the size WORD; extended does not. */
        if (n) { if (!t->extended && n<2) return 0; skip(&r,t->extended?n:n-2); }
    }
    return !r.bad;
}

typedef struct { int used, done, result, dirty, bx,by; unsigned int hwnd, defid, font;
                 unsigned int child[NCTL], count, skip_char; } Dialog;
static Dialog dialogs[16];
static Dialog *dialog(unsigned int h) { int i; for(i=0;i<16;++i) if(dialogs[i].used && dialogs[i].hwnd==h) return &dialogs[i]; return 0; }
static Dialog *ancestor(unsigned int h) { int i; for(i=0;h && i<96;++i) { Dialog *d=dialog(h); if(d) return d; h=GetParent(h); } return 0; }
void dlg_elf_dirty(unsigned int h) { Dialog *d=ancestor(h); if(d) d->dirty=1; }
void dlg_elf_destroyed(unsigned int h) { Dialog *d=dialog(h); if(d) { d->done=1; d->dirty=0; } }
S int EndDialog(unsigned int h, int result) {
    Dialog *d=dialog(h); if(!d) return 0;
    d->done=1; d->result=result;
    { char b[80]; int n=snprintf(b,sizeof b,"[dlg] EndDialog hwnd=0x%X result=%d\n",h,result); write(2,b,n); }
    return 1; /* destruction is deferred until proc returns */
}
static void dialog_focus(unsigned int h,unsigned int child);
S int DefDlgProcA(unsigned int h, unsigned int msg, unsigned int wp, unsigned int lp) {
    Dialog *d=dialog(h); (void)lp;
    if(!d) return 0;
    if(msg==0x10) { SendMessageA(h,0x111,2,GetDlgItem(h,2)); return 0; }
    if(msg==0x111 && ((wp&65535)==1 || (wp&65535)==2)) { EndDialog(h,(int)(wp&65535)); return 1; }
    if(msg==0x28) { dialog_focus(h,lp?wp:GetNextDlgTabItem(h,GetFocus(),wp!=0)); return 0; }
    if(msg==0x400) return (int)(0x534b0000u|d->defid); /* DM_GETDEFID */
    if(msg==0x401) { d->defid=wp; return 1; }
    return 0;
}
S int dlg_elf_wndproc(unsigned int h, unsigned int msg, unsigned int wp, unsigned int lp) {
    Dialog *d=dialog(h); DlgProc p; int ret;
    if(!d) return 0;
    if(getenv("REASM_TRACE") && (msg==0x201 || msg==0x202 || msg==0x111)) {
        char b[200];
        snprintf(b,sizeof b,"[dlg-input] wndproc hwnd=0x%X msg=0x%X wp=0x%X lp=0x%X focus=0x%X capture=0x%X\n",
            h,msg,wp,lp,GetFocus(),GetCapture()); write(2,b,strlen(b));
    }
    SetWindowLongA(h,0,0); /* DWL_MSGRESULT */
    p=(DlgProc)GetWindowLongA(h,4); /* DWL_DLGPROC */
    ret=p?p(h,msg,wp,lp):0;
    if(ret) {
        if(msg==0x110 || msg==0x138 || msg==0x135 || msg==0x136 || msg==0x137) return ret;
        return (int)GetWindowLongA(h,0);
    }
    return DefDlgProcA(h,msg,wp,lp);
}
S int SendDlgItemMessageA(unsigned int h,int id,unsigned int m,unsigned int w,unsigned int l) { return SendMessageA(GetDlgItem(h,id),m,w,l); }
S int MapDialogRect(unsigned int h,int *r) {
    Dialog *d=dialog(h); int i;
    if(!d || !r) return 0;
    for(i=0;i<4;++i) r[i]=r[i]*(i&1?d->by:d->bx)/(i&1?8:4);
    return 1;
}
static unsigned int next(unsigned int h,unsigned int cur,int prev,int group) {
    unsigned int v[NCTL], n=0, w=GetTopWindow(h), i, start=0, end, pos;
    while(w && n<NCTL) { v[n++]=w; w=GetWindow(w,2); }
    if(!n) return 0;
    pos=prev?0:n-1;
    for(i=0;i<n;++i) if(v[i]==cur) { pos=i; break; }
    end=n;
    if(group && cur) {
        start=pos;
        while(start && !(GetWindowLongA(v[start],-16)&0x20000)) --start;
        for(end=start+1;end<n;++end) if(GetWindowLongA(v[end],-16)&0x20000) break;
    }
    for(i=0;i<end-start;++i) {
        pos=prev?(pos<=start?end-1:pos-1):(pos+1>=end?start:pos+1);
        w=v[pos];
        if(IsWindowEnabled(w) && IsWindowVisible(w) && (group || (GetWindowLongA(w,-16)&0x10000))) return w;
    }
    return 0;
}
S unsigned int GetNextDlgTabItem(unsigned int h,unsigned int c,int prev) { return next(h,c,prev,0); }
S unsigned int GetNextDlgGroupItem(unsigned int h,unsigned int c,int prev) { return next(h,c,prev,1); }
static void dialog_focus(unsigned int h,unsigned int child) {
    if(!child) return;
    SetFocus(child);
    if(SendMessageA(child,0x87,0,0)&8) SendMessageA(child,0xB1,0,0xFFFFFFFFu);
    dlg_elf_dirty(h);
}
static unsigned int fold(unsigned int k) { return k>='a' && k<='z'?k-32:k; }
static int mnemonic(unsigned int h,unsigned int key) {
    unsigned int c=GetTopWindow(h); char text[TEXT],cls[64]; int i;
    for(;c;c=GetWindow(c,2)) {
        if(!IsWindowVisible(c) || !IsWindowEnabled(c)) continue;
        GetWindowTextA(c,text,sizeof text);
        for(i=0;text[i];++i) if(text[i]=='&' && text[i+1]) {
            if(text[i+1]=='&') { ++i; continue; }
            if(fold((unsigned char)text[i+1])!=fold(key)) continue;
            GetClassNameA(c,cls,sizeof cls);
            if(!strcasecmp(cls,"STATIC")) dialog_focus(h,GetNextDlgTabItem(h,c,0));
            else { dialog_focus(h,c); if(!strcasecmp(cls,"BUTTON")) SendMessageA(c,0xF5,0,0); }
            return 1;
        }
    }
    return 0;
}
S int IsDialogMessageA(unsigned int h,void *msg) {
    unsigned int *m=msg,focus,code; Dialog *d=dialog(h);
    if(!m || (m[0]!=h && (!ancestor(m[0]) || ancestor(m[0])->hwnd!=h))) return 0;
    if(m[1]!=0x100 && m[1]!=0x104 && m[1]!=0x102 && m[1]!=0x106 && m[1]!=0x101 && m[1]!=0x105) return 0;
    focus=GetFocus()?GetFocus():m[0]; code=(unsigned int)SendMessageA(focus,0x87,m[2],(unsigned int)msg);
    if(m[1]==0x101 || m[1]==0x105) { if(d && fold(d->skip_char)==fold(m[2])) d->skip_char=0; return 0; }
    if(code&4) return 0; /* control selectively requests this key/message */
    if(m[1]==0x102 || m[1]==0x106) {
        if(d && d->skip_char && fold(d->skip_char)==fold(m[2])) { d->skip_char=0; return 1; }
        if(m[2]==9 && !(code&2)) return 1;
        if(m[1]==0x106 || !(code&0x80)) return mnemonic(h,m[2]);
        return 0;
    }
    if(m[1]==0x104) {
        if(mnemonic(h,m[2])) { if(d) d->skip_char=m[2]; return 1; }
        return 0;
    }
    if(m[2]==9 && !(code&2)) {
        if(d) d->skip_char=9;
        dialog_focus(h,GetNextDlgTabItem(h,focus,(GetKeyState(16)&0x8000)!=0)); return 1;
    }
    if(m[2]==13 || m[2]==27) {
        unsigned int id=m[2]==27?2:(d?d->defid:1),child=GetDlgItem(h,id);
        if(m[2]==13 && (code&0x30)) { child=focus; id=GetDlgCtrlID(focus); }
        if(d) d->skip_char=m[2];
        if(child && !IsWindowEnabled(child)) return 1;
        SendMessageA(h,0x111,id,child); return 1;
    }
    if(m[2]>=37 && m[2]<=40 && !(code&1)) {
        dialog_focus(h,GetNextDlgGroupItem(h,focus,m[2]==37 || m[2]==38)); return 1;
    }
    return 0;
}

/* ---- Reading a parameter/string that lives in *this* process --------------------------------
 * An ARM LPARAM is a plain address in the single address space the port uses (guest images at
 * their PE VAs, guest stack and arena in the emulated regions), so the string is directly
 * readable -- but a bad integer must never become a SIGSEGV in a diagnostic.
 *
 * The previous implementation used process_vm_readv(), which under qemu-arm-static is a stub:
 * measured ENOSYS (errno 38) for *every* range, including a host stack buffer. That is why the
 * fatal dialog printed "<unreadable LPARAM>" for the template *and* for the control texts: the
 * read primitive failed, not the pointer. Reading /proc/self/maps to validate the range keeps
 * the original guarantee (no dereference of an untrusted address) without the unavailable
 * syscall. */
static int hexval(int c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static const char *hexnum(const char *p, unsigned int *out) {
    unsigned int v=0; int d=hexval((unsigned char)*p);
    if(d<0) return 0;
    while((d=hexval((unsigned char)*p))>=0) { v=(v<<4)|(unsigned int)d; ++p; }
    *out=v; return p;
}
typedef struct { unsigned int lo, hi; int found, readable; } Region;
/* Region containing va, plus the nearest region below it (reported when unmapped). */
static void region_info(unsigned int va, Region *hit, Region *below) {
    static char maps[1<<17];
    Region near; int fd, ok=0; long n; const char *p, *end;
    hit->lo=hit->hi=0; hit->found=hit->readable=0;
    near.lo=near.hi=0; near.found=near.readable=0;
    fd=open("/proc/self/maps",0);
    if(fd<0) { *below=near; return; }
    n=read(fd,maps,sizeof maps-1);
    close(fd);
    if(n<=0) { *below=near; return; }
    maps[n]=0; end=maps+n;
    for(p=maps;p<end;) {
        const char *line_end=p, *e; unsigned int lo, hi;
        while(line_end<end && *line_end && *line_end!='\n') ++line_end;
        e=hexnum(p,&lo);
        if(e && e<line_end && *e=='-') {
            e=hexnum(e+1,&hi);
            if(e && e<line_end && *e==' ' && hi>lo) {
                if(va>=lo && va<hi) { hit->lo=lo; hit->hi=hi; hit->found=1;
                                      hit->readable=e[1]=='r'; ok=1; }
                if(hi<=va && (!near.found || hi>near.hi)) { near.lo=lo; near.hi=hi; near.found=1; }
            }
        }
        if(ok || line_end==end) break;
        p=line_end+1;
    }
    *below=near;
}
/* Bytes from va that are contiguous mapped memory, capped at len. 0 = do not dereference. */
static unsigned int span_mapped(unsigned int va, unsigned int len) {
    Region hit, below;
    if(!va || !len || va+len<va) return 0;
    region_info(va,&hit,&below);
    if(!hit.found || !hit.readable) return 0;
    return (len < hit.hi-va) ? len : hit.hi-va;
}
/* Read arbitrary LPARAM safely: WM_INITDIALOG parameters are NOT always strings.
 * One write per record avoids interleaving with the MPQ worker's traces. */
static void log_text(const char *tag, const char *name, unsigned int ptr, const char *via) {
    char text[TEXT], out[TEXT*2+320]; unsigned int span, n=0, i, j=0;
    span=ptr?span_mapped(ptr,sizeof text-1):0;
    if(span) { memcpy(text,(const void *)(unsigned long)ptr,span); text[span]=0; n=(unsigned int)strlen(text); }
    j=(unsigned int)snprintf(out,sizeof out,"[dlg] %s=%s text=\"",tag,name);
    if(ptr && !n) {
        Region hit, below; const char *s="<unreadable LPARAM>";
        region_info(ptr,&hit,&below);
        while(*s) out[j++]=*s++;
        out[j++]='"';
        /* Diagnose the *address*, never the string: VA, whether it is mapped, the closest region
         * below and who asked for the read. */
        j+=(unsigned int)snprintf(out+j,sizeof out-j," va=0x%08X mapped=%d span=0x%X near=0x%08X-0x%08X via=%s",
            ptr,hit.found,span,below.lo,below.hi,via?via:"?");
        /* When the address *is* mapped but starts with NUL the bytes are logged, not guessed. */
        if(hit.found && hit.readable) {
            unsigned int k, base=ptr&~15u;
            unsigned int sp=span_mapped(base,32);
            j+=(unsigned int)snprintf(out+j,sizeof out-j," bytes=" );
            for(k=0;k<sp && j+3<sizeof out;++k) j+=(unsigned int)snprintf(out+j,3,"%02X",((const unsigned char *)(unsigned long)base)[k]);
        }
        out[j++]='\n'; write(2,out,j); return;
    }
    for(i=0;i<n && text[i] && j+5<sizeof out;++i) {
        unsigned char c=text[i];
        if(c=='\n' || c=='\r' || c=='\t' || c=='\\' || c=='\"') {
            out[j++]='\\'; out[j++]=c=='\n'?'n':c=='\r'?'r':c=='\t'?'t':c;
        } else if(c<32) { out[j++]='?'; } else out[j++]=c;
    }
    out[j++]='"';
    /* Fatal template 0x6A packs `target path\\nerror text`; expose its first line separately. */
    if(!strcmp(tag,"template") && !strcmp(name,"0x6A") && n) {
        unsigned int k=0;
        j+=(unsigned int)snprintf(out+j,sizeof out-j," target=\"");
        while(k<n && text[k] && text[k]!='\n' && text[k]!='\r' && j+5<sizeof out) {
            unsigned char c=text[k++];
            if(c=='\\' || c=='"') { out[j++]='\\'; out[j++]=c; }
            else if(c<32) out[j++]='?'; else out[j++]=c;
        }
        out[j++]='"';
    }
    out[j++]='\n'; write(2,out,j);
}
static void text_lines(void *dc, const char *s,int x,int y,int width,int height) {
    int i=0, n=0, yy=y, tm[14], cw=8, ch=16, columns; char line[512];
    if(GetTextMetricsA(dc,tm)) { cw=tm[5]>0?tm[5]:8; ch=tm[0]>0?tm[0]:16; }
    columns=width/cw;
    if(columns<1) return;
    if(columns>510) columns=510;
    while(s[i] && yy+ch<=y+height) {
        char c=s[i++]; if(c=='\r') continue;
        if(c=='\n') { TextOutA(dc,x,yy,line,n); yy+=ch; n=0; continue; }
        line[n++]=c;
        if(n>=columns) { TextOutA(dc,x,yy,line,n); yy+=ch; n=0; }
    }
    if(n && yy+ch<=y+height) TextOutA(dc,x,yy,line,n);
}
static void paint(Dialog *d) {
    void *dc, *oldfont; unsigned long oldcolor; unsigned int oldalign; int oldmode;
    unsigned int i; int r[4], cr[4]; char title[TEXT], cls[64];
    if(!host_elf_canvas || !gdi_elf_window_dc) return;
    dc=gdi_elf_window_dc(); if(!dc) return;
    GetWindowRect(d->hwnd,r);
    SelectClipRgn(dc,0); oldfont=SelectObject(dc,d->font?(void *)d->font:GetStockObject(17));
    oldalign=SetTextAlign(dc,0); oldcolor=SetTextColor(dc,0); oldmode=SetBkMode(dc,1);
    FillRect(dc,r,GetStockObject(1));
    Rectangle(dc,r[0],r[1],r[2],r[3]);
    GetWindowTextA(d->hwnd,title,sizeof title);
    text_lines(dc,title,r[0]+8,r[1]+2,r[2]-r[0]-16,16);
    for(i=0;i<d->count;++i) {
        unsigned int h=d->child[i];
        if(!IsWindowVisible(h)) continue;
        GetWindowRect(h,cr); GetWindowTextA(h,title,sizeof title); GetClassNameA(h,cls,sizeof cls);
        SetTextColor(dc,IsWindowEnabled(h)?0:0x808080);
        if(strcasecmp(cls,"STATIC")) { FillRect(dc,cr,GetStockObject(!strcasecmp(cls,"BUTTON")?1:0)); Rectangle(dc,cr[0],cr[1],cr[2],cr[3]); }
        if(!strcasecmp(cls,"EDIT") && !(GetWindowLongA(h,-16)&4)) {
            unsigned int sel=(unsigned int)SendMessageA(h,0xB0,0,0),a=sel&65535,b=sel>>16;
            int start[2]={0},end[2]={0},tm[14],box[4];
            /* Real GDI supplies clipping; no-video builds intentionally omit that module.
             * Without it, keep painting functional rather than requiring a clip provider. */
            if(IntersectClipRect) IntersectClipRect(dc,cr[0]+3,cr[1]+2,cr[2]-3,cr[3]-2);
            TextOutA(dc,cr[0]+4,cr[1]+2,title,(int)strlen(title));
            GetTextExtentPoint32A(dc,title,a,start); GetTextExtentPoint32A(dc,title,b,end); GetTextMetricsA(dc,tm);
            if(a!=b && (GetFocus()==h || (GetWindowLongA(h,-16)&0x100))) {
                box[0]=cr[0]+4+start[0]; box[1]=cr[1]+2; box[2]=cr[0]+4+end[0]; box[3]=box[1]+tm[0];
                FillRect(dc,box,GetStockObject(4)); SetTextColor(dc,0xFFFFFF);
                TextOutA(dc,box[0],box[1],title+a,b-a);
            } else if(GetFocus()==h) {
                Rectangle(dc,cr[0]+4+end[0],cr[1]+2,cr[0]+5+end[0],cr[1]+2+tm[0]);
            }
            SelectClipRgn(dc,0); /* paint starts with no clip; restore that state */
            SetTextColor(dc,IsWindowEnabled(h)?0:0x808080);
        } else text_lines(dc,title,cr[0]+4,cr[1]+2,cr[2]-cr[0]-8,cr[3]-cr[1]-4);
        if(GetFocus()==h && !strcasecmp(cls,"BUTTON")) Rectangle(dc,cr[0]+2,cr[1]+2,cr[2]-2,cr[3]-2);
    }
    SelectObject(dc,oldfont); SetTextColor(dc,oldcolor); SetTextAlign(dc,oldalign); SetBkMode(dc,oldmode);
    host_elf_present(); d->dirty=0;
}
/* Input has ONE consumer: Peek/IsDialogMessage/Dispatch below. Never recreate
 * key or click messages from host_elf_last_key/mouse snapshots. */
int dlg_elf_template(void *inst,const void *data,unsigned int size,unsigned int owner,
                     DlgProc proc,unsigned int param,const char *name) {
    Template *t; Dialog *d=0; int i, disabled=0, result, width=640,height=480,pitch;
    unsigned int oldfocus=GetFocus(), oldactive=GetActiveWindow(), m[7], quit=0;
    unsigned int savedpal[256]; int havepal=0; unsigned char *canvas, *savedpixels=0;
    int savedw=0,savedh=0,savedpitch=0;
    char *autoid;
    log_text("template",name,param,"bridge: imp_DialogBoxParamA -> DialogBoxParamA/WM_INITDIALOG LPARAM"); /* BEFORE resource parsing or callback */
    t=malloc(sizeof *t);
    if(!t || !parse(data,size,t)) { free(t); SetLastError(13); return -1; }
    for(i=0;i<16;++i) if(!dialogs[i].used) { d=&dialogs[i]; break; }
    if(!d) { free(t); SetLastError(8); return -1; }
    memset(d,0,sizeof *d); d->used=1; d->bx=8; d->by=16; d->defid=1;
    /* One host window/canvas is the supported backend contract. Recreate it if
     * the game's fatal handler has already shut DirectDraw down. */
    if(host_elf_init && host_elf_canvas) {
        canvas=host_elf_canvas(&width,&height,&pitch);
        if(canvas && width>0 && height>0 && pitch>=width && height<=8192 && pitch<=32768) {
            savedpixels=malloc((unsigned long)pitch*height);
            if(savedpixels) { memcpy(savedpixels,canvas,(unsigned long)pitch*height); savedw=width; savedh=height; savedpitch=pitch; }
        }
        if(!canvas) {
            width=640; height=480;
            if(host_elf_init(t->title)<0 || host_elf_display_mode(width,height,8)<0) { d->used=0; free(t); SetLastError(8); return -1; }
        }
        if(gdi_elf_init) gdi_elf_init();
        if(host_elf_get_palette) havepal=host_elf_get_palette(savedpal,0,256)==0;
        if(host_elf_set_palette) { unsigned int pal[256]; for(i=0;i<256;++i) pal[i]=(unsigned int)i*0x010101u; host_elf_set_palette(pal,0,256); }
        host_elf_set_title(t->title);
    }
    if(t->style&0x40) {
        d->font=(unsigned int)CreateFontA(t->point>0?-(t->point*96/72):-16,0,0,0,400,0,0,0,0,0,0,0,0,t->face);
        if(gdi_elf_window_dc) {
            void *dc=gdi_elf_window_dc(), *old=SelectObject(dc,(void *)d->font); int tm[14];
            if(GetTextMetricsA(dc,tm)) { d->bx=tm[5]>0?tm[5]:8; d->by=tm[0]>0?tm[0]:16; }
            SelectObject(dc,old);
        }
    }
    d->hwnd=CreateWindowExA(t->ex,"#32770",t->title,t->style&~0x40000000u,
        (width-t->w*d->bx/4)/2,(height-t->h*d->by/8)/2,t->w*d->bx/4,t->h*d->by/8,owner,0,inst,0);
    if(!d->hwnd) {
        if(d->font) DeleteObject((void *)d->font);
        if(havepal && host_elf_set_palette) host_elf_set_palette(savedpal,0,256);
        free(savedpixels); d->used=0; free(t); SetLastError(8); return -1;
    }
    SetWindowLongA(d->hwnd,-4,(unsigned long)dlg_elf_wndproc);
    SetWindowLongA(d->hwnd,4,(unsigned long)proc);
    for(i=0;i<(int)t->count;++i) {
        Item *c=&t->item[i]; unsigned int h=CreateWindowExA(c->ex,c->cls,c->title,c->style|0x40000000u,
            c->x*d->bx/4,c->y*d->by/8,c->w*d->bx/4,c->h*d->by/8,d->hwnd,c->id,inst,(void *)c->extra);
        if(!h) { EndDialog(d->hwnd,-1); break; }
        d->child[d->count++]=h;
        if(!strcasecmp(c->cls,"BUTTON") && (c->style&15)==1) d->defid=c->id;
    }
    if(d->font) {
        SendMessageA(d->hwnd,0x30,d->font,0);
        for(i=0;i<(int)d->count;++i) SendMessageA(d->child[i],0x30,d->font,0);
    }
    free(t);
    if(owner && IsWindowEnabled(owner)) { EnableWindow(owner,0); disabled=1; }
    if(!d->done) {
        unsigned int first=GetNextDlgTabItem(d->hwnd,0,0);
        if(dlg_elf_wndproc(d->hwnd,0x110,first,param) && !d->done) dialog_focus(d->hwnd,first); /* WM_INITDIALOG */
        if(!d->done) { ShowWindow(d->hwnd,5); SetActiveWindow(d->hwnd); d->dirty=1; }
    }
    for(i=0;i<(int)d->count;++i) {
        char text[TEXT], id[24]; GetWindowTextA(d->child[i],text,sizeof text);
        snprintf(id,sizeof id,"0x%X",GetDlgCtrlID(d->child[i])); log_text("control",id,(unsigned int)text,"dlg_elf_template local control text");
    }
    reasm_elf_log("[dlg] WM_INITDIALOG delivered; entering modal loop\n");
    /* Explicit diagnostic input only; never silently auto-dismiss dialogs. */
    autoid=getenv("SC_DLG_COMMAND");
    if(autoid && !d->done) { unsigned int id=atoi(autoid); PostMessageA(d->hwnd,0x111,id,GetDlgItem(d->hwnd,id)); }
    /* Explicit opt-in for automated diagnostics only. This is not input injection: it calls the
     * Win32 dialog API directly after the fatal text has been logged and WM_INITDIALOG delivered.
     * Default is unchanged; SC_DLG_AUTOCLOSE must equal exactly "1". */
    autoid=getenv("SC_DLG_AUTOCLOSE");
    if(autoid && autoid[0]=='1' && !autoid[1] && !d->done) {
        reasm_elf_log("[dlg] SC_DLG_AUTOCLOSE=1: EndDialog(hwnd,2) after text logging\n");
        EndDialog(d->hwnd,2);
    }
    while(!d->done && IsWindow(d->hwnd)) {
        while(!d->done && PeekMessageA(m,0,0,0,1)) {
            if(m[1]==0x12) { quit=1; result=(int)m[2]; EndDialog(d->hwnd,2); break; }
            if(!IsDialogMessageA(d->hwnd,m)) { TranslateMessage(m); DispatchMessageA(m); }
        }
        if(!d->done) { if(d->dirty) paint(d); usleep(10000); }
    }
    { int dialog_result=d->result;
      DestroyWindow(d->hwnd);
      if(d->font) {
          if(gdi_elf_window_dc && GetCurrentObject(gdi_elf_window_dc(),6)==(void *)d->font)
              SelectObject(gdi_elf_window_dc(),GetStockObject(17));
          DeleteObject((void *)d->font);
      }
      d->used=0;
      if(disabled && IsWindow(owner)) EnableWindow(owner,1);
      if(IsWindow(oldactive)) SetActiveWindow(oldactive);
      if(IsWindow(oldfocus)) SetFocus(oldfocus);
      if(havepal && host_elf_set_palette) host_elf_set_palette(savedpal,0,256);
      if(savedpixels && host_elf_canvas) {
          int w,h,p; unsigned char *px=host_elf_canvas(&w,&h,&p);
          if(px && w==savedw && h==savedh && p==savedpitch) { memcpy(px,savedpixels,(unsigned long)p*h); host_elf_present(); }
      }
      free(savedpixels);
      if(host_elf_set_title && owner && IsWindow(owner)) { char title[TEXT]; GetWindowTextA(owner,title,sizeof title); host_elf_set_title(title); }
      if(quit) PostQuitMessage(result);
      { char b[80]; int n=snprintf(b,sizeof b,"[dlg] destroyed; DialogBoxParamA returns %d\n",dialog_result); write(2,b,n); }
      return dialog_result; }
}
S int DialogBoxParamA(void *inst,const char *name,unsigned int owner,DlgProc proc,unsigned int param) {
    void *res; char label[128];
    if(owner && owner!=0x10000 && !IsWindow(owner)) { log_text("template","<invalid owner>",param,"DialogBoxParamA invalid-owner path"); return 0; }
    res=FindResourceA(inst,name,(const char *)5);
    if((unsigned int)name<65536) snprintf(label,sizeof label,"0x%X",(unsigned int)name);
    else snprintf(label,sizeof label,"%s",name?name:"(null)");
    return dlg_elf_template(inst,res?LockResource(LoadResource(inst,res)):0,
        res?SizeofResource(inst,res):0,owner,proc,param,label);
}
void *dlg_elf_lookup(const char *name) {
#define E(n) if(!strcasecmp(name,#n)) return (void *)n
    E(DialogBoxParamA); E(EndDialog); E(DefDlgProcA); E(IsDialogMessageA);
    E(SendDlgItemMessageA); E(MapDialogRect); E(GetNextDlgTabItem); E(GetNextDlgGroupItem);
#undef E
    return 0;
}

/* Reproducible regression gate. Does NOT run any original game/storm code.
 * SC_DLG_SELFTEST=1 after loader/TEB initialization selects this from main_elf.
 * Runs from the normal MPQ cwd (Local.dll is needed as read-only test data).
 */
extern unsigned int win32_ui_load_library(const char *);
extern S int LoadStringA(void *, unsigned int, char *, int);
extern S void *FindResourceExA(void *, const char *, const char *, unsigned short);
extern S void *LoadAcceleratorsA(void *, const char *);
extern S int SetDlgItemTextA(unsigned int, int, const char *);
extern S int GetDlgItemTextA(unsigned int, int, char *, int);
extern S int SetWindowTextA(unsigned int, const char *);
extern S unsigned int GetLastError(void);
extern S int GetMessageA(void *, unsigned int, unsigned int, unsigned int);
extern S int GetWindowTextLengthA(unsigned int);
extern S int CallWindowProcA(void *, unsigned int, unsigned int, unsigned int, unsigned int);
extern int strcmp(const char *, const char *);
extern int unsetenv(const char *);
static int test_pass, test_fail, test_mode, test_init, test_destroy, test_cmd;
static unsigned int test_owner, test_child;
static void input_drain(unsigned int h);
static void check(int condition,const char *what) {
    char b[200]; int n=snprintf(b,sizeof b,"[dlg-test] %s %s\n",condition?"PASS":"FAIL",what);
    write(2,b,n); if(condition) ++test_pass; else ++test_fail;
}
typedef struct { unsigned char b[4096]; unsigned int n; } Writer;
static void wr(Writer *w,unsigned int v,unsigned int bytes) { unsigned int i; for(i=0;i<bytes;++i) w->b[w->n++]=(unsigned char)(v>>(i*8)); }
static void wide(Writer *w,const char *s) { while(*s) wr(w,(unsigned char)*s++,2); wr(w,0,2); }
static void make_template(Writer *w,int ext) {
    int i; unsigned int atoms[]={0x80,0x80,0x81,0x82,0x85,0x83,0x80};
    unsigned int ids[]={1,2,100,1000,101,102,103};
    const char *titles[]={"OK","Cancel","edit","static","combo","list","check"};
    w->n=0;
    if(ext) { wr(w,1,2); wr(w,65535,2); wr(w,0,4); wr(w,0,4); wr(w,0x90C00040,4); }
    else { wr(w,0x90C00040,4); wr(w,0,4); }
    wr(w,7,2); wr(w,0,2); wr(w,0,2); wr(w,220,2); wr(w,160,2);
    wr(w,0,2); wr(w,0,2); wide(w,"Native dialog test");
    wr(w,12,2); if(ext) { wr(w,400,2); wr(w,0,1); wr(w,1,1); } wide(w,"System");
    for(i=0;i<7;++i) {
        unsigned int style=0x50010000u | (i==0?1:i==6?3:0);
        while(w->n&3) wr(w,0,1);
        if(ext) { wr(w,0,4); wr(w,0,4); wr(w,style,4); }
        else { wr(w,style,4); wr(w,0,4); }
        wr(w,10+(i%2)*105,2); wr(w,20+(i/2)*32,2); wr(w,95,2); wr(w,28,2);
        wr(w,ids[i],ext?4:2); wr(w,65535,2); wr(w,atoms[i],2); wide(w,titles[i]);
        wr(w,i==0?(ext?3:5):0,2); if(i==0) { wr(w,1,1); wr(w,2,1); wr(w,3,1); }
    }
}
static S int test_proc(unsigned int h,unsigned int m,unsigned int w,unsigned int l) {
    if(m==0x110) {
        char b[4096]; unsigned int c, msg[7]={0}; int r[4]={0,0,4,8}; Dialog *d=dialog(h);
        ++test_init; test_child=GetDlgItem(h,1000);
        check(l==(unsigned int)"test init text","WM_INITDIALOG LPARAM preserved");
        check(!IsWindowEnabled(test_owner),"owner disabled before INIT");
        check(GetDlgCtrlID(GetDlgItem(h,1))==1 && GetDlgCtrlID(GetDlgItem(h,102))==102,"child IDs and hierarchy");
        SetDlgItemTextA(h,1000,"first line\nsecond line");
        GetDlgItemTextA(h,1000,b,sizeof b); check(!strcmp(b,"first line\nsecond line"),"Set/GetDlgItemTextA multiline");
        SendDlgItemMessageA(h,100,0xC,0,(unsigned int)"abc");
        check(SendDlgItemMessageA(h,100,0xE,0,0)==3,"WM_SETTEXT/GETTEXTLENGTH edit");
        SendDlgItemMessageA(h,100,0xD,sizeof b,(unsigned int)b); check(!strcmp(b,"abc"),"WM_GETTEXT edit");
        SendDlgItemMessageA(h,100,0xC5,4,0); SendDlgItemMessageA(h,100,0x102,'d',0); SendDlgItemMessageA(h,100,0x102,'e',0);
        GetDlgItemTextA(h,100,b,sizeof b); check(!strcmp(b,"abcd"),"edit limit and characters");
        check(SendDlgItemMessageA(h,101,0x143,0,(unsigned int)"one")==0,"combo add string");
        SendDlgItemMessageA(h,101,0x143,0,(unsigned int)"two");
        check(SendDlgItemMessageA(h,101,0x146,0,0)==2 && SendDlgItemMessageA(h,101,0x14E,1,0)==1,"combo count/selection");
        SendDlgItemMessageA(h,101,0x148,1,(unsigned int)b); check(!strcmp(b,"two"),"combo get item text");
        check(SendDlgItemMessageA(h,102,0x180,0,(unsigned int)"list item")==0,"list add string");
        check(SendDlgItemMessageA(h,102,0x186,0,0)==0 && SendDlgItemMessageA(h,102,0x188,0,0)==0,"list selection");
        SendDlgItemMessageA(h,103,0xF1,1,0); check(SendDlgItemMessageA(h,103,0xF0,0,0)==1,"button check state");
        c=GetDlgItem(h,100); check(SendMessageA(c,0x31,0,0)!=0,"WM_SETFONT delivered");
        check(MapDialogRect(h,r) && r[2]==d->bx && r[3]==d->by,"MapDialogRect uses font units");
        c=GetDlgItem(h,1); SetFocus(c); msg[0]=c; msg[1]=0x100; msg[2]=9;
        check(IsDialogMessageA(h,msg) && GetFocus()==GetDlgItem(h,2),"Tab navigation");
        check(GetNextDlgTabItem(h,GetDlgItem(h,2),1)==c,"reverse Tab navigation");
        EnableWindow(GetDlgItem(h,2),0);
        check(GetNextDlgTabItem(h,c,0)==GetDlgItem(h,100),"Tab skips disabled controls"); EnableWindow(GetDlgItem(h,2),1);
        paint(d);
        if(host_elf_canvas) {
            int ww,hh,p,rr[4],x,y,dark=0,light=0; unsigned char *px=host_elf_canvas(&ww,&hh,&p);
            GetWindowRect(h,rr);
            for(y=rr[1];y<rr[3] && y<hh;++y) for(x=rr[0];x<rr[2] && x<ww;++x) if(x>=0 && y>=0) { if(px[y*p+x]<32) ++dark; if(px[y*p+x]>128) ++light; }
            check(dark>100 && light>100,"GDI text/border/background canvas pixels");
        }
        if(test_mode==5) {
            struct host_input_event e; int br[4]; unsigned int button=GetDlgItem(h,1);
            GetWindowRect(button,br); memset(&e,0,sizeof e);
            e.type=HOST_INPUT_DOWN; e.key=1; e.x=(br[0]+br[2])/2; e.y=(br[1]+br[3])/2;
            check(host_elf_test_event(&e)==1,"SDL mouse-down queued at Win32 dialog button");
            e.type=HOST_INPUT_UP;
            check(host_elf_test_event(&e)==1,"SDL mouse-up queued at Win32 dialog button");
            input_drain(h);
            check(test_cmd==1 && dialog(h) && dialog(h)->done && dialog(h)->result==71,
                  "mouse button click sends WM_COMMAND and ends Win32 dialog");
        }
        if(test_mode==0) PostMessageA(h,0x111,1,GetDlgItem(h,1));
        if(test_mode==1) PostMessageA(h,0x10,0,0);
        if(test_mode==2) EndDialog(h,73);
        if(test_mode==3) DestroyWindow(h);
        if(test_mode==4) PostQuitMessage(55);
        return 1;
    }
    if(m==0x111 && (w==1 || w==2)) { ++test_cmd; EndDialog(h,w==1?71:72); return 1; }
    if(m==2) { ++test_destroy; check(IsWindow(test_child),"WM_DESTROY precedes child destruction"); }
    return 0;
}
static S int resource_proc(unsigned int h,unsigned int m,unsigned int w,unsigned int l) {
    (void)w;
    if(m==0x110) { ++test_init; SetDlgItemTextA(h,1000,(const char *)l); EndDialog(h,90); return 1; }
    return 0;
}
int dlg_elf_selftest(void) {
    Writer w; Template *t=malloc(sizeof *t); unsigned int mod, i, n; void *res; char text[256]; int result;
    unsigned int msg[7];
    unsetenv("SC_DLG_COMMAND");
    check(host_elf_init && gdi_elf_window_dc,"native host/GDI available");
    if(!host_elf_init || !gdi_elf_window_dc || !t) { free(t); return 1; }
    mod=win32_ui_load_library("local.dll"); check(mod!=0,"LoadLibrary Local.dll case-insensitive");
    check(mod==win32_ui_load_library("LOCAL.DLL"),"stable resource module handle");
    res=FindResourceA((void *)mod,(const char *)106,(const char *)5);
    check(res && LockResource(LoadResource((void *)mod,res)) && SizeofResource((void *)mod,res)>18,"Local.dll dialog resource bytes");
    check(res==FindResourceA((void *)mod,"#106","#5"),"decimal resource names");
    check(res==FindResourceExA((void *)mod,(const char *)5,(const char *)106,0x409),"FindResourceExA exact language");
    check(!FindResourceExA((void *)mod,(const char *)5,(const char *)106,0x411),"missing language rejected");
    check(!LockResource((void *)123) && !SizeofResource((void *)mod,(void *)123),"invalid HRSRC safe");
    n=0; for(i=0;i<1024;++i) if(LoadStringA((void *)mod,i,text,sizeof text)>0) ++n;
    check(n==37,"37 Local.dll strings served");
    check(LoadAcceleratorsA((void *)mod,(const char *)101)!=0,"Local.dll accelerator resource");
    res=0; for(i=1;i<200 && !res;++i) res=FindResourceA(0,(const char *)i,(const char *)5);
    { const unsigned char *header=(const unsigned char *)0x400000;
      unsigned int opt=*(const unsigned int *)(header+60)+24;
      unsigned int start=0x400000+*(const unsigned int *)(header+opt+112);
      unsigned int end=start+*(const unsigned int *)(header+opt+116);
      check(res && (unsigned int)LockResource(res)>=start && (unsigned int)LockResource(res)<end,"exe resource at original VA"); }
    for(i=101;i<=112;++i) {
        res=FindResourceA((void *)mod,(const char *)i,(const char *)5);
        check(res && parse(LockResource(res),SizeofResource((void *)mod,res),t),"Local.dll template parsed");
    }
    for(i=0;i<2;++i) {
        unsigned int j; int all=1; make_template(&w,i);
        check(parse(w.b,w.n,t) && t->extended==(int)i && t->count==7,"standard/extended template and creation data");
        for(j=0;j<w.n;++j) if(parse(w.b,j,t)) { all=0; break; }
        check(all,"every truncated template rejected");
    }
    make_template(&w,0); w.b[8]=95; check(!parse(w.b,w.n,t),"control count bound");
    free(t);
    host_elf_init("Dialog test owner"); host_elf_display_mode(640,480,8);
    { int ww,hh,p; unsigned char *px=host_elf_canvas(&ww,&hh,&p); if(px) host_elf_fill8(px,p,ww,hh,0,17); }
    test_owner=CreateWindowExA(0,"STATIC","Owner",0x10000000,0,0,640,480,0,0,0,0);
    SetFocus(test_owner); SetActiveWindow(test_owner);
    for(test_mode=0;test_mode<6;++test_mode) {
        make_template(&w,test_mode&1); test_init=test_destroy=test_cmd=0;
        result=dlg_elf_template(0,w.b,w.n,test_owner,test_proc,(unsigned int)"test init text",test_mode&1?"extended-test":"standard-test");
        check(result==(test_mode==0?71:test_mode==1?72:test_mode==2?73:test_mode==3?0:test_mode==4?2:71),"modal result propagated");
        check(test_init==1 && test_destroy==1,"INIT/DESTROY delivered once");
        check(!IsWindow(test_child) && IsWindowEnabled(test_owner),"children destroyed and owner reenabled");
        check(GetFocus()==test_owner && GetActiveWindow()==test_owner,"focus/active restored");
        { int ww,hh,p; unsigned char *px=host_elf_canvas(&ww,&hh,&p); check(px && px[240*p+320]==17,"owner canvas restored after modal dialog"); }
        if(test_mode<2) check(test_cmd==1,"WM_COMMAND forwarded to dialog callback");
        if(test_mode==5) check(test_cmd==1,"button click reaches dialog callback once");
        if(test_mode==4) check(PeekMessageA(msg,0,0,0,1) && msg[1]==0x12 && msg[2]==55,"WM_QUIT reposted");
    }
    test_init=0;
    check(DialogBoxParamA((void *)mod,(const char *)106,test_owner,resource_proc,(unsigned int)"real resource test")==90 && test_init==1,"real Local.dll dialog created/callback/result");
    make_template(&w,1);
    check(dlg_elf_template(0,w.b,w.n-1,test_owner,test_proc,0x12345,"truncated-test")==-1 && GetLastError()==13,"invalid LPARAM log and malformed resource safe");
    check(DialogBoxParamA((void *)mod,(const char *)9999,test_owner,resource_proc,0)==-1,"missing dialog returns minus one");
    check(DialogBoxParamA((void *)mod,(const char *)106,0xABC,resource_proc,0)==0,"invalid owner returns zero");
    { unsigned int other=CreateWindowExA(0,"STATIC","Queue",0,0,0,10,10,0,0,0,0);
      PostMessageA(other,0x8001,11,0); PostMessageA(test_owner,0x8002,22,0);
      check(PeekMessageA(msg,test_owner,0x8002,0x8002,0) && msg[2]==22,"queue filters past unrelated message");
      check(GetMessageA(msg,test_owner,0x8002,0x8002)==1 && msg[2]==22,"GetMessage normal message result");
      check(PeekMessageA(msg,other,0,0,1) && msg[2]==11,"filtered removal preserves unrelated message");
      PostQuitMessage(44); check(GetMessageA(msg,test_owner,0,0)==0 && msg[2]==44,"GetMessage zero only for WM_QUIT");
      DestroyWindow(other); }
    DestroyWindow(test_owner);
    { char b[100]; int k=snprintf(b,sizeof b,"[dlg-test] RESULT %d passed, %d failed\n",test_pass,test_fail); write(2,b,k); }
    return test_fail?1:0;
}

static S int input_demo_proc(unsigned int h,unsigned int m,unsigned int w,unsigned int l) {
    char text[TEXT], out[TEXT+100]; int n;
    if(m==0x110) {
        if(getenv("SC_DLG_INPUT_DEMO")[0]!='r') {
            SetDlgItemTextA(h,100,""); SetFocus(GetDlgItem(h,100)); return 0;
        }
        return 1;
    }
    if(m==0x111 && (w>>16)==0x300) {
        GetWindowTextA(l,text,sizeof text);
        n=snprintf(out,sizeof out,"[dlg-input-demo] EN_CHANGE id=%u text=\"%s\"\n",w&65535,text); write(2,out,n);
    }
    if(m==0x111 && (w==1 || w==2)) {
        GetDlgItemTextA(h,100,text,sizeof text);
        n=snprintf(out,sizeof out,"[dlg-input-demo] COMMAND id=%u text=\"%s\"\n",w,text); write(2,out,n);
        EndDialog(h,(int)w); return 1;
    }
    return 0;
}
int dlg_elf_input_demo(void) {
    Writer w; unsigned int owner; int result;
    if(!host_elf_init || host_elf_init("T3-bis native dialog input") || host_elf_display_mode(640,480,8)) return 2;
    owner=CreateWindowExA(0,"STATIC","Probe owner",0x10000000,0,0,640,480,0,0,0,0);
    if(getenv("SC_DLG_INPUT_DEMO")[0]=='r') {
        unsigned int mod=win32_ui_load_library("local.dll");
        result=DialogBoxParamA((void *)mod,(const char *)108,owner,input_demo_proc,0);
    } else {
        make_template(&w,0);
        result=dlg_elf_template(0,w.b,w.n,owner,input_demo_proc,0,"T3-bis synthetic EDIT probe");
    }
    DestroyWindow(owner); if(host_elf_shutdown) host_elf_shutdown();
    return result==1 || result==2 ? 0 : 1;
}

extern S void *CreateAcceleratorTableA(const void *,int);
extern S int CopyAcceleratorTableA(void *,void *,int);
extern S int DestroyAcceleratorTable(void *);
extern S int TranslateAcceleratorA(unsigned int,void *,const void *);
static int input_changes,input_commands,input_keydown,input_chars,input_keyup;
static unsigned int input_command;
static void *input_original_edit;
static S int input_edit_probe(unsigned int h,unsigned int m,unsigned int w,unsigned int l) {
    if(m==0x100) ++input_keydown;
    if(m==0x102) ++input_chars;
    if(m==0x101) ++input_keyup;
    return CallWindowProcA(input_original_edit,h,m,w,l);
}
static void input_drain(unsigned int h) {
    unsigned int msg[7];
    while(PeekMessageA(msg,0,0,0,1))
        if(!IsDialogMessageA(h,msg)) { TranslateMessage(msg); DispatchMessageA(msg); }
}
static void input_mask(int mods) {
    struct host_input_event e; memset(&e,0,sizeof e);
    e.type=HOST_INPUT_KEYUP; e.modifiers=mods; host_elf_input_state(&e);
}
static S int input_semantic_proc(unsigned int h,unsigned int m,unsigned int w,unsigned int l) {
    if(m==0x111) {
        if((w>>16)==0x300) ++input_changes;
        if((w>>16)<=1) { ++input_commands; input_command=w; }
        return 1;
    }
    if(m==0x110) {
        unsigned int edit=GetDlgItem(h,100),msg[7]={0},a,b,sel;
        char text[TEXT]; struct host_input_event e; int r[4];
        unsigned char accel[6]={13,0,'K',0,0xBC,2},copied[6]; void *table;
        input_original_edit=(void *)SetWindowLongA(edit,-4,(unsigned int)input_edit_probe);
        SetFocus(edit); input_drain(h); input_mask(0);
        SetWindowTextA(edit,""); input_changes=0;
        SendMessageA(edit,0x100,'A',1); GetWindowTextA(edit,text,sizeof text);
        check(!text[0] && input_changes==0,"EDIT KEYDOWN never inserts a printable VK");
        SendMessageA(edit,0x102,'a',1); SendMessageA(edit,0x101,'A',0xC0000001u);
        GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"a") && input_changes==1,"EDIT CHAR inserts once, KEYUP never inserts");
        SetWindowTextA(edit,"abcd"); input_changes=0;
        SendMessageA(edit,0x100,8,1); GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"abcd"),"backspace KEYDOWN does not delete");
        SendMessageA(edit,0x102,8,1); GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"abc") && input_changes==1,"backspace CHAR deletes once");
        SendMessageA(edit,0xB1,1,3); a=b=99; sel=SendMessageA(edit,0xB0,(unsigned int)&a,(unsigned int)&b);
        check(a==1 && b==3 && sel==0x30001,"EM_SETSEL/GETSEL pointers and packed bounds");
        SendMessageA(edit,0x102,'Z',1); GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"aZ") && SendMessageA(edit,0xB0,0,0)==0x20002,"typing replaces selected text and moves caret");
        SetWindowTextA(edit,"abcd"); SendMessageA(edit,0xB1,1,3); input_changes=0;
        SendMessageA(edit,0x100,46,1); GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"ad") && input_changes==1,"Delete KEYDOWN removes selection once");
        SendMessageA(edit,0xB1,0,0xFFFFFFFFu); SendMessageA(edit,0xC2,0,(unsigned int)"xyz");
        GetWindowTextA(edit,text,sizeof text); check(!strcmp(text,"xyz"),"EM_REPLACESEL replaces full selection");
        SendMessageA(edit,0xCF,1,0); SendMessageA(edit,0x102,'Q',1);
        GetWindowTextA(edit,text,sizeof text); check(!strcmp(text,"xyz"),"read-only ignores character mutation"); SendMessageA(edit,0xCF,0,0);
        SendMessageA(edit,0xC5,3,0); SendMessageA(edit,0x102,'Q',1);
        GetWindowTextA(edit,text,sizeof text); check(!strcmp(text,"xyz") && SendMessageA(edit,0xD5,0,0)==3,"edit text limit"); SendMessageA(edit,0xC5,0,0);
        SendMessageA(edit,0x100,36,1); input_mask(1); SendMessageA(edit,0x100,39,1); input_mask(0);
        check(SendMessageA(edit,0xB0,0,0)==0x10000,"Home and Shift-Right select first character");
        SendMessageA(edit,0x102,'X',1); GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"Xyz"),"typing replaces keyboard selection");
        input_mask(0x40); SendMessageA(edit,0x100,'A',1); input_mask(0);
        check(SendMessageA(edit,0xB0,0,0)==0x30000,"Ctrl-A selects all");
        SendMessageA(edit,0xB1,0xFFFFFFFFu,0); check(SendMessageA(edit,0xB0,0,0)==0x30003,"EM_SETSEL minus one clears selection");
        SetWindowTextA(edit,""); SendMessageA(edit,0x102,0xE9,1); GetWindowTextA(edit,text,sizeof text);
        check((unsigned char)text[0]==0xE9 && !text[1],"EDIT accepts ANSI CP1252, not just ASCII");
        SetFocus(GetDlgItem(h,1)); memset(&e,0,sizeof e); e.type=HOST_INPUT_KEYDOWN; e.key=9; host_elf_test_event(&e);
        e.type=HOST_INPUT_KEYUP; host_elf_test_event(&e); input_drain(h);
        check(GetFocus()==GetDlgItem(h,2),"SDL FIFO Tab moves ONE control");
        e.type=HOST_INPUT_KEYDOWN; e.key=0x400000E1; e.modifiers=1; host_elf_test_event(&e);
        e.key=9; host_elf_test_event(&e); e.type=HOST_INPUT_KEYUP; host_elf_test_event(&e);
        e.key=0x400000E1; e.modifiers=0; host_elf_test_event(&e); input_drain(h);
        check(GetFocus()==GetDlgItem(h,1) && GetKeyState(16)==0,"SDL FIFO Shift-Tab and release same batch");
        SetWindowTextA(edit,"abc"); SetFocus(GetDlgItem(h,2)); e.type=HOST_INPUT_KEYDOWN; e.key=9; host_elf_test_event(&e);
        e.type=HOST_INPUT_KEYUP; host_elf_test_event(&e);
        /* Drain SDL into host FIFO before adding structured text. No SDL synthetic TEXTINPUT. */
        { extern void win32_input_pump(void); win32_input_pump(); }
        host_elf_test_text("xy"); input_drain(h); GetWindowTextA(edit,text,sizeof text);
        check(GetFocus()==edit && !strcmp(text,"xy"),"Tab retargets queued TEXT to new focus and autoselects EDIT");
        SetWindowTextA(edit,""); input_changes=input_keydown=input_chars=input_keyup=0;
        e.type=HOST_INPUT_KEYDOWN; e.key='a'; host_elf_test_event(&e);
        { extern void win32_input_pump(void); win32_input_pump(); }
        host_elf_test_text("a"); e.type=HOST_INPUT_KEYUP; host_elf_test_event(&e); input_drain(h);
        GetWindowTextA(edit,text,sizeof text);
        check(!strcmp(text,"a") && input_changes==1 && input_keydown==1 && input_chars==1 && input_keyup==1,"ordered KEYDOWN CHAR KEYUP: no legacy duplicate");
        input_commands=0; SetFocus(edit); msg[0]=edit; msg[1]=0x100; msg[2]=13;
        check(IsDialogMessageA(h,msg) && input_commands==1 && input_command==1,"Enter default button command once");
        msg[1]=0x102; check(IsDialogMessageA(h,msg) && input_commands==1,"consumed Enter CHAR does not leak to EDIT");
        msg[1]=0x100; msg[2]=27;
        check(IsDialogMessageA(h,msg) && input_commands==2 && input_command==2,"Escape cancel command once");
        SetDlgItemTextA(h,103,"&Choice"); input_commands=0; msg[1]=0x104; msg[2]='C'; input_mask(0x100);
        check(IsDialogMessageA(h,msg) && input_commands==1 && input_command==103,"Alt mnemonic invokes button");
        msg[1]=0x106; msg[2]='c'; check(IsDialogMessageA(h,msg) && input_commands==1,"mnemonic SYSCHAR is consumed once"); input_mask(0);
        table=CreateAcceleratorTableA(accel,1);
        check(table && CopyAcceleratorTableA(table,0,0)==1 && CopyAcceleratorTableA(table,copied,1)==1 && !memcmp(accel,copied,6),"accelerator create/copy ACCEL six-byte ABI");
        msg[0]=edit; msg[1]=0x100; msg[2]='K'; input_commands=0;
        check(!TranslateAcceleratorA(h,table,msg),"Ctrl-Shift accelerator rejects missing modifiers"); input_mask(0x41);
        check(TranslateAcceleratorA(h,table,msg) && input_commands==1 && input_command==0x102BC,"Ctrl-Shift accelerator synchronous COMMAND high word one"); input_mask(0x141);
        check(!TranslateAcceleratorA(h,table,msg) && input_commands==1,"accelerator rejects extra Alt"); input_mask(0);
        check(DestroyAcceleratorTable(table) && !CopyAcceleratorTableA(table,0,0) && !TranslateAcceleratorA(h,(void *)1,msg),"destroy and invalid accelerator handles safe");
        input_commands=0; GetWindowRect(GetDlgItem(h,103),r); memset(&e,0,sizeof e);
        e.x=(r[0]+r[2])/2; e.y=(r[1]+r[3])/2; e.key=1; e.type=HOST_INPUT_DOWN; host_elf_test_event(&e);
        e.type=HOST_INPUT_UP; host_elf_test_event(&e); input_drain(h);
        check(input_commands==1 && input_command==103,"mouse button down-up batch clicks once without legacy path");
        EndDialog(h,99); return 0;
    }
    (void)l; return 0;
}
int dlg_elf_input_selftest(void) {
    Writer w; int result; unsigned int owner; char b[100];
    if(!host_elf_init || !host_elf_input_state || host_elf_init("T3-bis input fidelity") || host_elf_display_mode(640,480,8)) return 2;
    owner=CreateWindowExA(0,"STATIC","Input test owner",0x10000000,0,0,640,480,0,0,0,0);
    make_template(&w,0); result=dlg_elf_template(0,w.b,w.n,owner,input_semantic_proc,0,"T3-bis synthetic fidelity selftest");
    check(result==99,"input selftest modal result"); DestroyWindow(owner); host_elf_shutdown();
    snprintf(b,sizeof b,"[dlg-input-selftest] RESULT %d passed, %d failed\n",test_pass,test_fail); reasm_elf_log(b);
    return test_fail?1:0;
}
