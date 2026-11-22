/* sc-reasm/tools/elf/win32_shim.h — shim Win32 del runtime ELF nativo di StarCraft.
 *
 * Nel mondo ELF non c'e' nessuna DLL Windows: ogni slot IAT importato per nome viene risolto da
 * `win32_shim_lookup`. Le API non ancora implementate restano trampolini UNK del toolkit
 * (log + _exit(3)): mai uno stub che ritorna (D19).
 */
#ifndef REASM_SC_WIN32_SHIM_H
#define REASM_SC_WIN32_SHIM_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            i32;
typedef unsigned long  ulong;

/* Inizializzazione dello stato finto (std handle, ambiente, heap). */
void win32_shim_init(void);

/* Timer Win32: la consegna (TIMERPROC o WM_TIMER) avviene nel thread del gioco, dal pump. */
int win32_shim_timers_pump(void);
unsigned win32_shim_timer_fires(void);
unsigned win32_shim_timer_misses(void);
unsigned win32_shim_wfmo_anomalies(void);

/* Risolve un'API per nome; NULL = non coperta (il chiamante usa il trampolino UNK). */
void *win32_shim_lookup(const char *name);

/* Tabella dei nomi coperti (per il riepilogo nel log). NULL-terminata. */
const char *const *win32_shim_names(void);

/* Traduzione path Win32 ('C:\a\b') <-> Unix ('/a/b'). */
void reasm_win_path(const char *in, char *out, int n);
void reasm_unix_to_win(const char *in, char *out, int n);

/* Sottosistema risorse/dialoghi/GDI (win32_ui.c). */
void win32_ui_init(void);
void *win32_ui_lookup(const char *name);
unsigned int win32_ui_load_library(const char *name);   /* 0 se non gestita */
unsigned int win32_ui_module_handle(const char *name);  /* 0 se non registrata */
unsigned int win32_ui_fake_module(const char *name);    /* handle dei moduli emulati dagli shim */
void *win32_ui_get_proc(unsigned long mod, const char *name);

#endif /* REASM_SC_WIN32_SHIM_H */
