/*
 * slant.c: Puzzle from nikoli.co.jp involving drawing a diagonal
 * line through each square of a grid.
 */

/*
 * In this puzzle you have a grid of squares, each of which must
 * contain a diagonal line; you also have clue numbers placed at
 * _points_ of that grid, which means there's a (w+1) x (h+1) array
 * of possible clue positions.
 * 
 * I'm therefore going to adopt a rigid convention throughout this
 * source file of using w and h for the dimensions of the grid of
 * squares, and W and H for the dimensions of the grid of points.
 * Thus, W == w+1 and H == h+1 always.
 * 
 * Clue arrays will be W*H `signed char's, and the clue at each
 * point will be a number from 0 to 4, or -1 if there's no clue.
 * 
 * Solution arrays will be W*H `signed char's, and the number at
 * each point will be +1 for a forward slash (/), -1 for a
 * backslash (\), and 0 for unknown.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_INK,
    NCOLOURS
};

struct game_params {
    int w, h;
};

typedef struct game_clues {
    int w, h;
    signed char *clues;
    int *dsf;			       /* scratch space for completion check */
    int refcount;
} game_clues;

struct game_state {
    struct game_params p;
    game_clues *clues;
    signed char *soln;
    int completed;
    int used_solve;		       /* used to suppress completion flash */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 8;

    return ret;
}

static const struct game_params slant_presets[] = {
  {5, 5},
  {8, 8},
  {12, 10},
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(slant_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = slant_presets[i];

    sprintf(str, "%dx%d", ret->w, ret->h);

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

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
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
     * (At least at the time of writing this comment) The grid
     * generator is actually capable of handling even zero grid
     * dimensions without crashing. Puzzles with a zero-area grid
     * are a bit boring, though, because they're already solved :-)
     */

    if (params->w < 1 || params->h < 1)
	return "Width and height must both be at least one";

    return NULL;
}

/*
 * Utility function used by both the solver and the filled-grid
 * generator.
 */

static void fill_square(int w, int h, int y, int x, int v,
			signed char *soln, int *dsf)
{
    int W = w+1 /*, H = h+1 */;

    soln[y*w+x] = v;

    if (v < 0)
	dsf_merge(dsf, y*W+x, (y+1)*W+(x+1));
    else
	dsf_merge(dsf, y*W+(x+1), (y+1)*W+x);
}

/*
 * Scratch space for solver.
 */
struct solver_scratch {
    int *dsf;
};

struct solver_scratch *new_scratch(int w, int h)
{
    int W = w+1, H = h+1;
    struct solver_scratch *ret = snew(struct solver_scratch);
    ret->dsf = snewn(W*H, int);
    return ret;
}

void free_scratch(struct solver_scratch *sc)
{
    sfree(sc->dsf);
    sfree(sc);
}

/*
 * Solver. Returns 0 for impossibility, 1 for success, 2 for
 * ambiguity or failure to converge.
 */
static int slant_solve(int w, int h, const signed char *clues,
		       signed char *soln, struct solver_scratch *sc)
{
    int W = w+1, H = h+1;
    int x, y, i;
    int done_something;

    /*
     * Clear the output.
     */
    memset(soln, 0, w*h);

    /*
     * Establish a disjoint set forest for tracking connectedness
     * between grid points.
     */
    for (i = 0; i < W*H; i++)
	sc->dsf[i] = i;		       /* initially all distinct */

    /*
     * Repeatedly try to deduce something until we can't.
     */
    do {
	done_something = FALSE;

	/*
	 * Any clue point with the number of remaining lines equal
	 * to zero or to the number of remaining undecided
	 * neighbouring squares can be filled in completely.
	 */
	for (y = 0; y < H; y++)
	    for (x = 0; x < W; x++) {
		int nu, nl, v, c;

		if ((c = clues[y*W+x]) < 0)
		    continue;

		/*
		 * We have a clue point. Count up the number of
		 * undecided neighbours, and also the number of
		 * lines already present.
		 */
		nu = 0;
		nl = c;
		if (x > 0 && y > 0 && (v = soln[(y-1)*w+(x-1)]) != +1)
		    v == 0 ? nu++ : nl--;
		if (x > 0 && y < h && (v = soln[y*w+(x-1)]) != -1)
		    v == 0 ? nu++ : nl--;
		if (x < w && y > 0 && (v = soln[(y-1)*w+x]) != -1)
		    v == 0 ? nu++ : nl--;
		if (x < w && y < h && (v = soln[y*w+x]) != +1)
		    v == 0 ? nu++ : nl--;

		/*
		 * Check the counts.
		 */
		if (nl < 0 || nl > nu) {
		    /*
		     * No consistent value for this at all!
		     */
		    return 0;	       /* impossible */
		}

		if (nu > 0 && (nl == 0 || nl == nu)) {
#ifdef SOLVER_DIAGNOSTICS
		    printf("%s around clue point at %d,%d\n",
			   nl ? "filling" : "emptying", x, y);
#endif
		    if (x > 0 && y > 0 && soln[(y-1)*w+(x-1)] == 0)
			fill_square(w, h, y-1, x-1, (nl ? -1 : +1), soln,
				    sc->dsf);
		    if (x > 0 && y < h && soln[y*w+(x-1)] == 0)
			fill_square(w, h, y, x-1, (nl ? +1 : -1), soln,
				    sc->dsf);
		    if (x < w && y > 0 && soln[(y-1)*w+x] == 0)
			fill_square(w, h, y-1, x, (nl ? +1 : -1), soln,
				    sc->dsf);
		    if (x < w && y < h && soln[y*w+x] == 0)
			fill_square(w, h, y, x, (nl ? -1 : +1), soln,
				    sc->dsf);

		    done_something = TRUE;
		}
	    }

	if (done_something)
	    continue;

	/*
	 * Failing that, we now apply the second condition, which
	 * is that no square may be filled in such a way as to form
	 * a loop.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		int fs, bs;

		if (soln[y*w+x])
		    continue;	       /* got this one already */

		fs = (dsf_canonify(sc->dsf, y*W+x) ==
		      dsf_canonify(sc->dsf, (y+1)*W+(x+1)));
		bs = (dsf_canonify(sc->dsf, (y+1)*W+x) ==
		      dsf_canonify(sc->dsf, y*W+(x+1)));

		if (fs && bs) {
		    /*
		     * Loop avoidance leaves no consistent value
		     * for this at all!
		     */
		    return 0;          /* impossible */
		}

		if (fs) {
		    /*
		     * Top left and bottom right corners of this
		     * square are already connected, which means we
		     * aren't allowed to put a backslash in here.
		     */
#ifdef SOLVER_DIAGNOSTICS
		    printf("placing / in %d,%d by loop avoidance\n", x, y);
#endif
		    fill_square(w, h, y, x, +1, soln, sc->dsf);
		    done_something = TRUE;
		} else if (bs) {
		    /*
		     * Top right and bottom left corners of this
		     * square are already connected, which means we
		     * aren't allowed to put a forward slash in
		     * here.
		     */
#ifdef SOLVER_DIAGNOSTICS
		    printf("placing \\ in %d,%d by loop avoidance\n", x, y);
#endif
		    fill_square(w, h, y, x, -1, soln, sc->dsf);
		    done_something = TRUE;
		}
	    }

    } while (done_something);

    /*
     * Solver can make no more progress. See if the grid is full.
     */
    for (i = 0; i < w*h; i++)
	if (!soln[i])
	    return 2;		       /* failed to converge */
    return 1;			       /* success */
}

/*
 * Filled-grid generator.
 */
static void slant_generate(int w, int h, signed char *soln, random_state *rs)
{
    int W = w+1, H = h+1;
    int x, y, i;
    int *dsf, *indices;

    /*
     * Clear the output.
     */
    memset(soln, 0, w*h);

    /*
     * Establish a disjoint set forest for tracking connectedness
     * between grid points.
     */
    dsf = snewn(W*H, int);
    for (i = 0; i < W*H; i++)
	dsf[i] = i;		       /* initially all distinct */

    /*
     * Prepare a list of the squares in the grid, and fill them in
     * in a random order.
     */
    indices = snewn(w*h, int);
    for (i = 0; i < w*h; i++)
	indices[i] = i;
    shuffle(indices, w*h, sizeof(*indices), rs);

    /*
     * Fill in each one in turn.
     */
    for (i = 0; i < w*h; i++) {
	int fs, bs, v;

	y = indices[i] / w;
	x = indices[i] % w;

	fs = (dsf_canonify(dsf, y*W+x) ==
	      dsf_canonify(dsf, (y+1)*W+(x+1)));
	bs = (dsf_canonify(dsf, (y+1)*W+x) ==
	      dsf_canonify(dsf, y*W+(x+1)));

	/*
	 * It isn't possible to get into a situation where we
	 * aren't allowed to place _either_ type of slash in a
	 * square.
	 * 
	 * Proof (thanks to Gareth Taylor):
	 * 
	 * If it were possible, it would have to be because there
	 * was an existing path (not using this square) between the
	 * top-left and bottom-right corners of this square, and
	 * another between the other two. These two paths would
	 * have to cross at some point.
	 * 
	 * Obviously they can't cross in the middle of a square, so
	 * they must cross by sharing a point in common. But this
	 * isn't possible either: if you chessboard-colour all the
	 * points on the grid, you find that any continuous
	 * diagonal path is entirely composed of points of the same
	 * colour. And one of our two hypothetical paths is between
	 * two black points, and the other is between two white
	 * points - therefore they can have no point in common. []
	 */
	assert(!(fs && bs));

	v = fs ? +1 : bs ? -1 : 2 * random_upto(rs, 2) - 1;
	fill_square(w, h, y, x, v, soln, dsf);
    }

    sfree(indices);
    sfree(dsf);
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int w = params->w, h = params->h, W = w+1, H = h+1;
    signed char *soln, *tmpsoln, *clues;
    int *clueindices;
    struct solver_scratch *sc;
    int x, y, v, i;
    char *desc;

    soln = snewn(w*h, signed char);
    tmpsoln = snewn(w*h, signed char);
    clues = snewn(W*H, signed char);
    clueindices = snewn(W*H, int);
    sc = new_scratch(w, h);

    do {
	/*
	 * Create the filled grid.
	 */
	slant_generate(w, h, soln, rs);

	/*
	 * Fill in the complete set of clues.
	 */
	for (y = 0; y < H; y++)
	    for (x = 0; x < W; x++) {
		v = 0;

		if (x > 0 && y > 0 && soln[(y-1)*w+(x-1)] == -1) v++;
		if (x > 0 && y < h && soln[y*w+(x-1)] == +1) v++;
		if (x < w && y > 0 && soln[(y-1)*w+x] == +1) v++;
		if (x < w && y < h && soln[y*w+x] == -1) v++;

		clues[y*W+x] = v;
	    }
    } while (slant_solve(w, h, clues, tmpsoln, sc) != 1);

    /*
     * Remove as many clues as possible while retaining solubility.
     */
    for (i = 0; i < W*H; i++)
	clueindices[i] = i;
    shuffle(clueindices, W*H, sizeof(*clueindices), rs);
    for (i = 0; i < W*H; i++) {
	y = clueindices[i] / W;
	x = clueindices[i] % W;
	v = clues[y*W+x];
	clues[y*W+x] = -1;
	if (slant_solve(w, h, clues, tmpsoln, sc) != 1)
	    clues[y*W+x] = v;	       /* put it back */
    }

    /*
     * Now we have the clue set as it will be presented to the
     * user. Encode it in a game desc.
     */
    {
	char *p;
	int run, i;

	desc = snewn(W*H+1, char);
	p = desc;
	run = 0;
	for (i = 0; i <= W*H; i++) {
	    int n = (i < W*H ? clues[i] : -2);

	    if (n == -1)
		run++;
	    else {
		if (run) {
		    while (run > 0) {
			int c = 'a' - 1 + run;
			if (run > 26)
			    c = 'z';
			*p++ = c;
			run -= c - ('a' - 1);
		    }
		}
		if (n >= 0)
		    *p++ = '0' + n;
		run = 0;
	    }
	}
	assert(p - desc <= W*H);
	*p++ = '\0';
	desc = sresize(desc, p - desc, char);
    }

    /*
     * Encode the solution as an aux_info.
     */
    {
	char *auxbuf;
	*aux = auxbuf = snewn(w*h+1, char);
	for (i = 0; i < w*h; i++)
	    auxbuf[i] = soln[i] < 0 ? '\\' : '/';
	auxbuf[w*h] = '\0';
    }

    free_scratch(sc);
    sfree(clueindices);
    sfree(clues);
    sfree(tmpsoln);
    sfree(soln);

    return desc;
}

static char *validate_desc(game_params *params, char *desc)
{
    int w = params->w, h = params->h, W = w+1, H = h+1;
    int area = W*H;
    int squares = 0;

    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n >= '0' && n <= '4') {
            squares++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";

    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    int w = params->w, h = params->h, W = w+1, H = h+1;
    game_state *state = snew(game_state);
    int area = W*H;
    int squares = 0;

    state->p = *params;
    state->soln = snewn(w*h, signed char);
    memset(state->soln, 0, w*h);
    state->completed = state->used_solve = FALSE;

    state->clues = snew(game_clues);
    state->clues->w = w;
    state->clues->h = h;
    state->clues->clues = snewn(W*H, signed char);
    state->clues->refcount = 1;
    state->clues->dsf = snewn(W*H, int);
    memset(state->clues->clues, -1, W*H);
    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n >= '0' && n <= '4') {
            state->clues->clues[squares++] = n - '0';
        } else
	    assert(!"can't get here");
    }
    assert(squares == area);

    return state;
}

static game_state *dup_game(game_state *state)
{
    int w = state->p.w, h = state->p.h;
    game_state *ret = snew(game_state);

    ret->p = state->p;
    ret->clues = state->clues;
    ret->clues->refcount++;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;

    ret->soln = snewn(w*h, signed char);
    memcpy(ret->soln, state->soln, w*h);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state);
}

static int check_completion(game_state *state)
{
    int w = state->p.w, h = state->p.h, W = w+1, H = h+1;
    int i, x, y;

    /*
     * Establish a disjoint set forest for tracking connectedness
     * between grid points. Use the dsf scratch space in the shared
     * clues structure, to avoid mallocing too often.
     */
    for (i = 0; i < W*H; i++)
	state->clues->dsf[i] = i;      /* initially all distinct */

    /*
     * Now go through the grid checking connectedness. While we're
     * here, also check that everything is filled in.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    int i1, i2;

	    if (state->soln[y*w+x] == 0)
		return FALSE;
	    if (state->soln[y*w+x] < 0) {
		i1 = y*W+x;
		i2 = (y+1)*W+(x+1);
	    } else {
		i1 = (y+1)*W+x;
		i2 = y*W+(x+1);
	    }

	    /*
	     * Our edge connects i1 with i2. If they're already
	     * connected, return failure. Otherwise, link them.
	     */
	    if (dsf_canonify(state->clues->dsf, i1) ==
		dsf_canonify(state->clues->dsf, i2))
		return FALSE;
	    else
		dsf_merge(state->clues->dsf, i1, i2);
	}

    /*
     * The grid is _a_ valid grid; let's see if it matches the
     * clues.
     */
    for (y = 0; y < H; y++)
	for (x = 0; x < W; x++) {
	    int v, c;

	    if ((c = state->clues->clues[y*W+x]) < 0)
		continue;

	    v = 0;

	    if (x > 0 && y > 0 && state->soln[(y-1)*w+(x-1)] == -1) v++;
	    if (x > 0 && y < h && state->soln[y*w+(x-1)] == +1) v++;
	    if (x < w && y > 0 && state->soln[(y-1)*w+x] == +1) v++;
	    if (x < w && y < h && state->soln[y*w+x] == -1) v++;

	    if (c != v)
		return FALSE;
	}

    return TRUE;
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    int w = state->p.w, h = state->p.h;
    signed char *soln;
    int bs, ret;
    int free_soln = FALSE;
    char *move, buf[80];
    int movelen, movesize;
    int x, y;

    if (aux) {
	/*
	 * If we already have the solution, save ourselves some
	 * time.
	 */
	soln = (signed char *)aux;
	bs = (signed char)'\\';
	free_soln = FALSE;
    } else {
	struct solver_scratch *sc = new_scratch(w, h);
	soln = snewn(w*h, signed char);
	bs = -1;
	ret = slant_solve(w, h, state->clues->clues, soln, sc);
	free_scratch(sc);
	if (ret != 1) {
	    sfree(soln);
	    if (ret == 0)
		return "This puzzle is not self-consistent";
	    else
		return "Unable to find a unique solution for this puzzle";
	}
	free_soln = TRUE;
    }

    /*
     * Construct a move string which turns the current state into
     * the solved state.
     */
    movesize = 256;
    move = snewn(movesize, char);
    movelen = 0;
    move[movelen++] = 'S';
    move[movelen] = '\0';
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    int v = (soln[y*w+x] == bs ? -1 : +1);
	    if (state->soln[y*w+x] != v) {
		int len = sprintf(buf, ";%c%d,%d", v < 0 ? '\\' : '/', x, y);
		if (movelen + len >= movesize) {
		    movesize = movelen + len + 256;
		    move = sresize(move, movesize, char);
		}
		strcpy(move + movelen, buf);
		movelen += len;
	    }
	}

    if (free_soln)
	sfree(soln);

    return move;
}

static char *game_text_format(game_state *state)
{
    int w = state->p.w, h = state->p.h, W = w+1, H = h+1;
    int x, y, len;
    char *ret, *p;

    /*
     * There are h+H rows of w+W columns.
     */
    len = (h+H) * (w+W+1) + 1;
    ret = snewn(len, char);
    p = ret;

    for (y = 0; y < H; y++) {
	for (x = 0; x < W; x++) {
	    if (state->clues->clues[y*W+x] >= 0)
		*p++ = state->clues->clues[y*W+x] + '0';
	    else
		*p++ = '+';
	    if (x < w)
		*p++ = '-';
	}
	*p++ = '\n';
	if (y < h) {
	    for (x = 0; x < W; x++) {
		*p++ = '|';
		if (x < w) {
		    if (state->soln[y*w+x] != 0)
			*p++ = (state->soln[y*w+x] < 0 ? '\\' : '/');
		    else
			*p++ = ' ';
		}
	    }
	    *p++ = '\n';
	}
    }
    *p++ = '\0';

    assert(p - ret == len);
    return ret;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER TILESIZE
#define CLUE_RADIUS (TILESIZE / 3)
#define CLUE_TEXTSIZE (TILESIZE / 2)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

#define FLASH_TIME 0.30F

/*
 * Bit fields in the `grid' and `todraw' elements of the drawstate.
 */
#define BACKSLASH 0x0001
#define FORWSLASH 0x0002
#define L_T       0x0004
#define L_B       0x0008
#define T_L       0x0010
#define T_R       0x0020
#define R_T       0x0040
#define R_B       0x0080
#define B_L       0x0100
#define B_R       0x0200
#define C_TL      0x0400
#define C_TR      0x0800
#define C_BL      0x1000
#define C_BR      0x2000
#define FLASH     0x4000

struct game_drawstate {
    int tilesize;
    int started;
    int *grid;
    int *todraw;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int w = state->p.w, h = state->p.h;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        int v;
        char buf[80];

        x = FROMCOORD(x);
        y = FROMCOORD(y);
        if (x < 0 || y < 0 || x >= w || y >= h)
            return NULL;

        if (button == LEFT_BUTTON) {
            /*
             * Left-clicking cycles blank -> \ -> / -> blank.
             */
            v = state->soln[y*w+x] - 1;
            if (v == -2)
                v = +1;
        } else {
            /*
             * Right-clicking cycles blank -> / -> \ -> blank.
             */
            v = state->soln[y*w+x] + 1;
            if (v == +2)
                v = -1;
        }

        sprintf(buf, "%c%d,%d", v==-1 ? '\\' : v==+1 ? '/' : 'C', x, y);
        return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(game_state *state, char *move)
{
    int w = state->p.w, h = state->p.h;
    char c;
    int x, y, n;
    game_state *ret = dup_game(state);

    while (*move) {
        c = *move;
	if (c == 'S') {
	    ret->used_solve = TRUE;
	    move++;
	} else if (c == '\\' || c == '/' || c == 'C') {
            move++;
            if (sscanf(move, "%d,%d%n", &x, &y, &n) != 2 ||
                x < 0 || y < 0 || x >= w || y >= h) {
                free_game(ret);
                return NULL;
            }
            ret->soln[y*w+x] = (c == '\\' ? -1 : c == '/' ? +1 : 0);
            move += n;
        } else {
            free_game(ret);
            return NULL;
        }
        if (*move == ';')
            move++;
        else if (*move) {
            free_game(ret);
            return NULL;
        }
    }

    if (!ret->completed)
	ret->completed = check_completion(ret);

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* fool the macros */
    struct dummy { int tilesize; } dummy = { tilesize }, *ds = &dummy;

    *x = 2 * BORDER + params->w * TILESIZE + 1;
    *y = 2 * BORDER + params->h * TILESIZE + 1;
}

static void game_set_size(game_drawstate *ds, game_params *params,
			  int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.7F;
    ret[COL_GRID * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.7F;
    ret[COL_GRID * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 0.7F;

    ret[COL_INK * 3 + 0] = 0.0F;
    ret[COL_INK * 3 + 1] = 0.0F;
    ret[COL_INK * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    int w = state->p.w, h = state->p.h;
    int i;
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->started = FALSE;
    ds->grid = snewn(w*h, int);
    ds->todraw = snewn(w*h, int);
    for (i = 0; i < w*h; i++)
	ds->grid[i] = ds->todraw[i] = -1;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_clue(frontend *fe, game_drawstate *ds,
		      int x, int y, int v)
{
    char p[2];

    if (v < 0)
	return;

    p[0] = v + '0';
    p[1] = '\0';
    draw_circle(fe, COORD(x), COORD(y), CLUE_RADIUS,
		COL_BACKGROUND, COL_INK);
    draw_text(fe, COORD(x), COORD(y), FONT_VARIABLE,
	      CLUE_TEXTSIZE, ALIGN_VCENTRE|ALIGN_HCENTRE,
	      COL_INK, p);
}

static void draw_tile(frontend *fe, game_drawstate *ds, game_clues *clues,
		      int x, int y, int v)
{
    int w = clues->w /*, h = clues->h*/, W = w+1 /*, H = h+1 */;
    int xx, yy;

    clip(fe, COORD(x), COORD(y), TILESIZE+1, TILESIZE+1);

    draw_rect(fe, COORD(x), COORD(y), TILESIZE, TILESIZE,
	      (v & FLASH) ? COL_GRID : COL_BACKGROUND);

    /*
     * Draw the grid lines.
     */
    draw_line(fe, COORD(x), COORD(y), COORD(x+1), COORD(y), COL_GRID);
    draw_line(fe, COORD(x), COORD(y+1), COORD(x+1), COORD(y+1), COL_GRID);
    draw_line(fe, COORD(x), COORD(y), COORD(x), COORD(y+1), COL_GRID);
    draw_line(fe, COORD(x+1), COORD(y), COORD(x+1), COORD(y+1), COL_GRID);

    /*
     * Draw the slash.
     */
    if (v & BACKSLASH) {
	draw_line(fe, COORD(x), COORD(y), COORD(x+1), COORD(y+1), COL_INK);
	draw_line(fe, COORD(x)+1, COORD(y), COORD(x+1), COORD(y+1)-1,
		  COL_INK);
	draw_line(fe, COORD(x), COORD(y)+1, COORD(x+1)-1, COORD(y+1),
		  COL_INK);
    } else if (v & FORWSLASH) {
	draw_line(fe, COORD(x+1), COORD(y), COORD(x), COORD(y+1), COL_INK);
	draw_line(fe, COORD(x+1)-1, COORD(y), COORD(x), COORD(y+1)-1,
		  COL_INK);
	draw_line(fe, COORD(x+1), COORD(y)+1, COORD(x)+1, COORD(y+1),
		  COL_INK);
    }

    /*
     * Draw dots on the grid corners that appear if a slash is in a
     * neighbouring cell.
     */
    if (v & L_T)
	draw_rect(fe, COORD(x), COORD(y)+1, 1, 1, COL_INK);
    if (v & L_B)
	draw_rect(fe, COORD(x), COORD(y+1)-1, 1, 1, COL_INK);
    if (v & R_T)
	draw_rect(fe, COORD(x+1), COORD(y)+1, 1, 1, COL_INK);
    if (v & R_B)
	draw_rect(fe, COORD(x+1), COORD(y+1)-1, 1, 1, COL_INK);
    if (v & T_L)
	draw_rect(fe, COORD(x)+1, COORD(y), 1, 1, COL_INK);
    if (v & T_R)
	draw_rect(fe, COORD(x+1)-1, COORD(y), 1, 1, COL_INK);
    if (v & B_L)
	draw_rect(fe, COORD(x)+1, COORD(y+1), 1, 1, COL_INK);
    if (v & B_R)
	draw_rect(fe, COORD(x+1)-1, COORD(y+1), 1, 1, COL_INK);
    if (v & C_TL)
	draw_rect(fe, COORD(x), COORD(y), 1, 1, COL_INK);
    if (v & C_TR)
	draw_rect(fe, COORD(x+1), COORD(y), 1, 1, COL_INK);
    if (v & C_BL)
	draw_rect(fe, COORD(x), COORD(y+1), 1, 1, COL_INK);
    if (v & C_BR)
	draw_rect(fe, COORD(x+1), COORD(y+1), 1, 1, COL_INK);

    /*
     * And finally the clues at the corners.
     */
    for (xx = x; xx <= x+1; xx++)
	for (yy = y; yy <= y+1; yy++)
	    draw_clue(fe, ds, xx, yy, clues->clues[yy*W+xx]);

    unclip(fe);
    draw_update(fe, COORD(x), COORD(y), TILESIZE+1, TILESIZE+1);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int w = state->p.w, h = state->p.h, W = w+1, H = h+1;
    int x, y;
    int flashing;

    if (flashtime > 0)
	flashing = (int)(flashtime * 3 / FLASH_TIME) != 1;
    else
	flashing = FALSE;

    if (!ds->started) {
	int ww, wh;
	game_compute_size(&state->p, TILESIZE, &ww, &wh);
	draw_rect(fe, 0, 0, ww, wh, COL_BACKGROUND);
	draw_update(fe, 0, 0, ww, wh);

	/*
	 * Draw any clues on the very edges (since normal tile
	 * redraw won't draw the bits outside the grid boundary).
	 */
	for (y = 0; y < H; y++) {
	    draw_clue(fe, ds, 0, y, state->clues->clues[y*W+0]);
	    draw_clue(fe, ds, w, y, state->clues->clues[y*W+w]);
	}
	for (x = 0; x < W; x++) {
	    draw_clue(fe, ds, x, 0, state->clues->clues[0*W+x]);
	    draw_clue(fe, ds, x, h, state->clues->clues[h*W+x]);
	}

	ds->started = TRUE;
    }

    /*
     * Loop over the grid and work out where all the slashes are.
     * We need to do this because a slash in one square affects the
     * drawing of the next one along.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    ds->todraw[y*w+x] = flashing ? FLASH : 0;

    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    if (state->soln[y*w+x] < 0) {
		ds->todraw[y*w+x] |= BACKSLASH;
		if (x > 0)
		    ds->todraw[y*w+(x-1)] |= R_T | C_TR;
		if (x+1 < w)
		    ds->todraw[y*w+(x+1)] |= L_B | C_BL;
		if (y > 0)
		    ds->todraw[(y-1)*w+x] |= B_L | C_BL;
		if (y+1 < h)
		    ds->todraw[(y+1)*w+x] |= T_R | C_TR;
		if (x > 0 && y > 0)
		    ds->todraw[(y-1)*w+(x-1)] |= C_BR;
		if (x+1 < w && y+1 < h)
		    ds->todraw[(y+1)*w+(x+1)] |= C_TL;
	    } else if (state->soln[y*w+x] > 0) {
		ds->todraw[y*w+x] |= FORWSLASH;
		if (x > 0)
		    ds->todraw[y*w+(x-1)] |= R_B | C_BR;
		if (x+1 < w)
		    ds->todraw[y*w+(x+1)] |= L_T | C_TL;
		if (y > 0)
		    ds->todraw[(y-1)*w+x] |= B_R | C_BR;
		if (y+1 < h)
		    ds->todraw[(y+1)*w+x] |= T_L | C_TL;
		if (x > 0 && y+1 < h)
		    ds->todraw[(y+1)*w+(x-1)] |= C_TR;
		if (x+1 < w && y > 0)
		    ds->todraw[(y-1)*w+(x+1)] |= C_BL;
	    }
	}
    }

    /*
     * Now go through and draw the grid squares.
     */
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    if (ds->todraw[y*w+x] != ds->grid[y*w+x]) {
		draw_tile(fe, ds, state->clues, x, y, ds->todraw[y*w+x]);
		ds->grid[y*w+x] = ds->todraw[y*w+x];
	    }
	}
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
    if (!oldstate->completed && newstate->completed &&
	!oldstate->used_solve && !newstate->used_solve)
        return FLASH_TIME;

    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame slant
#endif

const struct game thegame = {
    "Slant", "games.slant",
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
    TRUE, game_text_format,
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
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};
