# Pinebook Pro — harness di test, salto filmati, e blocco `standard.snp`

Data: 2026-11-22 · Target: `enne2@10.0.1.126` (Pinebook Pro, aarch64 + multiarch armhf) · binario `build/starcraft-armv7`

## 1. Harness CLI sul Pinebook (fatto)
`~/pbst/pbst` (`pbst` in `build/qa/pbp-harness/`):
```
pbst setup                 # dir di gioco con symlink ad asset/snp/dll + characters/ e Save/
pbst start|stop|status|log [N]
pbst shot [nome]           # gnome-screenshot -> ~/pbst/shots/<nome>.png
pbst click X Y [btn] | move X Y | down/up X Y | key <nome|num> | text <str> | wait <s> | cmdline <riga>
```
L'input non passa da tool X: usa **`SC_CONTROL_FILE`** del runtime (eventi immessi nella coda del gioco,
nessun focus richiesto). Verificato: click di menu, `text` in un campo edit, `key 13` per confermare.
Screenshot: `gnome-screenshot` (unico metodo che funziona su quel desktop; `import`/`xwd -root` falliscono
per il grab di Xwayland). Vista remota: pagina web auto-aggiornante servita da frigate su `:8099`
(`build/qa/pbp-live/`, frame presi ogni ~3 s).

## 2. Salto filmati: la variabile è `SC_SKIP_INTRO=1` (default nel harness)
- `SC_SMACK=0` **non è utilizzabile**: disattiva tutte le animazioni e il gioco esce con
  `Could not load "glue\mainmenu\Exit.smk"` (i .smk del menu sono obbligatori).
- `SC_SKIP_INTRO=1` salta solo l'introduzione: il gioco arriva al menu in ~8 s. È il default del harness.

## 3. File che mancavano nel deploy (risolti)
| Sintomo | Causa | Stato |
|---|---|---|
| "insufficient disk space" creando il profilo | mancavano `characters/` e `Save/` (CreateFile falliva) | **risolto** (dir create) |
| "Error initializing standard.snp" al primo tentativo | mancava `standard.snp`/`battle.snp` nel deploy | file copiati (hash = sandbox x86) |
| "Please insert your StarCraft CD" | mancava `Starcraft.mpq` | symlink aggiunto |
Profilo: **creazione verificata** → `characters/pbp.spc` (e `pbptest.spc`) scritti su disco dall'app ARM.

## 4. Blocco attuale: inizializzazione del provider di rete alla campagna
Cliccando **Terran Episode I** compare `Error initializing standard.snp. Please reinstall StarCraft.`

Evidenza (traccia `REASM_TRACE=1 SC_SNP_TRACE=1 SC_LIB_TRACE=1`):
- su **x86** la sequenza è: `[snp] caps.dat del provider servito in memoria` →
  `[risorse] caricato come FILE … standard.snp -> handle 0x71000003` → `SnpQuery 0/1` → `SnpBind` → `spiInitialize`;
- su **ARM** quelle righe **non compaiono**: nessun `CreateFileA`/`LoadLibraryA` con `standard.snp`
  (`grep -c snp game.log` = 0, nessun `[file-open]` con `.snp`, nessun `[lib] LoadLibraryA[snp]`).

Quindi sul target il gioco **non apre nemmeno il provider**: si ferma prima, e mostra il messaggio generico.
Le due tabelle che servono sono corrette in entrambi i build (`bridge_table.c`/`bridge_symbols.c`
contengono `SnpQuery`/`SnpBind`; `storm_ordinals` identici), quindi il problema non è il wiring statico.

## 5. Prossimo passo (traccia differenziale dei bridge)
Aggiungere una traccia opzionale (`SC_BRIDGE_LOG=1`) che stampi **nome e tipo** di ogni import instradato da
`arm_bridge_common` (ARM) e dal corrispondente path x86, poi confrontare la sequenza di chiamate attorno
al click su "Terran": la chiamata presente su x86 e assente su ARM individua il punto esatto in cui il
percorso di init del provider divergе.
