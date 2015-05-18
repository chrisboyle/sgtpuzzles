#!/bin/sh

# Run every puzzle in benchmarking mode, and generate a file of raw
# data that benchmark.pl will format into a web page.

# If any arguments are provided, use those as the list of games to
# benchmark. Otherwise, read the full list from gamedesc.txt.
if test $# = 0; then
    set -- $(cut -f1 -d: < gamedesc.txt)
fi

failures=false

for game in "$@"; do
    # Use 'env -i' to suppress any environment variables that might
    # change the preset list for a puzzle (e.g. user-defined extras)
    presets=$(env -i ./$game --list-presets | cut -f1 -d' ')
    for preset in $presets; do
	if ! env -i ./$game --test-solve --time-generation \
                            --generate 100 $preset;
        then
            echo "${game} ${preset} failed to generate" >&2
        fi
    done
done

if $failures; then exit 1; fi
