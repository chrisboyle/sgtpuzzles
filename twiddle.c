/*
 * twiddle.c: Puzzle involving rearranging a grid of squares by
 * rotating subsquares. Adapted and generalised from a
 * door-unlocking puzzle in Metroid Prime 2 (the one in the Main
 * Gyro Chamber).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define TILE_SIZE 48
#define BORDER    (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define PI 3.141592653589793238462643383279502884197169399

#define ANIM_PER_RADIUS_UNIT 0.13F
#define FLASH_FRAME 0.13F

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_HIGHLIGHT_GENTLE,
    COL_LOWLIGHT,
    COL_LOWLIGHT_GENTLE,
    NCOLOURS
};

struct game_params {
    int w, h, n;
    int rowsonly;
    int orientable;
};

struct game_state {
    int w, h, n;
    int orientable;
    int *grid;
    int completed;
    int movecount;
    int lastx, lasty, lastr;	       /* coordinates of last rotation */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 3;
    ret->n = 2;
    ret->rowsonly = ret->orientable = FALSE;

    return ret;
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

static int game_fetch_preset(int i, char **name, game_params **params)
{
    static struct {
        char *title;
        game_params params;
    } presets[] = {
        { "3x3 rows only", { 3, 3, 2, TRUE, FALSE } },
        { "3x3 normal", { 3, 3, 2, FALSE, FALSE } },
        { "3x3 orientable", { 3, 3, 2, FALSE, TRUE } },
        { "4x4 normal", { 4, 4, 2, FALSE } },
        { "4x4 orientable", { 4, 4, 2, FALSE, TRUE } },
        { "4x4 radius 3", { 4, 4, 3, FALSE } },
        { "5x5 radius 3", { 5, 5, 3, FALSE } },
        { "6x6 radius 4", { 6, 6, 4, FALSE } },
    };

    if (i < 0 || i >= lenof(presets))
        return FALSE;

    *name = dupstr(presets[i].title);
    *params = dup_params(&presets[i].params);

    return TRUE;
}

static game_params *decode_params(char const *string)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = atoi(string);
    ret->n = 2;
    ret->rowsonly = ret->orientable = FALSE;
    while (*string && isdigit(*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
	while (*string && isdigit(*string)) string++;
    }
    if (*string == 'n') {
        string++;
        ret->n = atoi(string);
	while (*string && isdigit(*string)) string++;
    }
    while (*string) {
	if (*string == 'r') {
	    ret->rowsonly = TRUE;
	} else if (*string == 'o') {
	    ret->orientable = TRUE;
	}
	string++;
    }

    return ret;
}

static char *encode_params(game_params *params)
{
    char buf[256];
    sprintf(buf, "%dx%dn%d%s%s", params->w, params->h, params->n,
	    params->rowsonly ? "r" : "",
	    params->orientable ? "o" : "");
    return dupstr(buf);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

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

    ret[2].name = "Rotation radius";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = "One number per row";
    ret[3].type = C_BOOLEAN;
    ret[3].sval = NULL;
    ret[3].ival = params->rowsonly;

    ret[4].name = "Orientation matters";
    ret[4].type = C_BOOLEAN;
    ret[4].sval = NULL;
    ret[4].ival = params->orientable;

    ret[5].name = NULL;
    ret[5].type = C_END;
    ret[5].sval = NULL;
    ret[5].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->n = atoi(cfg[2].sval);
    ret->rowsonly = cfg[3].ival;
    ret->orientable = cfg[4].ival;

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->n < 2)
	return "Rotation radius must be at least two";
    if (params->w < params->n)
	return "Width must be at least the rotation radius";
    if (params->h < params->n)
	return "Height must be at least the rotation radius";
    return NULL;
}

/*
 * This function actually performs a rotation on a grid. The `x'
 * and `y' coordinates passed in are the coordinates of the _top
 * left corner_ of the rotated region. (Using the centre would have
 * involved half-integers and been annoyingly fiddly. Clicking in
 * the centre is good for a user interface, but too inconvenient to
 * use internally.)
 */
static void do_rotate(int *grid, int w, int h, int n, int orientable,
		      int x, int y, int dir)
{
    int i, j;

    assert(x >= 0 && x+n <= w);
    assert(y >= 0 && y+n <= h);
    dir &= 3;
    if (dir == 0)
	return;			       /* nothing to do */

    grid += y*w+x;		       /* translate region to top corner */

    /*
     * If we were leaving the result of the rotation in a separate
     * grid, the simple thing to do would be to loop over each
     * square within the rotated region and assign it from its
     * source square. However, to do it in place without taking
     * O(n^2) memory, we need to be marginally more clever. What
     * I'm going to do is loop over about one _quarter_ of the
     * rotated region and permute each element within that quarter
     * with its rotational coset.
     * 
     * The size of the region I need to loop over is (n+1)/2 by
     * n/2, which is an obvious exact quarter for even n and is a
     * rectangle for odd n. (For odd n, this technique leaves out
     * one element of the square, which is of course the central
     * one that never moves anyway.)
     */
    for (i = 0; i < (n+1)/2; i++) {
	for (j = 0; j < n/2; j++) {
	    int k;
	    int g[4];
	    int p[4] = {
		j*w+i,
		i*w+(n-j-1),
		(n-j-1)*w+(n-i-1),
		(n-i-1)*w+j
	    };

	    for (k = 0; k < 4; k++)
		g[k] = grid[p[k]];

	    for (k = 0; k < 4; k++) {
		int v = g[(k+dir) & 3];
		if (orientable)
		    v ^= ((v+dir) ^ v) & 3;  /* alter orientation */
		grid[p[k]] = v;
	    }
	}
    }

    /*
     * Don't forget the orientation on the centre square, if n is
     * odd.
     */
    if (orientable && (n & 1)) {
	int v = grid[n/2*(w+1)];
	v ^= ((v+dir) ^ v) & 3;  /* alter orientation */
	grid[n/2*(w+1)] = v;
    }
}

static int grid_complete(int *grid, int wh, int orientable)
{
    int ok = TRUE;
    int i;
    for (i = 1; i < wh; i++)
	if (grid[i] < grid[i-1])
	    ok = FALSE;
    if (orientable) {
	for (i = 0; i < wh; i++)
	    if (grid[i] & 3)
		ok = FALSE;
    }
    return ok;
}

static char *new_game_seed(game_params *params, random_state *rs)
{
    int *grid;
    int w = params->w, h = params->h, n = params->n, wh = w*h;
    int i;
    char *ret;
    int retlen;
    int total_moves;

    /*
     * Set up a solved grid.
     */
    grid = snewn(wh, int);
    for (i = 0; i < wh; i++)
	grid[i] = ((params->rowsonly ? i/w : i) + 1) * 4;

    /*
     * Shuffle it. This game is complex enough that I don't feel up
     * to analysing its full symmetry properties (particularly at
     * n=4 and above!), so I'm going to do it the pedestrian way
     * and simply shuffle the grid by making a long sequence of
     * randomly chosen moves.
     */
    total_moves = w*h*n*n*2;
    for (i = 0; i < total_moves; i++) {
	int x, y;

	x = random_upto(rs, w - n + 1);
	y = random_upto(rs, h - n + 1);
	do_rotate(grid, w, h, n, params->orientable,
		  x, y, 1 + random_upto(rs, 3));

	/*
	 * Optionally one more move in case the entire grid has
	 * happened to come out solved.
	 */
	if (i == total_moves - 1 && grid_complete(grid, wh,
						  params->orientable))
	    i--;
    }

    /*
     * Now construct the game seed, by describing the grid as a
     * simple sequence of comma-separated integers.
     */
    ret = NULL;
    retlen = 0;
    for (i = 0; i < wh; i++) {
        char buf[80];
        int k;

        k = sprintf(buf, "%d,", grid[i]);

        ret = sresize(ret, retlen + k + 1, char);
        strcpy(ret + retlen, buf);
        retlen += k;
    }
    ret[retlen-1] = '\0';              /* delete last comma */

    sfree(grid);
    return ret;
}

static char *validate_seed(game_params *params, char *seed)
{
    char *p, *err;
    int w = params->w, h = params->h, wh = w*h;
    int i;

    p = seed;
    err = NULL;

    for (i = 0; i < wh; i++) {
	if (*p < '0' || *p > '9') {
	    return "Not enough numbers in string";
	}
	while (*p >= '0' && *p <= '9')
	    p++;
	if (i < wh-1 && *p != ',') {
	    return "Expected comma after number";
	}
	else if (i == wh-1 && *p) {
	    return "Excess junk at end of string";
	}

	if (*p) p++;		       /* eat comma */
    }

    return NULL;
}

static game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);
    int w = params->w, h = params->h, n = params->n, wh = w*h;
    int i;
    char *p;

    state->w = w;
    state->h = h;
    state->n = n;
    state->orientable = params->orientable;
    state->completed = 0;
    state->movecount = 0;
    state->lastx = state->lasty = state->lastr = -1;

    state->grid = snewn(wh, int);

    p = seed;

    for (i = 0; i < wh; i++) {
	state->grid[i] = atoi(p);
	while (*p >= '0' && *p <= '9')
	    p++;

	if (*p) p++;		       /* eat comma */
    }

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->orientable = state->orientable;
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->lastx = state->lastx;
    ret->lasty = state->lasty;
    ret->lastr = state->lastr;

    ret->grid = snewn(ret->w * ret->h, int);
    memcpy(ret->grid, state->grid, ret->w * ret->h * sizeof(int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static game_state *make_move(game_state *from, game_ui *ui, int x, int y,
			     int button)
{
    int w = from->w, h = from->h, n = from->n, wh = w*h;
    game_state *ret;
    int dir;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
	/*
	 * Determine the coordinates of the click. We offset by n-1
	 * half-blocks so that the user must click at the centre of
	 * a rotation region rather than at the corner.
	 */
	x -= (n-1) * TILE_SIZE / 2;
	y -= (n-1) * TILE_SIZE / 2;
	x = FROMCOORD(x);
	y = FROMCOORD(y);
	if (x < 0 || x > w-n || y < 0 || y > w-n)
	    return NULL;

	/*
	 * This is a valid move. Make it.
	 */
	ret = dup_game(from);
	ret->movecount++;
	dir = (button == LEFT_BUTTON ? 1 : -1);
	do_rotate(ret->grid, w, h, n, ret->orientable, x, y, dir);
	ret->lastx = x;
	ret->lasty = y;
	ret->lastr = dir;

	/*
	 * See if the game has been completed. To do this we simply
	 * test that the grid contents are in increasing order.
	 */
	if (!ret->completed && grid_complete(ret->grid, wh, ret->orientable))
	    ret->completed = ret->movecount;
	return ret;
    }
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct game_drawstate {
    int started;
    int w, h, bgcolour;
    int *grid;
};

static void game_size(game_params *params, int *x, int *y)
{
    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;
    float max;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /*
     * Drop the background colour so that the highlight is
     * noticeably brighter than it while still being under 1.
     */
    max = ret[COL_BACKGROUND*3];
    for (i = 1; i < 3; i++)
        if (ret[COL_BACKGROUND*3+i] > max)
            max = ret[COL_BACKGROUND*3+i];
    if (max * 1.2F > 1.0F) {
        for (i = 0; i < 3; i++)
            ret[COL_BACKGROUND*3+i] /= (max * 1.2F);
    }

    for (i = 0; i < 3; i++) {
        ret[COL_HIGHLIGHT * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 1.2F;
        ret[COL_HIGHLIGHT_GENTLE * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 1.1F;
        ret[COL_LOWLIGHT * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.8F;
        ret[COL_LOWLIGHT_GENTLE * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.9F;
        ret[COL_TEXT * 3 + i] = 0.0;
    }

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->bgcolour = COL_BACKGROUND;
    ds->grid = snewn(ds->w*ds->h, int);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->grid[i] = -1;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds);
}

struct rotation {
    int cx, cy, cw, ch;		       /* clip region */
    int ox, oy;			       /* rotation origin */
    float c, s;			       /* cos and sin of rotation angle */
    int lc, rc, tc, bc;		       /* colours of tile edges */
};

static void rotate(int *xy, struct rotation *rot)
{
    if (rot) {
	float xf = xy[0] - rot->ox, yf = xy[1] - rot->oy;
	float xf2, yf2;

	xf2 = rot->c * xf + rot->s * yf;
	yf2 = - rot->s * xf + rot->c * yf;

	xy[0] = xf2 + rot->ox + 0.5;   /* round to nearest */
	xy[1] = yf2 + rot->oy + 0.5;   /* round to nearest */
    }
}

static void draw_tile(frontend *fe, game_state *state, int x, int y,
                      int tile, int flash_colour, struct rotation *rot)
{
    int coords[8];
    char str[40];

    if (rot)
	clip(fe, rot->cx, rot->cy, rot->cw, rot->ch);

    /*
     * We must draw each side of the tile's highlight separately,
     * because in some cases (during rotation) they will all need
     * to be different colours.
     */

    /* The centre point is common to all sides. */
    coords[4] = x + TILE_SIZE / 2;
    coords[5] = y + TILE_SIZE / 2;
    rotate(coords+4, rot);

    /* Right side. */
    coords[0] = x + TILE_SIZE - 1;
    coords[1] = y + TILE_SIZE - 1;
    rotate(coords+0, rot);
    coords[2] = x + TILE_SIZE - 1;
    coords[3] = y;
    rotate(coords+2, rot);
    draw_polygon(fe, coords, 3, TRUE, rot ? rot->rc : COL_LOWLIGHT);
    draw_polygon(fe, coords, 3, FALSE, rot ? rot->rc : COL_LOWLIGHT);

    /* Bottom side. */
    coords[2] = x;
    coords[3] = y + TILE_SIZE - 1;
    rotate(coords+2, rot);
    draw_polygon(fe, coords, 3, TRUE, rot ? rot->bc : COL_LOWLIGHT);
    draw_polygon(fe, coords, 3, FALSE, rot ? rot->bc : COL_LOWLIGHT);

    /* Left side. */
    coords[0] = x;
    coords[1] = y;
    rotate(coords+0, rot);
    draw_polygon(fe, coords, 3, TRUE, rot ? rot->lc : COL_HIGHLIGHT);
    draw_polygon(fe, coords, 3, FALSE, rot ? rot->lc : COL_HIGHLIGHT);

    /* Top side. */
    coords[2] = x + TILE_SIZE - 1;
    coords[3] = y;
    rotate(coords+2, rot);
    draw_polygon(fe, coords, 3, TRUE, rot ? rot->tc : COL_HIGHLIGHT);
    draw_polygon(fe, coords, 3, FALSE, rot ? rot->tc : COL_HIGHLIGHT);

    /*
     * Now the main blank area in the centre of the tile.
     */
    if (rot) {
	coords[0] = x + HIGHLIGHT_WIDTH;
	coords[1] = y + HIGHLIGHT_WIDTH;
	rotate(coords+0, rot);
	coords[2] = x + HIGHLIGHT_WIDTH;
	coords[3] = y + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	rotate(coords+2, rot);
	coords[4] = x + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	coords[5] = y + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	rotate(coords+4, rot);
	coords[6] = x + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	coords[7] = y + HIGHLIGHT_WIDTH;
	rotate(coords+6, rot);
	draw_polygon(fe, coords, 4, TRUE, flash_colour);
	draw_polygon(fe, coords, 4, FALSE, flash_colour);
    } else {
	draw_rect(fe, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
		  TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
		  flash_colour);
    }

    /*
     * Next, the colour bars for orientation.
     */
    if (state->orientable) {
	int xdx, xdy, ydx, ydy;
	int cx, cy, displ, displ2;
	switch (tile & 3) {
	  case 0:
	    xdx = 1, xdy = 0;
	    ydx = 0, ydy = 1;
	    break;
	  case 1:
	    xdx = 0, xdy = -1;
	    ydx = 1, ydy = 0;
	    break;
	  case 2:
	    xdx = -1, xdy = 0;
	    ydx = 0, ydy = -1;
	    break;
	  default /* case 3 */:
	    xdx = 0, xdy = 1;
	    ydx = -1, ydy = 0;
	    break;
	}

	cx = x + TILE_SIZE / 2;
	cy = y + TILE_SIZE / 2;
	displ = TILE_SIZE / 2 - HIGHLIGHT_WIDTH - 2;
	displ2 = TILE_SIZE / 3 - HIGHLIGHT_WIDTH;

	coords[0] = cx - displ * xdx - displ2 * ydx;
	coords[1] = cy - displ * xdy - displ2 * ydy;
	rotate(coords+0, rot);
	coords[2] = cx + displ * xdx - displ2 * ydx;
	coords[3] = cy + displ * xdy - displ2 * ydy;
	rotate(coords+2, rot);
	coords[4] = cx + displ * ydx;
	coords[5] = cy + displ * ydy;
	rotate(coords+4, rot);
	draw_polygon(fe, coords, 3, TRUE, COL_LOWLIGHT_GENTLE);
	draw_polygon(fe, coords, 3, FALSE, COL_LOWLIGHT_GENTLE);
    }

    coords[0] = x + TILE_SIZE/2;
    coords[1] = y + TILE_SIZE/2;
    rotate(coords+0, rot);
    sprintf(str, "%d", tile / 4);
    draw_text(fe, coords[0], coords[1],
	      FONT_VARIABLE, TILE_SIZE/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
	      COL_TEXT, str);

    if (rot)
	unclip(fe);

    draw_update(fe, x, y, TILE_SIZE, TILE_SIZE);
}

static int highlight_colour(float angle)
{
    int colours[32] = {
	COL_LOWLIGHT,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
    };

    return colours[(int)((angle + 2*PI) / (PI/16)) & 31];
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir)
{
    return ANIM_PER_RADIUS_UNIT * sqrt(newstate->n-1);
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir)
{
    if (!oldstate->completed && newstate->completed)
        return 2 * FLASH_FRAME;
    else
        return 0.0F;
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int i, bgcolour;
    struct rotation srot, *rot;
    int lastx = -1, lasty = -1, lastr = -1;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
        bgcolour = COL_BACKGROUND;

    if (!ds->started) {
        int coords[6];

	draw_rect(fe, 0, 0,
		  TILE_SIZE * state->w + 2 * BORDER,
		  TILE_SIZE * state->h + 2 * BORDER, COL_BACKGROUND);
	draw_update(fe, 0, 0,
		    TILE_SIZE * state->w + 2 * BORDER,
		    TILE_SIZE * state->h + 2 * BORDER);

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[4] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[5] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        draw_polygon(fe, coords, 3, TRUE, COL_HIGHLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_HIGHLIGHT);

        coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
        draw_polygon(fe, coords, 3, TRUE, COL_LOWLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_LOWLIGHT);

        ds->started = TRUE;
    }

    /*
     * If we're drawing any rotated tiles, sort out the rotation
     * parameters, and also zap the rotation region to the
     * background colour before doing anything else.
     */
    if (oldstate) {
	float angle;
	float anim_max = game_anim_length(oldstate, state, dir);

	if (dir > 0) {
	    lastx = state->lastx;
	    lasty = state->lasty;
	    lastr = state->lastr;
	} else {
	    lastx = oldstate->lastx;
	    lasty = oldstate->lasty;
	    lastr = -oldstate->lastr;
	}

	rot = &srot;
	rot->cx = COORD(lastx);
	rot->cy = COORD(lasty);
	rot->cw = rot->ch = TILE_SIZE * state->n;
	rot->ox = rot->cx + rot->cw/2;
	rot->oy = rot->cy + rot->ch/2;
	angle = (-PI/2 * lastr) * (1.0 - animtime / anim_max);
	rot->c = cos(angle);
	rot->s = sin(angle);

	/*
	 * Sort out the colours of the various sides of the tile.
	 */
	rot->lc = highlight_colour(PI + angle);
	rot->rc = highlight_colour(angle);
	rot->tc = highlight_colour(PI/2 + angle);
	rot->bc = highlight_colour(-PI/2 + angle);

	draw_rect(fe, rot->cx, rot->cy, rot->cw, rot->ch, bgcolour);
    } else
	rot = NULL;

    /*
     * Now draw each tile.
     */
    for (i = 0; i < state->w * state->h; i++) {
	int t;
	int tx = i % state->w, ty = i / state->w;

	/*
	 * Figure out what should be displayed at this location.
	 * Usually it will be state->grid[i], unless we're in the
	 * middle of animating an actual rotation and this cell is
	 * within the rotation region, in which case we set -1
	 * (always display).
	 */
	if (oldstate && lastx >= 0 && lasty >= 0 &&
	    tx >= lastx && tx < lastx + state->n &&
	    ty >= lasty && ty < lasty + state->n)
	    t = -1;
	else
	    t = state->grid[i];

	if (ds->bgcolour != bgcolour ||   /* always redraw when flashing */
	    ds->grid[i] != t || ds->grid[i] == -1 || t == -1) {
	    int x = COORD(tx), y = COORD(ty);

	    draw_tile(fe, state, x, y, state->grid[i], bgcolour, rot);
            ds->grid[i] = t;
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

	sprintf(statusbuf, "%sMoves: %d",
		(state->completed ? "COMPLETED! " : ""),
		(state->completed ? state->completed : state->movecount));

	status_bar(fe, statusbuf);
    }
}

static int game_wants_statusbar(void)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame twiddle
#endif

const struct game thegame = {
    "Twiddle", "games.twiddle", TRUE,
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    game_configure,
    custom_params,
    validate_params,
    new_game_seed,
    validate_seed,
    new_game,
    dup_game,
    free_game,
    new_ui,
    free_ui,
    make_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
};
