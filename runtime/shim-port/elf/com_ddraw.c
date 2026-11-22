/* sc-reasm/tools/elf/com_ddraw.c — IDirectDraw/IDirectDrawSurface/Palette/Clipper in C (Q2).
 *
 * Vedi com_ddraw.h per il contratto e i riferimenti. Note di fedelta' al modello LOLA, con le
 * differenze dichiarate:
 *   [1] IDirectDraw::GetCaps ha 3 argomenti (this, driver caps, hel caps) come da vtable DX5;
 *       LOLA la dichiarava a 2. Qui si scrive un solo dword in ciascuna: le strutture DDCAPS
 *       complete sono 380 byte e il gioco (misurato) non le legge.
 *   [2] IDirectDrawSurface::GetPalette ritorna DDERR_NOPALETTEATTACHED quando nessuna palette e'
 *       agganciata, invece di DD_OK con puntatore a zero.
 *   [3] GetPixelFormat dichiara DDPF_PALETTEINDEXED8 a 8 bpp (percorso 8 bit del gioco), non un
 *       formato direct-color: qui il canvas e' la superficie a palette.
 *   [4] Sulla primaria il presente e' sull'Unlock e sui Blt diretti alla primaria (come LOLA).
 *   [5] Nessuno slot "indovinato": le arita' vengono dalla vtable pubblica DirectX 5+ (DX5: 23/33/7/9
 *       slot), la stessa usata da LOLA. Se in futuro si aggiunge uno slot non verificato si usa
 *       com_unk(): annuncia e ferma (regola D19), mai un `ret` con arita' sbagliata.
 */
#include "com_ddraw.h"
#include "host_elf.h"
#include "reasm_elf.h"
#if defined(__arm__) || defined(__i386__)
extern void arm_com_wrap_vtable(void **, const unsigned char *, unsigned);
#endif

#define S __attribute__((stdcall))
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef long HRES;

extern void *malloc(unsigned long);
extern void free(void *);
extern void *memset(void *, int, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);

#define DD_OK 0
#ifndef NULL
#define NULL ((void *) 0)
#endif
#define DDERR_NOCLIPLIST        0x800401F9u
#define DDERR_NOCLIPPERATTACHED 0x887600E1u
#define DDERR_NOPALETTEATTACHED 0x887600F0u

/* ---- offsets DDSURFACEDESC(2) (verificati: vedi task, dwSize 124 e ddsCaps a 0x68) ---- */
#define DDSD_FLAGS_OFF     0x04
#define DDSD_HEIGHT_OFF    0x08
#define DDSD_WIDTH_OFF     0x0C
#define DDSD_LPITCH_OFF    0x10
#define DDSD_LPSURFACE_OFF 0x24
#define DDSD_PIXELFORMAT_OFF 0x48
#define DDSD_CAPS_OFF      0x68
#define DDSD_SIZE          124

#define DDSD_CAPS        0x1u
#define DDSD_HEIGHT      0x2u
#define DDSD_WIDTH       0x4u
#define DDSD_PITCH       0x8u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_LPSURFACE   0x800u

#define DDSCAPS_BACKBUFFER      0x4u
#define DDSCAPS_COMPLEX         0x8u
#define DDSCAPS_FLIP            0x10u
#define DDSCAPS_OFFSCREENPLAIN  0x40u
#define DDSCAPS_PRIMARYSURFACE  0x200u
#define DDSCAPS_VIDEOMEMORY     0x4000u

#define DDBLT_ROP         0x00000040u
#define DDBLT_COLORFILL   0x00000400u
#define DDBLT_KEYSRC      0x00008000u
#define DDBLT_KEYDEST     0x00002000u
#define DDBLT_WAIT        0x01000000u
#define DDBLTFAST_SRCCOLORKEY 0x1u

#define DDPF_PALETTEINDEXED8 0x20u
#define DDPF_RGB             0x40u

#define DDBLTFX_DWROP_OFF      0x08
#define DDBLTFX_DWFILLCOLOR_OFF 0x50

/* ============================ oggetti ============================ */
struct Surf;
typedef struct DD {
    void **vt; long refs;
    struct Surf *primary, *back;
    int w, h, bpp;
    u32 pal[256]; int has_pal;
} DD;
typedef struct Pal { void **vt; long refs; u32 e[256]; } Pal;
typedef struct Clipper {
    void **vt; long refs; void *hwnd;
    u8 *clip_data; u32 clip_size; int has_clip_list; int changed;
} Clipper;
S long c_AddRef(void *t);
S long c_Release(void *t);

typedef struct Surf {
    void **vt; long refs;
    int bpp;
    u8 *px; int w, h, pitch; unsigned long size;
    int owns_px;                      /* 0 = il buffer e' il canvas del host (primaria) */
    u32 pal[256]; int has_pal;
    int is_primary, is_back;
    Clipper *clipper;
    struct Surf *attached;
    DD *dd;
    u32 ckey; u32 ckey_high; int has_ckey;
} Surf;

static DD *g_last_dd;
static Pal *g_primary_pal;            /* palette agganciata alla primaria (0 = nessuna) */
static int g_live;
static void *surface_vt[33];
static void *ddraw_vt[23];
static void *palette_vt[7];
static void *clipper_vt[9];

/* Slot non verificato: annuncia e ferma (D19). Non usato per gli slot implementati: resta per
 * chi in futuro volesse tagliare un metodo senza indovinarne l'arita'. */
static void com_unk(const char *iface, int slot) {
    reasm_elf_log("[com] NON IMPLEMENTATO: ");
    reasm_elf_log(iface);
    reasm_elf_log(" slot ");
    reasm_elf_log_hex((unsigned) slot);
    reasm_elf_log(" -> stop (D19: arita' non verificata)\n");
    extern void _exit(int);
    _exit(3);
}

/* ============================ helper ============================ */
static void surf_desc_fill(void *desc, Surf *s) {
    if (!desc) return;
    if (!s) return;
    *(u32 *) ((u8 *) desc + DDSD_HEIGHT_OFF) = (u32) s->h;
    *(u32 *) ((u8 *) desc + DDSD_WIDTH_OFF) = (u32) s->w;
    *(u32 *) ((u8 *) desc + DDSD_LPITCH_OFF) = (u32) s->pitch;
    *(u32 *) ((u8 *) desc + DDSD_LPSURFACE_OFF) = (u32) s->px;
    *(u32 *) ((u8 *) desc + DDSD_FLAGS_OFF) |=
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_LPSURFACE;
    if (*(u32 *) desc >= DDSD_SIZE)
        *(u32 *) ((u8 *) desc + DDSD_CAPS_OFF) |=
            s->is_primary ? DDSCAPS_PRIMARYSURFACE
                          : (s->is_back ? (DDSCAPS_BACKBUFFER | DDSCAPS_OFFSCREENPLAIN)
                                        : DDSCAPS_OFFSCREENPLAIN);
}

static void present(Surf *s) {
    if (!s) return;
    if (s->is_primary || s->is_back) host_elf_present();
}

/* La primaria vive nel canvas del host: se il display mode e' cambiato il buffer si e' spostato. */
static void sync_primary(void) {
    int cw = 0, ch = 0, cp = 0;
    u8 *p;
    if (!g_last_dd || !g_last_dd->primary) return;
    p = host_elf_canvas(&cw, &ch, &cp);
    if (!p || cw <= 0) return;
    g_last_dd->primary->px = p;
    g_last_dd->primary->w = cw;
    g_last_dd->primary->h = ch;
    g_last_dd->primary->pitch = cp;
    g_last_dd->primary->size = (unsigned long) cp * (unsigned long) ch;
}

/* Evaluate the Win32 ROP3 truth table (truth index: P,S,D), independently
 * for every bit. ROP2-style special cases are naturally covered by the same table. */
static u32 rop3_pixel(u32 rop, u32 pat, u32 src, u32 dst, u32 mask) {
    u32 truth = (rop >> 16) & 0xFFu, out = 0;
    unsigned k;
    for (k = 0; k < 8; ++k) if (truth & (1u << k)) {
        u32 term = (k & 4u) ? pat : ~pat;
        term &= (k & 2u) ? src : ~src;
        term &= (k & 1u) ? dst : ~dst;
        out |= term;
    }
    return out & mask;
}
static u32 pixel_load(const u8 *p, u32 bpp) {
    if (bpp == 8) return p[0];
    if (bpp == 16) return (u32)p[0] | ((u32)p[1] << 8);
    if (bpp == 24) return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void pixel_store(u8 *p, u32 bpp, u32 v) {
    p[0]=(u8)v;
    if (bpp >= 16) p[1]=(u8)(v >> 8);
    if (bpp >= 24) p[2]=(u8)(v >> 16);
    if (bpp == 32) p[3]=(u8)(v >> 24);
}
static int rop_clip_contains(const Surf *d, int x, int y) {
    const Clipper *c=d->clipper;
    u32 n,k;
    if (!c || !c->has_clip_list || !c->clip_data) return 1;
    n=*(const u32 *)(c->clip_data+8);
    for (k=0;k<n;k++) {
        const int *r=(const int *)(c->clip_data+32+k*16);
        if (x>=r[0] && y>=r[1] && x<r[2] && y<r[3]) return 1;
    }
    return 0;
}
static void rop_rect(Surf *d, Surf *s, const int *dr, const int *sr,
                     u32 rop, u32 pat, int ckey) {
    int x, y, dx0=dr?dr[0]:0, dy0=dr?dr[1]:0;
    int dx1=dr?dr[2]:(d?d->w:0), dy1=dr?dr[3]:(d?d->h:0);
    int sx0=sr?sr[0]:0, sy0=sr?sr[1]:0;
    int sx1=sr?sr[2]:(s?s->w:0), sy1=sr?sr[3]:(s?s->h:0);
    int dw=dx1-dx0, dh=dy1-dy0, sw=sx1-sx0, sh=sy1-sy0;
    u32 bpp, bytes, mask;
    if (!d || !d->px || !s || !s->px || dw<=0 || dh<=0 || sw<=0 || sh<=0 || d->bpp!=s->bpp) return;
    bpp=d->bpp; bytes=bpp/8;
    if (bytes!=1 && bytes!=2 && bytes!=3 && bytes!=4) return;
    mask=bpp==32?0xFFFFFFFFu:((1u<<bpp)-1u);
    for (y=dy0;y<dy1;y++) for (x=dx0;x<dx1;x++) {
        int sx,sy; u8 *dp; const u8 *sp; u32 sv,dv;
        if (x<0 || y<0 || x>=d->w || y>=d->h || !rop_clip_contains(d,x,y)) continue;
        sx=sx0+(int)(((long)(x-dx0)*sw)/dw);
        sy=sy0+(int)(((long)(y-dy0)*sh)/dh);
        if (sx<0 || sy<0 || sx>=s->w || sy>=s->h) continue;
        sp=s->px+(unsigned long)sy*s->pitch+(unsigned long)sx*bytes;
        sv=pixel_load(sp,bpp);
        if (ckey>=0 && (sv&0xFFu)==(u32)ckey) continue;
        dp=d->px+(unsigned long)y*d->pitch+(unsigned long)x*bytes;
        dv=pixel_load(dp,bpp);
        pixel_store(dp,bpp,rop3_pixel(rop,pat,sv,dv,mask));
    }
}

/* ============================ IDirectDrawSurface (33) ============================ */
S long s_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DD_OK; }
S long s_AddRef(void *t) { return ++((Surf *) t)->refs; }
S long s_Release(void *t) {
    Surf *s = (Surf *) t;
    if (--s->refs > 0) return s->refs;
    if (s->is_primary || s->is_back || (s->dd && (s->dd->primary == s || s->dd->back == s))) return 0;
    if (s->px && s->owns_px) free(s->px);
    if (s->clipper) { c_Release(s->clipper); s->clipper = 0; }
    --g_live;
    free(s);
    return 0;
}
S long s_AddAttachedSurface(void *t, void *x) { ((Surf *) t)->attached = (Surf *) x; return DD_OK; }
S long s_AddOverlayDirtyRect(void *t, void *r) { (void) t; (void) r; return DD_OK; }
/* Blit un rettangolo contro ogni RECT della clip list. La trasformazione sorgente->
 * destinazione e' mantenuta anche per Blt scalati; superfici senza lista conservano
 * il percorso diretto. Le clip list provengono da c_SetClipList (header validato). */
static void blit_clipped(Surf *d, Surf *s, const int *dr, const int *sr, int ckey) {
    Clipper *c = d->clipper;
    int drect[4], srect[4], dw, dh, sw, sh;
    u32 n, i;
    if (!dr || !c || !c->has_clip_list || !c->clip_data) {
        host_elf_blit8(d->px, d->pitch, d->w, d->h, s->px, s->pitch, s->w, s->h, dr, sr, ckey);
        return;
    }
    memcpy(drect, dr, sizeof drect);
    if (sr) memcpy(srect, sr, sizeof srect);
    else { srect[0] = 0; srect[1] = 0; srect[2] = s->w; srect[3] = s->h; }
    dw = drect[2] - drect[0]; dh = drect[3] - drect[1];
    sw = srect[2] - srect[0]; sh = srect[3] - srect[1];
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    n = *(const u32 *)(c->clip_data + 8);
    for (i = 0; i < n; ++i) {
        const int *cr = (const int *)(c->clip_data + 32 + i * 16);
        int l = cr[0] > drect[0] ? cr[0] : drect[0];
        int t = cr[1] > drect[1] ? cr[1] : drect[1];
        int r = cr[2] < drect[2] ? cr[2] : drect[2];
        int b = cr[3] < drect[3] ? cr[3] : drect[3];
        int dd[4], ss[4];
        if (l >= r || t >= b) continue;
        dd[0] = l; dd[1] = t; dd[2] = r; dd[3] = b;
        ss[0] = srect[0] + (int)(((long)(l - drect[0]) * sw) / dw);
        ss[1] = srect[1] + (int)(((long)(t - drect[1]) * sh) / dh);
        ss[2] = srect[0] + (int)(((long)(r - drect[0]) * sw) / dw);
        ss[3] = srect[1] + (int)(((long)(b - drect[1]) * sh) / dh);
        host_elf_blit8(d->px, d->pitch, d->w, d->h, s->px, s->pitch, s->w, s->h, dd, ss, ckey);
    }
}

S long s_Blt(void *t, void *dst, void *src, void *srcr, u32 flags, void *fx) {
    Surf *d = (Surf *) t, *s = (Surf *) src;
    int dx = 0, dy = 0, dw = -1, dh = -1;
    if (d && d->is_primary) sync_primary();
    if (dst) { const int *r = (const int *) dst; dx = r[0]; dy = r[1]; dw = r[2] - r[0]; dh = r[3] - r[1]; }
    if ((flags & DDBLT_ROP) && fx && d) {
        u32 rop = *(const u32 *) ((const u8 *) fx + DDBLTFX_DWROP_OFF);
        u32 pat = *(const u32 *) ((const u8 *) fx + DDBLTFX_DWFILLCOLOR_OFF);
        int dr[4];
        dr[0]=dst?dx:0; dr[1]=dst?dy:0;
        dr[2]=dst?dx+dw:d->w; dr[3]=dst?dy+dh:d->h;
        rop_rect(d,s,dr,(const int *)srcr,rop,pat,
                 ((flags&DDBLT_KEYSRC) && s && s->has_ckey)?(int)(s->ckey&0xFF):-1);
        present(d);
        return DD_OK;
    }
    if (flags & DDBLT_COLORFILL) {
        u32 c = 0;
        if (fx) c = *(const u32 *) ((const u8 *) fx + DDBLTFX_DWFILLCOLOR_OFF);
        if (!dst && d) { dx = 0; dy = 0; dw = d->w; dh = d->h; }
        if (d && d->px && dw > 0 && dh > 0) {
            int r[4];
            r[0] = dx; r[1] = dy; r[2] = dx + dw; r[3] = dy + dh;
            /* il colore di riempimento di una superficie a palette e' l'INDICE di palette */
            host_elf_fill8(d->px, d->pitch, d->w, d->h, r, (int) (c & 0xFF));
            present(d);
        }
        return DD_OK;
    }
    if (!s || !s->px || !d || !d->px) return DD_OK;
    if (s->bpp != d->bpp) {
        reasm_elf_log("[com] Blt: bpp diversi (src/dst), saltato\n");
        return DD_OK;
    }
    {
        int ckey = ((flags & DDBLT_KEYSRC) && s->has_ckey) ? (int) (s->ckey & 0xFF) : -1;
        int cr[4];
        if (!dst) { cr[0] = 0; cr[1] = 0; cr[2] = d->w; cr[3] = d->h; }
        else { cr[0] = dx; cr[1] = dy; cr[2] = dx + dw; cr[3] = dy + dh; }
        blit_clipped(d, s, cr, (const int *) srcr, ckey);
    }
    present(d);
    return DD_OK;
}
S long s_BltBatch(void *t, void *b, u32 c, u32 f) { (void) t; (void) b; (void) c; (void) f; return DD_OK; }
S long s_BltFast(void *t, u32 x, u32 y, void *src, void *srcr, u32 f) {
    Surf *d = (Surf *) t, *s = (Surf *) src;
    int cr[4], ckey;
    if (d && d->is_primary) sync_primary();
    if (!s || !s->px || !d || !d->px) return DD_OK;
    ckey = ((f & DDBLTFAST_SRCCOLORKEY) && s->has_ckey) ? (int) (s->ckey & 0xFF) : -1;
    {   const int *sr = (const int *) srcr;
        int sw = sr ? (sr[2] - sr[0]) : s->w;
        int sh = sr ? (sr[3] - sr[1]) : s->h;
        cr[0] = (int) x; cr[1] = (int) y; cr[2] = (int) x + sw; cr[3] = (int) y + sh;
    }
    blit_clipped(d, s, cr, (const int *) srcr, ckey);
    present(d);
    return DD_OK;
}
S long s_DeleteAttachedSurface(void *t, u32 f, void *x) { (void) f; (void) x; ((Surf *) t)->attached = 0; return DD_OK; }
S long s_EnumAttachedSurfaces(void *t, void *c, void *cb) { (void) t; (void) c; (void) cb; return DD_OK; }
S long s_EnumOverlayZOrders(void *t, u32 f, void *c, void *cb) { (void) t; (void) f; (void) c; (void) cb; return DD_OK; }
S long s_Flip(void *t, void *dest, u32 flags) {
    Surf *s = (Surf *) t;
    DD *dd = s->dd;
    (void) dest; (void) flags;
    if (dd && dd->back && dd->primary && dd->primary->size <= dd->back->size)
        memcpy(dd->primary->px, dd->back->px, dd->primary->size);
    present(dd && dd->back ? dd->back : s);
    return DD_OK;
}
S long s_GetAttachedSurface(void *t, void *caps, void **out) {
    Surf *s = (Surf *) t;
    DD *dd = s->dd;
    (void) caps;
    if (out) *out = s->attached ? (void *) s->attached
                                : (s->is_primary ? (void *) (dd ? dd->back : 0)
                                                 : (void *) (dd ? dd->primary : 0));
    return DD_OK;
}
S long s_GetBltStatus(void *t, u32 f) { (void) t; (void) f; return DD_OK; }
S long s_GetCaps(void *t, void *caps) {
    Surf *s = (Surf *) t;
    /* DDSCAPS e' UN dword: scriverne quattro sfora la struttura del chiamante (trappola reale di LOLA). */
    if (caps) {
        u32 *c = (u32 *) caps;
        c[0] = s->is_primary ? DDSCAPS_PRIMARYSURFACE : DDSCAPS_OFFSCREENPLAIN;
        if (s->is_back) c[0] |= DDSCAPS_BACKBUFFER;
    }
    return DD_OK;
}
S long s_GetClipper(void *t, void **c) {
    Surf *s = (Surf *) t;
    if (!c) return (long) 0x80070057u; /* E_INVALIDARG */
    *c = s->clipper;
    if (!s->clipper) return DDERR_NOCLIPPERATTACHED;
    c_AddRef(s->clipper);
    return DD_OK;
}
S long s_GetColorKey(void *t, u32 f, void *ck) {
    Surf *s = (Surf *) t;
    (void) f;
    if (ck) { u32 *p = (u32 *) ck; p[0] = s->ckey; p[1] = s->ckey_high; }   /* DDCOLORKEY = 8 byte */
    return DD_OK;
}
S long s_GetDC(void *t, void **dc) { if (dc) *dc = t; return DD_OK; }
S long s_GetFlipStatus(void *t, u32 f) { (void) t; (void) f; return DD_OK; }
S long s_GetOverlayPosition(void *t, long *x, long *y) { (void) t; if (x) *x = 0; if (y) *y = 0; return DD_OK; }
S long s_GetPalette(void *t, void **p) {
    Surf *s = (Surf *) t;
    if (p) *p = 0;
    return s->has_pal ? DD_OK : DDERR_NOPALETTEATTACHED;
}
S long s_GetPixelFormat(void *t, void *pf) {
    Surf *s = (Surf *) t;
    if (pf) {
        u32 *p = (u32 *) pf;
        memset(pf, 0, 32);
        p[0] = 32;                                       /* dwSize */
        if (s->bpp == 8) { p[1] = DDPF_PALETTEINDEXED8; p[3] = 8; }
        else if (s->bpp == 16) { p[1] = DDPF_RGB; p[3] = 16; p[4] = 0xF800; p[5] = 0x07E0; p[6] = 0x001F; }
        else if (s->bpp == 24) { p[1] = DDPF_RGB; p[3] = 24; p[4] = 0xFF0000; p[5] = 0x00FF00; p[6] = 0x0000FF; }
        else { p[1] = DDPF_RGB; p[3] = 32; p[4] = 0xFF0000; p[5] = 0x00FF00; p[6] = 0x0000FF; }
    }
    return DD_OK;
}
S long s_GetSurfaceDesc(void *t, void *desc) { surf_desc_fill(desc, (Surf *) t); return DD_OK; }
S long s_Initialize(void *t, void *dd, void *desc) { (void) t; (void) dd; (void) desc; return DD_OK; }
S long s_IsLost(void *t) { (void) t; return DD_OK; }
S long s_Lock(void *t, void *rect, void *desc, u32 flags, void *ev) {
    Surf *s = (Surf *) t;
    (void) rect; (void) flags; (void) ev;
    if (s->is_primary) sync_primary();
    surf_desc_fill(desc, s);
    return DD_OK;
}
S long s_ReleaseDC(void *t, void *dc) { (void) t; (void) dc; return DD_OK; }
S long s_Restore(void *t) { (void) t; return DD_OK; }
S long s_SetClipper(void *t, void *c) {
    Surf *s = (Surf *) t;
    Clipper *next = (Clipper *) c;
    if (next) c_AddRef(next);
    if (s->clipper) c_Release(s->clipper);
    s->clipper = next;
    return DD_OK;
}
S long s_SetColorKey(void *t, u32 flags, void *ck) {
    Surf *s = (Surf *) t;
    (void) flags;
    if (ck) { const u32 *p = (const u32 *) ck; s->ckey = p[0]; s->ckey_high = p[1]; s->has_ckey = 1; }
    return DD_OK;
}
S long s_SetOverlayPosition(void *t, long x, long y) { (void) t; (void) x; (void) y; return DD_OK; }
S long s_SetPalette(void *t, void *pal) {
    Surf *s = (Surf *) t;
    if (!pal) { s->has_pal = 0; if (s->is_primary) g_primary_pal = 0; return DD_OK; }
    /* Le voci si leggono DIRETTAMENTE dall'oggetto palette (Pal.e e' gia' 0x00RRGGBB).
     * TRAPPOLA REALE (trovata dal gate pixel): passare s->pal a GetEntries non funziona, perche'
     * GetEntries scrive byte PALETTEENTRY (R,G,B,flags) dentro un array di u32 -> i canali R e B
     * risultano invertiti (il framebuffer leggeva 0x56,0x34,0x12 al posto di 0x12,0x34,0x56). */
    {   Pal *pp = (Pal *) pal;
        memcpy(s->pal, pp->e, sizeof s->pal);
        s->has_pal = 1;
    }
    if (s->is_primary) {
        g_primary_pal = (Pal *) pal;
        host_elf_set_palette(s->pal, 0, 256);
    }
    return DD_OK;
}
S long s_Unlock(void *t, void *rect) {
    Surf *s = (Surf *) t;
    (void) rect;
    if (s->is_primary) sync_primary();
    present(s);
    return DD_OK;
}
S long s_UpdateOverlay(void *t, void *r, void *s, void *sr, u32 f, void *fx)
    { (void) t; (void) r; (void) s; (void) sr; (void) f; (void) fx; return DD_OK; }
S long s_UpdateOverlayDisplay(void *t, u32 f) { (void) t; (void) f; return DD_OK; }
S long s_UpdateOverlayZOrder(void *t, u32 f, void *s) { (void) t; (void) f; (void) s; return DD_OK; }

/* ============================ IDirectDrawPalette (7) ============================ */
S long p_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DD_OK; }
S long p_AddRef(void *t) { return ++((Pal *) t)->refs; }
S long p_Release(void *t) { return --((Pal *) t)->refs; }
S long p_GetCaps(void *t, u32 *c) { (void) t; if (c) *c = 0x4; return DD_OK; }   /* DDPCAPS_8BIT */
S long p_GetEntries(void *t, u32 f, u32 start, u32 n, void *e) {
    Pal *p = (Pal *) t;
    u8 *out = (u8 *) e;
    u32 i;
    (void) f;
    if (!e) return DD_OK;
    for (i = 0; i < n && start + i < 256; ++i) {
        u32 c = p->e[start + i];
        out[i * 4 + 0] = (u8) ((c >> 16) & 0xFF);   /* PALETTEENTRY: peRed, peGreen, peBlue, peFlags */
        out[i * 4 + 1] = (u8) ((c >> 8) & 0xFF);
        out[i * 4 + 2] = (u8) (c & 0xFF);
        out[i * 4 + 3] = 0;
    }
    return DD_OK;
}
S long p_Initialize(void *t, void *dd, u32 f, void *e) { (void) t; (void) dd; (void) f; (void) e; return DD_OK; }
/* SetEntries(this, dwFlags, dwStartingEntry, dwCount, lpEntries): 5 parametri -> ret 20.
 * (LOLA la dichiarava a 4: se il gioco la chiama direttamente, 4 byte restano sulla pila.) */
S long p_SetEntries(void *t, u32 flags, u32 start, u32 n, void *e) {
    Pal *p = (Pal *) t;
    const u8 *pe = (const u8 *) e;
    u32 i;
    (void) flags;
    if (!e) return DD_OK;
    for (i = 0; i < n && start + i < 256; ++i) {
        u32 c = ((u32) pe[i * 4 + 0] << 16) | ((u32) pe[i * 4 + 1] << 8) | (u32) pe[i * 4 + 2];
        p->e[start + i] = c;
        /* La superficie primaria usa la palette della superficie agganciata; finche' nessuna
         * palette e' agganciata si usa questa (comportamento osservato in LOLA: il gioco riempie
         * la palette con SetEntries e quella e' la palette con cui presentare). */
        if (!g_primary_pal || g_primary_pal == p) {
            u32 one = c;
            host_elf_set_palette(&one, (int) (start + i), 1);
        }
    }
    return DD_OK;
}

/* ============================ IDirectDrawClipper (9) ============================ */
S long c_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DD_OK; }
S long c_AddRef(void *t) { return ++((Clipper *) t)->refs; }
S long c_Release(void *t) {
    Clipper *c = (Clipper *) t;
    if (--c->refs > 0) return c->refs;
    if (c->clip_data) free(c->clip_data);
    free(c);
    return 0;
}
S long c_GetClipList(void *t, void *r, void *rg, u32 *sz) {
    Clipper *c = (Clipper *) t;
    (void) r; /* no host-window clipping; return the stored DirectDraw region list */
    if (!sz) return (long) 0x80070057u; /* E_INVALIDARG */
    if (!c->has_clip_list) { *sz = 0; return DDERR_NOCLIPLIST; }
    if (!rg || *sz < c->clip_size) {
        *sz = c->clip_size;
        return (long) 0x887600E4u; /* DDERR_REGIONTOOSMALL */
    }
    memcpy(rg, c->clip_data, c->clip_size);
    *sz = c->clip_size;
    return DD_OK;
}
S long c_GetHWnd(void *t, void **h) { if (h) *h = ((Clipper *) t)->hwnd; return DD_OK; }
S long c_Initialize(void *t, void *dd, u32 f) { (void) t; (void) dd; (void) f; return DD_OK; }
S long c_IsClipListChanged(void *t, int *b) {
    Clipper *c = (Clipper *) t;
    if (!b) return (long) 0x80070057u;
    *b = c->changed;
    c->changed = 0;
    return DD_OK;
}
S long c_SetClipList(void *t, void *rg, u32 f) {
    Clipper *c = (Clipper *) t;
    u32 size;
    u8 *copy;
    (void) f;
    if (!rg) {
        if (c->clip_data) free(c->clip_data);
        c->clip_data = 0; c->clip_size = 0; c->has_clip_list = 0; c->changed = 1;
        return DD_OK;
    }
    /* RGNDATAHEADER (32 bytes) + RECT array. Reject malformed/unbounded input. */
    size = *(const u32 *) rg;
    if (size < 32 || *(const u32 *) ((const u8 *) rg + 4) != 1u) return (long) 0x80070057u;
    {
        u32 count = *(const u32 *) ((const u8 *) rg + 8);
        u32 bytes = *(const u32 *) ((const u8 *) rg + 12);
        if (count > 65536u || bytes != count * 16u || bytes > 1024u * 1024u - 32u)
            return (long) 0x80070057u;
        size = 32u + bytes;
    }
    copy = (u8 *) malloc(size);
    if (!copy) return (long) 0x8007000Eu; /* E_OUTOFMEMORY */
    memcpy(copy, rg, size);
    if (c->clip_data) free(c->clip_data);
    c->clip_data = copy; c->clip_size = size; c->has_clip_list = 1; c->changed = 1;
    return DD_OK;
}
S long c_SetHWnd(void *t, u32 flags, void *hwnd) {
    Clipper *c = (Clipper *) t;
    (void) flags;
    if (c->hwnd != hwnd) c->changed = 1;
    c->hwnd = hwnd;
    return DD_OK;
}

/* ============================ IDirectDraw (23) ============================ */
S long d_QueryInterface(void *t, const void *iid, void **o) { (void) iid; if (o) *o = t; return DD_OK; }
S long d_AddRef(void *t) { return ++((DD *) t)->refs; }
S long d_Release(void *t) { return --((DD *) t)->refs; }
S long d_Compact(void *t) { (void) t; return DD_OK; }
S long d_CreateClipper(void *t, u32 f, void **c, void *o) {
    Clipper *cl;
    (void) t; (void) f; (void) o;
    cl = (Clipper *) malloc(sizeof(Clipper));
    if (!cl) return -1;
    memset(cl, 0, sizeof *cl);
    cl->vt = clipper_vt; cl->refs = 1;
    if (c) *c = cl;
    return DD_OK;
}
S long d_CreatePalette(void *t, u32 flags, void *entries, void **out, void *outer) {
    Pal *p;
    (void) t; (void) outer; (void) flags;
    p = (Pal *) malloc(sizeof(Pal));
    if (!p) return -1;
    p->vt = palette_vt; p->refs = 1;
    memset(p->e, 0, sizeof p->e);
    if (entries) p_SetEntries(p, 0, 0, 256, entries);
    if (out) *out = p;
    return DD_OK;
}
S long d_CreateSurface(void *t, void *desc, void **out, void *outer) {
    DD *dd = (DD *) t;
    u8 *d = (u8 *) desc;
    /* caps a 0x68: si legge solo se la descrizione e' abbastanza grande (v1 108, v2 124) */
    u32 caps = (d && *(const u32 *) d >= 0x6Cu) ? *(u32 *) (d + DDSD_CAPS_OFF) : 0;
    u32 dflags = d ? *(u32 *) (d + DDSD_FLAGS_OFF) : 0;
    int w = (d && (dflags & DDSD_WIDTH)) ? (int) *(u32 *) (d + DDSD_WIDTH_OFF) : dd->w;
    int h = (d && (dflags & DDSD_HEIGHT)) ? (int) *(u32 *) (d + DDSD_HEIGHT_OFF) : dd->h;
    Surf *s;
    int bpp = dd->bpp ? dd->bpp : 8;
    (void) outer;
    if (d && (dflags & DDSD_PIXELFORMAT)) {
        u32 bits = *(u32 *) (d + DDSD_PIXELFORMAT_OFF + 12);
        if (bits == 8 || bits == 16 || bits == 24 || bits == 32) bpp = (int) bits;
    }
    s = (Surf *) malloc(sizeof(Surf));
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    s->vt = surface_vt; s->refs = 1; s->dd = dd; s->bpp = bpp;
    s->is_primary = (caps & DDSCAPS_PRIMARYSURFACE) ? 1 : 0;
    s->is_back = (caps & DDSCAPS_BACKBUFFER) ? 1 : 0;
    if (s->is_primary) {
        int cw = 0, ch = 0, cp = 0;
        u8 *canvas = host_elf_canvas(&cw, &ch, &cp);
        if (!canvas) {
            if (host_elf_display_mode(w > 0 ? w : 640, h > 0 ? h : 480, dd->bpp) != 0) {
                free(s);
                return -1;
            }
            canvas = host_elf_canvas(&cw, &ch, &cp);
        }
        if (!canvas) { free(s); return -1; }
        if (cw > 0 && ch > 0) { w = cw; h = ch; }
        if (cp <= 0) cp = (w + 3) & ~3;
        s->px = canvas; s->owns_px = 0; s->pitch = cp;
        s->size = (unsigned long) cp * (unsigned long) h;
    } else {
        if (w <= 0) w = 640;
        if (h <= 0) h = 480;
        s->pitch = (w * (bpp ? bpp : 8) / 8 + 3) & ~3;
        s->size = (unsigned long) s->pitch * (unsigned long) h;
        s->px = (u8 *) malloc(s->size ? s->size : 1);
        if (!s->px) { free(s); return -1; }
        memset(s->px, 0, s->size);
        s->owns_px = 1;
    }
    s->w = w; s->h = h;
    if (dd->has_pal) { memcpy(s->pal, dd->pal, sizeof s->pal); s->has_pal = 1; }
    if (s->is_primary) {
        dd->primary = s;
        if (!dd->back) {                            /* la primaria ha sempre un back buffer */
            Surf *b = (Surf *) malloc(sizeof(Surf));
            if (b) {
                memset(b, 0, sizeof *b);
                b->vt = surface_vt; b->refs = 1; b->dd = dd; b->bpp = bpp;
                b->w = w; b->h = h; b->pitch = (w * (bpp ? bpp : 8) / 8 + 3) & ~3;
                b->size = (unsigned long) b->pitch * (unsigned long) h;
                b->px = (u8 *) malloc(b->size ? b->size : 1);
                if (b->px) { memset(b->px, 0, b->size); b->owns_px = 1; b->is_back = 1; dd->back = b; }
                else { free(b); }
            }
        }
        s->attached = dd->back;
    } else if (s->is_back) {
        dd->back = s;
    }
    ++g_live;
    if (out) *out = s;
    reasm_elf_log("[com] CreateSurface caps=");
    reasm_elf_log_hex(caps);
    reasm_elf_log(" size=");
    reasm_elf_log_hex((unsigned) (w << 16 | (h & 0xFFFF)));
    reasm_elf_log(s->is_primary ? " PRIMARY\n" : (s->is_back ? " BACK\n" : " OFFSCREEN\n"));
    return DD_OK;
}
S long d_DuplicateSurface(void *t, void *s, void **o) { (void) t; (void) s; if (o) *o = 0; return DD_OK; }
S long d_EnumDisplayModes(void *t, u32 f, void *desc, void *c, void *cb) { (void) t; (void) f; (void) desc; (void) c; (void) cb; return DD_OK; }
S long d_EnumSurfaces(void *t, u32 f, void *desc, void *c, void *cb) { (void) t; (void) f; (void) desc; (void) c; (void) cb; return DD_OK; }
S long d_FlipToGDISurface(void *t) { (void) t; return DD_OK; }
S long d_GetCaps(void *t, void *caps, void *helcaps) {
    (void) t;
    if (caps) *(u32 *) caps = DDSCAPS_VIDEOMEMORY;
    if (helcaps) *(u32 *) helcaps = DDSCAPS_VIDEOMEMORY;
    return DD_OK;
}
S long d_GetDisplayMode(void *t, void *desc) {
    DD *dd = (DD *) t;
    if (desc) {
        u8 *d = (u8 *) desc;
        surf_desc_fill(desc, 0);
        *(u32 *) (d + DDSD_HEIGHT_OFF) = (u32) dd->h;
        *(u32 *) (d + DDSD_WIDTH_OFF) = (u32) dd->w;
        *(u32 *) (d + DDSD_PIXELFORMAT_OFF) = 32;
        *(u32 *) (d + DDSD_PIXELFORMAT_OFF + 12) = (u32) (dd->bpp ? dd->bpp : 8);
        *(u32 *) (d + DDSD_FLAGS_OFF) |= DDSD_PIXELFORMAT;
    }
    return DD_OK;
}
S long d_GetFourCCCodes(void *t, u32 *n, u32 *c) { (void) t; if (n) *n = 0; (void) c; return DD_OK; }
S long d_GetGDISurface(void *t, void **s) { DD *dd = (DD *) t; if (s) *s = dd->primary; return DD_OK; }
S long d_GetMonitorFrequency(void *t, u32 *f) { (void) t; if (f) *f = 60; return DD_OK; }
S long d_GetScanLine(void *t, u32 *l) { (void) t; if (l) *l = host_elf_ticks() % 480; return DD_OK; }
S long d_GetVerticalBlankStatus(void *t, int *b) { (void) t; if (b) *b = 1; return DD_OK; }
S long d_Initialize(void *t, void *g) { (void) t; (void) g; return DD_OK; }
S long d_RestoreDisplayMode(void *t) { (void) t; return DD_OK; }
S long d_SetCooperativeLevel(void *t, void *hwnd, u32 flags) {
    (void) t;
    host_elf_coop_level((unsigned long) (unsigned int) hwnd, flags);   /* 19 = 0x13: bit 0x1 fullscreen */
    return DD_OK;
}
S long d_SetDisplayMode(void *t, u32 w, u32 h, u32 bpp, u32 rate, u32 flags) {
    DD *dd = (DD *) t;
    (void) rate; (void) flags;
    dd->w = (int) w; dd->h = (int) h; dd->bpp = (int) bpp;
    reasm_elf_log("[com] SetDisplayMode ");
    reasm_elf_log_hex((w << 16) | (h & 0xFFFF));
    reasm_elf_log_hex(bpp);
    reasm_elf_log("\n");
    if (host_elf_display_mode((int) w, (int) h, (int) bpp) != 0) return -1;
    sync_primary();
    return DD_OK;
}
S long d_WaitForVerticalBlank(void *t, u32 flags, void *ev) { (void) t; (void) flags; (void) ev; host_elf_delay(16); return DD_OK; }

/* ============================ costruzione ============================ */
static void init_vt(void) {
    if (surface_vt[0]) return;
    surface_vt[0] = s_QueryInterface; surface_vt[1] = s_AddRef; surface_vt[2] = s_Release;
    surface_vt[3] = s_AddAttachedSurface; surface_vt[4] = s_AddOverlayDirtyRect; surface_vt[5] = s_Blt;
    surface_vt[6] = s_BltBatch; surface_vt[7] = s_BltFast; surface_vt[8] = s_DeleteAttachedSurface;
    surface_vt[9] = s_EnumAttachedSurfaces; surface_vt[10] = s_EnumOverlayZOrders; surface_vt[11] = s_Flip;
    surface_vt[12] = s_GetAttachedSurface; surface_vt[13] = s_GetBltStatus; surface_vt[14] = s_GetCaps;
    surface_vt[15] = s_GetClipper; surface_vt[16] = s_GetColorKey; surface_vt[17] = s_GetDC;
    surface_vt[18] = s_GetFlipStatus; surface_vt[19] = s_GetOverlayPosition; surface_vt[20] = s_GetPalette;
    surface_vt[21] = s_GetPixelFormat; surface_vt[22] = s_GetSurfaceDesc; surface_vt[23] = s_Initialize;
    surface_vt[24] = s_IsLost; surface_vt[25] = s_Lock; surface_vt[26] = s_ReleaseDC;
    surface_vt[27] = s_Restore; surface_vt[28] = s_SetClipper; surface_vt[29] = s_SetColorKey;
    surface_vt[30] = s_SetOverlayPosition; surface_vt[31] = s_SetPalette; surface_vt[32] = s_Unlock;
    palette_vt[0] = p_QueryInterface; palette_vt[1] = p_AddRef; palette_vt[2] = p_Release;
    palette_vt[3] = p_GetCaps; palette_vt[4] = p_GetEntries; palette_vt[5] = p_Initialize;
    palette_vt[6] = p_SetEntries;
    clipper_vt[0] = c_QueryInterface; clipper_vt[1] = c_AddRef; clipper_vt[2] = c_Release;
    clipper_vt[3] = c_GetClipList; clipper_vt[4] = c_GetHWnd; clipper_vt[5] = c_Initialize;
    clipper_vt[6] = c_IsClipListChanged; clipper_vt[7] = c_SetClipList; clipper_vt[8] = c_SetHWnd;
    ddraw_vt[0] = d_QueryInterface; ddraw_vt[1] = d_AddRef; ddraw_vt[2] = d_Release;
    ddraw_vt[3] = d_Compact; ddraw_vt[4] = d_CreateClipper; ddraw_vt[5] = d_CreatePalette;
    ddraw_vt[6] = d_CreateSurface; ddraw_vt[7] = d_DuplicateSurface; ddraw_vt[8] = d_EnumDisplayModes;
    ddraw_vt[9] = d_EnumSurfaces; ddraw_vt[10] = d_FlipToGDISurface; ddraw_vt[11] = d_GetCaps;
    ddraw_vt[12] = d_GetDisplayMode; ddraw_vt[13] = d_GetFourCCCodes; ddraw_vt[14] = d_GetGDISurface;
    ddraw_vt[15] = d_GetMonitorFrequency; ddraw_vt[16] = d_GetScanLine; ddraw_vt[17] = d_GetVerticalBlankStatus;
    ddraw_vt[18] = d_Initialize; ddraw_vt[19] = d_RestoreDisplayMode; ddraw_vt[20] = d_SetCooperativeLevel;
    ddraw_vt[21] = d_SetDisplayMode; ddraw_vt[22] = d_WaitForVerticalBlank;
#if defined(__arm__) || defined(__i386__)
    {
        static const unsigned char sa[33] = {3,1,1,2,2,6,4,6,3,3,4,3,3,2,2,2,3,2,2,3,2,2,2,3,1,5,2,1,2,3,3,2,2};
        static const unsigned char pa[7] = {3,1,1,2,5,4,5};
        static const unsigned char ca[9] = {3,1,1,4,2,3,2,3,3};
        static const unsigned char da[23] = {3,1,1,1,4,5,4,3,5,5,1,3,2,3,2,2,2,2,2,1,3,6,3};
        arm_com_wrap_vtable(surface_vt, sa, 33);
        arm_com_wrap_vtable(palette_vt, pa, 7);
        arm_com_wrap_vtable(clipper_vt, ca, 9);
        arm_com_wrap_vtable(ddraw_vt, da, 23);
    }
#endif
}

long COM_STDCALL DirectDrawCreate(const void *guid, void **out, void *outer) {
    DD *dd;
    (void) guid; (void) outer;
    init_vt();
    dd = (DD *) malloc(sizeof(DD));
    if (!dd) return -1;
    memset(dd, 0, sizeof *dd);
    dd->vt = ddraw_vt; dd->refs = 1;
    dd->w = 640; dd->h = 480; dd->bpp = 8;      /* il percorso del gioco e' 8 bit + palette */
    g_last_dd = dd;
    if (out) *out = dd;
    return DD_OK;
}

long COM_STDCALL DirectDrawCreateClipper(unsigned long flags, void **out, void *outer) {
    Clipper *cl;
    (void) flags; (void) outer;
    init_vt();
    cl = (Clipper *) malloc(sizeof(Clipper));
    if (!cl) return -1;
    memset(cl, 0, sizeof *cl);
    cl->vt = clipper_vt; cl->refs = 1;
    if (out) *out = cl;
    return DD_OK;
}

void *com_ddraw_last(void) { return g_last_dd; }
void *com_ddraw_primary_surface(void) { return (g_last_dd && g_last_dd->primary) ? (void *) g_last_dd->primary : 0; }

void com_ddraw_shutdown(void) {
    DD *dd = g_last_dd;
    /* le superfici offscreen si liberano col Release del gioco; qui si liberano solo i buffer
     * posseduti da com_ddraw (mai il canvas del host) */
    if (dd) {
        if (dd->primary && dd->primary->owns_px) free(dd->primary->px);
        if (dd->back && dd->back->owns_px) free(dd->back->px);
        free(dd);
    }
    g_last_dd = 0;
    g_primary_pal = 0;
    g_live = 0;
    host_elf_shutdown();
}

/* com_unk e' mantenuta per il futuro: vedi commento in testa. Riferimento esplicito per evitare
 * che il compilatore la scarti silenziosamente quando serve in debug. */
void com_ddraw_unk_probe(int slot) { if (slot < 0) com_unk("?", slot); }
