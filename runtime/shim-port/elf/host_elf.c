/* sc-reasm/tools/elf/host_elf.c — host video SDL2 per il runtime ELF nativo (Q2).
 *
 * PERCHE' LE DICHIARAZIONI SONO A MANO: con `-m32` i pacchetti `-devel.i686` non ci sono e
 * <SDL2/SDL.h> non e' includibile (`SDL_config.h` pretende `SDL_config-i386.h`). Il runtime e'
 * comunque `-ffreestanding -nostdinc`, quindi si dichiarano a mano SOLO le API usate. I layout
 * delle struct pubbliche SDL2 (SDL_Surface, SDL_PixelFormat) sono verificati: gli offset sono
 * gli stessi delle versioni a 64 bit, cambiano solo le dimensioni dei puntatori
 * (offsets 32-bit usati qui: format@4 w@8 h@12 pitch@16 pixels@20; pixelformat Rmask@12
 * BytesPerPixel@9 Rloss@28 Rshift@32).
 *
 * MODELLO: il canvas e' memoria NOSTRA (come fa DirectDraw con la superficie primaria), non una
 * SDL_Surface: cosi' il gioco puo' scriverci con Lock/Unlock senza le regole di lock di SDL e il
 * present e' una conversione indice->RGB esplicita con la palette (host_blit8 di LOLA).
 */
#include "host_elf.h"
#include "reasm_elf.h"          /* log del runtime (toolkit) */
#include "win32_shim.h"         /* timer Win32: consegna dei TIMERPROC dal pump */
#include "sc_ctl.h"              /* API di automazione in-process */
#include <sys/stat.h>              /* identita' del control-file e rilevamento rotazione */

/* ---- libc dinamica (niente header: si resta freestanding) ---- */
extern void *malloc(unsigned long);
extern void free(void *);
extern void *memset(void *, int, unsigned long);
extern void *memcpy(void *, const void *, unsigned long);
extern int memcmp(const void *, const void *, unsigned long);
extern unsigned long pthread_self(void);
extern char *getenv(const char *);
extern int strcmp(const char *, const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern int sscanf(const char *, const char *, ...);
extern long atol(const char *);
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern int close(int);
extern void win32_input_pump(void) __attribute__((weak));

/* ---- API SDL2 usate (verificate con `nm -D` su /lib/libSDL2-2.0.so.0) ---- */
extern int SDL_Init(unsigned int flags);
extern int SDL_SetHint(const char *, const char *);
extern void SDL_Quit(void);
extern const char *SDL_GetError(void);
extern const char *SDL_GetCurrentVideoDriver(void);
extern void *SDL_CreateWindow(const char *title, int x, int y, int w, int h, unsigned int flags);
extern void SDL_DestroyWindow(void *win);
extern void *SDL_GetWindowSurface(void *win);
extern int SDL_UpdateWindowSurface(void *win);
extern int SDL_SetWindowFullscreen(void *win, unsigned int flags);
extern int SDL_GL_SetAttribute(int attr, int value);
extern void *SDL_GL_CreateContext(void *win);
extern void SDL_GL_DeleteContext(void *ctx);
extern void *SDL_GL_GetProcAddress(const char *name);
extern void SDL_GL_SwapWindow(void *win);
extern int SDL_GL_SetSwapInterval(int interval);
extern int SDL_SetWindowSize(void *win, int w, int h);
extern int SDL_PollEvent(void *e);
extern int SDL_PushEvent(const void *e);
extern void SDL_StartTextInput(void);
extern void SDL_GetWindowPosition(void *, int *, int *);
extern int SDL_SetWindowPosition(void *, int, int);
extern void SDL_WarpMouseInWindow(void *, int, int);
extern int SDL_ShowCursor(int);
extern int SDL_CaptureMouse(int);
extern int SDL_SetWindowMouseGrab(void *, int);
extern int SDL_GetGlobalMouseState(int *, int *);
extern void *SDL_CreateRenderer(void *win, int index, unsigned int flags);
extern void SDL_DestroyRenderer(void *r);
extern void *SDL_CreateTexture(void *r, unsigned int fmt, int access, int w, int h);
extern void SDL_DestroyTexture(void *t);
extern int SDL_UpdateTexture(void *t, const void *rect, const void *pixels, int pitch);
extern int SDL_RenderClear(void *r);
extern int SDL_RenderCopy(void *r, void *t, const void *src, const void *dst);
extern void SDL_RenderPresent(void *r);
extern int SDL_SetRenderDrawColor(void *r, unsigned char, unsigned char, unsigned char, unsigned char);
extern int SDL_RenderSetLogicalSize(void *r, int w, int h);
extern void SDL_RenderGetViewport(void *r, void *rect);
extern int SDL_GetWindowSize(void *win, int *w, int *h);
extern int SDL_GetRendererOutputSize(void *r, int *w, int *h);
extern void SDL_RenderGetScale(void *r, float *sx, float *sy);
extern int SDL_RenderSetViewport(void *r, const void *rect);
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1

extern unsigned int SDL_GetWindowID(void *win);
extern unsigned int SDL_GetTicks(void);
extern void SDL_Delay(unsigned int ms);
extern unsigned int SDL_MapRGB(const void *fmt, unsigned char r, unsigned char g, unsigned char b);

/* ---- costanti SDL2 (verificate su SDL_video.h della 2.0.32) ---- */
#define SDL_INIT_VIDEO                  0x00000020u
#define SDL_WINDOWPOS_UNDEFINED         0x1FFF0000u
#define SDL_WINDOW_SHOWN                0x00000004u
#define SDL_WINDOW_OPENGL               0x00000002u
#define SDL_GL_CONTEXT_MAJOR_VERSION    17
#define SDL_GL_CONTEXT_MINOR_VERSION    18
#define SDL_GL_CONTEXT_PROFILE_MASK     21
#define SDL_GL_CONTEXT_PROFILE_ES       4
#define SDL_WINDOW_FULLSCREEN_DESKTOP   0x00001001u
#define SDL_QUIT                        0x100u
#define SDL_WINDOWEVENT                 0x200u
#define SDL_WINDOWEVENT_CLOSE           14
#define SDL_KEYDOWN                     0x300u
#define SDL_KEYUP                       0x301u
#define SDL_TEXTINPUT                   0x303u
#define SDL_MOUSEMOTION                 0x400u
#define SDL_MOUSEBUTTONDOWN             0x401u
#define SDL_MOUSEBUTTONUP               0x402u
#define WINDOWEVENT_OFF                 0x0C    /* SDL_WindowEvent.event */

/* ---- layout pubblico minimo (vedi intestazione) ---- */
typedef struct SDL_PixelFormat {
    unsigned int format;
    void *palette;
    unsigned char BitsPerPixel, BytesPerPixel, padding[2];
    unsigned int Rmask, Gmask, Bmask, Amask;
    unsigned char Rloss, Gloss, Bloss, Aloss;
    unsigned char Rshift, Gshift, Bshift, Ashift;
    int refcount;
    struct SDL_PixelFormat *next;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    unsigned int flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *list_blitmap;
    struct { int x, y, w, h; } clip_rect;
    void *map;
    int refcount;
} SDL_Surface;

/* SDL_Event: 56 byte. Buffer piu' grande = sicuro: SDL ne scrive sizeof(SDL_Event). */
typedef struct { unsigned int type; unsigned char pad[60]; } SDL_EventBuf;

/* offset verificati degli eventi (SDLK/Mouse: vedi report del passo 1) */
#define EV_TYPE(e)      (*(const unsigned int *) (e))
#define EV_MOTION_STATE(e) (*(const int *) ((const unsigned char *) (e) + 0x10))
#define EV_X(e)         (*(const int *) ((const unsigned char *) (e) + 0x14))
#define EV_Y(e)         (*(const int *) ((const unsigned char *) (e) + 0x18))
#define EV_BUTTON(e)    (*(const unsigned char *) ((const unsigned char *) (e) + 0x10))
#define EV_BTN_STATE(e) (*(const unsigned char *) ((const unsigned char *) (e) + 0x11))
#define EV_SYM(e)       (*(const int *) ((const unsigned char *) (e) + 0x14))

/* ---- stato globale del host ---- */
static void *g_win;
static unsigned char *g_canvas;
static int g_w, g_h, g_pitch, g_bpp;
static int g_locked;
static int g_inited;
static int g_audio_inited;
static int g_quit;
static int g_want_fullscreen, g_is_fullscreen;
static unsigned int g_mouse_x, g_mouse_y;
static int g_mouse_btn;
static unsigned int g_last_key;
static unsigned int g_window_id;
static char g_title[128];
static unsigned long g_video_thread;
#define INPUT_CAP 512
static struct host_input_event g_input[INPUT_CAP];
static unsigned int g_input_head, g_input_tail, g_input_lost;
static unsigned char g_keys[512];
static unsigned int g_mods, g_polled_mods, g_test_start;
static int g_polled_buttons;
static int g_test_stage;

int host_elf_input_owner(void) {
    return g_win && pthread_self() == g_video_thread;
}
int host_elf_input_pending(void) { return g_input_head != g_input_tail; }
unsigned int host_elf_input_dropped(void) { return g_input_lost; }
static int input_full(void) { return (g_input_tail+1)%INPUT_CAP == g_input_head; }
static void input_put(const struct host_input_event *e) {
    if (input_full()) { ++g_input_lost; return; }
    g_input[g_input_tail] = *e; g_input_tail = (g_input_tail+1)%INPUT_CAP;
}
int host_elf_next_input(struct host_input_event *out) {
    if (!out || !host_elf_input_owner() || !host_elf_input_pending()) return 0;
    *out=g_input[g_input_head]; g_input_head=(g_input_head+1)%INPUT_CAP;
    return 1;
}
static int key_slot(int key) {
    if (key>=0 && key<256) return key;
    if ((unsigned int)key & 0x40000000u) {
        key &= ~0x40000000; if (key>=0 && key<256) return 256+key;
    }
    return -1;
}
int host_elf_key_state(int key) {
    int s;
    if (key==1) return (g_mouse_btn&1)?0x80:0;
    if (key==2) return (g_mouse_btn&4)?0x80:0;
    if (key==4) return (g_mouse_btn&2)?0x80:0;
    if (key==16) return (g_mods&3)?0x80:0;
    if (key==17) return (g_mods&0xC0)?0x80:0;
    if (key==18) return (g_mods&0x300)?0x80:0;
    /* VK_A..Z and SDL lower-case letters both work. */
    if (key>='A' && key<='Z') key += 'a'-'A';
    switch(key) { /* navigation VK -> SDL keycode; literal letters stay SDL */
    case 37: key=0x40000050; break; case 38: key=0x40000052; break;
    case 39: key=0x4000004F; break; case 40: key=0x40000051; break;
    case 45: key=0x40000049; break; case 36: key=0x4000004A; break;
    case 33: key=0x4000004B; break; case 46: key=127; break;
    case 35: key=0x4000004D; break; case 34: key=0x4000004E; break;
    }
    s=key_slot(key);
    return s>=0 && g_keys[s] ? 0x80 : 0;
}
/* GetKeyState is message-driven on Windows: retain down/up order at dispatch,
 * including a complete click polled in a single batch. */
void host_elf_dispatch_state(unsigned int m, unsigned int key) {
    int s, down;
    if (m==0x100 || m==0x101 || m==0x104 || m==0x105) {
        down=(m==0x100 || m==0x104);
        /* Modifier mask comes from the queued SDL snapshot, preserving left/right.
         * Never clear both SHIFT keys when only one was released. */
        if(key!=16 && key!=17 && key!=18) {
            if (key>='A' && key<='Z') key+='a'-'A';
            s=key_slot((int)key); if(s>=0) g_keys[s]=(unsigned char)down;
        }
    } else if (m>=0x200 && m<=0x209) {
        g_mouse_btn=((key&1)?1:0)|((key&2)?4:0)|((key&0x10)?2:0);
    }
}
void host_elf_input_state(const struct host_input_event *e) {
    int s;
    if(!e) return;
    g_mods=(unsigned int)e->modifiers;
    if(e->type==HOST_INPUT_KEYDOWN || e->type==HOST_INPUT_KEYUP) {
        s=key_slot(e->key); if(s>=0) g_keys[s]=(e->type==HOST_INPUT_KEYDOWN);
    }
    if(e->type==HOST_INPUT_MOVE || e->type==HOST_INPUT_DOWN || e->type==HOST_INPUT_UP)
        g_mouse_btn=e->buttons;
    if(e->type==HOST_INPUT_FOCUS && !e->key) { memset(g_keys,0,sizeof g_keys); g_mods=0; g_mouse_btn=0; }
}
/* Spazio di coordinate Win32 di questo port: il gioco assume di essere in fullscreen a (0,0)
 * — vedi SetCursorPos(320,240) a 0x4E087A e (320,200) a 0x47EB39/0x4C58A1, che sono coordinate
 * CLIENT. Se qui si restituissero coordinate schermo reali (finestra spostata dal window manager),
 * ogni ScreenToClient/SetCursorPos del gioco finirebbe in uno spazio diverso: il cursore viene
 * "riportato" nel posto sbagliato a ogni restore e la visuale scorre da sola. Quindi il cursore e'
 * riportato in coordinate CLIENT (0..w-1, 0..h-1), coerenti con GetWindowRect a origine 0,0. */
/* definiti piu' sotto (sezione renderer): servono gia' a warp/poll */
static void win_to_canvas(int wx, int wy, int *cx, int *cy);
static void canvas_to_win(int cx, int cy, int *wx, int *wy);

int host_elf_mouse_screen(int *x, int *y) {
    if (!g_win) return -1;
    if (x) *x = (int) g_mouse_x;
    if (y) *y = (int) g_mouse_y;
    return 0;
}
/* ClipCursor: il gioco confina il puntatore al rettangolo client (global 0x6cddb0, RECT in
 * coordinate SCHERMO) e lo rilascia con NULL. Il nostro shim lo ignorava: il puntatore poteva
 * uscire dalla finestra, il gioco leggeva coordinate fuori dal client e restava bloccato sul bordo
 * (scrolling continuo). Qui si applica il grab SDL, l'equivalente piu' vicino su X11/Wayland. */
int host_elf_mouse_clip(int on, int x0, int y0, int x1, int y1) {
    int wx=0, wy=0, cx=0, cy=0;
    if (!g_win) return -1;
    SDL_GetWindowPosition(g_win, &wx, &wy);
    SDL_SetWindowMouseGrab(g_win, on ? 1 : 0);
    if (on) {
        /* se il cursore e' fuori dal rettangolo richiesto (coordinate client), riportalo dentro */
        if (host_elf_mouse_screen(&cx, &cy) == 0 &&
            (cx < x0 || cx >= x1 || cy < y0 || cy >= y1) && x1 > x0 && y1 > y0) {
            { int wx, wy; canvas_to_win((x0 + x1) / 2, (y0 + y1) / 2, &wx, &wy);
              SDL_WarpMouseInWindow(g_win, wx, wy); }
            g_mouse_x = (unsigned int) ((x0 + x1) / 2);
            g_mouse_y = (unsigned int) ((y0 + y1) / 2);
        }
        if (getenv("REASM_TRACE")) {
            reasm_elf_log("[cursor] ClipCursor ON rect="); reasm_elf_log_hex((unsigned)x0);
            reasm_elf_log(","); reasm_elf_log_hex((unsigned)y0); reasm_elf_log(",");
            reasm_elf_log_hex((unsigned)x1); reasm_elf_log(","); reasm_elf_log_hex((unsigned)y1);
            reasm_elf_log(" grab=1\n");
        }
    } else if (getenv("REASM_TRACE")) {
        reasm_elf_log("[cursor] ClipCursor OFF (grab=0)\n");
    }
    return 0;
}
int host_elf_window_origin(int *x,int *y) {
    int wx=0,wy=0;
    if (!g_win) return -1;
    SDL_GetWindowPosition(g_win,&wx,&wy);
    if(x) *x=wx;
    if(y) *y=wy;
    return 0;
}
/* Solo per test/riproduzione: SC_WINDOW_POS="x,y" sposta la finestra dopo la creazione, cosi' si
 * puo' verificare in modo deterministico un difetto che dipende dalla posizione della finestra
 * (es. GetCursorPos in coordinate schermo vs ScreenToClient con rect del client a 0,0). */
static void window_pos_from_env(void) {
    const char *v = getenv("SC_WINDOW_POS");
    int x = 0, y = 0, i = 0, sign = 1;
    if (!v || !*v || !g_win) return;
    if (v[0] == '-') { sign = -1; ++i; }
    while (v[i] >= '0' && v[i] <= '9') { x = x*10 + (v[i]-'0'); ++i; }
    x *= sign; sign = 1;
    if (v[i] != ',') return;
    ++i;
    if (v[i] == '-') { sign = -1; ++i; }
    while (v[i] >= '0' && v[i] <= '9') { y = y*10 + (v[i]-'0'); ++i; }
    y *= sign;
    SDL_SetWindowPosition(g_win, x, y);
    reasm_elf_log("[host] finestra spostata da SC_WINDOW_POS\n");
}
int host_elf_mouse_warp(int x,int y) {
    int w = g_w, h = g_h;
    if(!host_elf_input_owner()) return -1;
    /* Coordinate CLIENT (spazio del gioco): il cursore non esce mai dal canvas. */
    if (w > 0 && x < 0) x = 0;
    if (w > 0 && x >= w) x = w - 1;
    if (h > 0 && y < 0) y = 0;
    if (h > 0 && y >= h) y = h - 1;
    { int wx, wy; canvas_to_win(x, y, &wx, &wy); SDL_WarpMouseInWindow(g_win, wx, wy); }
    g_mouse_x = (unsigned int) x; g_mouse_y = (unsigned int) y;
    if (getenv("SC_MOUSE_TRACE")) {
        char b[96]; snprintf(b, sizeof b, "[mouse] warp client=%d,%d\n", x, y); reasm_elf_log(b);
    }
    return 0;
}
void host_elf_mouse_capture(int on) { if(host_elf_input_owner()) SDL_CaptureMouse(on!=0); }
void host_elf_cursor_visible(int on) { if(host_elf_input_owner()) SDL_ShowCursor(on!=0); }

static unsigned int g_pal[256];      /* voci 0x00RRGGBB */
static int g_pal_dirty = 1;
static void *g_gl_context;
static int g_gl_requested, g_gl_failed, g_gl_explicit;
static int gl_ready;
static unsigned char *g_present_snapshot;
static unsigned long g_present_snapshot_size;
static unsigned int g_presented_at;
static unsigned int g_present_palette[256];
static int g_present_snapshot_valid, g_present_force = 1;
static int g_pal_set;                /* 0 = nessuna palette impostata (default: scala di grigi) */
static int g_sc_ctl_initialized;
static unsigned int g_map[256];      /* palette -> pixel nel formato della finestra */
static int g_map_valid;
static const void *g_map_fmt;

static const char *g_err = "";

static void copy_title(const char *s) {
    int i = 0;
    if (!s) s = "reasm-elf";
    while (s[i] && i < (int) sizeof g_title - 1) { g_title[i] = s[i]; ++i; }
    g_title[i] = 0;
}

static void log_drv(const char *what) {
    reasm_elf_log("[host] ");
    reasm_elf_log(what);
    reasm_elf_log(": video driver = ");
    { const char *d = SDL_GetCurrentVideoDriver(); reasm_elf_log(d ? d : "(null)"); }
    if (!g_inited) { reasm_elf_log(" | errore SDL: "); reasm_elf_log(SDL_GetError()); }
    reasm_elf_log("\n");
}

int host_elf_init(const char *title) {
    /* Su X11 chiediamo al compositore di NON comporre la finestra fullscreen (unredirect): e' lo
     * stesso effetto che su Wine si otteneva con gamescope. Senza questo, KWin/Mutter compositano
     * ogni frame e il gioco cala di frame rate in modo drasticamente percettibile. */
    SDL_SetHint("SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR", "1");
    copy_title(title);
    g_quit = 0; g_locked = 0; g_mouse_btn = 0; g_last_key = 0;
    g_mouse_x = 0; g_mouse_y = 0;
    g_input_head=g_input_tail=g_input_lost=0; g_mods=g_polled_mods=0; g_polled_buttons=0;
    memset(g_keys,0,sizeof g_keys);
    g_test_stage=0;
    if (!g_sc_ctl_initialized) { sc_ctl_start(); g_sc_ctl_initialized = 1; }
    g_w = g_h = g_pitch = 0; g_bpp = 8;
    g_want_fullscreen = g_is_fullscreen = 0;
    g_pal_set = 0; g_map_valid = 0; g_map_fmt = 0;
    g_pal_dirty = 1; g_gl_failed = 0; g_gl_explicit = 0;
    g_present_snapshot_valid = 0; g_present_force = 1;
    g_presented_at = 0;
    { int i; for (i = 0; i < 256; ++i) g_pal[i] = (unsigned int) (i * 0x010101u); }
    if (g_inited) return 0;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        g_err = SDL_GetError();
        reasm_elf_log("[host] SDL_Init fallita: ");
        reasm_elf_log(g_err);
        reasm_elf_log("\n");
        return -1;
    }
    g_inited = 1;
    log_drv("init");
    return 0;
}

static int canvas_alloc(int w, int h) {
    int pitch = (w + 3) & ~3;                 /* righe allineate a 4: pitch puo' essere > w */
    unsigned long size;
    if (w <= 0 || h <= 0) return -1;
    size = (unsigned long) pitch * (unsigned long) h;
    if (g_canvas) { free(g_canvas); g_canvas = 0; }
    if (g_present_snapshot) { free(g_present_snapshot); g_present_snapshot = 0; }
    g_present_snapshot_size = size; g_present_snapshot_valid = 0; g_present_force = 1;
    g_canvas = (unsigned char *) malloc(size);
    if (!g_canvas) { g_err = "malloc canvas"; return -1; }
    memset(g_canvas, 0, size);
    g_w = w; g_h = h; g_pitch = pitch;
    g_locked = 0;
    return 0;
}

int host_elf_display_mode(int w, int h, int bpp) {
    if (w <= 0) w = 640;
    if (h <= 0) h = 480;
    if (bpp != 8) {                            /* il canvas del host e' 8 bit indicizzato */
        reasm_elf_log("[host] display_mode: bpp != 8 richiesto; canvas comunque 8 bit\n");
    }
    if (!g_inited && host_elf_init(g_title) != 0) return -1;
    if (!g_win) {
        unsigned int flags = SDL_WINDOW_SHOWN;
        const char *present = getenv("SC_PRESENT");
        const char *driver = SDL_GetCurrentVideoDriver();
        g_gl_explicit = present && present[0] == 'g' && present[1] == 'l' && present[2] == 0;
        g_gl_requested = g_gl_explicit ||
                         ((!present || (present[0] == 'a' && present[1] == 'u' &&
                                        present[2] == 't' && present[3] == 'o' && !present[4])) &&
                          driver && strcmp(driver, "dummy") != 0);
        if (g_gl_requested) {
            /* Il profilo viene scelto per ogni tentativo in gl_context_attempt(). */
            flags |= SDL_WINDOW_OPENGL;
        }
        if (g_want_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        g_win = SDL_CreateWindow(g_title, (int) SDL_WINDOWPOS_UNDEFINED, (int) SDL_WINDOWPOS_UNDEFINED,
                                 w, h, flags);
        if (!g_win && (flags & SDL_WINDOW_OPENGL)) {
            reasm_elf_log("[host] GL SDL_CreateWindow OpenGL fallita: "); reasm_elf_log(SDL_GetError()); reasm_elf_log("; retry senza GL\n");
            g_gl_requested = 0; g_gl_failed = 1;
            flags &= ~SDL_WINDOW_OPENGL;
            g_win = SDL_CreateWindow(g_title, (int)SDL_WINDOWPOS_UNDEFINED, (int)SDL_WINDOWPOS_UNDEFINED, w, h, flags);
        }
        if (!g_win) {
            g_err = SDL_GetError();
            reasm_elf_log("[host] SDL_CreateWindow fallita: ");
            reasm_elf_log(g_err);
            reasm_elf_log("\n");
            return -1;
        }
        window_pos_from_env();
        g_video_thread = pthread_self();
        g_window_id = SDL_GetWindowID(g_win);
        g_test_start = SDL_GetTicks();
        SDL_StartTextInput();
        g_is_fullscreen = g_want_fullscreen;
        g_map_valid = 0;
        reasm_elf_log("[host] finestra creata\n");
    } else if (w != g_w || h != g_h) {
        SDL_SetWindowSize(g_win, w, h);
        g_map_valid = 0; g_present_force = 1;
    }
    g_bpp = 8;
    return canvas_alloc(w, h);
}

void host_elf_shutdown(void) {
    host_elf_audio_close();
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context = 0; }
    if (g_canvas) { free(g_canvas); g_canvas = 0; }
    g_locked = 0; g_w = g_h = g_pitch = 0;
    if (g_win) { SDL_DestroyWindow(g_win); g_win = 0; }
    g_map_valid = 0; g_map_fmt = 0;
    gl_ready = 0; g_gl_requested = 0; g_gl_failed = 0; g_gl_explicit = 0;
    if (g_present_snapshot) { free(g_present_snapshot); g_present_snapshot = 0; }
    g_present_snapshot_valid = 0; g_present_force = 1;
    if (g_inited || g_audio_inited) { SDL_Quit(); g_inited = 0; g_audio_inited = 0; }
}

int host_elf_coop_level(unsigned long hwnd, unsigned long flags) {
    (void) hwnd;                              /* in ELF non esiste un HWND Windows */
    {   /* SC_FORCE_WINDOWED=1: esegue in FINESTRA anche se il gioco chiede il fullscreen
         * (diagnostica/verifica: il fullscreen esclusivo occupa lo schermo e la cattura su
         * Wayland esce nera). Default invariato: senza la variabile vale la richiesta del gioco. */
        extern char *getenv(const char *);
        if (getenv("SC_FORCE_WINDOWED") != 0) flags &= ~0x1u;
    }
    g_want_fullscreen = (flags & 0x1u) ? 1 : 0;   /* DDSCL_FULLSCREEN = 0x1 (19 = 0x13) */
    if (g_win && g_want_fullscreen != g_is_fullscreen) {
        SDL_SetWindowFullscreen(g_win, g_want_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
        g_is_fullscreen = g_want_fullscreen;
        g_map_valid = 0; g_present_force = 1;
    }
    return 0;
}
int host_elf_is_fullscreen(void) { return g_is_fullscreen; }

void host_elf_set_title(const char *title) { copy_title(title); }

int host_elf_window_size(int *w, int *h) {
    if (w) *w = g_w;
    if (h) *h = g_h;
    return (g_w > 0 && g_h > 0) ? 0 : -1;
}

unsigned char *host_elf_canvas_lock(int *w, int *h, int *pitch) {
    if (!g_canvas) return 0;
    ++g_locked;
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (pitch) *pitch = g_pitch;
    return g_canvas;
}
void host_elf_canvas_unlock(void) { if (g_locked > 0) --g_locked; }
int host_elf_canvas_locked(void) { return g_locked; }
unsigned char *host_elf_canvas(int *w, int *h, int *pitch) {
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (pitch) *pitch = g_pitch;
    return g_canvas;                       /* accesso diretto, senza contatore di lock */
}

void host_elf_set_palette(const unsigned int *colors, int first, int n) {
    int i;
    if (!colors) return;
    for (i = 0; i < n && first + i < 256; ++i)
        if (first + i >= 0) g_pal[first + i] = colors[i] & 0x00FFFFFFu;
    g_pal_set = 1;
    g_pal_dirty = 1;
    g_map_valid = 0;
}
int host_elf_get_palette(unsigned int *colors, int first, int n) {
    int i;
    if (!colors) return -1;
    for (i = 0; i < n && first + i < 256; ++i)
        if (first + i >= 0) colors[i] = g_pal[first + i];
    return 0;
}

/* ---- primitive 8 bit su un buffer qualsiasi ---- */
int host_elf_fill8(unsigned char *px, int pitch, int sw, int sh, const int *rect, int idx) {
    int x0, y0, x1, y1, y;
    if (!px || sw <= 0 || sh <= 0) return 0;
    if (!rect) { x0 = 0; y0 = 0; x1 = sw; y1 = sh; }
    else { x0 = rect[0]; y0 = rect[1]; x1 = rect[2]; y1 = rect[3]; }
    /* UN RECT VUOTO NON DISEGNA NULLA (regola D23): nessuna normalizzazione, nessun fallback. */
    if (x1 <= x0 || y1 <= y0) return 0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x1 <= x0 || y1 <= y0) return 0;
    for (y = y0; y < y1; ++y)
        memset(px + (unsigned long) y * pitch + x0, idx & 0xFF, (unsigned long) (x1 - x0));
    return 1;
}

int host_elf_blit8(unsigned char *dpx, int dpitch, int dw, int dh,
                    const unsigned char *spx, int spitch, int sw, int sh,
                    const int *drect, const int *srect, int ckey) {
    int sx, sy, sww, shh, dx, dy, dww, dhh;
    if (!dpx || !spx || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0;
    if (srect) { sx = srect[0]; sy = srect[1]; sww = srect[2] - srect[0]; shh = srect[3] - srect[1]; }
    else { sx = 0; sy = 0; sww = sw; shh = sh; }
    if (sww <= 0 || shh <= 0) return 0;                 /* sorgente vuota: nulla da copiare */
    if (drect) { dx = drect[0]; dy = drect[1]; dww = drect[2] - drect[0]; dhh = drect[3] - drect[1]; }
    else { dx = 0; dy = 0; dww = sww; dhh = shh; }
    if (dww <= 0 || dhh <= 0) return 0;                 /* rect di destinazione vuoto: nulla (D23) */

    if (dww == sww && dhh == shh) {
        /* copia 1:1 con clipping (sequenza provata in LOLA) */
        int w = sww, h = shh, x0 = sx, y0 = sy, x, y;
        if (x0 < 0) { w += x0; dx -= x0; x0 = 0; }
        if (y0 < 0) { h += y0; dy -= y0; y0 = 0; }
        if (x0 + w > sw) w = sw - x0;
        if (y0 + h > sh) h = sh - y0;
        if (dx < 0) { w += dx; x0 -= dx; dx = 0; }
        if (dy < 0) { h += dy; y0 -= dy; dy = 0; }
        if (dx + w > dw) w = dw - dx;
        if (dy + h > dh) h = dh - dy;
        if (w <= 0 || h <= 0) return 0;
        for (y = 0; y < h; ++y) {
            const unsigned char *sp = spx + (unsigned long) (y0 + y) * spitch + x0;
            unsigned char *dp = dpx + (unsigned long) (dy + y) * dpitch + dx;
            if (ckey < 0) {
                memcpy(dp, sp, (unsigned long) w);
            } else {
                for (x = 0; x < w; ++x)
                    if (sp[x] != (unsigned char) (ckey & 0xFF)) dp[x] = sp[x];
            }
        }
        return 1;
    }
    /* dimensioni diverse = stiramento nearest-neighbour (come DirectDraw con lo stretch) */
    {
        int x, y, drew = 0;
        for (y = 0; y < dhh; ++y) {
            int yy = dy + y;
            int yy_src = sy + (int) ((long) y * shh / dhh);
            if (yy < 0 || yy >= dh || yy_src < 0 || yy_src >= sh) continue;
            for (x = 0; x < dww; ++x) {
                int xx = dx + x;
                int xx_src = sx + (int) ((long) x * sww / dww);
                unsigned char v;
                if (xx < 0 || xx >= dw || xx_src < 0 || xx_src >= sw) continue;
                v = spx[(unsigned long) yy_src * spitch + xx_src];
                if (ckey >= 0 && v == (unsigned char) (ckey & 0xFF)) continue;
                dpx[(unsigned long) yy * dpitch + xx] = v;
                drew = 1;
            }
        }
        return drew;
    }
}

/* ---- fullscreen + renderer con logical size 640x480 ---- */
/* ---- renderer con logical size 640x480: letterbox/pillarbox automatico in fullscreen ---- */
typedef struct { int x, y, w, h; } SdlRect;
static int g_fullscreen;
static int g_uitrace_present;

/* Viewport 4:3 piu' grande che sta nella finestra, centrata: barre nere ai lati (pillarbox) o
 * sopra/sotto (letterbox). Scala intera quando possibile (pixel netti), altrimenti frazionaria. */
static void renderer_viewport(SdlRect *vp) {
    int ww = 640, wh = 480, s;
    /* Viewport in coordinate FINESTRA (la surface su cui scriviamo e' quella della finestra). */
    if (g_win) SDL_GetWindowSize(g_win, &ww, &wh);
    if (ww <= 0) ww = 640;
    if (wh <= 0) wh = 480;
    if (ww * 3 >= wh * 4) {            /* finestra piu' larga del 4:3 -> limitata dall'altezza */
        vp->h = wh;
        vp->w = (wh * 4) / 3;
    } else {                           /* piu' stretta -> limitata dalla larghezza */
        vp->w = ww;
        vp->h = (ww * 3) / 4;
    }
    s = vp->w / 640;                   /* scala intera se possibile */
    if (s >= 1) { vp->w = 640 * s; vp->h = 480 * s; }
    vp->x = (ww - vp->w) / 2;
    vp->y = (wh - vp->h) / 2;
}

/* finestra -> canvas 640x480 (letterbox incluso). Sempre dentro 0..639 / 0..479. */
static void win_to_canvas(int wx, int wy, int *cx, int *cy) {
    SdlRect vp;
    int x, y;
    renderer_viewport(&vp);                     /* stessa viewport usata da present_scaled */
    x = (int) (((long) (wx - vp.x) * 640) / (vp.w > 0 ? vp.w : 640));
    y = (int) (((long) (wy - vp.y) * 480) / (vp.h > 0 ? vp.h : 480));
    if (x < 0) x = 0; if (x > 639) x = 639;
    if (y < 0) y = 0; if (y > 479) y = 479;
    *cx = x; *cy = y;
}

/* canvas 640x480 -> finestra (inverso di win_to_canvas), per SDL_WarpMouseInWindow. */
static void canvas_to_win(int cx, int cy, int *wx, int *wy) {
    SdlRect vp;
    renderer_viewport(&vp);
    *wx = vp.x + (int) (((long) cx * vp.w) / 640);
    *wy = vp.y + (int) (((long) cy * vp.h) / 480);
}

int host_elf_fullscreen_toggle(void) {
    if (!g_win) return -1;
    g_fullscreen = !g_fullscreen;
    {   /* SC_FULLSCREEN_MODE=exclusive chiede un vero cambio di modalita' video (spesso bypassa il
         * compositore del tutto); default: FULLSCREEN_DESKTOP (senza mode switch). */
        const char *m = getenv("SC_FULLSCREEN_MODE");
        unsigned int fsflag = 0x00001001u;                     /* FULLSCREEN_DESKTOP */
        if (m && m[0] == 'e') { fsflag = 0x00000001u; }        /* FULLSCREEN (exclusive) */
        if (SDL_SetWindowFullscreen(g_win, g_fullscreen ? fsflag : 0u) != 0) {
        g_fullscreen = !g_fullscreen;
        reasm_elf_log("[host] fullscreen: SDL_SetWindowFullscreen fallita\n");
        return -1;
    }
    }
    g_map_valid = 0;
    { char b[112];
      const char *m = getenv("SC_FULLSCREEN_MODE");
      int excl = m && m[0] == 'e';
      snprintf(b, sizeof b, "[host] fullscreen %s (%s, 4:3 letterbox, bypass compositore richiesto)\n",
               g_fullscreen ? "ON" : "OFF", excl ? "exclusive" : "desktop");
      reasm_elf_log(b); }
    return 0;
}

/* ---- present: canvas (indici) -> finestra (RGB) con la palette ---- */
static void rebuild_map(const SDL_PixelFormat *f) {
    int i;
    for (i = 0; i < 256; ++i) {
        unsigned int c = g_pal[i];
        g_map[i] = SDL_MapRGB(f, (unsigned char) ((c >> 16) & 0xFF),
                              (unsigned char) ((c >> 8) & 0xFF), (unsigned char) (c & 0xFF));
    }
    g_map_fmt = (const void *) f;
    g_map_valid = 1;
}

/* Present via renderer: converte il canvas indicizzato in ARGB usando la palette e lo copia con
 * SDL_RenderSetLogicalSize(640x480). In fullscreen-desktop SDL sceglie la viewport piu' grande che
 * conserva il 4:3 e il resto e' nero (pillarbox): le proporzioni originali restano esatte, senza
 * nessuna matematica nostra e senza deformazioni. */
/* Present con SCALATURA NOSTRA nella surface della finestra.
 * Perche' non il renderer: con la surface il calcolo della viewport e' esplicito e verificabile
 * (dst rect noto, scala nearest-neighbour), mentre sul renderer il risultato dipende da
 * logical-size/viewport interni del backend (osservato: contenuto a 1.5x e traslato in fullscreen).
 * Regola: 4:3 conservato, scala INTERA quando possibile (pixel netti), barre nere riempite. */
/* Tabelle di mappatura pixel: una divisione per colonna/riga, non per pixel.
 * Prima erano due divisioni PER PIXEL: a 1280x960 sono 2,4 M di divisioni per frame, ~10-15 ms
 * sprecati solo in aritmetica (misurato: e' la causa principale della lentezza in fullscreen). */
static int g_xmap_w, g_ymap_h;
static unsigned short g_xmap[4096];      /* x finestra -> x canvas (0..639) */
static unsigned short g_ymap[4096];      /* y finestra -> y canvas (0..479) */

static void build_maps(int w, int h, const SdlRect *vp) {
    int i;
    if (g_xmap_w != w) {
        g_xmap_w = w;
        for (i = 0; i < w && i < (int) (sizeof g_xmap / sizeof g_xmap[0]); ++i) {
            int sx = (int) (((long) (i - vp->x) * 640) / (vp->w > 0 ? vp->w : 640));
            if (sx < 0) sx = 0; if (sx > 639) sx = 639;
            g_xmap[i] = (unsigned short) sx;
        }
    }
    if (g_ymap_h != h) {
        g_ymap_h = h;
        for (i = 0; i < h && i < (int) (sizeof g_ymap / sizeof g_ymap[0]); ++i) {
            int sy = (int) (((long) (i - vp->y) * 480) / (vp->h > 0 ? vp->h : 480));
            if (sy < 0) sy = 0; if (sy > 479) sy = 479;
            g_ymap[i] = (unsigned short) sy;
        }
    }
}

/* Sfondo nero SOLO nelle barre: azzerare tutta la surface (8 MB a 1920x1080) a ogni frame costava
 * quanto il disegno vero. */
static void clear_bars(SDL_Surface *ws, const SdlRect *vp) {
    unsigned char *px = (unsigned char *) ws->pixels;
    unsigned long pitch = (unsigned long) ws->pitch;
    int y;
    if (vp->y > 0) memset(px, 0, pitch * (unsigned long) vp->y);
    if (vp->y + vp->h < ws->h)
        memset(px + pitch * (unsigned long) (vp->y + vp->h), 0, pitch * (unsigned long) (ws->h - vp->y - vp->h));
    if (vp->x > 0) {
        for (y = vp->y; y < vp->y + vp->h && y < ws->h; ++y)
            memset(px + pitch * (unsigned long) y, 0, (unsigned long) vp->x * (unsigned long) ws->format->BytesPerPixel);
    }
    if (vp->x + vp->w < ws->w) {
        int bpp = ws->format->BytesPerPixel;
        for (y = vp->y; y < vp->y + vp->h && y < ws->h; ++y)
            memset(px + pitch * (unsigned long) y + (unsigned long) (vp->x + vp->w) * bpp, 0,
                   (unsigned long) (ws->w - vp->x - vp->w) * bpp);
    }
}

/* GL/GLES entry points are resolved through SDL: no hard libGL/libGLES dependency. */
typedef unsigned int GLu;
typedef int GLi;
typedef unsigned char GLb;
typedef void (*PFNGLGEN)(GLi, GLu *);
typedef void (*PFNGLBIND)(GLu, GLu);
typedef void (*PFNGLPARAMI)(GLu, GLu, GLi);
typedef void (*PFNGLTEXIMAGE2D)(GLu, GLi, GLi, GLi, GLi, GLi, GLu, GLu, const void *);
typedef void (*PFNGLTEXSUBIMAGE2D)(GLu, GLi, GLi, GLi, GLi, GLi, GLu, GLu, const void *);
typedef void (*PFNGLPIXELSTOREI)(GLu, GLi);
typedef GLu (*PFNGLCREATESHADER)(GLu);
typedef void (*PFNGLSHADERSOURCE)(GLu, GLi, const char **, const GLi *);
typedef void (*PFNGLCOMPILESHADER)(GLu);
typedef void (*PFNGLGETSHADERIV)(GLu, GLu, GLi *);
typedef GLu (*PFNGLCREATEPROGRAM)(void);
typedef void (*PFNGLATTACHSHADER)(GLu, GLu);
typedef void (*PFNGLLINKPROGRAM)(GLu);
typedef void (*PFNGLGETPROGRAMIV)(GLu, GLu, GLi *);
typedef void (*PFNGLUSEPROGRAM)(GLu);
typedef GLi (*PFNGLGETATTRIB)(GLu, const char *);
typedef GLi (*PFNGLGETUNIFORM)(GLu, const char *);
typedef void (*PFNGLUNIFORMI)(GLi, GLi);
typedef void (*PFNGLENABLEATTRIB)(GLu);
typedef void (*PFNGLATTRIBPTR)(GLu, GLi, GLu, GLb, GLi, const void *);
typedef void (*PFNGLACTIVETEXTURE)(GLu);
typedef void (*PFNGLVIEWPORT)(GLi, GLi, GLi, GLi);
typedef void (*PFNGLCLEARCOLOR)(float, float, float, float);
typedef void (*PFNGLCLEAR)(GLu);
typedef void (*PFNGLDISABLE)(GLu);
typedef void (*PFNGLREADPIXELS)(GLi, GLi, GLi, GLi, GLu, GLu, void *);
typedef void (*PFNGLDRAWARRAYS)(GLu, GLi, GLi);
typedef const unsigned char *(*PFNGLGETSTRING)(GLu);
typedef void (*PFNGLGETINFOLOG)(GLu, GLi, GLi *, char *);
typedef struct {
    PFNGLGEN gen; PFNGLBIND bind; PFNGLPARAMI parami; PFNGLTEXIMAGE2D teximage;
    PFNGLTEXSUBIMAGE2D texsub; PFNGLPIXELSTOREI pixelstore; PFNGLCREATESHADER creates;
    PFNGLSHADERSOURCE source; PFNGLCOMPILESHADER compile; PFNGLGETSHADERIV shaderiv;
    PFNGLCREATEPROGRAM createprog; PFNGLATTACHSHADER attach; PFNGLLINKPROGRAM link;
    PFNGLGETPROGRAMIV programiv; PFNGLUSEPROGRAM use; PFNGLGETATTRIB attrib;
    PFNGLGETUNIFORM uniform; PFNGLUNIFORMI uniformi; PFNGLENABLEATTRIB enableattrib;
    PFNGLATTRIBPTR attribptr; PFNGLACTIVETEXTURE activetexture; PFNGLVIEWPORT viewport; PFNGLCLEARCOLOR clearcolor;
    PFNGLCLEAR clear; PFNGLDISABLE disable; PFNGLREADPIXELS readpixels;
    PFNGLDRAWARRAYS draw; PFNGLGETSTRING getstring;
    PFNGLGETINFOLOG shaderlog, programlog;
} GlApi;
static GlApi gl;
static GLu gl_idx_tex, gl_pal_tex, gl_program;
static GLi gl_pos_attr, gl_uv_attr, gl_idx_uniform, gl_pal_uniform;
static unsigned char gl_palette[256 * 4];
static void *gl_proc(const char *n) { return SDL_GL_GetProcAddress(n); }
#define GL_LOAD(field, name) do { gl.field = (void *) gl_proc(name); if (!gl.field) { gl_log_missing(name); return -1; } } while (0)

static void gl_log_missing(const char *name);
static int contains_ci(const char *text, const char *needle) {
    int i, j;
    if (!text || !needle || !*needle) return 0;
    for (i = 0; text[i]; ++i) {
        for (j = 0; needle[j] && text[i+j]; ++j) {
            char a = text[i+j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}
static int g_gl_software;
static int software_gl_renderer(const char *renderer) {
    return contains_ci(renderer, "llvmpipe") || contains_ci(renderer, "softpipe") ||
           contains_ci(renderer, "swrast") || contains_ci(renderer, "swiftshader") ||
           contains_ci(renderer, "microsoft basic render driver");
}

static const char *g_gl_profile = "unset";
static void gl_log(const char *s) { reasm_elf_log("[host] GL "); reasm_elf_log(s); }
static void gl_log_missing(const char *name) {
    gl_log("proc mancante: "); reasm_elf_log(name); reasm_elf_log("\n");
}
static void gl_log_info(const char *kind, GLu object, PFNGLGETINFOLOG fn) {
    char info[1024]; GLi n = 0;
    info[0] = 0; fn(object, (GLi)sizeof info - 1, &n, info);
    if (n < 0) n = 0; if (n >= (GLi)sizeof info) n = (GLi)sizeof info - 1;
    info[n] = 0;
    gl_log(kind); gl_log(": "); reasm_elf_log(info[0] ? info : "(info log vuoto)"); reasm_elf_log("\n");
}
static int gl_context_attempt(const char *profile, int major, int minor, int mask,
                              const char *vs, const char *fs) {
    const char *version, *renderer, *vendor;
    GLu shv, shf;
    GLi ok = 0;
    g_gl_profile = profile;
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, mask) != 0) {
        gl_log("SDL_GL_SetAttribute "); reasm_elf_log(profile); reasm_elf_log(" fallita: ");
        reasm_elf_log(SDL_GetError()); reasm_elf_log("\n"); return -1;
    }
    g_gl_context = SDL_GL_CreateContext(g_win);
    if (!g_gl_context) {
        gl_log("SDL_GL_CreateContext "); reasm_elf_log(profile); reasm_elf_log(" fallita: ");
        reasm_elf_log(SDL_GetError()); reasm_elf_log("\n"); return -1;
    }
    SDL_GL_SetSwapInterval(0);
    GL_LOAD(gen,"glGenTextures"); GL_LOAD(bind,"glBindTexture"); GL_LOAD(parami,"glTexParameteri");
    GL_LOAD(teximage,"glTexImage2D"); GL_LOAD(texsub,"glTexSubImage2D"); GL_LOAD(pixelstore,"glPixelStorei");
    GL_LOAD(creates,"glCreateShader"); GL_LOAD(source,"glShaderSource"); GL_LOAD(compile,"glCompileShader");
    GL_LOAD(shaderiv,"glGetShaderiv"); GL_LOAD(shaderlog,"glGetShaderInfoLog");
    GL_LOAD(createprog,"glCreateProgram"); GL_LOAD(attach,"glAttachShader");
    GL_LOAD(link,"glLinkProgram"); GL_LOAD(programiv,"glGetProgramiv"); GL_LOAD(programlog,"glGetProgramInfoLog");
    GL_LOAD(use,"glUseProgram"); GL_LOAD(attrib,"glGetAttribLocation"); GL_LOAD(uniform,"glGetUniformLocation");
    GL_LOAD(uniformi,"glUniform1i"); GL_LOAD(enableattrib,"glEnableVertexAttribArray");
    GL_LOAD(attribptr,"glVertexAttribPointer"); GL_LOAD(activetexture,"glActiveTexture");
    GL_LOAD(viewport,"glViewport"); GL_LOAD(clearcolor,"glClearColor"); GL_LOAD(clear,"glClear");
    GL_LOAD(draw,"glDrawArrays"); GL_LOAD(getstring,"glGetString");
    GL_LOAD(disable,"glDisable"); GL_LOAD(readpixels,"glReadPixels");
    version = (const char *)gl.getstring(0x1F02u);
    renderer = (const char *)gl.getstring(0x1F01u); vendor = (const char *)gl.getstring(0x1F00u);
    if (!version) { gl_log("glGetString(GL_VERSION) ha restituito NULL\n"); return -1; }
    if (software_gl_renderer(renderer) && !g_gl_explicit) {
        g_gl_software = 1; gl_log("renderer software rifiutato in auto-select: ");
        reasm_elf_log(renderer ? renderer : "(null)"); reasm_elf_log(" | vendor: ");
        reasm_elf_log(vendor ? vendor : "(null)"); reasm_elf_log("\n"); return -1;
    }
    shv = gl.creates(0x8B31u); gl.source(shv,1,&vs,0); gl.compile(shv); gl.shaderiv(shv,0x8B81u,&ok);
    if (!ok) { gl_log("vertex shader compile "); reasm_elf_log(profile); reasm_elf_log(" "); gl_log_info("driver info log",shv,gl.shaderlog); return -1; }
    shf = gl.creates(0x8B30u); gl.source(shf,1,&fs,0); gl.compile(shf); gl.shaderiv(shf,0x8B81u,&ok);
    if (!ok) { gl_log("fragment shader compile "); reasm_elf_log(profile); reasm_elf_log(" "); gl_log_info("driver info log",shf,gl.shaderlog); return -1; }
    gl_program=gl.createprog(); gl.attach(gl_program,shv); gl.attach(gl_program,shf); gl.link(gl_program);
    gl.programiv(gl_program,0x8B82u,&ok);
    if (!ok) { gl_log("shader link "); reasm_elf_log(profile); reasm_elf_log(" "); gl_log_info("driver info log",gl_program,gl.programlog); return -1; }
    gl_pos_attr=gl.attrib(gl_program,"a_pos"); gl_uv_attr=gl.attrib(gl_program,"a_uv");
    gl_idx_uniform=gl.uniform(gl_program,"u_idx"); gl_pal_uniform=gl.uniform(gl_program,"u_pal");
    gl.gen(1,&gl_idx_tex); gl.gen(1,&gl_pal_tex);
    gl.bind(0x0DE1u,gl_idx_tex); gl.parami(0x0DE1u,0x2801u,0x2600); gl.parami(0x0DE1u,0x2800u,0x2600);
    gl.parami(0x0DE1u,0x2802u,0x812F); gl.parami(0x0DE1u,0x2803u,0x812F);
    gl.pixelstore(0x0CF5u,1);
    if (major >= 3 || mask == 1) gl.teximage(0x0DE1u,0,0x8229,640,480,0,0x1903,0x1401,0);
    else gl.teximage(0x0DE1u,0,0x1909,640,480,0,0x1909,0x1401,0);
    gl.bind(0x0DE1u,gl_pal_tex); gl.parami(0x0DE1u,0x2801u,0x2600); gl.parami(0x0DE1u,0x2800u,0x2600);
    gl.parami(0x0DE1u,0x2802u,0x812F); gl.parami(0x0DE1u,0x2803u,0x812F);
    gl.teximage(0x0DE1u,0,0x1908,256,1,0,0x1908,0x1401,0);
    gl_ready=1; g_pal_dirty=1;
    gl_log("profil "); reasm_elf_log(profile); reasm_elf_log(" attivo; versione: ");
    reasm_elf_log(version); reasm_elf_log(" | renderer: "); reasm_elf_log(renderer ? renderer : "(null)"); reasm_elf_log("\n");
    return 0;
}
static int gl_init(void) {
    static const char *vs_es="attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv; void main(){gl_Position=vec4(a_pos,0.0,1.0);v_uv=a_uv;}";
    static const char *fs_es="precision mediump float; varying vec2 v_uv; uniform sampler2D u_idx; uniform sampler2D u_pal; void main(){float idx=floor(texture2D(u_idx,v_uv).r*255.0+0.5); gl_FragColor=texture2D(u_pal,vec2((idx+0.5)/256.0,0.5));}";
    static const char *vs_desktop="attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv; void main(){gl_Position=vec4(a_pos,0.0,1.0);v_uv=a_uv;}";
    static const char *fs_desktop="varying vec2 v_uv; uniform sampler2D u_idx; uniform sampler2D u_pal; void main(){float idx=floor(texture2D(u_idx,v_uv).r*255.0+0.5); gl_FragColor=texture2D(u_pal,vec2((idx+0.5)/256.0,0.5));}";
    static const char *vs_core="#version 150\nin vec2 a_pos; in vec2 a_uv; out vec2 v_uv; void main(){gl_Position=vec4(a_pos,0.0,1.0);v_uv=a_uv;}";
    static const char *fs_core="#version 150\nin vec2 v_uv; out vec4 fragColor; uniform sampler2D u_idx; uniform sampler2D u_pal; void main(){float idx=floor(texture(u_idx,v_uv).r*255.0+0.5); fragColor=texture(u_pal,vec2((idx+0.5)/256.0,0.5));}";
    if (gl_ready) return 0;
    memset(&gl,0,sizeof gl); g_gl_software=0;
    if (gl_context_attempt("GLES 2.0",2,0,SDL_GL_CONTEXT_PROFILE_ES,vs_es,fs_es)==0) goto ready;
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context=0; }
    memset(&gl,0,sizeof gl);
    if (gl_context_attempt("desktop GL 2.1",2,1,0,vs_desktop,fs_desktop)==0) goto ready;
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context=0; }
    memset(&gl,0,sizeof gl);
    if (gl_context_attempt("GLES 3.0",3,0,SDL_GL_CONTEXT_PROFILE_ES,vs_es,fs_es)==0) goto ready;
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context=0; }
    memset(&gl,0,sizeof gl);
    if (gl_context_attempt("desktop GL 3.2 core",3,2,1,vs_core,fs_core)==0) goto ready;
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context=0; }
    memset(&gl,0,sizeof gl); g_gl_profile="none";
    gl_log("tutti i profili falliti; abbandono GL\n"); return -1;
ready:
    reasm_elf_log("[host] present: OpenGL/GLES2 palette shader (8bpp upload)\n");
    return 0;
}

static int present_gl(void) {
    SdlRect vp;
    int ww,wh,i;
    static const float quad[] = {-1,-1,0,1, 1,-1,1,1, -1,1,0,0, 1,1,1,0};
    if (!g_win || g_gl_failed || gl_init()!=0) return -1;
    gl.bind(0x0DE1u,gl_idx_tex);
    gl.pixelstore(0x0CF5u,1);
    gl.texsub(0x0DE1u,0,0,0,640,480,0x1909,0x1401,g_canvas);
    gl.use(gl_program);
    gl.activetexture(0x84C0u); gl.bind(0x0DE1u,gl_idx_tex); gl.uniformi(gl_idx_uniform,0);
    gl.activetexture(0x84C1u); gl.bind(0x0DE1u,gl_pal_tex); gl.uniformi(gl_pal_uniform,1);
    if (g_pal_dirty) {
        for (i=0;i<256;++i) { unsigned int c=g_pal[i]; gl_palette[i*4]=(c>>16)&255; gl_palette[i*4+1]=(c>>8)&255; gl_palette[i*4+2]=c&255; gl_palette[i*4+3]=255; }
        gl.texsub(0x0DE1u,0,0,0,256,1,0x1908,0x1401,gl_palette); g_pal_dirty=0;
    }
    SDL_GetWindowSize(g_win,&ww,&wh); renderer_viewport(&vp);
    gl.viewport(0,0,ww,wh); gl.clearcolor(0,0,0,1); gl.clear(0x00004000u); gl.disable(0x0BD0u);
    gl.viewport(vp.x,wh-vp.y-vp.h,vp.w,vp.h);
    gl.enableattrib((GLu)gl_pos_attr); gl.enableattrib((GLu)gl_uv_attr);
    gl.attribptr((GLu)gl_pos_attr,2,0x1406,0,4*sizeof(float),quad);
    gl.attribptr((GLu)gl_uv_attr,2,0x1406,0,4*sizeof(float),quad+2);
    gl.draw(0x0005u,0,4);
    if (getenv("SC_GL_VERIFY") && vp.w == 640 && vp.h == 480) {
        static unsigned char *pixels; static int verified, mismatch_logged;
        unsigned int y,x,errors=0, first_x=0, first_y=0;
        if (!pixels) pixels=(unsigned char *)malloc(640u*480u*4u);
        if (!pixels) return -1;
        gl.pixelstore(0x0D05u,1); gl.readpixels(0,0,640,480,0x1908u,0x1401u,pixels);
        for(y=0;y<480;++y) for(x=0;x<640;++x) {
            unsigned int c=g_pal[g_canvas[(unsigned long)(479-y)*g_pitch+x]], p=(y*640u+x)*4u;
            if(pixels[p]!=(unsigned char)(c>>16) || pixels[p+1]!=(unsigned char)(c>>8) || pixels[p+2]!=(unsigned char)c || pixels[p+3]!=255) { if (!errors) { first_x=x; first_y=y; } ++errors; }
        }
        if(errors) { if (!mismatch_logged) { char b[160]; snprintf(b,sizeof b,"[host] GL SC_GL_VERIFY mismatch: %u pixel, primo (%u,%u)\n",errors,first_x,first_y); reasm_elf_log(b); mismatch_logged=1; } return -1; }
        if(!verified) { reasm_elf_log("[host] SC_GL_VERIFY PASS: 307200 pixel esatti\n"); verified=1; }
    }
    SDL_GL_SwapWindow(g_win);
    if (getenv("SC_FPS_LOG")) { static unsigned int start,frames; unsigned int now=SDL_GetTicks(); ++frames; if(now-start>=1000u){ char b[128]; snprintf(b,sizeof b,"[fps] GL %u fps, viewport %dx%d, finestra %dx%d\n",frames*1000u/(now-start),vp.w,vp.h,ww,wh); reasm_elf_log(b); start=now; frames=0; } }
    return 0;
}

/* Percorso GPU: canvas -> texture ARGB -> SDL_RenderCopy con destinazione = viewport 4:3.
 * Perche' esiste: con la surface, ogni frame costa 1,2 M scritture CPU a 1280x960 PIU' un
 * XShmPutImage da 8 MB verso X11; sotto un compositore (KWin/Mutter via XWayland) quel percorso e'
 * il collo di bottiglia — lo stesso che su Wine si aggirava con gamescope. Con la texture il costo
 * CPU e' solo la conversione 8->32 bit del canvas 640x480 e lo scaling lo fa la GPU.
 * Fallback: se il renderer non nasce (driver assente) si usa present_scaled (CPU). */
static void gl_fallback_window(void) {
    unsigned int flags = SDL_WINDOW_SHOWN;
    int w = g_w > 0 ? g_w : 640, h = g_h > 0 ? g_h : 480;
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context = 0; }
    gl_ready = 0; g_gl_failed = 1; g_gl_requested = 0;
    if (g_win) SDL_DestroyWindow(g_win);
    if (g_want_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_win = SDL_CreateWindow(g_title, (int)SDL_WINDOWPOS_UNDEFINED, (int)SDL_WINDOWPOS_UNDEFINED, w, h, flags);
    if (g_win) { g_window_id = SDL_GetWindowID(g_win); window_pos_from_env(); g_map_valid = 0; }
    reasm_elf_log("[host] GL non disponibile; finestra ricreata, fallback GPU/CPU\n");
}

static int present_gpu(SDL_Surface *ws) {
    SdlRect vp;
    static void *rend, *tex;
    static unsigned int *argb;
    int ww, wh, x, y;
    (void) ws;
    if (!g_win) return -1;
    if (!rend) {
        if (!g_gl_software) rend = SDL_CreateRenderer(g_win, -1, 0x2u /*ACCELERATED*/);
        if (!rend) rend = SDL_CreateRenderer(g_win, -1, 0x1u /*SOFTWARE*/);
        if (!rend) rend = SDL_CreateRenderer(g_win, -1, 0);
        if (!rend) return -1;
        tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
        if (!tex) { SDL_DestroyRenderer(rend); rend = 0; return -1; }
        argb = (unsigned int *) malloc(640u * 480u * 4u);
        if (!argb) { SDL_DestroyTexture(tex); SDL_DestroyRenderer(rend); rend = tex = 0; return -1; }
        reasm_elf_log("[host] present: percorso GPU (renderer + texture)\n");
    }
    for (y = 0; y < 480; ++y) {
        const unsigned char *src = g_canvas + (unsigned long) y * g_pitch;
        unsigned int *dst = argb + (unsigned long) y * 640u;
        for (x = 0; x < 640; ++x) dst[x] = 0xFF000000u | (g_pal[src[x]] & 0x00FFFFFFu);
    }
    if (SDL_UpdateTexture(tex, 0, argb, 640 * 4) != 0) return -1;
    renderer_viewport(&vp);
    SDL_GetWindowSize(g_win, &ww, &wh);
    if (vp.x + vp.w > ww) vp.w = ww - vp.x;
    if (vp.y + vp.h > wh) vp.h = wh - vp.y;
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 255);
    SDL_RenderClear(rend);
    SDL_RenderSetViewport(rend, 0);
    if (SDL_RenderCopy(rend, tex, 0, &vp) != 0) return -1;
    SDL_RenderPresent(rend);
    if (getenv("SC_FPS_LOG")) {
        static unsigned int last_log, frames; unsigned int now = SDL_GetTicks();
        ++frames;
        if (now - last_log >= 1000u) {
            char b[112];
            snprintf(b, sizeof b, "[fps] GPU %u fps, viewport %dx%d, finestra %dx%d\n",
                     frames * 1000u / (now - last_log), vp.w, vp.h, ww, wh);
            reasm_elf_log(b);
            last_log = now; frames = 0;
        }
    }
    return 0;
}

static int present_scaled(SDL_Surface *ws) {
    SdlRect vp;
    int x, y, bpp;
    unsigned int t0 = SDL_GetTicks();
    if (!ws || !ws->pixels || !ws->format) return -1;
    if (!g_map_valid || g_map_fmt != (const void *) ws->format) rebuild_map(ws->format);
    bpp = ws->format->BytesPerPixel;
    renderer_viewport(&vp);
    if (vp.x < 0) vp.x = 0;
    if (vp.y < 0) vp.y = 0;
    if (vp.x + vp.w > ws->w) vp.w = ws->w - vp.x;
    if (vp.y + vp.h > ws->h) vp.h = ws->h - vp.y;
    clear_bars(ws, &vp);
    build_maps(vp.x + vp.w, vp.y + vp.h, &vp);
    for (y = vp.y; y < vp.y + vp.h; ++y) {
        const unsigned char *src = g_canvas + (unsigned long) g_ymap[y] * g_pitch;
        unsigned char *dst = (unsigned char *) ws->pixels + (unsigned long) y * ws->pitch;
        const unsigned short *xm = g_xmap + vp.x;
        if (bpp == 4) {
            unsigned int *q = (unsigned int *) dst;
            for (x = 0; x < vp.w; ++x) q[vp.x + x] = g_map[src[xm[x]]];
        } else if (bpp == 2) {
            unsigned short *q = (unsigned short *) dst;
            for (x = 0; x < vp.w; ++x) q[vp.x + x] = (unsigned short) (g_map[src[xm[x]]] & 0xFFFF);
        } else if (bpp == 3) {
            for (x = 0; x < vp.w; ++x) {
                unsigned int v = g_map[src[xm[x]]];
                unsigned char *qq = dst + (unsigned long) (vp.x + x) * 3;
                qq[0] = (unsigned char) (v & 0xFF);
                qq[1] = (unsigned char) ((v >> 8) & 0xFF);
                qq[2] = (unsigned char) ((v >> 16) & 0xFF);
            }
        } else {
            for (x = 0; x < vp.w; ++x) dst[vp.x + x] = (unsigned char) (g_map[src[xm[x]]] & 0xFF);
        }
    }
    if (getenv("SC_FPS_LOG")) {
        static unsigned int last_log, frames, acc_ms; unsigned int now = SDL_GetTicks();
        ++frames; acc_ms += (now - t0);
        if (now - last_log >= 1000u) {
            char b[128];
            snprintf(b, sizeof b, "[fps] %u fps, present medio %.2f ms, viewport %dx%d, finestra %dx%d\n",
                     frames * 1000u / (now - last_log),
                     (double) acc_ms / (double) (frames ? frames : 1), vp.w, vp.h, ws->w, ws->h);
            reasm_elf_log(b);
            last_log = now; frames = 0; acc_ms = 0;
        }
    }
    return 0;
}

static unsigned int present_fps_cap(void) {
    const char *s = getenv("SC_FPS_CAP");
    unsigned int n = 0;
    if (!s || !*s) return 0;
    while (*s >= '0' && *s <= '9') {
        unsigned int d = (unsigned int)(*s++ - '0');
        if (n > 100000u) return 1000u;
        n = n * 10u + d;
    }
    return *s ? 0 : (n > 1000u ? 1000u : n);
}
static int bytes_equal(const unsigned char *a, const unsigned char *b, unsigned long n) {
    return memcmp(a, b, n) == 0;
}
static int present_is_duplicate(unsigned int now) {
    const char *dedup = getenv("SC_DEDUP_PRESENT");
    unsigned long size = (unsigned long)g_pitch * (unsigned long)g_h;
    if ((dedup && dedup[0] == '0') || getenv("SC_GL_VERIFY") || g_present_force ||
        g_pal_dirty || !g_present_snapshot_valid || now - g_presented_at >= 250u ||
        size != g_present_snapshot_size) return 0;
    if (!g_present_snapshot) {
        g_present_snapshot = (unsigned char *)malloc(size);
        if (!g_present_snapshot) return 0;
        g_present_snapshot_valid = 0;
        return 0;
    }
    return now - g_presented_at < 16u &&
           bytes_equal(g_canvas, g_present_snapshot, size) &&
           bytes_equal((const unsigned char *)g_pal,
                       (const unsigned char *)g_present_palette, sizeof g_pal);
}
static void present_snapshot_commit(unsigned int now) {
    unsigned long size = (unsigned long)g_pitch * (unsigned long)g_h;
    if (!g_present_snapshot && size == g_present_snapshot_size)
        g_present_snapshot = (unsigned char *)malloc(size);
    if (g_present_snapshot && size == g_present_snapshot_size) {
        memcpy(g_present_snapshot, g_canvas, size);
        memcpy(g_present_palette, g_pal, sizeof g_pal);
        g_present_snapshot_valid = 1;
    }
    g_presented_at = now;
    g_pal_dirty = 0;
    g_present_force = 0;
    sc_ctl_frame_presented(g_pal_set);
}

void host_elf_present(void) {
    SDL_Surface *ws;
    unsigned int now, cap;
    if (!g_win || !g_canvas) return;
    /* Il server non legge mai il canvas: la richiesta viene copiata qui, nel thread proprietario. */
    sc_ctl_present(g_canvas, g_w, g_h, g_pitch, g_pal);
    /* Input/pump resta sempre non-throttled anche quando si deduplica o si limita il present. */
    if (host_elf_input_owner() && win32_input_pump) win32_input_pump();
    now = SDL_GetTicks();
    if (present_is_duplicate(now)) return;
    cap = present_fps_cap();
    if (cap && g_present_snapshot_valid && now - g_presented_at < 1000u / cap) {
        /* Pacing reale: il gioco chiama present in un loop stretto (spins) finche' il frame non e' dovuto.
         * Dormire il tempo residuo invece di bruciare CPU replica il blocco del Blt con vsync originale;
         * l'input continua a essere consegnato durante l'attesa (sleep a fette di 2 ms).
         * SC_FPS_SLEEP=0 disabilita il pacing (comportamento busy-wait precedente). */
        static int sleep_en;
        static int sleep_init;
        if (!sleep_init) { const char *sv = getenv("SC_FPS_SLEEP"); sleep_en = !(sv && sv[0] == '0'); sleep_init = 1; }
        if (sleep_en) {
            static unsigned int max_sleep;
            static int max_init;
            unsigned int budget = 1000u / cap, wait = budget - (now - g_presented_at);
            if (!max_init) {
                const char *mv = getenv("SC_FPS_SLEEP_MAX");
                long lv = mv && *mv ? atol(mv) : 4;
                if (lv < 0) lv = 0;
                if (lv > 20) lv = 20;
                max_sleep = (unsigned int) lv; max_init = 1;
            }
            if (wait > max_sleep) wait = max_sleep;
            if (wait) SDL_Delay(wait);
            now = SDL_GetTicks();
            if (now - g_presented_at < 1000u / cap) return;
        } else {
            return;
        }
    }
    {   /* gl -> gpu -> cpu; gpu/cpu permettono A/B esplicito. */
        const char *mode = getenv("SC_PRESENT");
        static int gpu_ok = 1;
        int want_gl = g_gl_requested && !g_gl_failed && !(mode && mode[0] == 'g' && mode[1] == 'p') && !(mode && mode[0] == 'c');
        int want_gpu = !(mode && mode[0] == 'c');
        if (want_gl) {
            if (present_gl() == 0) { present_snapshot_commit(SDL_GetTicks()); return; }
            gl_fallback_window();
        }
        if (want_gpu && gpu_ok) {
            if (present_gpu(0) == 0) { present_snapshot_commit(SDL_GetTicks()); return; }
            gpu_ok = 0;
            reasm_elf_log("[host] present: GPU non disponibile, uso la scalatura CPU\n");
        }
    }
    ws = (SDL_Surface *) SDL_GetWindowSurface(g_win);
    if (!ws || !ws->pixels || !ws->format) return;
    if (present_scaled(ws) != 0) return;
    if (SDL_UpdateWindowSurface(g_win) != 0) log_drv("present: SDL_UpdateWindowSurface");
    present_snapshot_commit(SDL_GetTicks());
}

int host_elf_read_pixel(int x, int y, unsigned char rgb[3]) {
    SDL_Surface *ws;
    const SDL_PixelFormat *f;
    unsigned int v;
    if (!rgb) return -1;
    ws = g_win ? (SDL_Surface *) SDL_GetWindowSurface(g_win) : 0;
    if (!ws || !ws->pixels || !ws->format) return -1;
    if (x < 0 || y < 0 || x >= ws->w || y >= ws->h) return -1;
    f = ws->format;
    {
        const unsigned char *p = (const unsigned char *) ws->pixels + (unsigned long) y * ws->pitch;
        v = 0;
        switch (f->BytesPerPixel) {
        case 4: v = *(const unsigned int *) (p + x * 4); break;
        case 3: v = (unsigned int) p[x*3] | ((unsigned int) p[x*3+1] << 8) | ((unsigned int) p[x*3+2] << 16); break;
        case 2: v = *(const unsigned short *) (p + x * 2); break;
        default: v = p[x]; break;
        }
    }
    rgb[0] = (unsigned char) (((v & f->Rmask) >> f->Rshift) << f->Rloss);
    rgb[1] = (unsigned char) (((v & f->Gmask) >> f->Gshift) << f->Gloss);
    rgb[2] = (unsigned char) (((v & f->Bmask) >> f->Bshift) << f->Bloss);
    return 0;
}

/* ---- tempo ---- */
unsigned int host_elf_ticks(void) { return SDL_GetTicks(); }
void host_elf_delay(unsigned int ms) { SDL_Delay(ms); }

/* ---- eventi: mouse, tastiera, quit ---- */
int host_elf_test_event(const struct host_input_event *in) {
    SDL_EventBuf e; unsigned char *p=(unsigned char *)&e; unsigned int t;
    if(!g_win || !in) return -1;
    memset(&e,0,sizeof e);
    switch(in->type) {
    case HOST_INPUT_MOVE: t=SDL_MOUSEMOTION; break;
    case HOST_INPUT_DOWN: t=SDL_MOUSEBUTTONDOWN; break;
    case HOST_INPUT_UP: t=SDL_MOUSEBUTTONUP; break;
    case HOST_INPUT_KEYDOWN: t=SDL_KEYDOWN; break;
    case HOST_INPUT_KEYUP: t=SDL_KEYUP; break;
    /* sdl2-compat 2.32.70 returns NULL for synthetic TEXTINPUT and its
     * PushEvent wrapper dereferences NULL. Real SDL text events remain supported. */
    case HOST_INPUT_TEXT: return -1;
    case HOST_INPUT_FOCUS: case HOST_INPUT_EXPOSE: t=SDL_WINDOWEVENT; break;
    case HOST_INPUT_QUIT: t=SDL_QUIT; break;
    default: return -1;
    }
    e.type=t; *(unsigned int *)(p+8)=g_window_id;
    if(t==SDL_MOUSEMOTION) *(int *)(p+16)=in->buttons;
    if(t==SDL_MOUSEBUTTONDOWN || t==SDL_MOUSEBUTTONUP) {
        p[16]=(unsigned char)in->key; p[17]=(t==SDL_MOUSEBUTTONDOWN);
    }
    if(t>=SDL_MOUSEMOTION && t<=SDL_MOUSEBUTTONUP) {
        *(int *)(p+20)=in->x; *(int *)(p+24)=in->y;
    }
    if(t==SDL_KEYDOWN || t==SDL_KEYUP) {
        p[12]=(t==SDL_KEYDOWN); p[13]=(unsigned char)in->buttons; /* repeat */
        *(int *)(p+20)=in->key; *(unsigned short *)(p+24)=(unsigned short)in->modifiers;
    }
    if(t==SDL_WINDOWEVENT) p[12]=in->type==HOST_INPUT_EXPOSE?3:(in->key?12:13);
    return SDL_PushEvent(&e);
}
/* Test-only structured TEXT input, avoiding the installed sdl2-compat's
 * synthetic TEXTINPUT crash. Same FIFO/capacity/consumer as real SDL text. */
int host_elf_test_text(const char *utf8) {
    struct host_input_event e; int i;
    if(!host_elf_input_owner() || !utf8) return -1;
    if(input_full()) return 0;
    memset(&e,0,sizeof e); e.type=HOST_INPUT_TEXT; e.modifiers=(int)g_polled_mods;
    e.x=(int)g_mouse_x; e.y=(int)g_mouse_y; e.buttons=g_polled_buttons;
    for(i=0;i<31 && utf8[i];++i) e.text[i]=utf8[i];
    if(utf8[i]) return -1; /* fail, never split/truncate a committed UTF-8 string */
    input_put(&e); return 1;
}
/* replay: I/O minimale (il runtime e' freestanding: niente header di sistema) */
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern int close(int);
extern long lseek(int, long, int);
extern int sscanf(const char *, const char *, ...);
extern long atol(const char *);
extern int atoi(const char *);
extern int strncmp(const char *, const char *, unsigned long);
#define O_RDONLY 0
#define SEEK_SET 0

/* Replay di una sessione REALE registrata (SC_INPUT_REPLAY=<file>).
 * Formato, una riga per evento: `t_ms tipo x y [bottone]`, tipo in {move,down,up,text}, `#` commento.
 * Perche' esiste: riprodurre le azioni *dell'utente* (non una sceneggiatura inventata) e' l'unico modo
 * per dimostrare un difetto che dipende da dove si trova il cursore quando succede qualcosa.
 * Gli eventi sono emessi rispettando i tempi, dal thread che possiede l'input (il pump). */
typedef struct { unsigned int t, type, x, y, btn; } ReplayEv;
static ReplayEv g_rp[20000];
static int g_rp_n = -1, g_rp_i = 0;
static unsigned int g_rp_after = 0;
static void replay_load(void) {
    const char *path = getenv("SC_INPUT_REPLAY");
    char line[256];
    int fd, n = 0, i = 0, r;
    g_rp_n = 0;
    if (!path || !*path) { g_rp_n = -1; return; }
    fd = open(path, O_RDONLY);
    if (fd < 0) { reasm_elf_log("[replay] file non apribile\n"); g_rp_n = -1; return; }
    /* lettura riga per riga: il file puo' contenere migliaia di eventi (una sessione reale). */
    for (;;) {
        r = read(fd, &line[i], 1);
        if (r <= 0 || line[i] == '\n') {
            line[i] = 0;
            if (i > 0 && line[0] != '#' && n < (int) (sizeof g_rp / sizeof g_rp[0])) {
                ReplayEv ev; char kind[16];
                ev.x = ev.y = ev.btn = 0; kind[0] = 0;
                if (sscanf(line, "%u %15s %u %u %u", &ev.t, kind, &ev.x, &ev.y, &ev.btn) >= 2) {
                    if (!strcmp(kind, "move")) ev.type = HOST_INPUT_MOVE;
                    else if (!strcmp(kind, "down")) ev.type = HOST_INPUT_DOWN;
                    else if (!strcmp(kind, "up")) ev.type = HOST_INPUT_UP;
                    else ev.type = 0;
                    if (ev.type) g_rp[n++] = ev;
                }
            }
            i = 0;
            if (r <= 0) break;
            continue;
        }
        if (i < (int) sizeof line - 1) ++i;
    }
    close(fd);
    g_rp_n = n; g_rp_i = 0;
    { char b[96]; snprintf(b, sizeof b, "[replay] %d eventi caricati\n", g_rp_n); reasm_elf_log(b); }
}

static void replay_tick(unsigned int elapsed) {
    struct host_input_event e;
    while (g_rp_i < g_rp_n && elapsed >= g_rp[g_rp_i].t) {
        ReplayEv *ev = &g_rp[g_rp_i++];
        memset(&e, 0, sizeof e);
        e.type = ev->type;
        e.x = (int) ev->x; e.y = (int) ev->y;
        e.key = ev->btn ? (int) ev->btn : 1;
        if (ev->type == HOST_INPUT_DOWN || ev->type == HOST_INPUT_UP) { e.key = (int) (ev->btn ? ev->btn : 1); }
        host_elf_test_event(&e);
        if (getenv("REASM_TRACE")) {
            char b[128];
            snprintf(b, sizeof b, "[replay] t=%u type=%u xy=%u,%u btn=%u\n", ev->t, ev->type, ev->x, ev->y, ev->btn);
            reasm_elf_log(b);
        }
    }
}

/* Opt-in reproducible SDL injection; no game callbacks or forced UI actions. */
static void input_test_tick(void) {
    const char *script=getenv("SC_INPUT_SCRIPT");
    struct host_input_event e; unsigned int elapsed;
    /* "mp": verifica enum PROVIDER Storm #105 (non spiLockDeviceList).
     * "mp-select": seleziona la prima connessione e OK, poi si ferma alla schermata successiva.
     * "mp-create": prosegue con il profilo di prova p16test e apre Create Game LAN. */
    if (script && (!strcmp(script, "mp") || !strcmp(script, "mp-select") || !strcmp(script, "mp-create"))) {
        int advance = strcmp(script, "mp") != 0;
        int create = !strcmp(script, "mp-create");
        struct host_input_event m; unsigned int el = SDL_GetTicks() - g_test_start;
        memset(&m, 0, sizeof m);
        if (g_test_stage == 0 && el >= 2000) {
            m.type = HOST_INPUT_FOCUS; m.key = 1; host_elf_test_event(&m);
            m.type = HOST_INPUT_MOVE; m.x = 140; m.y = 200; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp move 140,200\n"); ++g_test_stage;
        } else if (g_test_stage == 1 && el >= 3000) {
            m.type = HOST_INPUT_MOVE; m.x = 140; m.y = 200; host_elf_test_event(&m);
            m.type = HOST_INPUT_DOWN; m.key = 1; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp Multiplayer DOWN 140,200\n"); ++g_test_stage;
        } else if (g_test_stage == 2 && el >= 3150) {
            m.type = HOST_INPUT_UP; m.key = 1; m.x = 140; m.y = 200; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp Multiplayer UP 140,200\n"); ++g_test_stage;
        } else if (g_test_stage == 3 && el >= 5000) {
            /* dialogo "Select Game Type": scelta "StarCraft Original" */
            m.type = HOST_INPUT_MOVE; m.x = 250; m.y = 210; host_elf_test_event(&m);
            m.type = HOST_INPUT_DOWN; m.key = 1; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp Original DOWN 250,210\n"); ++g_test_stage;
        } else if (g_test_stage == 4 && el >= 5150) {
            m.type = HOST_INPUT_UP; m.key = 1; m.x = 250; m.y = 210; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp Original UP 250,210\n"); ++g_test_stage;
        } else if (g_test_stage == 5 && el >= 9000) {
            m.type = HOST_INPUT_MOVE; m.x = 600; m.y = 450; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp park 600,450\n"); ++g_test_stage;
        } else if (advance && g_test_stage == 6 && el >= 12000) {
            m.type = HOST_INPUT_MOVE; m.x = 150; m.y = 102; host_elf_test_event(&m);
            m.type = HOST_INPUT_DOWN; m.key = 1; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp UDP DOWN 150,102\n"); ++g_test_stage;
        } else if (advance && g_test_stage == 7 && el >= 12150) {
            m.type = HOST_INPUT_UP; m.key = 1; m.x = 150; m.y = 102; host_elf_test_event(&m);
            ++g_test_stage;
        } else if (advance && g_test_stage == 8 && el >= 14000) {
            m.type = HOST_INPUT_MOVE; m.x = 518; m.y = 391; host_elf_test_event(&m);
            m.type = HOST_INPUT_DOWN; m.key = 1; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp connection OK DOWN 518,391\n"); ++g_test_stage;
        } else if (advance && g_test_stage == 9 && el >= 14150) {
            m.type = HOST_INPUT_UP; m.key = 1; m.x = 518; m.y = 391; host_elf_test_event(&m);
            ++g_test_stage;
        } else if (advance && g_test_stage == 10 && el >= 16000) {
            m.type = HOST_INPUT_MOVE; m.x = 320; m.y = 240; host_elf_test_event(&m);
            ++g_test_stage;
        } else if (create && g_test_stage == 11 && el >= 18000) {
            host_elf_test_text("p16test");
            reasm_elf_log("[input-test] mp profile p16test queued\n"); ++g_test_stage;
        } else if (create && g_test_stage == 12 && el >= 20000) {
            m.type = HOST_INPUT_KEYDOWN; m.key = 13; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp new profile Enter DOWN\n"); ++g_test_stage;
        } else if (create && g_test_stage == 13 && el >= 20150) {
            m.type = HOST_INPUT_KEYUP; m.key = 13; host_elf_test_event(&m); ++g_test_stage;
        } else if (create && g_test_stage == 14 && el >= 23000) {
            m.type = HOST_INPUT_KEYDOWN; m.key = 13; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp registry Enter DOWN\n"); ++g_test_stage;
        } else if (create && g_test_stage == 15 && el >= 23150) {
            m.type = HOST_INPUT_KEYUP; m.key = 13; host_elf_test_event(&m); ++g_test_stage;
        } else if (create && g_test_stage == 16 && el >= 26000) {
            m.type = HOST_INPUT_MOVE; m.x = 320; m.y = 240; host_elf_test_event(&m);
            ++g_test_stage;
        } else if (create && g_test_stage == 17 && el >= 30000) {
            m.type = HOST_INPUT_MOVE; m.x = 125; m.y = 342; host_elf_test_event(&m);
            m.type = HOST_INPUT_DOWN; m.key = 1; host_elf_test_event(&m);
            reasm_elf_log("[input-test] mp Create Game DOWN 125,342\n"); ++g_test_stage;
        } else if (create && g_test_stage == 18 && el >= 30150) {
            m.type = HOST_INPUT_UP; m.key = 1; m.x = 125; m.y = 342; host_elf_test_event(&m);
            ++g_test_stage;
        } else if (create && g_test_stage == 19 && el >= 32000) {
            m.type = HOST_INPUT_MOVE; m.x = 320; m.y = 240; host_elf_test_event(&m);
            ++g_test_stage;
        }
        return;
    }

    if (g_rp_n < 0) { replay_load(); { const char *a = getenv("SC_REPLAY_AFTER_MS"); g_rp_after = a && *a ? (unsigned int) atoi(a) : 0; } }
    elapsed=SDL_GetTicks()-g_test_start;
    /* Il replay puo' partire DOPO un prefisso eseguito dallo scenario (SC_INPUT_SCRIPT): serve a
     * riprodurre la CODA di una sessione reale (es. il clic su OK nel dialogo e i movimenti
     * successivi) dentro un percorso deterministico che arriva alla stessa schermata. */
    if (g_rp_n > 0 && elapsed >= g_rp_after) { replay_tick(elapsed - g_rp_after); return; }
    {   /* Live control file (SC_CONTROL_FILE=<path>): incremental command queue for isolated agents.
         * Commands (one per line): focus | move X Y | down X Y [btn] | up X Y [btn] |
         * click X Y [btn] | keydown K | keyup K | key K | text STR */
        const char *cpath = getenv("SC_CONTROL_FILE");
        static long ctrl_off = 0;       /* ultimo byte consumato in una riga completa */
        static long ctrl_read_off = 0;  /* ultimo byte prelevato dal kernel */
        static int ctrl_fd = -1;
        static int ctrl_pending = 0; /* 1=mouse up, 2=key up */
        static unsigned int ctrl_due_ms = 0;
        static unsigned int ctrl_retry_ms = 0;
        static unsigned int ctrl_identity_check_ms = 0;
        static int ctrl_retry_armed = 0;
        static int ctrl_identity_check_armed = 0;
        static int ctrl_px = 0, ctrl_py = 0, ctrl_pk = 1;
        static struct stat ctrl_identity;
        static char ctrl_buf[512];
        static unsigned int ctrl_buf_pos = 0, ctrl_buf_len = 0;
        static char cline[256];
        static int ci = 0;
        unsigned int now = SDL_GetTicks();
        if (ctrl_pending && now >= ctrl_due_ms) {
            struct host_input_event ce; memset(&ce, 0, sizeof ce);
            if (ctrl_pending == 1) {
                ce.type = HOST_INPUT_UP; ce.x = ctrl_px; ce.y = ctrl_py; ce.key = ctrl_pk;
            } else {
                ce.type = HOST_INPUT_KEYUP; ce.key = ctrl_pk;
            }
            host_elf_test_event(&ce);
            ctrl_pending = 0;
        }
        if (cpath && *cpath && !ctrl_pending) {
            struct stat path_st, fd_st;
            long ctrl_file_size = -1;
            int reset = 0;
            if (ctrl_fd >= 0) {
                int path_changed = 0;
                if (!ctrl_identity_check_armed || (unsigned int)(now - ctrl_identity_check_ms) < 0x80000000u) {
                    path_changed = stat(cpath, &path_st) != 0 ||
                                   path_st.st_dev != ctrl_identity.st_dev || path_st.st_ino != ctrl_identity.st_ino;
                    ctrl_identity_check_ms = now + 250u;
                    ctrl_identity_check_armed = 1;
                }
                if (path_changed || fstat(ctrl_fd, &fd_st) != 0) {
                    close(ctrl_fd); ctrl_fd = -1; reset = 1;
                } else {
                    ctrl_file_size = (long)fd_st.st_size;
                    if (fd_st.st_size < ctrl_off || fd_st.st_size < ctrl_read_off) {
                        /* Troncamento: scarta anche eventuali byte prefetched e la riga parziale. */
                        reset = 1;
                        if (lseek(ctrl_fd, 0, SEEK_SET) != 0) {
                            close(ctrl_fd); ctrl_fd = -1;
                        }
                    }
                }
            } else if (!ctrl_retry_armed || (unsigned int)(now - ctrl_retry_ms) < 0x80000000u) {
                if (stat(cpath, &path_st) == 0) ctrl_fd = open(cpath, O_RDONLY);
                if (ctrl_fd >= 0 && fstat(ctrl_fd, &fd_st) == 0) {
                    ctrl_identity = fd_st;
                    ctrl_file_size = (long)fd_st.st_size;
                    ctrl_identity_check_ms = now + 250u;
                    ctrl_identity_check_armed = 1;
                    ctrl_retry_armed = 0;
                    reset = 1;
                } else {
                    if (ctrl_fd >= 0) close(ctrl_fd);
                    ctrl_fd = -1;
                    ctrl_retry_ms = now + 250u;
                    ctrl_retry_armed = 1;
                }
            }
            if (reset) {
                ctrl_off = ctrl_read_off = 0;
                ctrl_buf_pos = ctrl_buf_len = 0;
                ci = 0;
            }
            if (ctrl_fd >= 0) {
                while (!ctrl_pending) {
                    if (ctrl_buf_pos >= ctrl_buf_len) {
                        long r;
                        if (ctrl_file_size <= ctrl_read_off) break;
                        r = read(ctrl_fd, ctrl_buf, sizeof ctrl_buf);
                        if (r <= 0) break; /* EOF/no byte nuovo: nessun polling interno */
                        ctrl_buf_pos = 0;
                        ctrl_buf_len = (unsigned int)r;
                        ctrl_read_off += r;
                    }
                    {
                        char ch = ctrl_buf[ctrl_buf_pos++];
                        if (ch != '\n') {
                            if (ci < (int)sizeof cline - 1) cline[ci++] = ch;
                            /* il parser storico conservava i primi 255 byte */
                            continue;
                        }
                        cline[ci] = 0;
                        ctrl_off = ctrl_read_off - (long)ctrl_buf_len + (long)ctrl_buf_pos;
                        ci = 0;
                        if (!cline[0] || cline[0] == '#') continue;
                        {
                            struct host_input_event ce; int a = 0, b = 0, c = 1;
                            memset(&ce, 0, sizeof ce);
                            if (!strcmp(cline, "focus")) {
                                ce.type = HOST_INPUT_FOCUS; ce.key = 1; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "move %d %d", &a, &b) == 2) {
                                ce.type = HOST_INPUT_MOVE; ce.x = a; ce.y = b; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "down %d %d %d", &a, &b, &c) >= 2) {
                                ce.type = HOST_INPUT_MOVE; ce.x = a; ce.y = b; host_elf_test_event(&ce);
                                memset(&ce, 0, sizeof ce);
                                ce.type = HOST_INPUT_DOWN; ce.x = a; ce.y = b; ce.key = c ? c : 1; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "up %d %d %d", &a, &b, &c) >= 2) {
                                ce.type = HOST_INPUT_MOVE; ce.x = a; ce.y = b; host_elf_test_event(&ce);
                                memset(&ce, 0, sizeof ce);
                                ce.type = HOST_INPUT_UP; ce.x = a; ce.y = b; ce.key = c ? c : 1; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "click %d %d %d", &a, &b, &c) >= 2) {
                                ce.type = HOST_INPUT_MOVE; ce.x = a; ce.y = b; host_elf_test_event(&ce);
                                memset(&ce, 0, sizeof ce);
                                ce.type = HOST_INPUT_DOWN; ce.x = a; ce.y = b; ce.key = c ? c : 1; host_elf_test_event(&ce);
                                ctrl_pending = 1; ctrl_px = a; ctrl_py = b; ctrl_pk = c ? c : 1; ctrl_due_ms = SDL_GetTicks() + 120;
                            } else if (sscanf(cline, "keydown %d", &a) == 1) {
                                ce.type = HOST_INPUT_KEYDOWN; ce.key = a; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "keyup %d", &a) == 1) {
                                ce.type = HOST_INPUT_KEYUP; ce.key = a; host_elf_test_event(&ce);
                            } else if (sscanf(cline, "key %d", &a) == 1) {
                                ce.type = HOST_INPUT_KEYDOWN; ce.key = a; host_elf_test_event(&ce);
                                ctrl_pending = 2; ctrl_pk = a; ctrl_due_ms = SDL_GetTicks() + 100;
                            } else if (!strncmp(cline, "text ", 5)) {
                                host_elf_test_text(cline + 5);
                            }
                        }
                    }
                }
            }
        }
    }
    if(!script || !*script) return;
    memset(&e,0,sizeof e);
    if(g_test_stage==0 && elapsed>=2000) {
        e.type=HOST_INPUT_FOCUS; e.key=1; host_elf_test_event(&e);
        e.type=HOST_INPUT_MOVE; e.x=100; e.y=100; host_elf_test_event(&e);
        reasm_elf_log("[input-test] move 100,100\n"); ++g_test_stage;
    } else if(g_test_stage==1 && elapsed>=4000) {
        e.type=HOST_INPUT_MOVE; e.x=320; e.y=190; host_elf_test_event(&e);
        e.type=HOST_INPUT_KEYDOWN; e.key='a'; host_elf_test_event(&e);
        reasm_elf_log("[input-test] move 320,190; hold a\n"); ++g_test_stage;
    } else if(g_test_stage==2 && elapsed>=5500) {
        reasm_elf_log(host_elf_key_state('a')==0x80?
          "[input-test] held a state=0x80 PASS\n":"[input-test] held a FAIL\n");
        e.type=HOST_INPUT_KEYUP; e.key='a'; host_elf_test_event(&e); ++g_test_stage;
    } else if(g_test_stage==3 && elapsed>=8000 && script[0]=='c') {
        e.type=HOST_INPUT_MOVE; e.x=220; e.y=120; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Single Player DOWN 220,120\n"); ++g_test_stage;
    } else if(g_test_stage==4 && elapsed>=8150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=220; e.y=120; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Single Player UP 220,120\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==5 && elapsed>=11000) {
        e.type=HOST_INPUT_MOVE; e.x=258; e.y=206; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Original game type DOWN 258,206\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==6 && elapsed>=11150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=258; e.y=206; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Original game type UP 258,206\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==7 && elapsed>=21000) {
        e.type=HOST_INPUT_MOVE; e.x=150; e.y=102; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Select Connection IPX DOWN 150,102\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==8 && elapsed>=21150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=150; e.y=102; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Select Connection IPX UP 150,102\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==9 && elapsed>=23000) {
        e.type=HOST_INPUT_MOVE; e.x=518; e.y=391; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Select Connection OK DOWN 518,391\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==10 && elapsed>=23150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=518; e.y=391; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Select Connection OK UP 518,391\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==11 && elapsed>=26000) {
        e.type=HOST_INPUT_MOVE; e.x=298; e.y=227; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] character name field DOWN 298,227\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==12 && elapsed>=26150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=298; e.y=227; host_elf_test_event(&e);
        reasm_elf_log("[input-test] character name field UP 298,227\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==13 && elapsed>=27000) {
        int r=host_elf_test_text("p8test");
        reasm_elf_log(r==1?"[input-test] campaign profile text queued\n":"[input-test] campaign profile text FAILED\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==14 && elapsed>=30000) {
        e.type=HOST_INPUT_MOVE; e.x=235; e.y=313; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] new profile OK DOWN 235,313\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==15 && elapsed>=30150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=235; e.y=313; host_elf_test_event(&e);
        reasm_elf_log("[input-test] new profile OK UP 235,313\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==16 && elapsed>=34000) {
        e.type=HOST_INPUT_MOVE; e.x=518; e.y=391; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] registry OK DOWN 518,391\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==17 && elapsed>=34150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=518; e.y=391; host_elf_test_event(&e);
        reasm_elf_log("[input-test] registry OK UP 518,391\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==18 && elapsed>=40000) {
        e.type=HOST_INPUT_MOVE; e.x=313; e.y=256; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Terran Episode I DOWN (root 505,400)\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==19 && elapsed>=40150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=313; e.y=256; host_elf_test_event(&e);
        reasm_elf_log("[input-test] Terran Episode I UP (root 505,400)\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==20 && elapsed>=46000) {
        /* Briefing della missione: bottone Start in basso a destra (640x480). */
        e.type=HOST_INPUT_MOVE; e.x=520; e.y=400; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] briefing Start DOWN 520,400\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==21 && elapsed>=46150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=520; e.y=400; host_elf_test_event(&e);
        reasm_elf_log("[input-test] briefing Start UP 520,400\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==22 && elapsed>=52000) {
        /* Parcheggia il cursore al CENTRO della finestra e non toccarlo piu': serve a distinguere
         * lo scrolling "da bordo" (legittimo) da un difetto di coordinate (scroll continuo). */
        e.type=HOST_INPUT_MOVE; e.x=320; e.y=240; host_elf_test_event(&e);
        reasm_elf_log("[input-test] map park 320,240\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==24 && elapsed>=90000) {
        /* Se dopo Start compare la scelta difficolta'/mappa, conferma col pulsante di destra. */
        e.type=HOST_INPUT_MOVE; e.x=520; e.y=400; host_elf_test_event(&e);
        e.type=HOST_INPUT_DOWN; e.key=1; host_elf_test_event(&e);
        reasm_elf_log("[input-test] post-Start OK DOWN 520,400\n"); ++g_test_stage;
    } else if(!strcmp(script,"campaign") && g_test_stage==25 && elapsed>=90150) {
        e.type=HOST_INPUT_UP; e.key=1; e.x=520; e.y=400; host_elf_test_event(&e);
        reasm_elf_log("[input-test] post-Start OK UP 520,400\n"); ++g_test_stage;
    }
}
/* Polling dello stato GLOBALE del mouse.
 * Perche': gli eventi SDL arrivano solo mentre il puntatore e' sopra la finestra (o in capture).
 * Se il puntatore esce — e senza un vero grab puo' uscire — g_mouse_x/y resta fermo sull'ultima
 * posizione, che tipicamente e' il bordo da cui e' uscito: il gioco continua a leggere quel valore e
 * la visuale continua a scorrere in quella direzione. Con SDL_GetGlobalMouseState la posizione e'
 * sempre quella vera, anche fuori dalla finestra (convertita in coordinate finestra e limitata). */
static void mouse_poll_global(void) {
    int gx = 0, gy = 0, wx = 0, wy = 0, nx, ny, w = g_w, h = g_h;
    struct host_input_event e;
    if (!g_win || !host_elf_input_owner()) return;
    if (getenv("SC_CONTROL_FILE") || getenv("SC_INPUT_SCRIPT") || getenv("SC_INPUT_REPLAY") || getenv("SC_NO_GLOBAL_MOUSE")) return;
    if (SDL_GetGlobalMouseState(&gx, &gy) != 0) return;
    SDL_GetWindowPosition(g_win, &wx, &wy);
    { int cx, cy;
      win_to_canvas(gx - wx, gy - wy, &cx, &cy);
      nx = cx; ny = cy;
      if (w > 0 && nx >= w) nx = w - 1;
      if (h > 0 && ny >= h) ny = h - 1; }
    if ((unsigned int) nx == g_mouse_x && (unsigned int) ny == g_mouse_y) return;
    g_mouse_x = (unsigned int) nx; g_mouse_y = (unsigned int) ny;
    if (getenv("SC_MOUSE_TRACE")) {
        char b[112];
        snprintf(b, sizeof b, "[mouse] poll global=%d,%d window=%d,%d -> %d,%d\n", gx, gy, wx, wy, nx, ny);
        reasm_elf_log(b);
    }
    memset(&e, 0, sizeof e);
    e.type = HOST_INPUT_MOVE;
    e.x = nx; e.y = ny;
    e.buttons = g_polled_buttons;
    e.modifiers = g_polled_mods;
    input_put(&e);
}

/* Hook di prova (solo per verifiche headless, dichiarati):
 *  SC_FULLSCREEN_TEST=1        attiva il fullscreen dopo 3 s (una volta)
 *  SC_MOUSE_MAP_TEST="wx,wy"   dopo 5 s stampa viewport + coordinate canvas risultanti
 */
static void test_hooks_tick(void) {
    const char *fs = getenv("SC_FULLSCREEN_TEST");
    const char *mm = getenv("SC_MOUSE_MAP_TEST");
    unsigned int el = SDL_GetTicks() - g_test_start;
    static int fs_done, mm_done;
    if (fs && !fs_done && el >= 3000) { fs_done = 1; host_elf_fullscreen_toggle(); }
    {   /* SC_WINDOW_SIZE="w,h": ridimensiona la finestra (serve a provare il letterbox senza WM) */
        const char *ws = getenv("SC_WINDOW_SIZE");
        static int ws_done;
        if (ws && !ws_done && el >= 2000) {
            int w = 0, h = 0;
            ws_done = 1;
            if (sscanf(ws, "%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
                SDL_SetWindowSize(g_win, w, h);
                SDL_SetWindowPosition(g_win, 0, 0);   /* solo test: porta la finestra a 0,0 */
                { char b[96]; snprintf(b, sizeof b, "[host] finestra ridimensionata a %dx%d\n", w, h); reasm_elf_log(b); }
            }
        }
    }
    if (mm && !mm_done && el >= 5000) {
        SdlRect vp; int wx = 0, wy = 0, cx = 0, cy = 0;
        mm_done = 1;
        renderer_viewport(&vp);
        if (sscanf(mm, "%d,%d", &wx, &wy) == 2) {
            char b[160];
            win_to_canvas(wx, wy, &cx, &cy);
            int ow = 0, oh = 0, iw = 0, ih = 0;
            (void) ow; (void) oh;
            if (g_win) SDL_GetWindowSize(g_win, &iw, &ih);
            snprintf(b, sizeof b, "[map] viewport=%d,%d %dx%d output=%dx%d finestra=%dx%d punto=%d,%d -> canvas=%d,%d\n",
                     vp.x, vp.y, vp.w, vp.h, ow, oh, iw, ih, wx, wy, cx, cy);
            reasm_elf_log(b);
        }
    }
}

/* Cadenza massima del pump di input: il gioco chiama la pump centinaia di volte per frame e ogni
 * SDL_PollEvent senza eventi costa una recvmsg() sul socket X/Wayland (EAGAIN). Un tetto a ~500 Hz
 * (2 ms) riduce di ~35x le syscall senza cambiare il comportamento osservabile: gli eventi restano
 * accodati da SDL e vengono consegnati al giro successivo. SC_PUMP_HZ=0 disattiva il tetto. */
static unsigned int pump_hz_cap(void) {
    static unsigned int hz = 0xFFFFFFFFu;
    if (hz == 0xFFFFFFFFu) {
        const char *s = getenv("SC_PUMP_HZ");
        long v = s && *s ? atol(s) : 500;
        if (v < 0) v = 0;
        if (v > 20000) v = 20000;
        hz = (unsigned int) v;
    }
    return hz;
}

int host_elf_pump(void) {
    SDL_EventBuf e;
    unsigned int hz;
    static unsigned int last_ms;
    static int have_last;
    unsigned int now_ms;
    if(!host_elf_input_owner()) return g_quit;
    hz = pump_hz_cap();
    now_ms = SDL_GetTicks();
    if (hz && have_last && (now_ms - last_ms) * hz < 1000u) return g_quit;
    last_ms = now_ms; have_last = 1;
    mouse_poll_global();
    test_hooks_tick();
    input_test_tick();
    /* Timer Win32: consegna TIMERPROC/WM_TIMER QUI, cioe' nel thread del gioco che ha creato il
     * timer (semantica Windows). Senza questa chiamata i timer registrati non scattano mai. */
    /* debole: i gate standalone (dsound_test, video_test) non linkano win32_shim.c */
    { extern int win32_shim_timers_pump(void) __attribute__((weak));
      if (win32_shim_timers_pump) win32_shim_timers_pump(); }
    /* Backpressure: leave SDL events in SDL rather than lose key/button UP. */
    while (!input_full() && SDL_PollEvent(&e)) {
        unsigned int t=EV_TYPE(&e); struct host_input_event in; int enqueue=1;
        memset(&in,0,sizeof in);
        if(t!=SDL_QUIT && *(unsigned int *)((unsigned char *)&e+8)!=g_window_id) continue;
        if(t==SDL_QUIT) { g_quit=1; in.type=HOST_INPUT_QUIT; }
        else if(t==SDL_WINDOWEVENT) {
            int w=((unsigned char *)&e)[WINDOWEVENT_OFF];
            if(w==SDL_WINDOWEVENT_CLOSE) { g_quit=1; in.type=HOST_INPUT_QUIT; }
            else if(w==12 || w==13) {
                in.type=HOST_INPUT_FOCUS; in.key=(w==12);
                if(w==13) { memset(g_keys,0,sizeof g_keys); g_mods=g_polled_mods=0; g_mouse_btn=g_polled_buttons=0; }
            } else if(w==3) { in.type=HOST_INPUT_EXPOSE; g_present_force=1; }
            else enqueue=0;
        } else if(t==SDL_MOUSEMOTION || t==SDL_MOUSEBUTTONDOWN || t==SDL_MOUSEBUTTONUP) {
            int b=EV_BUTTON(&e);
            { int cx, cy; win_to_canvas(EV_X(&e), EV_Y(&e), &cx, &cy);
              g_mouse_x=(unsigned int)cx; g_mouse_y=(unsigned int)cy; }
            if(t==SDL_MOUSEMOTION) { g_polled_buttons=EV_MOTION_STATE(&e); in.type=HOST_INPUT_MOVE; }
            else if(b>=1 && b<=3) {
                int mask=1<<(b-1);
                if(t==SDL_MOUSEBUTTONDOWN) g_polled_buttons|=mask; else g_polled_buttons&=~mask;
                in.type=t==SDL_MOUSEBUTTONDOWN?HOST_INPUT_DOWN:HOST_INPUT_UP; in.key=b;
            } else enqueue=0;
        } else if(t==SDL_KEYDOWN || t==SDL_KEYUP) {
            int s=key_slot(EV_SYM(&e));
            /* Fullscreen: F11 oppure Alt+Invio. L'evento NON viene inoltrato al gioco (su Windows
             * Alt+Invio e' gestito dal sistema, non dall'applicazione). */
            if(t==SDL_KEYDOWN) {
                int sym=EV_SYM(&e), mods=*(const unsigned short *)((const unsigned char *)&e+24);
                if(sym==0x40000044/*F11*/ || ((sym==0x0D/*Return*/ || sym==0x40000058/*KP Enter*/) && (mods & 0x0300))) {
                    host_elf_fullscreen_toggle();
                    continue;
                }
            }
            in.type=t==SDL_KEYDOWN?HOST_INPUT_KEYDOWN:HOST_INPUT_KEYUP; in.key=EV_SYM(&e);
            if(s>=0) g_keys[s]=(t==SDL_KEYDOWN);
            g_polled_mods=*(unsigned short *)((unsigned char *)&e+24);
            if(t==SDL_KEYDOWN) g_last_key=(unsigned int)in.key;
            in.buttons=((unsigned char *)&e)[13]; /* repeat for key events */
        } else if(t==SDL_TEXTINPUT) { in.type=HOST_INPUT_TEXT; memcpy(in.text,(unsigned char *)&e+12,32); in.text[31]=0; }
        else enqueue=0;
        in.x=(int)g_mouse_x; in.y=(int)g_mouse_y;
        g_mods=g_polled_mods; g_mouse_btn=g_polled_buttons;
        in.modifiers=(int)g_polled_mods;
        if(in.type!=HOST_INPUT_KEYDOWN && in.type!=HOST_INPUT_KEYUP) in.buttons=g_polled_buttons;
        if(enqueue) input_put(&in);
    }
    return g_quit;
}

void host_elf_mouse(int *x, int *y, int *buttons) {
    if (x) *x = (int) g_mouse_x;
    if (y) *y = (int) g_mouse_y;
    if (buttons) *buttons = g_mouse_btn << 1; /* preserve T1 legacy left-button bit 2 */
}
unsigned int host_elf_last_key(void) { return g_last_key; }
const char *host_elf_last_error(void) { return g_err ? g_err : ""; }

/* ============================ AUDIO (Q2c) ============================
 * Device 22050 Hz 2ch 16 bit + mixer software, governato dalla callback di SDL (nessun thread
 * nostro). Le API SDL audio sono dichiarate a mano come quelle video (header non includibili in
 * -m32): layout di SDL_AudioSpec verificato (freq@0 format@4 channels@6 silence@7 samples@8
 * size@12 callback@16 userdata@20, sizeof 24) e AUDIO_S16LSB = 0x8010. */
extern int SDL_InitSubSystem(unsigned int flags);
extern const char *SDL_GetCurrentAudioDriver(void);
extern unsigned int SDL_OpenAudioDevice(const char *device, int capture, const void *want,
                                        void *have, int allowed);
extern void SDL_CloseAudioDevice(unsigned int dev);
extern void SDL_PauseAudioDevice(unsigned int dev, int pause_on);
extern int SDL_GetAudioDeviceStatus(unsigned int dev);

typedef struct {
    int freq;
    unsigned short format;
    unsigned char channels, silence;
    unsigned short samples, padding;
    unsigned int size;
    void (*callback)(void *ud, unsigned char *stream, int len);
    void *userdata;
} SDL_AudioSpec_;

#define SDL_INIT_AUDIO    0x00000010u
#define SDL_AUDIO_S16LSB  0x8010u

#define HOST_RING_SIZE 65536u

typedef struct {
    int used;
    const unsigned char *pcm;
    unsigned long bytes;
    int rate, ch, bits, loop, vol, pan;
    volatile unsigned long pos;      /* posizione di lettura in BYTE */
    volatile unsigned long played;   /* frame consumati dal mix */
    volatile int playing, ended, started;
} HostVoice;

static HostVoice g_voices[HOST_VOICES];
static unsigned int g_adev;
static int g_aopen, g_arate = 22050, g_ach = 2, g_abits = 16;
static const char *g_adrv = "?";
static const char *g_aerr = "";
static volatile unsigned long g_aframes, g_abytes;
static volatile unsigned int g_apeak;
static unsigned char g_ring[HOST_RING_SIZE];
static volatile unsigned long g_ring_rd, g_ring_wr;

static int host_clamp16(int v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : v); }

/* coda diretta: produttore = thread del gioco, consumatore = thread audio di SDL */
static void ring_write(const unsigned char *d, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        unsigned long w = g_ring_wr;
        if (w - g_ring_rd >= HOST_RING_SIZE) break;      /* piena: il resto si scarta */
        g_ring[w % HOST_RING_SIZE] = d[i];
        g_ring_wr = w + 1;
    }
}
static void ring_read(unsigned char *out, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        if (g_ring_rd >= g_ring_wr) { out[i] = 0; continue; }
        out[i] = g_ring[g_ring_rd % HOST_RING_SIZE];
        ++g_ring_rd;
    }
}

/* mixer: somma le voci attive nel buffer di uscita (S16LSB, canali = g_ach) */
static void mix_voices(unsigned char *out, int len) {
    int out_ch = (g_ach > 1) ? 2 : 1;
    int frames = (int) (len / (out_ch * 2));
    int v, i;
    for (v = 0; v < HOST_VOICES; ++v) {
        HostVoice *p = &g_voices[v];
        unsigned long fb, step, frac = 0, total, fr;
        int gl, gr;
        if (!p->used || !p->playing || !p->pcm || p->bytes == 0) continue;
        if (p->bits != 8 && p->bits != 16) continue;
        if (p->ch != 1 && p->ch != 2) continue;
        if (p->rate <= 0) continue;
        fb = (unsigned long) p->ch * (unsigned long) (p->bits / 8);
        total = p->bytes / fb;
        if (total == 0) continue;
        /* passo in frame, 16.16: rate_voce / rate_uscita (rate < 65536 -> nessun overflow a 32 bit) */
        step = ((unsigned long) p->rate * 65536ul) / (unsigned long) (g_arate ? g_arate : p->rate);
        gl = (p->vol < 0) ? 0 : (p->vol > 256 ? 256 : p->vol);
        gr = gl;
        if (p->pan > 0) gl = gl * (100 - (p->pan > 100 ? 100 : p->pan)) / 100;
        else if (p->pan < 0) gr = gr * (100 + (p->pan < -100 ? -100 : p->pan)) / 100;
        for (i = 0; i < frames; ++i) {
            int l, r;
            if (p->pos >= p->bytes) {
                if (p->loop) p->pos = 0;
                else { p->playing = 0; p->ended = 1; break; }
            }
            fr = p->pos / fb;
            if (fr >= total) { p->playing = 0; p->ended = 1; break; }
            {
                const unsigned char *q = p->pcm + fr * fb;
                if (p->bits == 16) {
                    l = *(const short *) q;
                    r = (p->ch > 1) ? *(const short *) (q + 2) : l;
                } else {
                    l = (((int) q[0]) - 128) << 8;
                    r = (p->ch > 1) ? ((((int) q[1]) - 128) << 8) : l;
                }
            }
            if (out_ch == 1) {
                int m = (l + r) / 2;
                *(short *) (out + i * 2) = (short) host_clamp16(*(short *) (out + i * 2) + m * gl / 256);
            } else {
                *(short *) (out + i * 4) = (short) host_clamp16(*(short *) (out + i * 4) + l * gl / 256);
                *(short *) (out + i * 4 + 2) = (short) host_clamp16(*(short *) (out + i * 4 + 2) + r * gr / 256);
            }
            ++p->played;
            frac += step;
            while (frac >= 65536ul) { p->pos += fb; frac -= 65536ul; }
        }
    }
}

static void audio_cb(void *ud, unsigned char *stream, int len) {
    (void) ud;
    if (len <= 0) return;
    ring_read(stream, len);
    mix_voices(stream, len);
    {   int i, n = len / 2, pk = 0;
        const short *s = (const short *) stream;
        for (i = 0; i < n; ++i) {
            int v = s[i] < 0 ? -s[i] : s[i];
            if (v > pk) pk = v;
        }
        if ((unsigned int) pk > g_apeak) g_apeak = (unsigned int) pk;
    }
    g_abytes += (unsigned long) len;
    g_aframes += (unsigned long) len / (unsigned long) (g_ach * (g_abits / 8));
}

int host_elf_audio_open(int rate, int channels, int bits) {
    SDL_AudioSpec_ want, have;
    if (g_aopen) {
        if (rate == g_arate && channels == g_ach) return 0;
        host_elf_audio_close();
    }
    if (rate <= 0) rate = 22050;
    if (channels != 1 && channels != 2) channels = 2;
    if (bits != 8 && bits != 16) bits = 16;
    (void) bits;                                  /* il device si apre sempre in S16LSB */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        g_aerr = SDL_GetError();
        reasm_elf_log("[host] SDL_InitSubSystem(AUDIO) fallita: ");
        reasm_elf_log(g_aerr);
        reasm_elf_log("\n");
        return -1;
    }
    g_audio_inited = 1;
    { int i2; for (i2 = 0; i2 < 24; ++i2) ((unsigned char *) &want)[i2] = 0; }
    want.freq = rate;
    want.format = SDL_AUDIO_S16LSB;
    want.channels = (unsigned char) channels;
    want.samples = 512;
    want.callback = audio_cb;
    want.userdata = 0;
    g_adev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (!g_adev) {
        g_aerr = SDL_GetError();
        reasm_elf_log("[host] SDL_OpenAudioDevice fallita: ");
        reasm_elf_log(g_aerr);
        reasm_elf_log("\n");
        return -1;
    }
    g_aopen = 1;
    g_arate = have.freq;
    g_ach = have.channels;
    g_abits = 16;
    g_adrv = SDL_GetCurrentAudioDriver();
    g_ring_rd = g_ring_wr = 0;
    g_aframes = g_abytes = 0;
    g_apeak = 0;
    SDL_PauseAudioDevice(g_adev, 0);              /* avvia la riproduzione */
    reasm_elf_log("[host] audio aperto: driver = ");
    reasm_elf_log(g_adrv ? g_adrv : "?");
    reasm_elf_log(", " );
    reasm_elf_log_hex((unsigned) g_arate);
    reasm_elf_log(" Hz, canali ");
    reasm_elf_log_hex((unsigned) g_ach);
    reasm_elf_log("\n");
    return 0;
}

void host_elf_audio_close(void) {
    if (!g_aopen) return;
    SDL_PauseAudioDevice(g_adev, 1);
    SDL_CloseAudioDevice(g_adev);
    g_adev = 0; g_aopen = 0;
    g_ring_rd = g_ring_wr = 0;
}
int host_elf_audio_is_open(void) { return g_aopen; }
int host_elf_audio_format(int *rate, int *channels, int *bits) {
    if (!g_aopen) return -1;
    if (rate) *rate = g_arate;
    if (channels) *channels = g_ach;
    if (bits) *bits = g_abits;
    return 0;
}
const char *host_elf_audio_driver(void) { return g_adrv ? g_adrv : "?"; }
int host_elf_audio_write(const void *pcm, int bytes) {
    if (!g_aopen || !pcm || bytes <= 0) return -1;
    ring_write((const unsigned char *) pcm, bytes);
    return bytes;
}
void host_elf_audio_pause(int pause_on) { if (g_aopen) SDL_PauseAudioDevice(g_adev, pause_on ? 1 : 0); }
unsigned long host_elf_audio_frames(void) { return g_aframes; }
unsigned long host_elf_audio_bytes(void) { return g_abytes; }
unsigned long host_elf_audio_queued(void) { return (unsigned long) (g_ring_wr - g_ring_rd); }
unsigned int host_elf_audio_peak(void) { return g_apeak; }
void host_elf_audio_peak_reset(void) { g_apeak = 0; }

int host_elf_voice_used_count(void) {
    int i, n = 0;
    for (i = 0; i < HOST_VOICES; ++i) if (g_voices[i].used) ++n;
    return n;
}

int host_elf_voice_open(const host_audio_voice *v) {
    int i;
    if (!v || !v->pcm || v->bytes == 0) return -1;
    for (i = 0; i < HOST_VOICES; ++i)
        if (!g_voices[i].used) {
            HostVoice *p = &g_voices[i];
            p->used = 1;
            p->pcm = v->pcm; p->bytes = v->bytes;
            p->rate = v->rate > 0 ? v->rate : 22050;
            p->ch = (v->channels == 1) ? 1 : 2;
            p->bits = (v->bits == 8) ? 8 : 16;
            p->loop = v->loop ? 1 : 0;
            p->vol = (v->volume < 0) ? 0 : (v->volume > 256 ? 256 : v->volume);
            p->pan = (v->pan < -100) ? -100 : (v->pan > 100 ? 100 : v->pan);
            p->pos = 0; p->played = 0;
            p->playing = 0; p->ended = 0; p->started = 0;
            return i;
        }
    return -1;
}
static HostVoice *voice_get(int id) {
    if (id < 0 || id >= HOST_VOICES) return 0;
    return g_voices[id].used ? &g_voices[id] : 0;
}
void host_elf_voice_close(int id) { HostVoice *p = voice_get(id); if (p) { p->used = 0; p->pcm = 0; p->bytes = 0; p->playing = 0; } }
void host_elf_voice_detach(int id) { HostVoice *p = voice_get(id); if (p) { p->pcm = 0; p->bytes = 0; p->playing = 0; } }
void host_elf_voice_set(int id, const host_audio_voice *v) {
    HostVoice *p = voice_get(id);
    if (!p || !v) return;
    if (v->pcm && v->bytes) { p->pcm = v->pcm; p->bytes = v->bytes; }
    if (v->rate > 0) p->rate = v->rate;
    if (v->channels == 1 || v->channels == 2) p->ch = v->channels;
    if (v->bits == 8 || v->bits == 16) p->bits = v->bits;
    p->loop = v->loop ? 1 : 0;
    p->vol = (v->volume < 0) ? 0 : (v->volume > 256 ? 256 : v->volume);
    p->pan = (v->pan < -100) ? -100 : (v->pan > 100 ? 100 : v->pan);
}
void host_elf_voice_set_pos(int id, unsigned long pos) {
    HostVoice *p = voice_get(id);
    if (!p) return;
    if (pos > p->bytes) pos = p->bytes;
    p->pos = pos;
}
unsigned long host_elf_voice_pos(int id) { HostVoice *p = voice_get(id); return p ? p->pos : 0; }
unsigned long host_elf_voice_played(int id) { HostVoice *p = voice_get(id); return p ? p->played : 0; }
unsigned int host_elf_voice_status(int id) {
    HostVoice *p = voice_get(id);
    if (!p) return 0;
    return (p->playing ? HOST_VOICE_PLAYING : 0u) |
           (p->playing && p->loop ? HOST_VOICE_LOOPING : 0u) |
           (p->ended && !p->playing ? HOST_VOICE_ENDED : 0u);
}
void host_elf_voice_play(int id) {
    HostVoice *p = voice_get(id);
    if (!p) return;
    p->ended = 0;
    p->started = 1;
    p->playing = 1;
}
void host_elf_voice_stop(int id) { HostVoice *p = voice_get(id); if (p) p->playing = 0; }
void host_elf_voices_reset(void) { int i; for (i = 0; i < HOST_VOICES; ++i) { g_voices[i].used = 0; g_voices[i].pcm = 0; g_voices[i].bytes = 0; g_voices[i].playing = 0; } }
