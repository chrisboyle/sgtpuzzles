/*
 * midend.c: general middle fragment sitting between the
 * platform-specific front end and game-specific back end.
 * Maintains a move list, takes care of Undo and Redo commands, and
 * processes standard keystrokes for undo/redo/new/restart/quit.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "puzzles.h"

struct midend_data {
    frontend *frontend;
    random_state *random;
    const game *ourgame;

    char *seed;
    int fresh_seed;
    int nstates, statesize, statepos;

    game_params **presets;
    char **preset_names;
    int npresets, presetsize;

    game_params *params;
    game_state **states;
    game_drawstate *drawstate;
    game_state *oldstate;
    game_ui *ui;
    float anim_time, anim_pos;
    float flash_time, flash_pos;
    int dir;

    int pressed_mouse_button;
};

#define ensure(me) do { \
    if ((me)->nstates >= (me)->statesize) { \
	(me)->statesize = (me)->nstates + 128; \
	(me)->states = sresize((me)->states, (me)->statesize, game_state *); \
    } \
} while (0)

midend_data *midend_new(frontend *fe, const game *ourgame)
{
    midend_data *me = snew(midend_data);
    void *randseed;
    int randseedsize;

    get_random_seed(&randseed, &randseedsize);

    me->frontend = fe;
    me->ourgame = ourgame;
    me->random = random_init(randseed, randseedsize);
    me->nstates = me->statesize = me->statepos = 0;
    me->states = NULL;
    me->params = ourgame->default_params();
    me->seed = NULL;
    me->fresh_seed = FALSE;
    me->drawstate = NULL;
    me->oldstate = NULL;
    me->presets = NULL;
    me->preset_names = NULL;
    me->npresets = me->presetsize = 0;
    me->anim_time = me->anim_pos = 0.0F;
    me->flash_time = me->flash_pos = 0.0F;
    me->dir = 0;
    me->ui = NULL;
    me->pressed_mouse_button = 0;

    sfree(randseed);

    return me;
}

void midend_free(midend_data *me)
{
    sfree(me->states);
    sfree(me->seed);
    me->ourgame->free_params(me->params);
    sfree(me);
}

void midend_size(midend_data *me, int *x, int *y)
{
    me->ourgame->size(me->params, x, y);
}

void midend_set_params(midend_data *me, game_params *params)
{
    me->ourgame->free_params(me->params);
    me->params = me->ourgame->dup_params(params);
}

void midend_new_game(midend_data *me)
{
    while (me->nstates > 0)
	me->ourgame->free_game(me->states[--me->nstates]);

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawstate);

    assert(me->nstates == 0);

    if (!me->fresh_seed) {
	sfree(me->seed);
	me->seed = me->ourgame->new_seed(me->params, me->random);
    } else
	me->fresh_seed = FALSE;

    ensure(me);
    me->states[me->nstates++] = me->ourgame->new_game(me->params, me->seed);
    me->statepos = 1;
    me->drawstate = me->ourgame->new_drawstate(me->states[0]);
    if (me->ui)
        me->ourgame->free_ui(me->ui);
    me->ui = me->ourgame->new_ui(me->states[0]);
    me->pressed_mouse_button = 0;
}

void midend_restart_game(midend_data *me)
{
    while (me->nstates > 1)
	me->ourgame->free_game(me->states[--me->nstates]);
    me->statepos = me->nstates;
    me->ourgame->free_ui(me->ui);
    me->ui = me->ourgame->new_ui(me->states[0]);
}

static int midend_undo(midend_data *me)
{
    if (me->statepos > 1) {
	me->statepos--;
        me->dir = -1;
        return 1;
    } else
        return 0;
}

static int midend_redo(midend_data *me)
{
    if (me->statepos < me->nstates) {
	me->statepos++;
        me->dir = +1;
        return 1;
    } else
        return 0;
}

static void midend_finish_move(midend_data *me)
{
    float flashtime;

    if (me->oldstate || me->statepos > 1) {
	flashtime = me->ourgame->flash_length(me->oldstate ? me->oldstate :
					      me->states[me->statepos-2],
					      me->states[me->statepos-1],
					      me->oldstate ? me->dir : +1);
	if (flashtime > 0) {
	    me->flash_pos = 0.0F;
	    me->flash_time = flashtime;
	}
    }

    if (me->oldstate)
	me->ourgame->free_game(me->oldstate);
    me->oldstate = NULL;
    me->anim_pos = me->anim_time = 0;
    me->dir = 0;

    if (me->flash_time == 0 && me->anim_time == 0)
	deactivate_timer(me->frontend);
    else
	activate_timer(me->frontend);
}

static void midend_stop_anim(midend_data *me)
{
    if (me->oldstate || me->anim_time) {
	midend_finish_move(me);
        midend_redraw(me);
    }
}

static int midend_really_process_key(midend_data *me, int x, int y, int button)
{
    game_state *oldstate = me->ourgame->dup_game(me->states[me->statepos - 1]);
    float anim_time;

    if (button == 'n' || button == 'N' || button == '\x0E') {
	midend_stop_anim(me);
	midend_new_game(me);
        midend_redraw(me);
        return 1;                      /* never animate */
    } else if (button == 'r' || button == 'R') {
	midend_stop_anim(me);
	midend_restart_game(me);
        midend_redraw(me);
        return 1;                      /* never animate */
    } else if (button == 'u' || button == 'u' ||
               button == '\x1A' || button == '\x1F') {
	midend_stop_anim(me);
	if (!midend_undo(me))
            return 1;
    } else if (button == '\x12') {
	midend_stop_anim(me);
	if (!midend_redo(me))
            return 1;
    } else if (button == 'q' || button == 'Q' || button == '\x11') {
	me->ourgame->free_game(oldstate);
        return 0;
    } else {
        game_state *s = me->ourgame->make_move(me->states[me->statepos-1],
					       me->ui, x, y, button);

        if (s == me->states[me->statepos-1]) {
            /*
             * make_move() is allowed to return its input state to
             * indicate that although no move has been made, the UI
             * state has been updated and a redraw is called for.
             */
            midend_redraw(me);
            return 1;
        } else if (s) {
	    midend_stop_anim(me);
            while (me->nstates > me->statepos)
                me->ourgame->free_game(me->states[--me->nstates]);
            ensure(me);
            me->states[me->nstates] = s;
            me->statepos = ++me->nstates;
            me->dir = +1;
        } else {
            me->ourgame->free_game(oldstate);
            return 1;
        }
    }

    /*
     * See if this move requires an animation.
     */
    anim_time = me->ourgame->anim_length(oldstate, me->states[me->statepos-1],
					 me->dir);

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

int midend_process_key(midend_data *me, int x, int y, int button)
{
    int ret = 1;

    /*
     * Harmonise mouse drag and release messages.
     * 
     * Some front ends might accidentally switch from sending, say,
     * RIGHT_DRAG messages to sending LEFT_DRAG, half way through a
     * drag. (This can happen on the Mac, for example, since
     * RIGHT_DRAG is usually done using Command+drag, and if the
     * user accidentally releases Command half way through the drag
     * then there will be trouble.)
     * 
     * It would be an O(number of front ends) annoyance to fix this
     * in the front ends, but an O(number of back ends) annoyance
     * to have each game capable of dealing with it. Therefore, we
     * fix it _here_ in the common midend code so that it only has
     * to be done once.
     * 
     * The possible ways in which things can go screwy in the front
     * end are:
     * 
     *  - in a system containing multiple physical buttons button
     *    presses can inadvertently overlap. We can see ABab (caps
     *    meaning button-down and lowercase meaning button-up) when
     *    the user had semantically intended AaBb.
     * 
     *  - in a system where one button is simulated by means of a
     *    modifier key and another button, buttons can mutate
     *    between press and release (possibly during drag). So we
     *    can see Ab instead of Aa.
     * 
     * Definite requirements are:
     * 
     *  - button _presses_ must never be invented or destroyed. If
     *    the user presses two buttons in succession, the button
     *    presses must be transferred to the backend unchanged. So
     *    if we see AaBb , that's fine; if we see ABab (the button
     *    presses inadvertently overlapped) we must somehow
     *    `correct' it to AaBb.
     * 
     *  - every mouse action must end up looking like a press, zero
     *    or more drags, then a release. This allows back ends to
     *    make the _assumption_ that incoming mouse data will be
     *    sane in this regard, and not worry about the details.
     * 
     * So my policy will be:
     * 
     *  - treat any button-up as a button-up for the currently
     *    pressed button, or ignore it if there is no currently
     *    pressed button.
     * 
     *  - treat any drag as a drag for the currently pressed
     *    button, or ignore it if there is no currently pressed
     *    button.
     * 
     *  - if we see a button-down while another button is currently
     *    pressed, invent a button-up for the first one and then
     *    pass the button-down through as before.
     * 
     */
    if (IS_MOUSE_DRAG(button) || IS_MOUSE_RELEASE(button)) {
        if (me->pressed_mouse_button) {
            if (IS_MOUSE_DRAG(button)) {
                button = me->pressed_mouse_button +
                    (LEFT_DRAG - LEFT_BUTTON);
            } else {
                button = me->pressed_mouse_button +
                    (LEFT_RELEASE - LEFT_BUTTON);
            }
        } else
            return ret;                /* ignore it */
    } else if (IS_MOUSE_DOWN(button) && me->pressed_mouse_button) {
        /*
         * Fabricate a button-up for the previously pressed button.
         */
        ret = ret && midend_really_process_key
            (me, x, y, (me->pressed_mouse_button +
                        (LEFT_RELEASE - LEFT_BUTTON)));
    }

    /*
     * Now send on the event we originally received.
     */
    ret = ret && midend_really_process_key(me, x, y, button);

    /*
     * And update the currently pressed button.
     */
    if (IS_MOUSE_RELEASE(button))
        me->pressed_mouse_button = 0;
    else if (IS_MOUSE_DOWN(button))
        me->pressed_mouse_button = button;

    return ret;
}

void midend_redraw(midend_data *me)
{
    if (me->statepos > 0 && me->drawstate) {
        start_draw(me->frontend);
        if (me->oldstate && me->anim_time > 0 &&
            me->anim_pos < me->anim_time) {
            assert(me->dir != 0);
            me->ourgame->redraw(me->frontend, me->drawstate, me->oldstate,
				me->states[me->statepos-1], me->dir,
				me->ui, me->anim_pos, me->flash_pos);
        } else {
            me->ourgame->redraw(me->frontend, me->drawstate, NULL,
				me->states[me->statepos-1], +1 /*shrug*/,
				me->ui, 0.0, me->flash_pos);
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
        char *seed = me->ourgame->new_seed(me->params, me->random);
        state = me->ourgame->new_game(me->params, seed);
        sfree(seed);
    } else
        state = me->states[0];

    ret = me->ourgame->colours(me->frontend, state, ncolours);

    if (me->nstates == 0)
        me->ourgame->free_game(state);

    return ret;
}

int midend_num_presets(midend_data *me)
{
    if (!me->npresets) {
        char *name;
        game_params *preset;

        while (me->ourgame->fetch_preset(me->npresets, &name, &preset)) {
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
    return me->ourgame->wants_statusbar();
}

config_item *midend_get_config(midend_data *me, int which, char **wintitle)
{
    char *titlebuf, *parstr;
    config_item *ret;

    titlebuf = snewn(40 + strlen(me->ourgame->name), char);

    switch (which) {
      case CFG_SETTINGS:
	sprintf(titlebuf, "%s configuration", me->ourgame->name);
	*wintitle = dupstr(titlebuf);
	return me->ourgame->configure(me->params);
      case CFG_SEED:
	sprintf(titlebuf, "%s game selection", me->ourgame->name);
	*wintitle = dupstr(titlebuf);

	ret = snewn(2, config_item);

	ret[0].type = C_STRING;
	ret[0].name = "Game ID";
	ret[0].ival = 0;
        /*
         * The text going in here will be a string encoding of the
         * parameters, plus a colon, plus the game seed. This is a
         * full game ID.
         */
        parstr = me->ourgame->encode_params(me->params);
        ret[0].sval = snewn(strlen(parstr) + strlen(me->seed) + 2, char);
        sprintf(ret[0].sval, "%s:%s", parstr, me->seed);
        sfree(parstr);

	ret[1].type = C_END;
	ret[1].name = ret[1].sval = NULL;
	ret[1].ival = 0;

	return ret;
    }

    assert(!"We shouldn't be here");
    return NULL;
}

char *midend_game_id(midend_data *me, char *id, int def_seed)
{
    char *error, *par, *seed;
    game_params *params;

    seed = strchr(id, ':');

    if (seed) {
        /*
         * We have a colon separating parameters from game seed. So
         * `par' now points to the parameters string, and `seed' to
         * the seed string.
         */
        *seed++ = '\0';
        par = id;
    } else {
        /*
         * We only have one string. Depending on `def_seed', we
         * take it to be either parameters or seed.
         */
        if (def_seed) {
            seed = id;
            par = NULL;
        } else {
            seed = NULL;
            par = id;
        }
    }

    if (par) {
        params = me->ourgame->decode_params(par);
        error = me->ourgame->validate_params(params);
        if (error) {
            me->ourgame->free_params(params);
            return error;
        }
        me->ourgame->free_params(me->params);
        me->params = params;
    }

    if (seed) {
        error = me->ourgame->validate_seed(me->params, seed);
        if (error)
            return error;

        sfree(me->seed);
        me->seed = dupstr(seed);
        me->fresh_seed = TRUE;
    }

    return NULL;
}

char *midend_set_config(midend_data *me, int which, config_item *cfg)
{
    char *error;
    game_params *params;

    switch (which) {
      case CFG_SETTINGS:
	params = me->ourgame->custom_params(cfg);
	error = me->ourgame->validate_params(params);

	if (error) {
	    me->ourgame->free_params(params);
	    return error;
	}

	me->ourgame->free_params(me->params);
	me->params = params;
	break;

      case CFG_SEED:
        error = midend_game_id(me, cfg[0].sval, TRUE);
	if (error)
	    return error;
	break;
    }

    return NULL;
}

char *midend_text_format(midend_data *me)
{
    if (me->ourgame->can_format_as_text && me->statepos > 0)
	return me->ourgame->text_format(me->states[me->statepos-1]);
    else
	return NULL;
}
