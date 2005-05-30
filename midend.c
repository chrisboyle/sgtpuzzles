/*
 * midend.c: general middle fragment sitting between the
 * platform-specific front end and game-specific back end.
 * Maintains a move list, takes care of Undo and Redo commands, and
 * processes standard keystrokes for undo/redo/new/quit.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>

#include "puzzles.h"

enum { DEF_PARAMS, DEF_SEED, DEF_DESC };   /* for midend_game_id_int */

struct midend_state_entry {
    game_state *state;
    int special;                       /* created by solve or restart */
};

struct midend_data {
    frontend *frontend;
    random_state *random;
    const game *ourgame;

    char *desc, *seedstr;
    game_aux_info *aux_info;
    enum { GOT_SEED, GOT_DESC, GOT_NOTHING } genmode;
    int nstates, statesize, statepos;

    game_params **presets;
    char **preset_names;
    int npresets, presetsize;

    game_params *params, *curparams;
    struct midend_state_entry *states;
    game_drawstate *drawstate;
    game_state *oldstate;
    game_ui *ui;
    float anim_time, anim_pos;
    float flash_time, flash_pos;
    int dir;

    int timing;
    float elapsed;
    char *laststatus;

    int pressed_mouse_button;
};

#define ensure(me) do { \
    if ((me)->nstates >= (me)->statesize) { \
	(me)->statesize = (me)->nstates + 128; \
	(me)->states = sresize((me)->states, (me)->statesize, \
                               struct midend_state_entry); \
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
    me->curparams = NULL;
    me->desc = NULL;
    me->seedstr = NULL;
    me->aux_info = NULL;
    me->genmode = GOT_NOTHING;
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
    me->laststatus = NULL;
    me->timing = FALSE;
    me->elapsed = 0.0F;

    sfree(randseed);

    return me;
}

void midend_free(midend_data *me)
{
    sfree(me->states);
    sfree(me->desc);
    sfree(me->seedstr);
    random_free(me->random);
    if (me->aux_info)
	me->ourgame->free_aux_info(me->aux_info);
    me->ourgame->free_params(me->params);
    if (me->curparams)
        me->ourgame->free_params(me->curparams);
    sfree(me->laststatus);
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

static void midend_set_timer(midend_data *me)
{
    me->timing = (me->ourgame->is_timed &&
		  me->ourgame->timing_state(me->states[me->statepos-1].state));
    if (me->timing || me->flash_time || me->anim_time)
	activate_timer(me->frontend);
    else
	deactivate_timer(me->frontend);
}

void midend_new_game(midend_data *me)
{
    while (me->nstates > 0)
	me->ourgame->free_game(me->states[--me->nstates].state);

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawstate);

    assert(me->nstates == 0);

    if (me->genmode == GOT_DESC) {
	me->genmode = GOT_NOTHING;
    } else {
        random_state *rs;

        if (me->genmode == GOT_SEED) {
            me->genmode = GOT_NOTHING;
        } else {
            /*
             * Generate a new random seed. 15 digits comes to about
             * 48 bits, which should be more than enough.
             * 
             * I'll avoid putting a leading zero on the number,
             * just in case it confuses anybody who thinks it's
             * processed as an integer rather than a string.
             */
            char newseed[16];
            int i;
            newseed[15] = '\0';
            newseed[0] = '1' + random_upto(me->random, 9);
            for (i = 1; i < 15; i++)
                newseed[i] = '0' + random_upto(me->random, 10);
            sfree(me->seedstr);
            me->seedstr = dupstr(newseed);

	    if (me->curparams)
		me->ourgame->free_params(me->curparams);
	    me->curparams = me->ourgame->dup_params(me->params);
        }

	sfree(me->desc);
	if (me->aux_info)
	    me->ourgame->free_aux_info(me->aux_info);
	me->aux_info = NULL;

        rs = random_init(me->seedstr, strlen(me->seedstr));
        me->desc = me->ourgame->new_desc(me->curparams, rs, &me->aux_info);
        random_free(rs);
    }

    ensure(me);
    me->states[me->nstates].state =
	me->ourgame->new_game(me, me->params, me->desc);
    me->states[me->nstates].special = TRUE;
    me->nstates++;
    me->statepos = 1;
    me->drawstate = me->ourgame->new_drawstate(me->states[0].state);
    me->elapsed = 0.0F;
    midend_set_timer(me);
    if (me->ui)
        me->ourgame->free_ui(me->ui);
    me->ui = me->ourgame->new_ui(me->states[0].state);
    me->pressed_mouse_button = 0;
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

    /*
     * We do not flash if the later of the two states is special.
     * This covers both forward Solve moves and backward (undone)
     * Restart moves.
     */
    if ((me->oldstate || me->statepos > 1) &&
        ((me->dir > 0 && !me->states[me->statepos-1].special) ||
         (me->dir < 0 && me->statepos < me->nstates &&
          !me->states[me->statepos].special))) {
	flashtime = me->ourgame->flash_length(me->oldstate ? me->oldstate :
					      me->states[me->statepos-2].state,
					      me->states[me->statepos-1].state,
					      me->oldstate ? me->dir : +1,
					      me->ui);
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

    midend_set_timer(me);
}

static void midend_stop_anim(midend_data *me)
{
    if (me->oldstate || me->anim_time) {
	midend_finish_move(me);
        midend_redraw(me);
    }
}

void midend_restart_game(midend_data *me)
{
    game_state *s;

    midend_stop_anim(me);

    assert(me->statepos >= 1);
    if (me->statepos == 1)
        return;                        /* no point doing anything at all! */

    s = me->ourgame->dup_game(me->states[0].state);

    /*
     * Now enter the restarted state as the next move.
     */
    midend_stop_anim(me);
    while (me->nstates > me->statepos)
	me->ourgame->free_game(me->states[--me->nstates].state);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].special = TRUE;   /* we just restarted */
    me->statepos = ++me->nstates;
    me->anim_time = 0.0;
    midend_finish_move(me);
    midend_redraw(me);
    midend_set_timer(me);
}

static int midend_really_process_key(midend_data *me, int x, int y, int button)
{
    game_state *oldstate =
        me->ourgame->dup_game(me->states[me->statepos - 1].state);
    int special = FALSE, gotspecial = FALSE;
    float anim_time;

    if (button == 'n' || button == 'N' || button == '\x0E') {
	midend_stop_anim(me);
	midend_new_game(me);
        midend_redraw(me);
        return 1;                      /* never animate */
    } else if (button == 'u' || button == 'u' ||
               button == '\x1A' || button == '\x1F') {
	midend_stop_anim(me);
        special = me->states[me->statepos-1].special;
        gotspecial = TRUE;
	if (!midend_undo(me))
            return 1;
    } else if (button == 'r' || button == 'R' ||
               button == '\x12') {
	midend_stop_anim(me);
	if (!midend_redo(me))
            return 1;
    } else if (button == 'q' || button == 'Q' || button == '\x11') {
	me->ourgame->free_game(oldstate);
        return 0;
    } else {
        game_state *s =
            me->ourgame->make_move(me->states[me->statepos-1].state,
                                   me->ui, x, y, button);

        if (s == me->states[me->statepos-1].state) {
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
                me->ourgame->free_game(me->states[--me->nstates].state);
            ensure(me);
            me->states[me->nstates].state = s;
            me->states[me->nstates].special = FALSE;   /* normal move */
            me->statepos = ++me->nstates;
            me->dir = +1;
        } else {
            me->ourgame->free_game(oldstate);
            return 1;
        }
    }

    if (!gotspecial)
        special = me->states[me->statepos-1].special;

    /*
     * See if this move requires an animation.
     */
    if (special) {
        anim_time = 0;
    } else {
        anim_time = me->ourgame->anim_length(oldstate,
                                             me->states[me->statepos-1].state,
                                             me->dir, me->ui);
    }

    me->oldstate = oldstate;
    if (anim_time > 0) {
        me->anim_time = anim_time;
    } else {
        me->anim_time = 0.0;
	midend_finish_move(me);
    }
    me->anim_pos = 0.0;

    midend_redraw(me);

    midend_set_timer(me);

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
				me->states[me->statepos-1].state, me->dir,
				me->ui, me->anim_pos, me->flash_pos);
        } else {
            me->ourgame->redraw(me->frontend, me->drawstate, NULL,
				me->states[me->statepos-1].state, +1 /*shrug*/,
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

    midend_redraw(me);

    if (me->timing) {
	float oldelapsed = me->elapsed;
	me->elapsed += tplus;
	if ((int)oldelapsed != (int)me->elapsed)
	    status_bar(me->frontend, me->laststatus ? me->laststatus : "");
    }

    midend_set_timer(me);
}

float *midend_colours(midend_data *me, int *ncolours)
{
    game_state *state = NULL;
    float *ret;

    if (me->nstates == 0) {
	game_aux_info *aux = NULL;
        char *desc = me->ourgame->new_desc(me->params, me->random, &aux);
        state = me->ourgame->new_game(me, me->params, desc);
        sfree(desc);
	if (aux)
	    me->ourgame->free_aux_info(aux);
    } else
        state = me->states[0].state;

    ret = me->ourgame->colours(me->frontend, state, ncolours);

    {
        int i;

        /*
         * Allow environment-based overrides for the standard
         * colours by defining variables along the lines of
         * `NET_COLOUR_4=6000c0'.
         */

        for (i = 0; i < *ncolours; i++) {
            char buf[80], *e;
            unsigned int r, g, b;
            int j;

            sprintf(buf, "%s_COLOUR_%d", me->ourgame->name, i);
            for (j = 0; buf[j]; j++)
                buf[j] = toupper((unsigned char)buf[j]);
            if ((e = getenv(buf)) != NULL &&
                sscanf(e, "%2x%2x%2x", &r, &g, &b) == 3) {
                ret[i*3 + 0] = r / 255.0;
                ret[i*3 + 1] = g / 255.0;
                ret[i*3 + 2] = b / 255.0;
            }
        }
    }

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

    {
        /*
         * Allow environment-based extensions to the preset list by
         * defining a variable along the lines of `SOLO_PRESETS=2x3
         * Advanced:2x3da'. Colon-separated list of items,
         * alternating between textual titles in the menu and
         * encoded parameter strings.
         */
        char buf[80], *e, *p;
        int j;

        sprintf(buf, "%s_PRESETS", me->ourgame->name);
        for (j = 0; buf[j]; j++)
            buf[j] = toupper((unsigned char)buf[j]);

        if ((e = getenv(buf)) != NULL) {
            p = e = dupstr(e);

            while (*p) {
                char *name, *val;
                game_params *preset;

                name = p;
                while (*p && *p != ':') p++;
                if (*p) *p++ = '\0';
                val = p;
                while (*p && *p != ':') p++;
                if (*p) *p++ = '\0';

                preset = me->ourgame->default_params();
                me->ourgame->decode_params(preset, val);

		if (me->ourgame->validate_params(preset)) {
		    /* Drop this one from the list. */
		    me->ourgame->free_params(preset);
		    continue;
		}

                if (me->presetsize <= me->npresets) {
                    me->presetsize = me->npresets + 10;
                    me->presets = sresize(me->presets, me->presetsize,
                                          game_params *);
                    me->preset_names = sresize(me->preset_names,
                                               me->presetsize, char *);
                }

                me->presets[me->npresets] = preset;
                me->preset_names[me->npresets] = name;
                me->npresets++;
            }
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

void midend_supersede_game_desc(midend_data *me, char *desc)
{
    sfree(me->desc);
    me->desc = dupstr(desc);
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
      case CFG_DESC:
	sprintf(titlebuf, "%s %s selection", me->ourgame->name,
                which == CFG_SEED ? "random" : "game");
	*wintitle = dupstr(titlebuf);

	ret = snewn(2, config_item);

	ret[0].type = C_STRING;
        if (which == CFG_SEED)
            ret[0].name = "Game random seed";
        else
            ret[0].name = "Game ID";
	ret[0].ival = 0;
        /*
         * For CFG_DESC the text going in here will be a string
         * encoding of the restricted parameters, plus a colon,
         * plus the game description. For CFG_SEED it will be the
         * full parameters, plus a hash, plus the random seed data.
         * Either of these is a valid full game ID (although only
         * the former is likely to persist across many code
         * changes).
         */
        parstr = me->ourgame->encode_params(me->curparams, which == CFG_SEED);
        if (which == CFG_DESC) {
            ret[0].sval = snewn(strlen(parstr) + strlen(me->desc) + 2, char);
            sprintf(ret[0].sval, "%s:%s", parstr, me->desc);
        } else if (me->seedstr) {
            ret[0].sval = snewn(strlen(parstr) + strlen(me->seedstr) + 2, char);
            sprintf(ret[0].sval, "%s#%s", parstr, me->seedstr);
        } else {
            /*
             * If the current game was not randomly generated, the
             * best we can do is to give a template for typing a
             * new seed in.
             */
            ret[0].sval = snewn(strlen(parstr) + 2, char);
            sprintf(ret[0].sval, "%s#", parstr);
        }
        sfree(parstr);

	ret[1].type = C_END;
	ret[1].name = ret[1].sval = NULL;
	ret[1].ival = 0;

	return ret;
    }

    assert(!"We shouldn't be here");
    return NULL;
}

static char *midend_game_id_int(midend_data *me, char *id, int defmode)
{
    char *error, *par, *desc, *seed;

    seed = strchr(id, '#');
    desc = strchr(id, ':');

    if (desc && (!seed || desc < seed)) {
        /*
         * We have a colon separating parameters from game
         * description. So `par' now points to the parameters
         * string, and `desc' to the description string.
         */
        *desc++ = '\0';
        par = id;
        seed = NULL;
    } else if (seed && (!desc || seed < desc)) {
        /*
         * We have a hash separating parameters from random seed.
         * So `par' now points to the parameters string, and `seed'
         * to the seed string.
         */
        *seed++ = '\0';
        par = id;
        desc = NULL;
    } else {
        /*
         * We only have one string. Depending on `defmode', we take
         * it to be either parameters, seed or description.
         */
        if (defmode == DEF_SEED) {
            seed = id;
            par = desc = NULL;
        } else if (defmode == DEF_DESC) {
            desc = id;
            par = seed = NULL;
        } else {
            par = id;
            seed = desc = NULL;
        }
    }

    if (par) {
        game_params *tmpparams;
        tmpparams = me->ourgame->dup_params(me->params);
        me->ourgame->decode_params(tmpparams, par);
        error = me->ourgame->validate_params(tmpparams);
        if (error) {
            me->ourgame->free_params(tmpparams);
            return error;
        }
        if (me->curparams)
            me->ourgame->free_params(me->curparams);
        me->curparams = tmpparams;

        /*
         * Now filter only the persistent parts of this state into
         * the long-term params structure, unless we've _only_
         * received a params string in which case the whole lot is
         * persistent.
         */
        if (seed || desc) {
            char *tmpstr = me->ourgame->encode_params(tmpparams, FALSE);
            me->ourgame->decode_params(me->params, tmpstr);
            sfree(tmpstr);
        } else {
            me->ourgame->free_params(me->params);
            me->params = me->ourgame->dup_params(tmpparams);
        }
    }

    sfree(me->desc);
    me->desc = NULL;
    sfree(me->seedstr);
    me->seedstr = NULL;

    if (desc) {
        error = me->ourgame->validate_desc(me->params, desc);
        if (error)
            return error;

        me->desc = dupstr(desc);
        me->genmode = GOT_DESC;
	if (me->aux_info)
	    me->ourgame->free_aux_info(me->aux_info);
	me->aux_info = NULL;
    }

    if (seed) {
        me->seedstr = dupstr(seed);
        me->genmode = GOT_SEED;
    }

    return NULL;
}

char *midend_game_id(midend_data *me, char *id)
{
    return midend_game_id_int(me, id, DEF_PARAMS);
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
      case CFG_DESC:
        error = midend_game_id_int(me, cfg[0].sval,
                                   (which == CFG_SEED ? DEF_SEED : DEF_DESC));
	if (error)
	    return error;
	break;
    }

    return NULL;
}

char *midend_text_format(midend_data *me)
{
    if (me->ourgame->can_format_as_text && me->statepos > 0)
	return me->ourgame->text_format(me->states[me->statepos-1].state);
    else
	return NULL;
}

char *midend_solve(midend_data *me)
{
    game_state *s;
    char *msg;

    if (!me->ourgame->can_solve)
	return "This game does not support the Solve operation";

    if (me->statepos < 1)
	return "No game set up to solve";   /* _shouldn't_ happen! */

    msg = "Solve operation failed";    /* game _should_ overwrite on error */
    s = me->ourgame->solve(me->states[0].state, me->aux_info, &msg);
    if (!s)
	return msg;

    /*
     * Now enter the solved state as the next move.
     */
    midend_stop_anim(me);
    while (me->nstates > me->statepos)
	me->ourgame->free_game(me->states[--me->nstates].state);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].special = TRUE;   /* created using solve */
    me->statepos = ++me->nstates;
    me->anim_time = 0.0;
    midend_finish_move(me);
    midend_redraw(me);
    midend_set_timer(me);
    return NULL;
}

char *midend_rewrite_statusbar(midend_data *me, char *text)
{
    /*
     * An important special case is that we are occasionally called
     * with our own laststatus, to update the timer.
     */
    if (me->laststatus != text) {
	sfree(me->laststatus);
	me->laststatus = dupstr(text);
    }

    if (me->ourgame->is_timed) {
	char timebuf[100], *ret;
	int min, sec;

	sec = me->elapsed;
	min = sec / 60;
	sec %= 60;
	sprintf(timebuf, "[%d:%02d] ", min, sec);

	ret = snewn(strlen(timebuf) + strlen(text) + 1, char);
	strcpy(ret, timebuf);
	strcat(ret, text);
	return ret;

    } else {
	return dupstr(text);
    }
}
