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

enum { NEWGAME, MOVE, SOLVE, RESTART };/* for midend_state_entry.movetype */

#define special(type) ( (type) != MOVE )

struct midend_state_entry {
    game_state *state;
    char *movestr;
    int movetype;
};

struct midend_data {
    frontend *frontend;
    random_state *random;
    const game *ourgame;

    game_params **presets;
    char **preset_names;
    int npresets, presetsize;

    /*
     * `desc' and `privdesc' deserve a comment.
     * 
     * `desc' is the game description as presented to the user when
     * they ask for Game -> Specific. `privdesc', if non-NULL, is a
     * different game description used to reconstruct the initial
     * game_state when de-serialising. If privdesc is NULL, `desc'
     * is used for both.
     * 
     * For almost all games, `privdesc' is NULL and never used. The
     * exception (as usual) is Mines: the initial game state has no
     * squares open at all, but after the first click `desc' is
     * rewritten to describe a game state with an initial click and
     * thus a bunch of squares open. If we used that desc to
     * serialise and deserialise, then the initial game state after
     * deserialisation would look unlike the initial game state
     * beforehand, and worse still execute_move() might fail on the
     * attempted first click. So `privdesc' is also used in this
     * case, to provide a game description describing the same
     * fixed mine layout _but_ no initial click. (These game IDs
     * may also be typed directly into Mines if you like.)
     */
    char *desc, *privdesc, *seedstr;
    char *aux_info;
    enum { GOT_SEED, GOT_DESC, GOT_NOTHING } genmode;

    int nstates, statesize, statepos;
    struct midend_state_entry *states;

    game_params *params, *curparams;
    game_drawstate *drawstate;
    game_ui *ui;

    game_state *oldstate;
    float anim_time, anim_pos;
    float flash_time, flash_pos;
    int dir;

    int timing;
    float elapsed;
    char *laststatus;

    int pressed_mouse_button;

    int winwidth, winheight;
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
    me->desc = me->privdesc = NULL;
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
    me->winwidth = me->winheight = 0;

    sfree(randseed);

    return me;
}

static void midend_free_game(midend_data *me)
{
    while (me->nstates > 0) {
        me->nstates--;
	me->ourgame->free_game(me->states[me->nstates].state);
	sfree(me->states[me->nstates].movestr);
    }

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawstate);
}

void midend_free(midend_data *me)
{
    int i;

    midend_free_game(me);

    random_free(me->random);
    sfree(me->states);
    sfree(me->desc);
    sfree(me->privdesc);
    sfree(me->seedstr);
    sfree(me->aux_info);
    me->ourgame->free_params(me->params);
    if (me->npresets) {
	for (i = 0; i < me->npresets; i++) {
	    sfree(me->presets[i]);
	    sfree(me->preset_names[i]);
	}
	sfree(me->presets);
	sfree(me->preset_names);
    }
    if (me->ui)
        me->ourgame->free_ui(me->ui);
    if (me->curparams)
        me->ourgame->free_params(me->curparams);
    sfree(me->laststatus);
    sfree(me);
}

void midend_size(midend_data *me, int *x, int *y, int expand)
{
    me->ourgame->size(me->params, me->drawstate, x, y, expand);
    me->winwidth = *x;
    me->winheight = *y;
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

static void midend_size_new_drawstate(midend_data *me)
{
    me->ourgame->size(me->params, me->drawstate, &me->winwidth, &me->winheight,
                      TRUE);
}

void midend_force_redraw(midend_data *me)
{
    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawstate);
    me->drawstate = me->ourgame->new_drawstate(me->states[0].state);
    midend_size_new_drawstate(me);
    midend_redraw(me);
}

void midend_new_game(midend_data *me)
{
    midend_free_game(me);

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
	sfree(me->privdesc);
        sfree(me->aux_info);
	me->aux_info = NULL;

        rs = random_init(me->seedstr, strlen(me->seedstr));
        me->desc = me->ourgame->new_desc(me->curparams, rs,
					 &me->aux_info, TRUE);
	me->privdesc = NULL;
        random_free(rs);
    }

    ensure(me);
    me->states[me->nstates].state =
	me->ourgame->new_game(me, me->params, me->desc);
    me->states[me->nstates].movestr = NULL;
    me->states[me->nstates].movetype = NEWGAME;
    me->nstates++;
    me->statepos = 1;
    me->drawstate = me->ourgame->new_drawstate(me->states[0].state);
    midend_size_new_drawstate(me);
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
        if (me->ui)
            me->ourgame->changed_state(me->ui,
                                       me->states[me->statepos-1].state,
                                       me->states[me->statepos-2].state);
	me->statepos--;
        me->dir = -1;
        return 1;
    } else
        return 0;
}

static int midend_redo(midend_data *me)
{
    if (me->statepos < me->nstates) {
        if (me->ui)
            me->ourgame->changed_state(me->ui,
                                       me->states[me->statepos-1].state,
                                       me->states[me->statepos].state);
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
        ((me->dir > 0 && !special(me->states[me->statepos-1].movetype)) ||
         (me->dir < 0 && me->statepos < me->nstates &&
          !special(me->states[me->statepos].movetype)))) {
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

void midend_stop_anim(midend_data *me)
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

    /*
     * During restart, we reconstruct the game from the (public)
     * game description rather than from states[0], because that
     * way Mines gets slightly more sensible behaviour (restart
     * goes to _after_ the first click so you don't have to
     * remember where you clicked).
     */
    s = me->ourgame->new_game(me, me->params, me->desc);

    /*
     * Now enter the restarted state as the next move.
     */
    midend_stop_anim(me);
    while (me->nstates > me->statepos)
	me->ourgame->free_game(me->states[--me->nstates].state);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].movestr = dupstr(me->desc);
    me->states[me->nstates].movetype = RESTART;
    me->statepos = ++me->nstates;
    if (me->ui)
        me->ourgame->changed_state(me->ui,
                                   me->states[me->statepos-2].state,
                                   me->states[me->statepos-1].state);
    me->anim_time = 0.0;
    midend_finish_move(me);
    midend_redraw(me);
    midend_set_timer(me);
}

static int midend_really_process_key(midend_data *me, int x, int y, int button)
{
    game_state *oldstate =
        me->ourgame->dup_game(me->states[me->statepos - 1].state);
    int special = FALSE, gotspecial = FALSE, ret = 1;
    float anim_time;

    if (button == 'n' || button == 'N' || button == '\x0E') {
	midend_stop_anim(me);
	midend_new_game(me);
        midend_redraw(me);
	goto done;		       /* never animate */
    } else if (button == 'u' || button == 'u' ||
               button == '\x1A' || button == '\x1F') {
	midend_stop_anim(me);
        special = special(me->states[me->statepos-1].movetype);
        gotspecial = TRUE;
	if (!midend_undo(me))
	    goto done;
    } else if (button == 'r' || button == 'R' ||
               button == '\x12' || button == '\x19') {
	midend_stop_anim(me);
	if (!midend_redo(me))
            goto done;
    } else if (button == 'q' || button == 'Q' || button == '\x11') {
	ret = 0;
	goto done;
    } else {
        game_state *s;
	char *movestr;
	
	movestr =
            me->ourgame->interpret_move(me->states[me->statepos-1].state,
					me->ui, me->drawstate, x, y, button);
	if (!movestr)
	    s = NULL;
	else if (!*movestr)
	    s = me->states[me->statepos-1].state;
	else {
	    s = me->ourgame->execute_move(me->states[me->statepos-1].state,
					  movestr);
	    assert(s != NULL);
	}

        if (s == me->states[me->statepos-1].state) {
            /*
             * make_move() is allowed to return its input state to
             * indicate that although no move has been made, the UI
             * state has been updated and a redraw is called for.
             */
            midend_redraw(me);
            goto done;
        } else if (s) {
	    midend_stop_anim(me);
            while (me->nstates > me->statepos)
                me->ourgame->free_game(me->states[--me->nstates].state);
            ensure(me);
            assert(movestr != NULL);
            me->states[me->nstates].state = s;
            me->states[me->nstates].movestr = movestr;
            me->states[me->nstates].movetype = MOVE;
            me->statepos = ++me->nstates;
            me->dir = +1;
        } else {
            goto done;
        }
    }

    if (!gotspecial)
        special = special(me->states[me->statepos-1].movetype);

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

    me->oldstate = oldstate; oldstate = NULL;
    if (anim_time > 0) {
        me->anim_time = anim_time;
    } else {
        me->anim_time = 0.0;
	midend_finish_move(me);
    }
    me->anim_pos = 0.0;

    midend_redraw(me);

    midend_set_timer(me);

    done:
    if (oldstate) me->ourgame->free_game(oldstate);
    return ret;
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
     * 2005-05-31: An addendum to the above. Some games might want
     * a `priority order' among buttons, such that if one button is
     * pressed while another is down then a fixed one of the
     * buttons takes priority no matter what order they're pressed
     * in. Mines, in particular, wants to treat a left+right click
     * like a left click for the benefit of users of other
     * implementations. So the last of the above points is modified
     * in the presence of an (optional) button priority order.
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
	 * If the new button has lower priority than the old one,
	 * don't bother doing this.
	 */
	if (me->ourgame->mouse_priorities &
	    BUTTON_BEATS(me->pressed_mouse_button, button))
	    return ret;		       /* just ignore it */

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
	char *aux = NULL;
        char *desc = me->ourgame->new_desc(me->params, me->random,
					   &aux, TRUE);
        state = me->ourgame->new_game(me, me->params, desc);
        sfree(desc);
        sfree(aux);
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

void midend_supersede_game_desc(midend_data *me, char *desc, char *privdesc)
{
    sfree(me->desc);
    sfree(me->privdesc);
    me->desc = dupstr(desc);
    me->privdesc = privdesc ? dupstr(privdesc) : NULL;
}

config_item *midend_get_config(midend_data *me, int which, char **wintitle)
{
    char *titlebuf, *parstr, *rest;
    config_item *ret;
    char sep;

    assert(wintitle);
    titlebuf = snewn(40 + strlen(me->ourgame->name), char);

    switch (which) {
      case CFG_SETTINGS:
	sprintf(titlebuf, "%s configuration", me->ourgame->name);
	*wintitle = titlebuf;
	return me->ourgame->configure(me->params);
      case CFG_SEED:
      case CFG_DESC:
        if (!me->curparams) {
          sfree(titlebuf);
          return NULL;
        }
        sprintf(titlebuf, "%s %s selection", me->ourgame->name,
                which == CFG_SEED ? "random" : "game");
        *wintitle = titlebuf;

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
        assert(parstr);
        if (which == CFG_DESC) {
            rest = me->desc ? me->desc : "";
            sep = ':';
        } else {
            rest = me->seedstr ? me->seedstr : "";
            sep = '#';
        }
        ret[0].sval = snewn(strlen(parstr) + strlen(rest) + 2, char);
        sprintf(ret[0].sval, "%s%c%s", parstr, sep, rest);
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
    sfree(me->privdesc);
    me->desc = me->privdesc = NULL;
    sfree(me->seedstr);
    me->seedstr = NULL;

    if (desc) {
        error = me->ourgame->validate_desc(me->params, desc);
        if (error)
            return error;

        me->desc = dupstr(desc);
        me->genmode = GOT_DESC;
        sfree(me->aux_info);
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
    char *msg, *movestr;

    if (!me->ourgame->can_solve)
	return "This game does not support the Solve operation";

    if (me->statepos < 1)
	return "No game set up to solve";   /* _shouldn't_ happen! */

    msg = "Solve operation failed";    /* game _should_ overwrite on error */
    movestr = me->ourgame->solve(me->states[0].state,
				 me->states[me->statepos-1].state,
				 me->aux_info, &msg);
    if (!movestr)
	return msg;
    s = me->ourgame->execute_move(me->states[me->statepos-1].state, movestr);
    assert(s);

    /*
     * Now enter the solved state as the next move.
     */
    midend_stop_anim(me);
    while (me->nstates > me->statepos)
	me->ourgame->free_game(me->states[--me->nstates].state);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].movestr = movestr;
    me->states[me->nstates].movetype = SOLVE;
    me->statepos = ++me->nstates;
    if (me->ui)
        me->ourgame->changed_state(me->ui,
                                   me->states[me->statepos-2].state,
                                   me->states[me->statepos-1].state);
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

#define SERIALISE_MAGIC "Simon Tatham's Portable Puzzle Collection"
#define SERIALISE_VERSION "1"

void midend_serialise(midend_data *me,
                      void (*write)(void *ctx, void *buf, int len),
                      void *wctx)
{
    int i;

    /*
     * Each line of the save file contains three components. First
     * exactly 8 characters of header word indicating what type of
     * data is contained on the line; then a colon followed by a
     * decimal integer giving the length of the main string on the
     * line; then a colon followed by the string itself (exactly as
     * many bytes as previously specified, no matter what they
     * contain). Then a newline (of reasonably flexible form).
     */
#define wr(h,s) do { \
    char hbuf[80]; \
    char *str = (s); \
    sprintf(hbuf, "%-8.8s:%d:", (h), (int)strlen(str)); \
    write(wctx, hbuf, strlen(hbuf)); \
    write(wctx, str, strlen(str)); \
    write(wctx, "\n", 1); \
} while (0)

    /*
     * Magic string identifying the file, and version number of the
     * file format.
     */
    wr("SAVEFILE", SERIALISE_MAGIC);
    wr("VERSION", SERIALISE_VERSION);

    /*
     * The game name. (Copied locally to avoid const annoyance.)
     */
    {
        char *s = dupstr(me->ourgame->name);
        wr("GAME", s);
        sfree(s);
    }

    /*
     * The current long-term parameters structure, in full.
     */
    if (me->params) {
        char *s = me->ourgame->encode_params(me->params, TRUE);
        wr("PARAMS", s);
        sfree(s);
    }

    /*
     * The current short-term parameters structure, in full.
     */
    if (me->curparams) {
        char *s = me->ourgame->encode_params(me->curparams, TRUE);
        wr("CPARAMS", s);
        sfree(s);
    }

    /*
     * The current game description, the privdesc, and the random seed.
     */
    if (me->seedstr)
        wr("SEED", me->seedstr);
    if (me->desc)
        wr("DESC", me->desc);
    if (me->privdesc)
        wr("PRIVDESC", me->privdesc);

    /*
     * The game's aux_info. We obfuscate this to prevent spoilers
     * (people are likely to run `head' or similar on a saved game
     * file simply to find out what it is, and don't necessarily
     * want to be told the answer to the puzzle!)
     */
    if (me->aux_info) {
        unsigned char *s1;
        char *s2;
        int len;

        len = strlen(me->aux_info);
        s1 = snewn(len, unsigned char);
        memcpy(s1, me->aux_info, len);
        obfuscate_bitmap(s1, len*8, FALSE);
        s2 = bin2hex(s1, len);

        wr("AUXINFO", s2);

        sfree(s2);
        sfree(s1);
    }

    /*
     * Any required serialisation of the game_ui.
     */
    if (me->ui) {
        char *s = me->ourgame->encode_ui(me->ui);
        if (s) {
            wr("UI", s);
            sfree(s);
        }
    }

    /*
     * The game time, if it's a timed game.
     */
    if (me->ourgame->is_timed) {
        char buf[80];
        sprintf(buf, "%g", me->elapsed);
        wr("TIME", buf);
    }

    /*
     * The length of, and position in, the states list.
     */
    {
        char buf[80];
        sprintf(buf, "%d", me->nstates);
        wr("NSTATES", buf);
        sprintf(buf, "%d", me->statepos);
        wr("STATEPOS", buf);
    }

    /*
     * For each state after the initial one (which we know is
     * constructed from either privdesc or desc), enough
     * information for execute_move() to reconstruct it from the
     * previous one.
     */
    for (i = 1; i < me->nstates; i++) {
        assert(me->states[i].movetype != NEWGAME);   /* only state 0 */
        switch (me->states[i].movetype) {
          case MOVE:
            wr("MOVE", me->states[i].movestr);
            break;
          case SOLVE:
            wr("SOLVE", me->states[i].movestr);
            break;
          case RESTART:
            wr("RESTART", me->states[i].movestr);
            break;
        }
    }

#undef wr
}

/*
 * This function returns NULL on success, or an error message.
 */
char *midend_deserialise(midend_data *me,
                         int (*read)(void *ctx, void *buf, int len),
                         void *rctx)
{
    int nstates = 0, statepos = -1, gotstates = 0;
    int started = FALSE;
    int i;

    char *val = NULL;
    /* Initially all errors give the same report */
    char *ret = "Data does not appear to be a saved game file";

    /*
     * We construct all the new state in local variables while we
     * check its sanity. Only once we have finished reading the
     * serialised data and detected no errors at all do we start
     * modifying stuff in the midend_data passed in.
     */
    char *seed = NULL, *parstr = NULL, *desc = NULL, *privdesc = NULL;
    char *auxinfo = NULL, *uistr = NULL, *cparstr = NULL;
    float elapsed = 0.0F;
    game_params *params = NULL, *cparams = NULL;
    game_ui *ui = NULL;
    struct midend_state_entry *states = NULL;

    /*
     * Loop round and round reading one key/value pair at a time
     * from the serialised stream, until we have enough game states
     * to finish.
     */
    while (nstates <= 0 || statepos < 0 || gotstates < nstates-1) {
        char key[9], c;
        int len;

        do {
            if (!read(rctx, key, 1)) {
                /* unexpected EOF */
                goto cleanup;
            }
        } while (key[0] == '\r' || key[0] == '\n');

        if (!read(rctx, key+1, 8)) {
            /* unexpected EOF */
            goto cleanup;
        }

        if (key[8] != ':') {
            if (started)
                ret = "Data was incorrectly formatted for a saved game file";
        }
        len = strcspn(key, ": ");
        assert(len <= 8);
        key[len] = '\0';

        len = 0;
        while (1) {
            if (!read(rctx, &c, 1)) {
                /* unexpected EOF */
                goto cleanup;
            }

            if (c == ':') {
                break;
            } else if (c >= '0' && c <= '9') {
                len = (len * 10) + (c - '0');
            } else {
                if (started)
                    ret = "Data was incorrectly formatted for a"
                    " saved game file";
                goto cleanup;
            }
        }

        val = snewn(len+1, char);
        if (!read(rctx, val, len)) {
            if (started)
            goto cleanup;
        }
        val[len] = '\0';

        if (!started) {
            if (strcmp(key, "SAVEFILE") || strcmp(val, SERIALISE_MAGIC)) {
                /* ret already has the right message in it */
                goto cleanup;
            }
            /* Now most errors are this one, unless otherwise specified */
            ret = "Saved data ended unexpectedly";
            started = TRUE;
        } else {
            if (!strcmp(key, "VERSION")) {
                if (strcmp(val, SERIALISE_VERSION)) {
                    ret = "Cannot handle this version of the saved game"
                        " file format";
                    goto cleanup;
                }
            } else if (!strcmp(key, "GAME")) {
                if (strcmp(val, me->ourgame->name)) {
                    ret = "Save file is from a different game";
                    goto cleanup;
                }
            } else if (!strcmp(key, "PARAMS")) {
                sfree(parstr);
                parstr = val;
                val = NULL;
            } else if (!strcmp(key, "CPARAMS")) {
                sfree(cparstr);
                cparstr = val;
                val = NULL;
            } else if (!strcmp(key, "SEED")) {
                sfree(seed);
                seed = val;
                val = NULL;
            } else if (!strcmp(key, "DESC")) {
                sfree(desc);
                desc = val;
                val = NULL;
            } else if (!strcmp(key, "PRIVDESC")) {
                sfree(privdesc);
                privdesc = val;
                val = NULL;
            } else if (!strcmp(key, "AUXINFO")) {
                unsigned char *tmp;
                int len = strlen(val) / 2;   /* length in bytes */
                tmp = hex2bin(val, len);
                obfuscate_bitmap(tmp, len*8, TRUE);

                sfree(auxinfo);
                auxinfo = snewn(len + 1, char);
                memcpy(auxinfo, tmp, len);
                auxinfo[len] = '\0';
                sfree(tmp);
            } else if (!strcmp(key, "UI")) {
                sfree(uistr);
                uistr = val;
                val = NULL;
            } else if (!strcmp(key, "TIME")) {
                elapsed = atof(val);
            } else if (!strcmp(key, "NSTATES")) {
                nstates = atoi(val);
                if (nstates <= 0) {
                    ret = "Number of states in save file was negative";
                    goto cleanup;
                }
                if (states) {
                    ret = "Two state counts provided in save file";
                    goto cleanup;
                }
                states = snewn(nstates, struct midend_state_entry);
                for (i = 0; i < nstates; i++) {
                    states[i].state = NULL;
                    states[i].movestr = NULL;
                    states[i].movetype = NEWGAME;
                }
            } else if (!strcmp(key, "STATEPOS")) {
                statepos = atoi(val);
            } else if (!strcmp(key, "MOVE")) {
                gotstates++;
                states[gotstates].movetype = MOVE;
                states[gotstates].movestr = val;
                val = NULL;
            } else if (!strcmp(key, "SOLVE")) {
                gotstates++;
                states[gotstates].movetype = SOLVE;
                states[gotstates].movestr = val;
                val = NULL;
            } else if (!strcmp(key, "RESTART")) {
                gotstates++;
                states[gotstates].movetype = RESTART;
                states[gotstates].movestr = val;
                val = NULL;
            }
        }

        sfree(val);
        val = NULL;
    }

    params = me->ourgame->default_params();
    me->ourgame->decode_params(params, parstr);
    if (me->ourgame->validate_params(params)) {
        ret = "Long-term parameters in save file are invalid";
        goto cleanup;
    }
    cparams = me->ourgame->default_params();
    me->ourgame->decode_params(cparams, cparstr);
    if (me->ourgame->validate_params(cparams)) {
        ret = "Short-term parameters in save file are invalid";
        goto cleanup;
    }
    if (!desc) {
        ret = "Game description in save file is missing";
        goto cleanup;
    } else if (me->ourgame->validate_desc(params, desc)) {
        ret = "Game description in save file is invalid";
        goto cleanup;
    }
    if (privdesc && me->ourgame->validate_desc(params, privdesc)) {
        ret = "Game private description in save file is invalid";
        goto cleanup;
    }
    if (statepos < 0 || statepos >= nstates) {
        ret = "Game position in save file is out of range";
    }

    states[0].state = me->ourgame->new_game(me, params,
                                            privdesc ? privdesc : desc);
    for (i = 1; i < nstates; i++) {
        assert(states[i].movetype != NEWGAME);
        switch (states[i].movetype) {
          case MOVE:
          case SOLVE:
            states[i].state = me->ourgame->execute_move(states[i-1].state,
                                                        states[i].movestr);
            if (states[i].state == NULL) {
                ret = "Save file contained an invalid move";
                goto cleanup;
            }
            break;
          case RESTART:
            if (me->ourgame->validate_desc(params, states[i].movestr)) {
                ret = "Save file contained an invalid restart move";
                goto cleanup;
            }
            states[i].state = me->ourgame->new_game(me, params,
                                                    states[i].movestr);
            break;
        }
    }

    ui = me->ourgame->new_ui(states[0].state);
    me->ourgame->decode_ui(ui, uistr);

    /*
     * Now we've run out of possible error conditions, so we're
     * ready to start overwriting the real data in the current
     * midend. We'll do this by swapping things with the local
     * variables, so that the same cleanup code will free the old
     * stuff.
     */
    {
        char *tmp;

        tmp = me->desc;
        me->desc = desc;
        desc = tmp;

        tmp = me->privdesc;
        me->privdesc = privdesc;
        privdesc = tmp;

        tmp = me->seedstr;
        me->seedstr = seed;
        seed = tmp;

        tmp = me->aux_info;
        me->aux_info = auxinfo;
        auxinfo = tmp;
    }

    me->genmode = GOT_NOTHING;

    me->statesize = nstates;
    nstates = me->nstates;
    me->nstates = me->statesize;
    {
        struct midend_state_entry *tmp;
        tmp = me->states;
        me->states = states;
        states = tmp;
    }
    me->statepos = statepos;

    {
        game_params *tmp;

        tmp = me->params;
        me->params = params;
        params = tmp;

        tmp = me->curparams;
        me->curparams = cparams;
        cparams = tmp;
    }

    me->oldstate = NULL;
    me->anim_time = me->anim_pos = me->flash_time = me->flash_pos = 0.0F;
    me->dir = 0;

    {
        game_ui *tmp;

        tmp = me->ui;
        me->ui = ui;
        ui = tmp;
    }

    me->elapsed = elapsed;
    me->pressed_mouse_button = 0;

    midend_set_timer(me);

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawstate);
    me->drawstate =
        me->ourgame->new_drawstate(me->states[me->statepos-1].state);
    midend_size_new_drawstate(me);

    ret = NULL;                        /* success! */

    cleanup:
    sfree(val);
    sfree(seed);
    sfree(parstr);
    sfree(cparstr);
    sfree(desc);
    sfree(privdesc);
    sfree(auxinfo);
    sfree(uistr);
    if (params)
        me->ourgame->free_params(params);
    if (cparams)
        me->ourgame->free_params(cparams);
    if (ui)
        me->ourgame->free_ui(ui);
    if (states) {
        int i;

        for (i = 0; i < nstates; i++) {
            if (states[i].state)
                me->ourgame->free_game(states[i].state);
            sfree(states[i].movestr);
        }
        sfree(states);
    }

    return ret;
}
