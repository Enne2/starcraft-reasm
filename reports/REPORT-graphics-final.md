# REPORT — chiusura bug grafici (flicker intro, ritratti campagna, intro nera ARM)

Data: 2026-11-23/24. Host x86: frigate. Target ARM: Pinebook Pro (enne2@10.0.1.126).
Binari verificati: `build/starcraft-x86` (i686) e `build/starcraft-armv7` (armv7a/armhf, hard-float).

Metodo: tutte le prove sono **numeriche** (luminanza media di aree, hash del canvas, contatori
`present-*`) o su log; nessuna finestra aperta sullo schermo dell'utente, audio `dummy`,
schermo virtuale (Xvfb) in background. Le poche immagini ispezionate sono state ridotte a
≤320 px / ≤9 KB.

---

## Bug 1 — flicker nel filmato introduttivo (x86 e ARM) + audio in loop

**Causa (misurata).** Il present host veniva chiamato dopo OGNI `Blt`/`BltFast`/riempimento
DirectDraw. Sul percorso GL reale: 280 present in 15 s di cui **261 con canvas nero (93%)** — cioè
gli stati intermedi (clear, riempimenti di lavoro) venivano promossi a frame visibili. Inoltre il
pacing (sleep fino a 4 ms) stava DENTRO la chiamata di disegno: il thread di gioco rallentava e il
thread che riempie i buffer DirectSound restava indietro → **il pezzo audio andava in loop**.

**Correzione** (`com_ddraw.c`, `host_elf.c`, `gdi_elf.c`, `dlg_elf.c`, `win32_ui.c`):
- su back buffer `Blt/BltFast/fill` **non presentano**: marcano `g_back_dirty` (memoria nascosta);
- la primaria usa `host_elf_present_coalesced()`: max un present ogni `SC_PRESENT_MIN_MS` (12 ms),
  **mai bloccante** (`g_present_nowait` esce prima del pacing);
- `Flip` è il confine di frame: copia back→primary **solo se il back è stato disegnato** (prima
  copiava sempre memoria azzerata sopra il filmato) e presenta subito;
- `PeekMessageA` fa un tick di presentazione a cadenza (schermate disegnate solo via SCode).

**Verifica.** x86, 22 s di intro: 171 present, **3 neri** (solo avvio), **169 frame distinti**
(hash FNV del canvas). ARM: 315 present di filmato, 275 con contenuto, 0 crash.

---

## Bug 2 — selezione campagna: Terran e Zerg neri

**Causa (misurata).** Le tre figure sono film Smacker 184×128 disegnati dal blitter SCode.
`SmackToBuffer` validava `pitch × destheight` contro la **tabella globale dei buffer aperti**, non
contro il contratto della chiamata: le destinazioni del gioco (con padding) venivano rifiutate e i
frame **non scritti**. Log: `ToBuffer fuori buffer: bytes/pitch/height=0x98A00/0xE8/0xD0` (439
occorrenze) e `0x10D80/0xC4/0xDC` (439). Solo il film Protoss aveva un buffer che combaciava.

**Correzione** (`smack_elf.c`):
- la tabella `buffer_range_*` riguarda **solo** i buffer di `SmackBufferOpen`;
- per una destinazione del chiamante il limite è `pitch × destheight` **di quella chiamata**;
- il controllo anti-overrun valida l'**ultimo byte scritto** (`(top+rows-1)*pitch + left + w`),
  non il padding finale; un puntatore interno a un buffer nostro usa la capacità residua;
- `SmackBufferClose` libera il range registrato.

**Verifica** (crop identici, luminanza media e massimo):

| area | prima (x86) | dopo (x86) | dopo (ARM) |
|---|---|---|---|
| Terran | 0,0109 | **0,0538** (max 1,00) | **0,0538** (max 1,00) |
| Zerg | 0,0392 | **0,0968** (max 0,67) | **0,0968** (max 0,67) |
| Protoss | 0,0554 | 0,0666 (max 0,56) | 0,0669 (max 0,56) |

Tutte e tre sopra la soglia 0,02 **senza passare il mouse**; l'hover non altera più i valori.

---

## Bug 3 — intro nera su ARM

**Causa (oracolo IDA + tracce).** Il percorso filmato full-screen di Storm è
`storm_457` (0x1502F500) → `sub_1502EFA0` (0x1502EFA0), che compone i rettangoli via `storm_334`
(0x15030AB0) e **non arriva mai a un Blt/Flip**: le nostre uniche presentazioni erano su Blt/Unlock,
quindi il rettangolo del film veniva azzerato dal riempimento ROP di Storm prima di ogni present.
Su x86 il percorso DirectDraw copriva il caso; su ARM no.

**Correzione** (`storm_sdraw.c` + `host_elf.c` + `smack_elf.c`):
- `smack_elf_buffer_owner()` espone il buffer Smack che possiede un puntatore;
- dopo il blit SCode 2× che scrive il canvas, se la sorgente appartiene a un buffer di filmato si
  chiama `host_elf_movie_present()` (present immediato, fuori dal cap FPS);
- finché il film è aperto, i present "normali" non sovrascrivono il frame (`g_movie_buffer`);
  `SmackBufferClose` → `host_elf_movie_end()` ripristina il percorso normale.

**Verifica ARM** (Xvfb locale, audio dummy, in background): intro completa; 315 present di filmato,
275 con contenuto; fotogrammi riconoscibili (figura/ambiente), poi menu. Nessun `SIGSEGV`.

---

## Salvataggio e caricamento (verifica end-to-end)

| prova | x86 | ARM |
|---|---|---|
| profilo creato dall'UI (`.spc`) | `npQA.spc` 36 B | `armQA.spc` 37 B |
| primo salvataggio in missione | `Save/npQA/createdsave.sng` **104 674 B** — **la directory del profilo è stata creata dal gioco** (assente prima) | `Save/armQA/armsave1.sng` **104 746 B** |
| dialog Save si chiude | sì (ritorno in gioco, luminanza 0,174) | sì (0,172) |
| Load: voce elencata | `svsave1` visibile nel dialog | `armsave1` visibile |
| Load: file aperto e partita ripresa | `[file-open] save/enne2ù/svsave1.sng … ok` + gameplay (media 0,172, sd 0,106) | `[file-open] save/armQA/armsave1.sng … ok-casefold` + gameplay (0,172, sd 0,106) |

Nota di percorso: il gioco salva in `Save/<profilo>/`; se la cartella del profilo manca la crea da
sé (`CreateDirectoryA` → `mkdirat`), verificato sul campo. La cartella `Save/` deve esistere
(la crea il runner/launcher).

---

## Suite di regressione (x86)

`tests/*/check.sh`, 9 test, tutti **esito 0**:

```
sc_ctl_test 0            x86_runtime_check 0       x86_scomp_test 0
x86_affinity_test 0      x86_scode_lut_test 0      x86_shadow_mem_test 0
x86_gl_palette_test 0    x86_io_readahead_test 0   x86_vm_pool_test 0
```

`x86_scode_lut_test` inizialmente falliva per una variabile non usata (`-Werror`) introdotta dalle
sonde: rimossa la dichiarazione ridondante, ora passa.

---

## Note d'ambiente (per chi ripete le prove)

- `tools/run-isolated.sh` sceglie display in 100..299 guardando `/tmp/.X<n>-lock`: dopo molte
  sessioni restavano **200 lock orfani** e il runner rifiutava di partire. Pulizia sicura (verifica
  del PID dentro il lock, `:0` intatto): rimossi 200, tenuti 2 vivi.
- Sul Pinebook **non** esiste Xvfb e non si possono installare pacchetti: il `.deb` `xvfb` per jammy
  arm64 è stato scaricato ed **estratto localmente** (`/tmp/sc-qa.s7VGz4/x/usr/bin/Xvfb`), usato con
  `-displayfd` e chiuso a fine prova. Nessun `apt`, nessun file di sistema toccato.
- Il binario ARM precedente andava in `SIGSEGV` subito dopo `[risorse]`: gli oggetti di entry ARMv7
  in `build/lift-full/entry/` erano stati rigenerati. Ricostruito con
  `tools/build-entry-arm.sh` → `build/lift-full/entry-armv7`, e la build va fatta con
  `ENTRY_DIR=$PWD/build/lift-full/entry-armv7 SYSROOT=$PWD/build/sysroot tools/build-runtime-armv7.sh`.

## Limiti dichiarati

- L'audio è stato verificato solo in modo indiretto (assenza di stallo nel thread di gioco e nessuna
  attesa bloccante nel present). La conferma "a orecchio" del loop spetta all'utente.
- Le prove coprono una missione Terran (Boot Camp/episodio I) su entrambe le architetture; non tutti
  i filmati del gioco.
- `runtime/shim-port/elf/storm_sdraw.c` (non tracciato, preesistente) è una copia storica non usata
  dalla build: lasciata com'è.
