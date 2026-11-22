/* sc-reasm/tools/elf/host_elf.h — host video nativo per il runtime ELF (Q2).
 *
 * Unico punto del runtime in cui si parla con SDL2. Tutto il resto (oggetti COM DirectDraw di
 * com_ddraw.c, e piu' tardi la SDraw di storm) usa QUESTA API: cosi' le regole di geometria
 * (rettangolo vuoto = non disegna, clipping, color key, stretch) stanno in un posto solo.
 *
 * Vincolo di build: `gcc -m32 -nostdinc -ffreestanding` NON riesce a includere <SDL2/SDL.h>
 * (SDL_config.h pretende SDL_config-i386.h, assente senza i pacchetti -devel.i686): le API SDL
 * usate sono dichiarate a mano dentro host_elf.c, come gia' si fa per libc nel runtime del
 * toolkit. Questo header non include nulla e non espone tipi SDL.
 *
 * Convenzioni:
 *  - canvas = superficie 8 bit indicizzata della finestra (la "primaria" DirectDraw).
 *  - palette = 256 voci 0x00RRGGBB.
 *  - un rect e' int[4] = {left, top, right, bottom}: NULL = tutta la superficie.
 *    UN RECT VUOTO (right <= left o bottom <= top) NON DISEGNA NULLA (regola D23, da LOLA).
 *  - tutte le funzioni ritornano 0 su successo, <0 su errore, salvo dove indicato.
 *  - il runtime e' i386: `unsigned char *` = pixel buffer.
 */
#ifndef HOST_ELF_H
#define HOST_ELF_H

/* ---- ciclo di vita della finestra ---- */
int host_elf_init(const char *title);                 /* 0 ok (non crea ancora la finestra) */
int host_elf_display_mode(int w, int h, int bpp);     /* crea/ridimensiona finestra + canvas 8 bit */
void host_elf_shutdown(void);
int host_elf_window_size(int *w, int *h);             /* 0 ok; w/h della finestra (canvas) */
/* SetCooperativeLevel(hwnd, flags): bit 0x1 = DDSCL_FULLSCREEN. In un ELF non esiste un HWND
 * Windows: il parametro viene accettato e ignorato, la finestra e' quella di SDL. */
int host_elf_coop_level(unsigned long hwnd, unsigned long flags);
int host_elf_is_fullscreen(void);
void host_elf_set_title(const char *title);

/* ---- canvas 8 bit: lock/unlock e present ----
 * Lock/Unlock sono contatori software sul buffer del canvas (nessuna SDL_LockSurface: il canvas
 * e' memoria nostra e SDL non lo tocca mai). Il pitch e' >= larghezza e puo' essere > (righe
 * allineate): mai assumere pitch == w. */
unsigned char *host_elf_canvas_lock(int *w, int *h, int *pitch);
void host_elf_canvas_unlock(void);
int host_elf_canvas_locked(void);
unsigned char *host_elf_canvas(int *w, int *h, int *pitch);   /* senza lock: per letture */
void host_elf_present(void);                          /* converti canvas -> finestra + update */

/* ---- palette: colors[i] = 0x00RRGGBB ---- */
void host_elf_set_palette(const unsigned int *colors, int first, int n);
int host_elf_get_palette(unsigned int *colors, int first, int n);

/* ---- primitive 8 bit su un buffer qualsiasi (usate da com_ddraw per ogni superficie) ----
 * Ritornano 1 se hanno disegnato almeno un pixel, 0 se il rect era vuoto o interamente clippato. */
int host_elf_fill8(unsigned char *px, int pitch, int sw, int sh,
                   const int *rect, int idx);
/* ckey: indice sorgente da NON copiare (color key), oppure -1 per copiare tutto.
 * Se il rect di destinazione ha dimensioni diverse dalla sorgente: stiramento nearest-neighbour
 * (come DirectDraw con lo stretch abilitato). */
int host_elf_blit8(unsigned char *dpx, int dpitch, int dw, int dh,
                   const unsigned char *spx, int spitch, int sw, int sh,
                   const int *drect, const int *srect, int ckey);

/* ---- readback del framebuffer della finestra (per i gate pixel) ---- */
int host_elf_read_pixel(int x, int y, unsigned char rgb[3]);   /* 0 ok */

/* ---- tempo e input ---- */
unsigned int host_elf_ticks(void);
void host_elf_delay(unsigned int ms);
int host_elf_pump(void);                              /* 1 = l'utente ha chiesto di uscire */
void host_elf_mouse(int *x, int *y, int *buttons);
unsigned int host_elf_last_key(void);
const char *host_elf_last_error(void);

/* T3: FIFO, SDL keycodes + SDL KMOD mask; mouse buttons use SDL_BUTTON masks.
 * Single consumer: window-owning thread. next_input does not poll SDL. */
enum { HOST_INPUT_MOVE=1, HOST_INPUT_DOWN, HOST_INPUT_UP, HOST_INPUT_KEYDOWN,
       HOST_INPUT_KEYUP, HOST_INPUT_TEXT, HOST_INPUT_FOCUS, HOST_INPUT_QUIT,
       HOST_INPUT_EXPOSE };
struct host_input_event {
    int type, key, modifiers, x, y, buttons;
    char text[32];
};
int host_elf_next_input(struct host_input_event *out); /* 1 event, 0 empty */
int host_elf_input_pending(void);
int host_elf_key_state(int key); /* 0x80 pressed; SDL letters, VK mouse/modifiers */
int host_elf_input_owner(void);
int host_elf_mouse_screen(int *x, int *y);
int host_elf_mouse_warp(int x, int y); /* SCREEN coordinates */
void host_elf_mouse_capture(int on);
void host_elf_cursor_visible(int on);
int host_elf_test_event(const struct host_input_event *e); /* SDL_PushEvent, tests only */
int host_elf_test_text(const char *utf8); /* TEST ONLY: host FIFO, not SDL_PushEvent */
void host_elf_dispatch_state(unsigned int message, unsigned int key);
void host_elf_input_state(const struct host_input_event *e); /* dequeued MSG snapshot */
unsigned int host_elf_input_dropped(void);

/* ---- AUDIO (Q2c): device 22050 Hz 2ch 16 bit + mixer software ----
 * L'uscita passa dalla callback di SDL (nessun thread nostro: SDL crea e governa il thread audio).
 * Il mixer somma le "voci" registrate dal chiamante (i buffer DirectSound) e ricampiona quelle con
 * frequenza diversa dall'uscita; una coda (ring) permette anche la scrittura diretta.
 * I dati di una voce NON vengono copiati: restano di proprieta' del chiamante, che deve
 * richiamare host_elf_voice_close() o host_elf_voice_detach() prima di liberarli. */
typedef struct host_audio_voice {
    const unsigned char *pcm;    /* dati PCM (memoria del chiamante) */
    unsigned long bytes;         /* dimensione in byte */
    int rate, channels, bits;    /* formato della voce: 8/16 bit, 1/2 canali */
    int loop;                    /* 1 = ripeti dall'inizio a fine buffer */
    int volume;                  /* 0..256 (256 = unitario) */
    int pan;                     /* -100 (sinistra) .. +100 (destra) */
} host_audio_voice;

#define HOST_VOICE_PLAYING 0x1u
#define HOST_VOICE_LOOPING 0x2u
#define HOST_VOICE_ENDED   0x4u
#define HOST_VOICES        32

int host_elf_audio_open(int rate, int channels, int bits);   /* 0 ok */
void host_elf_audio_close(void);
int host_elf_audio_is_open(void);
int host_elf_audio_format(int *rate, int *channels, int *bits);   /* formato OTTENUTO */
const char *host_elf_audio_driver(void);
int host_elf_audio_write(const void *pcm, int bytes);        /* push diretto (formato del device) */
void host_elf_audio_pause(int pause_on);
unsigned long host_elf_audio_frames(void);                    /* frame usciti dal device (gate) */
unsigned long host_elf_audio_bytes(void);                     /* byte PCM usciti dal device (gate) */
unsigned long host_elf_audio_queued(void);                    /* byte ancora in coda */
unsigned int host_elf_audio_peak(void);                       /* picco |campione| visto (gate) */
void host_elf_audio_peak_reset(void);

int  host_elf_voice_open(const host_audio_voice *v);          /* id voce (>=0) o -1 */
int host_elf_voice_used_count(void);                /* diagnostica: voci occupate */
void host_elf_voice_close(int id);
void host_elf_voice_set(int id, const host_audio_voice *v);  /* aggiorna parametri (posizione intatta) */
void host_elf_voice_set_pos(int id, unsigned long pos);      /* posizione in BYTE */
unsigned long host_elf_voice_pos(int id);
unsigned long host_elf_voice_played(int id);                  /* frame consumati dal mix */
unsigned int host_elf_voice_status(int id);                   /* HOST_VOICE_* */
void host_elf_voice_play(int id);
void host_elf_voice_stop(int id);
void host_elf_voice_detach(int id);                           /* i dati non sono piu' validi */
void host_elf_voices_reset(void);

#endif /* HOST_ELF_H */
