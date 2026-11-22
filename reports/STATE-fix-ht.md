# STATE-fix-ht

- Verifica dinamica completata; causa radice e fix non ancora dimostrati.
- Binario QA: `build/qa/fix-ht/starcraft-x86-ht`; sorgente trace: `build/qa/fix-ht/src/storm_sdraw_arm.c`; trace filtrata `SC_HT_TRACE=1`, runner `fix-ht` fermato.
- Hover centro: `src=0A`, `dst_before=2A`, LUT `0A`, `dst_after=0A`; `0x140×0x60`, `src=20607AA4`, `dst=205A0010`, base `20AE0010`; 1 zero LUT/65536, `LUT[012A]=01`.
- Registry centro: `src=FE`, `dst_before=43`, LUT `00`, `dst_after=00`; `0x16C×0x160`, `src=20520010`, `dst=205A0010`, base `20AA0010`; 257 zeri LUT/65536, `LUT[012A]=01`.
- Global palette buffer IDA `unk_6D05B0` letto tutto zero (1024/1024). Il confronto con input palette `unk_5994E0` non è stato acquisito: non dichiarare root cause.
- IDA: `sub_4DD910` @ `0x4DD910` resetta builder; `sub_4DD9E0` @ `0x4DD9E0` cambia palette e richiama `sub_4178B0`; `sub_4C99C0` @ `0x4C99C0` inizializza con `unk_5994E0`; funzioni di generazione `sub_4178B0`/`sub_4174A0`.
- Screenshot letti: `build/qa/fix-ht/{hover-150ms,hover-400ms,hover-1000ms,gametype,after-original}.png`.
- `build/qa/fix-ht/fix.patch` resta vuoto intenzionalmente. Nessun cambiamento a `runtime/`, `tools/`, `build/obj-x86/`, `build/starcraft-x86`.

## Prossimo passo
In sessione autorizzata: tracciare da copia QA la palette sorgente a `0x005994E0` appena prima di `sub_4178B0` @ `0x4DDB3A`, confrontarla byte-per-byte con `unk_6D05B0` post-copia e con i dati attesi da `BackGnd.pcx`/`tEffect.pcx`; solo dopo attribuire il lookup Registry a LUT errata o input errato, quindi patch QA e riverifica visiva.