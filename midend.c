/*
 * midend.c: general middle fragment sitting between the
 * platform-specific front end and game-specific back end.
 * Maintains a move list, takes care of Undo and Redo commands, and
 * processes standard keystrokes for undo/redo/new/restart/quit.
 */

#include <stdio.h>
#include <assert.h>

#include "puzzles.h"

struct midend_data {
    char *seed;
    int nstates, statesize, statepos;
    game_params *params;
    game_state **states;
};

#define ensure(me) do { \
    if ((me)->nstates >= (me)->statesize) { \
	(me)->statesize = (me)->nstates + 128; \
	(me)->states = sresize((me)->states, (me)->statesize, game_state *); \
    } \
} while (0)

midend_data *midend_new(void)
{
    midend_data *me = snew(midend_data);

    me->nstates = me->statesize = me->statepos = 0;
    me->states = NULL;
    me->params = default_params();
    me->seed = NULL;

    return me;
}

void midend_free(midend_data *me)
{
    sfree(me->states);
    sfree(me->seed);
    free_params(me->params);
    sfree(me);
}

void midend_size(midend_data *me, int *x, int *y)
{
    game_size(me->params, x, y);
}

void midend_set_params(midend_data *me, game_params *params)
{
    free_params(me->params);
    me->params = params;
}

void midend_new_game(midend_data *me, char *seed)
{
    while (me->nstates > 0)
	free_game(me->states[--me->nstates]);

    assert(me->nstates == 0);

    sfree(me->seed);
    if (seed)
	me->seed = dupstr(seed);
    else
	me->seed = new_game_seed(me->params);

    ensure(me);
    me->states[me->nstates++] = new_game(me->params, me->seed);
    me->statepos = 1;
}

void midend_restart_game(midend_data *me)
{
    while (me->nstates > 1)
	free_game(me->states[--me->nstates]);
    me->statepos = me->nstates;
}

void midend_undo(midend_data *me)
{
    if (me->statepos > 1)
	me->statepos--;
}

void midend_redo(midend_data *me)
{
    if (me->statepos < me->nstates)
	me->statepos++;
}

int midend_process_key(midend_data *me, int x, int y, int button)
{
    game_state *s;

    if (button == 'n' || button == 'N' || button == '\x0E') {
	midend_new_game(me, NULL);
	return 1;
    } else if (button == 'r' || button == 'R') {
	midend_restart_game(me);
	return 1;
    } else if (button == 'u' || button == 'u' ||
	       button == '\x1A' || button == '\x1F') {
	midend_undo(me);
	return 1;
    } else if (button == '\x12') {
	midend_redo(me);
	return 1;
    } else if (button == 'q' || button == 'Q' || button == '\x11') {
	return 0;
    }

    s = make_move(me->states[me->statepos-1], x, y, button);

    if (s) {
	while (me->nstates > me->statepos)
	    free_game(me->states[--me->nstates]);
	ensure(me);
	me->states[me->nstates] = s;
	me->statepos = ++me->nstates;
    }

    return 1;
}
