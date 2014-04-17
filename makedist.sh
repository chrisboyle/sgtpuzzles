#!/bin/sh 

# Build a Unix source distribution from the Puzzles SVN area.
#
# Pass a numeric argument to have the archive tagged as that SVN
# revision. Otherwise, the script will work it out itself by
# calling `svnversion', or failing that it will not version-tag the
# archive at all.

case "$#" in
  0)
    # Ignore errors; if we can't get a version, we'll have a blank
    # string.
    rev=`svnversion . 2>/dev/null`
    if test "x$rev" = "xexported"; then rev=; fi
    ;;
  *)
    case "$1" in *[!0-9M]*) echo "Malformed revision number '$1'">&2;exit 1;;esac
    rev="$1"
    ;;
esac

if test "x$rev" != "x"; then
  arcsuffix="-r$rev"
  ver="-DREVISION=$rev"
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
  icons/Makefile icons/*.{sav,pl,sh} icons/win16pal.xpm \
  icons/*.png icons/*.ico icons/*.rc icons/*.c \
  configure.ac mkauto.sh aclocal.m4 configure depcomp install-sh missing; do
  case $i in
    */*) ln -s ../../../$i tmp.$$/puzzles$arcsuffix/$i;;
    *)   ln -s ../../$i tmp.$$/puzzles$arcsuffix/$i;;
  esac
  if test "x$ver" != "x"; then
    md5sum $i >> tmp.$$/puzzles$arcsuffix/manifest
  fi
done

if test "x$ver" != "x"; then
  echo "$ver" >> tmp.$$/puzzles$arcsuffix/version.def
fi

tar -C tmp.$$ -chzf - puzzles$arcsuffix > ../puzzles$arcsuffix.tar.gz

rm -rf tmp.$$
