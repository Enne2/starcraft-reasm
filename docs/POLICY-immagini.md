# Policy immagini — verifica schermo senza gonfiare il contesto

## Il problema misurato (2026-11-23, sessione su questo repo)

| metrica | valore |
|---|---|
| blocchi immagine nel transcript della sessione | **112** |
| byte di base64 solo per le immagini | **46,9 MB** |
| peso del file di sessione `.jsonl` | **62,6 MB** (4375 righe; testo: 2,7 MB) |

Causa: ogni immagine letta con `read` (o allegata) diventa un blocco immagine nella history. La
history viene **reinviata a ogni turno**, quindi il costo si paga N volte e cresce in modo
cumulativo: una singola sessione di debug schermo arriva a decine di MB e alla soglia di contesto
(816k token su 1.048.576 = 77,9% in questo caso, con un solo argomento tecnico in corso).

Nota: `agy_context_prune` **non** può rimuovere i blocchi immagine — il pianificatore (sia `class`
sia `curator`) pota solo testo e `thinking`. Una volta inserite, le immagini restano.

## Regola

1. **Mai `read` su un PNG/JPEG di screenshot.** Le verifiche "il frame è nero / c'è contenuto /
   il dialog è comparso" si fanno con numeri.
2. Strumento unico: `tools/shot-stat.sh` (usa `magick`, o `convert` su macchine senza ImageMagick):

   ```sh
   tools/shot-stat.sh /tmp/frame.png                 # file=… mean=… max=… stddev=… nonblack=…
   tools/shot-stat.sh --crop-out /tmp/c.jpg /tmp/frame.png 320 240 200 120   # ritaglio ≤320px, ≤15 KB
   ```

   Interpretazione: `mean ≈ 0` e `max = 0` → schermo realmente nero; `mean ≥ 0.02` → contenuto
   visibile; `stddev` alto → immagini strutturate (menu, HUD) e non un fondo uniforme.
3. Se serve **guardare** un'immagine (colori, testo, layout):
   - prima preferisci `show_image mode=display` — la mostra nel terminale dell'utente **senza**
     inviarla al modello;
   - se serve davvero al modello, allora `shot-stat.sh --crop-out` (ritaglio ≤320 px, JPEG q45,
     ≤15 KB) e **una sola immagine per volta**.
4. Su macchine remote: **non trasferire il PNG**. Calcolare le statistiche dove il file è già
   (`ssh … convert f.png -colorspace Gray -format "mean=%[fx:mean] max=%[fx:maxima]" info:`) e
   stampare solo testo.
5. Diagnostica del gioco: preferire le sonde già presenti (`SC_PRESENT_DBG=1`, `REASM_TRACE=1`,
   `[canvas-write]`, `[storm-scode-video]`, `[smack]`) che stampano numeri nei log.

## Perché è la soluzione definitiva

Il contatore che conta non è il disco: è il contesto inviato al modello. Con la regola sopra il
numero di blocchi immagine per sessione passa da decine a **zero** in condizioni normali, e il
debug schermo non consuma contesto in proporzione al numero di catture.
