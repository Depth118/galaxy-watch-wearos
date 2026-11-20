#!/bin/sh
# Charging-screen and splash assets (ImageMagick 7), from the watch's own files:
#   mkassets.sh <param.bin from the combination firmware> <bootanimation.zip from
#   the Wear OS system image> <out dir>
# Out: splash.raw (360x360 XRGB: first frame of the Wear OS boot animation, the
# white Pixel "G", -> /oem/r800dev/splash.raw, drawn by initramfs/init.c) and
# charger/{d0..d9,pct,bolt}.rgb (-> /oem/r800dev/charger/, used by r800charger).
set -e
P=$1; Z=$2; O=$3
M=${MAGICK:-magick}
T=$(mktemp -d)
mkdir -p "$O/charger"
tar --force-local -C "$T" -xf "$P" $(for i in 0 1 2 3 4 5 6 7 8 9; do echo pow_digital_num_$i.raw; done)
for i in 0 1 2 3 4 5 6 7 8 9; do cp "$T/pow_digital_num_$i.raw" "$O/charger/d$i.rgb"; done  # 55x110 RGB
unzip -o -q "$Z" part1/00000.png -d "$T"
"$M" "$T/part1/00000.png" -background black -gravity center -extent 360x360 -alpha remove -alpha opaque -depth 8 BGRA:"$O/splash.raw"
"$M" -background black -fill white -font Segoe-UI-Light -pointsize 44 label:% -trim +repage \
    -gravity south -background black -extent 36x96 -gravity north -extent 36x110 -depth 8 rgb:"$O/charger/pct.rgb"
"$M" -size 320x240 xc:black -fill white -stroke none \
    -draw "polygon 194,18 104,130 152,130 128,222 216,106 166,106 194,18" -resize 80x60 -depth 8 rgb:"$O/charger/bolt.rgb"
rm -rf "$T"
