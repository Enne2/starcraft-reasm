/* sc-reasm/tools/elf/com_ddraw.h — oggetti DirectDraw in C per il runtime ELF nativo (Q2).
 *
 * Modello di riferimento (read-only): ~/dev/lola-recomp/runtime/com/ddraw_shim.c, di cui qui si
 * conservano le "leggi" gia' accertate: vtable in stile C (this = primo argomento di stack, quindi
 * normali funzioni STDCALL: nessun thunk), layout DirectX 5+ (IDirectDraw 23 slot,
 * IDirectDrawSurface 33, IDirectDrawPalette 7, IDirectDrawClipper 9; Blt=5, IsLost=24, Restore=27,
 * SetClipper=28, Lock=25, Unlock=32, SetPalette=31), DDSURFACEDESC(2) con ddsCaps a 0x68 e
 * dwSize=124, un RECT VUOTO non disegna nulla.
 *
 * Differenza rispetto a LOLA: qui il percorso e' 8 bit + palette (come il gioco: SetDisplayMode
 * 640x480x8), quindi GetPixelFormat dichiara DDPF_PALETTEINDEXED8; e i pixel non passano da
 * SDL_Surface ma dal canvas del host (host_elf.h), unico posto che tocca SDL.
 */
#ifndef COM_DDRAW_H
#define COM_DDRAW_H

#ifdef __i386__
#define COM_STDCALL __attribute__((stdcall))
#else
#define COM_STDCALL
#endif

/* Punto d'ingresso come lo chiama il gioco: `push outer; push out; push guid; call`
 * (WINAPI = stdcall, 3 argomenti -> il callee pulisce 12 byte). */
long COM_STDCALL DirectDrawCreate(const void *guid, void **out, void *outer);
long COM_STDCALL DirectDrawCreateClipper(unsigned long flags, void **out, void *outer);

/* Oggetto IDirectDraw piu' recente (il gioco ne crea uno: e' il device corrente). */
void *com_ddraw_last(void);
/* Superficie primaria del device corrente, come puntatore all'oggetto COM (o 0). */
void *com_ddraw_primary_surface(void);

/* Smonta lo stato: libera le superfici create da com_ddraw e chiude la finestra del host.
 * (I buffer delle superfici primarie sono del host: non vengono liberati qui.) */
void com_ddraw_shutdown(void);

#endif /* COM_DDRAW_H */
