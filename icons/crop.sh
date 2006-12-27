#!/bin/sh 

# Crop one image into another, after first checking that the source
# image has the expected size in pixels.
#
# This is used in the Puzzles icon build scripts to construct icons
# which are zoomed in on a particular sub-area of the puzzle's
# basic screenshot. This way I can define crop areas in pixels,
# while not having to worry too much that if I adjust the source
# puzzle so as to alter the layout the crop area might start
# hitting the wrong bit of picture. Most layout changes I can
# conveniently imagine will also alter the overall image size, so
# this script will give a build error and alert me to the fact that
# I need to fiddle with the icon makefile.

infile="$1"
outfile="$2"
insize="$3"
crop="$4"

# Special case: if no input size or crop parameter was specified at
# all, we just copy the input to the output file.

if test $# -lt 3; then
  cp "$infile" "$outfile"
  exit 0
fi

# Check the input image size.
realsize=`identify -format %wx%h "$infile"`
if test "x$insize" != "x$realsize"; then
  echo "crop.sh: '$infile' has wrong initial size: $realsize != $insize" >&2
  exit 1
fi

# And crop.
convert -crop "$crop" "$infile" "$outfile"
