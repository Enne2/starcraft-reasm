/* sc-reasm/tools/elf/gdi_elf.h — GDI nativo (8 bit + palette) per il runtime ELF (Q2b).
 *
 * Superficie coperta: le 37 API gdi32 realmente importate (23 exe + 29 storm, docs/12 Appendice B),
 * di cui 16 sono la wave W3 di docs/11, piu' le API del modello completo richieste dal task
 * (BitBlt/StretchBlt/FillRect/PatBlt/LineTo/DrawTextA/DIB/regioni/palette).
 *
 * CONVENZIONE: tutte le API hanno la convenzione WINAPI = STDCALL a N argomenti -> il callee fa
 * `ret 4*N`. La tabella in fondo elenca il valore di N per ogni funzione: Q1 la usa per la sua
 * tabella. Handle = puntatori opachi a oggetti in un registro tipizzato (mai 0 per un oggetto
 * valido). `NULL` e' sempre "nessun oggetto".
 *
 * Non include nulla e non espone tipi SDL: il DC finestra disegna sul canvas di host_elf.h.
 */
#ifndef GDI_ELF_H
#define GDI_ELF_H

#ifdef __i386__
#define GDI_STDCALL __attribute__((stdcall))
#else
#define GDI_STDCALL
#endif

#define GDI_EXTERN(ret, name, args) ret GDI_STDCALL name args

/* ---- ciclo di vita del modulo ---- */
/* Crea il DC della finestra (sul canvas del host) e il registro oggetti. 0 = ok.
 * Va chiamata dopo host_elf_display_mode(); se il canvas non esiste, il DC finestra resta
 * "senza superficie" e ogni disegno e' un no-op silenzioso. */
int gdi_elf_init(void);
void gdi_elf_shutdown(void);
void *gdi_elf_window_dc(void);          /* HDC della finestra (0 se non inizializzato) */

/* ======================= DC e oggetti ======================= */
GDI_EXTERN(void *,   CreateCompatibleDC,      (void *hdc));                       /* ret  4 */
GDI_EXTERN(void *,   CreateCompatibleBitmap,  (void *hdc, int cx, int cy));       /* ret 12 */
GDI_EXTERN(int,      DeleteDC,                (void *hdc));                       /* ret  4 */
GDI_EXTERN(int,      DeleteObject,            (void *hobj));                      /* ret  4 */
GDI_EXTERN(void *,   SelectObject,            (void *hdc, void *hobj));           /* ret  8 */
GDI_EXTERN(void *,   GetStockObject,          (int i));                           /* ret  4 */
GDI_EXTERN(void *,   GetCurrentObject,        (void *hdc, unsigned int type));   /* ret  8 */
GDI_EXTERN(int,      GetObjectA,              (void *hobj, int c, void *pv));     /* ret 12 */
GDI_EXTERN(int,      GetDeviceCaps,           (void *hdc, int index));           /* ret  8 */
GDI_EXTERN(int,      GdiFlush,                (void));                           /* ret  0 */

/* ======================= penne / pennelli / bitmap DIB ======================= */
GDI_EXTERN(void *,   CreatePen,               (int style, int width, unsigned long color)); /* ret 12 */
GDI_EXTERN(void *,   CreateSolidBrush,        (unsigned long color));                        /* ret  4 */
/* CreateDIBitmap(hdc, pbmih, flInit, pjBits, pbmi, iUsage): 6 argomenti */
GDI_EXTERN(void *,   CreateDIBitmap,          (void *hdc, const void *pbmih, unsigned long flInit,
                                               const void *pjBits, const void *pbmi, unsigned int iUsage));
GDI_EXTERN(long,     GetBitmapBits,           (void *hbm, long cb, void *pv));    /* ret 12 */
/* Set/GetDIBits(hdc, hbm, start, cLines, bits, bmi, usage): 7 argomenti */
GDI_EXTERN(int,      SetDIBits,               (void *hdc, void *hbm, unsigned int start,
                                               unsigned int cLines, const void *bits,
                                               const void *bmi, unsigned int usage));
GDI_EXTERN(int,      GetDIBits,               (void *hdc, void *hbm, unsigned int start,
                                               unsigned int cLines, void *bits,
                                               void *bmi, unsigned int usage));
/* SetDIBitsToDevice(hdc, xDst,yDst,w,h, xSrc,ySrc, startScan,cLines, bits, bmi, usage): 12 */
GDI_EXTERN(int,      SetDIBitsToDevice,       (void *hdc, int xd, int yd, unsigned long w, unsigned long h,
                                               int xs, int ys, unsigned int startScan, unsigned int cLines,
                                               const void *bits, const void *bmi, unsigned int usage));

/* ======================= disegno ======================= */
/* BitBlt(dst, x,y,cx,cy, src, sx,sy, rop): 9 */
GDI_EXTERN(int,      BitBlt,                  (void *hdst, int x, int y, int cx, int cy,
                                               void *hsrc, int sx, int sy, unsigned long rop));
/* StretchBlt(dst, xd,yd,wd,hd, src, xs,ys,ws,hs, rop): 11 */
GDI_EXTERN(int,      StretchBlt,             (void *hdst, int xd, int yd, int wd, int hd,
                                               void *hsrc, int xs, int ys, int ws, int hs,
                                               unsigned long rop));
GDI_EXTERN(int,      FillRect,                (void *hdc, const void *lprc, void *hbr)); /* ret 12 */
GDI_EXTERN(int,      Rectangle,               (void *hdc, int left, int top, int right, int bottom)); /* ret 20 */
GDI_EXTERN(int,      PatBlt,                  (void *hdc, int x, int y, int w, int h, unsigned long rop)); /* ret 24 */
GDI_EXTERN(int,      MoveToEx,                (void *hdc, int x, int y, void *lppt));    /* ret 16 */
GDI_EXTERN(int,      LineTo,                  (void *hdc, int x, int y));               /* ret 12 */

/* ======================= testo e font ======================= */
/* CreateFontA(14 argomenti) */
GDI_EXTERN(void *,   CreateFontA,             (int h, int w, int esc, int orient, int weight,
                                               unsigned long italic, unsigned long underline,
                                               unsigned long strike, unsigned long charset,
                                               unsigned long outprec, unsigned long clipprec,
                                               unsigned long quality, unsigned long pitchfam,
                                               const char *face));
GDI_EXTERN(void *,   CreateFontIndirectA,     (const void *lplf));                /* ret  4 */
GDI_EXTERN(int,      GetTextMetricsA,         (void *hdc, void *lptm));           /* ret  8 */
GDI_EXTERN(int,      GetTextExtentPoint32A,   (void *hdc, const char *s, int c, void *psize)); /* ret 16 */
/* GetTextExtentExPointA(hdc, s, cch, maxExtent, lpFit, lpDx, lpSize): 7 */
GDI_EXTERN(int,      GetTextExtentExPointA,  (void *hdc, const char *s, int cch, int nMaxExtent,
                                               int *lpFit, int *lpDx, void *lpSize));
GDI_EXTERN(int,      GetCharABCWidthsA,       (void *hdc, unsigned int first, unsigned int last, void *lpabc)); /* ret 16 */
GDI_EXTERN(int,      TextOutA,                (void *hdc, int x, int y, const char *s, int c)); /* ret 20 */
/* ExtTextOutA(hdc, x, y, options, lprect, s, c, lpDx): 8 */
GDI_EXTERN(int,      ExtTextOutA,             (void *hdc, int x, int y, unsigned int options,
                                               const void *lprect, const char *s, unsigned int c,
                                               const int *lpDx));
GDI_EXTERN(int,      DrawTextA,               (void *hdc, const char *s, int cch, void *lprc, unsigned int fmt)); /* ret 20 */
GDI_EXTERN(unsigned long, SetTextColor,       (void *hdc, unsigned long color)); /* ret  8 */
GDI_EXTERN(unsigned long, SetBkColor,         (void *hdc, unsigned long color)); /* ret  8 */
GDI_EXTERN(int,      SetBkMode,               (void *hdc, int mode));             /* ret  8 */
GDI_EXTERN(unsigned int,  SetTextAlign,       (void *hdc, unsigned int align));  /* ret  8 */
GDI_EXTERN(unsigned long, GetTextColor,       (void *hdc));                       /* ret  4 */
GDI_EXTERN(unsigned long, GetBkColor,         (void *hdc));                       /* ret  4 */
GDI_EXTERN(int,      GetBkMode,               (void *hdc));                       /* ret  4 */
GDI_EXTERN(unsigned int,  GetTextAlign,       (void *hdc));                       /* ret  4 */

/* ======================= regioni ======================= */
GDI_EXTERN(void *,   CreateRectRgn,           (int x1, int y1, int x2, int y2));   /* ret 16 */
GDI_EXTERN(void *,   CreateRectRgnIndirect,   (const void *lprc));               /* ret  4 */
GDI_EXTERN(int,      CombineRgn,              (void *dst, void *s1, void *s2, int mode)); /* ret 16 */
GDI_EXTERN(int,      OffsetRgn,               (void *rgn, int x, int y));         /* ret 12 */
GDI_EXTERN(int,      PtInRegion,              (void *rgn, int x, int y));         /* ret 12 */
GDI_EXTERN(int,      RectInRegion,            (void *rgn, const void *lprc));     /* ret  8 */
GDI_EXTERN(unsigned long, GetRegionData,      (void *rgn, unsigned long nCount, void *lpRgnData)); /* ret 12 */
GDI_EXTERN(int,      SelectClipRgn,           (void *hdc, void *rgn));            /* ret  8 */
GDI_EXTERN(int,      IntersectClipRect,       (void *hdc, int l, int t, int r, int b));  /* ret 20 */

/* ======================= palette ======================= */
GDI_EXTERN(void *,   CreatePalette,           (const void *lplp));                /* ret  4 */
GDI_EXTERN(unsigned int, SetPaletteEntries,   (void *hpal, unsigned int start, unsigned int n, const void *pe)); /* ret 16 */
GDI_EXTERN(unsigned int, GetPaletteEntries,   (void *hpal, unsigned int start, unsigned int n, void *pe));        /* ret 16 */
GDI_EXTERN(unsigned int, GetSystemPaletteEntries, (void *hdc, unsigned int start, unsigned int n, void *pe));     /* ret 16 */
GDI_EXTERN(unsigned int, RealizePalette,      (void *hdc));                      /* ret  4 */
GDI_EXTERN(void *,   SelectPalette,           (void *hdc, void *hpal, int forceBkgd)); /* ret 12 */

/* ======================= costanti usate dai chiamanti =======================
 * (solo quelle che servono a leggere/scrivere le strutture; vedi docs/15 per il dettaglio) */
#define GDI_SRCCOPY      0x00CC0020ul
#define GDI_NOTSRCCOPY   0x00330008ul
#define GDI_SRCAND       0x008800C6ul
#define GDI_SRCPAINT     0x00EE0086ul
#define GDI_SRCINVERT    0x00660046ul
#define GDI_BLACKNESS    0x00000042ul
#define GDI_WHITENESS    0x00FF0062ul
#define GDI_DSTINVERT    0x00550009ul
#define GDI_PATCOPY      0x00F00021ul
#define GDI_PATINVERT    0x005A0049ul
#define GDI_ETO_OPAQUE   0x0002u
#define GDI_ETO_CLIPPED  0x0004u
#define GDI_TA_LEFT      0
#define GDI_TA_RIGHT     2
#define GDI_TA_CENTER    6
#define GDI_TA_UPDATECP  1
#define GDI_TRANSPARENT  1
#define GDI_OPAQUE       2

#endif /* GDI_ELF_H */

/* ======================= TABELLA ARITA' PER Q1 =======================
 *  API                        N argomenti   ret N*4   note
 *  CreateCompatibleDC              1            4
 *  CreateCompatibleBitmap          3           12
 *  DeleteDC                        1            4
 *  DeleteObject                    1            4
 *  SelectObject                    2            8   ritorna l'oggetto precedente
 *  GetStockObject                  1            4   WHITE_BRUSH 0, LTGRAY 1, GRAY 2, DKGRAY 3,
 *                                                    BLACK 4, NULL 5, HOLLOW 5, WHITE_PEN 6,
 *                                                    BLACK_PEN 7, NULL_PEN 8, OEM_FIXED 10,
 *                                                    ANSI_FIXED 11, ANSI_VAR 12, SYSTEM_FONT 13,
 *                                                    DEFAULT_GUI_FONT 17, DC_BRUSH 18, DC_PEN 19
 *  GetCurrentObject                2            8
 *  GetObjectA                      3           12
 *  GetDeviceCaps                   2            8
 *  GdiFlush                        0            0
 *  CreatePen                       3           12
 *  CreateSolidBrush                1            4
 *  CreateDIBitmap                  6           24
 *  GetBitmapBits                   3           12
 *  SetDIBits                       7           28
 *  GetDIBits                       7           28
 *  SetDIBitsToDevice              12           48
 *  BitBlt                          9           36
 *  StretchBlt                     11           44
 *  FillRect                        3           12
 *  Rectangle                       5           20
 *  PatBlt                          6           24
 *  MoveToEx                        4           16
 *  LineTo                          3           12
 *  CreateFontA                    14           56
 *  CreateFontIndirectA             1            4
 *  GetTextMetricsA                 2            8
 *  GetTextExtentPoint32A           4           16
 *  GetTextExtentExPointA           7           28
 *  GetCharABCWidthsA               4           16
 *  TextOutA                        5           20
 *  ExtTextOutA                     8           32
 *  DrawTextA                       5           20
 *  SetTextColor                    2            8
 *  SetBkColor                      2            8
 *  SetBkMode                       2            8
 *  SetTextAlign                    2            8
 *  GetTextColor / GetBkColor / GetBkMode / GetTextAlign   1   4
 *  CreateRectRgn                   4           16
 *  CreateRectRgnIndirect           1            4
 *  CombineRgn                      4           16   RGN_AND 1, RGN_OR 2, RGN_XOR 3, RGN_DIFF 4, RGN_COPY 5
 *  OffsetRgn                       3           12
 *  PtInRegion                      3           12
 *  RectInRegion                    2            8
 *  GetRegionData                   3           12
 *  SelectClipRgn                   2            8
 *  IntersectClipRect               5           20
 *  CreatePalette                   1            4
 *  SetPaletteEntries               4           16
 *  GetPaletteEntries               4           16
 *  GetSystemPaletteEntries         4           16
 *  RealizePalette                  1            4
 *  SelectPalette                   3           12
 *  Nome in gioco (IAT): identico al nome API, nessuna decorazione (stdcall su ELF i386 emette
 *  `ret N` ma NON cambia il nome del simbolo).
 * ===================================================================== */
