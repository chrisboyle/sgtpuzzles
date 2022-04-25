#!/bin/sh
for mode in day night; do
  for game in blackbox bridges cube dominosa fifteen filling flip flood galaxies guess inertia keen lightup loopy magnets map mines mosaic net netslide palisade pattern pearl pegs range rect samegame signpost singles sixteen slant solo tents towers tracks twiddle undead unequal unruly untangle; do
    in=../../../../../fastlane/metadata/android/en-US/images/phoneScreenshots/icon_${mode}_${game}_*.png
    out=../../../../../app/src/main/res
    perl square.pl /usr/bin/convert 256 5 $in $out/drawable-xxxhdpi/${mode}_${game}.png &
    perl square.pl /usr/bin/convert 192 4 $in $out/drawable-xxhdpi/${mode}_${game}.png &
    perl square.pl /usr/bin/convert 128 4 $in $out/drawable-xhdpi/${mode}_${game}.png &
    perl square.pl /usr/bin/convert 96 3 $in $out/drawable-hdpi/${mode}_${game}.png &
    perl square.pl /usr/bin/convert 64 3 $in $out/drawable-mdpi/${mode}_${game}.png &
  done
done
