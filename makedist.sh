#!/bin/sh 

perl mkfiles.pl

mkdir tmp.$$
mkdir tmp.$$/puzzles

# Build Windows Help and text versions of the manual for convenience.
halibut --winhelp=puzzles.hlp --text=puzzles.txt puzzles.but

for i in *.c *.h *.but LICENCE README Recipe mkfiles.pl Makefile.* \
  puzzles.txt puzzles.hlp puzzles.cnt; do
  ln -s ../../$i tmp.$$/puzzles
done

tar -C tmp.$$ -chzf - puzzles > ../puzzles.tar.gz

rm -rf tmp.$$
