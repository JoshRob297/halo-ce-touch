#!/bin/bash
# Las peliculas del disco (bink/*.bik) a los MP4 que el APK lleva en
# assets/cinematics/. El port no tiene decodificador de Bink, asi que el
# aparato reproduce estos (MoviePlayer.java); las peticiones del motor
# (d:\bink\intro_es.bik y compania) las atiende host_movie.c, que elige el
# archivo por el idioma que el motor pide (sufijo _es para espanol, sin
# sufijo para ingles) y cae al que haya cuando el idioma pedido no tiene uno.
#
#   ./transcode_cinematics.sh <carpeta es> [carpeta en] [destino]
#
# Los .bik espanoles salen del ISO europeo (Es,It): /bink/intro_es.bik,
# attract1_es.bik, attract2_es.bik, attract3_es.bik, credits_es.bik.
# Los ingleses salen del ISO americano (USA): /bink/intro.bik y compania.
set -e
src_es="${1:?carpeta con los .bik}"
src_en="${2:-}"
out="${3:-port/android/app/src/main/assets/cinematics}"
mkdir -p "$out"

transcode() { ffmpeg -hide_banner -v error -y -i "$1" -threads 4 \
    -c:v libx264 -preset veryfast -crf 26 -c:a aac -b:a 96k "$out/$2"; }

transcode "$src_es/intro_es.bik"    intro_es.mp4    &
transcode "$src_es/credits_es.bik"  credits_es.mp4  &
transcode "$src_es/Attract1_es.bik" attract1_es.mp4 &
transcode "$src_es/attract2_es.bik" attract2_es.mp4 &
transcode "$src_es/Attract3_es.bik" attract3_es.mp4 &

if [ -n "$src_en" ] && [ -d "$src_en" ]; then
    transcode "$src_en/intro.bik"    intro.mp4    &
    transcode "$src_en/credits.bik"  credits.mp4  &
    transcode "$src_en/attract1.bik" attract1.mp4 &
    transcode "$src_en/attract2.bik" attract2.mp4 &
    transcode "$src_en/attract3.bik" attract3.mp4 &
fi

wait

ls -l "$out"
