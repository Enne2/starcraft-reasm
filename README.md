# StarCraft ARMv6 — laboratorio di ricompilazione statica

Obiettivo: tradurre `StarCraft.exe` e `storm.dll` per il Raspberry Pi Zero originale (Linux ARMv6, ARM1176JZF-S, VFPv2 hard-float, senza NEON), con SRW → LLASM → LLVM. Non è ancora un port giocabile. I binari originali provengono da una copia locale di `sc-reasm`; non sono sorgenti del progetto.

## Riproduzione

```sh
make check      # build strumenti, avvio freestanding e test PE32 ordinale x86/ARMv6
make inventory  # analisi preliminare di x87/SIMD sugli intervalli funzione esportati da IDA
```

`SC_REASM` può indicare una copia diversa dello stesso progetto x86. Per l'inventario occorrono `capstone` e `pefile`. Per compilare occorrono SCons, un compilatore C/C++, LDC, Clang, `opt` e `llc`. Il test ARM richiede `qemu-arm-static` e il linker GNU ARM: se necessario impostare `ARM_LD` al percorso del linker. Il test freestanding non richiede un sysroot; la futura build SDL2 deve invece usare un sysroot compatibile con Raspberry Pi OS ARMv6. Il test di lifting (`make check-slice`) richiede anche MinGW i686 e Wine (con un prefisso dedicato sotto `build/`).

- `vendor/sr/`: snapshot di M-HT/SR (revisione in `vendor/sr-revision.txt`). `tools/build-tools.sh` costruisce SRW in modalità LLASM in uno snapshot di build e compila `llasm` per la macchina host.
- `tools/lift.sh`: avvia SRW; per i PE senza base relocations è necessario un CSV compatibile nel workspace del lifter. Non sostituire il CSV con un file vuoto per ottenere un falso successo.
- `tools/compile-arm.sh input.ll build/output.o`: produce un oggetto ARM. Imposta il data layout del target prima di ottimizzare. L'uscita `.elf` è per un test autosufficiente con `_start`, non per il gioco completo.
- `tools/inventory.py`: evidenza di triage, non di reachability; gli intervalli possono comprendere dati, e trovare un `case` del mnemonic non prova il supporto di tutti gli operandi o dei flag.
- `work/*/REPORT.md`: evidenze dei singoli filoni. I risultati dei gate devono distinguere il caricamento del PE, l'emissione di IR, l'esecuzione di test sintetici e l'esecuzione di codice originale tradotto.

## Stato del lifting

- La fixture sintetica chiama una DLL per ordinale. Il PE originale della fixture dà il risultato atteso sotto Wine, e il corpo generato da SRW/LLASM dà lo stesso risultato sotto QEMU `-cpu arm1176`; l'import ARM è un bridge specifico della fixture, non Storm ricompilato. Nessuna istruzione del gioco è eseguita in questo test.
- `tools/export-relocs-idapython.py` ha prodotto 31.241 candidati verificati contro i byte di `StarCraft.exe` in `work/relocs/relocations.csv`. L'export è conservativo e non ne dimostra la completezza.
- Con quel CSV il loader SRW riconosce **135/135** import ordinali di Storm nell'EXE originale. Log di verifica: `build/supervisor-exe/loader.log`. Il disassemblaggio si ferma su flag live-in a `0x00407939` e segnala riferimenti interni a istruzioni a `0x00406C50` e `0x00406DDC`. Per la DLL, la prima annotazione sperimentale sposta lo stop da `0x15002BEB` a `0x15005DAA`; non dimostra ancora parità dei flag.

Il prossimo test è una coppia reale di funzioni StarCraft→Storm contro il port x86. Grafica e audio non sono oggetto di questa fase: serviranno i servizi degli shim esistenti, dietro bridge ABI adatti all'ARM.
