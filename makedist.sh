#!/bin/sh 

perl mkfiles.pl

mkdir tmp.$$
mkdir tmp.$$/puzzles

# Build Windows Help and text versions of the manual for convenience.
halibut --winhelp=puzzles.hlp --text=puzzles.txt puzzles.but

# Build a text version of the HACKING document.
halibut --text=HACKING HACKING.but

for i in *.c *.m *.h *.but *.plist *.icns LICENCE README Recipe \
  mkfiles.pl Makefile Makefile.* \
  HACKING puzzles.txt puzzles.hlp puzzles.cnt; do
  ln -s ../../$i tmp.$$/puzzles
done

tar -C tmp.$$ -chzf - puzzles > ../puzzles.tar.gz

rm -rf tmp.$$
