#!/bin/sh 

# Generate a screenshot from a puzzle save file. Takes the
# following arguments, in order:
#
#  - the name of the puzzle binary
#  - the name of the save file
#  - the name of the output image file
#  - (optionally) the proportion of the next move to redo before
#    taking the screenshot.
#
# This script requires access to an X server in order to run, but
# seems to work fine under xvfb-run if you haven't got a real one
# available (or if you don't want to use it for some reason).

binary="$1"
save="$2"
image="$3"
if test "x$4" != "x"; then
  redo="--redo $4"
else
  redo=
fi

"$binary" $redo --screenshot "$image" --load "$save"
