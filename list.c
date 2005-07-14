/*
 * list.c: List of puzzles.
 */

#include "puzzles.h"

/*
 * The available games can be most easily enumerated by searching
 * for the line in each game source file saying "#define thegame
 * <gamename>". Hence, the following piece of shell/Perl should
 * regenerate this list automatically:

perl -ne '/^#define thegame (\S+)/ and $1 ne "nullgame" and print "extern const game $1;\n"' *.c
echo -e '\nconst game *gamelist[] = {'
perl -ne '/^#define thegame (\S+)/ and $1 ne "nullgame" and print "    &$1,\n"' *.c
echo -e '};\n\nconst int gamecount = lenof(gamelist);'

 */

extern const game cube;
extern const game dominosa;
extern const game fifteen;
extern const game flip;
extern const game guess;
extern const game mines;
extern const game net;
extern const game netslide;
extern const game pattern;
extern const game pegs;
extern const game rect;
extern const game samegame;
extern const game sixteen;
extern const game solo;
extern const game twiddle;

const game *gamelist[] = {
    &cube,
    &dominosa,
    &fifteen,
    &flip,
    &guess,
    &mines,
    &net,
    &netslide,
    &pattern,
    &pegs,
    &rect,
    &samegame,
    &sixteen,
    &solo,
    &twiddle,
};

const int gamecount = lenof(gamelist);
