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
    frontend *frontend;
    char *seed;
    int nstates, statesize, statepos;

    game_params **presets;
    char **preset_names;
    int npresets, presetsize;

    game_params *params;
    game_state **states;
    game_drawstate *drawstate;
    game_state *oldstate;
    float anim_time, anim_pos;
    float flash_time, flash_pos;
};

#define ensure(me) do { \
    if ((me)->nstates >= (me)->statesize) { \
	(me)->statesize = (me)->nstates + 128; \
	(me)->states = sresize((me)->states, (me)->statesize, game_state *); \
    } \
} while (0)

midend_data *midend_new(frontend *frontend)
{
    midend_data *me = snew(midend_data);

    me->frontend = frontend;
    me->nstates = me->statesize = me->statepos = 0;
    me->states = NULL;
    me->params = default_params();
    me->seed = NULL;
    me->drawstate = NULL;
    me->oldstate = NULL;
    me->presets = NULL;
    me->preset_names = NULL;
    me->npresets = me->presetsize = 0;
    me->anim_time = me->anim_pos = 0.0F;
    me->flash_time = me->flash_pos = 0.0F;

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
    me->params = dup_params(params);
}

void midend_new_game(midend_data *me, char *seed)
{
    while (me->nstates > 0)
	free_game(me->states[--me->nstates]);

    if (me->drawstate)
        game_free_drawstate(me->drawstate);

    assert(me->nstates == 0);

    sfree(me->seed);
    if (seed)
	me->seed = dupstr(seed);
    else
	me->seed = new_game_seed(me->params);

    ensure(me);
    me->states[me->nstates++] = new_game(me->params, me->seed);
    me->statepos = 1;
    me->drawstate = game_new_drawstate(me->states[0]);
}

void midend_restart_game(midend_data *me)
{
    while (me->nstates > 1)
	free_game(me->states[--me->nstates]);
    me->statepos = me->nstates;
}

static int midend_undo(midend_data *me)
{
    if (me->statepos > 1) {
	me->statepos--;
        return 1;
    } else
        return 0;
}

static int midend_redo(midend_data *me)
{
    if (me->statepos < me->nstates) {
	me->statepos++;
        return 1;
    } else
        return 0;
}

static void midend_finish_move(midend_data *me)
{
    float flashtime;

    if (me->oldstate || me->statepos > 1) {
	flashtime = game_flash_length(me->oldstate ? me->oldstate :
				      me->states[me->statepos-2],
				      me->states[me->statepos-1]);
	if (flashtime > 0) {
	    me->flash_pos = 0.0F;
	    me->flash_time = flashtime;
	}
    }

    if (me->oldstate)
	free_game(me->oldstate);
    me->oldstate = NULL;
    me->anim_pos = me->anim_time = 0;

    if (me->flash_time == 0 && me->anim_time == 0)
	deactivate_timer(me->frontend);
    else
	activate_timer(me->frontend);
}

int midend_process_key(midend_data *me, int x, int y, int button)
{
    game_state *oldstate = dup_game(me->states[me->statepos - 1]);
    float anim_time;

    if (me->oldstate || me->anim_time) {
	midend_finish_move(me);
        midend_redraw(me);
    }

    if (button == 'n' || button == 'N' || button == '\x0E') {
	midend_new_game(me, NULL);
        midend_redraw(me);
        return 1;                      /* never animate */
    } else if (button == 'r' || button == 'R') {
	midend_restart_game(me);
        midend_redraw(me);
        return 1;                      /* never animate */
    } else if (button == 'u' || button == 'u' ||
               button == '\x1A' || button == '\x1F') {
	if (!midend_undo(me))
            return 1;
    } else if (button == '\x12') {
	if (!midend_redo(me))
            return 1;
    } else if (button == 'q' || button == 'Q' || button == '\x11') {
	free_game(oldstate);
        return 0;
    } else {
        game_state *s = make_move(me->states[me->statepos-1], x, y, button);

        if (s) {
            while (me->nstates > me->statepos)
                free_game(me->states[--me->nstates]);
            ensure(me);
            me->states[me->nstates] = s;
            me->statepos = ++me->nstates;
        } else {
            free_game(oldstate);
            return 1;
        }
    }

    /*
     * See if this move requires an animation.
     */
    anim_time = game_anim_length(oldstate, me->states[me->statepos-1]);

    me->oldstate = oldstate;
    if (anim_time > 0) {
        me->anim_time = anim_time;
    } else {
        me->anim_time = 0.0;
	midend_finish_move(me);
    }
    me->anim_pos = 0.0;

    midend_redraw(me);

    activate_timer(me->frontend);

    return 1;
}

void midend_redraw(midend_data *me)
{
    if (me->statepos > 0 && me->drawstate) {
        start_draw(me->frontend);
        if (me->oldstate && me->anim_time > 0 &&
            me->anim_pos < me->anim_time) {
            game_redraw(me->frontend, me->drawstate, me->oldstate,
                        me->states[me->statepos-1], me->anim_pos,
			me->flash_pos);
        } else {
            game_redraw(me->frontend, me->drawstate, NULL,
                        me->states[me->statepos-1], 0.0, me->flash_pos);
        }
        end_draw(me->frontend);
    }
}

void midend_timer(midend_data *me, float tplus)
{
    me->anim_pos += tplus;
    if (me->anim_pos >= me->anim_time ||
        me->anim_time == 0 || !me->oldstate) {
	if (me->anim_time > 0)
	    midend_finish_move(me);
    }
    me->flash_pos += tplus;
    if (me->flash_pos >= me->flash_time || me->flash_time == 0) {
	me->flash_pos = me->flash_time = 0;
    }
    if (me->flash_time == 0 && me->anim_time == 0)
	deactivate_timer(me->frontend);
    midend_redraw(me);
}

float *midend_colours(midend_data *me, int *ncolours)
{
    game_state *state = NULL;
    float *ret;

    if (me->nstates == 0) {
        char *seed = new_game_seed(me->params);
        state = new_game(me->params, seed);
        sfree(seed);
    } else
        state = me->states[0];

    ret = game_colours(me->frontend, state, ncolours);

    if (me->nstates == 0)
        free_game(state);

    return ret;
}

int midend_num_presets(midend_data *me)
{
    if (!me->npresets) {
        char *name;
        game_params *preset;

        while (game_fetch_preset(me->npresets, &name, &preset)) {
            if (me->presetsize <= me->npresets) {
                me->presetsize = me->npresets + 10;
                me->presets = sresize(me->presets, me->presetsize,
                                      game_params *);
                me->preset_names = sresize(me->preset_names, me->presetsize,
                                           char *);
            }

            me->presets[me->npresets] = preset;
            me->preset_names[me->npresets] = name;
            me->npresets++;
        }
    }

    return me->npresets;
}

void midend_fetch_preset(midend_data *me, int n,
                         char **name, game_params **params)
{
    assert(n >= 0 && n < me->npresets);
    *name = me->preset_names[n];
    *params = me->presets[n];
}

int midend_wants_statusbar(midend_data *me)
{
    return game_wants_statusbar();
}

config_item *midend_get_config(midend_data *me)
{
    return game_configure(me->params);
}

char *midend_set_config(midend_data *me, config_item *cfg)
{
    char *error;
    game_params *params;

    params = custom_params(cfg);
    error = validate_params(params);

    if (error) {
	free_params(params);
	return error;
    }

    free_params(me->params);
    me->params = params;

    return NULL;
}
