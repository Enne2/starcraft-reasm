# Reloc overlap diagnostic — 2026-11-19

## Evidenza IDA privata

Analisi sulla copia `build/relocs/StarCraft.exe`, sessione IDA privata `6c3becb1`; imagebase `0x400000`. `ida_func` colloca entrambi gli indirizzi nella funzione `_memcpy` a `0x406BF0`. Ho letto la decompilazione completa: funzione ottimizzata con percorso di copia avanti/indietro, dispatch a tabelle e `rep movsd`.

- `0x406C3D` è `jmp dword ptr [eax*4+0x406C50]`, byte `FF 24 85 50 6C 40 00`; l’operand offset a `0x406C40` contiene dword `0x00406C50`. Xref IDA dall’istruzione al target `0x406C50`.
- `0x406DD1` è `jmp dword ptr [eax*4+0x406DDC]`, byte `FF 24 85 DC 6D 40 00`; l’operand offset a `0x406DD4` contiene dword `0x00406DDC`. Xref IDA dall’istruzione al target `0x406DDC`.
- Entrambi gli indirizzi di destinazione coincidono con il byte +4, cioè il campo disp32, di altre istruzioni sovrapposte: istruzione a `0x406C4C` (`FF 24 8D D0 6C 40 00`) e istruzione a `0x406DD8` (`FF 24 8D D8 6E 40 00`). È evidenza di flussi/decodifiche sovrapposti nel codice, non un semplice puntatore a istruzione isolato. Gli indirizzi specifici e i byte sono confermati nel DB privato.

## Export e test

Nessuna modifica a `tools/export-relocs-idapython.py`: i fixup `0x406C40 → 0x406C50` e `0x406DD4 → 0x406DDC` sono coerenti con gli operand offset e i dword originali; rimuoverli o reinterpretarli come non-relocation non sarebbe giustificato. Due tentativi locali con `ignored_areas.sci` e `fixup_do_not_interpret_as_code.sci` non sono stati mantenuti: con quelle indicazioni SRW continuava a segnalare entrambi i casi. Non è stato introdotto alcun bypass dei flag.

Aggiunto `work/relocs/test_reloc_overlap.py`:
1. controlla i due record CSV, i byte macchina dell’istruzione di origine, il dword originale e le istruzioni sovrapposte;
2. esegue SRW dalla directory di lavoro corretta e richiede che l’import Storm sia `135/135`, che non ricompaia `reading inside of instruction` e che l’esecuzione raggiunga un errore di flag successivo.

Comando e risultato: `python3 -m unittest work/relocs/test_reloc_overlap.py -v` — **2 test PASS**. Il run integrato terminato durante il test ha superato l’import ordinal (`storm.dll resolved=135/135`) e non ha riprodotto i due errori di lettura-in-istruzione.

## Nuovo gate SRW

Primo stop osservato nel run integrato: `Error: unknown flags - 0 - 1010539 (0x4f7b6b) - fclex` (exit 5). Quindi i due indirizzi segnalati nel log non bloccano il run corrente; il gate successivo è la gestione dei flag dell’istruzione `fclex` a VA `0x4F7B6B`. Non ho aggiunto SCI perché l’analisi non giustifica di mascherare o bypassare tale flag.

Nota: nel test IDA la label della funzione è stata temporaneamente rinominata in memoria e immediatamente ripristinata a `_memcpy`; il DB privato non è stato salvato. Nessuna sessione IDA concorrente è stata toccata. Nessuna modifica a SRW common, build code, binari originali o sorgenti proprietari altrui.
