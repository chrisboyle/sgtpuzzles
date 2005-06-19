/*
 * 'same game' -- try to remove all the coloured squares by
 *                selecting regions of contiguous colours.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define TILE_INNER (ds->tileinner)
#define TILE_GAP (ds->tilegap)
#define TILE_SIZE (TILE_INNER + TILE_GAP)
#define PREFERRED_TILE_SIZE 32
#define BORDER (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH 2

#define FLASH_FRAME 0.13F

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define X(state, i) ( (i) % (state)->params.w )
#define Y(state, i) ( (i) / (state)->params.w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

enum {
    COL_BACKGROUND,
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8, COL_9,
    COL_IMPOSSIBLE, COL_SEL, COL_HIGHLIGHT, COL_LOWLIGHT,
    NCOLOURS
};

/* scoresub is 1 or 2 (for (n-1)^2 or (n-2)^2) */
struct game_params {
    int w, h, ncols, scoresub;
};

/* These flags must be unique across all uses; in the game_state,
 * the game_ui, and the drawstate (as they all get combined in the
 * drawstate). */
#define TILE_COLMASK    0x00ff
#define TILE_SELECTED   0x0100 /* used in ui and drawstate */
#define TILE_JOINRIGHT  0x0200 /* used in drawstate */
#define TILE_JOINDOWN   0x0400 /* used in drawstate */
#define TILE_JOINDIAG   0x0800 /* used in drawstate */
#define TILE_HASSEL     0x1000 /* used in drawstate */
#define TILE_IMPOSSIBLE 0x2000 /* used in drawstate */

#define TILE(gs,x,y) ((gs)->tiles[(gs)->params.w*(y)+(x)])
#define COL(gs,x,y) (TILE(gs,x,y) & TILE_COLMASK)
#define ISSEL(gs,x,y) (TILE(gs,x,y) & TILE_SELECTED)

#define SWAPTILE(gs,x1,y1,x2,y2) do {   \
    int t = TILE(gs,x1,y1);               \
    TILE(gs,x1,y1) = TILE(gs,x2,y2);      \
    TILE(gs,x2,y2) = t;                   \
} while (0)

static int npoints(game_params *params, int nsel)
{
    int sdiff = nsel - params->scoresub;
    return (sdiff > 0) ? sdiff * sdiff : 0;
}

struct game_state {
    struct game_params params;
    int n;
    int *tiles; /* colour only */
    int score;
    int complete, impossible;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    ret->w = 5;
    ret->h = 5;
    ret->ncols = 3;
    ret->scoresub = 2;
    return ret;
}

static const struct game_params samegame_presets[] = {
    { 5, 5, 3, 2 },
    { 10, 5, 3, 2 },
    { 15, 10, 3, 2 },
    { 15, 10, 4, 2 },
    { 20, 15, 4, 2 }
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(samegame_presets))
	return FALSE;

    ret = snew(game_params);
    *ret = samegame_presets[i];

    sprintf(str, "%dx%d, %d colours", ret->w, ret->h, ret->ncols);

    *name = dupstr(str);
    *params = ret;
    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
	p++;
	params->h = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->h = params->w;
    }
    if (*p++ == 'c') {
	params->ncols = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->ncols = 3;
    }
    if (*p++ == 's') {
	params->scoresub = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->scoresub = 2;
    }
}

static char *encode_params(game_params *params, int full)
{
    char ret[80];

    sprintf(ret, "%dx%dc%ds%d",
	    params->w, params->h, params->ncols, params->scoresub);
    return dupstr(ret);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

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

    ret[2].name = "No. of colours";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->ncols);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = "Scoring system";
    ret[3].type = C_CHOICES;
    ret[3].sval = ":(n-1)^2:(n-2)^2";
    ret[3].ival = params->scoresub-1;

    ret[4].name = NULL;
    ret[4].type = C_END;
    ret[4].sval = NULL;
    ret[4].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->ncols = atoi(cfg[2].sval);
    ret->scoresub = cfg[3].ival + 1;

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->w < 1 || params->h < 1)
	return "Width and height must both be positive";
    if (params->ncols < 2)
	return "It's too easy with only one colour...";
    if (params->ncols > 9)
	return "Maximum of 9 colours";

    /* ...and we must make sure we can generate at least 2 squares
     * of each colour so it's theoretically soluble. */
    if ((params->w * params->h) < (params->ncols * 2))
	return "Too many colours makes given grid size impossible";

    if ((params->scoresub < 1) || (params->scoresub > 2))
	return "Scoring system not recognised";

    return NULL;
}

/* Currently this is a very very dumb game-generation engine; it
 * just picks randomly from the tile space. I had a look at a few
 * other same game implementations, and none of them attempt to do
 * anything to try and make sure the grid started off with a nice
 * set of large blocks.
 *
 * It does at least make sure that there are >= 2 of each colour
 * present at the start.
 */

static char *new_game_desc(game_params *params, random_state *rs,
			   game_aux_info **aux, int interactive)
{
    char *ret;
    int n, i, j, c, retlen, *tiles;

    n = params->w * params->h;
    tiles = snewn(n, int);
    memset(tiles, 0, n*sizeof(int));

    /* randomly place two of each colour */
    for (c = 0; c < params->ncols; c++) {
	for (j = 0; j < 2; j++) {
	    do {
		i = (int)random_upto(rs, n);
	    } while (tiles[i] != 0);
	    tiles[i] = c+1;
	}
    }

    /* fill in the rest randomly */
    for (i = 0; i < n; i++) {
	if (tiles[i] == 0)
	    tiles[i] = (int)random_upto(rs, params->ncols)+1;
    }

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
    ret[retlen-1] = '\0'; /* delete last comma */

    sfree(tiles);
    return ret;
}

static void game_free_aux_info(game_aux_info *aux)
{
    assert(!"Shouldn't happen");
}

static char *validate_desc(game_params *params, char *desc)
{
    int area = params->w * params->h, i;
    char *p = desc;

    for (i = 0; i < area; i++) {
	char *q = p;
	int n;

	if (!isdigit(*p))
	    return "Not enough numbers in string";
	while (isdigit(*p)) p++;

	if (i < area-1 && *p != ',')
	    return "Expected comma after number";
	else if (i == area-1 && *p)
	    return "Excess junk at end of string";

	n = atoi(q);
	if (n < 0 || n > params->ncols)
	    return "Colour out of range";

	if (*p) p++; /* eat comma */
    }
    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    game_state *state = snew(game_state);
    char *p = desc;
    int i;

    state->params = *params; /* struct copy */
    state->n = state->params.w * state->params.h;
    state->tiles = snewn(state->n, int);

    for (i = 0; i < state->n; i++) {
	assert(*p);
	state->tiles[i] = atoi(p);
	while (*p && *p != ',')
            p++;
        if (*p) p++;                   /* eat comma */
    }
    state->complete = state->impossible = 0;
    state->score = 0;

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    *ret = *state; /* structure copy, except... */

    ret->tiles = snewn(state->n, int);
    memcpy(ret->tiles, state->tiles, state->n * sizeof(int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state);
}

static game_state *solve_game(game_state *state, game_state *currstate,
			      game_aux_info *aux, char **error)
{
    return NULL;
}

static char *game_text_format(game_state *state)
{
    char *ret, *p;
    int x, y, maxlen;

    maxlen = state->params.h * (state->params.w + 1);
    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->params.h; y++) {
	for (x = 0; x < state->params.w; x++) {
	    int t = TILE(state,x,y);
	    if (t <= 0)      *p++ = ' ';
	    else if (t < 10) *p++ = '0'+t;
	    else             *p++ = 'a'+(t-10);
	}
	*p++ = '\n';
    }
    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

struct game_ui {
    struct game_params params;
    int *tiles; /* selected-ness only */
    int nselected;
    int xsel, ysel, displaysel;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->params = state->params; /* structure copy */
    ui->tiles = snewn(state->n, int);
    memset(ui->tiles, 0, state->n*sizeof(int));
    ui->nselected = 0;

    ui->xsel = ui->ysel = ui->displaysel = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui->tiles);
    sfree(ui);
}

static void sel_clear(game_ui *ui, game_state *state)
{
    int i;

    for (i = 0; i < state->n; i++)
	ui->tiles[i] &= ~TILE_SELECTED;
    ui->nselected = 0;
}


static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    sel_clear(ui, newstate);
}

static void sel_remove(game_ui *ui, game_state *state)
{
    int i, nremoved = 0;

    state->score += npoints(&state->params, ui->nselected);

    for (i = 0; i < state->n; i++) {
	if (ui->tiles[i] & TILE_SELECTED) {
	    nremoved++;
	    state->tiles[i] = 0;
	    ui->tiles[i] &= ~TILE_SELECTED;
	}
    }
    ui->nselected = 0;
}

static void sel_expand(game_ui *ui, game_state *state, int tx, int ty)
{
    int ns = 1, nadded, x, y, c;

    TILE(ui,tx,ty) |= TILE_SELECTED;
    do {
	nadded = 0;

	for (x = 0; x < state->params.w; x++) {
	    for (y = 0; y < state->params.h; y++) {
		if (x == tx && y == ty) continue;
		if (ISSEL(ui,x,y)) continue;

		c = COL(state,x,y);
		if ((x > 0) &&
		    ISSEL(ui,x-1,y) && COL(state,x-1,y) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((x+1 < state->params.w) &&
		    ISSEL(ui,x+1,y) && COL(state,x+1,y) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((y > 0) &&
		    ISSEL(ui,x,y-1) && COL(state,x,y-1) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((y+1 < state->params.h) &&
		    ISSEL(ui,x,y+1) && COL(state,x,y+1) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}
	    }
	}
	ns += nadded;
    } while (nadded > 0);

    if (ns > 1) {
	ui->nselected = ns;
    } else {
	sel_clear(ui, state);
    }
}

static int sg_emptycol(game_state *ret, int x)
{
    int y;
    for (y = 0; y < ret->params.h; y++) {
	if (COL(ret,x,y)) return 0;
    }
    return 1;
}


static void sg_snuggle(game_state *ret)
{
    int x,y, ndone;

    /* make all unsupported tiles fall down. */
    do {
	ndone = 0;
	for (x = 0; x < ret->params.w; x++) {
	    for (y = ret->params.h-1; y > 0; y--) {
		if (COL(ret,x,y) != 0) continue;
		if (COL(ret,x,y-1) != 0) {
		    SWAPTILE(ret,x,y,x,y-1);
		    ndone++;
		}
	    }
	}
    } while (ndone);

    /* shuffle all columns as far left as they can go. */
    do {
	ndone = 0;
	for (x = 0; x < ret->params.w-1; x++) {
	    if (sg_emptycol(ret,x) && !sg_emptycol(ret,x+1)) {
		ndone++;
		for (y = 0; y < ret->params.h; y++) {
		    SWAPTILE(ret,x,y,x+1,y);
		}
	    }
	}
    } while (ndone);
}

static void sg_check(game_state *ret)
{
    int x,y, complete = 1, impossible = 1;

    for (x = 0; x < ret->params.w; x++) {
	for (y = 0; y < ret->params.h; y++) {
	    if (COL(ret,x,y) == 0)
		continue;
	    complete = 0;
	    if (x+1 < ret->params.w) {
		if (COL(ret,x,y) == COL(ret,x+1,y))
		    impossible = 0;
	    }
	    if (y+1 < ret->params.h) {
		if (COL(ret,x,y) == COL(ret,x,y+1))
		    impossible = 0;
	    }
	}
    }
    ret->complete = complete;
    ret->impossible = impossible;
}

struct game_drawstate {
    int started, bgcolour;
    int tileinner, tilegap;
    int *tiles; /* contains colour and SELECTED. */
};

static game_state *make_move(game_state *from, game_ui *ui, game_drawstate *ds,
                             int x, int y, int button)
{
    int tx, ty;
    game_state *ret = from;

    ui->displaysel = 0;

    if (button == RIGHT_BUTTON || button == LEFT_BUTTON) {
	tx = FROMCOORD(x); ty= FROMCOORD(y);
    } else if (button == CURSOR_UP || button == CURSOR_DOWN ||
	       button == CURSOR_LEFT || button == CURSOR_RIGHT) {
	int dx = 0, dy = 0;
	ui->displaysel = 1;
	dx = (button == CURSOR_LEFT) ? -1 : ((button == CURSOR_RIGHT) ? +1 : 0);
	dy = (button == CURSOR_DOWN) ? +1 : ((button == CURSOR_UP)    ? -1 : 0);
	ui->xsel = (ui->xsel + from->params.w + dx) % from->params.w;
	ui->ysel = (ui->ysel + from->params.h + dy) % from->params.h;
	return ret;
    } else if (button == CURSOR_SELECT || button == ' ' || button == '\r' ||
	       button == '\n') {
	ui->displaysel = 1;
	tx = ui->xsel;
	ty = ui->ysel;
    } else
	return NULL;

    if (tx < 0 || tx >= from->params.w || ty < 0 || ty >= from->params.h)
	return NULL;
    if (COL(from, tx, ty) == 0) return NULL;

    if (ISSEL(ui,tx,ty)) {
	if (button == RIGHT_BUTTON)
	    sel_clear(ui, from);
	else {
	    /* this is the actual move. */
	    ret = dup_game(from);
	    sel_remove(ui, ret);
	    sg_snuggle(ret); /* shifts blanks down and to the left */
	    sg_check(ret);   /* checks for completeness or impossibility */
	}
    } else {
	sel_clear(ui, from); /* might be no-op */
	sel_expand(ui, from, tx, ty);
    }
    if (ret->complete || ret->impossible)
	ui->displaysel = 0;

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_size(game_params *params, game_drawstate *ds, int *x, int *y,
                      int expand)
{
    int tsx, tsy, ts;

    /*
     * We could choose the tile gap dynamically as well if we
     * wanted to; for example, at low tile sizes it might be
     * sensible to leave it out completely. However, for the moment
     * and for the sake of simplicity I'm just going to fix it at
     * 2.
     */
    ds->tilegap = 2;

    /*
     * Each window dimension equals the tile size (inner plus gap)
     * times the grid dimension, plus another tile size (border is
     * half the width of a tile), minus one tile gap.
     * 
     * We must cast to unsigned before adding to *x and *y, since
     * they might be INT_MAX!
     */
    tsx = (unsigned)(*x + ds->tilegap) / (params->w + 1);
    tsy = (unsigned)(*y + ds->tilegap) / (params->h + 1);

    ts = min(tsx, tsy);
    if (expand)
        ds->tileinner = ts - ds->tilegap;
    else
        ds->tileinner = min(ts, PREFERRED_TILE_SIZE) - ds->tilegap;

    *x = TILE_SIZE * params->w + 2 * BORDER - TILE_GAP;
    *y = TILE_SIZE * params->h + 2 * BORDER - TILE_GAP;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_1 * 3 + 0] = 0.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 1.0F;

    ret[COL_2 * 3 + 0] = 0.0F;
    ret[COL_2 * 3 + 1] = 0.5F;
    ret[COL_2 * 3 + 2] = 0.0F;

    ret[COL_3 * 3 + 0] = 1.0F;
    ret[COL_3 * 3 + 1] = 0.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    ret[COL_4 * 3 + 0] = 1.0F;
    ret[COL_4 * 3 + 1] = 1.0F;
    ret[COL_4 * 3 + 2] = 0.0F;

    ret[COL_5 * 3 + 0] = 1.0F;
    ret[COL_5 * 3 + 1] = 0.0F;
    ret[COL_5 * 3 + 2] = 1.0F;

    ret[COL_6 * 3 + 0] = 0.0F;
    ret[COL_6 * 3 + 1] = 1.0F;
    ret[COL_6 * 3 + 2] = 1.0F;

    ret[COL_7 * 3 + 0] = 0.5F;
    ret[COL_7 * 3 + 1] = 0.5F;
    ret[COL_7 * 3 + 2] = 1.0F;

    ret[COL_8 * 3 + 0] = 0.5F;
    ret[COL_8 * 3 + 1] = 1.0F;
    ret[COL_8 * 3 + 2] = 0.5F;

    ret[COL_9 * 3 + 0] = 1.0F;
    ret[COL_9 * 3 + 1] = 0.5F;
    ret[COL_9 * 3 + 2] = 0.5F;

    ret[COL_IMPOSSIBLE * 3 + 0] = 0.0F;
    ret[COL_IMPOSSIBLE * 3 + 1] = 0.0F;
    ret[COL_IMPOSSIBLE * 3 + 2] = 0.0F;

    ret[COL_SEL * 3 + 0] = 1.0F;
    ret[COL_SEL * 3 + 1] = 1.0F;
    ret[COL_SEL * 3 + 2] = 1.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_LOWLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0 / 3.0;
    ret[COL_LOWLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0 / 3.0;
    ret[COL_LOWLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0 / 3.0;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = 0;
    ds->tileinner = ds->tilegap = 0;   /* not decided yet */
    ds->tiles = snewn(state->n, int);
    for (i = 0; i < state->n; i++)
	ds->tiles[i] = -1;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

/* Drawing routing for the tile at (x,y) is responsible for drawing
 * itself and the gaps to its right and below. If we're the same colour
 * as the tile to our right, then we fill in the gap; ditto below, and if
 * both then we fill the teeny tiny square in the corner as well.
 */

static void tile_redraw(frontend *fe, game_drawstate *ds,
			int x, int y, int dright, int dbelow,
                        int tile, int bgcolour)
{
    int outer = bgcolour, inner = outer, col = tile & TILE_COLMASK;

    if (col) {
	if (tile & TILE_IMPOSSIBLE) {
	    outer = col;
	    inner = COL_IMPOSSIBLE;
	} else if (tile & TILE_SELECTED) {
	    outer = COL_SEL;
	    inner = col;
	} else {
	    outer = inner = col;
	}
    }
    draw_rect(fe, COORD(x), COORD(y), TILE_INNER, TILE_INNER, outer);
    draw_rect(fe, COORD(x)+TILE_INNER/4, COORD(y)+TILE_INNER/4,
	      TILE_INNER/2, TILE_INNER/2, inner);

    if (dright)
	draw_rect(fe, COORD(x)+TILE_INNER, COORD(y), TILE_GAP, TILE_INNER,
		  (tile & TILE_JOINRIGHT) ? outer : bgcolour);
    if (dbelow)
	draw_rect(fe, COORD(x), COORD(y)+TILE_INNER, TILE_INNER, TILE_GAP,
		  (tile & TILE_JOINDOWN) ? outer : bgcolour);
    if (dright && dbelow)
	draw_rect(fe, COORD(x)+TILE_INNER, COORD(y)+TILE_INNER, TILE_GAP, TILE_GAP,
		  (tile & TILE_JOINDIAG) ? outer : bgcolour);

    if (tile & TILE_HASSEL) {
	int sx = COORD(x)+2, sy = COORD(y)+2, ssz = TILE_INNER-5;
	int scol = (outer == COL_SEL) ? COL_LOWLIGHT : COL_HIGHLIGHT;
	draw_line(fe, sx,     sy,     sx+ssz, sy,     scol);
	draw_line(fe, sx+ssz, sy,     sx+ssz, sy+ssz, scol);
	draw_line(fe, sx+ssz, sy+ssz, sx,     sy+ssz, scol);
	draw_line(fe, sx,     sy+ssz, sx,     sy,     scol);
    }

    draw_update(fe, COORD(x), COORD(y), TILE_SIZE, TILE_SIZE);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int bgcolour, x, y;

    /* This was entirely cloned from fifteen.c; it should probably be
     * moved into some generic 'draw-recessed-rectangle' utility fn. */
    if (!ds->started) {
	int coords[10];

	draw_rect(fe, 0, 0,
		  TILE_SIZE * state->params.w + 2 * BORDER,
		  TILE_SIZE * state->params.h + 2 * BORDER, COL_BACKGROUND);
	draw_update(fe, 0, 0,
		    TILE_SIZE * state->params.w + 2 * BORDER,
		    TILE_SIZE * state->params.h + 2 * BORDER);

	/*
	 * Recessed area containing the whole puzzle.
	 */
	coords[0] = COORD(state->params.w) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[1] = COORD(state->params.h) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[2] = COORD(state->params.w) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[4] = coords[2] - TILE_SIZE;
	coords[5] = coords[3] + TILE_SIZE;
	coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[9] = COORD(state->params.h) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[6] = coords[8] + TILE_SIZE;
	coords[7] = coords[9] - TILE_SIZE;
	draw_polygon(fe, coords, 5, TRUE, COL_HIGHLIGHT);
	draw_polygon(fe, coords, 5, FALSE, COL_HIGHLIGHT);

	coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
	draw_polygon(fe, coords, 5, TRUE, COL_LOWLIGHT);
	draw_polygon(fe, coords, 5, FALSE, COL_LOWLIGHT);

	ds->started = 1;
    }

    if (flashtime > 0.0) {
	int frame = (int)(flashtime / FLASH_FRAME);
	bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
	bgcolour = COL_BACKGROUND;

    for (x = 0; x < state->params.w; x++) {
	for (y = 0; y < state->params.h; y++) {
	    int i = (state->params.w * y) + x;
	    int col = COL(state,x,y), tile = col;
	    int dright = (x+1 < state->params.w);
	    int dbelow = (y+1 < state->params.h);

	    tile |= ISSEL(ui,x,y);
	    if (state->impossible)
		tile |= TILE_IMPOSSIBLE;
	    if (dright && COL(state,x+1,y) == col)
		tile |= TILE_JOINRIGHT;
	    if (dbelow && COL(state,x,y+1) == col)
		tile |= TILE_JOINDOWN;
	    if ((tile & TILE_JOINRIGHT) && (tile & TILE_JOINDOWN) &&
		COL(state,x+1,y+1) == col)
		tile |= TILE_JOINDIAG;

	    if (ui->displaysel && ui->xsel == x && ui->ysel == y)
		tile |= TILE_HASSEL;

	    /* For now we're never expecting oldstate at all (because we have
	     * no animation); when we do we might well want to be looking
	     * at the tile colours from oldstate, not state. */
	    if ((oldstate && COL(oldstate,x,y) != col) ||
		(flashtime > 0.0) ||
		(ds->bgcolour != bgcolour) ||
		(tile != ds->tiles[i])) {
		tile_redraw(fe, ds, x, y, dright, dbelow, tile, bgcolour);
		ds->tiles[i] = tile;
	    }
	}
    }
    ds->bgcolour = bgcolour;

    {
	char status[255], score[80];

	sprintf(score, "Score: %d", state->score);

	if (state->complete)
	    sprintf(status, "COMPLETE! %s", score);
	else if (state->impossible)
	    sprintf(status, "Cannot move! %s", score);
	else if (ui->nselected)
	    sprintf(status, "%s  Selected: %d (%d)",
		    score, ui->nselected, npoints(&state->params, ui->nselected));
	else
	    sprintf(status, "%s", score);
	status_bar(fe, status);
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if ((!oldstate->complete && newstate->complete) ||
        (!oldstate->impossible && newstate->impossible))
	return 2 * FLASH_FRAME;
    else
	return 0.0F;
}

static int game_wants_statusbar(void)
{
    return TRUE;
}

static int game_timing_state(game_state *state)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame samegame
#endif

const struct game thegame = {
    "Same Game", "games.samegame",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    game_free_aux_info,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    FALSE, solve_game,
    TRUE, game_text_format,
    new_ui,
    free_ui,
    game_changed_state,
    make_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};
