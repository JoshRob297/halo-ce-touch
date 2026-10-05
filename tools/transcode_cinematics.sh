#!/bin/bash
# Las peliculas del disco (bink/*_es.bik) a los MP4 que el APK lleva en
# assets/cinematics/. El port no tiene decodificador de Bink, asi que el
# aparato reproduce estos (MoviePlayer.java); las peticiones del motor
# (d:\bink\intro_es.bik y compania) las atiende host_movie.c.
#
#   ./transcode_cinematics.sh <carpeta con los .bik> [destino]
#
# Los .bik salen del ISO europeo (Es,It): /bink/intro_es.bik, attract1_es.bik,
# attract2_es.bik, attract3_es.bik, credits_es.bik.
set -e
src="${1:?carpeta con los .bik}"
out="${2:-port/android/app/src/main/assets/cinematics}"
mkdir -p "$out"

transcode() { ffmpeg -hide_banner -v error -y -i "$src/$1" -threads 4 \
    -c:v libx264 -preset veryfast -crf 26 -c:a aac -b:a 96k "$out/$2"; }

transcode intro_es.bik    intro_es.mp4    &
transcode credits_es.bik  credits_es.mp4  &
transcode Attract1_es.bik attract1_es.mp4 &
transcode attract2_es.bik attract2_es.mp4 &
transcode Attract3_es.bik attract3_es.mp4 &
wait

ls -l "$out"
