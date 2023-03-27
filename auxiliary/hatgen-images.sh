#!/bin/sh

# Regenerate all the SVG images in the auxiliary/doc directory for
# hats.html and hatmap.html.

set -e

hatgen=$1

if ! test -x "$hatgen"; then
    echo "Provide pathname to hatgen as an argument" >&2
    exit 1
fi

for tile in H T P F; do
    "$hatgen" "$tile" > single-"$tile".svg
    "$hatgen" c"$tile" > expanded-"$tile".svg
    "$hatgen" h"$tile" > hats-single-"$tile".svg
    "$hatgen" H"$tile" > kitemap-"$tile".svg
    "$hatgen" C"$tile" > metamap-"$tile".svg
done

"$hatgen" --hat > hat-kites.svg
