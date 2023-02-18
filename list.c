/*
 * list.c: List of pointers to puzzle structures, for monolithic
 * platforms.
 *
 * This file depends on the header "generated-games.h", which is
 * constructed by CMakeLists.txt.
 */

#include "puzzles.h"

#define GAME(x) &x,
const game *gamelist[] = {
#include "generated-games.h"
};
#undef GAME

const int gamecount = lenof(gamelist);
