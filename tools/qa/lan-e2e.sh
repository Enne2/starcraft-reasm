#!/bin/bash
# Flusso LAN completo con verifiche di stato a ogni passo. Uso:
#   lan-e2e.sh <sandbox-a> <sandbox-b> <nome-a> <nome-b> [attesa-dopo-start]
# Prerequisito: le due istanze gia' avviate (run-isolated start) con i loro IP/porte.
set -u
R=/home/enne2/dev/starcraft-reasm-generic
A=$1; B=$2; NA=$3; NB=$4; WAIT=${5:-12}
SA=$A/sc-ctl.sock; SB=$B/sc-ctl.sock
LA=$(dirname "$A")/game.log; LB=$(dirname "$B")/game.log
ctl(){ python3 $R/tools/scctl --sock "$1" "${@:2}" >/dev/null; }
press(){ local S=$1 x=$2 y=$3 d=${4:-1.2}; ctl "$S" move $x $y; sleep 0.3; ctl "$S" down $x $y; sleep 0.25; ctl "$S" up $x $y; sleep "$d"; }
mean(){ ctl "$1" shot /tmp/lan-e2e.png; magick /tmp/lan-e2e.png -colorspace Gray -format '%[fx:mean]' info:; }

echo "[1] ingresso in LAN"
python3 $R/tools/qa/lan-enter.py "$SA" "$LA" "$NA" | tail -1
python3 $R/tools/qa/lan-enter.py "$SB" "$LB" "$NB" | tail -1

echo "[2] host crea la partita"
press "$SA" 125 342 2.5
press "$SA" 518 391 4
if grep -qa "announce tx" "$LA"; then echo "    host: annuncio inviato"; else echo "    host: NESSUN annuncio"; fi

echo "[3] client entra"
press "$SB" 120 60 1.0
press "$SB" 518 391 5
grep -qa "spiGetGameInfo hit" "$LB" && echo "    client: getGameInfo OK" || echo "    client: getGameInfo assente"

echo "[4] host avvia"
press "$SA" 518 391 "$WAIT"

echo "[5] stato finale"
printf "    host mean=%s  client mean=%s\n" "$(mean "$SA")" "$(mean "$SB")"
printf "    host fps=%s client fps=%s\n" "$(grep -ac '\[fps\]' "$LA")" "$(grep -ac '\[fps\]' "$LB")"
for f in "$LA" "$LB"; do
  printf "    %s: recv=%s send=%s\n" "$(basename "$(dirname "$f")")" \
     "$(grep -acE 'recv len=' "$f")" "$(grep -acE 'send len=' "$f")"
done
