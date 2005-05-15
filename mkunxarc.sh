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
    ;;
  *)
    case "$1" in *[!0-9]*) echo "Malformed revision number '$1'">&2;exit 1;;esac
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
make -s -f Makefile.doc

arcname="puzzles$arcsuffix"
mkdir uxarc
mkdir uxarc/$arcname
find . -name uxarc -prune -o \
       -name CVS -prune -o \
       -name '*.app' -prune -o \
       -name '.[^.]*' -prune -o \
       -name . -o \
       -type d -exec mkdir uxarc/$arcname/{} \;
find . -name uxarc -prune -o \
       -name CVS -prune -o \
       -name '.[^.]*' -prune -o \
       -name '*.app' -prune -o \
       -name '*.zip' -prune -o \
       -name '*.tar.gz' -prune -o \
       -type f -exec ln -s $PWD/{} uxarc/$arcname/{} \;
if test "x$ver" != "x"; then
  (cd uxarc/$arcname;
   md5sum `find . -name '*.[ch]' -print` > manifest;
   echo "$ver" > version.def)
fi
tar -C uxarc -chzof $arcname.tar.gz $arcname
rm -rf uxarc
