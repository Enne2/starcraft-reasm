/* sc-reasm/tools/elf/gdi_elf.c — GDI nativo (8 bit + palette) per il runtime ELF (Q2b).
 *
 * Vedi gdi_elf.h per il contratto e la tabella delle arita'. Note di progetto:
 *
 *  - Handle = puntatore a un oggetto in un registro tipizzato (lista linkata). Mai un numero magico:
 *    un handle non valido e' un puntatore non riconosciuto e viene ignorato (no crash).
 *  - La superficie di destinazione si risolve AD OGNI operazione: per il DC finestra e' il canvas
 *    di host_elf (che puo' essere riallocato da un SetDisplayMode), per un DC memoria e' la bitmap
 *    selezionata (in Windows 1x1 di default).
 *  - Colore: su superficie a 8 bit `PALETTEINDEX(i)` (0x01xxxxxx) e' l'indice i; un COLORREF RGB
 *    viene mappato sulla voce di palette piu' vicina (come fa GDI su un device a palette).
 *  - CLIP: la regione di clip del DC e' un insieme di span (y, x0, x1); ogni scrittura di pixel la
 *    attraversa. Una regione NON e' quindi un rettangolo: fuori da essa non cambia nulla.
 *  - Font: bitmap interno 8x8/8x16 per ASCII 32..126 (vedi docs/15 per la provenienza).
 *
 * Modello di riferimento per le idee (read-only): ~/dev/lola-recomp/runtime/com/ddraw_shim.c
 * (com_gdi_text/fill/bitblt/setdibits, shim_set_text_color/bk_color/bk_mode, font6x12).
 */
#include "gdi_elf.h"
#include "host_elf.h"
#include "reasm_elf.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

extern void *malloc(unsigned long);
extern void *realloc(void *, unsigned long);
extern void free(void *);
extern void *memset(void *, int, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);

#ifndef NULL
#define NULL ((void *) 0)
#endif

/* ============================ oggetti ============================ */
enum { OBJ_DC = 1, OBJ_BITMAP, OBJ_FONT, OBJ_PEN, OBJ_BRUSH, OBJ_PALETTE, OBJ_REGION };

typedef struct GObj { int type; int is_stock; struct GObj *next; } GObj;

typedef struct {
    GObj o;
    u8 *px;
    int w, h, pitch, bpp;     /* bpp: 8 = indici, 24/32 = RGB */
    u32 pal[256];
    int has_pal;
} GBitmap;

typedef struct { int y, x0, x1; } RSpan;         /* [x0, x1) sulla riga y */

typedef struct {
    GObj o;
    RSpan *s;
    int n, cap;
} GRegion;

typedef struct { GObj o; int style, width; u32 color; } GPen;
typedef struct { GObj o; int style; u32 color; } GBrush;

typedef struct {
    GObj o;
    int cell_w, cell_h, ascent;
    int weight, charset, italic, underline, strike;
    int req_height;
    char face[32];
} GFont;

typedef struct {
    GObj o;
    u8 r[256], g[256], b[256], f[256];    /* PALETTEENTRY esplicite (byte R,G,B,flags) */
} GPalette;
#define PAL_RGB(p, i) (((u32) (p)->r[i] << 16) | ((u32) (p)->g[i] << 8) | (u32) (p)->b[i])

typedef struct {
    GObj o;
    /* superficie di destinazione risolta al volo */
    GBitmap *bm;                 /* bitmap selezionata (0 = nessuna) */
    int owns_bm;                 /* 1 = la bitmap di default del DC (1x1) e' nostra */
    int is_window;               /* 1 = canvas del host */
    GFont *font;
    GPen *pen;
    GBrush *brush;
    GPalette *pal;
    GRegion *clip;               /* 0 = nessun clip */
    u32 text_color, bk_color;
    int bk_mode;                 /* TRANSPARENT 1 / OPAQUE 2 */
    u32 text_align;
    int cur_x, cur_y;
    int pal_realized;
} GDC;

/* registro globale (lista linkata: basta per centinaia di oggetti) */
static GObj *g_objs;
static GDC *g_win_dc;
static GFont *g_default_font;
static int g_inited;

static void *obj_new(int type, unsigned long size) {
    GObj *o = (GObj *) malloc(size);
    if (!o) return NULL;
    memset(o, 0, size);
    o->type = type;
    o->next = g_objs;
    g_objs = o;
    return o;
}
static void obj_unlink(GObj *o) {
    GObj **p = &g_objs;
    while (*p && *p != o) p = &(*p)->next;
    if (*p) *p = o->next;
}
static int obj_valid(const void *h, int type) {
    const GObj *o = (const GObj *) h;
    if (!o) return 0;
    { const GObj *q = g_objs; while (q) { if (q == o) return (q->type == type); q = q->next; } }
    return 0;
}

/* ============================ font bitmap interno ============================
 * Tabelle incorporate per ASCII 32..126 (95 glifi), generate dai font console PSF del sistema.
 * Provenienza e comando di rigenerazione: docs/15-GDI-ELF.md. */
static const unsigned char g_font8x16[95][16] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 32 ' ' */
    { 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 33 '!' */
    { 0x00, 0x66, 0x22, 0x22, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 34 '"' */
    { 0x00, 0x00, 0x00, 0x12, 0x12, 0x12, 0x7e, 0x24, 0x24, 0x7e, 0x48, 0x48, 0x48, 0x00, 0x00, 0x00 },   /* 35 '#' */
    { 0x00, 0x00, 0x00, 0x08, 0x3e, 0x49, 0x48, 0x38, 0x0e, 0x09, 0x49, 0x3e, 0x08, 0x00, 0x00, 0x00 },   /* 36 '$' */
    { 0x00, 0x00, 0x00, 0x31, 0x4a, 0x4a, 0x34, 0x08, 0x08, 0x16, 0x29, 0x29, 0x46, 0x00, 0x00, 0x00 },   /* 37 '%' */
    { 0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x22, 0x1c, 0x39, 0x45, 0x42, 0x46, 0x39, 0x00, 0x00, 0x00 },   /* 38 '&' */
    { 0x00, 0x18, 0x08, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 39 "'" */
    { 0x00, 0x00, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x08, 0x08, 0x04, 0x00, 0x00 },   /* 40 '(' */
    { 0x00, 0x00, 0x20, 0x10, 0x10, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x10, 0x20, 0x00, 0x00 },   /* 41 ')' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x49, 0x2a, 0x1c, 0x2a, 0x49, 0x08, 0x00, 0x00, 0x00, 0x00 },   /* 42 '*' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x7f, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00 },   /* 43 '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x08, 0x08, 0x10, 0x00 },   /* 44 ',' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 45 '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00 },   /* 46 '.' */
    { 0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x40, 0x40, 0x00, 0x00, 0x00 },   /* 47 '/' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x4a, 0x52, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 48 '0' */
    { 0x00, 0x00, 0x00, 0x08, 0x18, 0x28, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3e, 0x00, 0x00, 0x00 },   /* 49 '1' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x02, 0x0c, 0x10, 0x20, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00 },   /* 50 '2' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x02, 0x1c, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 51 '3' */
    { 0x00, 0x00, 0x00, 0x04, 0x0c, 0x14, 0x24, 0x44, 0x44, 0x7e, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00 },   /* 52 '4' */
    { 0x00, 0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x7c, 0x02, 0x02, 0x02, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 53 '5' */
    { 0x00, 0x00, 0x00, 0x1c, 0x20, 0x40, 0x40, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 54 '6' */
    { 0x00, 0x00, 0x00, 0x7e, 0x02, 0x02, 0x04, 0x04, 0x04, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 55 '7' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 56 '8' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x02, 0x02, 0x04, 0x38, 0x00, 0x00, 0x00 },   /* 57 '9' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 },   /* 58 ':' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x08, 0x08, 0x10, 0x00, 0x00 },   /* 59 ';' */
    { 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00 },   /* 60 '<' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 61 '=' */
    { 0x00, 0x00, 0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00, 0x00 },   /* 62 '>' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x02, 0x04, 0x08, 0x08, 0x00, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 63 '?' */
    { 0x00, 0x00, 0x00, 0x1c, 0x22, 0x4a, 0x56, 0x52, 0x52, 0x52, 0x4e, 0x20, 0x1e, 0x00, 0x00, 0x00 },   /* 64 '@' */
    { 0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 65 'A' */
    { 0x00, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x7c, 0x00, 0x00, 0x00 },   /* 66 'B' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x40, 0x40, 0x40, 0x40, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 67 'C' */
    { 0x00, 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00, 0x00, 0x00 },   /* 68 'D' */
    { 0x00, 0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00 },   /* 69 'E' */
    { 0x00, 0x00, 0x00, 0x7e, 0x40, 0x40, 0x40, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00 },   /* 70 'F' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x40, 0x40, 0x4e, 0x42, 0x42, 0x46, 0x3a, 0x00, 0x00, 0x00 },   /* 71 'G' */
    { 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 72 'H' */
    { 0x00, 0x00, 0x00, 0x3e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3e, 0x00, 0x00, 0x00 },   /* 73 'I' */
    { 0x00, 0x00, 0x00, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00 },   /* 74 'J' */
    { 0x00, 0x00, 0x00, 0x42, 0x44, 0x48, 0x50, 0x60, 0x60, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00 },   /* 75 'K' */
    { 0x00, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00 },   /* 76 'L' */
    { 0x00, 0x00, 0x00, 0x42, 0x42, 0x66, 0x7e, 0x5a, 0x5a, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 77 'M' */
    { 0x00, 0x00, 0x00, 0x42, 0x62, 0x62, 0x52, 0x52, 0x4a, 0x4a, 0x46, 0x46, 0x42, 0x00, 0x00, 0x00 },   /* 78 'N' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 79 'O' */
    { 0x00, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00 },   /* 80 'P' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x5a, 0x66, 0x3c, 0x03, 0x00, 0x00 },   /* 81 'Q' */
    { 0x00, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x7c, 0x48, 0x44, 0x44, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 82 'R' */
    { 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x40, 0x30, 0x0c, 0x02, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 83 'S' */
    { 0x00, 0x00, 0x00, 0x7f, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 84 'T' */
    { 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 85 'U' */
    { 0x00, 0x00, 0x00, 0x41, 0x41, 0x41, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 86 'V' */
    { 0x00, 0x00, 0x00, 0x82, 0x82, 0x82, 0x82, 0x92, 0x92, 0x92, 0x92, 0x6c, 0x28, 0x00, 0x00, 0x00 },   /* 87 'W' */
    { 0x00, 0x00, 0x00, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x24, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 88 'X' */
    { 0x00, 0x00, 0x00, 0x41, 0x41, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00 },   /* 89 'Y' */
    { 0x00, 0x00, 0x00, 0x7e, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00 },   /* 90 'Z' */
    { 0x00, 0x00, 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00, 0x00 },   /* 91 '[' */
    { 0x00, 0x00, 0x00, 0x40, 0x40, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x00, 0x00, 0x00 },   /* 92 '\\' */
    { 0x00, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x70, 0x00, 0x00 },   /* 93 ']' */
    { 0x00, 0x18, 0x24, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 94 '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00 },   /* 95 '_' */
    { 0x00, 0x18, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 96 '`' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x02, 0x3e, 0x42, 0x42, 0x46, 0x3a, 0x00, 0x00, 0x00 },   /* 97 'a' */
    { 0x00, 0x00, 0x40, 0x40, 0x40, 0x5c, 0x62, 0x42, 0x42, 0x42, 0x42, 0x62, 0x5c, 0x00, 0x00, 0x00 },   /* 98 'b' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x40, 0x40, 0x40, 0x40, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 99 'c' */
    { 0x00, 0x00, 0x02, 0x02, 0x02, 0x3a, 0x46, 0x42, 0x42, 0x42, 0x42, 0x46, 0x3a, 0x00, 0x00, 0x00 },   /* 100 'd' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x7e, 0x40, 0x40, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 101 'e' */
    { 0x00, 0x00, 0x0c, 0x10, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00 },   /* 102 'f' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x46, 0x42, 0x42, 0x42, 0x46, 0x3a, 0x02, 0x02, 0x02, 0x3c },   /* 103 'g' */
    { 0x00, 0x00, 0x40, 0x40, 0x40, 0x5c, 0x62, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 104 'h' */
    { 0x00, 0x00, 0x00, 0x18, 0x00, 0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00, 0x00 },   /* 105 'i' */
    { 0x00, 0x00, 0x04, 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x48, 0x30 },   /* 106 'j' */
    { 0x00, 0x00, 0x40, 0x40, 0x40, 0x44, 0x48, 0x50, 0x60, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00 },   /* 107 'k' */
    { 0x00, 0x00, 0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x00, 0x00 },   /* 108 'l' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x00, 0x00, 0x00 },   /* 109 'm' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x62, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 110 'n' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 111 'o' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x62, 0x42, 0x42, 0x42, 0x62, 0x5c, 0x40, 0x40, 0x40, 0x40 },   /* 112 'p' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x46, 0x42, 0x42, 0x42, 0x46, 0x3a, 0x02, 0x02, 0x02, 0x02 },   /* 113 'q' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x62, 0x42, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00 },   /* 114 'r' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x40, 0x30, 0x0c, 0x02, 0x42, 0x3c, 0x00, 0x00, 0x00 },   /* 115 's' */
    { 0x00, 0x00, 0x10, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00, 0x00 },   /* 116 't' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x46, 0x3a, 0x00, 0x00, 0x00 },   /* 117 'u' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x24, 0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00 },   /* 118 'v' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00 },   /* 119 'w' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00 },   /* 120 'x' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x26, 0x1a, 0x02, 0x02, 0x02, 0x3c },   /* 121 'y' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7e, 0x00, 0x00, 0x00 },   /* 122 'z' */
    { 0x00, 0x00, 0x0c, 0x10, 0x10, 0x10, 0x10, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00 },   /* 123 '{' */
    { 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00 },   /* 124 '|' */
    { 0x00, 0x00, 0x30, 0x08, 0x08, 0x08, 0x08, 0x0c, 0x08, 0x08, 0x08, 0x08, 0x08, 0x30, 0x00, 0x00 },   /* 125 '}' */
    { 0x00, 0x00, 0x31, 0x49, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 126 '~' */
};

static const unsigned char g_font8x8[95][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 32 ' ' */
    { 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x18, 0x00, 0x18 },   /* 33 '!' */
    { 0x00, 0xee, 0x66, 0xcc, 0x00, 0x00, 0x00, 0x00 },   /* 34 '"' */
    { 0x00, 0x6c, 0xfe, 0x6c, 0x6c, 0x6c, 0xfe, 0x6c },   /* 35 '#' */
    { 0x18, 0x7e, 0xdb, 0xd8, 0x7e, 0x1b, 0xdb, 0x7e },   /* 36 '$' */
    { 0x00, 0x00, 0xc6, 0xcc, 0x18, 0x30, 0x66, 0xc6 },   /* 37 '%' */
    { 0x00, 0x38, 0x6c, 0x3a, 0x7e, 0xcc, 0xcc, 0x76 },   /* 38 '&' */
    { 0x00, 0x38, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00 },   /* 39 "'" */
    { 0x00, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0c },   /* 40 '(' */
    { 0x00, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x18, 0x30 },   /* 41 ')' */
    { 0x00, 0x00, 0x6c, 0x38, 0xfe, 0x38, 0x6c, 0x00 },   /* 42 '*' */
    { 0x00, 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00 },   /* 43 '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x18, 0x70 },   /* 44 ',' */
    { 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00 },   /* 45 '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18 },   /* 46 '.' */
    { 0x00, 0x06, 0x0c, 0x18, 0x18, 0x30, 0x60, 0xc0 },   /* 47 '/' */
    { 0x00, 0x7c, 0xc6, 0xce, 0xd6, 0xe6, 0xc6, 0x7c },   /* 48 '0' */
    { 0x00, 0x18, 0x78, 0x18, 0x18, 0x18, 0x18, 0x7e },   /* 49 '1' */
    { 0x00, 0x7c, 0xc6, 0x8c, 0x38, 0x60, 0xc2, 0xfe },   /* 50 '2' */
    { 0x00, 0x7c, 0xc6, 0x06, 0x3c, 0x06, 0xc6, 0x7c },   /* 51 '3' */
    { 0x00, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x1e },   /* 52 '4' */
    { 0x00, 0xfe, 0xc0, 0xfc, 0x06, 0x06, 0xc6, 0x7c },   /* 53 '5' */
    { 0x00, 0x7c, 0xc6, 0xc0, 0xfc, 0xc6, 0xc6, 0x7c },   /* 54 '6' */
    { 0x00, 0xfe, 0xc6, 0x06, 0x0c, 0x18, 0x30, 0x30 },   /* 55 '7' */
    { 0x00, 0x7c, 0xc6, 0xc6, 0x7c, 0xc6, 0xc6, 0x7c },   /* 56 '8' */
    { 0x00, 0x7c, 0xc6, 0xc6, 0x7e, 0x06, 0xc6, 0x7c },   /* 57 '9' */
    { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00 },   /* 58 ':' */
    { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x70 },   /* 59 ';' */
    { 0x00, 0x00, 0x18, 0x30, 0x60, 0x30, 0x18, 0x00 },   /* 60 '<' */
    { 0x00, 0x00, 0x00, 0xfe, 0x00, 0xfe, 0x00, 0x00 },   /* 61 '=' */
    { 0x00, 0x00, 0x30, 0x18, 0x0c, 0x18, 0x30, 0x00 },   /* 62 '>' */
    { 0x00, 0x7c, 0xc6, 0xcc, 0x18, 0x18, 0x00, 0x18 },   /* 63 '?' */
    { 0x00, 0x7c, 0xc6, 0xc6, 0xde, 0xdc, 0xc0, 0x7e },   /* 64 '@' */
    { 0x00, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6 },   /* 65 'A' */
    { 0x00, 0xfc, 0x66, 0x66, 0x7c, 0x66, 0x66, 0xfc },   /* 66 'B' */
    { 0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc2, 0x66, 0x3c },   /* 67 'C' */
    { 0x00, 0xf8, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0xf8 },   /* 68 'D' */
    { 0x00, 0xfe, 0x66, 0x60, 0x7c, 0x60, 0x66, 0xfe },   /* 69 'E' */
    { 0x00, 0xfe, 0x66, 0x60, 0x7c, 0x60, 0x60, 0xf0 },   /* 70 'F' */
    { 0x00, 0x7c, 0xc6, 0xc0, 0xce, 0xc6, 0xc6, 0x7c },   /* 71 'G' */
    { 0x00, 0xc6, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6 },   /* 72 'H' */
    { 0x00, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c },   /* 73 'I' */
    { 0x00, 0x3c, 0x18, 0x18, 0x18, 0xd8, 0xd8, 0x70 },   /* 74 'J' */
    { 0x00, 0xc6, 0xcc, 0xd8, 0xf0, 0xd8, 0xcc, 0xc6 },   /* 75 'K' */
    { 0x00, 0xf0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xfe },   /* 76 'L' */
    { 0x00, 0xc6, 0xc6, 0xee, 0xfe, 0xd6, 0xc6, 0xc6 },   /* 77 'M' */
    { 0x00, 0xc6, 0xe6, 0xf6, 0xde, 0xce, 0xc6, 0xc6 },   /* 78 'N' */
    { 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c },   /* 79 'O' */
    { 0x00, 0xfc, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xf0 },   /* 80 'P' */
    { 0x00, 0x7c, 0xc6, 0xc6, 0xd6, 0xd6, 0x7c, 0x06 },   /* 81 'Q' */
    { 0x00, 0xfc, 0x66, 0x66, 0x7c, 0x78, 0x66, 0xe6 },   /* 82 'R' */
    { 0x00, 0x7c, 0xc6, 0xc0, 0x7c, 0x06, 0xc6, 0x7c },   /* 83 'S' */
    { 0x00, 0xff, 0xdb, 0x99, 0x18, 0x18, 0x18, 0x3c },   /* 84 'T' */
    { 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c },   /* 85 'U' */
    { 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0x6c, 0x38, 0x10 },   /* 86 'V' */
    { 0x00, 0xc6, 0xc6, 0xd6, 0xd6, 0xfe, 0xee, 0x6c },   /* 87 'W' */
    { 0x00, 0xc6, 0xc6, 0x6c, 0x38, 0x6c, 0xc6, 0xc6 },   /* 88 'X' */
    { 0x00, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x3c },   /* 89 'Y' */
    { 0x00, 0xfe, 0xc6, 0x8c, 0x38, 0x62, 0xc6, 0xfe },   /* 90 'Z' */
    { 0x00, 0x7c, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7c },   /* 91 '[' */
    { 0x00, 0xc0, 0x60, 0x30, 0x30, 0x18, 0x0c, 0x06 },   /* 92 '\\' */
    { 0x00, 0x7c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x7c },   /* 93 ']' */
    { 0x00, 0x38, 0x6c, 0xc6, 0x00, 0x00, 0x00, 0x00 },   /* 94 '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff },   /* 95 '_' */
    { 0x00, 0x38, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00 },   /* 96 '`' */
    { 0x00, 0x00, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0x76 },   /* 97 'a' */
    { 0x00, 0xe0, 0x60, 0x7c, 0x66, 0x66, 0x66, 0xfc },   /* 98 'b' */
    { 0x00, 0x00, 0x00, 0x78, 0xcc, 0xc0, 0xcc, 0x78 },   /* 99 'c' */
    { 0x00, 0x0e, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x7e },   /* 100 'd' */
    { 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0x7c },   /* 101 'e' */
    { 0x00, 0x1c, 0x36, 0x30, 0xfc, 0x30, 0x30, 0x78 },   /* 102 'f' */
    { 0x00, 0x00, 0x76, 0xcc, 0xcc, 0x7c, 0x0c, 0xf8 },   /* 103 'g' */
    { 0x00, 0x00, 0xe0, 0x60, 0x7c, 0x66, 0x66, 0xe6 },   /* 104 'h' */
    { 0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3c },   /* 105 'i' */
    { 0x0c, 0x0c, 0x00, 0x1c, 0x0c, 0x0c, 0xcc, 0x78 },   /* 106 'j' */
    { 0x00, 0xe0, 0x60, 0x66, 0x6c, 0x78, 0x6c, 0xe6 },   /* 107 'k' */
    { 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c },   /* 108 'l' */
    { 0x00, 0x00, 0x00, 0xec, 0xfe, 0xd6, 0xd6, 0xc6 },   /* 109 'm' */
    { 0x00, 0x00, 0x00, 0xdc, 0x66, 0x66, 0x66, 0x66 },   /* 110 'n' */
    { 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7c },   /* 111 'o' */
    { 0x00, 0x00, 0xdc, 0x66, 0x66, 0x7c, 0x60, 0xf0 },   /* 112 'p' */
    { 0x00, 0x00, 0x76, 0xcc, 0xcc, 0x7c, 0x0e, 0x0e },   /* 113 'q' */
    { 0x00, 0x00, 0x00, 0xdc, 0x66, 0x60, 0x60, 0xf0 },   /* 114 'r' */
    { 0x00, 0x00, 0x00, 0x7c, 0xc0, 0x7c, 0x06, 0xfc },   /* 115 's' */
    { 0x00, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x36, 0x1c },   /* 116 't' */
    { 0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x76 },   /* 117 'u' */
    { 0x00, 0x00, 0x00, 0xc6, 0xc6, 0x6c, 0x38, 0x10 },   /* 118 'v' */
    { 0x00, 0x00, 0x00, 0xc6, 0xd6, 0xd6, 0xfe, 0x6c },   /* 119 'w' */
    { 0x00, 0x00, 0x00, 0xc6, 0x6c, 0x38, 0x6c, 0xc6 },   /* 120 'x' */
    { 0x00, 0x00, 0x00, 0xcc, 0xcc, 0x7c, 0x0c, 0xf8 },   /* 121 'y' */
    { 0x00, 0x00, 0x00, 0xfe, 0x8c, 0x38, 0x62, 0xfe },   /* 122 'z' */
    { 0x00, 0x0e, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0e },   /* 123 '{' */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },   /* 124 '|' */
    { 0x00, 0x70, 0x18, 0x18, 0x0e, 0x18, 0x18, 0x70 },   /* 125 '}' */
    { 0x00, 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 126 '~' */
};


static const u8 *font_glyph(const GFont *f, int ch) {
    if (ch < 32 || ch > 126) ch = '?';
    if (f && f->cell_h <= 8) return g_font8x8[ch - 32];
    return g_font8x16[ch - 32];
}

static GFont *font_make(int height, int weight, int charset, int italic, int underline, int strike, const char *face) {
    GFont *f = (GFont *) obj_new(OBJ_FONT, sizeof(GFont));
    int i;
    if (!f) return NULL;
    if (height == 0) height = 16;
    if (height < 0) height = -height;
    f->req_height = height;
    f->cell_h = (height > 10) ? 16 : 8;      /* due taglie disponibili: 8x8 e 8x16 */
    f->cell_w = 8;
    f->ascent = f->cell_h - 2;
    f->weight = weight;
    f->charset = charset;
    f->italic = italic ? 1 : 0;
    f->underline = underline ? 1 : 0;
    f->strike = strike ? 1 : 0;
    if (face) { for (i = 0; i < 31 && face[i]; ++i) f->face[i] = face[i]; f->face[i] = 0; }
    return f;
}

/* ============================ superficie ============================ */
static int dc_surface(GDC *dc, u8 **px, int *w, int *h, int *pitch, int *bpp) {
    if (dc->is_window) {
        int cw = 0, ch = 0, cp = 0;
        u8 *c = host_elf_canvas(&cw, &ch, &cp);
        if (!c || cw <= 0 || ch <= 0) return 0;
        if (px) *px = c;
        if (w) *w = cw;
        if (h) *h = ch;
        if (pitch) *pitch = cp;
        if (bpp) *bpp = 8;
        return 1;
    }
    if (dc->bm && dc->bm->px) {
        if (px) *px = dc->bm->px;
        if (w) *w = dc->bm->w;
        if (h) *h = dc->bm->h;
        if (pitch) *pitch = dc->bm->pitch;
        if (bpp) *bpp = dc->bm->bpp;
        return 1;
    }
    return 0;
}

/* scrittura di un pixel nel formato della superficie (8 = indice, 24/32 = RGB) */
static void put_px(u8 *px, int pitch, int bpp, int x, int y, u32 v) {
    u8 *p = px + (unsigned long) y * pitch;
    if (bpp == 8) p[x] = (u8) (v & 0xFF);
    else if (bpp == 24) { p[x*3] = (u8) (v & 0xFF); p[x*3+1] = (u8) ((v >> 8) & 0xFF); p[x*3+2] = (u8) ((v >> 16) & 0xFF); }
    else *(u32 *) (p + x * 4) = v;
}
static u32 get_px(const u8 *px, int pitch, int bpp, int x, int y) {
    const u8 *p = px + (unsigned long) y * pitch;
    if (bpp == 8) return p[x];
    if (bpp == 24) return (u32) p[x*3] | ((u32) p[x*3+1] << 8) | ((u32) p[x*3+2] << 16);
    return *(const u32 *) (p + x * 4);
}

/* ============================ colore ============================ */
/* Palette effettiva del DC: quella selezionata, altrimenti quella del host (canvas 8 bit). */
static int dc_palette_rgb(GDC *dc, int i, u32 *out) {
    if (dc->pal) { *out = PAL_RGB(dc->pal, i & 0xFF); return 1; }
    if (dc->bm && dc->bm->has_pal) { *out = dc->bm->pal[i & 0xFF] & 0xFFFFFFu; return 1; }
    { u32 p[256];
      if (host_elf_get_palette(p, 0, 256) == 0) { *out = p[i & 0xFF] & 0xFFFFFFu; return 1; } }
    *out = 0; return 0;
}

/* COLORREF -> indice (superficie a 8 bit) oppure RGB (superficie diretta). */
static u32 dc_color(GDC *dc, u32 cref) {
    int bpp = 8;
    int is_index = (cref & 0x01000000u) != 0;      /* PALETTEINDEX(i) */
    u32 rgb = cref & 0x00FFFFFFu;
    { u8 *px; int w, h, pitch; if (!dc_surface(dc, &px, &w, &h, &pitch, &bpp)) bpp = 8; }
    if (is_index) {
        int i = (int) (cref & 0xFF);
        if (bpp == 8) return (u32) i;
        if (!dc_palette_rgb(dc, i, &rgb)) return 0;
        return rgb;
    }
    if (bpp != 8) return rgb;
    /* RGB su superficie a palette: voce piu' vicina (match esatto se c'e') */
    {
        int i, best = 0; long bestd = -1;
        for (i = 0; i < 256; ++i) {
            u32 c;
            if (!dc_palette_rgb(dc, i, &c)) break;
            if (c == rgb) return (u32) i;
            { long dr = (long) ((c >> 16) & 0xFF) - (long) ((rgb >> 16) & 0xFF);
              long dg = (long) ((c >> 8) & 0xFF) - (long) ((rgb >> 8) & 0xFF);
              long db = (long) (c & 0xFF) - (long) (rgb & 0xFF);
              long d = dr * dr + dg * dg + db * db;
              if (bestd < 0 || d < bestd) { bestd = d; best = i; } }
        }
        return (u32) best;
    }
}

/* ============================ regioni (span) ============================ */
#define region_n(r) ((r) ? (r)->n : 0)

static int region_reserve(GRegion *r, int need) {
    if (need <= r->cap) return 1;
    {
        int cap = r->cap ? r->cap : 16;
        RSpan *ns;
        while (cap < need) cap *= 2;
        ns = (RSpan *) realloc(r->s, (unsigned long) cap * sizeof(RSpan));
        if (!ns) return 0;
        r->s = ns; r->cap = cap;
    }
    return 1;
}

/* inserimento ordinato (y, x0) con fusione di span adiacenti/sovrapposti sulla stessa riga */
static void region_add_span(GRegion *r, int y, int x0, int x1) {
    int i, j;
    if (x1 <= x0) return;
    if (!region_reserve(r, r->n + 1)) return;
    i = 0;
    while (i < r->n && (r->s[i].y < y || (r->s[i].y == y && r->s[i].x0 <= x0))) ++i;
    for (j = r->n; j > i; --j) r->s[j] = r->s[j - 1];
    r->s[i].y = y; r->s[i].x0 = x0; r->s[i].x1 = x1;
    ++r->n;
    /* fusione con il precedente e con il successivo sulla stessa riga */
    for (i = 0; i + 1 < r->n; ++i) {
        while (i + 1 < r->n && r->s[i].y == r->s[i+1].y && r->s[i+1].x0 <= r->s[i].x1) {
            if (r->s[i+1].x1 > r->s[i].x1) r->s[i].x1 = r->s[i+1].x1;
            for (j = i + 1; j + 1 < r->n; ++j) r->s[j] = r->s[j+1];
            --r->n;
        }
    }
}
static void region_clear(GRegion *r) { if (r) r->n = 0; }
static void region_rect(GRegion *r, int x0, int y0, int x1, int y1) {
    int y;
    if (!r) return;
    region_clear(r);
    if (x1 <= x0 || y1 <= y0) return;
    for (y = y0; y < y1; ++y) region_add_span(r, y, x0, x1);
}

static int region_is_rect(GRegion *r) {
    int i;
    if (!r || r->n == 0) return 1;
    for (i = 1; i < r->n; ++i) {
        if (r->s[i].y != r->s[i-1].y + 1) return 0;
        if (r->s[i].x0 != r->s[0].x0 || r->s[i].x1 != r->s[0].x1) return 0;
    }
    return 1;
}
static int region_type(GRegion *r) {
    if (!r || r->n == 0) return 1;                 /* NULLREGION */
    if (region_is_rect(r)) return 2;               /* SIMPLEREGION */
    return 3;                                      /* COMPLEXREGION */
}

static int region_contains(GRegion *r, int x, int y) {
    int i;
    if (!r || r->n == 0) return 0;
    for (i = 0; i < r->n; ++i)
        if (r->s[i].y == y && x >= r->s[i].x0 && x < r->s[i].x1) return 1;
    return 0;
}
/* copertura completa di [x0,x1) sulla riga y da parte dell'unione degli span */
static int region_covers_row(GRegion *r, int y, int x0, int x1) {
    int i, cur = x0;
    if (!r || r->n == 0) return 0;
    for (i = 0; i < r->n && cur < x1; ++i) {
        if (r->s[i].y != y) continue;
        if (r->s[i].x1 <= cur) continue;
        if (r->s[i].x0 > cur) return 0;
        if (r->s[i].x1 > cur) cur = r->s[i].x1;
    }
    return cur >= x1;
}
static int region_rect_in(GRegion *r, int x0, int y0, int x1, int y1) {
    int y;
    if (x1 <= x0 || y1 <= y0) return 0;
    for (y = y0; y < y1; ++y) if (!region_covers_row(r, y, x0, x1)) return 0;
    return 1;
}

static void region_copy_from(GRegion *dst, GRegion *src) {
    region_clear(dst);
    if (src && src->n) { if (region_reserve(dst, src->n)) { memcpy(dst->s, src->s, (unsigned long) src->n * sizeof(RSpan)); dst->n = src->n; } }
}
static void region_and(GRegion *dst, GRegion *a, GRegion *b) {
    GRegion out; int i, j;
    memset(&out, 0, sizeof out);
    for (i = 0; i < region_n(a); ++i)
        for (j = 0; j < region_n(b); ++j)
            if (a->s[i].y == b->s[j].y) {
                int x0 = a->s[i].x0 > b->s[j].x0 ? a->s[i].x0 : b->s[j].x0;
                int x1 = a->s[i].x1 < b->s[j].x1 ? a->s[i].x1 : b->s[j].x1;
                if (x1 > x0) region_add_span(&out, a->s[i].y, x0, x1);
            }
    region_copy_from(dst, &out);
    if (out.s) free(out.s);
}
static void region_or(GRegion *dst, GRegion *a, GRegion *b) {
    GRegion out; int i;
    memset(&out, 0, sizeof out);
    for (i = 0; i < region_n(a); ++i) region_add_span(&out, a->s[i].y, a->s[i].x0, a->s[i].x1);
    for (i = 0; i < region_n(b); ++i) region_add_span(&out, b->s[i].y, b->s[i].x0, b->s[i].x1);
    region_copy_from(dst, &out);
    if (out.s) free(out.s);
}
static void region_diff(GRegion *dst, GRegion *a, GRegion *b) {
    GRegion out; int i, j;
    memset(&out, 0, sizeof out);
    for (i = 0; i < region_n(a); ++i) {
        int cur = a->s[i].x0;
        for (j = 0; j < region_n(b); ++j) {
            if (b->s[j].y != a->s[i].y) continue;
            if (b->s[j].x1 <= cur) continue;
            if (b->s[j].x0 >= a->s[i].x1) break;
            if (b->s[j].x0 > cur) region_add_span(&out, a->s[i].y, cur, b->s[j].x0);
            if (b->s[j].x1 > cur) cur = b->s[j].x1;
            if (cur >= a->s[i].x1) break;
        }
        if (cur < a->s[i].x1) region_add_span(&out, a->s[i].y, cur, a->s[i].x1);
    }
    region_copy_from(dst, &out);
    if (out.s) free(out.s);
}
static void region_xor(GRegion *dst, GRegion *a, GRegion *b) {
    GRegion u, n, out;
    memset(&u, 0, sizeof u); memset(&n, 0, sizeof n); memset(&out, 0, sizeof out);
    region_or(&u, a, b);
    region_and(&n, a, b);
    region_diff(&out, &u, &n);
    region_copy_from(dst, &out);
    if (u.s) free(u.s);
    if (n.s) free(n.s);
    if (out.s) free(out.s);
}

/* span ammessi sulla riga y per [x0,x1) (intersezione con il clip del DC e con la superficie) */
static int clip_spans(GDC *dc, int y, int x0, int x1, int sw, RSpan *out, int maxout) {
    int n = 0, i;
    if (x0 < 0) x0 = 0;
    if (x1 > sw) x1 = sw;
    if (x1 <= x0) return 0;
    if (!dc->clip || dc->clip->n == 0) {
        if (dc->clip) return 0;                 /* regione vuota = clip totale */
        out[0].y = y; out[0].x0 = x0; out[0].x1 = x1;
        return 1;
    }
    for (i = 0; i < dc->clip->n && n < maxout; ++i) {
        int a, b;
        if (dc->clip->s[i].y != y) continue;
        a = dc->clip->s[i].x0 > x0 ? dc->clip->s[i].x0 : x0;
        b = dc->clip->s[i].x1 < x1 ? dc->clip->s[i].x1 : x1;
        if (b > a) { out[n].y = y; out[n].x0 = a; out[n].x1 = b; ++n; }
    }
    return n;
}

/* riempimento di un rettangolo con il colore/indice v, rispettando il clip */
static void fill_rect_raw(GDC *dc, u8 *px, int w, int h, int pitch, int bpp,
                           int x0, int y0, int x1, int y1, u32 v) {
    int y;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 <= x0 || y1 <= y0) return;
    for (y = y0; y < y1; ++y) {
        RSpan sp[64]; int n = clip_spans(dc, y, x0, x1, w, sp, 64), k;
        for (k = 0; k < n; ++k) {
            if (bpp == 8) memset(px + (unsigned long) y * pitch + sp[k].x0, (int) (v & 0xFF),
                                 (unsigned long) (sp[k].x1 - sp[k].x0));
            else { int x; for (x = sp[k].x0; x < sp[k].x1; ++x) put_px(px, pitch, bpp, x, y, v); }
        }
    }
}

/* ============================ present ============================ */
static void dc_present(GDC *dc) { if (dc->is_window) host_elf_present(); }

/* ============================ palette ============================ */
static void palette_to_host(GPalette *p) {
    u32 tmp[256]; int i;
    if (!p) return;
    for (i = 0; i < 256; ++i) tmp[i] = PAL_RGB(p, i);
    host_elf_set_palette(tmp, 0, 256);
}

/* ============================ stock objects ============================ */
static GObj *g_stock[32];

static void stock_init(void) {
    static const struct { int idx; u32 color; } brushes[] = {
        { 0, 0xFFFFFFu }, { 1, 0xC0C0C0u }, { 2, 0x808080u }, { 3, 0x404040u }, { 4, 0x000000u }
    };
    static const struct { int idx; u32 color; } pens[] = { { 6, 0xFFFFFFu }, { 7, 0x000000u } };
    unsigned i;
    for (i = 0; i < sizeof brushes / sizeof brushes[0]; ++i) {
        GBrush *b = (GBrush *) obj_new(OBJ_BRUSH, sizeof(GBrush));
        if (!b) return;
        b->color = brushes[i].color; b->style = 0; b->o.is_stock = 1;
        g_stock[brushes[i].idx] = (GObj *) b;
    }
    for (i = 0; i < sizeof pens / sizeof pens[0]; ++i) {
        GPen *p = (GPen *) obj_new(OBJ_PEN, sizeof(GPen));
        if (!p) return;
        p->color = pens[i].color; p->width = 1; p->style = 0; p->o.is_stock = 1;
        g_stock[pens[i].idx] = (GObj *) p;
    }
    {   GBrush *b = (GBrush *) obj_new(OBJ_BRUSH, sizeof(GBrush));
        if (b) { b->style = 1 /*BS_NULL*/; b->o.is_stock = 1; g_stock[5] = (GObj *) b; } }
    {   GPen *p = (GPen *) obj_new(OBJ_PEN, sizeof(GPen));
        if (p) { p->style = 5 /*PS_NULL*/; p->o.is_stock = 1; g_stock[8] = (GObj *) p; } }
    {   int i;
        for (i = 10; i <= 14; ++i) { GFont *f = font_make(i == 10 ? 8 : 16, 400, 0, 0, 0, 0, "System");
            if (f) { f->o.is_stock = 1; g_stock[i] = (GObj *) f; } } }
    {   GPalette *p = (GPalette *) obj_new(OBJ_PALETTE, sizeof(GPalette));
        if (p) { int i; for (i = 0; i < 256; ++i) { p->r[i] = (u8) i; p->g[i] = (u8) i; p->b[i] = (u8) i; }
                 p->o.is_stock = 1; g_stock[15] = (GObj *) p; g_stock[16] = (GObj *) p; } }
    {   GFont *f = font_make(16, 400, 0, 0, 0, 0, "System");
        if (f) { f->o.is_stock = 1; g_stock[17] = (GObj *) f; } }
    g_stock[18] = g_stock[5];       /* DC_BRUSH: il DC ha il suo pennello */
    g_stock[19] = g_stock[8];       /* DC_PEN */
}

/* ============================ ciclo di vita ============================ */
int gdi_elf_init(void) {
    u32 p[256]; int i;
    if (g_inited) return 0;
    g_inited = 1;
    g_objs = NULL; g_win_dc = NULL; g_default_font = NULL;
    { int k; for (k = 0; k < 32; ++k) g_stock[k] = NULL; }
    stock_init();
    g_default_font = (GFont *) g_stock[17];
    if (!g_default_font) g_default_font = font_make(16, 400, 0, 0, 0, 0, "System");
    {   GDC *dc = (GDC *) obj_new(OBJ_DC, sizeof(GDC));
        if (!dc) return -1;
        dc->is_window = 1;
        dc->font = g_default_font;
        dc->brush = (GBrush *) g_stock[0];
        dc->pen = (GPen *) g_stock[7];
        dc->text_color = 0x00FFFFFFu;
        dc->bk_color = 0x00000000u;
        dc->bk_mode = 1;                        /* TRANSPARENT */
        dc->text_align = GDI_TA_LEFT;
        g_win_dc = dc;
    }
    /* la palette di partenza del DC finestra e' quella del canvas (host) */
    if (host_elf_get_palette(p, 0, 256) != 0) for (i = 0; i < 256; ++i) p[i] = (u32) (i * 0x010101u);
    (void) p;
    return 0;
}

void gdi_elf_shutdown(void) {
    /* il registro e' una lista: si libera tutto, comprese le bitmap */
    GObj *o = g_objs;
    while (o) {
        GObj *n = o->next;
        if (o->type == OBJ_BITMAP) { GBitmap *b = (GBitmap *) o; if (b->px) free(b->px); }
        if (o->type == OBJ_REGION) { GRegion *r = (GRegion *) o; if (r->s) free(r->s); }
        free(o);
        o = n;
    }
    g_objs = NULL; g_win_dc = NULL; g_default_font = NULL; g_inited = 0;
}

void *gdi_elf_window_dc(void) { return (void *) g_win_dc; }

/* ============================ DC e oggetti ============================ */
GDI_EXTERN(void *, CreateCompatibleDC, (void *hdc)) {
    GDC *src = (GDC *) hdc;
    GDC *dc;
    if (hdc && !obj_valid(hdc, OBJ_DC)) return NULL;
    dc = (GDC *) obj_new(OBJ_DC, sizeof(GDC));
    if (!dc) return NULL;
    dc->font = g_default_font;
    dc->brush = (GBrush *) g_stock[0];
    dc->pen = (GPen *) g_stock[7];
    dc->text_color = 0x00FFFFFFu;
    dc->bk_mode = 1;
    dc->text_align = GDI_TA_LEFT;
    /* come GDI: un memory DC nasce con una bitmap 1x1 selezionata (qui 8 bit, non monocromatica,
     * cosi' il formato e' quello del device) */
    dc->bm = (GBitmap *) CreateCompatibleBitmap(dc, 1, 1);
    dc->owns_bm = 1;
    (void) src;
    return dc;
}

GDI_EXTERN(void *, CreateCompatibleBitmap, (void *hdc, int cx, int cy)) {
    int bpp = 8, w = 0, h = 0, pitch;
    GBitmap *bm;
    u8 *px; int pw, ph, pp;
    if (!obj_valid(hdc, OBJ_DC)) return NULL;
    if (!dc_surface((GDC *) hdc, &px, &pw, &ph, &pp, &bpp)) bpp = 8;
    if (cx <= 0) cx = 1;
    if (cy <= 0) cy = 1;
    pitch = (cx * (bpp ? bpp : 8) / 8 + 3) & ~3;
    w = cx; h = cy;
    bm = (GBitmap *) obj_new(OBJ_BITMAP, sizeof(GBitmap));
    if (!bm) return NULL;
    bm->px = (u8 *) malloc((unsigned long) pitch * h);
    if (!bm->px) return NULL;
    memset(bm->px, 0, (unsigned long) pitch * h);
    bm->w = w; bm->h = h; bm->pitch = pitch; bm->bpp = bpp;
    return bm;
}

GDI_EXTERN(int, DeleteDC, (void *hdc)) {
    GDC *dc = (GDC *) hdc;
    if (!dc || dc == g_win_dc || !obj_valid(hdc, OBJ_DC)) return 0;
    if (dc->clip) DeleteObject(dc->clip);      /* la regione di clip e' di proprieta' del DC */
    if (dc->owns_bm && dc->bm) DeleteObject(dc->bm);
    obj_unlink((GObj *) dc);
    free(dc);
    return 1;
}

GDI_EXTERN(int, DeleteObject, (void *hobj)) {
    GObj *o = (GObj *) hobj;
    if (!o || !obj_valid(hobj, o->type)) return 0;
    if (o->is_stock) return 1;                 /* gli oggetti stock non si liberano */
    switch (o->type) {
    case OBJ_BITMAP: { GBitmap *b = (GBitmap *) o; if (b->px) free(b->px); break; }
    case OBJ_REGION: { GRegion *r = (GRegion *) o; if (r->s) free(r->s); break; }
    default: break;
    }
    obj_unlink(o);
    free(o);
    return 1;
}

GDI_EXTERN(void *, SelectObject, (void *hdc, void *hobj)) {
    GDC *dc = (GDC *) hdc;
    GObj *o = (GObj *) hobj;
    if (!obj_valid(hdc, OBJ_DC) || !o) return NULL;
    switch (o->type) {
    case OBJ_BITMAP: { GBitmap *old = dc->bm; dc->bm = (GBitmap *) o; dc->owns_bm = 0; return old; }
    case OBJ_FONT:   { GFont *old = dc->font; dc->font = (GFont *) o; return old; }
    case OBJ_PEN:    { GPen *old = dc->pen; dc->pen = (GPen *) o; return old; }
    case OBJ_BRUSH:  { GBrush *old = dc->brush; dc->brush = (GBrush *) o; return old; }
    case OBJ_PALETTE:{ GPalette *old = dc->pal; dc->pal = (GPalette *) o; return old; }
    default: return NULL;
    }
}

GDI_EXTERN(void *, GetStockObject, (int i)) {
    if (i < 0 || i > 31) return NULL;
    return (void *) g_stock[i];
}

GDI_EXTERN(void *, GetCurrentObject, (void *hdc, unsigned int type)) {
    GDC *dc = (GDC *) hdc;
    if (!obj_valid(hdc, OBJ_DC)) return NULL;
    switch (type) {
    case 1 /*OBJ_PEN*/: return dc->pen;
    case 2 /*OBJ_BRUSH*/: return dc->brush;
    case 5 /*OBJ_FONT*/: return dc->font;
    case 6 /*OBJ_BITMAP*/: return dc->bm;
    case 7 /*OBJ_PAL*/: return dc->pal;
    default: return NULL;
    }
}

GDI_EXTERN(int, GetObjectA, (void *hobj, int c, void *pv)) {
    GObj *o = (GObj *) hobj;
    if (!o || !obj_valid(hobj, o->type) || !pv || c <= 0) return 0;
    switch (o->type) {
    case OBJ_BITMAP: {
        GBitmap *b = (GBitmap *) o;
        u8 *d = (u8 *) pv;
        if (c < 24) return 0;
        memset(d, 0, 24);
        *(u32 *) (d + 0) = 0;
        *(u32 *) (d + 4) = (u32) b->w;
        *(u32 *) (d + 8) = (u32) b->h;
        *(u32 *) (d + 12) = (u32) b->pitch;
        *(u16 *) (d + 16) = 1;
        *(u16 *) (d + 18) = (u16) b->bpp;
        *(u32 *) (d + 20) = (u32) (unsigned int) b->px;
        return 24; }
    case OBJ_FONT: {
        GFont *f = (GFont *) o;
        u8 *d = (u8 *) pv;
        if (c < 60) return 0;
        memset(d, 0, 60);
        *(u32 *) (d + 0) = (u32) f->req_height;
        *(u32 *) (d + 4) = (u32) f->cell_w;
        *(u32 *) (d + 16) = (u32) f->weight;
        d[20] = (u8) f->italic; d[21] = (u8) f->underline; d[22] = (u8) f->strike;
        d[23] = (u8) f->charset; d[27] = 0x31;      /* FIXED_PITCH|FF_MODERN */
        { int i; for (i = 0; i < 31 && f->face[i]; ++i) d[28 + i] = (u8) f->face[i]; }
        return 60; }
    case OBJ_BRUSH: {
        GBrush *b = (GBrush *) o;
        u8 *d = (u8 *) pv;
        if (c < 12) return 0;
        *(u32 *) (d + 0) = (u32) b->style;
        *(u32 *) (d + 4) = b->color;
        *(u32 *) (d + 8) = 0;
        return 12; }
    case OBJ_PEN: {
        GPen *p = (GPen *) o;
        u8 *d = (u8 *) pv;
        if (c < 12) return 0;
        *(u32 *) (d + 0) = (u32) p->style;
        *(u32 *) (d + 4) = p->color;
        *(u32 *) (d + 8) = 0;
        return 12; }
    case OBJ_PALETTE: {
        GPalette *p = (GPalette *) o;
        u8 *e = (u8 *) pv;
        int i, n = c / 4;
        if (n > 256) n = 256;
        for (i = 0; i < n; ++i) { e[i*4] = p->r[i]; e[i*4+1] = p->g[i]; e[i*4+2] = p->b[i]; e[i*4+3] = p->f[i]; }
        return n * 4; }
    default: return 0;
    }
}

GDI_EXTERN(int, GetDeviceCaps, (void *hdc, int index)) {
    GDC *dc = (GDC *) hdc;
    u8 *px; int w = 0, h = 0, pitch, bpp = 8;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    dc_surface(dc, &px, &w, &h, &pitch, &bpp);
    switch (index) {
    case 8:  return w;                       /* HORZRES */
    case 10: return h;                       /* VERTRES */
    case 12: return bpp;                     /* BITSPIXEL */
    case 14: return 1;                       /* PLANES */
    case 24: return bpp == 8 ? 256 : -1;     /* NUMCOLORS */
    case 38: return 0x0001 | 0x0100;         /* RASTERCAPS: RC_BITBLT|RC_PALETTE */
    case 88: case 90: return 96;             /* LOGPIXELSX/Y */
    case 104: return 256;                    /* SIZEPALETTE */
    case 108: return 18;                     /* COLORRES */
    case 94: return 0;                       /* CAPS1 */
    default: return 0;
    }
}

GDI_EXTERN(int, GdiFlush, (void)) { return 1; }

/* ============================ penne / pennelli ============================ */
GDI_EXTERN(void *, CreatePen, (int style, int width, unsigned long color)) {
    GPen *p = (GPen *) obj_new(OBJ_PEN, sizeof(GPen));
    if (!p) return NULL;
    /* il COLORREF si conserva INTERO: `PALETTEINDEX(i)` = 0x01000000|i e' un'informazione, non
     * un colore RGB (mascherarlo lo trasforma in un RGB scuro e la mappatura sbaglia indice). */
    p->style = style; p->width = width > 0 ? width : 1; p->color = color;
    return p;
}
GDI_EXTERN(void *, CreateSolidBrush, (unsigned long color)) {
    GBrush *b = (GBrush *) obj_new(OBJ_BRUSH, sizeof(GBrush));
    if (!b) return NULL;
    b->style = 0; b->color = color;
    return b;
}

/* ============================ bitmap DIB ============================ */
/* BITMAPINFOHEADER: biSize 0, biWidth 4, biHeight 8, biPlanes 12, biBitCount 14, biClrUsed 32
 * BITMAPINFO: header (40) + RGBQUAD bmiColors[] (B,G,R,0) */
static int dib_bpp(const void *bmi) {
    if (!bmi) return 8;
    return (int) *(const u16 *) ((const u8 *) bmi + 14);
}
static int dib_pal_n(const void *bmi) {
    int used = (int) *(const u32 *) ((const u8 *) bmi + 32);
    if (used <= 0 || used > 256) used = dib_bpp(bmi) == 8 ? 256 : 0;
    return used;
}
static void bitmap_set_dib_pal(GBitmap *bm, const void *bmi) {
    const u8 *c = (const u8 *) bmi + 40;
    int n = dib_pal_n(bmi), i;
    for (i = 0; i < 256; ++i) {
        if (i < n) bm->pal[i] = ((u32) c[i*4+2] << 16) | ((u32) c[i*4+1] << 8) | (u32) c[i*4];
        else bm->pal[i] = (u32) (i * 0x010101u);
    }
    bm->has_pal = 1;
}
/* stride DIB: allineato a 4 byte per riga */
static int dib_stride(int w, int bpp) { return (w * bpp / 8 + 3) & ~3; }

GDI_EXTERN(void *, CreateDIBitmap, (void *hdc, const void *pbmih, unsigned long flInit,
                                    const void *pjBits, const void *pbmi, unsigned int iUsage)) {
    const u8 *h = (const u8 *) pbmih;
    int w, hgt, bpp, pitch, y;
    GBitmap *bm;
    (void) hdc; (void) iUsage;
    if (!pbmih) return NULL;
    w = (int) *(const u32 *) (h + 4);
    hgt = (int) *(const u32 *) (h + 8);
    bpp = (int) *(const u16 *) (h + 14);
    if (bpp != 8 && bpp != 24 && bpp != 32) bpp = 8;
    if (w <= 0) w = 1;
    if (hgt < 0) hgt = -hgt;
    if (hgt <= 0) hgt = 1;
    pitch = (w * bpp / 8 + 3) & ~3;
    bm = (GBitmap *) obj_new(OBJ_BITMAP, sizeof(GBitmap));
    if (!bm) return NULL;
    bm->px = (u8 *) malloc((unsigned long) pitch * hgt);
    if (!bm->px) { DeleteObject(bm); return NULL; }
    memset(bm->px, 0, (unsigned long) pitch * hgt);
    bm->w = w; bm->h = hgt; bm->pitch = pitch; bm->bpp = bpp;
    if (pbmi) bitmap_set_dib_pal(bm, pbmi);
    if ((flInit & 0x4u /*CBM_INIT*/) && pjBits) {     /* 4 = CBM_INIT */
        const u8 *src = (const u8 *) pjBits;
        int stride = dib_stride(w, bpp);
        for (y = 0; y < hgt; ++y) memcpy(bm->px + (unsigned long) y * pitch, src + (unsigned long) y * stride, (unsigned long) (w * bpp / 8));
    }
    return bm;
}

GDI_EXTERN(long, GetBitmapBits, (void *hbm, long cb, void *pv)) {
    GBitmap *b = (GBitmap *) hbm;
    unsigned long total, n;
    if (!obj_valid(hbm, OBJ_BITMAP) || !pv) return 0;
    total = (unsigned long) b->pitch * b->h;
    n = (cb > 0 && (unsigned long) cb < total) ? (unsigned long) cb : total;
    memcpy(pv, b->px, n);
    return (long) n;
}

GDI_EXTERN(int, SetDIBits, (void *hdc, void *hbm, unsigned int start, unsigned int cLines,
                            const void *bits, const void *bmi, unsigned int usage)) {
    GBitmap *b = (GBitmap *) hbm;
    int sbpp, stride, y, n;
    const u8 *src = (const u8 *) bits;
    const u8 *colors = (const u8 *) bmi + 40;
    (void) hdc; (void) usage;
    if (!obj_valid(hbm, OBJ_BITMAP) || !bits || !bmi) return 0;
    sbpp = dib_bpp(bmi);
    if ((sbpp != 8 && sbpp != 24 && sbpp != 32) ||
        (b->bpp != 8 && b->bpp != 24 && b->bpp != 32)) return 0;
    stride = dib_stride(b->w, sbpp);
    n = (int) cLines;
    if (start > (unsigned) b->h) return 0;
    if (start + (unsigned) n > (unsigned) b->h) n = b->h - (int) start;
    if (n <= 0) n = b->h - (int) start;
    for (y = 0; y < n; ++y) {
        int dy = (int) start + y, x;
        u8 *d = b->px + (unsigned long) dy * b->pitch;
        const u8 *s = src + (unsigned long) y * stride;
        if (sbpp == b->bpp) {
            memcpy(d, s, (unsigned long) (b->w * sbpp / 8));
            continue;
        }
        for (x = 0; x < b->w; ++x) {
            u32 rgb;
            if (sbpp == 8) {
                u32 used = *(const u32 *) ((const u8 *) bmi + 32);
                u32 ix = s[x];
                if (used == 0 || used > 256) used = 256;
                if (ix < used) rgb = ((u32) colors[ix*4+2] << 16) |
                                    ((u32) colors[ix*4+1] << 8) | colors[ix*4];
                else rgb = 0;
            } else if (sbpp == 24) {
                rgb = (u32) s[x*3] | ((u32) s[x*3+1] << 8) | ((u32) s[x*3+2] << 16);
            } else {
                const u8 *p = s + x*4;
                rgb = (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16);
            }
            if (b->bpp == 8) {
                int i, best = 0; long bestd = -1;
                for (i = 0; i < 256; ++i) {
                    u32 c = b->has_pal ? b->pal[i] : (u32)(i * 0x010101u);
                    long dr = (long)((c >> 16) & 255) - (long)((rgb >> 16) & 255);
                    long dg = (long)((c >> 8) & 255) - (long)((rgb >> 8) & 255);
                    long db = (long)(c & 255) - (long)(rgb & 255);
                    long dist = dr*dr + dg*dg + db*db;
                    if (bestd < 0 || dist < bestd) { bestd = dist; best = i; }
                    if (!dist) break;
                }
                d[x] = (u8) best;
            } else if (b->bpp == 24) {
                d[x*3] = (u8) rgb; d[x*3+1] = (u8)(rgb >> 8); d[x*3+2] = (u8)(rgb >> 16);
            } else *(u32 *)(d + x*4) = rgb;
        }
    }
    return n;
}

GDI_EXTERN(int, GetDIBits, (void *hdc, void *hbm, unsigned int start, unsigned int cLines,
                            void *bits, void *bmi, unsigned int usage)) {
    GBitmap *b = (GBitmap *) hbm;
    u8 *bh = (u8 *) bmi;
    int want, stride, y, n;
    u8 *dst = (u8 *) bits;
    (void) hdc; (void) usage;
    if (!obj_valid(hbm, OBJ_BITMAP) || !bmi) return 0;
    if (!bits) {
        /* interrogazione della dimensione: come GDI non tocca l'header passato */
        return b->h;
    }
    want = (int) *(const u16 *) (bh + 14);
    stride = dib_stride(b->w, want);
    n = (int) cLines;
    if (n <= 0 || start + (unsigned) n > (unsigned) b->h) n = b->h - (int) start;
    if (n < 0) n = 0;
    for (y = 0; y < n; ++y) {
        int sy = (int) start + y;
        if (sy < 0 || sy >= b->h) continue;
        /* DIB bottom-up: la riga 0 del DIB e' l'ultima della bitmap */
        {   u8 *row = dst + (unsigned long) (b->h - 1 - sy) * stride;
            const u8 *s = b->px + (unsigned long) sy * b->pitch;
            if (want == b->bpp) memcpy(row, s, (unsigned long) (b->w * want / 8));
            else if (b->bpp == 8 && want == 24) {
                int x;
                for (x = 0; x < b->w; ++x) {
                    u32 c;
                    if (b->has_pal) c = b->pal[s[x]];
                    else if (!dc_palette_rgb((GDC *) hdc, s[x], &c)) c = 0;
                    row[x*3] = (u8) c; row[x*3+1] = (u8) (c >> 8); row[x*3+2] = (u8) (c >> 16);
                }
            } else if (b->bpp == 24 && want == 8) {
                int x; for (x = 0; x < b->w; ++x) row[x] = (u8) (s[x*3] & 0xFF);
            }
        }
    }
    memset(bh, 0, 40);
    *(u32 *) (bh + 0) = 40;
    *(u32 *) (bh + 4) = (u32) b->w;
    *(u32 *) (bh + 8) = (u32) b->h;
    *(u16 *) (bh + 12) = 1;
    *(u16 *) (bh + 14) = (u16) want;
    *(u32 *) (bh + 20) = (u32) (stride * b->h);
    return n;
}

GDI_EXTERN(int, SetDIBitsToDevice, (void *hdc, int xd, int yd, unsigned long w, unsigned long h,
                                    int xs, int ys, unsigned int startScan, unsigned int cLines,
                                    const void *bits, const void *bmi, unsigned int usage)) {
    GDC *dc = (GDC *) hdc;
    const u8 *src = (const u8 *) bits;
    int sbpp, sstride, i, j, drawn = 0;
    u8 *px; int dw, dh, dpitch, dbpp;
    (void) xs; (void) ys; (void) usage;
    if (!obj_valid(hdc, OBJ_DC) || !bits || !bmi) return 0;
    if (!dc_surface(dc, &px, &dw, &dh, &dpitch, &dbpp)) return 0;
    sbpp = dib_bpp(bmi);
    sstride = dib_stride((int) w, sbpp);
    if (h == 0) h = (unsigned long) (cLines ? cLines : 1);
    for (j = 0; j < (int) h; ++j) {
        int yy = yd + j;
        if (yy < 0 || yy >= dh) continue;
        for (i = 0; i < (int) w; ++i) {
            int xx = xd + i;
            RSpan sp[64]; int n, k;
            u32 v = 0;
            const u8 *sp_row = src + (unsigned long) j * sstride;
            if (xx < 0 || xx >= dw) continue;
            if (sbpp == 8) v = sp_row[i];
            else if (sbpp == 24) v = (u32) sp_row[i*3] | ((u32) sp_row[i*3+1] << 8) | ((u32) sp_row[i*3+2] << 16);
            else v = *(const u32 *) (sp_row + i * 4);
            if (dbpp == 8) {
                if (sbpp != 8) {       /* RGB -> indice: mappatura piu' vicina */
                    int p; long bestd = -1; u32 best = 0;
                    for (p = 0; p < 256; ++p) {
                        u32 c; if (!dc_palette_rgb(dc, p, &c)) break;
                        { long dr = (long)((c>>16)&0xFF) - (long)((v>>16)&0xFF);
                          long dg = (long)((c>>8)&0xFF) - (long)((v>>8)&0xFF);
                          long db = (long)(c&0xFF) - (long)(v&0xFF);
                          long d = dr*dr+dg*dg+db*db; if (bestd < 0 || d < bestd) { bestd = d; best = (u32) p; } }
                    }
                    v = best;
                }
            }
            n = clip_spans(dc, yy, xx, xx + 1, dw, sp, 64);
            for (k = 0; k < n; ++k) { put_px(px, dpitch, dbpp, sp[k].x0, yy, v); drawn = 1; }
        }
    }
    (void) startScan;
    dc_present(dc);
    return drawn;
}

/* ============================ disegno ============================ */
static void blit_core(GDC *dc, int xd, int yd, int wd, int hd,
                      GDC *src, int xs, int ys, int ws, int hs, u32 rop) {
    u8 *dpx, *spx; int dw, dh, dpitch, dbpp, sw, sh, spitch, sbpp;
    int x, y;
    if (!dc_surface(dc, &dpx, &dw, &dh, &dpitch, &dbpp)) return;
    if (!src || !dc_surface(src, &spx, &sw, &sh, &spitch, &sbpp)) return;
    if (wd <= 0 || hd <= 0 || ws <= 0 || hs <= 0) return;
    for (y = 0; y < hd; ++y) {
        int yy = yd + y;
        int sy = (hd == hs) ? ys + y : ys + (int) ((long) y * hs / hd);
        if (yy < 0 || yy >= dh) continue;
        for (x = 0; x < wd; ++x) {
            int xx = xd + x;
            int sx = (wd == ws) ? xs + x : xs + (int) ((long) x * ws / wd);
            u32 v, d;
            RSpan sp[64]; int n, k;
            if (xx < 0 || xx >= dw || sx < 0 || sx >= sw || sy < 0 || sy >= sh) continue;
            n = clip_spans(dc, yy, xx, xx + 1, dw, sp, 64);
            if (n == 0) continue;
            v = get_px(spx, spitch, sbpp, sx, sy);
            if (sbpp == 8 && dbpp == 8) {
                /* nessuna conversione */
            } else if (sbpp == 8 && dbpp != 8) {
                dc_palette_rgb(src, (int) v, &v);
            } else if (sbpp != 8 && dbpp == 8) {
                int p; long bestd = -1; u32 best = 0; u32 rgb = v & 0xFFFFFFu;
                for (p = 0; p < 256; ++p) {
                    u32 c; if (!dc_palette_rgb(dc, p, &c)) break;
                    { long dr = (long)((c>>16)&0xFF) - (long)((rgb>>16)&0xFF);
                      long dg = (long)((c>>8)&0xFF) - (long)((rgb>>8)&0xFF);
                      long db = (long)(c&0xFF) - (long)(rgb&0xFF);
                      long d = dr*dr+dg*dg+db*db; if (bestd < 0 || d < bestd) { bestd = d; best = (u32) p; } }
                }
                v = best;
            }
            d = get_px(dpx, dpitch, dbpp, xx, yy);
            switch (rop) {
            case GDI_BLACKNESS: v = 0; break;
            case GDI_WHITENESS: v = (dbpp == 8) ? 0xFFu : 0xFFFFFFu; break;
            case GDI_DSTINVERT: v = (dbpp == 8) ? ((d ^ 0xFFu) & 0xFFu) : (~d & 0xFFFFFFu); break;
            case GDI_SRCAND:    v = d & v; break;
            case GDI_SRCPAINT:  v = d | v; break;
            case GDI_SRCINVERT: case GDI_PATINVERT: v = d ^ v; break;
            case GDI_PATCOPY:   v = v; break;
            case GDI_NOTSRCCOPY: v = (dbpp == 8) ? ((v ^ 0xFFu) & 0xFFu) : (~v & 0xFFFFFFu); break;
            default: break;                        /* SRCCOPY e ignote */
            }
            for (k = 0; k < n; ++k) put_px(dpx, dpitch, dbpp, sp[k].x0, yy, v);
        }
    }
    dc_present(dc);
}

GDI_EXTERN(int, BitBlt, (void *hdst, int x, int y, int cx, int cy, void *hsrc, int sx, int sy, unsigned long rop)) {
    if (!obj_valid(hdst, OBJ_DC) || !obj_valid(hsrc, OBJ_DC)) return 0;
    blit_core((GDC *) hdst, x, y, cx, cy, (GDC *) hsrc, sx, sy, cx, cy, rop);
    return 1;
}
GDI_EXTERN(int, StretchBlt, (void *hdst, int xd, int yd, int wd, int hd, void *hsrc,
                             int xs, int ys, int ws, int hs, unsigned long rop)) {
    if (!obj_valid(hdst, OBJ_DC) || !obj_valid(hsrc, OBJ_DC)) return 0;
    blit_core((GDC *) hdst, xd, yd, wd, hd, (GDC *) hsrc, xs, ys, ws, hs, rop);
    return 1;
}

GDI_EXTERN(int, FillRect, (void *hdc, const void *lprc, void *hbr)) {
    GDC *dc = (GDC *) hdc;
    const u8 *r = (const u8 *) lprc;
    GBrush *b = (GBrush *) hbr;
    u8 *px; int w, h, pitch, bpp;
    u32 v;
    if (!obj_valid(hdc, OBJ_DC) || !lprc) return 0;
    if (!obj_valid(hbr, OBJ_BRUSH)) return 0;
    if (b->style == 1) return 1;                  /* BS_NULL: niente */
    if (!dc_surface(dc, &px, &w, &h, &pitch, &bpp)) return 0;
    v = dc_color(dc, b->color);
    fill_rect_raw(dc, px, w, h, pitch, bpp, *(const int *) (r + 0), *(const int *) (r + 4),
                  *(const int *) (r + 8), *(const int *) (r + 12), v);
    dc_present(dc);
    return 1;
}

GDI_EXTERN(int, Rectangle, (void *hdc, int left, int top, int right, int bottom)) {
    GDC *dc = (GDC *) hdc;
    u8 *px; int w, h, pitch, bpp;
    u32 fillv, penv;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (!dc_surface(dc, &px, &w, &h, &pitch, &bpp)) return 0;
    if (right <= left || bottom <= top) return 1;
    /* riempimento con il pennello, poi bordo con la penna. Come GDI il bordo esterno e' su
     * left/top e il bordo interno su right-1/bottom-1 (il rect e' bottom-right escluso). */
    if (dc->brush && dc->brush->style != 1) {
        fillv = dc_color(dc, dc->brush->color);
        if (right - 1 > left + 1 && bottom - 1 > top + 1)
            fill_rect_raw(dc, px, w, h, pitch, bpp, left + 1, top + 1, right - 1, bottom - 1, fillv);
    }
    if (dc->pen && dc->pen->style != 5 /*PS_NULL*/) {
        penv = dc_color(dc, dc->pen->color);
        {   int x, y;
            for (x = left; x < right; ++x) {
                RSpan sp[64]; int n, k;
                n = clip_spans(dc, top, x, x + 1, w, sp, 64);
                for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, top, penv);
            }
            for (x = left; x < right; ++x) {
                RSpan sp[64]; int n, k;
                n = clip_spans(dc, bottom - 1, x, x + 1, w, sp, 64);
                for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, bottom - 1, penv);
            }
            for (y = top; y < bottom; ++y) {
                RSpan sp[64]; int n, k;
                n = clip_spans(dc, y, left, left + 1, w, sp, 64);
                for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, y, penv);
            }
            for (y = top; y < bottom; ++y) {
                RSpan sp[64]; int n, k;
                n = clip_spans(dc, y, right - 1, right, w, sp, 64);
                for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, y, penv);
            }
        }
    }
    dc_present(dc);
    return 1;
}

GDI_EXTERN(int, PatBlt, (void *hdc, int x, int y, int w, int h, unsigned long rop)) {
    GDC *dc = (GDC *) hdc;
    u8 *px; int sw, sh, pitch, bpp;
    u32 v = 0; int yy, xx;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (!dc_surface(dc, &px, &sw, &sh, &pitch, &bpp)) return 0;
    if (w <= 0 || h <= 0) return 1;
    if (dc->brush && dc->brush->style != 1) v = dc_color(dc, dc->brush->color);
    for (yy = y; yy < y + h; ++yy) {
        if (yy < 0 || yy >= sh) continue;
        for (xx = x; xx < x + w; ++xx) {
            RSpan sp[64]; int n, k; u32 d, res;
            if (xx < 0 || xx >= sw) continue;
            n = clip_spans(dc, yy, xx, xx + 1, sw, sp, 64);
            if (n == 0) continue;
            d = get_px(px, pitch, bpp, xx, yy);
            switch (rop) {
            case GDI_PATCOPY:   res = v; break;
            case GDI_BLACKNESS: res = 0; break;
            case GDI_WHITENESS: res = (bpp == 8) ? 0xFFu : 0xFFFFFFu; break;
            case GDI_DSTINVERT: res = (bpp == 8) ? ((d ^ 0xFFu) & 0xFFu) : (~d & 0xFFFFFFu); break;
            case GDI_PATINVERT: case GDI_SRCINVERT: res = d ^ v; break;
            case GDI_SRCPAINT:  res = d | v; break;
            case GDI_SRCAND:    res = d & v; break;
            default:            res = v; break;
            }
            for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, yy, res);
        }
    }
    dc_present(dc);
    return 1;
}

GDI_EXTERN(int, MoveToEx, (void *hdc, int x, int y, void *lppt)) {
    GDC *dc = (GDC *) hdc;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (lppt) { *(int *) ((u8 *) lppt + 0) = dc->cur_x; *(int *) ((u8 *) lppt + 4) = dc->cur_y; }
    dc->cur_x = x; dc->cur_y = y;
    return 1;
}

GDI_EXTERN(int, LineTo, (void *hdc, int x, int y)) {
    GDC *dc = (GDC *) hdc;
    u8 *px; int w, h, pitch, bpp;
    int x0, y0, x1, y1, dx, dy, sx, sy, err, i;
    u32 v;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (!dc_surface(dc, &px, &w, &h, &pitch, &bpp)) { dc->cur_x = x; dc->cur_y = y; return 1; }
    v = dc->pen ? dc_color(dc, dc->pen->color) : 0;
    x0 = dc->cur_x; y0 = dc->cur_y; x1 = x; y1 = y;
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y1 - y0 : y0 - y1;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;
    for (i = 0; i <= dx + dy + 1; ++i) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            RSpan sp[64]; int n, k;
            n = clip_spans(dc, y0, x0, x0 + 1, w, sp, 64);
            for (k = 0; k < n; ++k) put_px(px, pitch, bpp, sp[k].x0, y0, v);
        }
        if (x0 == x1 && y0 == y1) break;
        { int e2 = 2 * err;
          if (e2 > -dy) { err -= dy; x0 += sx; }
          if (e2 < dx) { err += dx; y0 += sy; } }
    }
    dc->cur_x = x; dc->cur_y = y;
    dc_present(dc);
    return 1;
}

/* ============================ testo ============================ */
static void text_core(GDC *dc, int x, int y, const char *s, int n, const int *dx,
                      const int *cliprect, int force_opaque_fill) {
    u8 *px; int w, h, pitch, bpp;
    GFont *f = dc->font ? dc->font : g_default_font;
    u32 tcol, bcol;
    int i, cx = x, cy = y;
    if (!obj_valid(dc, OBJ_DC) || !s) return;
    if (n < 0) { n = 0; while (s[n]) ++n; }
    if (n == 0) return;
    if (!dc_surface(dc, &px, &w, &h, &pitch, &bpp)) return;
    if (!f) return;
    tcol = dc_color(dc, dc->text_color);
    bcol = dc_color(dc, dc->bk_color);
    if (dc->text_align & GDI_TA_UPDATECP) { cx = dc->cur_x; cy = dc->cur_y; }
    if ((dc->text_align & 0x6u) == GDI_TA_RIGHT) cx -= n * f->cell_w;
    else if ((dc->text_align & 0x6u) == GDI_TA_CENTER) cx -= (n * f->cell_w) / 2;
    if (dc->text_align & 0x18u) {                 /* TA_BASELINE(0x18) / TA_BOTTOM(0x8) */
        if ((dc->text_align & 0x18u) == 0x18u) cy -= f->ascent;
        else cy -= f->cell_h;
    }
    if (force_opaque_fill && cliprect) {
        fill_rect_raw(dc, px, w, h, pitch, bpp, cliprect[0], cliprect[1], cliprect[2], cliprect[3], bcol);
    }
    for (i = 0; i < n; ++i) {
        int ch = (u8) s[i];
        const u8 *g = font_glyph(f, ch);
        int gx = cx, gy = cy, r, b;
        if (cliprect && (gx + f->cell_w <= cliprect[0] || gx >= cliprect[2])) { cx += f->cell_w; continue; }
        for (r = 0; r < f->cell_h; ++r) {
            int yy = gy + r;
            if (yy < 0 || yy >= h) continue;
            if (cliprect && (yy < cliprect[1] || yy >= cliprect[3])) continue;
            for (b = 0; b < f->cell_w; ++b) {
                int xx = gx + b;
                int on;
                RSpan sp[64]; int nn, k;
                if (xx < 0 || xx >= w) continue;
                if (cliprect && (xx < cliprect[0] || xx >= cliprect[2])) continue;
                on = (b < 8) ? ((g[r] >> (7 - b)) & 1) : 0;
                if (!on && dc->bk_mode != GDI_OPAQUE && !force_opaque_fill) continue;
                nn = clip_spans(dc, yy, xx, xx + 1, w, sp, 64);
                for (k = 0; k < nn; ++k) put_px(px, pitch, bpp, sp[k].x0, yy, on ? tcol : bcol);
            }
        }
        if (dx) cx += dx[i];
        else cx += f->cell_w;
    }
    if (dc->text_align & GDI_TA_UPDATECP) { dc->cur_x = cx; dc->cur_y = cy; }
    dc_present(dc);
}

GDI_EXTERN(void *, CreateFontA, (int h, int w, int esc, int orient, int weight, unsigned long italic,
                                 unsigned long underline, unsigned long strike, unsigned long charset,
                                 unsigned long outprec, unsigned long clipprec, unsigned long quality,
                                 unsigned long pitchfam, const char *face)) {
    (void) w; (void) esc; (void) orient; (void) outprec; (void) clipprec; (void) quality; (void) pitchfam;
    return font_make(h, (int) weight, (int) charset, (int) italic, (int) underline, (int) strike, face);
}

GDI_EXTERN(void *, CreateFontIndirectA, (const void *lplf)) {
    const u8 *l = (const u8 *) lplf;
    if (!l) return NULL;
    return font_make((int) *(const u32 *) (l + 0), (int) *(const u32 *) (l + 16),
                     (int) l[23], l[20], l[21], l[22], (const char *) (l + 28));
}

GDI_EXTERN(int, GetTextMetricsA, (void *hdc, void *lptm)) {
    GDC *dc = (GDC *) hdc;
    GFont *f;
    u8 *m = (u8 *) lptm;
    if (!obj_valid(hdc, OBJ_DC) || !lptm) return 0;
    f = dc->font ? dc->font : g_default_font;
    if (!f) return 0;
    memset(m, 0, 56);
    *(u32 *) (m + 0) = (u32) f->cell_h;
    *(u32 *) (m + 4) = (u32) f->ascent;
    *(u32 *) (m + 8) = (u32) (f->cell_h - f->ascent);
    *(u32 *) (m + 12) = 0;
    *(u32 *) (m + 16) = 0;
    *(u32 *) (m + 20) = (u32) f->cell_w;
    *(u32 *) (m + 24) = (u32) f->cell_w;
    *(u32 *) (m + 28) = (u32) f->weight;
    *(u32 *) (m + 32) = 0;
    *(u32 *) (m + 36) = 96;
    *(u32 *) (m + 40) = 96;
    m[44] = 32; m[45] = 126; m[46] = '?'; m[47] = ' ';
    m[48] = (u8) f->italic; m[49] = (u8) f->underline; m[50] = (u8) f->strike;
    m[51] = 0x31;                                 /* FIXED_PITCH | FF_MODERN */
    m[52] = (u8) f->charset;
    return 1;
}

GDI_EXTERN(int, GetTextExtentPoint32A, (void *hdc, const char *s, int c, void *psize)) {
    GDC *dc = (GDC *) hdc;
    GFont *f;
    if (!obj_valid(hdc, OBJ_DC) || !psize) return 0;
    f = dc->font ? dc->font : g_default_font;
    if (!f) return 0;
    if (c < 0) { c = 0; if (s) while (s[c]) ++c; }
    *(int *) psize = c * f->cell_w;
    *(int *) ((u8 *) psize + 4) = f->cell_h;
    return 1;
}

GDI_EXTERN(int, GetTextExtentExPointA, (void *hdc, const char *s, int cch, int nMaxExtent,
                                         int *lpFit, int *lpDx, void *lpSize)) {
    GDC *dc = (GDC *) hdc;
    GFont *f;
    int i, fit = 0;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    f = dc->font ? dc->font : g_default_font;
    if (!f) return 0;
    if (cch < 0) { cch = 0; if (s) while (s[cch]) ++cch; }
    for (i = 0; i < cch; ++i) {
        int adv = (i + 1) * f->cell_w;
        if (lpDx) lpDx[i] = adv;
        if (nMaxExtent <= 0 || adv <= nMaxExtent) fit = i + 1;
    }
    if (lpFit) *lpFit = fit;
    if (lpSize) { *(int *) lpSize = cch * f->cell_w; *(int *) ((u8 *) lpSize + 4) = f->cell_h; }
    (void) s;
    return 1;
}

GDI_EXTERN(int, GetCharABCWidthsA, (void *hdc, unsigned int first, unsigned int last, void *lpabc)) {
    GDC *dc = (GDC *) hdc;
    GFont *f;
    u32 i;
    if (!obj_valid(hdc, OBJ_DC) || !lpabc) return 0;
    f = dc->font ? dc->font : g_default_font;
    if (!f) return 0;
    for (i = first; i <= last; ++i) {
        u8 *a = (u8 *) lpabc + (unsigned long) (i - first) * 12;   /* ABC = 3 LONG */
        *(int *) (a + 0) = 0;
        *(int *) (a + 4) = (int) (i < 32 || i > 126 ? f->cell_w : f->cell_w);
        *(int *) (a + 8) = 0;
    }
    return 1;
}

GDI_EXTERN(int, TextOutA, (void *hdc, int x, int y, const char *s, int c)) {
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    text_core((GDC *) hdc, x, y, s, c, NULL, NULL, 0);
    return 1;
}

GDI_EXTERN(int, ExtTextOutA, (void *hdc, int x, int y, unsigned int options, const void *lprect,
                              const char *s, unsigned int c, const int *lpDx)) {
    GDC *dc = (GDC *) hdc;
    const int *rc = (const int *) lprect;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    text_core(dc, x, y, s, (int) c, lpDx,
              (options & GDI_ETO_CLIPPED) ? rc : NULL,
              (options & GDI_ETO_OPAQUE) ? 1 : 0);
    return 1;
}

GDI_EXTERN(int, DrawTextA, (void *hdc, const char *s, int cch, void *lprc, unsigned int fmt)) {
    GDC *dc = (GDC *) hdc;
    int *rc = (int *) lprc;
    GFont *f;
    int w, x, y;
    if (!obj_valid(hdc, OBJ_DC) || !lprc || !s) return 0;
    f = dc->font ? dc->font : g_default_font;
    if (!f) return 0;
    if (cch < 0) { cch = 0; while (s[cch]) ++cch; }
    w = cch * f->cell_w;
    if (fmt & 0x00000400u /*DT_CALCRECT*/) {
        int rw = rc[2] - rc[0];
        if (!(fmt & 0x00000020u /*DT_SINGLELINE*/) && w > rw && rw > 0) {
            int lines = (w + rw - 1) / rw;
            rc[3] = rc[1] + lines * f->cell_h;
        } else {
            rc[2] = rc[0] + w;
            rc[3] = rc[1] + f->cell_h;
        }
        return f->cell_h;
    }
    x = rc[0];
    if (fmt & 0x00000001u /*DT_CENTER*/) x = rc[0] + ((rc[2] - rc[0]) - w) / 2;
    else if (fmt & 0x00000002u /*DT_RIGHT*/) x = rc[2] - w;
    y = rc[1];
    if (fmt & 0x00000004u /*DT_VCENTER*/) y = rc[1] + ((rc[3] - rc[1]) - f->cell_h) / 2;
    else if (fmt & 0x00000008u /*DT_BOTTOM*/) y = rc[3] - f->cell_h;
    text_core(dc, x, y, s, cch, NULL, (fmt & 0x00010000u /*DT_NOCLIP*/) ? NULL : rc, 0);
    return f->cell_h;
}

GDI_EXTERN(unsigned long, SetTextColor, (void *hdc, unsigned long color)) {
    GDC *dc = (GDC *) hdc;
    unsigned long old;
    if (!obj_valid(hdc, OBJ_DC)) return 0xFFFFFFFFul;
    old = dc->text_color; dc->text_color = color;
    return old;
}
GDI_EXTERN(unsigned long, SetBkColor, (void *hdc, unsigned long color)) {
    GDC *dc = (GDC *) hdc;
    unsigned long old;
    if (!obj_valid(hdc, OBJ_DC)) return 0xFFFFFFFFul;
    old = dc->bk_color; dc->bk_color = color;
    return old;
}
GDI_EXTERN(int, SetBkMode, (void *hdc, int mode)) {
    GDC *dc = (GDC *) hdc;
    int old;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    old = dc->bk_mode; dc->bk_mode = mode;
    return old;
}
GDI_EXTERN(unsigned int, SetTextAlign, (void *hdc, unsigned int align)) {
    GDC *dc = (GDC *) hdc;
    unsigned int old;
    if (!obj_valid(hdc, OBJ_DC)) return 0xFFFFFFFFu;
    old = dc->text_align; dc->text_align = align;
    return old;
}
GDI_EXTERN(unsigned long, GetTextColor, (void *hdc)) { return obj_valid(hdc, OBJ_DC) ? ((GDC *) hdc)->text_color : 0; }
GDI_EXTERN(unsigned long, GetBkColor, (void *hdc)) { return obj_valid(hdc, OBJ_DC) ? ((GDC *) hdc)->bk_color : 0; }
GDI_EXTERN(int, GetBkMode, (void *hdc)) { return obj_valid(hdc, OBJ_DC) ? ((GDC *) hdc)->bk_mode : 0; }
GDI_EXTERN(unsigned int, GetTextAlign, (void *hdc)) { return obj_valid(hdc, OBJ_DC) ? ((GDC *) hdc)->text_align : 0; }

/* ============================ regioni (API) ============================ */
GDI_EXTERN(void *, CreateRectRgn, (int x1, int y1, int x2, int y2)) {
    GRegion *r = (GRegion *) obj_new(OBJ_REGION, sizeof(GRegion));
    if (!r) return NULL;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    region_rect(r, x1, y1, x2, y2);
    return r;
}
GDI_EXTERN(void *, CreateRectRgnIndirect, (const void *lprc)) {
    const int *r = (const int *) lprc;
    if (!lprc) return NULL;
    return CreateRectRgn(r[0], r[1], r[2], r[3]);
}
GDI_EXTERN(int, CombineRgn, (void *dst, void *s1, void *s2, int mode)) {
    GRegion *d = (GRegion *) dst;
    GRegion *a = (GRegion *) s1, *b = (GRegion *) s2;
    if (!obj_valid(dst, OBJ_REGION)) return 0;
    switch (mode) {
    case 1: if (obj_valid(s1, OBJ_REGION) && obj_valid(s2, OBJ_REGION)) region_and(d, a, b); else region_clear(d); break;
    case 2: if (obj_valid(s1, OBJ_REGION) && obj_valid(s2, OBJ_REGION)) region_or(d, a, b);
            else if (obj_valid(s1, OBJ_REGION)) region_copy_from(d, a);
            else if (obj_valid(s2, OBJ_REGION)) region_copy_from(d, b);
            else region_clear(d);
            break;
    case 3: if (obj_valid(s1, OBJ_REGION) && obj_valid(s2, OBJ_REGION)) region_xor(d, a, b); else region_clear(d); break;
    case 4: if (obj_valid(s1, OBJ_REGION) && obj_valid(s2, OBJ_REGION)) region_diff(d, a, b);
            else if (obj_valid(s1, OBJ_REGION)) region_copy_from(d, a);
            else region_clear(d);
            break;
    case 5: if (obj_valid(s1, OBJ_REGION)) region_copy_from(d, a); else region_clear(d); break;
    default: return 0;
    }
    return region_type(d);
}
GDI_EXTERN(int, OffsetRgn, (void *rgn, int x, int y)) {
    GRegion *r = (GRegion *) rgn;
    int i;
    if (!obj_valid(rgn, OBJ_REGION)) return 0;
    for (i = 0; i < r->n; ++i) { r->s[i].y += y; r->s[i].x0 += x; r->s[i].x1 += x; }
    return region_type(r);
}
GDI_EXTERN(int, PtInRegion, (void *rgn, int x, int y)) {
    if (!obj_valid(rgn, OBJ_REGION)) return 0;
    return region_contains((GRegion *) rgn, x, y);
}
GDI_EXTERN(int, RectInRegion, (void *rgn, const void *lprc)) {
    const int *r = (const int *) lprc;
    if (!obj_valid(rgn, OBJ_REGION) || !lprc) return 0;
    return region_rect_in((GRegion *) rgn, r[0], r[1], r[2], r[3]);
}
GDI_EXTERN(unsigned long, GetRegionData, (void *rgn, unsigned long nCount, void *lpRgnData)) {
    GRegion *r = (GRegion *) rgn;
    int i, n, minx = 0, miny = 0, maxx = 0, maxy = 0;
    unsigned long need;
    if (!obj_valid(rgn, OBJ_REGION)) return 0;
    n = r->n;
    need = 32 + (unsigned long) n * 16;
    if (!lpRgnData || nCount < need) return need;
    for (i = 0; i < n; ++i) {
        if (i == 0) { minx = r->s[i].x0; maxx = r->s[i].x1; miny = r->s[i].y; maxy = r->s[i].y + 1; }
        else {
            if (r->s[i].x0 < minx) minx = r->s[i].x0;
            if (r->s[i].x1 > maxx) maxx = r->s[i].x1;
            if (r->s[i].y < miny) miny = r->s[i].y;
            if (r->s[i].y + 1 > maxy) maxy = r->s[i].y + 1;
        }
    }
    {   u8 *d = (u8 *) lpRgnData;
        memset(d, 0, need);
        *(u32 *) (d + 0) = (u32) need;             /* dwSize */
        *(u32 *) (d + 4) = 1;                      /* iType = RDH_RECTANGLES */
        *(u32 *) (d + 8) = (u32) n;                /* nCount */
        *(u32 *) (d + 12) = (u32) (n * 16);        /* nRgnSize */
        *(int *) (d + 16) = minx; *(int *) (d + 20) = miny;
        *(int *) (d + 24) = maxx; *(int *) (d + 28) = maxy;
        for (i = 0; i < n; ++i) {
            u8 *rc = d + 32 + i * 16;
            *(int *) (rc + 0) = r->s[i].x0;
            *(int *) (rc + 4) = r->s[i].y;
            *(int *) (rc + 8) = r->s[i].x1;
            *(int *) (rc + 12) = r->s[i].y + 1;
        }
    }
    return need;
}
GDI_EXTERN(int, SelectClipRgn, (void *hdc, void *rgn)) {
    GDC *dc = (GDC *) hdc;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (!rgn) {
        /* SelectClipRgn(NULL) TOGLIE il clip: non e' una regione vuota (che clipperebbe tutto) */
        if (dc->clip) { DeleteObject(dc->clip); dc->clip = NULL; }
        return 1;                              /* NULLREGION */
    }
    if (!obj_valid(rgn, OBJ_REGION)) return 0;
    /* il DC COPIA la regione: cosi' non dipende dalla vita dell'oggetto del chiamante */
    if (!dc->clip) dc->clip = (GRegion *) obj_new(OBJ_REGION, sizeof(GRegion));
    region_copy_from(dc->clip, (GRegion *) rgn);
    return region_type(dc->clip);
}
GDI_EXTERN(int, IntersectClipRect, (void *hdc, int l, int t, int r, int b)) {
    GDC *dc = (GDC *) hdc;
    GRegion *rect;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    rect = (GRegion *) CreateRectRgn(l, t, r, b);
    if (!rect) return 0;
    if (!dc->clip) {
        dc->clip = (GRegion *) obj_new(OBJ_REGION, sizeof(GRegion));
        if (dc->clip) region_copy_from(dc->clip, rect);
    } else {
        GRegion *res = (GRegion *) obj_new(OBJ_REGION, sizeof(GRegion));
        if (res) { region_and(res, dc->clip, rect); region_copy_from(dc->clip, res); DeleteObject(res); }
    }
    DeleteObject(rect);
    return dc->clip ? region_type(dc->clip) : 0;
}

/* ============================ palette (API) ============================ */
GDI_EXTERN(void *, CreatePalette, (const void *lplp)) {
    const u8 *l = (const u8 *) lplp;
    GPalette *p;
    int n, i;
    if (!l) return NULL;
    n = (int) *(const u16 *) (l + 2);
    if (n < 0) n = 0;
    if (n > 256) n = 256;
    p = (GPalette *) obj_new(OBJ_PALETTE, sizeof(GPalette));
    if (!p) return NULL;
    for (i = 0; i < n; ++i) {
        p->r[i] = l[4 + i * 4 + 0];
        p->g[i] = l[4 + i * 4 + 1];
        p->b[i] = l[4 + i * 4 + 2];
        p->f[i] = l[4 + i * 4 + 3];
    }
    return p;
}
GDI_EXTERN(unsigned int, SetPaletteEntries, (void *hpal, unsigned int start, unsigned int n, const void *pe)) {
    GPalette *p = (GPalette *) hpal;
    const u8 *e = (const u8 *) pe;
    unsigned int i;
    if (!obj_valid(hpal, OBJ_PALETTE) || !pe) return 0;
    for (i = 0; i < n && start + i < 256; ++i) {
        p->r[start+i] = e[i*4+0]; p->g[start+i] = e[i*4+1]; p->b[start+i] = e[i*4+2]; p->f[start+i] = e[i*4+3];
    }
    return i;
}
GDI_EXTERN(unsigned int, GetPaletteEntries, (void *hpal, unsigned int start, unsigned int n, void *pe)) {
    GPalette *p = (GPalette *) hpal;
    u8 *e = (u8 *) pe;
    unsigned int i;
    if (!obj_valid(hpal, OBJ_PALETTE) || !pe) return 0;
    for (i = 0; i < n && start + i < 256; ++i) {
        e[i*4+0] = p->r[start+i]; e[i*4+1] = p->g[start+i]; e[i*4+2] = p->b[start+i]; e[i*4+3] = p->f[start+i];
    }
    return i;
}
GDI_EXTERN(unsigned int, GetSystemPaletteEntries, (void *hdc, unsigned int start, unsigned int n, void *pe)) {
    GDC *dc = (GDC *) hdc;
    u8 *e = (u8 *) pe;
    unsigned int i;
    if (!obj_valid(hdc, OBJ_DC) || !pe) return 0;
    for (i = 0; i < n && start + i < 256; ++i) {
        u32 c;
        if (dc->pal) { e[i*4+0] = dc->pal->r[start+i]; e[i*4+1] = dc->pal->g[start+i]; e[i*4+2] = dc->pal->b[start+i]; e[i*4+3] = 0; }
        else if (dc_palette_rgb(dc, (int) (start + i), &c)) {
            e[i*4+0] = (u8) ((c >> 16) & 0xFF); e[i*4+1] = (u8) ((c >> 8) & 0xFF); e[i*4+2] = (u8) (c & 0xFF); e[i*4+3] = 0;
        }
    }
    return i;
}
GDI_EXTERN(unsigned int, RealizePalette, (void *hdc)) {
    GDC *dc = (GDC *) hdc;
    if (!obj_valid(hdc, OBJ_DC)) return 0;
    if (dc->is_window && dc->pal) {
        palette_to_host(dc->pal);
        if (dc->pal_realized) return 0;
        dc->pal_realized = 1;
        return 256;
    }
    if (dc->pal) { if (dc->pal_realized) return 0; dc->pal_realized = 1; return 256; }
    return 0;
}
GDI_EXTERN(void *, SelectPalette, (void *hdc, void *hpal, int forceBkgd)) {
    GDC *dc = (GDC *) hdc;
    GPalette *old;
    (void) forceBkgd;
    if (!obj_valid(hdc, OBJ_DC)) return NULL;
    old = dc->pal;
    dc->pal = obj_valid(hpal, OBJ_PALETTE) ? (GPalette *) hpal : NULL;
    dc->pal_realized = 0;
    return old;
}
