/* sc-reasm/tools/elf/com_dsound.h — DirectSound in C per il runtime ELF nativo (Q2c).
 *
 * Superficie: IDirectSound (11 metodi) + IDirectSoundBuffer (21 metodi), come i modelli
 * ~/dev/lola-recomp/runtime/com/dsound_shim.c e ~/dev/reasm-toolkit/port/dsound_shim.c.
 * Convenzione: le chiamate del gioco sono in stile C (`push oggetto; call [vtable+off]`) -> normali
 * funzioni STDCALL, `this` primo argomento di stack, nessun thunk. Le arita' sono quelle del DDK
 * (NON dedotte dai call-site): tabella completa in coda al file.
 *
 * Punto aperto di docs/05 §5 (il gioco RISOLVE DirectSoundCreate ma non la chiama): qui c'e' la
 * strumentazione per chiuderlo — contatore + log della PRIMA chiamata reale con il return address
 * del chiamante, piu' un contatore separato delle *risoluzioni* (GetProcAddress), che Q1 puo'
 * incrementare con com_dsound_note_resolve().
 */
#ifndef COM_DSOUND_H
#define COM_DSOUND_H

#ifdef __i386__
#define DSOUND_STDCALL __attribute__((stdcall))
#else
#define DSOUND_STDCALL
#endif

/* Entry point come li chiama il gioco: 3 argomenti -> ret 12. */
long DSOUND_STDCALL DirectSoundCreate(const void *guid, void **out, void *outer);
long DSOUND_STDCALL DirectSoundCreate8(const void *guid, void **out, void *outer);

/* Oggetti persistenti, NON canali simultanei (HOST_VOICES). Il gioco ha 1144 suoni
 * in cache + duplicati: 48 slot provocavano errori silenziosi dopo poche frasi. */
#define DSOUND_BUFFER_CAPACITY 2048

/* Statistiche (per il supervisore e per i gate). */
typedef struct com_dsound_stats {
    unsigned int creates;         /* chiamate reali a DirectSoundCreate/Create8 */
    unsigned int first_caller;    /* return address della PRIMA chiamata (0 = mai chiamata) */
    unsigned int first_tick;      /* host_elf_ticks() della prima chiamata */
    unsigned int resolves;        /* risoluzioni di DirectSoundCreate (via note_resolve) */
    unsigned int resolve_caller;  /* return address della prima risoluzione (0 = mai) */
    unsigned int buffers;         /* Create/DuplicateSoundBuffer riusciti (totale) */
    unsigned int plays;           /* Play totali */
    unsigned int unlocks;         /* Unlock totali */
    unsigned int bytes_locked;    /* byte massimi resi scrivibili da Lock (diagnostica) */
    unsigned int bytes_mixed;     /* PCM uscito verso il device (diagnostica) */
    unsigned int voice_steals;    /* voci rubate a un buffer piu' vecchio (mixer pieno) */
    unsigned int voice_fail;      /* Play rimasti SENZA voce (difetto: non deve accadere) */
    unsigned int buffers_live;    /* tutti gli oggetti vivi, primaria inclusa */
    unsigned int buffers_peak;    /* massimo simultaneo, NON totale delle creazioni */
    unsigned int buffer_fail;     /* Create/Duplicate falliti: SEMPRE loggati */
    unsigned int buffer_capacity; /* limite statico dichiarato, indipendente dal mixer */
} com_dsound_stats;

void com_dsound_get_stats(com_dsound_stats *out);
/* Q1 la chiama quando GetProcAddress("DirectSoundCreate") viene risolta: distingue
 * "risolta ma mai chiamata" da "risolta e chiamata" (docs/05 §5). */
void com_dsound_note_resolve(unsigned int caller);
void *com_dsound_last(void);                 /* ultimo IDirectSound creato (0 = nessuno) */
void *com_dsound_first_buffer(void);         /* primo IDirectSoundBuffer creato (0 = nessuno) */
void com_dsound_reset(void);                 /* chiude voci e oggetti: per i gate */
void com_dsound_shutdown(void);              /* come reset + azzera le statistiche */

/* Aggancio alla risoluzione dinamica: chiamate da win32_shim.c (hook deboli, stesso schema di
 * win32_smack_*). Riducono LoadLibraryA("dsound.dll") e GetProcAddress(handle, "DirectSoundCreate")
 * al modulo nativo. Se com_dsound non e' nel link i simboli sono NULL e nulla cambia. */
void *win32_dsound_load_library(const char *name);
void *win32_dsound_get_proc(void *mod, const char *name);

/* P11 (additiva, audio dei filmati SMACK in tools/elf/smack_audio.c): apre l'uscita host se il gioco
 * non l'ha ancora fatto (formato della primaria, stessa politica di ds_ensure_out) e dice se e'
 * aperta: 1 = si', 0 = no. Chi riceve 0 non deve fingere che l'audio esca (D19). */
int com_dsound_out_ready(void);

#endif /* COM_DSOUND_H */

/* ======================= TABELLA ARITA' PER Q1 =======================
 *  IDirectSound (11)                 N     ret        IDirectSoundBuffer (21)          N     ret
 *  QueryInterface                   3     12         QueryInterface                   3     12
 *  AddRef                           1      4         AddRef                           1      4
 *  Release                          1      4         Release                          1      4
 *  CreateSoundBuffer                4     16         GetCaps                          2      8
 *  GetCaps                          2      8         GetCurrentPosition               3     12
 *  DuplicateSoundBuffer             3     12         GetFormat                        4     16
 *  SetCooperativeLevel              3     12         GetVolume                        2      8
 *  Compact                          1      4         GetPan                           2      8
 *  GetSpeakerConfig                 2      8         GetFrequency                     2      8
 *  SetSpeakerConfig                 2      8         GetStatus                        2      8
 *  Initialize                       2      8         Initialize                       3     12
 *                                                     Lock                             8     32
 *  Fuori dai documentati (UNK, stop):                 Play                             4     16
 *   IDirectSound slot 11 (IDirectSound8::             SetCurrentPosition               2      8
 *   VerifyCertification)                              SetFormat                        2      8
 *   IDirectSoundBuffer slot 21..23                    SetVolume                        2      8
 *   (IDirectSoundBuffer8::SetFX/                      SetPan                           2      8
 *    AcquireResources/GetObjectInPath)               SetFrequency                     2      8
 *                                                     Stop                             1      4
 *                                                     Unlock                           5     20
 *                                                     Restore                          1      4
 *
 *  DirectSoundCreate(guid, out, outer) = 3 -> ret 12;  DirectSoundCreate8 idem.
 *  Play(dwReserved1, dwPriority, dwFlags): i FLAG sono il TERZO argomento (DDK). I modelli
 *  precedenti leggevano il primo (PE) o li ignoravano (LOLA): qui vale la firma documentata.
 *  Nome in gioco: identico al nome API, nessuna decorazione (stdcall su ELF i386 -> `ret N`).
 * ===================================================================== */
