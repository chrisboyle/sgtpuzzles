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
#include <limits.h>
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
#define DP1 (DIRECTIONS+1)
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
    COL_HINT,
    NCOLOURS
};

struct game_params {
    int w, h;
};

typedef struct soln {
    int refcount;
    int len;
    unsigned char *list;
} soln;

struct game_state {
    game_params p;
    int px, py;
    int gems;
    char *grid;
    int distance_moved;
    bool dead;
    bool cheated;
    int solnpos;
    soln *soln;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 10;
#ifdef PORTRAIT_SCREEN
    ret->h = 10;
#else
    ret->h = 8;
#endif
    return ret;
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

static const struct game_params inertia_presets[] = {
#ifdef PORTRAIT_SCREEN
    { 10, 10 },
    { 12, 12 },
    { 16, 16 },
#else
    { 10, 8 },
    { 15, 12 },
    { 20, 16 },
#endif
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params p, *ret;
    char *retname;
    char namebuf[80];

    if (i < 0 || i >= lenof(inertia_presets))
	return false;

    p = inertia_presets[i];
    ret = dup_params(&p);
    sprintf(namebuf, "%dx%d", ret->w, ret->h);
    retname = dupstr(namebuf);

    *params = ret;
    *name = retname;
    return true;
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

static char *encode_params(const game_params *params, bool full)
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
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
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
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";

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
    bool *reachable_from, *reachable_to;
    int *positions;
};

static struct solver_scratch *new_scratch(int w, int h)
{
    struct solver_scratch *sc = snew(struct solver_scratch);

    sc->reachable_from = snewn(w * h * DIRECTIONS, bool);
    sc->reachable_to = snewn(w * h * DIRECTIONS, bool);
    sc->positions = snewn(w * h * DIRECTIONS, int);

    return sc;
}

static void free_scratch(struct solver_scratch *sc)
{
    sfree(sc->reachable_from);
    sfree(sc->reachable_to);
    sfree(sc->positions);
    sfree(sc);
}

static bool can_go(int w, int h, char *grid,
                   int x1, int y1, int dir1, int x2, int y2, int dir2)
{
    /*
     * Returns true if we can transition directly from (x1,y1)
     * going in direction dir1, to (x2,y2) going in direction dir2.
     */

    /*
     * If we're actually in the middle of an unoccupyable square,
     * we cannot make any move.
     */
    if (AT(w, h, grid, x1, y1) == WALL ||
	AT(w, h, grid, x1, y1) == MINE)
	return false;

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
	return true;

    /*
     * If a move is capable of continuing here, then x1,y1,dir1 can
     * move one space further on.
     */
    if (x2 == x1+DX(dir1) && y2 == y1+DY(dir1) && dir1 == dir2 &&
	(AT(w, h, grid, x2, y2) == BLANK ||
	 AT(w, h, grid, x2, y2) == GEM ||
	 AT(w, h, grid, x2, y2) == STOP ||
	 AT(w, h, grid, x2, y2) == START))
	return true;

    /*
     * That's it.
     */
    return false;
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

    memset(sc->reachable_from, 0, wh * DIRECTIONS * sizeof(bool));
    memset(sc->reachable_to, 0, wh * DIRECTIONS * sizeof(bool));

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
	bool *reachable = (pass == 0 ? sc->reachable_from : sc->reachable_to);
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
	    reachable[index] = true;
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
		if (x2 >= 0 && x2 < w &&
		    y2 >= 0 && y2 < h &&
		    !reachable[i2]) {
		    bool ok;
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
			reachable[i2] = true;
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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    return gengrid(params->w, params->h, rs);
}

static const char *validate_desc(const game_params *params, const char *desc)
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

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
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
    state->dead = false;

    state->cheated = false;
    state->solnpos = 0;
    state->soln = NULL;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int w = state->p.w, h = state->p.h, wh = w*h;
    game_state *ret = snew(game_state);

    ret->p = state->p;
    ret->px = state->px;
    ret->py = state->py;
    ret->gems = state->gems;
    ret->grid = snewn(wh, char);
    ret->distance_moved = state->distance_moved;
    ret->dead = false;
    memcpy(ret->grid, state->grid, wh);
    ret->cheated = state->cheated;
    ret->soln = state->soln;
    if (ret->soln)
	ret->soln->refcount++;
    ret->solnpos = state->solnpos;

    return ret;
}

static void free_game(game_state *state)
{
    if (state->soln && --state->soln->refcount == 0) {
	sfree(state->soln->list);
	sfree(state->soln);
    }
    sfree(state->grid);
    sfree(state);
}

/*
 * Internal function used by solver.
 */
static int move_goes_to(int w, int h, char *grid, int x, int y, int d)
{
    int dr;

    /*
     * See where we'd get to if we made this move.
     */
    dr = -1;			       /* placate optimiser */
    while (1) {
	if (AT(w, h, grid, x+DX(d), y+DY(d)) == WALL) {
	    dr = DIRECTIONS;	       /* hit a wall, so end up stationary */
	    break;
	}
	x += DX(d);
	y += DY(d);
	if (AT(w, h, grid, x, y) == STOP) {
	    dr = DIRECTIONS;	       /* hit a stop, so end up stationary */
	    break;
	}
	if (AT(w, h, grid, x, y) == GEM) {
	    dr = d;		       /* hit a gem, so we're still moving */
	    break;
	}
	if (AT(w, h, grid, x, y) == MINE)
	    return -1;		       /* hit a mine, so move is invalid */
    }
    assert(dr >= 0);
    return (y*w+x)*DP1+dr;
}

static int compare_integers(const void *av, const void *bv)
{
    const int *a = (const int *)av;
    const int *b = (const int *)bv;
    if (*a < *b)
	return -1;
    else if (*a > *b)
	return +1;
    else
	return 0;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int w = currstate->p.w, h = currstate->p.h, wh = w*h;
    int *nodes, *nodeindex, *edges, *backedges, *edgei, *backedgei, *circuit;
    int nedges;
    int *dist, *dist2, *list;
    int *unvisited;
    int circuitlen, circuitsize;
    int head, tail, pass, i, j, n, x, y, d, dd;
    const char *err;
    char *soln, *p;

    /*
     * Before anything else, deal with the special case in which
     * all the gems are already collected.
     */
    for (i = 0; i < wh; i++)
	if (currstate->grid[i] == GEM)
	    break;
    if (i == wh) {
	*error = "Game is already solved";
	return NULL;
    }

    /*
     * Solving Inertia is a question of first building up the graph
     * of where you can get to from where, and secondly finding a
     * tour of the graph which takes in every gem.
     * 
     * This is of course a close cousin of the travelling salesman
     * problem, which is NP-complete; so I rather doubt that any
     * _optimal_ tour can be found in plausible time. Hence I'll
     * restrict myself to merely finding a not-too-bad one.
     * 
     * First construct the graph, by bfsing out move by move from
     * the current player position. Graph vertices will be
     * 	- every endpoint of a move (place the ball can be
     * 	  stationary)
     * 	- every gem (place the ball can go through in motion).
     * 	  Vertices of this type have an associated direction, since
     * 	  if a gem can be collected by sliding through it in two
     * 	  different directions it doesn't follow that you can
     * 	  change direction at it.
     * 
     * I'm going to refer to a non-directional vertex as
     * (y*w+x)*DP1+DIRECTIONS, and a directional one as
     * (y*w+x)*DP1+d.
     */

    /*
     * nodeindex[] maps node codes as shown above to numeric
     * indices in the nodes[] array.
     */
    nodeindex = snewn(DP1*wh, int);
    for (i = 0; i < DP1*wh; i++)
	nodeindex[i] = -1;

    /*
     * Do the bfs to find all the interesting graph nodes.
     */
    nodes = snewn(DP1*wh, int);
    head = tail = 0;

    nodes[tail] = (currstate->py * w + currstate->px) * DP1 + DIRECTIONS;
    nodeindex[nodes[0]] = tail;
    tail++;

    while (head < tail) {
	int nc = nodes[head++], nnc;

	d = nc % DP1;

	/*
	 * Plot all possible moves from this node. If the node is
	 * directed, there's only one.
	 */
	for (dd = 0; dd < DIRECTIONS; dd++) {
	    x = nc / DP1;
	    y = x / w;
	    x %= w;

	    if (d < DIRECTIONS && d != dd)
		continue;

	    nnc = move_goes_to(w, h, currstate->grid, x, y, dd);
	    if (nnc >= 0 && nnc != nc) {
		if (nodeindex[nnc] < 0) {
		    nodes[tail] = nnc;
		    nodeindex[nnc] = tail;
		    tail++;
		}
	    }
	}
    }
    n = head;

    /*
     * Now we know how many nodes we have, allocate the edge array
     * and go through setting up the edges.
     */
    edges = snewn(DIRECTIONS*n, int);
    edgei = snewn(n+1, int);
    nedges = 0;

    for (i = 0; i < n; i++) {
	int nc = nodes[i];

	edgei[i] = nedges;

	d = nc % DP1;
	x = nc / DP1;
	y = x / w;
	x %= w;

	for (dd = 0; dd < DIRECTIONS; dd++) {
	    int nnc;

	    if (d >= DIRECTIONS || d == dd) {
		nnc = move_goes_to(w, h, currstate->grid, x, y, dd);

		if (nnc >= 0 && nnc != nc)
		    edges[nedges++] = nodeindex[nnc];
	    }
	}
    }
    edgei[n] = nedges;

    /*
     * Now set up the backedges array.
     */
    backedges = snewn(nedges, int);
    backedgei = snewn(n+1, int);
    for (i = j = 0; i < nedges; i++) {
	while (j+1 < n && i >= edgei[j+1])
	    j++;
	backedges[i] = edges[i] * n + j;
    }
    qsort(backedges, nedges, sizeof(int), compare_integers);
    backedgei[0] = 0;
    for (i = j = 0; i < nedges; i++) {
	int k = backedges[i] / n;
	backedges[i] %= n;
	while (j < k)
	    backedgei[++j] = i;
    }
    backedgei[n] = nedges;

    /*
     * Set up the initial tour. At all times, our tour is a circuit
     * of graph vertices (which may, and probably will often,
     * repeat vertices). To begin with, it's got exactly one vertex
     * in it, which is the player's current starting point.
     */
    circuitsize = 256;
    circuit = snewn(circuitsize, int);
    circuitlen = 0;
    circuit[circuitlen++] = 0;	       /* node index 0 is the starting posn */

    /*
     * Track which gems are as yet unvisited.
     */
    unvisited = snewn(wh, int);
    for (i = 0; i < wh; i++)
	unvisited[i] = false;
    for (i = 0; i < wh; i++)
	if (currstate->grid[i] == GEM)
	    unvisited[i] = true;

    /*
     * Allocate space for doing bfses inside the main loop.
     */
    dist = snewn(n, int);
    dist2 = snewn(n, int);
    list = snewn(n, int);

    err = NULL;
    soln = NULL;

    /*
     * Now enter the main loop, in each iteration of which we
     * extend the tour to take in an as yet uncollected gem.
     */
    while (1) {
	int target, n1, n2, bestdist, extralen, targetpos;

#ifdef TSP_DIAGNOSTICS
	printf("circuit is");
	for (i = 0; i < circuitlen; i++) {
	    int nc = nodes[circuit[i]];
	    printf(" (%d,%d,%d)", nc/DP1%w, nc/(DP1*w), nc%DP1);
	}
	printf("\n");
	printf("moves are ");
	x = nodes[circuit[0]] / DP1 % w;
	y = nodes[circuit[0]] / DP1 / w;
	for (i = 1; i < circuitlen; i++) {
	    int x2, y2, dx, dy;
	    if (nodes[circuit[i]] % DP1 != DIRECTIONS)
		continue;
	    x2 = nodes[circuit[i]] / DP1 % w;
	    y2 = nodes[circuit[i]] / DP1 / w;
	    dx = (x2 > x ? +1 : x2 < x ? -1 : 0);
	    dy = (y2 > y ? +1 : y2 < y ? -1 : 0);
	    for (d = 0; d < DIRECTIONS; d++)
		if (DX(d) == dx && DY(d) == dy)
		    printf("%c", "89632147"[d]);
	    x = x2;
	    y = y2;
	}
	printf("\n");
#endif

	/*
	 * First, start a pair of bfses at _every_ vertex currently
	 * in the tour, and extend them outwards to find the
	 * nearest as yet unreached gem vertex.
	 * 
	 * This is largely a heuristic: we could pick _any_ doubly
	 * reachable node here and still get a valid tour as
	 * output. I hope that picking a nearby one will result in
	 * generally good tours.
	 */
	for (pass = 0; pass < 2; pass++) {
	    int *ep = (pass == 0 ? edges : backedges);
	    int *ei = (pass == 0 ? edgei : backedgei);
	    int *dp = (pass == 0 ? dist : dist2);
	    head = tail = 0;
	    for (i = 0; i < n; i++)
		dp[i] = -1;
	    for (i = 0; i < circuitlen; i++) {
		int ni = circuit[i];
		if (dp[ni] < 0) {
		    dp[ni] = 0;
		    list[tail++] = ni;
		}
	    }
	    while (head < tail) {
		int ni = list[head++];
		for (i = ei[ni]; i < ei[ni+1]; i++) {
		    int ti = ep[i];
		    if (ti >= 0 && dp[ti] < 0) {
			dp[ti] = dp[ni] + 1;
			list[tail++] = ti;
		    }
		}
	    }
	}
	/* Now find the nearest unvisited gem. */
	bestdist = -1;
	target = -1;
	for (i = 0; i < n; i++) {
	    if (unvisited[nodes[i] / DP1] &&
		dist[i] >= 0 && dist2[i] >= 0) {
		int thisdist = dist[i] + dist2[i];
		if (bestdist < 0 || bestdist > thisdist) {
		    bestdist = thisdist;
		    target = i;
		}
	    }
	}

	if (target < 0) {
	    /*
	     * If we get to here, we haven't found a gem we can get
	     * at all, which means we terminate this loop.
	     */
	    break;
	}

	/*
	 * Now we have a graph vertex at list[tail-1] which is an
	 * unvisited gem. We want to add that vertex to our tour.
	 * So we run two more breadth-first searches: one starting
	 * from that vertex and following forward edges, and
	 * another starting from the same vertex and following
	 * backward edges. This allows us to determine, for each
	 * node on the current tour, how quickly we can get both to
	 * and from the target vertex from that node.
	 */
#ifdef TSP_DIAGNOSTICS
	printf("target node is %d (%d,%d,%d)\n", target, nodes[target]/DP1%w,
	       nodes[target]/DP1/w, nodes[target]%DP1);
#endif

	for (pass = 0; pass < 2; pass++) {
	    int *ep = (pass == 0 ? edges : backedges);
	    int *ei = (pass == 0 ? edgei : backedgei);
	    int *dp = (pass == 0 ? dist : dist2);

	    for (i = 0; i < n; i++)
		dp[i] = -1;
	    head = tail = 0;

	    dp[target] = 0;
	    list[tail++] = target;

	    while (head < tail) {
		int ni = list[head++];
		for (i = ei[ni]; i < ei[ni+1]; i++) {
		    int ti = ep[i];
		    if (ti >= 0 && dp[ti] < 0) {
			dp[ti] = dp[ni] + 1;
/*printf("pass %d: set dist of vertex %d to %d (via %d)\n", pass, ti, dp[ti], ni);*/
			list[tail++] = ti;
		    }
		}
	    }
	}

	/*
	 * Now for every node n, dist[n] gives the length of the
	 * shortest path from the target vertex to n, and dist2[n]
	 * gives the length of the shortest path from n to the
	 * target vertex.
	 * 
	 * Our next step is to search linearly along the tour to
	 * find the optimum place to insert a trip to the target
	 * vertex and back. Our two options are either
	 *  (a) to find two adjacent vertices A,B in the tour and
	 * 	replace the edge A->B with the path A->target->B
	 *  (b) to find a single vertex X in the tour and replace
	 * 	it with the complete round trip X->target->X.
	 * We do whichever takes the fewest moves.
	 */
	n1 = n2 = -1;
	bestdist = -1;
	for (i = 0; i < circuitlen; i++) {
	    int thisdist;

	    /*
	     * Try a round trip from vertex i.
	     */
	    if (dist[circuit[i]] >= 0 &&
		dist2[circuit[i]] >= 0) {
		thisdist = dist[circuit[i]] + dist2[circuit[i]];
		if (bestdist < 0 || thisdist < bestdist) {
		    bestdist = thisdist;
		    n1 = n2 = i;
		}
	    }

	    /*
	     * Try a trip from vertex i via target to vertex i+1.
	     */
	    if (i+1 < circuitlen &&
		dist2[circuit[i]] >= 0 &&
		dist[circuit[i+1]] >= 0) {
		thisdist = dist2[circuit[i]] + dist[circuit[i+1]];
		if (bestdist < 0 || thisdist < bestdist) {
		    bestdist = thisdist;
		    n1 = i;
		    n2 = i+1;
		}
	    }
	}
	if (bestdist < 0) {
	    /*
	     * We couldn't find a round trip taking in this gem _at
	     * all_. Give up.
	     */
	    err = "Unable to find a solution from this starting point";
	    break;
	}
#ifdef TSP_DIAGNOSTICS
	printf("insertion point: n1=%d, n2=%d, dist=%d\n", n1, n2, bestdist);
#endif

#ifdef TSP_DIAGNOSTICS
	printf("circuit before lengthening is");
	for (i = 0; i < circuitlen; i++) {
	    printf(" %d", circuit[i]);
	}
	printf("\n");
#endif

	/*
	 * Now actually lengthen the tour to take in this round
	 * trip.
	 */
	extralen = dist2[circuit[n1]] + dist[circuit[n2]];
	if (n1 != n2)
	    extralen--;
	circuitlen += extralen;
	if (circuitlen >= circuitsize) {
	    circuitsize = circuitlen + 256;
	    circuit = sresize(circuit, circuitsize, int);
	}
	memmove(circuit + n2 + extralen, circuit + n2,
		(circuitlen - n2 - extralen) * sizeof(int));
	n2 += extralen;

#ifdef TSP_DIAGNOSTICS
	printf("circuit in middle of lengthening is");
	for (i = 0; i < circuitlen; i++) {
	    printf(" %d", circuit[i]);
	}
	printf("\n");
#endif

	/*
	 * Find the shortest-path routes to and from the target,
	 * and write them into the circuit.
	 */
	targetpos = n1 + dist2[circuit[n1]];
	assert(targetpos - dist2[circuit[n1]] == n1);
	assert(targetpos + dist[circuit[n2]] == n2);
	for (pass = 0; pass < 2; pass++) {
	    int dir = (pass == 0 ? -1 : +1);
	    int *ep = (pass == 0 ? backedges : edges);
	    int *ei = (pass == 0 ? backedgei : edgei);
	    int *dp = (pass == 0 ? dist : dist2);
	    int nn = (pass == 0 ? n2 : n1);
	    int ni = circuit[nn], ti, dest = nn;

	    while (1) {
		circuit[dest] = ni;
		if (dp[ni] == 0)
		    break;
		dest += dir;
		ti = -1;
/*printf("pass %d: looking at vertex %d\n", pass, ni);*/
		for (i = ei[ni]; i < ei[ni+1]; i++) {
		    ti = ep[i];
		    if (ti >= 0 && dp[ti] == dp[ni] - 1)
			break;
		}
		assert(i < ei[ni+1] && ti >= 0);
		ni = ti;
	    }
	}

#ifdef TSP_DIAGNOSTICS
	printf("circuit after lengthening is");
	for (i = 0; i < circuitlen; i++) {
	    printf(" %d", circuit[i]);
	}
	printf("\n");
#endif

	/*
	 * Finally, mark all gems that the new piece of circuit
	 * passes through as visited.
	 */
	for (i = n1; i <= n2; i++) {
	    int pos = nodes[circuit[i]] / DP1;
	    assert(pos >= 0 && pos < wh);
	    unvisited[pos] = false;
	}
    }

#ifdef TSP_DIAGNOSTICS
    printf("before reduction, moves are ");
    x = nodes[circuit[0]] / DP1 % w;
    y = nodes[circuit[0]] / DP1 / w;
    for (i = 1; i < circuitlen; i++) {
	int x2, y2, dx, dy;
	if (nodes[circuit[i]] % DP1 != DIRECTIONS)
	    continue;
	x2 = nodes[circuit[i]] / DP1 % w;
	y2 = nodes[circuit[i]] / DP1 / w;
	dx = (x2 > x ? +1 : x2 < x ? -1 : 0);
	dy = (y2 > y ? +1 : y2 < y ? -1 : 0);
	for (d = 0; d < DIRECTIONS; d++)
	    if (DX(d) == dx && DY(d) == dy)
		printf("%c", "89632147"[d]);
	x = x2;
	y = y2;
    }
    printf("\n");
#endif

    /*
     * That's got a basic solution. Now optimise it by removing
     * redundant sections of the circuit: it's entirely possible
     * that a piece of circuit we carefully inserted at one stage
     * to collect a gem has become pointless because the steps
     * required to collect some _later_ gem necessarily passed
     * through the same one.
     * 
     * So first we go through and work out how many times each gem
     * is collected. Then we look for maximal sections of circuit
     * which are redundant in the sense that their removal would
     * not reduce any gem's collection count to zero, and replace
     * each one with a bfs-derived fastest path between their
     * endpoints.
     */
    while (1) {
	int oldlen = circuitlen;
	int dir;

	for (dir = +1; dir >= -1; dir -= 2) {

	    for (i = 0; i < wh; i++)
		unvisited[i] = 0;
	    for (i = 0; i < circuitlen; i++) {
		int xy = nodes[circuit[i]] / DP1;
		if (currstate->grid[xy] == GEM)
		    unvisited[xy]++;
	    }

	    /*
	     * If there's any gem we didn't end up visiting at all,
	     * give up.
	     */
	    for (i = 0; i < wh; i++) {
		if (currstate->grid[i] == GEM && unvisited[i] == 0) {
		    err = "Unable to find a solution from this starting point";
		    break;
		}
	    }
	    if (i < wh)
		break;

	    for (i = j = (dir > 0 ? 0 : circuitlen-1);
		 i < circuitlen && i >= 0;
		 i += dir) {
		int xy = nodes[circuit[i]] / DP1;
		if (currstate->grid[xy] == GEM && unvisited[xy] > 1) {
		    unvisited[xy]--;
		} else if (currstate->grid[xy] == GEM || i == circuitlen-1) {
		    /*
		     * circuit[i] collects a gem for the only time,
		     * or is the last node in the circuit.
		     * Therefore it cannot be removed; so we now
		     * want to replace the path from circuit[j] to
		     * circuit[i] with a bfs-shortest path.
		     */
		    int p, q, k, dest, ni, ti, thisdist;

		    /*
		     * Set up the upper and lower bounds of the
		     * reduced section.
		     */
		    p = min(i, j);
		    q = max(i, j);

#ifdef TSP_DIAGNOSTICS
		    printf("optimising section from %d - %d\n", p, q);
#endif

		    for (k = 0; k < n; k++)
			dist[k] = -1;
		    head = tail = 0;

		    dist[circuit[p]] = 0;
		    list[tail++] = circuit[p];

		    while (head < tail && dist[circuit[q]] < 0) {
			int ni = list[head++];
			for (k = edgei[ni]; k < edgei[ni+1]; k++) {
			    int ti = edges[k];
			    if (ti >= 0 && dist[ti] < 0) {
				dist[ti] = dist[ni] + 1;
				list[tail++] = ti;
			    }
			}
		    }

		    thisdist = dist[circuit[q]];
		    assert(thisdist >= 0 && thisdist <= q-p);

		    memmove(circuit+p+thisdist, circuit+q,
			    (circuitlen - q) * sizeof(int));
		    circuitlen -= q-p;
		    q = p + thisdist;
		    circuitlen += q-p;

		    if (dir > 0)
			i = q;	       /* resume loop from the right place */

#ifdef TSP_DIAGNOSTICS
		    printf("new section runs from %d - %d\n", p, q);
#endif

		    dest = q;
		    assert(dest >= 0);
		    ni = circuit[q];

		    while (1) {
			/* printf("dest=%d circuitlen=%d ni=%d dist[ni]=%d\n", dest, circuitlen, ni, dist[ni]); */
			circuit[dest] = ni;
			if (dist[ni] == 0)
			    break;
			dest--;
			ti = -1;
			for (k = backedgei[ni]; k < backedgei[ni+1]; k++) {
			    ti = backedges[k];
			    if (ti >= 0 && dist[ti] == dist[ni] - 1)
				break;
			}
			assert(k < backedgei[ni+1] && ti >= 0);
			ni = ti;
		    }

		    /*
		     * Now re-increment the visit counts for the
		     * new path.
		     */
		    while (++p < q) {
			int xy = nodes[circuit[p]] / DP1;
			if (currstate->grid[xy] == GEM)
			    unvisited[xy]++;
		    }

		    j = i;

#ifdef TSP_DIAGNOSTICS
		    printf("during reduction, circuit is");
		    for (k = 0; k < circuitlen; k++) {
			int nc = nodes[circuit[k]];
			printf(" (%d,%d,%d)", nc/DP1%w, nc/(DP1*w), nc%DP1);
		    }
		    printf("\n");
		    printf("moves are ");
		    x = nodes[circuit[0]] / DP1 % w;
		    y = nodes[circuit[0]] / DP1 / w;
		    for (k = 1; k < circuitlen; k++) {
			int x2, y2, dx, dy;
			if (nodes[circuit[k]] % DP1 != DIRECTIONS)
			    continue;
			x2 = nodes[circuit[k]] / DP1 % w;
			y2 = nodes[circuit[k]] / DP1 / w;
			dx = (x2 > x ? +1 : x2 < x ? -1 : 0);
			dy = (y2 > y ? +1 : y2 < y ? -1 : 0);
			for (d = 0; d < DIRECTIONS; d++)
			    if (DX(d) == dx && DY(d) == dy)
				printf("%c", "89632147"[d]);
			x = x2;
			y = y2;
		    }
		    printf("\n");
#endif
		}
	    }

#ifdef TSP_DIAGNOSTICS
	    printf("after reduction, moves are ");
	    x = nodes[circuit[0]] / DP1 % w;
	    y = nodes[circuit[0]] / DP1 / w;
	    for (i = 1; i < circuitlen; i++) {
		int x2, y2, dx, dy;
		if (nodes[circuit[i]] % DP1 != DIRECTIONS)
		    continue;
		x2 = nodes[circuit[i]] / DP1 % w;
		y2 = nodes[circuit[i]] / DP1 / w;
		dx = (x2 > x ? +1 : x2 < x ? -1 : 0);
		dy = (y2 > y ? +1 : y2 < y ? -1 : 0);
		for (d = 0; d < DIRECTIONS; d++)
		    if (DX(d) == dx && DY(d) == dy)
			printf("%c", "89632147"[d]);
		x = x2;
		y = y2;
	    }
	    printf("\n");
#endif
	}

	/*
	 * If we've managed an entire reduction pass in each
	 * direction and not made the solution any shorter, we're
	 * _really_ done.
	 */
	if (circuitlen == oldlen)
	    break;
    }

    /*
     * Encode the solution as a move string.
     */
    if (!err) {
	soln = snewn(circuitlen+2, char);
	p = soln;
	*p++ = 'S';
	x = nodes[circuit[0]] / DP1 % w;
	y = nodes[circuit[0]] / DP1 / w;
	for (i = 1; i < circuitlen; i++) {
	    int x2, y2, dx, dy;
	    if (nodes[circuit[i]] % DP1 != DIRECTIONS)
		continue;
	    x2 = nodes[circuit[i]] / DP1 % w;
	    y2 = nodes[circuit[i]] / DP1 / w;
	    dx = (x2 > x ? +1 : x2 < x ? -1 : 0);
	    dy = (y2 > y ? +1 : y2 < y ? -1 : 0);
	    for (d = 0; d < DIRECTIONS; d++)
		if (DX(d) == dx && DY(d) == dy) {
		    *p++ = '0' + d;
		    break;
		}
	    assert(d < DIRECTIONS);
	    x = x2;
	    y = y2;
	}
	*p++ = '\0';
	assert(p - soln < circuitlen+2);
    }

    sfree(list);
    sfree(dist);
    sfree(dist2);
    sfree(unvisited);
    sfree(circuit);
    sfree(backedgei);
    sfree(backedges);
    sfree(edgei);
    sfree(edges);
    sfree(nodeindex);
    sfree(nodes);

    if (err)
	*error = err;

    return soln;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->p.w, h = state->p.h, r, c;
    int cw = 4, ch = 2, gw = cw*w + 2, gh = ch * h + 1, len = gw * gh;
    char *board = snewn(len + 1, char);

    sprintf(board, "%*s+\n", len - 2, "");

    for (r = 0; r < h; ++r) {
	for (c = 0; c < w; ++c) {
	    int cell = r*ch*gw + cw*c, center = cell + gw*ch/2 + cw/2;
	    int i = r*w + c;
	    switch (state->grid[i]) {
	    case BLANK: break;
	    case GEM: board[center] = 'o'; break;
	    case MINE: board[center] = 'M'; break;
	    case STOP: board[center-1] = '('; board[center+1] = ')'; break;
	    case WALL: memset(board + center - 1, 'X', 3);
	    }

	    if (r == state->py && c == state->px) {
		if (!state->dead) board[center] = '@';
		else memcpy(board + center - 1, ":-(", 3);
	    }
	    board[cell] = '+';
	    memset(board + cell + 1, '-', cw - 1);
	    for (i = 1; i < ch; ++i) board[cell + i*gw] = '|';
	}
	for (c = 0; c < ch; ++c) {
	    board[(r*ch+c)*gw + gw - 2] = "|+"[!c];
	    board[(r*ch+c)*gw + gw - 1] = '\n';
	}
    }
    memset(board + len - gw, '-', gw - 2);
    for (c = 0; c < w; ++c) board[len - gw + cw*c] = '+';

    return board;
}

struct game_ui {
    float anim_length;
    int flashtype;
    int deaths;
    bool just_made_move;
    bool just_died;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->anim_length = 0.0F;
    ui->flashtype = 0;
    ui->deaths = 0;
    ui->just_made_move = false;
    ui->just_died = false;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    char buf[80];
    /*
     * The deaths counter needs preserving across a serialisation.
     */
    sprintf(buf, "D%d", ui->deaths);
    return dupstr(buf);
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    int p = 0;
    sscanf(encoding, "D%d%n", &ui->deaths, &p);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    /*
     * Increment the deaths counter. We only do this if
     * ui->just_made_move is set (redoing a suicide move doesn't
     * kill you _again_), and also we only do it if the game wasn't
     * already completed (once you're finished, you can play).
     */
    if (!oldstate->dead && newstate->dead && ui->just_made_move &&
	oldstate->gems) {
	ui->deaths++;
	ui->just_died = true;
    } else {
	ui->just_died = false;
    }
    ui->just_made_move = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) &&
        state->soln && state->solnpos < state->soln->len)
        return "Advance";
    return "";
}

struct game_drawstate {
    game_params p;
    int tilesize;
    bool started;
    unsigned short *grid;
    blitter *player_background;
    bool player_bg_saved;
    int pbgx, pbgy;
};

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#ifdef SMALL_SCREEN
#define BORDER    (TILESIZE / 4)
#else
#define BORDER    (TILESIZE)
#endif
#define HIGHLIGHT_WIDTH (TILESIZE / 10)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
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

	    angle = (angle + (float)(PI/8)) / (float)(PI/4);
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
    else if (IS_CURSOR_SELECT(button) &&
             state->soln && state->solnpos < state->soln->len)
	dir = state->soln->list[state->solnpos];

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
    ui->just_made_move = true;
    sprintf(buf, "%d", dir);
    return dupstr(buf);
}

static void install_new_solution(game_state *ret, const char *move)
{
    int i;
    soln *sol;
    assert (*move == 'S');
    ++move;

    sol = snew(soln);
    sol->len = strlen(move);
    sol->list = snewn(sol->len, unsigned char);
    for (i = 0; i < sol->len; ++i) sol->list[i] = move[i] - '0';

    if (ret->soln && --ret->soln->refcount == 0) {
	sfree(ret->soln->list);
	sfree(ret->soln);
    }

    ret->soln = sol;
    sol->refcount = 1;

    ret->cheated = true;
    ret->solnpos = 0;
}

static void discard_solution(game_state *ret)
{
    --ret->soln->refcount;
    assert(ret->soln->refcount > 0); /* ret has a soln-pointing dup */
    ret->soln = NULL;
    ret->solnpos = 0;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int dir;
    game_state *ret;

    if (*move == 'S') {
	/*
	 * This is a solve move, so we don't actually _change_ the
	 * grid but merely set up a stored solution path.
	 */
        if (move[1] == '\0') return NULL; /* Solution must be non-empty. */
	ret = dup_game(state);
	install_new_solution(ret, move);
	return ret;
    }

    dir = atoi(move);
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
	    ret->dead = true;
	    break;
	}

	if (AT(w, h, ret->grid, ret->px, ret->py) == STOP ||
	    AT(w, h, ret->grid, ret->px+DX(dir),
	       ret->py+DY(dir)) == WALL)
	    break;
    }

    if (ret->soln) {
	if (ret->dead || ret->gems == 0)
	    discard_solution(ret);
	else if (ret->soln->list[ret->solnpos] == dir &&
            ret->solnpos+1 < ret->soln->len)
	    ++ret->solnpos;
	else {
	    const char *error = NULL;
            char *soln = solve_game(NULL, ret, NULL, &error);
	    if (!error) {
		install_new_solution(ret, soln);
		sfree(soln);
	    } else discard_solution(ret);
	}
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

    *x = 2 * BORDER + 1 + params->w * TILESIZE;
    *y = 2 * BORDER + 1 + params->h * TILESIZE;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;

    assert(!ds->player_background);    /* set_size is never called twice */
    assert(!ds->player_bg_saved);

    ds->player_background = blitter_new(dr, TILESIZE, TILESIZE);
}

static float *game_colours(frontend *fe, int *ncolours)
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

    ret[COL_HINT * 3 + 0] = 1.0F;
    ret[COL_HINT * 3 + 1] = 1.0F;
    ret[COL_HINT * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->p.w, h = state->p.h, wh = w*h;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;

    /* We can't allocate the blitter rectangle for the player background
     * until we know what size to make it. */
    ds->player_background = NULL;
    ds->player_bg_saved = false;
    ds->pbgx = ds->pbgy = -1;

    ds->p = state->p;		       /* structure copy */
    ds->started = false;
    ds->grid = snewn(wh, unsigned short);
    for (i = 0; i < wh; i++)
	ds->grid[i] = UNDRAWN;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    if (ds->player_background)
	blitter_free(dr, ds->player_background);
    sfree(ds->grid);
    sfree(ds);
}

static void draw_player(drawing *dr, game_drawstate *ds, int x, int y,
			bool dead, int hintdir)
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

    if (!dead && hintdir >= 0) {
	float scale = (DX(hintdir) && DY(hintdir) ? 0.8F : 1.0F);
	int ax = (TILESIZE*2/5) * scale * DX(hintdir);
	int ay = (TILESIZE*2/5) * scale * DY(hintdir);
	int px = -ay, py = ax;
	int ox = x + TILESIZE/2, oy = y + TILESIZE/2;
	int coords[14], *c;

	c = coords;
	*c++ = ox + px/9;
	*c++ = oy + py/9;
	*c++ = ox + px/9 + ax*2/3;
	*c++ = oy + py/9 + ay*2/3;
	*c++ = ox + px/3 + ax*2/3;
	*c++ = oy + py/3 + ay*2/3;
	*c++ = ox + ax;
	*c++ = oy + ay;
	*c++ = ox - px/3 + ax*2/3;
	*c++ = oy - py/3 + ay*2/3;
	*c++ = ox - px/9 + ax*2/3;
	*c++ = oy - py/9 + ay*2/3;
	*c++ = ox - px/9;
	*c++ = oy - py/9;
	draw_polygon(dr, coords, 7, COL_HINT, COL_OUTLINE);
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

	draw_circle(dr, cx, cy, 5*r/6, COL_MINE, COL_MINE);
	draw_rect(dr, cx - r/6, cy - r, 2*(r/6)+1, 2*r+1, COL_MINE);
	draw_rect(dr, cx - r, cy - r/6, 2*r+1, 2*(r/6)+1, COL_MINE);
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
	coords[1] = ty+TILESIZE/2-TILESIZE*5/14;
	coords[2] = tx+TILESIZE/2-TILESIZE*5/14;
	coords[3] = ty+TILESIZE/2;
	coords[4] = tx+TILESIZE/2;
	coords[5] = ty+TILESIZE/2+TILESIZE*5/14;
	coords[6] = tx+TILESIZE/2+TILESIZE*5/14;
	coords[7] = ty+TILESIZE/2;

	draw_polygon(dr, coords, 4, COL_GEM, COL_OUTLINE);
    }

    unclip(dr);
    draw_update(dr, tx, ty, TILESIZE, TILESIZE);
}

#define BASE_ANIM_LENGTH 0.1F
#define FLASH_LENGTH 0.3F

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
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
	ds->player_bg_saved = false;
    }

    /*
     * Initialise a fresh drawstate.
     */
    if (!ds->started) {
	/*
	 * Draw the grid lines.
	 */
	for (y = 0; y <= h; y++)
	    draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y),
		      COL_LOWLIGHT);
	for (x = 0; x <= w; x++)
	    draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h),
		      COL_LOWLIGHT);

	ds->started = true;
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
    if (state->dead && (!oldstate || oldstate->dead)) {
	sprintf(status, "DEAD!");
    } else if (state->gems || (oldstate && oldstate->gems)) {
	if (state->cheated)
	    sprintf(status, "Auto-solver used. ");
	else
	    *status = '\0';
	sprintf(status + strlen(status), "Gems: %d", gems);
    } else if (state->cheated) {
	sprintf(status, "Auto-solved.");
    } else {
	sprintf(status, "COMPLETED!");
    }
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
    draw_player(dr, ds, ds->pbgx, ds->pbgy,
		(state->dead && !oldstate),
		(!oldstate && state->soln ?
		 state->soln->list[state->solnpos] : -1));
    ds->player_bg_saved = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    int dist;
    if (dir > 0)
	dist = newstate->distance_moved;
    else
	dist = oldstate->distance_moved;
    ui->anim_length = sqrt(dist) * BASE_ANIM_LENGTH;
    return ui->anim_length;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
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

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    *x = ds->pbgx;
    *y = ds->pbgy;
    *w = *h = TILESIZE;
}

static int game_status(const game_state *state)
{
    /*
     * We never report the game as lost, on the grounds that if the
     * player has died they're quite likely to want to undo and carry
     * on.
     */
    return state->gems == 0 ? +1 : 0;
}

#ifdef COMBINED
#define thegame inertia
#endif

const struct game thegame = {
    "Inertia", "games.inertia", "inertia",
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    true, solve_game,
    true, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    current_key_label,
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};
