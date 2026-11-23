#!/bin/sh
# shot-stat.sh — verifica NUMERICA di uno screenshot, senza leggere l'immagine nel contesto.
#
# Perche': ogni immagine inviata al modello viene poi reinviata a ogni turno e accumulata nel
# transcript (misurato: 112 blocchi immagine = 46,9 MB in una sola sessione). Per le verifiche
# di "il frame e' nero / c'e' contenuto" bastano le statistiche: qui si stampa luminanza media,
# massimo, percentuale di pixel non neri e checksum di un'area.
#
# Uso:
#   tools/shot-stat.sh file.png [x0 y0 w h]
#   tools/shot-stat.sh --crop-out /tmp/crop.jpg file.png 320 240 200 120   # ritaglio minuscolo
#
# Senza ImageMagick `magick` usa `convert` (Pinebook).
set -u
MAGICK=magick
command -v magick >/dev/null 2>&1 || MAGICK=convert

if [ "${1:-}" = "--crop-out" ]; then
    out=$2; img=$3; x0=$4; y0=$5; w=$6; h=$7
    # ritaglio 320px max, q45: tipicamente 8-15 KB (adeguato per un'occhiata umana)
    $MAGICK "$img" -crop "${w}x${h}+${x0}+${y0}" +repage -resize 320x -quality 45 -strip "$out"
    printf 'crop %s %s byte\n' "$out" "$(wc -c <"$out" | tr -d ' ')"
    exit 0
fi

img=${1:?serve un png}
if [ $# -ge 5 ]; then
    geo="-crop ${5}x${6}+${3}+${4} +repage"
else
    geo=""
fi
# shellcheck disable=SC2086
$MAGICK "$img" $geo -colorspace Gray -format \
  'file=%f mean=%[fx:mean] max=%[fx:maxima] stddev=%[fx:standard_deviation] nonblack=%[fx:mean>0.02]' info:
echo
