/*
 * nullgame.c [FIXME]: Template defining the null game (in which no
 * moves are permitted and nothing is ever drawn). This file exists
 * solely as a basis for constructing new game definitions - it
 * helps to have something which will compile from the word go and
 * merely doesn't _do_ very much yet.
 * 
 * Parts labelled FIXME actually want _removing_ (e.g. the dummy
 * field in each of the required data structures, and this entire
 * comment itself) when converting this source file into one
 * describing a real game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"

const char *const game_name = "Null Game";

enum {
    COL_BACKGROUND,
    NCOLOURS
};

struct game_params {
    int FIXME;
};

struct game_state {
    int FIXME;
};

game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->FIXME = 0;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
}

void free_params(game_params *params)
{
    sfree(params);
}

game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

char *new_game_seed(game_params *params)
{
    return dupstr("FIXME");
}

game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);

    state->FIXME = 0;

    return state;
}

game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->FIXME = state->FIXME;

    return ret;
}

void free_game(game_state *state)
{
    sfree(state);
}

game_state *make_move(game_state *from, int x, int y, int button)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct game_drawstate {
    int FIXME;
};

void game_size(game_params *params, int *x, int *y)
{
    *x = *y = 200;                     /* FIXME */
}

float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->FIXME = 0;

    return ds;
}

void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds);
}

void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, float animtime, float flashtime)
{
}

float game_anim_length(game_state *oldstate, game_state *newstate)
{
    return 0.0F;
}

float game_flash_length(game_state *oldstate, game_state *newstate)
{
    return 0.0F;
}

int game_wants_statusbar(void)
{
    return FALSE;
}
