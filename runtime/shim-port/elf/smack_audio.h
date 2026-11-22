/* sc-reasm/tools/elf/smack_audio.h — P11: audio dei filmati SMACK (intro e menu).
 *
 * PERIMETRO: modulo NUOVO. Non tocca smack_elf.c (di un altro ramo): espone 3 hook che il
 * supervisore chiama con UNA riga ciascuno (testo esatto in fondo a questo header). Se il modulo
 * non e' nel link, le dichiarazioni deboli locali valgono NULL e nulla cambia.
 *
 * ATTIVAZIONE: la variabile SC_SMACK_AUDIO. Assente o "1" => audio dei filmati attivo; "0" =>
 * il modulo non fa NULLA (nessun dlopen, nessuna voce, nessuna allocazione): comportamento
 * identico a prima di P11.
 *
 * MISURA (cosa e' coperto e cosa no, regola D19):
 *   coperto:   tracce PCM dichiarate nell'header SMK (u32 a offset 72..99), fino a 2 tracce per
 *              filmato, 8/16 bit, 1/2 canali, qualunque frequenza (il mixer host ricampiona);
 *              fino a 24 oggetti SMACK agganciati insieme (storm ne tiene aperti ~18 nel menu).
 *   non coperto (dichiarato nei log, mai finto successo): audio Bink (compress==2), >2 tracce per
 *              filmato, bitdepth/canali diversi da 8/16 e 1/2. In quei casi la traccia si spegne e
 *              la riga di log lo dice.
 *
 * SEMANTICA DELLE UNITA': i byte consegnati al mixer sono ESATTAMENTE gli smk_get_audio/
 * smk_get_audio_size di quel frame, nel formato della traccia (nessuna conversione nostra).
 * La conversione al formato del device (ricampionamento, mono->stereo) e' quella del mixer host,
 * gia' verificata dal gate dsound_test.sh: non e' codice di questo modulo.
 */
#ifndef SMACK_AUDIO_H
#define SMACK_AUDIO_H

/* ---- aggancio (chiamati da smack_elf.c, vedi testo in fondo) ----
 * La chiave dello stream e' l'HANDLE libsmacker (l'oggetto SMACK di smack_elf.c non esiste ancora
 * quando l'audio va abilitato: `o = calloc(...)` avviene DOPO `smk_first`, e l'audio del frame 0
 * va abilitato PRIMA di `smk_first`). */
void smack_audio_hook_open(void *lib_smk);                    /* prima di smk_first() */
void smack_audio_hook_frame(void *smk_obj, void *lib_smk);   /* percorso sincrono */
void smack_audio_hook_frame_snapshot(void *smk_obj, void *lib_smk, unsigned long frame,
                                     const unsigned char *const pcm[7], const unsigned long sizes[7]);
void smack_audio_hook_close(void *lib_smk);                   /* in SmackClose, prima di smk_close */

/* ---- API SMACK che oggi ritornano 0 senza fare nulla ---- */
int smack_audio_sound_use_dsound(void *idsound);            /* SmackSoundUseDirectSound(arg) */
int smack_audio_volume_pan(void *smk_obj, unsigned track, unsigned volume, unsigned pan);

/* ---- solo per i gate: riceve gli stessi byte consegnati al mixer ---- */
typedef void (*smack_audio_sink_fn)(void *smk_obj, int track, unsigned long frame,
                                    const void *pcm, unsigned long bytes);
void smack_audio_set_sink(smack_audio_sink_fn fn);

typedef struct smack_audio_stats {
    unsigned streams;             /* oggetti SMACK agganciati (hook_open riusciti) */
    unsigned tracks;              /* tracce abilitate (smk_enable_audio) */
    unsigned uncovered;           /* tracce dichiarate NON coperte */
    unsigned chunks;              /* chunk consegnati al mixer */
    unsigned long bytes;          /* byte consegnati al mixer (formato della traccia) */
    unsigned long dropped;        /* byte scartati per anello pieno (dichiarati nel log) */
    unsigned long unrouted;       /* byte NON consegnati: uscita host non aperta */
    unsigned underruns;           /* risincronizzazioni per underrun (lettore oltre lo scrittore) */
    unsigned skipped;             /* frame senza audio (chunk assente) */
    unsigned long rms_last_x100;  /* RMS dell'ultimo chunk in unita' int16, x100 */
    unsigned hash_last;           /* FNV-1a (2166136261/16777619) degli ultimi byte consegnati */
    unsigned long last_frame;     /* indice dell'ultimo frame consegnato */
    unsigned use_dsound_calls;
    unsigned volume_pan_calls;
    unsigned unrouted_logs;       /* quante volte la riga "uscita host non aperta" e' stata stampata */
    unsigned open_slots;          /* slot di stream ancora aperti (0 dopo SmackClose) */
} smack_audio_stats;

void smack_audio_get_stats(smack_audio_stats *out);
void smack_audio_reset(void);     /* gate: azzera stato e statistiche (non chiude l'uscita host) */

#endif /* SMACK_AUDIO_H */

/* ==================== AGGANCIO PER IL SUPERVISORE (5 righe, una per punto) ====================
 * Ogni riga e' autosufficiente (dichiarazione debole locale): se smack_audio.c non e' nel link
 * il simbolo vale NULL e il comportamento resta quello di prima. Il supervisore NON deve
 * includere questo header in smack_elf.c.
 *
 * 1) tools/elf/smack_elf.c — in SmackOpen(), subito PRIMA di `if (p_first(s) == (char) -1) {`:
 *    { extern void smack_audio_hook_open(void *) __attribute__((weak)); if (smack_audio_hook_open) smack_audio_hook_open(s); }
 *
 * 2) tools/elf/smack_elf.c — in SmackDoFrame(), subito PRIMA del `return 0;` finale (il commento
 *    "storm presents a decoded frame only with this return value"):
 *    { extern void smack_audio_hook_frame(void *, void *) __attribute__((weak)); if (smack_audio_hook_frame) smack_audio_hook_frame(o, (void *) (ulong) priv(o)[SMK_PRIV_SMK / 4]); }
 *
 * 3) tools/elf/smack_elf.c — in SmackClose(), subito PRIMA di `if (priv(o)[SMK_PRIV_SMK / 4]) p_close(...)`:
 *    { extern void smack_audio_hook_close(void *) __attribute__((weak)); if (smack_audio_hook_close) smack_audio_hook_close((void *) (ulong) priv(o)[SMK_PRIV_SMK / 4]); }
 *
 * 4) tools/elf/smack_elf.c — SmackSoundUseDirectSound(): aggiungere come PRIMA riga del corpo
 *    { extern int smack_audio_sound_use_dsound(void *) __attribute__((weak)); if (smack_audio_sound_use_dsound) return (u32) smack_audio_sound_use_dsound((void *) (ulong) a1); }
 *
 * 5) tools/elf/smack_elf.c — SmackVolumePan(): aggiungere come PRIMA riga del corpo
 *    { extern int smack_audio_volume_pan(void *, unsigned, unsigned, unsigned) __attribute__((weak)); if (smack_audio_volume_pan) return (u32) smack_audio_volume_pan(smk, track, volume, pan); }
 *
 * 6) tools/elf/build.sh — nel loop DENTRO `if [ "${SC_ELF_NO_VIDEO:-0}" != 1 ]` (accanto a com_dsound.c):
 *    aggiungere "$HERE/smack_audio.c"   (usa host_elf_*, quindi NON fuori dal blocco).
 * ============================================================================================== */
