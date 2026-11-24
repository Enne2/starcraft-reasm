#!/bin/bash
# Flusso LAN riproducibile: host crea, client entra, host avvia. Uso:
#   lan-flow.sh <sandbox-a> <sandbox-b> [attesa-dopo-start]
set -u
A=$1/sc-ctl.sock; B=$2/sc-ctl.sock; WAIT=${3:-20}
press(){ local S=$1 x=$2 y=$3 d=${4:-1.2}; python3 /home/enne2/dev/starcraft-reasm-generic/tools/scctl --sock "$S" move $x $y >/dev/null
         sleep 0.3; python3 /home/enne2/dev/starcraft-reasm-generic/tools/scctl --sock "$S" down $x $y >/dev/null
         sleep 0.25; python3 /home/enne2/dev/starcraft-reasm-generic/tools/scctl --sock "$S" up $x $y >/dev/null; sleep "$d"; }
for S in "$A" "$B"; do
  press "$S" 140 200 2.2; press "$S" 250 210 2.8; press "$S" 150 102 0.9; press "$S" 518 391 3.5
  press "$S" 120 96 1.0; python3 /home/enne2/dev/starcraft-reasm-generic/tools/scctl --sock "$S" key enter >/dev/null; sleep 3.5
done
press "$A" 125 342 2.5; press "$A" 518 391 4; sleep 1
press "$B" 120 60 1.0; press "$B" 518 391 5
press "$A" 518 391 "$WAIT"
