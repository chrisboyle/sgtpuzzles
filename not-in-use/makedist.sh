#!/bin/sh 

# Build a Unix source distribution from the Puzzles SVN area.
#
# Pass a version number argument to have the archive tagged with that
# version number. Otherwise, the script will not version-tag the
# archive at all.

version="$1"

if test "x$version" != "x"; then
  arcsuffix="-$version"
  ver="-DVER=$version"
else
  arcsuffix=
  ver=
fi

perl mkfiles.pl

mkdir tmp.$$
mkdir tmp.$$/puzzles$arcsuffix
mkdir tmp.$$/puzzles$arcsuffix/icons

# Build Windows Help and text versions of the manual for convenience.
halibut --winhelp=puzzles.hlp --text=puzzles.txt puzzles.but

# Build a text version of the HACKING document.
halibut --text=HACKING devel.but

for i in *.c *.m *.h *.R *.rc *.but *.plist *.icns LICENCE README Recipe \
  *.rc2 mkfiles.pl Makefile Makefile.* \
  HACKING puzzles.txt puzzles.hlp puzzles.cnt puzzles.chm \
  icons/Makefile icons/*.sav icons/*.pl icons/*.sh icons/win16pal.xpm \
  icons/*.png icons/*.ico icons/*.rc icons/*.c \
  configure.ac mkauto.sh aclocal.m4 \
  configure depcomp install-sh missing compile; do
  case $i in
    */*) ln -s ../../../$i tmp.$$/puzzles$arcsuffix/$i;;
    *)   ln -s ../../$i tmp.$$/puzzles$arcsuffix/$i;;
  esac
done

tar -C tmp.$$ -chzf - puzzles$arcsuffix > ../puzzles$arcsuffix.tar.gz

rm -rf tmp.$$
