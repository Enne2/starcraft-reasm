# Report — presenter OpenGL hardware affidabile (`opt-gl`)

## Esito
Aggiornato il blocco GL/present di `runtime/shim-port/elf/host_elf.c`. Il percorso GL ora registra i fallimenti SDL/context, i simboli GL mancanti, i log driver di compile/link shader e i mismatch `SC_GL_VERIFY`, con prefisso `[host] GL`. Tenta in sequenza GLES 2.0 → desktop GL 2.1 → GLES 3.0 → desktop GL 3.2 core; ogni tentativo configura il profilo prima di creare il contesto. Il fallback esistente GL → renderer SDL/texture → CPU resta operativo. Il filtro dei renderer software in auto-select è conservato.

## Verifiche

- **Compilazione C11:** PASS i686 e x86-64 (`-Wall -Wextra`). Un warning invariato su entrambi: `g_uitrace_present` inutilizzata.
- **Link privato ELF32:** PASS, prodotto in `build/qa/opt-gl/starcraft-x86`; non è stato sovrascritto `build/starcraft-x86`. L'oggetto di shim condiviso è stato ripristinato dopo il link.
- **Pixel GL:** PASS, `SC_GL_VERIFY PASS: 307200 pixel esatti`, log finale `build/qa/opt-gl/gl-verify-final.log`. Profilo effettivamente selezionato: GLES 2.0; driver Mesa 26.2.6, renderer hardware AMD Radeon 780M/radeonsi, finestra 640×480. Questo verifica il readback RGBA per ogni pixel rispetto al canvas indicizzato e alla palette.
- **Compositor:** test eseguiti su `WAYLAND_DISPLAY=wl-nested`, SDL Wayland, compositor KWin headless GPU-backed. Nessuna finestra sul desktop reale e nessun input simulato.
- **A/B menu, due run per modalità:** processo misurato sommando i tick CPU di `/proc/<pid>/task/*/stat`; PID individuato leggendo `/proc/*/comm == starcraft-x86`. Campioni CPU di 8 s, FPS dai log. FPS per run: mediana dei campioni stabili (esclusi avvio e intervallo terminale parziale); poi mediana dei due run.

| Modalità | CPU run 1/2; mediana (% di un core) | FPS mediana run 1/2; mediana |
|---|---:|---:|
| `SC_PRESENT=gl` | 76,7 / 77,9; **77,28%** | 134 / 133,5; **133,75** |
| `SC_PRESENT=gpu` | 77,3 / 77,6; **77,43%** | 130,5 / 133; **131,75** |
| `SC_PRESENT=cpu` | 78,6 / 78,6; **78,57%** | 130,5 / 130,5; **130,5** |

Log A/B finali: `build/qa/opt-gl/ab-final-{gl,gpu,cpu}-{1,2}.log`. Sono prove brevi sullo stesso avvio/menu, non una partita Boot Camp; il carico comune del guest domina questi risultati, quindi non dimostrano un vantaggio prestazionale significativo del presenter.

- `git diff --check`: PASS.
- La verifica sul binario standard preesistente con `tests/x86_gl_palette_test/check.sh` è passata anch'essa, ma non è usata come prova della patch; la prova attribuibile a questa modifica è quella sul link privato sopra.

## Limiti

- Sul driver hardware disponibile GLES 2.0 ha avuto successo al primo tentativo. I fallback di profilo e i messaggi di errore per profili rifiutati non sono stati attivati a runtime; la sessione Wayland reale ad alta risoluzione che originava il difetto non era disponibile per una riproduzione controllata.
- Boot Camp e screenshot visivo A/B non verificati: non ho iniettato input né pilotato il gioco. `SC_GL_VERIFY` fornisce la verifica numerica esatta sul percorso GL, ma non prova l'equivalenza visiva dell'intera finestra (barre/viewport incluse) con il presenter GPU.
- Nessuna regressione osservata nel percorso GLES2 hardware o nel fallback legacy; il percorso GPU/CPU è stato incluso nell'A/B sullo stesso build privato.

## Decisioni autonome

Ho mantenuto il fallback conservativo in caso di errore GL e non ho simulato input per arrivare a Boot Camp, in coerenza con il vincolo di non iniettare input. Ho limitato le modifiche sorgente al blocco GL/present e lasciato intatti i cambiamenti concorrenti nelle altre sezioni di `host_elf.c` e negli altri file.
