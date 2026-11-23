#!/bin/bash
set -u
export DISPLAY=":$(head -1 ../fin.display)"
c(){ python3 ../scctl --sock "$PWD/game.sock" "$@" >/dev/null; }
shot(){ c shot "$PWD/$1.png"; printf '%s ' "$1"; convert "$1.png" -colorspace Gray -format '%[fx:mean]\n' info:; }
c click 220 120; sleep 2
c click 262 214; sleep 3
shot a-gametype
c click 75 342; sleep 1; c text armQA; c key enter; sleep 2
find characters -iname 'armqa.spc' -printf 'profile %s %p\n'
c click 530 428; sleep 2
c click 220 120; sleep 2; c click 262 214; sleep 3
c click 100 109; sleep 1; c click 517 391; sleep 4
shot b-campaign
for g in 150x110+235+100 145x105+36+177 140x152+462+114; do convert b-campaign.png -crop "$g" +repage -colorspace Gray -format '%[fx:mean] %[fx:maxima]\n' info:; done
c move 310 155; sleep 1; shot c-hover-terran
c click 318 248; sleep 3; c click 320 440; sleep 2; c click 320 440; sleep 3
c click 538 390; sleep 5; c click 197 262; sleep 4
shot d-mission
c click 439 399; sleep 2; c click 313 81; sleep 2
c click 320 84; sleep 1; c text armsave; sleep 1; c click 180 261; sleep 3
find Save -iname '*.sng' -printf 'save %s %p\n'
shot e-after-save
