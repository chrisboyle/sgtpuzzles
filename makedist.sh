#!/bin/sh 

perl mkfiles.pl

mkdir tmp.$$
mkdir tmp.$$/puzzles

for i in *.c *.h LICENCE README Recipe mkfiles.pl Makefile.*; do
  ln -s ../../$i tmp.$$/puzzles
done

tar -C tmp.$$ -chzf - puzzles > ../puzzles.tar.gz

rm -rf tmp.$$
