# SOL-fix-ht — verifica dinamica (stato aperto)

## Esito dinamico
Binario QA ELF32 `build/qa/fix-ht/starcraft-x86-ht`, lanciato solo con `run-isolated.sh` su Xvfb/audio dummy. Trace `SC_HT_TRACE=1`, limitata ai rettangoli `0x140×0x60` e `0x16C×0x160`; screenshot acquisiti e letti in `build/qa/fix-ht/` (`hover-150ms.png`, `hover-400ms.png`, `hover-1000ms.png`, `gametype.png`, `after-original.png`). Runner `fix-ht` fermato.

- **Hover, centro (160,48)**: `ret=0x080AFF90`, `w=320 rows=96`, `src=0x20607AA4`, `dst=0x205A0010`, LUT `0x20AE0010`. Campione `src=0x0A`, `dst_before=0x2A`; `LUT[(0x0A<<8)|0x2A]=0x0A`; `dst_after=0x0A`. Ripetizioni osservate hanno anche `src=0xC6`, con `dst_before=0x2A`, LUT/result `0xC6`. LUT: 1 byte zero su 65536; `LUT[0x012A]=0x01` (test richiesto `(1,0x2A)`). Il byte scritto dall’interprete coincide col lookup; non prova che il rettangolo sorgente/destinazione sia quello semanticamente atteso.
- **Registry, centro (182,176)**: `ret=0x080B0C20` (return originale `0x417129`), `w=364 rows=352`, `src=0x20520010`, `dst=0x205A0010`, LUT `0x20AA0010` in questa run. Campione `src=0xFE`, `dst_before=0x43`; lookup `LUT[(0xFE<<8)|0x43]=0x00`; `dst_after=0x00`. LUT: 257 byte zero su 65536; `LUT[0x012A]=0x01`. Il valore scritto coincide con la LUT, quindi il campione non indica divergenza dell’interprete SCode. Gli indirizzi heap della LUT variano tra run (`0x20AA`/`0x20AE`); sono registrati per run, non presunti costanti.
- `0x006D05B0` (global IDA `unk_6D05B0`, buffer 1024 byte usato dal builder LUT) risultava tutto zero nei campioni tracciati (`pzero=1024`, entry 0, `0x43` e `0xFE` zero). Questo è un indizio, non una prova che sia la palette attiva del pannello: in questa passata non è stato acquisito il contenuto runtime della sorgente `0x005994E0` né i suoi 1024 byte confrontati col buffer.

## Oracolo IDA e limite della conclusione
- `sub_4174A0` @ `0x4174A0` genera la tabella 64 KiB usando la tabella palette da 1024 byte e il parametro modalità/intensità; `sub_4178B0` @ `0x4178B0` copia palette in `unk_6D05B0` e rigenera le mappe.
- `sub_4DD910` @ `0x4DD910` chiama `sub_417C20(nullptr,nullptr)` (`0x4DD925`), che azzera `unk_6D05B0` (`0x417C31`). `sub_4DD9E0` @ `0x4DD9E0` al cambio palette copia il buffer alternato `unk_5994E0`/`unk_51C008` e chiama `sub_4178B0` (`0x4DDB3A`). `sub_4C99C0` @ `0x4C99C0` inizializza l’effetto con `sub_417C20(&v6,&unk_5994E0)` (`0x4C9A51`).
- La trace mostra che Registry riceve un valore zero dal lookup e lo scrive correttamente, ma non determina se quel risultato sia errato senza confrontare la palette sorgente attesa e quella fornita. Hover sample non scrive nero: screenshot a 150 ms mostra soprattutto l’animazione che copre il testo, non dimostra un difetto LUT.
- Nessun fix funzionale è stato applicato; `build/qa/fix-ht/fix.patch` resta vuoto intenzionalmente. Non sono state modificate aree vietate.

## Prossimo passo
In una nuova sessione autorizzata, strumentare solo la copia QA per campionare `0x005994E0` prima di `sub_4178B0` @ `0x4DDB3A`, contare byte zero e confrontare i 1024 byte con `0x006D05B0` subito dopo la copia e con i dati attesi della risorsa palette `BackGnd.pcx`/`tEffect.pcx`. Correlare il singolo sample Registry `(src=0xFE,dst=0x43)` al lookup nel binario originale. Solo se la prima divergenza è nella palette/LUT, correggere la copia QA, ricompilare, ripetere screenshot; non alterare SCode o API palette sulla base del solo pixel campionato.

## Decisioni autonome
Interrompo senza fix speculativo: il renderer applica esattamente il valore LUT per i campioni osservati; la causa a monte (input/palette o semantica attesa) non è ancora provata.
