# SOL-b3-transp — diagnosi ripresa, fix ancora aperto

## Esito verificato
- Build QA isolata corretta e avviabile: `build/qa/b3-transp/starcraft-x86-b3` (226,494,352 B, ELF32 i386). Link con entry object assoluti, `build/obj-x86/runtime/guest_data_link.ld` + `runtime_bounds.ld`, `.text=0x18000000`, `-z muldefs`; linker script usa path assoluti per selezionare gli input `.text` guest.
- Root del crash precedente: l'invocazione manuale passava gli entry object con path relativi, quindi i selettori nel linker script non li associavano: `.guest_exe_code` e `.guest_storm_code` erano vuoti, il codice finiva nel `.text` host, output 278,409,000 B e startup SIGSEGV (`pc=0x188719C4`, fault `0x1505FC20`). Con entry path assoluti le sezioni hanno size `0x68F1CF` e `0x1DEFCA` alle VA `0x08000000`/`0x10000000`; il gioco si avvia.
- Run `./tools/run-isolated.sh` ID `b3-transp`, Xvfb privato, SDL dummy. Screenshot letti: `build/qa/b3-transp/screenshots/gametype-before.png`, `registry-before.png`; il pannello nero è riprodotto in entrambe.

## Call-site ristretto
- Nessuna chiamata strumentata a `s_Blt`, `s_BltFast`, `FillRect` o `BitBlt` osservata in quelle schermate. Il rendering passa invece ripetutamente da Storm SDraw: conteggi nel log 2037 `storm_313`, 765 `storm_334` (sull'intervallo catturato).
- Call candidata Registry in log `[b3]`: Storm #334, `size1=0x4A`, `size2=0x2BC5`, `flags=0x40`, `width=0x16C`, `rows=0x160`, `dst=0x205A0010`, `src=0x20520010`, `base=0x20AE0010`, `dst_delta=0x114`, `src_delta=0`. Return guest `0x080AFF90` mappato al return originale `0x416F1F`.
- IDA su copie in `build/qa/b3-transp/oracle/`: `sub_416ED0` imposta il descriptor e chiama lo thunk `storm_334` @ `0x416F1A`; `sub_416F30` gestisce la trasformazione palette/tabella e chiama `sub_416ED0` o `storm_334` @ `0x417124` (return `0x417129`). Storm originale #334 `0x15030AB0` patcha/dispatcha il kernel; #313 `0x15036260` usa #334 per ROP fuori fast-path.
- Evidenza compatibile con SCode/LUT, ma non dimostra ancora che la chiamata `0x16C×0x160` sia proprio il fill nero né quale opcode/lookup sia errato. Nessun dump SCode del call-site selezionato acquisito; nessuna correzione né screenshot fixed verificato. Codice ROP3 generale è già presente e i suoi test passano, ma non spiega queste schermate osservate.

## Consegnabili e prossimi passi
- Log: `build/isolated/b3-transp/game.log`; sorgenti e oggetti strumentati: `build/qa/b3-transp/src/`, `rt/`, `sh/`. Nessuna modifica a `runtime/`, `tools/`, `build/obj-x86/` o `build/starcraft-x86`.
- `build/qa/b3-transp/fix.patch` resta vuoto: non applico una correzione speculativa.
- Runner `b3-transp` fermato. MENU/Save Game in-game non verificati.

## Prossimo passo
Aggiungere al solo SCode privato un dump one-shot pre-patch filtrato su `width=0x16C`, `rows=0x160`, `size1=0x4A`, `size2=0x2BC5`; riavviare con `SC_SCODE_DUMP=1`, catturare bytecode + descriptor/caller `0x416F1F`, disassemblarlo opcode per opcode e confrontare con l'implementazione originale Storm (IDA `0x15030AB0` e generatori/chiamanti). Correlare un singolo output/pixel del rettangolo nero; solo dopo applicare il fix privato e ripetere gli screenshot.

## Decisioni autonome
Conservo lo stato senza patch funzionale: l'oracolo dimostra il percorso Storm/SCode candidato ma non ancora l'opcode responsabile. Evito modifiche al renderer non sostenute dal dump/confronto.
