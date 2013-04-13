/*
 * fifteen.c: standard 15-puzzle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define PREFERRED_TILE_SIZE 48
#define TILE_SIZE (ds->tilesize)
#define BORDER    (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define ANIM_TIME 0.13F
#define FLASH_FRAME 0.13F

#define X(state, i) ( (i) % (state)->w )
#define Y(state, i) ( (i) / (state)->w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    NCOLOURS
};

struct game_params {
    int w, h;
};

struct game_state {
    int w, h, n;
    int *tiles;
    int gap_pos;
    int completed;
    int used_solve;		       /* used to suppress completion flash */
    int movecount;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 4;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    if (i == 0) {
	*params = default_params();
	*name = dupstr("4x4");
	return TRUE;
    }
    return FALSE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
    }
}

static char *encode_params(const game_params *params, int full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->w < 2 || params->h < 2)
	return "Width and height must both be at least two";

    return NULL;
}

static int perm_parity(int *perm, int n)
{
    int i, j, ret;

    ret = 0;

    for (i = 0; i < n-1; i++)
        for (j = i+1; j < n; j++)
            if (perm[i] > perm[j])
                ret = !ret;

    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int gap, n, i, x;
    int x1, x2, p1, p2, parity;
    int *tiles, *used;
    char *ret;
    int retlen;

    n = params->w * params->h;

    tiles = snewn(n, int);
    used = snewn(n, int);

    for (i = 0; i < n; i++) {
        tiles[i] = -1;
        used[i] = FALSE;
    }

    gap = random_upto(rs, n);
    tiles[gap] = 0;
    used[0] = TRUE;

    /*
     * Place everything else except the last two tiles.
     */
    for (x = 0, i = n-1; i > 2; i--) {
        int k = random_upto(rs, i);
        int j;

        for (j = 0; j < n; j++)
            if (!used[j] && (k-- == 0))
                break;

        assert(j < n && !used[j]);
        used[j] = TRUE;

        while (tiles[x] >= 0)
            x++;
        assert(x < n);
        tiles[x] = j;
    }

    /*
     * Find the last two locations, and the last two pieces.
     */
    while (tiles[x] >= 0)
        x++;
    assert(x < n);
    x1 = x;
    x++;
    while (tiles[x] >= 0)
        x++;
    assert(x < n);
    x2 = x;

    for (i = 0; i < n; i++)
        if (!used[i])
            break;
    p1 = i;
    for (i = p1+1; i < n; i++)
        if (!used[i])
            break;
    p2 = i;

    /*
     * Determine the required parity of the overall permutation.
     * This is the XOR of:
     * 
     * 	- The chessboard parity ((x^y)&1) of the gap square. The
     * 	  bottom right counts as even.
     * 
     *  - The parity of n. (The target permutation is 1,...,n-1,0
     *    rather than 0,...,n-1; this is a cyclic permutation of
     *    the starting point and hence is odd iff n is even.)
     */
    parity = ((X(params, gap) - (params->w-1)) ^
	      (Y(params, gap) - (params->h-1)) ^
	      (n+1)) & 1;

    /*
     * Try the last two tiles one way round. If that fails, swap
     * them.
     */
    tiles[x1] = p1;
    tiles[x2] = p2;
    if (perm_parity(tiles, n) != parity) {
        tiles[x1] = p2;
        tiles[x2] = p1;
        assert(perm_parity(tiles, n) == parity);
    }

    /*
     * Now construct the game description, by describing the tile
     * array as a simple sequence of comma-separated integers.
     */
    ret = NULL;
    retlen = 0;
    for (i = 0; i < n; i++) {
        char buf[80];
        int k;

        k = sprintf(buf, "%d,", tiles[i]);

        ret = sresize(ret, retlen + k + 1, char);
        strcpy(ret + retlen, buf);
        retlen += k;
    }
    ret[retlen-1] = '\0';              /* delete last comma */

    sfree(tiles);
    sfree(used);

    return ret;
}

static char *validate_desc(const game_params *params, const char *desc)
{
    const char *p;
    char *err;
    int i, area;
    int *used;

    area = params->w * params->h;
    p = desc;
    err = NULL;

    used = snewn(area, int);
    for (i = 0; i < area; i++)
	used[i] = FALSE;

    for (i = 0; i < area; i++) {
	const char *q = p;
	int n;

	if (*p < '0' || *p > '9') {
	    err = "Not enough numbers in string";
	    goto leave;
	}
	while (*p >= '0' && *p <= '9')
	    p++;
	if (i < area-1 && *p != ',') {
	    err = "Expected comma after number";
	    goto leave;
	}
	else if (i == area-1 && *p) {
	    err = "Excess junk at end of string";
	    goto leave;
	}
	n = atoi(q);
	if (n < 0 || n >= area) {
	    err = "Number out of range";
	    goto leave;
	}
	if (used[n]) {
	    err = "Number used twice";
	    goto leave;
	}
	used[n] = TRUE;

	if (*p) p++;		       /* eat comma */
    }

    leave:
    sfree(used);
    return err;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int i;
    const char *p;

    state->w = params->w;
    state->h = params->h;
    state->n = params->w * params->h;
    state->tiles = snewn(state->n, int);

    state->gap_pos = 0;
    p = desc;
    i = 0;
    for (i = 0; i < state->n; i++) {
        assert(*p);
        state->tiles[i] = atoi(p);
        if (state->tiles[i] == 0)
            state->gap_pos = i;
        while (*p && *p != ',')
            p++;
        if (*p) p++;                   /* eat comma */
    }
    assert(!*p);
    assert(state->tiles[state->gap_pos] == 0);

    state->completed = state->movecount = 0;
    state->used_solve = FALSE;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->tiles = snewn(state->w * state->h, int);
    memcpy(ret->tiles, state->tiles, state->w * state->h * sizeof(int));
    ret->gap_pos = state->gap_pos;
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->used_solve = state->used_solve;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
    return dupstr("S");
}

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    char *ret, *p, buf[80];
    int x, y, col, maxlen;

    /*
     * First work out how many characters we need to display each
     * number.
     */
    col = sprintf(buf, "%d", state->n-1);

    /*
     * Now we know the exact total size of the grid we're going to
     * produce: it's got h rows, each containing w lots of col, w-1
     * spaces and a trailing newline.
     */
    maxlen = state->h * state->w * (col+1);

    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->h; y++) {
	for (x = 0; x < state->w; x++) {
	    int v = state->tiles[state->w*y+x];
	    if (v == 0)
		sprintf(buf, "%*s", col, "");
	    else
		sprintf(buf, "%*d", col, v);
	    memcpy(p, buf, col);
	    p += col;
	    if (x+1 == state->w)
		*p++ = '\n';
	    else
		*p++ = ' ';
	}
    }

    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

static game_ui *new_ui(const game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    int started;
    int w, h, bgcolour;
    int *tiles;
    int tilesize;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gx, gy, dx, dy;
    char buf[80];

    button &= ~MOD_MASK;

    gx = X(state, state->gap_pos);
    gy = Y(state, state->gap_pos);

    if (button == CURSOR_RIGHT && gx > 0)
        dx = gx - 1, dy = gy;
    else if (button == CURSOR_LEFT && gx < state->w-1)
        dx = gx + 1, dy = gy;
    else if (button == CURSOR_DOWN && gy > 0)
        dy = gy - 1, dx = gx;
    else if (button == CURSOR_UP && gy < state->h-1)
        dy = gy + 1, dx = gx;
    else if (button == LEFT_BUTTON) {
        dx = FROMCOORD(x);
        dy = FROMCOORD(y);
        if (dx < 0 || dx >= state->w || dy < 0 || dy >= state->h)
            return NULL;               /* out of bounds */
        /*
         * Any click location should be equal to the gap location
         * in _precisely_ one coordinate.
         */
        if ((dx == gx && dy == gy) || (dx != gx && dy != gy))
            return NULL;
    } else
        return NULL;                   /* no move */

    sprintf(buf, "M%d,%d", dx, dy);
    return dupstr(buf);
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int gx, gy, dx, dy, ux, uy, up, p;
    game_state *ret;

    if (!strcmp(move, "S")) {
	int i;

	ret = dup_game(from);

	/*
	 * Simply replace the grid with a solved one. For this game,
	 * this isn't a useful operation for actually telling the user
	 * what they should have done, but it is useful for
	 * conveniently being able to get hold of a clean state from
	 * which to practise manoeuvres.
	 */
	for (i = 0; i < ret->n; i++)
	    ret->tiles[i] = (i+1) % ret->n;
	ret->gap_pos = ret->n-1;
	ret->used_solve = TRUE;
	ret->completed = ret->movecount = 1;

	return ret;
    }

    gx = X(from, from->gap_pos);
    gy = Y(from, from->gap_pos);

    if (move[0] != 'M' ||
	sscanf(move+1, "%d,%d", &dx, &dy) != 2 ||
	(dx == gx && dy == gy) || (dx != gx && dy != gy) ||
	dx < 0 || dx >= from->w || dy < 0 || dy >= from->h)
	return NULL;

    /*
     * Find the unit displacement from the original gap
     * position towards this one.
     */
    ux = (dx < gx ? -1 : dx > gx ? +1 : 0);
    uy = (dy < gy ? -1 : dy > gy ? +1 : 0);
    up = C(from, ux, uy);

    ret = dup_game(from);

    ret->gap_pos = C(from, dx, dy);
    assert(ret->gap_pos >= 0 && ret->gap_pos < ret->n);

    ret->tiles[ret->gap_pos] = 0;

    for (p = from->gap_pos; p != ret->gap_pos; p += up) {
        assert(p >= 0 && p < from->n);
        ret->tiles[p] = from->tiles[p + up];
	ret->movecount++;
    }

    /*
     * See if the game has been completed.
     */
    if (!ret->completed) {
        ret->completed = ret->movecount;
        for (p = 0; p < ret->n; p++)
            if (ret->tiles[p] != (p < ret->n-1 ? p+1 : 0))
                ret->completed = 0;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++)
        ret[COL_TEXT * 3 + i] = 0.0;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->bgcolour = COL_BACKGROUND;
    ds->tiles = snewn(ds->w*ds->h, int);
    ds->tilesize = 0;                  /* haven't decided yet */
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, int tile, int flash_colour)
{
    if (tile == 0) {
        draw_rect(dr, x, y, TILE_SIZE, TILE_SIZE,
                  flash_colour);
    } else {
        int coords[6];
        char str[40];

        coords[0] = x + TILE_SIZE - 1;
        coords[1] = y + TILE_SIZE - 1;
        coords[2] = x + TILE_SIZE - 1;
        coords[3] = y;
        coords[4] = x;
        coords[5] = y + TILE_SIZE - 1;
        draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);

        coords[0] = x;
        coords[1] = y;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);

        draw_rect(dr, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
                  TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
                  flash_colour);

        sprintf(str, "%d", tile);
        draw_text(dr, x + TILE_SIZE/2, y + TILE_SIZE/2,
                  FONT_VARIABLE, TILE_SIZE/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
                  COL_TEXT, str);
    }
    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, pass, bgcolour;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
        bgcolour = COL_BACKGROUND;

    if (!ds->started) {
        int coords[10];

	draw_rect(dr, 0, 0,
		  TILE_SIZE * state->w + 2 * BORDER,
		  TILE_SIZE * state->h + 2 * BORDER, COL_BACKGROUND);
	draw_update(dr, 0, 0,
		    TILE_SIZE * state->w + 2 * BORDER,
		    TILE_SIZE * state->h + 2 * BORDER);

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[4] = coords[2] - TILE_SIZE;
        coords[5] = coords[3] + TILE_SIZE;
        coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[9] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[6] = coords[8] + TILE_SIZE;
        coords[7] = coords[9] - TILE_SIZE;
        draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

        coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
        draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);

        ds->started = TRUE;
    }

    /*
     * Now draw each tile. We do this in two passes to make
     * animation easy.
     */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < state->n; i++) {
            int t, t0;
            /*
             * Figure out what should be displayed at this
             * location. It's either a simple tile, or it's a
             * transition between two tiles (in which case we say
             * -1 because it must always be drawn).
             */

            if (oldstate && oldstate->tiles[i] != state->tiles[i])
                t = -1;
            else
                t = state->tiles[i];

            t0 = t;

            if (ds->bgcolour != bgcolour ||   /* always redraw when flashing */
                ds->tiles[i] != t || ds->tiles[i] == -1 || t == -1) {
                int x, y;

                /*
                 * Figure out what to _actually_ draw, and where to
                 * draw it.
                 */
                if (t == -1) {
                    int x0, y0, x1, y1;
                    int j;

                    /*
                     * On the first pass, just blank the tile.
                     */
                    if (pass == 0) {
                        x = COORD(X(state, i));
                        y = COORD(Y(state, i));
                        t = 0;
                    } else {
                        float c;

                        t = state->tiles[i];

                        /*
                         * Don't bother moving the gap; just don't
                         * draw it.
                         */
                        if (t == 0)
                            continue;

                        /*
                         * Find the coordinates of this tile in the old and
                         * new states.
                         */
                        x1 = COORD(X(state, i));
                        y1 = COORD(Y(state, i));
                        for (j = 0; j < oldstate->n; j++)
                            if (oldstate->tiles[j] == state->tiles[i])
                                break;
                        assert(j < oldstate->n);
                        x0 = COORD(X(state, j));
                        y0 = COORD(Y(state, j));

                        c = (animtime / ANIM_TIME);
                        if (c < 0.0F) c = 0.0F;
                        if (c > 1.0F) c = 1.0F;

                        x = x0 + (int)(c * (x1 - x0));
                        y = y0 + (int)(c * (y1 - y0));
                    }

                } else {
                    if (pass == 0)
                        continue;
                    x = COORD(X(state, i));
                    y = COORD(Y(state, i));
                }

                draw_tile(dr, ds, state, x, y, t, bgcolour);
            }
            ds->tiles[i] = t0;
        }
    }
    ds->bgcolour = bgcolour;

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];

        /*
         * Don't show the new status until we're also showing the
         * new _state_ - after the game animation is complete.
         */
        if (oldstate)
            state = oldstate;

	if (state->used_solve)
	    sprintf(statusbuf, "Moves since auto-solve: %d",
		    state->movecount - state->completed);
	else
	    sprintf(statusbuf, "%sMoves: %d",
		    (state->completed ? "COMPLETED! " : ""),
		    (state->completed ? state->completed : state->movecount));

	status_bar(dr, statusbuf);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return ANIM_TIME;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->used_solve && !newstate->used_solve)
        return 2 * FLASH_FRAME;
    else
        return 0.0F;
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame fifteen
#endif

const struct game thegame = {
    "Fifteen", "games.fifteen", "fifteen",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    FALSE, FALSE, game_print_size, game_print,
    TRUE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};
