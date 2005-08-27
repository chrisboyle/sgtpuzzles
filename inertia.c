/*
 * inertia.c: Game involving navigating round a grid picking up
 * gems.
 * 
 * Game rules and basic generator design by Ben Olmstead.
 * This re-implementation was written by Simon Tatham.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/* Used in the game_state */
#define BLANK   'b'
#define GEM     'g'
#define MINE    'm'
#define STOP    's'
#define WALL    'w'

/* Used in the game IDs */
#define START   'S'

/* Used in the game generation */
#define POSSGEM 'G'

/* Used only in the game_drawstate*/
#define UNDRAWN '?'

#define DIRECTIONS 8
#define DX(dir) ( (dir) & 3 ? (((dir) & 7) > 4 ? -1 : +1) : 0 )
#define DY(dir) ( DX((dir)+6) )

/*
 * Lvalue macro which expects x and y to be in range.
 */
#define LV_AT(w, h, grid, x, y) ( (grid)[(y)*(w)+(x)] )

/*
 * Rvalue macro which can cope with x and y being out of range.
 */
#define AT(w, h, grid, x, y) ( (x)<0 || (x)>=(w) || (y)<0 || (y)>=(h) ? \
			       WALL : LV_AT(w, h, grid, x, y) )

enum {
    COL_BACKGROUND,
    COL_OUTLINE,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    COL_PLAYER,
    COL_DEAD_PLAYER,
    COL_MINE,
    COL_GEM,
    COL_WALL,
    NCOLOURS
};

struct game_params {
    int w, h;
};

struct game_state {
    game_params p;
    int px, py;
    int gems;
    char *grid;
    int distance_moved;
    int dead;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 10;
    ret->h = 8;

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

static const struct game_params inertia_presets[] = {
    { 10, 8 },
    { 15, 12 },
    { 20, 16 },
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params p, *ret;
    char *retname;
    char namebuf[80];

    if (i < 0 || i >= lenof(inertia_presets))
	return FALSE;

    p = inertia_presets[i];
    ret = dup_params(&p);
    sprintf(namebuf, "%dx%d", ret->w, ret->h);
    retname = dupstr(namebuf);

    *params = ret;
    *name = retname;
    return TRUE;
}

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
    }
}

static char *encode_params(game_params *params, int full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

static config_item *game_configure(game_params *params)
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

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    /*
     * Avoid completely degenerate cases which only have one
     * row/column. We probably could generate completable puzzles
     * of that shape, but they'd be forced to be extremely boring
     * and at large sizes would take a while to happen upon at
     * random as well.
     */
    if (params->w < 2 || params->h < 2)
	return "Width and height must both be at least two";

    /*
     * The grid construction algorithm creates 1/5 as many gems as
     * grid squares, and must create at least one gem to have an
     * actual puzzle. However, an area-five grid is ruled out by
     * the above constraint, so the practical minimum is six.
     */
    if (params->w * params->h < 6)
	return "Grid area must be at least six squares";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver used by grid generator.
 */

struct solver_scratch {
    unsigned char *reachable_from, *reachable_to;
    int *positions;
};

static struct solver_scratch *new_scratch(int w, int h)
{
    struct solver_scratch *sc = snew(struct solver_scratch);

    sc->reachable_from = snewn(w * h * DIRECTIONS, unsigned char);
    sc->reachable_to = snewn(w * h * DIRECTIONS, unsigned char);
    sc->positions = snewn(w * h * DIRECTIONS, int);

    return sc;
}

static void free_scratch(struct solver_scratch *sc)
{
    sfree(sc);
}

static int can_go(int w, int h, char *grid,
		  int x1, int y1, int dir1, int x2, int y2, int dir2)
{
    /*
     * Returns TRUE if we can transition directly from (x1,y1)
     * going in direction dir1, to (x2,y2) going in direction dir2.
     */

    /*
     * If we're actually in the middle of an unoccupyable square,
     * we cannot make any move.
     */
    if (AT(w, h, grid, x1, y1) == WALL ||
	AT(w, h, grid, x1, y1) == MINE)
	return FALSE;

    /*
     * If a move is capable of stopping at x1,y1,dir1, and x2,y2 is
     * the same coordinate as x1,y1, then we can make the
     * transition (by stopping and changing direction).
     * 
     * For this to be the case, we have to either have a wall
     * beyond x1,y1,dir1, or have a stop on x1,y1.
     */
    if (x2 == x1 && y2 == y1 &&
	(AT(w, h, grid, x1, y1) == STOP ||
	 AT(w, h, grid, x1, y1) == START ||
	 AT(w, h, grid, x1+DX(dir1), y1+DY(dir1)) == WALL))
	return TRUE;

    /*
     * If a move is capable of continuing here, then x1,y1,dir1 can
     * move one space further on.
     */
    if (x2 == x1+DX(dir1) && y2 == y1+DY(dir1) && dir1 == dir2 &&
	(AT(w, h, grid, x2, y2) == BLANK ||
	 AT(w, h, grid, x2, y2) == GEM ||
	 AT(w, h, grid, x2, y2) == STOP ||
	 AT(w, h, grid, x2, y2) == START))
	return TRUE;

    /*
     * That's it.
     */
    return FALSE;
}

static int find_gem_candidates(int w, int h, char *grid,
			       struct solver_scratch *sc)
{
    int wh = w*h;
    int head, tail;
    int sx, sy, gx, gy, gd, pass, possgems;

    /*
     * This function finds all the candidate gem squares, which are
     * precisely those squares which can be picked up on a loop
     * from the starting point back to the starting point. Doing
     * this may involve passing through such a square in the middle
     * of a move; so simple breadth-first search over the _squares_
     * of the grid isn't quite adequate, because it might be that
     * we can only reach a gem from the start by moving over it in
     * one direction, but can only return to the start if we were
     * moving over it in another direction.
     * 
     * Instead, we BFS over a space which mentions each grid square
     * eight times - once for each direction. We also BFS twice:
     * once to find out what square+direction pairs we can reach
     * _from_ the start point, and once to find out what pairs we
     * can reach the start point from. Then a square is reachable
     * if any of the eight directions for that square has both
     * flags set.
     */

    memset(sc->reachable_from, 0, wh * DIRECTIONS);
    memset(sc->reachable_to, 0, wh * DIRECTIONS);

    /*
     * Find the starting square.
     */
    sx = -1;			       /* placate optimiser */
    for (sy = 0; sy < h; sy++) {
	for (sx = 0; sx < w; sx++)
	    if (AT(w, h, grid, sx, sy) == START)
		break;
	if (sx < w)
	    break;
    }
    assert(sy < h);

    for (pass = 0; pass < 2; pass++) {
	unsigned char *reachable = (pass == 0 ? sc->reachable_from :
				    sc->reachable_to);
	int sign = (pass == 0 ? +1 : -1);
	int dir;

#ifdef SOLVER_DIAGNOSTICS
	printf("starting pass %d\n", pass);
#endif

	/*
	 * `head' and `tail' are indices within sc->positions which
	 * track the list of board positions left to process.
	 */
	head = tail = 0;
	for (dir = 0; dir < DIRECTIONS; dir++) {
	    int index = (sy*w+sx)*DIRECTIONS+dir;
	    sc->positions[tail++] = index;
	    reachable[index] = TRUE;
#ifdef SOLVER_DIAGNOSTICS
	    printf("starting point %d,%d,%d\n", sx, sy, dir);
#endif
	}

	/*
	 * Now repeatedly pick an element off the list and process
	 * it.
	 */
	while (head < tail) {
	    int index = sc->positions[head++];
	    int dir = index % DIRECTIONS;
	    int x = (index / DIRECTIONS) % w;
	    int y = index / (w * DIRECTIONS);
	    int n, x2, y2, d2, i2;

#ifdef SOLVER_DIAGNOSTICS
	    printf("processing point %d,%d,%d\n", x, y, dir);
#endif
	    /*
	     * The places we attempt to switch to here are:
	     * 	- each possible direction change (all the other
	     * 	  directions in this square)
	     * 	- one step further in the direction we're going (or
	     * 	  one step back, if we're in the reachable_to pass).
	     */
	    for (n = -1; n < DIRECTIONS; n++) {
		if (n < 0) {
		    x2 = x + sign * DX(dir);
		    y2 = y + sign * DY(dir);
		    d2 = dir;
		} else {
		    x2 = x;
		    y2 = y;
		    d2 = n;
		}
		i2 = (y2*w+x2)*DIRECTIONS+d2;
		if (!reachable[i2]) {
		    int ok;
#ifdef SOLVER_DIAGNOSTICS
		    printf("  trying point %d,%d,%d", x2, y2, d2);
#endif
		    if (pass == 0)
			ok = can_go(w, h, grid, x, y, dir, x2, y2, d2);
		    else
			ok = can_go(w, h, grid, x2, y2, d2, x, y, dir);
#ifdef SOLVER_DIAGNOSTICS
		    printf(" - %sok\n", ok ? "" : "not ");
#endif
		    if (ok) {
			sc->positions[tail++] = i2;
			reachable[i2] = TRUE;
		    }
		}
	    }
	}
    }

    /*
     * And that should be it. Now all we have to do is find the
     * squares for which there exists _some_ direction such that
     * the square plus that direction form a tuple which is both
     * reachable from the start and reachable to the start.
     */
    possgems = 0;
    for (gy = 0; gy < h; gy++)
	for (gx = 0; gx < w; gx++)
	    if (AT(w, h, grid, gx, gy) == BLANK) {
		for (gd = 0; gd < DIRECTIONS; gd++) {
		    int index = (gy*w+gx)*DIRECTIONS+gd;
		    if (sc->reachable_from[index] && sc->reachable_to[index]) {
#ifdef SOLVER_DIAGNOSTICS
			printf("space at %d,%d is reachable via"
			       " direction %d\n", gx, gy, gd);
#endif
			LV_AT(w, h, grid, gx, gy) = POSSGEM;
			possgems++;
			break;
		    }
		}
	    }

    return possgems;
}

/* ----------------------------------------------------------------------
 * Grid generation code.
 */

static char *gengrid(int w, int h, random_state *rs)
{
    int wh = w*h;
    char *grid = snewn(wh+1, char);
    struct solver_scratch *sc = new_scratch(w, h);
    int maxdist_threshold, tries;

    maxdist_threshold = 2;
    tries = 0;

    while (1) {
	int i, j;
	int possgems;
	int *dist, *list, head, tail, maxdist;

	/*
	 * We're going to fill the grid with the five basic piece
	 * types in about 1/5 proportion. For the moment, though,
	 * we leave out the gems, because we'll put those in
	 * _after_ we run the solver to tell us where the viable
	 * locations are.
	 */
	i = 0;
	for (j = 0; j < wh/5; j++)
	    grid[i++] = WALL;
	for (j = 0; j < wh/5; j++)
	    grid[i++] = STOP;
	for (j = 0; j < wh/5; j++)
	    grid[i++] = MINE;
	assert(i < wh);
	grid[i++] = START;
	while (i < wh)
	    grid[i++] = BLANK;
	shuffle(grid, wh, sizeof(*grid), rs);

	/*
	 * Find the viable gem locations, and immediately give up
	 * and try again if there aren't enough of them.
	 */
	possgems = find_gem_candidates(w, h, grid, sc);
	if (possgems < wh/5)
	    continue;

	/*
	 * We _could_ now select wh/5 of the POSSGEMs and set them
	 * to GEM, and have a viable level. However, there's a
	 * chance that a large chunk of the level will turn out to
	 * be unreachable, so first we test for that.
	 * 
	 * We do this by finding the largest distance from any
	 * square to the nearest POSSGEM, by breadth-first search.
	 * If this is above a critical threshold, we abort and try
	 * again.
	 * 
	 * (This search is purely geometric, without regard to
	 * walls and long ways round.)
	 */
	dist = sc->positions;
	list = sc->positions + wh;
	for (i = 0; i < wh; i++)
	    dist[i] = -1;
	head = tail = 0;
	for (i = 0; i < wh; i++)
	    if (grid[i] == POSSGEM) {
		dist[i] = 0;
		list[tail++] = i;
	    }
	maxdist = 0;
	while (head < tail) {
	    int pos, x, y, d;

	    pos = list[head++];
	    if (maxdist < dist[pos])
		maxdist = dist[pos];

	    x = pos % w;
	    y = pos / w;

	    for (d = 0; d < DIRECTIONS; d++) {
		int x2, y2, p2;

		x2 = x + DX(d);
		y2 = y + DY(d);

		if (x2 >= 0 && x2 < w && y2 >= 0 && y2 < h) {
		    p2 = y2*w+x2;
		    if (dist[p2] < 0) {
			dist[p2] = dist[pos] + 1;
			list[tail++] = p2;
		    }
		}
	    }
	}
	assert(head == wh && tail == wh);

	/*
	 * Now abandon this grid and go round again if maxdist is
	 * above the required threshold.
	 * 
	 * We can safely start the threshold as low as 2. As we
	 * accumulate failed generation attempts, we gradually
	 * raise it as we get more desperate.
	 */
	if (maxdist > maxdist_threshold) {
	    tries++;
	    if (tries == 50) {
		maxdist_threshold++;
		tries = 0;
	    }
	    continue;
	}

	/*
	 * Now our reachable squares are plausibly evenly
	 * distributed over the grid. I'm not actually going to
	 * _enforce_ that I place the gems in such a way as not to
	 * increase that maxdist value; I'm now just going to trust
	 * to the RNG to pick a sensible subset of the POSSGEMs.
	 */
	j = 0;
	for (i = 0; i < wh; i++)
	    if (grid[i] == POSSGEM)
		list[j++] = i;
	shuffle(list, j, sizeof(*list), rs);
	for (i = 0; i < j; i++)
	    grid[list[i]] = (i < wh/5 ? GEM : BLANK);
	break;
    }

    free_scratch(sc);

    grid[wh] = '\0';

    return grid;
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    return gengrid(params->w, params->h, rs);
}

static char *validate_desc(game_params *params, char *desc)
{
    int w = params->w, h = params->h, wh = w*h;
    int starts = 0, gems = 0, i;

    for (i = 0; i < wh; i++) {
	if (!desc[i])
	    return "Not enough data to fill grid";
	if (desc[i] != WALL && desc[i] != START && desc[i] != STOP &&
	    desc[i] != GEM && desc[i] != MINE && desc[i] != BLANK)
	    return "Unrecognised character in game description";
	if (desc[i] == START)
	    starts++;
	if (desc[i] == GEM)
	    gems++;
    }
    if (desc[i])
	return "Too much data to fill grid";
    if (starts < 1)
	return "No starting square specified";
    if (starts > 1)
	return "More than one starting square specified";
    if (gems < 1)
	return "No gems specified";

    return NULL;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int w = params->w, h = params->h, wh = w*h;
    int i;
    game_state *state = snew(game_state);

    state->p = *params;		       /* structure copy */

    state->grid = snewn(wh, char);
    assert(strlen(desc) == wh);
    memcpy(state->grid, desc, wh);

    state->px = state->py = -1;
    state->gems = 0;
    for (i = 0; i < wh; i++) {
	if (state->grid[i] == START) {
	    state->grid[i] = STOP;
	    state->px = i % w;
	    state->py = i / w;
	} else if (state->grid[i] == GEM) {
	    state->gems++;
	}
    }

    assert(state->gems > 0);
    assert(state->px >= 0 && state->py >= 0);

    state->distance_moved = 0;
    state->dead = FALSE;

    return state;
}

static game_state *dup_game(game_state *state)
{
    int w = state->p.w, h = state->p.h, wh = w*h;
    game_state *ret = snew(game_state);

    ret->p = state->p;
    ret->px = state->px;
    ret->py = state->py;
    ret->gems = state->gems;
    ret->grid = snewn(wh, char);
    ret->distance_moved = state->distance_moved;
    ret->dead = FALSE;
    memcpy(ret->grid, state->grid, wh);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    return NULL;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

struct game_ui {
    float anim_length;
    int flashtype;
    int deaths;
    int just_made_move;
    int just_died;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->anim_length = 0.0F;
    ui->flashtype = 0;
    ui->deaths = 0;
    ui->just_made_move = FALSE;
    ui->just_died = FALSE;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    char buf[80];
    /*
     * The deaths counter needs preserving across a serialisation.
     */
    sprintf(buf, "D%d", ui->deaths);
    return dupstr(buf);
}

static void decode_ui(game_ui *ui, char *encoding)
{
    int p = 0;
    sscanf(encoding, "D%d%n", &ui->deaths, &p);
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    /*
     * Increment the deaths counter. We only do this if
     * ui->just_made_move is set (redoing a suicide move doesn't
     * kill you _again_), and also we only do it if the game isn't
     * completed (once you're finished, you can play).
     */
    if (!oldstate->dead && newstate->dead && ui->just_made_move &&
	newstate->gems) {
	ui->deaths++;
	ui->just_died = TRUE;
    } else {
	ui->just_died = FALSE;
    }
    ui->just_made_move = FALSE;
}

struct game_drawstate {
    game_params p;
    int tilesize;
    int started;
    unsigned short *grid;
    blitter *player_background;
    int player_bg_saved, pbgx, pbgy;
};

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER    (TILESIZE)
#define HIGHLIGHT_WIDTH (TILESIZE / 10)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int dir;
    char buf[80];

    dir = -1;

    if (button == LEFT_BUTTON) {
	/*
	 * Mouse-clicking near the target point (or, more
	 * accurately, in the appropriate octant) is an alternative
	 * way to input moves.
	 */

	if (FROMCOORD(x) != state->px || FROMCOORD(y) != state->py) {
	    int dx, dy;
	    float angle;

	    dx = FROMCOORD(x) - state->px;
	    dy = FROMCOORD(y) - state->py;
	    /* I pass dx,dy rather than dy,dx so that the octants
	     * end up the right way round. */
	    angle = atan2(dx, -dy);

	    angle = (angle + (PI/8)) / (PI/4);
	    assert(angle > -16.0F);
	    dir = (int)(angle + 16.0F) & 7;
	}
    } else if (button == CURSOR_UP || button == (MOD_NUM_KEYPAD | '8'))
        dir = 0;
    else if (button == CURSOR_DOWN || button == (MOD_NUM_KEYPAD | '2'))
        dir = 4;
    else if (button == CURSOR_LEFT || button == (MOD_NUM_KEYPAD | '4'))
        dir = 6;
    else if (button == CURSOR_RIGHT || button == (MOD_NUM_KEYPAD | '6'))
        dir = 2;
    else if (button == (MOD_NUM_KEYPAD | '7'))
        dir = 7;
    else if (button == (MOD_NUM_KEYPAD | '1'))
        dir = 5;
    else if (button == (MOD_NUM_KEYPAD | '9'))
        dir = 1;
    else if (button == (MOD_NUM_KEYPAD | '3'))
        dir = 3;

    if (dir < 0)
	return NULL;

    /*
     * Reject the move if we can't make it at all due to a wall
     * being in the way.
     */
    if (AT(w, h, state->grid, state->px+DX(dir), state->py+DY(dir)) == WALL)
	return NULL;

    /*
     * Reject the move if we're dead!
     */
    if (state->dead)
	return NULL;

    /*
     * Otherwise, we can make the move. All we need to specify is
     * the direction.
     */
    ui->just_made_move = TRUE;
    sprintf(buf, "%d", dir);
    return dupstr(buf);
}

static game_state *execute_move(game_state *state, char *move)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int dir = atoi(move);
    game_state *ret;

    if (dir < 0 || dir >= DIRECTIONS)
	return NULL;		       /* huh? */

    if (state->dead)
	return NULL;

    if (AT(w, h, state->grid, state->px+DX(dir), state->py+DY(dir)) == WALL)
	return NULL;		       /* wall in the way! */

    /*
     * Now make the move.
     */
    ret = dup_game(state);
    ret->distance_moved = 0;
    while (1) {
	ret->px += DX(dir);
	ret->py += DY(dir);
	ret->distance_moved++;

	if (AT(w, h, ret->grid, ret->px, ret->py) == GEM) {
	    LV_AT(w, h, ret->grid, ret->px, ret->py) = BLANK;
	    ret->gems--;
	}

	if (AT(w, h, ret->grid, ret->px, ret->py) == MINE) {
	    ret->dead = TRUE;
	    break;
	}

	if (AT(w, h, ret->grid, ret->px, ret->py) == STOP ||
	    AT(w, h, ret->grid, ret->px+DX(dir),
	       ret->py+DY(dir)) == WALL)
	    break;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = 2 * BORDER + 1 + params->w * TILESIZE;
    *y = 2 * BORDER + 1 + params->h * TILESIZE;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;

    assert(!ds->player_bg_saved);

    if (ds->player_background)
	blitter_free(dr, ds->player_background);
    ds->player_background = blitter_new(dr, TILESIZE, TILESIZE);
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    ret[COL_PLAYER * 3 + 0] = 0.0F;
    ret[COL_PLAYER * 3 + 1] = 1.0F;
    ret[COL_PLAYER * 3 + 2] = 0.0F;

    ret[COL_DEAD_PLAYER * 3 + 0] = 1.0F;
    ret[COL_DEAD_PLAYER * 3 + 1] = 0.0F;
    ret[COL_DEAD_PLAYER * 3 + 2] = 0.0F;

    ret[COL_MINE * 3 + 0] = 0.0F;
    ret[COL_MINE * 3 + 1] = 0.0F;
    ret[COL_MINE * 3 + 2] = 0.0F;

    ret[COL_GEM * 3 + 0] = 0.6F;
    ret[COL_GEM * 3 + 1] = 1.0F;
    ret[COL_GEM * 3 + 2] = 1.0F;

    for (i = 0; i < 3; i++) {
	ret[COL_WALL * 3 + i] = (3 * ret[COL_BACKGROUND * 3 + i] +
				 1 * ret[COL_HIGHLIGHT * 3 + i]) / 4;
    }

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    int w = state->p.w, h = state->p.h, wh = w*h;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;

    /* We can't allocate the blitter rectangle for the player background
     * until we know what size to make it. */
    ds->player_background = NULL;
    ds->player_bg_saved = FALSE;
    ds->pbgx = ds->pbgy = -1;

    ds->p = state->p;		       /* structure copy */
    ds->started = FALSE;
    ds->grid = snewn(wh, unsigned short);
    for (i = 0; i < wh; i++)
	ds->grid[i] = UNDRAWN;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_player(drawing *dr, game_drawstate *ds, int x, int y,
			int dead)
{
    if (dead) {
	int coords[DIRECTIONS*4];
	int d;

	for (d = 0; d < DIRECTIONS; d++) {
	    float x1, y1, x2, y2, x3, y3, len;

	    x1 = DX(d);
	    y1 = DY(d);
	    len = sqrt(x1*x1+y1*y1); x1 /= len; y1 /= len;

	    x3 = DX(d+1);
	    y3 = DY(d+1);
	    len = sqrt(x3*x3+y3*y3); x3 /= len; y3 /= len;

	    x2 = (x1+x3) / 4;
	    y2 = (y1+y3) / 4;

	    coords[d*4+0] = x + TILESIZE/2 + (int)((TILESIZE*3/7) * x1);
	    coords[d*4+1] = y + TILESIZE/2 + (int)((TILESIZE*3/7) * y1);
	    coords[d*4+2] = x + TILESIZE/2 + (int)((TILESIZE*3/7) * x2);
	    coords[d*4+3] = y + TILESIZE/2 + (int)((TILESIZE*3/7) * y2);
	}
	draw_polygon(dr, coords, DIRECTIONS*2, COL_DEAD_PLAYER, COL_OUTLINE);
    } else {
	draw_circle(dr, x + TILESIZE/2, y + TILESIZE/2,
		    TILESIZE/3, COL_PLAYER, COL_OUTLINE);
    }
    draw_update(dr, x, y, TILESIZE, TILESIZE);
}

#define FLASH_DEAD 0x100
#define FLASH_WIN  0x200
#define FLASH_MASK 0x300

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, int v)
{
    int tx = COORD(x), ty = COORD(y);
    int bg = (v & FLASH_DEAD ? COL_DEAD_PLAYER :
	      v & FLASH_WIN ? COL_HIGHLIGHT : COL_BACKGROUND);

    v &= ~FLASH_MASK;

    clip(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1);
    draw_rect(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1, bg);

    if (v == WALL) {
	int coords[6];

        coords[0] = tx + TILESIZE;
        coords[1] = ty + TILESIZE;
        coords[2] = tx + TILESIZE;
        coords[3] = ty + 1;
        coords[4] = tx + 1;
        coords[5] = ty + TILESIZE;
        draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);

        coords[0] = tx + 1;
        coords[1] = ty + 1;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);

        draw_rect(dr, tx + 1 + HIGHLIGHT_WIDTH, ty + 1 + HIGHLIGHT_WIDTH,
                  TILESIZE - 2*HIGHLIGHT_WIDTH,
		  TILESIZE - 2*HIGHLIGHT_WIDTH, COL_WALL);
    } else if (v == MINE) {
	int cx = tx + TILESIZE / 2;
	int cy = ty + TILESIZE / 2;
	int r = TILESIZE / 2 - 3;
	int coords[4*5*2];
	int xdx = 1, xdy = 0, ydx = 0, ydy = 1;
	int tdx, tdy, i;

	for (i = 0; i < 4*5*2; i += 5*2) {
	    coords[i+2*0+0] = cx - r/6*xdx + r*4/5*ydx;
	    coords[i+2*0+1] = cy - r/6*xdy + r*4/5*ydy;
	    coords[i+2*1+0] = cx - r/6*xdx + r*ydx;
	    coords[i+2*1+1] = cy - r/6*xdy + r*ydy;
	    coords[i+2*2+0] = cx + r/6*xdx + r*ydx;
	    coords[i+2*2+1] = cy + r/6*xdy + r*ydy;
	    coords[i+2*3+0] = cx + r/6*xdx + r*4/5*ydx;
	    coords[i+2*3+1] = cy + r/6*xdy + r*4/5*ydy;
	    coords[i+2*4+0] = cx + r*3/5*xdx + r*3/5*ydx;
	    coords[i+2*4+1] = cy + r*3/5*xdy + r*3/5*ydy;

	    tdx = ydx;
	    tdy = ydy;
	    ydx = xdx;
	    ydy = xdy;
	    xdx = -tdx;
	    xdy = -tdy;
	}

	draw_polygon(dr, coords, 5*4, COL_MINE, COL_MINE);

	draw_rect(dr, cx-r/3, cy-r/3, r/3, r/4, COL_HIGHLIGHT);
    } else if (v == STOP) {
	draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
		    TILESIZE*3/7, -1, COL_OUTLINE);
	draw_rect(dr, tx + TILESIZE*3/7, ty+1,
		  TILESIZE - 2*(TILESIZE*3/7) + 1, TILESIZE-1, bg);
	draw_rect(dr, tx+1, ty + TILESIZE*3/7,
		  TILESIZE-1, TILESIZE - 2*(TILESIZE*3/7) + 1, bg);
    } else if (v == GEM) {
	int coords[8];

	coords[0] = tx+TILESIZE/2;
	coords[1] = ty+TILESIZE*1/7;
	coords[2] = tx+TILESIZE*1/7;
	coords[3] = ty+TILESIZE/2;
	coords[4] = tx+TILESIZE/2;
	coords[5] = ty+TILESIZE-TILESIZE*1/7;
	coords[6] = tx+TILESIZE-TILESIZE*1/7;
	coords[7] = ty+TILESIZE/2;

	draw_polygon(dr, coords, 4, COL_GEM, COL_OUTLINE);
    }

    unclip(dr);
    draw_update(dr, tx, ty, TILESIZE, TILESIZE);
}

#define BASE_ANIM_LENGTH 0.1F
#define FLASH_LENGTH 0.3F

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int x, y;
    float ap;
    int player_dist;
    int flashtype;
    int gems, deaths;
    char status[256];

    if (flashtime &&
	!((int)(flashtime * 3 / FLASH_LENGTH) % 2))
	flashtype = ui->flashtype;
    else
	flashtype = 0;

    /*
     * Erase the player sprite.
     */
    if (ds->player_bg_saved) {
	assert(ds->player_background);
        blitter_load(dr, ds->player_background, ds->pbgx, ds->pbgy);
        draw_update(dr, ds->pbgx, ds->pbgy, TILESIZE, TILESIZE);
	ds->player_bg_saved = FALSE;
    }

    /*
     * Initialise a fresh drawstate.
     */
    if (!ds->started) {
	int wid, ht;

	/*
	 * Blank out the window initially.
	 */
	game_compute_size(&ds->p, TILESIZE, &wid, &ht);
	draw_rect(dr, 0, 0, wid, ht, COL_BACKGROUND);
	draw_update(dr, 0, 0, wid, ht);

	/*
	 * Draw the grid lines.
	 */
	for (y = 0; y <= h; y++)
	    draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y),
		      COL_LOWLIGHT);
	for (x = 0; x <= w; x++)
	    draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h),
		      COL_LOWLIGHT);

	ds->started = TRUE;
    }

    /*
     * If we're in the process of animating a move, let's start by
     * working out how far the player has moved from their _older_
     * state.
     */
    if (oldstate) {
	ap = animtime / ui->anim_length;
	player_dist = ap * (dir > 0 ? state : oldstate)->distance_moved;
    } else {
	player_dist = 0;
	ap = 0.0F;
    }

    /*
     * Draw the grid contents.
     * 
     * We count the gems as we go round this loop, for the purposes
     * of the status bar. Of course we have a gems counter in the
     * game_state already, but if we do the counting in this loop
     * then it tracks gems being picked up in a sliding move, and
     * updates one by one.
     */
    gems = 0;
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    unsigned short v = (unsigned char)state->grid[y*w+x];

	    /*
	     * Special case: if the player is in the process of
	     * moving over a gem, we draw the gem iff they haven't
	     * gone past it yet.
	     */
	    if (oldstate && oldstate->grid[y*w+x] != state->grid[y*w+x]) {
		/*
		 * Compute the distance from this square to the
		 * original player position.
		 */
		int dist = max(abs(x - oldstate->px), abs(y - oldstate->py));

		/*
		 * If the player has reached here, use the new grid
		 * element. Otherwise use the old one.
		 */
		if (player_dist < dist)
		    v = oldstate->grid[y*w+x];
		else
		    v = state->grid[y*w+x];
	    }

	    /*
	     * Special case: erase the mine the dead player is
	     * sitting on. Only at the end of the move.
	     */
	    if (v == MINE && !oldstate && state->dead &&
		x == state->px && y == state->py)
		v = BLANK;

	    if (v == GEM)
		gems++;

	    v |= flashtype;

	    if (ds->grid[y*w+x] != v) {
		draw_tile(dr, ds, x, y, v);
		ds->grid[y*w+x] = v;
	    }
	}

    /*
     * Gem counter in the status bar. We replace it with
     * `COMPLETED!' when it reaches zero ... or rather, when the
     * _current state_'s gem counter is zero. (Thus, `Gems: 0' is
     * shown between the collection of the last gem and the
     * completion of the move animation that did it.)
     */
    if (state->dead && (!oldstate || oldstate->dead))
	sprintf(status, "DEAD!");
    else if (state->gems || (oldstate && oldstate->gems))
	sprintf(status, "Gems: %d", gems);
    else
	sprintf(status, "COMPLETED!");
    /* We subtract one from the visible death counter if we're still
     * animating the move at the end of which the death took place. */
    deaths = ui->deaths;
    if (oldstate && ui->just_died) {
	assert(deaths > 0);
	deaths--;
    }
    if (deaths)
	sprintf(status + strlen(status), "   Deaths: %d", deaths);
    status_bar(dr, status);

    /*
     * Draw the player sprite.
     */
    assert(!ds->player_bg_saved);
    assert(ds->player_background);
    {
	int ox, oy, nx, ny;
	nx = COORD(state->px);
	ny = COORD(state->py);
	if (oldstate) {
	    ox = COORD(oldstate->px);
	    oy = COORD(oldstate->py);
	} else {
	    ox = nx;
	    oy = ny;
	}
	ds->pbgx = ox + ap * (nx - ox);
	ds->pbgy = oy + ap * (ny - oy);
    }
    blitter_save(dr, ds->player_background, ds->pbgx, ds->pbgy);
    draw_player(dr, ds, ds->pbgx, ds->pbgy, (state->dead && !oldstate));
    ds->player_bg_saved = TRUE;
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    int dist;
    if (dir > 0)
	dist = newstate->distance_moved;
    else
	dist = oldstate->distance_moved;
    ui->anim_length = sqrt(dist) * BASE_ANIM_LENGTH;
    return ui->anim_length;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->dead && newstate->dead) {
	ui->flashtype = FLASH_DEAD;
	return FLASH_LENGTH;
    } else if (oldstate->gems && !newstate->gems) {
	ui->flashtype = FLASH_WIN;
	return FLASH_LENGTH;
    }
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return TRUE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame inertia
#endif

const struct game thegame = {
    "Inertia", "games.inertia",
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
    FALSE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    FALSE, FALSE, game_print_size, game_print,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};
