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

struct midend_serialise_buf {
    char *buf;
    int len, size;
};

struct midend {
    frontend *frontend;
    random_state *random;
    const game *ourgame;

    struct preset_menu *preset_menu;
    char **encoded_presets; /* for midend_which_preset to check against */
    int n_encoded_presets;

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

    struct midend_serialise_buf newgame_undo, newgame_redo;
    bool newgame_can_store_undo;

    game_params *params, *curparams;
    game_drawstate *drawstate;
    bool first_draw;
    game_ui *ui;

    game_state *oldstate;
    float anim_time, anim_pos;
    float flash_time, flash_pos;
    int dir;

    bool timing;
    float elapsed;
    char *laststatus;

    drawing *drawing;

    int pressed_mouse_button;

    int preferred_tilesize, preferred_tilesize_dpr, tilesize;
    int winwidth, winheight;

    void (*game_id_change_notify_function)(void *);
    void *game_id_change_notify_ctx;
};

#define ensure(me) do { \
    if ((me)->nstates >= (me)->statesize) { \
	(me)->statesize = (me)->nstates + 128; \
	(me)->states = sresize((me)->states, (me)->statesize, \
                               struct midend_state_entry); \
    } \
} while (0)

/*
 * Structure storing all the decoded data from reading a serialised
 * game. We keep it in one of these while we check its sanity, and
 * only once we're completely satisfied do we install it all in the
 * midend structure proper.
 */
struct deserialise_data {
    char *seed, *parstr, *desc, *privdesc;
    char *auxinfo, *uistr, *cparstr;
    float elapsed;
    game_params *params, *cparams;
    game_ui *ui;
    struct midend_state_entry *states;
    int nstates, statepos;
};

/*
 * Forward reference.
 */
static const char *midend_deserialise_internal(
    midend *me, bool (*read)(void *ctx, void *buf, int len), void *rctx,
    const char *(*check)(void *ctx, midend *, const struct deserialise_data *),
    void *cctx);

void midend_reset_tilesize(midend *me)
{
    me->preferred_tilesize = me->ourgame->preferred_tilesize;
    me->preferred_tilesize_dpr = 1.0;
    {
        /*
         * Allow an environment-based override for the default tile
         * size by defining a variable along the lines of
         * `NET_TILESIZE=15'.
         *
         * XXX How should this interact with DPR?
         */

	char buf[80], *e;
	int j, k, ts;

	sprintf(buf, "%s_TILESIZE", me->ourgame->name);
	for (j = k = 0; buf[j]; j++)
	    if (!isspace((unsigned char)buf[j]))
		buf[k++] = toupper((unsigned char)buf[j]);
	buf[k] = '\0';
	if ((e = getenv(buf)) != NULL && sscanf(e, "%d", &ts) == 1 && ts > 0)
	    me->preferred_tilesize = ts;
    }
}

midend *midend_new(frontend *fe, const game *ourgame,
		   const drawing_api *drapi, void *drhandle)
{
    midend *me = snew(midend);
    void *randseed;
    int randseedsize;

    get_random_seed(&randseed, &randseedsize);

    me->frontend = fe;
    me->ourgame = ourgame;
    me->random = random_new(randseed, randseedsize);
    me->nstates = me->statesize = me->statepos = 0;
    me->states = NULL;
    me->newgame_undo.buf = NULL;
    me->newgame_undo.size = me->newgame_undo.len = 0;
    me->newgame_redo.buf = NULL;
    me->newgame_redo.size = me->newgame_redo.len = 0;
    me->newgame_can_store_undo = false;
    me->params = ourgame->default_params();
    me->game_id_change_notify_function = NULL;
    me->game_id_change_notify_ctx = NULL;
    me->encoded_presets = NULL;
    me->n_encoded_presets = 0;

    /*
     * Allow environment-based changing of the default settings by
     * defining a variable along the lines of `NET_DEFAULT=25x25w'
     * in which the value is an encoded parameter string.
     */
    {
        char buf[80], *e;
        int j, k;
        sprintf(buf, "%s_DEFAULT", me->ourgame->name);
	for (j = k = 0; buf[j]; j++)
	    if (!isspace((unsigned char)buf[j]))
		buf[k++] = toupper((unsigned char)buf[j]);
	buf[k] = '\0';
        if ((e = getenv(buf)) != NULL)
            me->ourgame->decode_params(me->params, e);
    }
    me->curparams = NULL;
    me->desc = me->privdesc = NULL;
    me->seedstr = NULL;
    me->aux_info = NULL;
    me->genmode = GOT_NOTHING;
    me->drawstate = NULL;
    me->first_draw = true;
    me->oldstate = NULL;
    me->preset_menu = NULL;
    me->anim_time = me->anim_pos = 0.0F;
    me->flash_time = me->flash_pos = 0.0F;
    me->dir = 0;
    me->ui = NULL;
    me->pressed_mouse_button = 0;
    me->laststatus = NULL;
    me->timing = false;
    me->elapsed = 0.0F;
    me->tilesize = me->winwidth = me->winheight = 0;
    if (drapi)
	me->drawing = drawing_new(drapi, me, drhandle);
    else
	me->drawing = NULL;

    midend_reset_tilesize(me);

    sfree(randseed);

    return me;
}

const game *midend_which_game(midend *me)
{
    return me->ourgame;
}

static void midend_purge_states(midend *me)
{
    while (me->nstates > me->statepos) {
        me->ourgame->free_game(me->states[--me->nstates].state);
        if (me->states[me->nstates].movestr)
            sfree(me->states[me->nstates].movestr);
    }
    me->newgame_redo.len = 0;
}

static void midend_free_game(midend *me)
{
    while (me->nstates > 0) {
        me->nstates--;
	me->ourgame->free_game(me->states[me->nstates].state);
	sfree(me->states[me->nstates].movestr);
    }

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawing, me->drawstate);
}

static void midend_free_preset_menu(midend *me, struct preset_menu *menu)
{
    if (menu) {
        int i;
        for (i = 0; i < menu->n_entries; i++) {
            sfree(menu->entries[i].title);
            if (menu->entries[i].params)
                me->ourgame->free_params(menu->entries[i].params);
            midend_free_preset_menu(me, menu->entries[i].submenu);
        }
        sfree(menu->entries);
        sfree(menu);
    }
}

void midend_free(midend *me)
{
    int i;

    midend_free_game(me);

    for (i = 0; i < me->n_encoded_presets; i++)
        sfree(me->encoded_presets[i]);
    sfree(me->encoded_presets);
    if (me->drawing)
	drawing_free(me->drawing);
    random_free(me->random);
    sfree(me->newgame_undo.buf);
    sfree(me->newgame_redo.buf);
    sfree(me->states);
    sfree(me->desc);
    sfree(me->privdesc);
    sfree(me->seedstr);
    sfree(me->aux_info);
    me->ourgame->free_params(me->params);
    midend_free_preset_menu(me, me->preset_menu);
    if (me->ui)
        me->ourgame->free_ui(me->ui);
    if (me->curparams)
        me->ourgame->free_params(me->curparams);
    sfree(me->laststatus);
    sfree(me);
}

static void midend_size_new_drawstate(midend *me)
{
    /*
     * Don't even bother, if we haven't worked out our tile size
     * anyway yet.
     */
    if (me->tilesize > 0) {
	me->ourgame->compute_size(me->params, me->tilesize,
				  &me->winwidth, &me->winheight);
	me->ourgame->set_size(me->drawing, me->drawstate,
			      me->params, me->tilesize);
    }
}

/*
 * There is no one correct way to convert tilesizes between device
 * pixel ratios, because there's only a loosely-defined relationship
 * between tilesize and the actual size of a puzzle.  We define this
 * function as the canonical conversion function so everything in the
 * midend will be consistent.
 */
static int convert_tilesize(midend *me, int old_tilesize,
                            double old_dpr, double new_dpr)
{
    int x, y, rx, ry, min, max;
    game_params *defaults;

    if (new_dpr == old_dpr)
        return old_tilesize;

    defaults = me->ourgame->default_params();

    me->ourgame->compute_size(defaults, old_tilesize, &x, &y);
    x *= new_dpr / old_dpr;
    y *= new_dpr / old_dpr;

    min = max = 1;
    do {
        max *= 2;
        me->ourgame->compute_size(defaults, max, &rx, &ry);
    } while (rx <= x && ry <= y);

    while (max - min > 1) {
	int mid = (max + min) / 2;
	me->ourgame->compute_size(defaults, mid, &rx, &ry);
	if (rx <= x && ry <= y)
	    min = mid;
	else
	    max = mid;
    }

    me->ourgame->free_params(defaults);
    return min;
}

void midend_size(midend *me, int *x, int *y, bool user_size,
                 double device_pixel_ratio)
{
    int min, max;
    int rx, ry;

    /*
     * We can't set the size on the same drawstate twice. So if
     * we've already sized one drawstate, we must throw it away and
     * create a new one.
     */
    if (me->drawstate && me->tilesize > 0) {
        me->ourgame->free_drawstate(me->drawing, me->drawstate);
        me->drawstate = me->ourgame->new_drawstate(me->drawing,
                                                   me->states[0].state);
        me->first_draw = true;
    }

    /*
     * Find the tile size that best fits within the given space. If
     * `user_size' is true, we must actually find the _largest_ such
     * tile size, in order to get as close to the user's explicit
     * request as possible; otherwise, we bound above at the game's
     * preferred tile size, so that the game gets what it wants
     * provided that this doesn't break the constraint from the
     * front-end (which is likely to be a screen size or similar).
     */
    if (user_size) {
	max = 1;
	do {
	    max *= 2;
	    me->ourgame->compute_size(me->params, max, &rx, &ry);
	} while (rx <= *x && ry <= *y);
    } else
	max = convert_tilesize(me, me->preferred_tilesize,
                               me->preferred_tilesize_dpr,
                               device_pixel_ratio) + 1;
    min = 1;

    /*
     * Now binary-search between min and max. We're looking for a
     * boundary rather than a value: the point at which tile sizes
     * stop fitting within the given dimensions. Thus, we stop when
     * max and min differ by exactly 1.
     */
    while (max - min > 1) {
	int mid = (max + min) / 2;
	me->ourgame->compute_size(me->params, mid, &rx, &ry);
	if (rx <= *x && ry <= *y)
	    min = mid;
	else
	    max = mid;
    }

    /*
     * Now `min' is a valid size, and `max' isn't. So use `min'.
     */

    me->tilesize = min;
    if (user_size) {
        /* If the user requested a change in size, make it permanent. */
        me->preferred_tilesize = me->tilesize;
        me->preferred_tilesize_dpr = device_pixel_ratio;
    }
    midend_size_new_drawstate(me);
    *x = me->winwidth;
    *y = me->winheight;
}

int midend_tilesize(midend *me) { return me->tilesize; }

void midend_set_params(midend *me, game_params *params)
{
    me->ourgame->free_params(me->params);
    me->params = me->ourgame->dup_params(params);
}

game_params *midend_get_params(midend *me)
{
    return me->ourgame->dup_params(me->params);
}

static char *encode_params(midend *me, const game_params *params, bool full)
{
    char *encoded = me->ourgame->encode_params(params, full);
    int i;

    /* Assert that the params consist of printable ASCII containing
     * neither '#' nor ':'. */
    for (i = 0; encoded[i]; i++)
        assert(encoded[i] >= 32 && encoded[i] < 127 &&
	       encoded[i] != '#' && encoded[i] != ':');
    return encoded;
}

static void assert_printable_ascii(char const *s)
{
    /* Assert that s is entirely printable ASCII, and hence safe for
     * writing in a save file. */
    int i;
    for (i = 0; s[i]; i++)
        assert(s[i] >= 32 && s[i] < 127);
}

static void midend_set_timer(midend *me)
{
    me->timing = (me->ourgame->is_timed &&
		  me->ourgame->timing_state(me->states[me->statepos-1].state,
					    me->ui));
    if (me->timing || me->flash_time || me->anim_time)
	activate_timer(me->frontend);
    else
	deactivate_timer(me->frontend);
}

void midend_force_redraw(midend *me)
{
    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawing, me->drawstate);
    me->drawstate = me->ourgame->new_drawstate(me->drawing,
					       me->states[0].state);
    me->first_draw = true;
    midend_size_new_drawstate(me);
    midend_redraw(me);
}

static void newgame_serialise_write(void *ctx, const void *buf, int len)
{
    struct midend_serialise_buf *ser = (struct midend_serialise_buf *)ctx;
    int new_len;

    assert(len < INT_MAX - ser->len);
    new_len = ser->len + len;
    if (new_len > ser->size) {
	ser->size = new_len + new_len / 4 + 1024;
	ser->buf = sresize(ser->buf, ser->size, char);
    }
    memcpy(ser->buf + ser->len, buf, len);
    ser->len = new_len;
}

void midend_new_game(midend *me)
{
    me->newgame_undo.len = 0;
    if (me->newgame_can_store_undo) {
        /*
         * Serialise the whole of the game that we're about to
         * supersede, so that the 'New Game' action can be undone
         * later.
         *
         * We omit this in various situations, such as if there
         * _isn't_ a current game (not even a starting position)
         * because this is the initial call to midend_new_game when
         * the midend is first set up, or if the midend state has
         * already begun to be overwritten by midend_set_config. In
         * those situations, we want to avoid writing out any
         * serialisation, because they will be either invalid, or
         * worse, valid but wrong.
         */
        midend_purge_states(me);
        midend_serialise(me, newgame_serialise_write, &me->newgame_undo);
    }

    midend_stop_anim(me);
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
            newseed[0] = '1' + (char)random_upto(me->random, 9);
            for (i = 1; i < 15; i++)
                newseed[i] = '0' + (char)random_upto(me->random, 10);
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

        rs = random_new(me->seedstr, strlen(me->seedstr));
	/*
	 * If this midend has been instantiated without providing a
	 * drawing API, it is non-interactive. This means that it's
	 * being used for bulk game generation, and hence we should
	 * pass the non-interactive flag to new_desc.
	 */
        me->desc = me->ourgame->new_desc(me->curparams, rs,
					 &me->aux_info, (me->drawing != NULL));
	assert_printable_ascii(me->desc);
	me->privdesc = NULL;
        random_free(rs);
    }

    ensure(me);

    /*
     * It might seem a bit odd that we're using me->params to
     * create the initial game state, rather than me->curparams
     * which is better tailored to this specific game and which we
     * always know.
     * 
     * It's supposed to be an invariant in the midend that
     * me->params and me->curparams differ in no aspect that is
     * important after generation (i.e. after new_desc()). By
     * deliberately passing the _less_ specific of these two
     * parameter sets, we provoke play-time misbehaviour in the
     * case where a game has failed to encode a play-time parameter
     * in the non-full version of encode_params().
     */
    me->states[me->nstates].state =
	me->ourgame->new_game(me, me->params, me->desc);

    /*
     * As part of our commitment to self-testing, test the aux
     * string to make sure nothing ghastly went wrong.
     */
    if (me->ourgame->can_solve && me->aux_info) {
	game_state *s;
	const char *msg;
        char *movestr;

	msg = NULL;
	movestr = me->ourgame->solve(me->states[0].state,
				     me->states[0].state,
				     me->aux_info, &msg);
	assert(movestr && !msg);
	s = me->ourgame->execute_move(me->states[0].state, movestr);
	assert(s);
	me->ourgame->free_game(s);
	sfree(movestr);
    }

    me->states[me->nstates].movestr = NULL;
    me->states[me->nstates].movetype = NEWGAME;
    me->nstates++;
    me->statepos = 1;
    me->drawstate = me->ourgame->new_drawstate(me->drawing,
					       me->states[0].state);
    me->first_draw = true;
    midend_size_new_drawstate(me);
    me->elapsed = 0.0F;
    me->flash_pos = me->flash_time = 0.0F;
    me->anim_pos = me->anim_time = 0.0F;
    if (me->ui)
        me->ourgame->free_ui(me->ui);
    me->ui = me->ourgame->new_ui(me->states[0].state);
    midend_set_timer(me);
    me->pressed_mouse_button = 0;

    if (me->game_id_change_notify_function)
        me->game_id_change_notify_function(me->game_id_change_notify_ctx);

    me->newgame_can_store_undo = true;
}

bool midend_can_undo(midend *me)
{
    return (me->statepos > 1 || me->newgame_undo.len);
}

bool midend_can_redo(midend *me)
{
    return (me->statepos < me->nstates || me->newgame_redo.len);
}

struct newgame_undo_deserialise_read_ctx {
    struct midend_serialise_buf *ser;
    int len, pos;
};

static bool newgame_undo_deserialise_read(void *ctx, void *buf, int len)
{
    struct newgame_undo_deserialise_read_ctx *const rctx = ctx;

    if (len > rctx->len - rctx->pos)
        return false;

    memcpy(buf, rctx->ser->buf + rctx->pos, len);
    rctx->pos += len;
    return true;
}

struct newgame_undo_deserialise_check_ctx {
    bool refused;
};

static const char *newgame_undo_deserialise_check(
    void *vctx, midend *me, const struct deserialise_data *data)
{
    struct newgame_undo_deserialise_check_ctx *ctx =
        (struct newgame_undo_deserialise_check_ctx *)vctx;
    char *old, *new;

    /*
     * Undoing a New Game operation is only permitted if it doesn't
     * change the game parameters. The point of having the ability at
     * all is to recover from the momentary finger error of having hit
     * the 'n' key (perhaps in place of some other nearby key), or hit
     * the New Game menu item by mistake when aiming for the adjacent
     * Restart; in both those situations, the game params are the same
     * before and after the new-game operation.
     *
     * In principle, we could generalise this so that _any_ call to
     * midend_new_game could be undone, but that would need all front
     * ends to be alert to the possibility that any keystroke passed
     * to midend_process_key might (if it turns out to have been one
     * of the synonyms for undo, which the frontend doesn't
     * necessarily check for) have various knock-on effects like
     * needing to select a different preset in the game type menu, or
     * even resizing the window. At least for the moment, it's easier
     * not to do that, and to simply disallow any newgame-undo that is
     * disruptive in either of those ways.
     *
     * We check both params and cparams, to be as safe as possible.
     */

    old = encode_params(me, me->params, true);
    new = encode_params(me, data->params, true);
    if (strcmp(old, new)) {
        /* Set a flag to distinguish this deserialise failure
         * from one due to faulty decoding */
        ctx->refused = true;
        return "Undoing this new-game operation would change params";
    }

    old = encode_params(me, me->curparams, true);
    new = encode_params(me, data->cparams, true);
    if (strcmp(old, new)) {
        ctx->refused = true;
        return "Undoing this new-game operation would change params";
    }

    /*
     * Otherwise, fine, go ahead.
     */
    return NULL;
}

static bool midend_undo(midend *me)
{
    const char *deserialise_error;

    if (me->statepos > 1) {
        if (me->ui)
            me->ourgame->changed_state(me->ui,
                                       me->states[me->statepos-1].state,
                                       me->states[me->statepos-2].state);
	me->statepos--;
        me->dir = -1;
        return true;
    } else if (me->newgame_undo.len) {
	struct newgame_undo_deserialise_read_ctx rctx;
	struct newgame_undo_deserialise_check_ctx cctx;
        struct midend_serialise_buf serbuf;

        /*
         * Serialise the current game so that you can later redo past
         * this undo. Once we're committed to the undo actually
         * happening, we'll copy this data into place.
         */
        serbuf.buf = NULL;
        serbuf.len = serbuf.size = 0;
        midend_serialise(me, newgame_serialise_write, &serbuf);

	rctx.ser = &me->newgame_undo;
	rctx.len = me->newgame_undo.len; /* copy for reentrancy safety */
	rctx.pos = 0;
        cctx.refused = false;
        deserialise_error = midend_deserialise_internal(
            me, newgame_undo_deserialise_read, &rctx,
            newgame_undo_deserialise_check, &cctx);
        if (cctx.refused) {
            /*
             * Our post-deserialisation check shows that we can't use
             * this saved game after all. (deserialise_error will
             * contain the dummy error message generated by our check
             * function, which we ignore.)
             */
            sfree(serbuf.buf);
            return false;
        } else {
            /*
             * There should never be any _other_ deserialisation
             * error, because this serialised data has been held in
             * our memory since it was created, and hasn't had any
             * opportunity to be corrupted on disk, accidentally
             * replaced by the wrong file, etc., by user error.
             */
            assert(!deserialise_error);

            /*
             * Clear the old newgame_undo serialisation, so that we
             * don't try to undo past the beginning of the game we've
             * just gone back to and end up at the front of it again.
             */
            me->newgame_undo.len = 0;

            /*
             * Copy the serialisation of the game we've just left into
             * the midend so that we can redo back into it later.
             */
            me->newgame_redo.len = 0;
            newgame_serialise_write(&me->newgame_redo, serbuf.buf, serbuf.len);

            sfree(serbuf.buf);
            return true;
        }
    } else
        return false;
}

static bool midend_redo(midend *me)
{
    const char *deserialise_error;

    if (me->statepos < me->nstates) {
        if (me->ui)
            me->ourgame->changed_state(me->ui,
                                       me->states[me->statepos-1].state,
                                       me->states[me->statepos].state);
	me->statepos++;
        me->dir = +1;
        return true;
    } else if (me->newgame_redo.len) {
	struct newgame_undo_deserialise_read_ctx rctx;
	struct newgame_undo_deserialise_check_ctx cctx;
        struct midend_serialise_buf serbuf;

        /*
         * Serialise the current game so that you can later undo past
         * this redo. Once we're committed to the undo actually
         * happening, we'll copy this data into place.
         */
        serbuf.buf = NULL;
        serbuf.len = serbuf.size = 0;
        midend_serialise(me, newgame_serialise_write, &serbuf);

	rctx.ser = &me->newgame_redo;
	rctx.len = me->newgame_redo.len; /* copy for reentrancy safety */
	rctx.pos = 0;
        cctx.refused = false;
        deserialise_error = midend_deserialise_internal(
            me, newgame_undo_deserialise_read, &rctx,
            newgame_undo_deserialise_check, &cctx);
        if (cctx.refused) {
            /*
             * Our post-deserialisation check shows that we can't use
             * this saved game after all. (deserialise_error will
             * contain the dummy error message generated by our check
             * function, which we ignore.)
             */
            sfree(serbuf.buf);
            return false;
        } else {
            /*
             * There should never be any _other_ deserialisation
             * error, because this serialised data has been held in
             * our memory since it was created, and hasn't had any
             * opportunity to be corrupted on disk, accidentally
             * replaced by the wrong file, etc., by user error.
             */
            assert(!deserialise_error);

            /*
             * Clear the old newgame_redo serialisation, so that we
             * don't try to redo past the end of the game we've just
             * come into and end up at the back of it again.
             */
            me->newgame_redo.len = 0;

            /*
             * Copy the serialisation of the game we've just left into
             * the midend so that we can undo back into it later.
             */
            me->newgame_undo.len = 0;
            newgame_serialise_write(&me->newgame_undo, serbuf.buf, serbuf.len);

            sfree(serbuf.buf);
            return true;
        }
    } else
        return false;
}

static void midend_finish_move(midend *me)
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

void midend_stop_anim(midend *me)
{
    if (me->oldstate || me->anim_time != 0) {
	midend_finish_move(me);
        midend_redraw(me);
    }
}

void midend_restart_game(midend *me)
{
    game_state *s;

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
    midend_purge_states(me);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].movestr = dupstr(me->desc);
    me->states[me->nstates].movetype = RESTART;
    me->statepos = ++me->nstates;
    if (me->ui)
        me->ourgame->changed_state(me->ui,
                                   me->states[me->statepos-2].state,
                                   me->states[me->statepos-1].state);
    me->flash_pos = me->flash_time = 0.0F;
    midend_finish_move(me);
    midend_redraw(me);
    midend_set_timer(me);
}

static bool midend_really_process_key(midend *me, int x, int y, int button,
                                      bool *handled)
{
    game_state *oldstate =
        me->ourgame->dup_game(me->states[me->statepos - 1].state);
    int type = MOVE;
    bool gottype = false, ret = true;
    float anim_time;
    game_state *s;
    char *movestr = NULL;

    if (!IS_UI_FAKE_KEY(button)) {
        movestr = me->ourgame->interpret_move(
            me->states[me->statepos-1].state,
            me->ui, me->drawstate, x, y, button);
    }

    if (!movestr) {
	if (button == 'n' || button == 'N' || button == '\x0E' ||
            button == UI_NEWGAME) {
	    midend_new_game(me);
	    midend_redraw(me);
            *handled = true;
	    goto done;		       /* never animate */
	} else if (button == 'u' || button == 'U' || button == '*' ||
		   button == '\x1A' || button == '\x1F' ||
                   button == UI_UNDO) {
	    midend_stop_anim(me);
	    type = me->states[me->statepos-1].movetype;
	    gottype = true;
	    if (!midend_undo(me))
		goto done;
            *handled = true;
	} else if (button == 'r' || button == 'R' || button == '#' ||
		   button == '\x12' || button == '\x19' ||
                   button == UI_REDO) {
	    midend_stop_anim(me);
	    if (!midend_redo(me))
		goto done;
            *handled = true;
	} else if ((button == '\x13' || button == UI_SOLVE) &&
                   me->ourgame->can_solve) {
            *handled = true;
	    if (midend_solve(me))
		goto done;
	} else if (button == 'q' || button == 'Q' || button == '\x11' ||
                   button == UI_QUIT) {
	    ret = false;
            *handled = true;
	    goto done;
	} else
	    goto done;
    } else {
        *handled = true;
	if (movestr == UI_UPDATE)
	    s = me->states[me->statepos-1].state;
	else {
	    assert_printable_ascii(movestr);
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
            midend_set_timer(me);
            goto done;
        } else if (s) {
	    midend_stop_anim(me);
            midend_purge_states(me);
            ensure(me);
            assert(movestr != NULL);
            me->states[me->nstates].state = s;
            me->states[me->nstates].movestr = movestr;
            me->states[me->nstates].movetype = MOVE;
            me->statepos = ++me->nstates;
            me->dir = +1;
	    if (me->ui)
		me->ourgame->changed_state(me->ui,
					   me->states[me->statepos-2].state,
					   me->states[me->statepos-1].state);
        } else {
            goto done;
        }
    }

    if (!gottype)
        type = me->states[me->statepos-1].movetype;

    /*
     * See if this move requires an animation.
     */
    if (special(type) && !(type == SOLVE &&
			   (me->ourgame->flags & SOLVE_ANIMATES))) {
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

bool midend_process_key(midend *me, int x, int y, int button, bool *handled)
{
    bool ret = true, dummy_handled;

    if (handled == NULL) handled = &dummy_handled;
    *handled = false;
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
     *
     * A further addition: we translate certain keyboard presses to
     * cursor key 'select' buttons, so that a) frontends don't have
     * to translate these themselves (like they do for CURSOR_UP etc),
     * and b) individual games don't have to hard-code button presses
     * of '\n' etc for keyboard-based cursors. The choice of buttons
     * here could eventually be controlled by a runtime configuration
     * option.
     *
     * We also handle converting MOD_CTRL|'a' etc into '\x01' etc,
     * specially recognising Ctrl+Shift+Z, and stripping modifier
     * flags off keys that aren't meant to have them.
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
	if (me->ourgame->flags &
	    BUTTON_BEATS(me->pressed_mouse_button, button))
	    return ret;		       /* just ignore it */

        /*
         * Fabricate a button-up for the previously pressed button.
         */
        ret = ret && midend_really_process_key
            (me, x, y, (me->pressed_mouse_button +
                        (LEFT_RELEASE - LEFT_BUTTON)), handled);
    }

    /* Canonicalise CTRL+ASCII. */
    if ((button & MOD_CTRL) && (button & ~MOD_MASK) < 0x80)
        button = button & (0x1f | (MOD_MASK & ~MOD_CTRL));
    /* Special handling to make CTRL+SHFT+Z into REDO. */
    if ((button & (~MOD_MASK | MOD_SHFT)) == (MOD_SHFT | '\x1A'))
        button = UI_REDO;
    /* interpret_move() expects CTRL and SHFT only on cursor keys. */
    if (!IS_CURSOR_MOVE(button & ~MOD_MASK))
        button &= ~(MOD_CTRL | MOD_SHFT);
    /* ... and NUM_KEYPAD only on numbers. */
    if ((button & ~MOD_MASK) < '0' || (button & ~MOD_MASK) > '9')
        button &= ~MOD_NUM_KEYPAD;
    /*
     * Translate keyboard presses to cursor selection.
     */
    if (button == '\n' || button == '\r')
      button = CURSOR_SELECT;
    if (button == ' ')
      button = CURSOR_SELECT2;

    /*
     * Normalise both backspace characters (8 and 127) to \b. Easier
     * to do this once, here, than to require all front ends to
     * carefully generate the same one - now each front end can
     * generate whichever is easiest.
     */
    if (button == '\177')
	button = '\b';

    /*
     * Now send on the event we originally received.
     */
    ret = ret && midend_really_process_key(me, x, y, button, handled);

    /*
     * And update the currently pressed button.
     */
    if (IS_MOUSE_RELEASE(button))
        me->pressed_mouse_button = 0;
    else if (IS_MOUSE_DOWN(button))
        me->pressed_mouse_button = button;

    return ret;
}

key_label *midend_request_keys(midend *me, int *n)
{
    key_label *keys = NULL;
    int nkeys = 0, i;

    if(me->ourgame->request_keys)
    {
        keys = me->ourgame->request_keys(midend_get_params(me), &nkeys);
        for(i = 0; i < nkeys; ++i)
        {
            if(!keys[i].label)
                keys[i].label = button2label(keys[i].button);
        }
    }

    if(n)
        *n = nkeys;

    return keys;
}

/* Return a good label to show next to a key right now. */
const char *midend_current_key_label(midend *me, int button)
{
    assert(IS_CURSOR_SELECT(button));
    if (!me->ourgame->current_key_label) return "";
    return me->ourgame->current_key_label(
        me->ui, me->states[me->statepos-1].state, button);
}

void midend_redraw(midend *me)
{
    assert(me->drawing);

    if (me->statepos > 0 && me->drawstate) {
        bool first_draw = me->first_draw;
        me->first_draw = false;

        start_draw(me->drawing);

        if (first_draw) {
            /*
             * The initial contents of the window are not guaranteed
             * by the front end. But we also don't want to require
             * every single game to go to the effort of clearing the
             * window on setup. So we centralise here the operation of
             * covering the whole window with colour 0 (assumed to be
             * the puzzle's background colour) the first time we do a
             * redraw operation with a new drawstate.
             */
            draw_rect(me->drawing, 0, 0, me->winwidth, me->winheight, 0);
        }

        if (me->oldstate && me->anim_time > 0 &&
            me->anim_pos < me->anim_time) {
            assert(me->dir != 0);
            me->ourgame->redraw(me->drawing, me->drawstate, me->oldstate,
				me->states[me->statepos-1].state, me->dir,
				me->ui, me->anim_pos, me->flash_pos);
        } else {
            me->ourgame->redraw(me->drawing, me->drawstate, NULL,
				me->states[me->statepos-1].state, +1 /*shrug*/,
				me->ui, 0.0, me->flash_pos);
        }

        if (first_draw) {
            /*
             * Call a big draw_update on the whole window, in case the
             * game backend didn't.
             */
            draw_update(me->drawing, 0, 0, me->winwidth, me->winheight);
        }

        end_draw(me->drawing);
    }
}

/*
 * Nasty hacky function used to implement the --redo option in
 * gtk.c. Only used for generating the puzzles' icons.
 */
void midend_freeze_timer(midend *me, float tprop)
{
    me->anim_pos = me->anim_time * tprop;
    midend_redraw(me);
    deactivate_timer(me->frontend);
}

void midend_timer(midend *me, float tplus)
{
    bool need_redraw = (me->anim_time > 0 || me->flash_time > 0);

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

    if (need_redraw)
        midend_redraw(me);

    if (me->timing) {
	float oldelapsed = me->elapsed;
	me->elapsed += tplus;
	if ((int)oldelapsed != (int)me->elapsed)
	    status_bar(me->drawing, me->laststatus ? me->laststatus : "");
    }

    midend_set_timer(me);
}

float *midend_colours(midend *me, int *ncolours)
{
    float *ret;

    ret = me->ourgame->colours(me->frontend, ncolours);
    assert(*ncolours >= 1);

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
            int j, k;

            sprintf(buf, "%s_COLOUR_%d", me->ourgame->name, i);
            for (j = k = 0; buf[j]; j++)
		if (!isspace((unsigned char)buf[j]))
		    buf[k++] = toupper((unsigned char)buf[j]);
	    buf[k] = '\0';
            if ((e = getenv(buf)) != NULL &&
                sscanf(e, "%2x%2x%2x", &r, &g, &b) == 3) {
                ret[i*3 + 0] = r / 255.0F;
                ret[i*3 + 1] = g / 255.0F;
                ret[i*3 + 2] = b / 255.0F;
            }
            assert(0.0F <= ret[i*3 + 0] && ret[i*3 + 0] <= 1.0F);
            assert(0.0F <= ret[i*3 + 1] && ret[i*3 + 1] <= 1.0F);
            assert(0.0F <= ret[i*3 + 2] && ret[i*3 + 2] <= 1.0F);
        }
    }

    return ret;
}

struct preset_menu *preset_menu_new(void)
{
    struct preset_menu *menu = snew(struct preset_menu);
    menu->n_entries = 0;
    menu->entries_size = 0;
    menu->entries = NULL;
    return menu;
}

static struct preset_menu_entry *preset_menu_add(struct preset_menu *menu,
                                                 char *title)
{
    struct preset_menu_entry *toret;
    if (menu->n_entries >= menu->entries_size) {
        menu->entries_size = menu->n_entries * 5 / 4 + 10;
        menu->entries = sresize(menu->entries, menu->entries_size,
                                struct preset_menu_entry);
    }
    toret = &menu->entries[menu->n_entries++];
    toret->title = title;
    toret->params = NULL;
    toret->submenu = NULL;
    return toret;
}

struct preset_menu *preset_menu_add_submenu(struct preset_menu *parent,
                                            char *title)
{
    struct preset_menu_entry *entry = preset_menu_add(parent, title);
    entry->submenu = preset_menu_new();
    return entry->submenu;
}

void preset_menu_add_preset(struct preset_menu *parent,
                            char *title, game_params *params)
{
    struct preset_menu_entry *entry = preset_menu_add(parent, title);
    entry->params = params;
}

game_params *preset_menu_lookup_by_id(struct preset_menu *menu, int id)
{
    int i;
    game_params *retd;

    for (i = 0; i < menu->n_entries; i++) {
        if (id == menu->entries[i].id)
            return menu->entries[i].params;
        if (menu->entries[i].submenu &&
            (retd = preset_menu_lookup_by_id(
                 menu->entries[i].submenu, id)) != NULL)
            return retd;
    }

    return NULL;
}

static char *preset_menu_add_from_user_env(
    midend *me, struct preset_menu *menu, char *p, bool top_level)
{
    while (*p) {
        char *name, *val;
        game_params *preset;

        name = p;
        while (*p && *p != ':') p++;
        if (*p) *p++ = '\0';
        val = p;
        while (*p && *p != ':') p++;
        if (*p) *p++ = '\0';

        if (!strcmp(val, "#")) {
            /*
             * Special case: either open a new submenu with the given
             * title, or terminate the current submenu.
             */
            if (*name) {
                struct preset_menu *submenu =
                    preset_menu_add_submenu(menu, dupstr(name));
                p = preset_menu_add_from_user_env(me, submenu, p, false);
            } else {
                /*
                 * If we get a 'close submenu' indication at the top
                 * level, there's not much we can do but quietly
                 * ignore it.
                 */
                if (!top_level)
                    return p;
            }
            continue;
        }

        preset = me->ourgame->default_params();
        me->ourgame->decode_params(preset, val);

        if (me->ourgame->validate_params(preset, true)) {
            /* Drop this one from the list. */
            me->ourgame->free_params(preset);
            continue;
        }

        preset_menu_add_preset(menu, dupstr(name), preset);
    }

    return p;
}

static void preset_menu_alloc_ids(midend *me, struct preset_menu *menu)
{
    int i;

    for (i = 0; i < menu->n_entries; i++)
        menu->entries[i].id = me->n_encoded_presets++;

    for (i = 0; i < menu->n_entries; i++)
        if (menu->entries[i].submenu)
            preset_menu_alloc_ids(me, menu->entries[i].submenu);
}

static void preset_menu_encode_params(midend *me, struct preset_menu *menu)
{
    int i;

    for (i = 0; i < menu->n_entries; i++) {
        if (menu->entries[i].params) {
            me->encoded_presets[menu->entries[i].id] =
	        encode_params(me, menu->entries[i].params, true);
        } else {
            preset_menu_encode_params(me, menu->entries[i].submenu);
        }
    }
}

struct preset_menu *midend_get_presets(midend *me, int *id_limit)
{
    int i;

    if (me->preset_menu)
        return me->preset_menu;

#if 0
    /* Expect the game to implement exactly one of the two preset APIs */
    assert(me->ourgame->fetch_preset || me->ourgame->preset_menu);
    assert(!(me->ourgame->fetch_preset && me->ourgame->preset_menu));
#endif

    if (me->ourgame->fetch_preset) {
        char *name;
        game_params *preset;

        /* Simple one-level menu */
        assert(!me->ourgame->preset_menu);
        me->preset_menu = preset_menu_new();
        for (i = 0; me->ourgame->fetch_preset(i, &name, &preset); i++)
            preset_menu_add_preset(me->preset_menu, name, preset);

    } else {
        /* Hierarchical menu provided by the game backend */
        me->preset_menu = me->ourgame->preset_menu();
    }

    {
        /*
         * Allow user extensions to the preset list by defining an
         * environment variable <gamename>_PRESETS whose value is a
         * colon-separated list of items, alternating between textual
         * titles in the menu and encoded parameter strings. For
         * example, "SOLO_PRESETS=2x3 Advanced:2x3da" would define
         * just one additional preset for Solo.
         */
        char buf[80], *e;
        int j, k;

        sprintf(buf, "%s_PRESETS", me->ourgame->name);
	for (j = k = 0; buf[j]; j++)
	    if (!isspace((unsigned char)buf[j]))
		buf[k++] = toupper((unsigned char)buf[j]);
	buf[k] = '\0';

        if ((e = getenv(buf)) != NULL) {
            e = dupstr(e);
            preset_menu_add_from_user_env(me, me->preset_menu, e, true);
            sfree(e);
        }
    }

    /*
     * Finalise the menu: allocate an integer id to each entry, and
     * store string encodings of the presets' parameters in
     * me->encoded_presets.
     */
    me->n_encoded_presets = 0;
    preset_menu_alloc_ids(me, me->preset_menu);
    me->encoded_presets = snewn(me->n_encoded_presets, char *);
    for (i = 0; i < me->n_encoded_presets; i++)
        me->encoded_presets[i] = NULL;
    preset_menu_encode_params(me, me->preset_menu);

    if (id_limit)
        *id_limit = me->n_encoded_presets;
    return me->preset_menu;
}

int midend_which_preset(midend *me)
{
    char *encoding = encode_params(me, me->params, true);
    int i, ret;

    ret = -1;
    for (i = 0; i < me->n_encoded_presets; i++)
	if (me->encoded_presets[i] &&
            !strcmp(encoding, me->encoded_presets[i])) {
	    ret = i;
	    break;
	}

    sfree(encoding);
    return ret;
}

bool midend_wants_statusbar(midend *me)
{
    return me->ourgame->wants_statusbar;
}

void midend_request_id_changes(midend *me, void (*notify)(void *), void *ctx)
{
    me->game_id_change_notify_function = notify;
    me->game_id_change_notify_ctx = ctx;
}

bool midend_get_cursor_location(midend *me,
                                int *x_out, int *y_out,
                                int *w_out, int *h_out)
{
    int x, y, w, h;
    x = y = -1;
    w = h = 1;

    if(me->ourgame->get_cursor_location)
        me->ourgame->get_cursor_location(me->ui,
                                         me->drawstate,
                                         me->states[me->statepos-1].state,
                                         me->params,
                                         &x, &y, &w, &h);

    if(x == -1 && y == -1)
        return false;

    if(x_out)
        *x_out = x;
    if(y_out)
        *y_out = y;
    if(w_out)
        *w_out = w;
    if(h_out)
        *h_out = h;
    return true;
}

void midend_supersede_game_desc(midend *me, const char *desc,
                                const char *privdesc)
{
    /* Assert that the descriptions consists only of printable ASCII. */
    assert_printable_ascii(desc);
    if (privdesc)
	assert_printable_ascii(privdesc);
    sfree(me->desc);
    sfree(me->privdesc);
    me->desc = dupstr(desc);
    me->privdesc = privdesc ? dupstr(privdesc) : NULL;
    if (me->game_id_change_notify_function)
        me->game_id_change_notify_function(me->game_id_change_notify_ctx);
}

config_item *midend_get_config(midend *me, int which, char **wintitle)
{
    char *titlebuf, *parstr;
    const char *rest;
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
        /*
         * For CFG_DESC the text going in here will be a string
         * encoding of the restricted parameters, plus a colon,
         * plus the game description. For CFG_SEED it will be the
         * full parameters, plus a hash, plus the random seed data.
         * Either of these is a valid full game ID (although only
         * the former is likely to persist across many code
         * changes).
         */
        parstr = encode_params(me, me->curparams, which == CFG_SEED);
        assert(parstr);
        if (which == CFG_DESC) {
            rest = me->desc ? me->desc : "";
            sep = ':';
        } else {
            rest = me->seedstr ? me->seedstr : "";
            sep = '#';
        }
        ret[0].u.string.sval = snewn(strlen(parstr) + strlen(rest) + 2, char);
        sprintf(ret[0].u.string.sval, "%s%c%s", parstr, sep, rest);
        sfree(parstr);

	ret[1].type = C_END;
	ret[1].name = NULL;

	return ret;
    }

    assert(!"We shouldn't be here");
    return NULL;
}

static const char *midend_game_id_int(midend *me, const char *id, int defmode)
{
    const char *error;
    char *par = NULL;
    const char *desc, *seed;
    game_params *newcurparams, *newparams, *oldparams1, *oldparams2;
    bool free_params;

    seed = strchr(id, '#');
    desc = strchr(id, ':');

    if (desc && (!seed || desc < seed)) {
        /*
         * We have a colon separating parameters from game
         * description. So `par' now points to the parameters
         * string, and `desc' to the description string.
         */
        par = snewn(desc-id + 1, char);
        strncpy(par, id, desc-id);
        par[desc-id] = '\0';
        desc++;
        seed = NULL;
    } else if (seed && (!desc || seed < desc)) {
        /*
         * We have a hash separating parameters from random seed.
         * So `par' now points to the parameters string, and `seed'
         * to the seed string.
         */
        par = snewn(seed-id + 1, char);
        strncpy(par, id, seed-id);
        par[seed-id] = '\0';
        seed++;
        desc = NULL;
    } else {
        /*
         * We only have one string. Depending on `defmode', we take
         * it to be either parameters, seed or description.
         */
        if (defmode == DEF_SEED) {
            seed = id;
            par = NULL;
            desc = NULL;
        } else if (defmode == DEF_DESC) {
            desc = id;
            par = NULL;
            seed = NULL;
        } else {
            par = dupstr(id);
            seed = desc = NULL;
        }
    }

    /*
     * We must be reasonably careful here not to modify anything in
     * `me' until we have finished validating things. This function
     * must either return an error and do nothing to the midend, or
     * return success and do everything; nothing in between is
     * acceptable.
     */
    newcurparams = newparams = oldparams1 = oldparams2 = NULL;

    if (par) {
        /*
         * The params string may underspecify the game parameters, so
         * we must first initialise newcurparams with a full set of
         * params from somewhere else before we decode_params the
         * input string over the top.
         *
         * But which set? It depends on what other data we have.
         *
         * If we've been given a _descriptive_ game id, then that may
         * well underspecify by design, e.g. Solo game descriptions
         * often start just '3x3:' without specifying one of Solo's
         * difficulty settings, because it isn't necessary once a game
         * has been generated (and you might not even know it, if
         * you're manually transcribing a game description). In that
         * situation, I've always felt that the best thing to set the
         * difficulty to (for use if the user hits 'New Game' after
         * pasting in that game id) is whatever it was previously set
         * to. That is, we use whatever is already in me->params as
         * the basis for our decoding of this input string.
         *
         * A random-seed based game id, however, should use the real,
         * built-in default params, and not even check the
         * <game>_DEFAULT environment setting, because when people
         * paste each other random seeds - whether it's two users
         * arranging to generate the same game at the same time to
         * race solving them, or a user sending a bug report upstream
         * - the whole point is for the random game id to always be
         * interpreted the same way, even if it does underspecify.
         *
         * A parameter string typed in on its own, with no seed _or_
         * description, gets treated the same way as a random seed,
         * because again I think the most likely reason for doing that
         * is to have a portable representation of a set of params.
         */
        if (desc) {
            newcurparams = me->ourgame->dup_params(me->params);
        } else {
            newcurparams = me->ourgame->default_params();
        }
        me->ourgame->decode_params(newcurparams, par);
        sfree(par);
        error = me->ourgame->validate_params(newcurparams, desc == NULL);
        if (error) {
            me->ourgame->free_params(newcurparams);
            return error;
        }
        oldparams1 = me->curparams;

        /*
         * Now filter only the persistent parts of this state into
         * the long-term params structure, unless we've _only_
         * received a params string in which case the whole lot is
         * persistent.
         */
        oldparams2 = me->params;
        if (seed || desc) {
            char *tmpstr;

            newparams = me->ourgame->dup_params(me->params);

            tmpstr = encode_params(me, newcurparams, false);
            me->ourgame->decode_params(newparams, tmpstr);

            sfree(tmpstr);
        } else {
            newparams = me->ourgame->dup_params(newcurparams);
        }
        free_params = true;
    } else {
        newcurparams = me->curparams;
        newparams = me->params;
        free_params = false;
    }

    if (desc) {
        error = me->ourgame->validate_desc(newparams, desc);
        if (error) {
            if (free_params) {
                if (newcurparams)
                    me->ourgame->free_params(newcurparams);
                if (newparams)
                    me->ourgame->free_params(newparams);
            }
            return error;
        }
    }

    /*
     * Now we've got past all possible error points. Update the
     * midend itself.
     */
    me->params = newparams;
    me->curparams = newcurparams;
    if (oldparams1)
        me->ourgame->free_params(oldparams1);
    if (oldparams2)
        me->ourgame->free_params(oldparams2);

    sfree(me->desc);
    sfree(me->privdesc);
    me->desc = me->privdesc = NULL;
    sfree(me->seedstr);
    me->seedstr = NULL;

    if (desc) {
        me->desc = dupstr(desc);
        me->genmode = GOT_DESC;
        sfree(me->aux_info);
	me->aux_info = NULL;
    }

    if (seed) {
        me->seedstr = dupstr(seed);
        me->genmode = GOT_SEED;
    }

    me->newgame_can_store_undo = false;

    return NULL;
}

const char *midend_game_id(midend *me, const char *id)
{
    return midend_game_id_int(me, id, DEF_PARAMS);
}

char *midend_get_game_id(midend *me)
{
    char *parstr, *ret;

    parstr = encode_params(me, me->curparams, false);
    assert(parstr);
    assert(me->desc);
    ret = snewn(strlen(parstr) + strlen(me->desc) + 2, char);
    sprintf(ret, "%s:%s", parstr, me->desc);
    sfree(parstr);
    return ret;
}

char *midend_get_random_seed(midend *me)
{
    char *parstr, *ret;

    if (!me->seedstr)
        return NULL;

    parstr = encode_params(me, me->curparams, true);
    assert(parstr);
    ret = snewn(strlen(parstr) + strlen(me->seedstr) + 2, char);
    sprintf(ret, "%s#%s", parstr, me->seedstr);
    sfree(parstr);
    return ret;
}

const char *midend_set_config(midend *me, int which, config_item *cfg)
{
    const char *error;
    game_params *params;

    switch (which) {
      case CFG_SETTINGS:
	params = me->ourgame->custom_params(cfg);
	error = me->ourgame->validate_params(params, true);

	if (error) {
	    me->ourgame->free_params(params);
	    return error;
	}

	me->ourgame->free_params(me->params);
	me->params = params;
	break;

      case CFG_SEED:
      case CFG_DESC:
        error = midend_game_id_int(me, cfg[0].u.string.sval,
                                   (which == CFG_SEED ? DEF_SEED : DEF_DESC));
	if (error)
	    return error;
	break;
    }

    return NULL;
}

bool midend_can_format_as_text_now(midend *me)
{
    if (me->ourgame->can_format_as_text_ever)
	return me->ourgame->can_format_as_text_now(me->params);
    else
	return false;
}

char *midend_text_format(midend *me)
{
    if (me->ourgame->can_format_as_text_ever && me->statepos > 0 &&
	me->ourgame->can_format_as_text_now(me->params))
	return me->ourgame->text_format(me->states[me->statepos-1].state);
    else
	return NULL;
}

const char *midend_solve(midend *me)
{
    game_state *s;
    const char *msg;
    char *movestr;

    if (!me->ourgame->can_solve)
	return "This game does not support the Solve operation";

    if (me->statepos < 1)
	return "No game set up to solve";   /* _shouldn't_ happen! */

    msg = NULL;
    movestr = me->ourgame->solve(me->states[0].state,
				 me->states[me->statepos-1].state,
				 me->aux_info, &msg);
    assert(movestr != UI_UPDATE);
    if (!movestr) {
	if (!msg)
	    msg = "Solve operation failed";   /* _shouldn't_ happen, but can */
	return msg;
    }
    assert_printable_ascii(movestr);
    s = me->ourgame->execute_move(me->states[me->statepos-1].state, movestr);
    assert(s);

    /*
     * Now enter the solved state as the next move.
     */
    midend_stop_anim(me);
    midend_purge_states(me);
    ensure(me);
    me->states[me->nstates].state = s;
    me->states[me->nstates].movestr = movestr;
    me->states[me->nstates].movetype = SOLVE;
    me->statepos = ++me->nstates;
    if (me->ui)
        me->ourgame->changed_state(me->ui,
                                   me->states[me->statepos-2].state,
                                   me->states[me->statepos-1].state);
    me->dir = +1;
    if (me->ourgame->flags & SOLVE_ANIMATES) {
	me->oldstate = me->ourgame->dup_game(me->states[me->statepos-2].state);
        me->anim_time =
	    me->ourgame->anim_length(me->states[me->statepos-2].state,
				     me->states[me->statepos-1].state,
				     +1, me->ui);
        me->anim_pos = 0.0;
    } else {
	me->anim_time = 0.0;
	midend_finish_move(me);
    }
    if (me->drawing)
        midend_redraw(me);
    midend_set_timer(me);
    return NULL;
}

int midend_status(midend *me)
{
    /*
     * We should probably never be called when the state stack has no
     * states on it at all - ideally, midends should never be left in
     * that state for long enough to get put down and forgotten about.
     * But if we are, I think we return _true_ - pedantically speaking
     * a midend in that state is 'vacuously solved', and more
     * practically, a user whose midend has been left in that state
     * probably _does_ want the 'new game' option to be prominent.
     */
    if (me->statepos == 0)
        return +1;

    return me->ourgame->status(me->states[me->statepos-1].state);
}

char *midend_rewrite_statusbar(midend *me, const char *text)
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

	sec = (int)me->elapsed;
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

void midend_serialise(midend *me,
                      void (*write)(void *ctx, const void *buf, int len),
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
    const char *str = (s); \
    char lbuf[9];                               \
    copy_left_justified(lbuf, sizeof(lbuf), h); \
    sprintf(hbuf, "%s:%d:", lbuf, (int)strlen(str)); \
    assert_printable_ascii(hbuf); \
    write(wctx, hbuf, strlen(hbuf)); \
    assert_printable_ascii(str); \
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
        char *s = encode_params(me, me->params, true);
        wr("PARAMS", s);
        sfree(s);
    }

    /*
     * The current short-term parameters structure, in full.
     */
    if (me->curparams) {
        char *s = encode_params(me, me->curparams, true);
        wr("CPARAMS", s);
        sfree(s);
    }

    /*
     * The current game description, the privdesc, and the random seed.
     */
    if (me->seedstr) {
        /*
         * Random seeds are not necessarily printable ASCII.
         * Hex-encode the seed if necessary.  Printable ASCII seeds
         * are emitted unencoded for compatibility with older
         * versions.
         */
        int i;

        for (i = 0; me->seedstr[i]; i++)
            if (me->seedstr[i] < 32 || me->seedstr[i] >= 127)
                break;
        if (me->seedstr[i]) {
            char *hexseed = bin2hex((unsigned char *)me->seedstr,
                                    strlen(me->seedstr));

            wr("HEXSEED", hexseed);
            sfree(hexseed);
        } else
            wr("SEED", me->seedstr);
    }
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
        obfuscate_bitmap(s1, len*8, false);
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
        assert(me->statepos >= 1 && me->statepos <= me->nstates);
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
 * Internal version of midend_deserialise, taking an extra check
 * function to be called just before beginning to install things in
 * the midend.
 *
 * Like midend_deserialise proper, this function returns NULL on
 * success, or an error message.
 */
static const char *midend_deserialise_internal(
    midend *me, bool (*read)(void *ctx, void *buf, int len), void *rctx,
    const char *(*check)(void *ctx, midend *, const struct deserialise_data *),
    void *cctx)
{
    struct deserialise_data data;
    int gotstates = 0;
    bool started = false;
    int i;

    char *val = NULL;
    /* Initially all errors give the same report */
    const char *ret = "Data does not appear to be a saved game file";

    data.seed = data.parstr = data.desc = data.privdesc = NULL;
    data.auxinfo = data.uistr = data.cparstr = NULL;
    data.elapsed = 0.0F;
    data.params = data.cparams = NULL;
    data.ui = NULL;
    data.states = NULL;
    data.nstates = 0;
    data.statepos = -1;

    /*
     * Loop round and round reading one key/value pair at a time
     * from the serialised stream, until we have enough game states
     * to finish.
     */
    while (data.nstates <= 0 || data.statepos < 0 ||
           gotstates < data.nstates-1) {
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
	    goto cleanup;
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
            } else if (c >= '0' && c <= '9' && len < (INT_MAX - 10) / 10) {
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
            /* unexpected EOF */
            goto cleanup;
        }
        val[len] = '\0';
        /* Validate that all values (apart from SEED) are printable ASCII. */
        if (strcmp(key, "SEED"))
            for (i = 0; val[i]; i++)
                if (val[i] < 32 || val[i] >= 127) {
                    ret = "Forbidden characters in saved game file";
                    goto cleanup;
                }

        if (!started) {
            if (strcmp(key, "SAVEFILE") || strcmp(val, SERIALISE_MAGIC)) {
                /* ret already has the right message in it */
                goto cleanup;
            }
            /* Now most errors are this one, unless otherwise specified */
            ret = "Saved data ended unexpectedly";
            started = true;
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
                sfree(data.parstr);
                data.parstr = val;
                val = NULL;
            } else if (!strcmp(key, "CPARAMS")) {
                sfree(data.cparstr);
                data.cparstr = val;
                val = NULL;
            } else if (!strcmp(key, "HEXSEED")) {
                unsigned char *tmp;
                int len = strlen(val) / 2;   /* length in bytes */
                tmp = hex2bin(val, len);
                sfree(data.seed);
                data.seed = snewn(len + 1, char);
                memcpy(data.seed, tmp, len);
                data.seed[len] = '\0';
                sfree(tmp);
            } else if (!strcmp(key, "SEED")) {
                sfree(data.seed);
                data.seed = val;
                val = NULL;
            } else if (!strcmp(key, "DESC")) {
                sfree(data.desc);
                data.desc = val;
                val = NULL;
            } else if (!strcmp(key, "PRIVDESC")) {
                sfree(data.privdesc);
                data.privdesc = val;
                val = NULL;
            } else if (!strcmp(key, "AUXINFO")) {
                unsigned char *tmp;
                int len = strlen(val) / 2;   /* length in bytes */
                tmp = hex2bin(val, len);
                obfuscate_bitmap(tmp, len*8, true);

                sfree(data.auxinfo);
                data.auxinfo = snewn(len + 1, char);
                memcpy(data.auxinfo, tmp, len);
                data.auxinfo[len] = '\0';
                sfree(tmp);
            } else if (!strcmp(key, "UI")) {
                sfree(data.uistr);
                data.uistr = val;
                val = NULL;
            } else if (!strcmp(key, "TIME")) {
                data.elapsed = (float)atof(val);
            } else if (!strcmp(key, "NSTATES")) {
                if (data.states) {
                    ret = "Two state counts provided in save file";
                    goto cleanup;
                }
                data.nstates = atoi(val);
                if (data.nstates <= 0) {
                    ret = "Number of states in save file was negative";
                    goto cleanup;
                }
                data.states = snewn(data.nstates, struct midend_state_entry);
                for (i = 0; i < data.nstates; i++) {
                    data.states[i].state = NULL;
                    data.states[i].movestr = NULL;
                    data.states[i].movetype = NEWGAME;
                }
            } else if (!strcmp(key, "STATEPOS")) {
                data.statepos = atoi(val);
            } else if (!strcmp(key, "MOVE") ||
                       !strcmp(key, "SOLVE") ||
                       !strcmp(key, "RESTART")) {
                if (!data.states) {
                    ret = "No state count provided in save file";
                    goto cleanup;
                }
                if (data.statepos < 0) {
                    ret = "No game position provided in save file";
                    goto cleanup;
                }
                gotstates++;
                assert(gotstates < data.nstates);
                if (!strcmp(key, "MOVE"))
                    data.states[gotstates].movetype = MOVE;
                else if (!strcmp(key, "SOLVE"))
                    data.states[gotstates].movetype = SOLVE;
                else
                    data.states[gotstates].movetype = RESTART;
                data.states[gotstates].movestr = val;
                val = NULL;
            }
        }

        sfree(val);
        val = NULL;
    }

    data.params = me->ourgame->default_params();
    if (!data.parstr) {
        ret = "Long-term parameters in save file are missing";
        goto cleanup;
    }
    me->ourgame->decode_params(data.params, data.parstr);
    if (me->ourgame->validate_params(data.params, true)) {
        ret = "Long-term parameters in save file are invalid";
        goto cleanup;
    }
    data.cparams = me->ourgame->default_params();
    if (!data.cparstr) {
        ret = "Short-term parameters in save file are missing";
        goto cleanup;
    }
    me->ourgame->decode_params(data.cparams, data.cparstr);
    if (me->ourgame->validate_params(data.cparams, false)) {
        ret = "Short-term parameters in save file are invalid";
        goto cleanup;
    }
    if (data.seed && me->ourgame->validate_params(data.cparams, true)) {
        /*
         * The seed's no use with this version, but we can perfectly
         * well use the rest of the data.
         */
        sfree(data.seed);
        data.seed = NULL;
    }
    if (!data.desc) {
        ret = "Game description in save file is missing";
        goto cleanup;
    } else if (me->ourgame->validate_desc(data.cparams, data.desc)) {
        ret = "Game description in save file is invalid";
        goto cleanup;
    }
    if (data.privdesc &&
        me->ourgame->validate_desc(data.cparams, data.privdesc)) {
        ret = "Game private description in save file is invalid";
        goto cleanup;
    }
    if (data.statepos < 1 || data.statepos > data.nstates) {
        ret = "Game position in save file is out of range";
        goto cleanup;
    }

    if (!data.states) {
        ret = "No state count provided in save file";
        goto cleanup;
    }
    data.states[0].state = me->ourgame->new_game(
        me, data.cparams, data.privdesc ? data.privdesc : data.desc);

    for (i = 1; i < data.nstates; i++) {
        assert(data.states[i].movetype != NEWGAME);
        switch (data.states[i].movetype) {
          case MOVE:
          case SOLVE:
            data.states[i].state = me->ourgame->execute_move(
                data.states[i-1].state, data.states[i].movestr);
            if (data.states[i].state == NULL) {
                ret = "Save file contained an invalid move";
                goto cleanup;
            }
            break;
          case RESTART:
            if (me->ourgame->validate_desc(
                    data.cparams, data.states[i].movestr)) {
                ret = "Save file contained an invalid restart move";
                goto cleanup;
            }
            data.states[i].state = me->ourgame->new_game(
                me, data.cparams, data.states[i].movestr);
            break;
        }
    }

    data.ui = me->ourgame->new_ui(data.states[0].state);
    if (data.uistr)
        me->ourgame->decode_ui(data.ui, data.uistr);

    /*
     * Run the externally provided check function, and abort if it
     * returns an error message.
     */
    if (check && (ret = check(cctx, me, &data)) != NULL)
        goto cleanup;            /* error message is already in ret */

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
        me->desc = data.desc;
        data.desc = tmp;

        tmp = me->privdesc;
        me->privdesc = data.privdesc;
        data.privdesc = tmp;

        tmp = me->seedstr;
        me->seedstr = data.seed;
        data.seed = tmp;

        tmp = me->aux_info;
        me->aux_info = data.auxinfo;
        data.auxinfo = tmp;
    }

    me->genmode = GOT_NOTHING;

    me->statesize = data.nstates;
    data.nstates = me->nstates;
    me->nstates = me->statesize;
    {
        struct midend_state_entry *tmp;
        tmp = me->states;
        me->states = data.states;
        data.states = tmp;
    }
    me->statepos = data.statepos;

    /*
     * Don't save the "new game undo/redo" state.  So "new game" twice or
     * (in some environments) switching away and back, will make a
     * "new game" irreversible.  Maybe in the future we will have a
     * more sophisticated way to decide when to discard the previous
     * game state.
     */
    me->newgame_undo.len = 0;
    me->newgame_redo.len = 0;

    {
        game_params *tmp;

        tmp = me->params;
        me->params = data.params;
        data.params = tmp;

        tmp = me->curparams;
        me->curparams = data.cparams;
        data.cparams = tmp;
    }

    me->oldstate = NULL;
    me->anim_time = me->anim_pos = me->flash_time = me->flash_pos = 0.0F;
    me->dir = 0;

    {
        game_ui *tmp;

        tmp = me->ui;
        me->ui = data.ui;
        data.ui = tmp;
    }

    me->elapsed = data.elapsed;
    me->pressed_mouse_button = 0;

    midend_set_timer(me);

    if (me->drawstate)
        me->ourgame->free_drawstate(me->drawing, me->drawstate);
    me->drawstate =
        me->ourgame->new_drawstate(me->drawing,
				   me->states[me->statepos-1].state);
    me->first_draw = true;
    midend_size_new_drawstate(me);
    if (me->game_id_change_notify_function)
        me->game_id_change_notify_function(me->game_id_change_notify_ctx);

    ret = NULL;                        /* success! */

    cleanup:
    sfree(val);
    sfree(data.seed);
    sfree(data.parstr);
    sfree(data.cparstr);
    sfree(data.desc);
    sfree(data.privdesc);
    sfree(data.auxinfo);
    sfree(data.uistr);
    if (data.params)
        me->ourgame->free_params(data.params);
    if (data.cparams)
        me->ourgame->free_params(data.cparams);
    if (data.ui)
        me->ourgame->free_ui(data.ui);
    if (data.states) {
        int i;

        for (i = 0; i < data.nstates; i++) {
            if (data.states[i].state)
                me->ourgame->free_game(data.states[i].state);
            sfree(data.states[i].movestr);
        }
        sfree(data.states);
    }

    return ret;
}

const char *midend_deserialise(
    midend *me, bool (*read)(void *ctx, void *buf, int len), void *rctx)
{
    return midend_deserialise_internal(me, read, rctx, NULL, NULL);
}

/*
 * This function examines a saved game file just far enough to
 * determine which game type it contains. It returns NULL on success
 * and the game name string in 'name' (which will be dynamically
 * allocated and should be caller-freed), or an error message on
 * failure.
 */
const char *identify_game(char **name,
                          bool (*read)(void *ctx, void *buf, int len),
                          void *rctx)
{
    int nstates = 0, statepos = -1, gotstates = 0;
    bool started = false;

    char *val = NULL;
    /* Initially all errors give the same report */
    const char *ret = "Data does not appear to be a saved game file";

    *name = NULL;

    /*
     * Loop round and round reading one key/value pair at a time from
     * the serialised stream, until we've found the game name.
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
	    goto cleanup;
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
            } else if (c >= '0' && c <= '9' && len < (INT_MAX - 10) / 10) {
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
            /* unexpected EOF */
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
            started = true;
        } else {
            if (!strcmp(key, "VERSION")) {
                if (strcmp(val, SERIALISE_VERSION)) {
                    ret = "Cannot handle this version of the saved game"
                        " file format";
                    goto cleanup;
                }
            } else if (!strcmp(key, "GAME")) {
                *name = dupstr(val);
                ret = NULL;
                goto cleanup;
            }
        }

        sfree(val);
        val = NULL;
    }

    cleanup:
    sfree(val);
    return ret;
}

const char *midend_print_puzzle(midend *me, document *doc, bool with_soln)
{
    game_state *soln = NULL;

    if (me->statepos < 1)
	return "No game set up to print";/* _shouldn't_ happen! */

    if (with_soln) {
	const char *msg;
        char *movestr;

	if (!me->ourgame->can_solve)
	    return "This game does not support the Solve operation";

	msg = "Solve operation failed";/* game _should_ overwrite on error */
	movestr = me->ourgame->solve(me->states[0].state,
				     me->states[me->statepos-1].state,
				     me->aux_info, &msg);
	if (!movestr)
	    return msg;
	soln = me->ourgame->execute_move(me->states[me->statepos-1].state,
					 movestr);
	assert(soln);

	sfree(movestr);
    } else
	soln = NULL;

    /*
     * This call passes over ownership of the two game_states and
     * the game_params. Hence we duplicate the ones we want to
     * keep, and we don't have to bother freeing soln if it was
     * non-NULL.
     */
    document_add_puzzle(doc, me->ourgame,
			me->ourgame->dup_params(me->curparams),
			me->ourgame->dup_game(me->states[0].state), soln);

    return NULL;
}
