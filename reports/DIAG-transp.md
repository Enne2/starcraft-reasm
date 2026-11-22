# Diagnosi Bug 3 — pannelli neri / trasparenza

## Esito

La sessione isolata riproduce il sintomo visivo: in `Select Game Type` (shot `build/isolated/diag-transp/shot-gametype.png`) il pannello copre opacamente lo sfondo; dopo il percorso Original/Registry, `shot-black.png` mostra un'area interna nera nel pannello Registry. L'avanzamento fino alla schermata è reale; non ho raggiunto menu in-game/Save Game. Runner esclusivo `./tools/run-isolated.sh`, display Xvfb privato `:172`, SDL audio dummy; istanza arrestata.

## Risultato del confronto

**Difetto concreto trovato — `runtime/shim-port/elf/com_ddraw.c`:** `s_Blt` (262–269) intercetta `DDBLT_ROP`, chiama `rop_rect(d,...,rop,pat)` e ritorna prima del percorso sorgente (284–295). `rop_rect` (165–186) è implementato come operazione destination-only: per 8 bit `SRCAND`, `SRCPAINT` e `SRCINVERT` usano `pat` al posto del pixel sorgente (`*p &= pat`, `*p |= pat`, `*p ^= pat`); ROP non elencati fanno niente. Questo viola la semantica dei ROP che dipendono da source/destination e può trasformare una composizione/tint in una superficie non composta (incluso nero se l'operando/pattern è 0). **È la correzione primaria da applicare al renderer COM:** valutare il truth-table ROP con source, destination e pattern reali (mantenendo clipping, pitch, color-key/flags); non trattare tutti i ROP come dest-only.

**Limite della diagnosi causale:** il runner non logga flags/ROP di ogni `Blt`; lo screenshot da solo non dimostra che il pannello osservato passi da questo ramo. Quindi il difetto COM è provato nel codice, ma il nesso diretto con quelle due schermate resta da confermare con una trace mirata prima di dichiararlo causa unica.

## Storm SDraw / LUT

Oracolo IDA sul `build/lift-full/storm.orig.dll`: export 313 a `0x15036260` accetta SRCCOPY `0x00CC0020` e PATCOPY `0x00F00021` nel fast path, altrimenti passa il descrittore a #334; export 334 è `0x15030AB0`, patcha entry/loop SCode e salta al kernel a `0x15030D2A` (`storm_313` `0x150362AE–0x150363BB`; `storm_334` `0x15030AD4–0x15030D2A`). Lo SCode è runtime-generated: IDA sul PE statico non dà la semantica completa di ogni LUT.

In `runtime/common/storm_sdraw_arm.c`, #313 (`161–168`) gestisce esplicitamente solo quei due ROP; gli altri falliscono chiusi. #334 (`403–490`) patcha/dispatcha i kernel e `run_scode` interpreta i load separati da `src` (`ESI`) e `dst` (`EDI`), l'accesso `[eax+ebp]` a LUT e store, con access-check al punto d'uso (`249–254`, `292–330`). L'ordine `src/dst` dei byte LUT osservabili è ricavato dal bytecode effettivamente catturato, non va invertito per supposizione. Nessun errore puntuale d'indice/orientamento LUT è dimostrato qui; il fast path C LUT è solo per fingerprint 2x (`364–401`, `478–484`), non una soluzione generale per tutti i blend.

`DDBLT_KEYSRC` invece è instradato al blit keyed in `s_Blt` (`290–294`); `host_elf_blit8` salta i pixel sorgente uguali alla chiave (`host_elf.c:501–560`). `DDBLT_COLORFILL` tratta il valore come indice di palette (`com_ddraw.c:271–282`), non è alpha blending.

## GDI / Dialoghi

`gdi_elf.c:1173–1180` implementa `FillRect` come riempimento opaco; stock brush 1 è grigio `0xC0C0C0` (`617–620`), stock brush 4 è nero (`0x000000`). `dlg_elf.c:403` riempie la finestra dialogo intera con brush 1, poi bordi/controlli (`403–412`). Queste righe producono dialoghi nativi opachi per progetto, ma il brush 1 non è nero e non spiega il pannello Registry custom osservato; non attribuisco loro il bug generale.

## Prossima verifica mirata

Aggiungere (dopo autorizzazione) log temporaneo in `s_Blt` di flags, ROP, pattern, key e indici source/destination su rettangolo campione; correlare il frame nero. Per il ramo #334 registrare SCode/descriptor, LUT base e i due byte che formano l'indice per lo stesso pixel, poi confrontare il risultato pixel-by-pixel con esecuzione originale. Applicare prima il ROP completo solo se la trace dimostra che la schermata passa per `DDBLT_ROP`.

## Decisioni autonome

Ho scelto di non modificare runtime né build e di non inventare l'ordine della LUT: l'evidenza disponibile individua un difetto semantico certo in `s_Blt`, ma non prova ancora che sia il chiamante delle schermate mostrate. Ho chiuso l'istanza isolata dopo gli screenshot.
