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
    COL_SLANT1,
    COL_SLANT2,
    COL_ERROR,
    NCOLOURS
};

/*
 * In standalone solver mode, `verbose' is a variable which can be
 * set by command-line option; in debugging mode it's simply always
 * true.
 */
#if defined STANDALONE_SOLVER
#define SOLVER_DIAGNOSTICS
int verbose = FALSE;
#elif defined SOLVER_DIAGNOSTICS
#define verbose TRUE
#endif

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(HARD,Hard,h)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const slant_diffnames[] = { DIFFLIST(TITLE) };
static char const slant_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    int w, h, diff;
};

typedef struct game_clues {
    int w, h;
    signed char *clues;
    int *tmpdsf;
    int refcount;
} game_clues;

#define ERR_VERTEX 1
#define ERR_SQUARE 2
#define ERR_SQUARE_TMP 4

struct game_state {
    struct game_params p;
    game_clues *clues;
    signed char *soln;
    unsigned char *errors;
    int completed;
    int used_solve;		       /* used to suppress completion flash */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 8;
    ret->diff = DIFF_EASY;

    return ret;
}

static const struct game_params slant_presets[] = {
    {5, 5, DIFF_EASY},
    {5, 5, DIFF_HARD},
    {8, 8, DIFF_EASY},
    {8, 8, DIFF_HARD},
    {12, 10, DIFF_EASY},
    {12, 10, DIFF_HARD},
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(slant_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = slant_presets[i];

    sprintf(str, "%dx%d %s", ret->w, ret->h, slant_diffnames[ret->diff]);

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
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
	int i;
	string++;
	for (i = 0; i < DIFFCOUNT; i++)
	    if (*string == slant_diffchars[i])
		ret->diff = i;
	if (*string) string++;
    }
}

static char *encode_params(game_params *params, int full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);
    if (full)
	sprintf(data + strlen(data), "d%c", slant_diffchars[params->diff]);

    return dupstr(data);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

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

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].sval = DIFFCONFIG;
    ret[2].ival = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->diff = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    /*
     * (At least at the time of writing this comment) The grid
     * generator is actually capable of handling even zero grid
     * dimensions without crashing. Puzzles with a zero-area grid
     * are a bit boring, though, because they're already solved :-)
     * And puzzles with a dimension of 1 can't be made Hard, which
     * means the simplest thing is to forbid them altogether.
     */

    if (params->w < 2 || params->h < 2)
	return "Width and height must both be at least two";

    return NULL;
}

/*
 * Scratch space for solver.
 */
struct solver_scratch {
    /*
     * Disjoint set forest which tracks the connected sets of
     * points.
     */
    int *connected;

    /*
     * Counts the number of possible exits from each connected set
     * of points. (That is, the number of possible _simultaneous_
     * exits: an unconnected point labelled 2 has an exit count of
     * 2 even if all four possible edges are still under
     * consideration.)
     */
    int *exits;

    /*
     * Tracks whether each connected set of points includes a
     * border point.
     */
    unsigned char *border;

    /*
     * Another disjoint set forest. This one tracks _squares_ which
     * are known to slant in the same direction.
     */
    int *equiv;

    /*
     * Stores slash values which we know for an equivalence class.
     * When we fill in a square, we set slashval[canonify(x)] to
     * the same value as soln[x], so that we can then spot other
     * squares equivalent to it and fill them in immediately via
     * their known equivalence.
     */
    signed char *slashval;

    /*
     * Useful to have this information automatically passed to
     * solver subroutines. (This pointer is not dynamically
     * allocated by new_scratch and free_scratch.)
     */
    const signed char *clues;
};

static struct solver_scratch *new_scratch(int w, int h)
{
    int W = w+1, H = h+1;
    struct solver_scratch *ret = snew(struct solver_scratch);
    ret->connected = snewn(W*H, int);
    ret->exits = snewn(W*H, int);
    ret->border = snewn(W*H, unsigned char);
    ret->equiv = snewn(w*h, int);
    ret->slashval = snewn(w*h, signed char);
    return ret;
}

static void free_scratch(struct solver_scratch *sc)
{
    sfree(sc->slashval);
    sfree(sc->equiv);
    sfree(sc->border);
    sfree(sc->exits);
    sfree(sc->connected);
    sfree(sc);
}

/*
 * Wrapper on dsf_merge() which updates the `exits' and `border'
 * arrays.
 */
static void merge_vertices(int *connected,
			   struct solver_scratch *sc, int i, int j)
{
    int exits = -1, border = FALSE;    /* initialise to placate optimiser */

    if (sc) {
	i = dsf_canonify(connected, i);
	j = dsf_canonify(connected, j);

	/*
	 * We have used one possible exit from each of the two
	 * classes. Thus, the viable exit count of the new class is
	 * the sum of the old exit counts minus two.
	 */
	exits = sc->exits[i] + sc->exits[j] - 2;

	border = sc->border[i] || sc->border[j];
    }

    dsf_merge(connected, i, j);

    if (sc) {
	i = dsf_canonify(connected, i);
	sc->exits[i] = exits;
	sc->border[i] = border;
    }
}

/*
 * Called when we have just blocked one way out of a particular
 * point. If that point is a non-clue point (thus has a variable
 * number of exits), we have therefore decreased its potential exit
 * count, so we must decrement the exit count for the group as a
 * whole.
 */
static void decr_exits(struct solver_scratch *sc, int i)
{
    if (sc->clues[i] < 0) {
	i = dsf_canonify(sc->connected, i);
	sc->exits[i]--;
    }
}

static void fill_square(int w, int h, int x, int y, int v,
			signed char *soln,
			int *connected, struct solver_scratch *sc)
{
    int W = w+1 /*, H = h+1 */;

    assert(x >= 0 && x < w && y >= 0 && y < h);

    if (soln[y*w+x] != 0) {
	return;			       /* do nothing */
    }

#ifdef SOLVER_DIAGNOSTICS
    if (verbose)
	printf("  placing %c in %d,%d\n", v == -1 ? '\\' : '/', x, y);
#endif

    soln[y*w+x] = v;

    if (sc) {
	int c = dsf_canonify(sc->equiv, y*w+x);
	sc->slashval[c] = v;
    }

    if (v < 0) {
	merge_vertices(connected, sc, y*W+x, (y+1)*W+(x+1));
	if (sc) {
	    decr_exits(sc, y*W+(x+1));
	    decr_exits(sc, (y+1)*W+x);
	}
    } else {
	merge_vertices(connected, sc, y*W+(x+1), (y+1)*W+x);
	if (sc) {
	    decr_exits(sc, y*W+x);
	    decr_exits(sc, (y+1)*W+(x+1));
	}
    }
}

/*
 * Solver. Returns 0 for impossibility, 1 for success, 2 for
 * ambiguity or failure to converge.
 */
static int slant_solve(int w, int h, const signed char *clues,
		       signed char *soln, struct solver_scratch *sc,
		       int difficulty)
{
    int W = w+1, H = h+1;
    int x, y, i, j;
    int done_something;

    /*
     * Clear the output.
     */
    memset(soln, 0, w*h);

    sc->clues = clues;

    /*
     * Establish a disjoint set forest for tracking connectedness
     * between grid points.
     */
    for (i = 0; i < W*H; i++)
	sc->connected[i] = i;	       /* initially all distinct */

    /*
     * Establish a disjoint set forest for tracking which squares
     * are known to slant in the same direction.
     */
    for (i = 0; i < w*h; i++)
	sc->equiv[i] = i;	       /* initially all distinct */

    /*
     * Clear the slashval array.
     */
    memset(sc->slashval, 0, w*h);

    /*
     * Initialise the `exits' and `border' arrays. Theses is used
     * to do second-order loop avoidance: the dual of the no loops
     * constraint is that every point must be somehow connected to
     * the border of the grid (otherwise there would be a solid
     * loop around it which prevented this).
     * 
     * I define a `dead end' to be a connected group of points
     * which contains no border point, and which can form at most
     * one new connection outside itself. Then I forbid placing an
     * edge so that it connects together two dead-end groups, since
     * this would yield a non-border-connected isolated subgraph
     * with no further scope to extend it.
     */
    for (y = 0; y < H; y++)
	for (x = 0; x < W; x++) {
	    if (y == 0 || y == H-1 || x == 0 || x == W-1)
		sc->border[y*W+x] = TRUE;
	    else
		sc->border[y*W+x] = FALSE;

	    if (clues[y*W+x] < 0)
		sc->exits[y*W+x] = 4;
	    else
		sc->exits[y*W+x] = clues[y*W+x];
	}

    /*
     * Make a one-off preliminary pass over the grid looking for
     * starting-point arrangements. The ones we need to spot are:
     * 
     * 	- two adjacent 1s in the centre of the grid imply that each
     * 	  one's single line points towards the other. (If either 1
     * 	  were connected on the far side, the two squares shared
     * 	  between the 1s would both link to the other 1 as a
     * 	  consequence of neither linking to the first.) Thus, we
     * 	  can fill in the four squares around them.
     * 
     * 	- dually, two adjacent 3s imply that each one's _non_-line
     * 	  points towards the other.
     * 
     * 	- if the pair of 1s and 3s is not _adjacent_ but is
     * 	  separated by one or more 2s, the reasoning still applies.
     * 
     * This is more advanced than just spotting obvious starting
     * squares such as central 4s and edge 2s, so we disable it on
     * DIFF_EASY.
     * 
     * (I don't like this loop; it feels grubby to me. My
     * mathematical intuition feels there ought to be some more
     * general deductive form which contains this loop as a special
     * case, but I can't bring it to mind right now.)
     */
    if (difficulty > DIFF_EASY) {
	for (y = 1; y+1 < H; y++)
	    for (x = 1; x+1 < W; x++) {
		int v = clues[y*W+x], s, x2, y2, dx, dy;
		if (v != 1 && v != 3)
		    continue;
		/* Slash value of the square up and left of (x,y). */
		s = (v == 1 ? +1 : -1);

		/* Look in each direction once. */
		for (dy = 0; dy < 2; dy++) {
		    dx = 1 - dy;
		    x2 = x+dx;
		    y2 = y+dy;
		    if (x2+1 >= W || y2+1 >= H)
			continue;	       /* too close to the border */
		    while (x2+dx+1 < W && y2+dy+1 < H && clues[y2*W+x2] == 2)
			x2 += dx, y2 += dy;
		    if (clues[y2*W+x2] == v) {
#ifdef SOLVER_DIAGNOSTICS
			if (verbose)
			    printf("found adjacent %ds at %d,%d and %d,%d\n",
				   v, x, y, x2, y2);
#endif
			fill_square(w, h, x-1, y-1, s, soln,
				    sc->connected, sc);
			fill_square(w, h, x-1+dy, y-1+dx, -s, soln,
				    sc->connected, sc);
			fill_square(w, h, x2, y2, s, soln,
				    sc->connected, sc);
			fill_square(w, h, x2-dy, y2-dx, -s, soln,
				    sc->connected, sc);
		    }
		}
	    }
    }

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
		struct {
		    int pos, slash;
		} neighbours[4];
		int nneighbours;
		int nu, nl, c, s, eq, eq2, last, meq, mj1, mj2;

		if ((c = clues[y*W+x]) < 0)
		    continue;

		/*
		 * We have a clue point. Start by listing its
		 * neighbouring squares, in order around the point,
		 * together with the type of slash that would be
		 * required in that square to connect to the point.
		 */
		nneighbours = 0;
		if (x > 0 && y > 0) {
		    neighbours[nneighbours].pos = (y-1)*w+(x-1);
		    neighbours[nneighbours].slash = -1;
		    nneighbours++;
		}
		if (x > 0 && y < h) {
		    neighbours[nneighbours].pos = y*w+(x-1);
		    neighbours[nneighbours].slash = +1;
		    nneighbours++;
		}
		if (x < w && y < h) {
		    neighbours[nneighbours].pos = y*w+x;
		    neighbours[nneighbours].slash = -1;
		    nneighbours++;
		}
		if (x < w && y > 0) {
		    neighbours[nneighbours].pos = (y-1)*w+x;
		    neighbours[nneighbours].slash = +1;
		    nneighbours++;
		}

		/*
		 * Count up the number of undecided neighbours, and
		 * also the number of lines already present.
		 *
		 * If we're not on DIFF_EASY, then in this loop we
		 * also track whether we've seen two adjacent empty
		 * squares belonging to the same equivalence class
		 * (meaning they have the same type of slash). If
		 * so, we count them jointly as one line.
		 */
		nu = 0;
		nl = c;
		last = neighbours[nneighbours-1].pos;
		if (soln[last] == 0)
		    eq = dsf_canonify(sc->equiv, last);
		else
		    eq = -1;
		meq = mj1 = mj2 = -1;
		for (i = 0; i < nneighbours; i++) {
		    j = neighbours[i].pos;
		    s = neighbours[i].slash;
		    if (soln[j] == 0) {
			nu++;	       /* undecided */
			if (meq < 0 && difficulty > DIFF_EASY) {
			    eq2 = dsf_canonify(sc->equiv, j);
			    if (eq == eq2 && last != j) {
				/*
				 * We've found an equivalent pair.
				 * Mark it. This also inhibits any
				 * further equivalence tracking
				 * around this square, since we can
				 * only handle one pair (and in
				 * particular we want to avoid
				 * being misled by two overlapping
				 * equivalence pairs).
				 */
				meq = eq;
				mj1 = last;
				mj2 = j;
				nl--;   /* count one line */
				nu -= 2;   /* and lose two undecideds */
			    } else
				eq = eq2;
			}
		    } else {
			eq = -1;
			if (soln[j] == s)
			    nl--;      /* here's a line */
		    }
		    last = j;
		}

		/*
		 * Check the counts.
		 */
		if (nl < 0 || nl > nu) {
		    /*
		     * No consistent value for this at all!
		     */
#ifdef SOLVER_DIAGNOSTICS
		    if (verbose)
			printf("need %d / %d lines around clue point at %d,%d!\n",
			       nl, nu, x, y);
#endif
		    return 0;	       /* impossible */
		}

		if (nu > 0 && (nl == 0 || nl == nu)) {
#ifdef SOLVER_DIAGNOSTICS
		    if (verbose) {
			if (meq >= 0)
			    printf("partially (since %d,%d == %d,%d) ",
				   mj1%w, mj1/w, mj2%w, mj2/w);
			printf("%s around clue point at %d,%d\n",
			       nl ? "filling" : "emptying", x, y);
		    }
#endif
		    for (i = 0; i < nneighbours; i++) {
			j = neighbours[i].pos;
			s = neighbours[i].slash;
			if (soln[j] == 0 && j != mj1 && j != mj2)
			    fill_square(w, h, j%w, j/w, (nl ? s : -s), soln,
					sc->connected, sc);
		    }

		    done_something = TRUE;
		} else if (nu == 2 && nl == 1 && difficulty > DIFF_EASY) {
		    /*
		     * If we have precisely two undecided squares
		     * and precisely one line to place between
		     * them, _and_ those squares are adjacent, then
		     * we can mark them as equivalent to one
		     * another.
		     * 
		     * This even applies if meq >= 0: if we have a
		     * 2 clue point and two of its neighbours are
		     * already marked equivalent, we can indeed
		     * mark the other two as equivalent.
		     * 
		     * We don't bother with this on DIFF_EASY,
		     * since we wouldn't have used the results
		     * anyway.
		     */
		    last = -1;
		    for (i = 0; i < nneighbours; i++) {
			j = neighbours[i].pos;
			if (soln[j] == 0 && j != mj1 && j != mj2) {
			    if (last < 0)
				last = i;
			    else if (last == i-1 || (last == 0 && i == 3))
				break; /* found a pair */
			}
		    }
		    if (i < nneighbours) {
			int sv1, sv2;

			assert(last >= 0);
			/*
			 * neighbours[last] and neighbours[i] are
			 * the pair. Mark them equivalent.
			 */
#ifdef SOLVER_DIAGNOSTICS
			if (verbose) {
			    if (meq >= 0)
				printf("since %d,%d == %d,%d, ",
				       mj1%w, mj1/w, mj2%w, mj2/w);
			}
#endif
			mj1 = neighbours[last].pos;
			mj2 = neighbours[i].pos;
#ifdef SOLVER_DIAGNOSTICS
			if (verbose)
			    printf("clue point at %d,%d implies %d,%d == %d,"
				   "%d\n", x, y, mj1%w, mj1/w, mj2%w, mj2/w);
#endif
			mj1 = dsf_canonify(sc->equiv, mj1);
			sv1 = sc->slashval[mj1];
			mj2 = dsf_canonify(sc->equiv, mj2);
			sv2 = sc->slashval[mj2];
			if (sv1 != 0 && sv2 != 0 && sv1 != sv2) {
#ifdef SOLVER_DIAGNOSTICS
			    if (verbose)
				printf("merged two equivalence classes with"
				       " different slash values!\n");
#endif
			    return 0;
			}
			sv1 = sv1 ? sv1 : sv2;
			dsf_merge(sc->equiv, mj1, mj2);
			mj1 = dsf_canonify(sc->equiv, mj1);
			sc->slashval[mj1] = sv1;
		    }
		}
	    }

	if (done_something)
	    continue;

	/*
	 * Failing that, we now apply the second condition, which
	 * is that no square may be filled in such a way as to form
	 * a loop. Also in this loop (since it's over squares
	 * rather than points), we check slashval to see if we've
	 * already filled in another square in the same equivalence
	 * class.
	 * 
	 * The slashval check is disabled on DIFF_EASY, as is dead
	 * end avoidance. Only _immediate_ loop avoidance remains.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		int fs, bs, v;
		int c1, c2;
#ifdef SOLVER_DIAGNOSTICS
		char *reason = "<internal error>";
#endif

		if (soln[y*w+x])
		    continue;	       /* got this one already */

		fs = FALSE;
		bs = FALSE;

		if (difficulty > DIFF_EASY)
		    v = sc->slashval[dsf_canonify(sc->equiv, y*w+x)];
		else
		    v = 0;

		/*
		 * Try to rule out connectivity between (x,y) and
		 * (x+1,y+1); if successful, we will deduce that we
		 * must have a forward slash.
		 */
		c1 = dsf_canonify(sc->connected, y*W+x);
		c2 = dsf_canonify(sc->connected, (y+1)*W+(x+1));
		if (c1 == c2) {
		    fs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "simple loop avoidance";
#endif
		}
		if (difficulty > DIFF_EASY &&
		    !sc->border[c1] && !sc->border[c2] &&
		    sc->exits[c1] <= 1 && sc->exits[c2] <= 1) {
		    fs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "dead end avoidance";
#endif
		}
		if (v == +1) {
		    fs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "equivalence to an already filled square";
#endif
		}

		/*
		 * Now do the same between (x+1,y) and (x,y+1), to
		 * see if we are required to have a backslash.
		 */
		c1 = dsf_canonify(sc->connected, y*W+(x+1));
		c2 = dsf_canonify(sc->connected, (y+1)*W+x);
		if (c1 == c2) {
		    bs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "simple loop avoidance";
#endif
		}
		if (difficulty > DIFF_EASY &&
		    !sc->border[c1] && !sc->border[c2] &&
		    sc->exits[c1] <= 1 && sc->exits[c2] <= 1) {
		    bs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "dead end avoidance";
#endif
		}
		if (v == -1) {
		    bs = TRUE;
#ifdef SOLVER_DIAGNOSTICS
		    reason = "equivalence to an already filled square";
#endif
		}

		if (fs && bs) {
		    /*
		     * No consistent value for this at all!
		     */
#ifdef SOLVER_DIAGNOSTICS
		    if (verbose)
			printf("%d,%d has no consistent slash!\n", x, y);
#endif
		    return 0;          /* impossible */
		}

		if (fs) {
#ifdef SOLVER_DIAGNOSTICS
		    if (verbose)
			printf("employing %s\n", reason);
#endif
		    fill_square(w, h, x, y, +1, soln, sc->connected, sc);
		    done_something = TRUE;
		} else if (bs) {
#ifdef SOLVER_DIAGNOSTICS
		    if (verbose)
			printf("employing %s\n", reason);
#endif
		    fill_square(w, h, x, y, -1, soln, sc->connected, sc);
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
    int *connected, *indices;

    /*
     * Clear the output.
     */
    memset(soln, 0, w*h);

    /*
     * Establish a disjoint set forest for tracking connectedness
     * between grid points.
     */
    connected = snewn(W*H, int);
    for (i = 0; i < W*H; i++)
	connected[i] = i;		       /* initially all distinct */

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

	fs = (dsf_canonify(connected, y*W+x) ==
	      dsf_canonify(connected, (y+1)*W+(x+1)));
	bs = (dsf_canonify(connected, (y+1)*W+x) ==
	      dsf_canonify(connected, y*W+(x+1)));

	/*
	 * It isn't possible to get into a situation where we
	 * aren't allowed to place _either_ type of slash in a
	 * square. Thus, filled-grid generation never has to
	 * backtrack.
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
	fill_square(w, h, x, y, v, soln, connected, NULL);
    }

    sfree(indices);
    sfree(connected);
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int w = params->w, h = params->h, W = w+1, H = h+1;
    signed char *soln, *tmpsoln, *clues;
    int *clueindices;
    struct solver_scratch *sc;
    int x, y, v, i, j;
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

	/*
	 * With all clue points filled in, all puzzles are easy: we can
	 * simply process the clue points in lexicographic order, and
	 * at each clue point we will always have at most one square
	 * undecided, which we can then fill in uniquely.
	 */
	assert(slant_solve(w, h, clues, tmpsoln, sc, DIFF_EASY) == 1);

	/*
	 * Remove as many clues as possible while retaining solubility.
	 *
	 * In DIFF_HARD mode, we prioritise the removal of obvious
	 * starting points (4s, 0s, border 2s and corner 1s), on
	 * the grounds that having as few of these as possible
	 * seems like a good thing. In particular, we can often get
	 * away without _any_ completely obvious starting points,
	 * which is even better.
	 */
	for (i = 0; i < W*H; i++)
	    clueindices[i] = i;
	shuffle(clueindices, W*H, sizeof(*clueindices), rs);
	for (j = 0; j < 2; j++) {
	    for (i = 0; i < W*H; i++) {
		int pass, yb, xb;

		y = clueindices[i] / W;
		x = clueindices[i] % W;
		v = clues[y*W+x];

		/*
		 * Identify which pass we should process this point
		 * in. If it's an obvious start point, _or_ we're
		 * in DIFF_EASY, then it goes in pass 0; otherwise
		 * pass 1.
		 */
		xb = (x == 0 || x == W-1);
		yb = (y == 0 || y == H-1);
		if (params->diff == DIFF_EASY || v == 4 || v == 0 ||
		    (v == 2 && (xb||yb)) || (v == 1 && xb && yb))
		    pass = 0;
		else
		    pass = 1;

		if (pass == j) {
		    clues[y*W+x] = -1;
		    if (slant_solve(w, h, clues, tmpsoln, sc,
				    params->diff) != 1)
			clues[y*W+x] = v;	       /* put it back */
		}
	    }
	}

	/*
	 * And finally, verify that the grid is of _at least_ the
	 * requested difficulty, by running the solver one level
	 * down and verifying that it can't manage it.
	 */
    } while (params->diff > 0 &&
	     slant_solve(w, h, clues, tmpsoln, sc, params->diff - 1) <= 1);

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

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int w = params->w, h = params->h, W = w+1, H = h+1;
    game_state *state = snew(game_state);
    int area = W*H;
    int squares = 0;

    state->p = *params;
    state->soln = snewn(w*h, signed char);
    memset(state->soln, 0, w*h);
    state->completed = state->used_solve = FALSE;
    state->errors = snewn(W*H, unsigned char);
    memset(state->errors, 0, W*H);

    state->clues = snew(game_clues);
    state->clues->w = w;
    state->clues->h = h;
    state->clues->clues = snewn(W*H, signed char);
    state->clues->refcount = 1;
    state->clues->tmpdsf = snewn(W*H, int);
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
    int w = state->p.w, h = state->p.h, W = w+1, H = h+1;
    game_state *ret = snew(game_state);

    ret->p = state->p;
    ret->clues = state->clues;
    ret->clues->refcount++;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;

    ret->soln = snewn(w*h, signed char);
    memcpy(ret->soln, state->soln, w*h);

    ret->errors = snewn(W*H, unsigned char);
    memcpy(ret->errors, state->errors, W*H);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->errors);
    sfree(state->soln);
    assert(state->clues);
    if (--state->clues->refcount <= 0) {
        sfree(state->clues->clues);
        sfree(state->clues->tmpdsf);
        sfree(state->clues);
    }
    sfree(state);
}

/*
 * Utility function to return the current degree of a vertex. If
 * `anti' is set, it returns the number of filled-in edges
 * surrounding the point which _don't_ connect to it; thus 4 minus
 * its anti-degree is the maximum degree it could have if all the
 * empty spaces around it were filled in.
 * 
 * (Yes, _4_ minus its anti-degree even if it's a border vertex.)
 * 
 * If ret > 0, *sx and *sy are set to the coordinates of one of the
 * squares that contributed to it.
 */
static int vertex_degree(int w, int h, signed char *soln, int x, int y,
                         int anti, int *sx, int *sy)
{
    int ret = 0;

    assert(x >= 0 && x <= w && y >= 0 && y <= h);
    if (x > 0 && y > 0 && soln[(y-1)*w+(x-1)] - anti < 0) {
        if (sx) *sx = x-1;
        if (sy) *sy = y-1;
        ret++;
    }
    if (x > 0 && y < h && soln[y*w+(x-1)] + anti > 0) {
        if (sx) *sx = x-1;
        if (sy) *sy = y;
        ret++;
    }
    if (x < w && y > 0 && soln[(y-1)*w+x] + anti > 0) {
        if (sx) *sx = x;
        if (sy) *sy = y-1;
        ret++;
    }
    if (x < w && y < h && soln[y*w+x] - anti < 0) {
        if (sx) *sx = x;
        if (sy) *sy = y;
        ret++;
    }

    return anti ? 4 - ret : ret;
}

static int check_completion(game_state *state)
{
    int w = state->p.w, h = state->p.h, W = w+1, H = h+1;
    int i, x, y, err = FALSE;
    int *dsf;

    memset(state->errors, 0, W*H);

    /*
     * To detect loops in the grid, we iterate through each edge
     * building up a dsf of connected components, and raise the
     * alarm whenever we find an edge that connects two
     * already-connected vertices.
     * 
     * We use the `tmpdsf' scratch space in the shared clues
     * structure, to avoid mallocing too often.
     * 
     * When we find such an edge, we then search around the grid to
     * find the loop it is a part of, so that we can highlight it
     * as an error for the user. We do this by the hand-on-one-wall
     * technique: the search will follow branches off the inside of
     * the loop, discover they're dead ends, and unhighlight them
     * again when returning to the actual loop.
     * 
     * This technique guarantees that every loop it tracks will
     * surround a disjoint area of the grid (since if an existing
     * loop appears on the boundary of a new one, so that there are
     * multiple possible paths that would come back to the starting
     * point, it will pick the one that allows it to turn right
     * most sharply and hence the one that does not re-surround the
     * area of the previous one). Thus, the total time taken in
     * searching round loops is linear in the grid area since every
     * edge is visited at most twice.
     */
    dsf = state->clues->tmpdsf;
    for (i = 0; i < W*H; i++)
        dsf[i] = i;		       /* initially all distinct */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int i1, i2;

            if (state->soln[y*w+x] == 0)
                continue;
            if (state->soln[y*w+x] < 0) {
                i1 = y*W+x;
                i2 = (y+1)*W+(x+1);
            } else {
                i1 = y*W+(x+1);
                i2 = (y+1)*W+x;
            }

            /*
             * Our edge connects i1 with i2. If they're already
             * connected, flag an error. Otherwise, link them.
             */
            if (dsf_canonify(dsf, i1) == dsf_canonify(dsf, i2)) {
		int x1, y1, x2, y2, dx, dy, dt, pass;

		err = TRUE;

		/*
		 * Now search around the boundary of the loop to
		 * highlight it.
		 * 
		 * We have to do this in two passes. The first
		 * time, we toggle ERR_SQUARE_TMP on each edge;
		 * this pass terminates with ERR_SQUARE_TMP set on
		 * exactly the loop edges. In the second pass, we
		 * trace round that loop again and turn
		 * ERR_SQUARE_TMP into ERR_SQUARE. We have to do
		 * this because otherwise we might cancel part of a
		 * loop highlighted in a previous iteration of the
		 * outer loop.
		 */

		for (pass = 0; pass < 2; pass++) {

		    x1 = i1 % W;
		    y1 = i1 / W;
		    x2 = i2 % W;
		    y2 = i2 / W;

		    do {
			/* Mark this edge. */
			if (pass == 0) {
			    state->errors[min(y1,y2)*W+min(x1,x2)] ^=
				ERR_SQUARE_TMP;
			} else {
			    state->errors[min(y1,y2)*W+min(x1,x2)] |=
				ERR_SQUARE;
			    state->errors[min(y1,y2)*W+min(x1,x2)] &=
				~ERR_SQUARE_TMP;
			}

			/*
			 * Progress to the next edge by turning as
			 * sharply right as possible. In fact we do
			 * this by facing back along the edge and
			 * turning _left_ until we see an edge we
			 * can follow.
			 */
			dx = x1 - x2;
			dy = y1 - y2;

			for (i = 0; i < 4; i++) {
			    /*
			     * Rotate (dx,dy) to the left.
			     */
			    dt = dx; dx = dy; dy = -dt;

			    /*
			     * See if (x2,y2) has an edge in direction
			     * (dx,dy).
			     */
			    if (x2+dx < 0 || x2+dx >= W ||
				y2+dy < 0 || y2+dy >= H)
				continue;  /* off the side of the grid */
			    /* In the second pass, ignore unmarked edges. */
			    if (pass == 1 &&
				!(state->errors[(y2-(dy<0))*W+x2-(dx<0)] &
				  ERR_SQUARE_TMP))
				continue;
			    if (state->soln[(y2-(dy<0))*w+x2-(dx<0)] ==
				(dx==dy ? -1 : +1))
				break;
			}

			/*
			 * In pass 0, we expect to have found
			 * _some_ edge we can follow, even if it
			 * was found by rotating all the way round
			 * and going back the way we came.
			 * 
			 * In pass 1, because we're removing the
			 * mark on each edge that allows us to
			 * follow it, we expect to find _no_ edge
			 * we can follow when we've come all the
			 * way round the loop.
			 */
			if (pass == 1 && i == 4)
			    break;
			assert(i < 4);

			/*
			 * Set x1,y1 to x2,y2, and x2,y2 to be the
			 * other end of the new edge.
			 */
			x1 = x2;
			y1 = y2;
			x2 += dx;
			y2 += dy;
		    } while (y2*W+x2 != i2);

		}
		
	    } else
                dsf_merge(dsf, i1, i2);
        }

    /*
     * Now go through and check the degree of each clue vertex, and
     * mark it with ERR_VERTEX if it cannot be fulfilled.
     */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int c;

	    if ((c = state->clues->clues[y*W+x]) < 0)
		continue;

            /*
             * Check to see if there are too many connections to
             * this vertex _or_ too many non-connections. Either is
             * grounds for marking the vertex as erroneous.
             */
            if (vertex_degree(w, h, state->soln, x, y,
                              FALSE, NULL, NULL) > c ||
                vertex_degree(w, h, state->soln, x, y,
                              TRUE, NULL, NULL) > 4-c) {
                state->errors[y*W+x] |= ERR_VERTEX;
                err = TRUE;
            }
        }

    /*
     * Now our actual victory condition is that (a) none of the
     * above code marked anything as erroneous, and (b) every
     * square has an edge in it.
     */

    if (err)
        return FALSE;

    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (state->soln[y*w+x] == 0)
		return FALSE;

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
	ret = slant_solve(w, h, state->clues->clues, soln, sc, DIFF_HARD);
	free_scratch(sc);
	if (ret != 1) {
	    sfree(soln);
	    if (ret == 0)
		*error = "This puzzle is not self-consistent";
	    else
		*error = "Unable to find a unique solution for this puzzle";
            return NULL;
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
		int len = sprintf(buf, ";%c%d,%d", (int)(v < 0 ? '\\' : '/'), x, y);
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
#define BACKSLASH 0x00000001L
#define FORWSLASH 0x00000002L
#define L_T       0x00000004L
#define ERR_L_T   0x00000008L
#define L_B       0x00000010L
#define ERR_L_B   0x00000020L
#define T_L       0x00000040L
#define ERR_T_L   0x00000080L
#define T_R       0x00000100L
#define ERR_T_R   0x00000200L
#define C_TL      0x00000400L
#define ERR_C_TL  0x00000800L
#define FLASH     0x00001000L
#define ERRSLASH  0x00002000L
#define ERR_TL    0x00004000L
#define ERR_TR    0x00008000L
#define ERR_BL    0x00010000L
#define ERR_BR    0x00020000L

struct game_drawstate {
    int tilesize;
    int started;
    long *grid;
    long *todraw;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int w = state->p.w, h = state->p.h;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        int v;
        char buf[80];

	/*
	 * This is an utterly awful hack which I should really sort out
	 * by means of a proper configuration mechanism. One Slant
	 * player has observed that they prefer the mouse buttons to
	 * function exactly the opposite way round, so here's a
	 * mechanism for environment-based configuration. I cache the
	 * result in a global variable - yuck! - to avoid repeated
	 * lookups.
	 */
	{
	    static int swap_buttons = -1;
	    if (swap_buttons < 0) {
		char *env = getenv("SLANT_SWAP_BUTTONS");
		swap_buttons = (env && (env[0] == 'y' || env[0] == 'Y'));
	    }
	    if (swap_buttons) {
		if (button == LEFT_BUTTON)
		    button = RIGHT_BUTTON;
		else
		    button = LEFT_BUTTON;
	    }
	}

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

        sprintf(buf, "%c%d,%d", (int)(v==-1 ? '\\' : v==+1 ? '/' : 'C'), x, y);
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

    /*
     * We never clear the `completed' flag, but we must always
     * re-run the completion check because it also highlights
     * errors in the grid.
     */
    ret->completed = check_completion(ret) || ret->completed;

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

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
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

    ret[COL_SLANT1 * 3 + 0] = 0.0F;
    ret[COL_SLANT1 * 3 + 1] = 0.0F;
    ret[COL_SLANT1 * 3 + 2] = 0.0F;

    ret[COL_SLANT2 * 3 + 0] = 0.0F;
    ret[COL_SLANT2 * 3 + 1] = 0.0F;
    ret[COL_SLANT2 * 3 + 2] = 0.0F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    int w = state->p.w, h = state->p.h;
    int i;
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->started = FALSE;
    ds->grid = snewn((w+2)*(h+2), long);
    ds->todraw = snewn((w+2)*(h+2), long);
    for (i = 0; i < (w+2)*(h+2); i++)
	ds->grid[i] = ds->todraw[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->todraw);
    sfree(ds->grid);
    sfree(ds);
}

static void draw_clue(drawing *dr, game_drawstate *ds,
		      int x, int y, long v, long err, int bg, int colour)
{
    char p[2];
    int ccol = colour >= 0 ? colour : ((x ^ y) & 1) ? COL_SLANT1 : COL_SLANT2;
    int tcol = colour >= 0 ? colour : err ? COL_ERROR : COL_INK;

    if (v < 0)
	return;

    p[0] = v + '0';
    p[1] = '\0';
    draw_circle(dr, COORD(x), COORD(y), CLUE_RADIUS,
		bg >= 0 ? bg : COL_BACKGROUND, ccol);
    draw_text(dr, COORD(x), COORD(y), FONT_VARIABLE,
	      CLUE_TEXTSIZE, ALIGN_VCENTRE|ALIGN_HCENTRE, tcol, p);
}

static void draw_tile(drawing *dr, game_drawstate *ds, game_clues *clues,
		      int x, int y, long v)
{
    int w = clues->w, h = clues->h, W = w+1 /*, H = h+1 */;
    int chesscolour = (x ^ y) & 1;
    int fscol = chesscolour ? COL_SLANT2 : COL_SLANT1;
    int bscol = chesscolour ? COL_SLANT1 : COL_SLANT2;

    clip(dr, COORD(x), COORD(y), TILESIZE, TILESIZE);

    draw_rect(dr, COORD(x), COORD(y), TILESIZE, TILESIZE,
	      (v & FLASH) ? COL_GRID : COL_BACKGROUND);

    /*
     * Draw the grid lines.
     */
    if (x >= 0 && x < w && y >= 0)
        draw_rect(dr, COORD(x), COORD(y), TILESIZE+1, 1, COL_GRID);
    if (x >= 0 && x < w && y < h)
        draw_rect(dr, COORD(x), COORD(y+1), TILESIZE+1, 1, COL_GRID);
    if (y >= 0 && y < h && x >= 0)
        draw_rect(dr, COORD(x), COORD(y), 1, TILESIZE+1, COL_GRID);
    if (y >= 0 && y < h && x < w)
        draw_rect(dr, COORD(x+1), COORD(y), 1, TILESIZE+1, COL_GRID);
    if (x == -1 && y == -1)
        draw_rect(dr, COORD(x+1), COORD(y+1), 1, 1, COL_GRID);
    if (x == -1 && y == h)
        draw_rect(dr, COORD(x+1), COORD(y), 1, 1, COL_GRID);
    if (x == w && y == -1)
        draw_rect(dr, COORD(x), COORD(y+1), 1, 1, COL_GRID);
    if (x == w && y == h)
        draw_rect(dr, COORD(x), COORD(y), 1, 1, COL_GRID);

    /*
     * Draw the slash.
     */
    if (v & BACKSLASH) {
        int scol = (v & ERRSLASH) ? COL_ERROR : bscol;
	draw_line(dr, COORD(x), COORD(y), COORD(x+1), COORD(y+1), scol);
	draw_line(dr, COORD(x)+1, COORD(y), COORD(x+1), COORD(y+1)-1,
		  scol);
	draw_line(dr, COORD(x), COORD(y)+1, COORD(x+1)-1, COORD(y+1),
		  scol);
    } else if (v & FORWSLASH) {
        int scol = (v & ERRSLASH) ? COL_ERROR : fscol;
	draw_line(dr, COORD(x+1), COORD(y), COORD(x), COORD(y+1), scol);
	draw_line(dr, COORD(x+1)-1, COORD(y), COORD(x), COORD(y+1)-1,
		  scol);
	draw_line(dr, COORD(x+1), COORD(y)+1, COORD(x)+1, COORD(y+1),
		  scol);
    }

    /*
     * Draw dots on the grid corners that appear if a slash is in a
     * neighbouring cell.
     */
    if (v & (L_T | BACKSLASH))
	draw_rect(dr, COORD(x), COORD(y)+1, 1, 1,
                  (v & ERR_L_T ? COL_ERROR : bscol));
    if (v & (L_B | FORWSLASH))
	draw_rect(dr, COORD(x), COORD(y+1)-1, 1, 1,
                  (v & ERR_L_B ? COL_ERROR : fscol));
    if (v & (T_L | BACKSLASH))
	draw_rect(dr, COORD(x)+1, COORD(y), 1, 1,
                  (v & ERR_T_L ? COL_ERROR : bscol));
    if (v & (T_R | FORWSLASH))
	draw_rect(dr, COORD(x+1)-1, COORD(y), 1, 1,
                  (v & ERR_T_R ? COL_ERROR : fscol));
    if (v & (C_TL | BACKSLASH))
	draw_rect(dr, COORD(x), COORD(y), 1, 1,
                  (v & ERR_C_TL ? COL_ERROR : bscol));

    /*
     * And finally the clues at the corners.
     */
    if (x >= 0 && y >= 0)
        draw_clue(dr, ds, x, y, clues->clues[y*W+x], v & ERR_TL, -1, -1);
    if (x < w && y >= 0)
        draw_clue(dr, ds, x+1, y, clues->clues[y*W+(x+1)], v & ERR_TR, -1, -1);
    if (x >= 0 && y < h)
        draw_clue(dr, ds, x, y+1, clues->clues[(y+1)*W+x], v & ERR_BL, -1, -1);
    if (x < w && y < h)
        draw_clue(dr, ds, x+1, y+1, clues->clues[(y+1)*W+(x+1)], v & ERR_BR,
		  -1, -1);

    unclip(dr);
    draw_update(dr, COORD(x), COORD(y), TILESIZE, TILESIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
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
	draw_rect(dr, 0, 0, ww, wh, COL_BACKGROUND);
	draw_update(dr, 0, 0, ww, wh);
	ds->started = TRUE;
    }

    /*
     * Loop over the grid and work out where all the slashes are.
     * We need to do this because a slash in one square affects the
     * drawing of the next one along.
     */
    for (y = -1; y <= h; y++)
	for (x = -1; x <= w; x++) {
            if (x >= 0 && x < w && y >= 0 && y < h)
                ds->todraw[(y+1)*(w+2)+(x+1)] = flashing ? FLASH : 0;
            else
                ds->todraw[(y+1)*(w+2)+(x+1)] = 0;
        }

    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
            int err = state->errors[y*W+x] & ERR_SQUARE;

	    if (state->soln[y*w+x] < 0) {
		ds->todraw[(y+1)*(w+2)+(x+1)] |= BACKSLASH;
                ds->todraw[(y+2)*(w+2)+(x+1)] |= T_R;
                ds->todraw[(y+1)*(w+2)+(x+2)] |= L_B;
                ds->todraw[(y+2)*(w+2)+(x+2)] |= C_TL;
                if (err) {
                    ds->todraw[(y+1)*(w+2)+(x+1)] |= ERRSLASH | 
			ERR_T_L | ERR_L_T | ERR_C_TL;
                    ds->todraw[(y+2)*(w+2)+(x+1)] |= ERR_T_R;
                    ds->todraw[(y+1)*(w+2)+(x+2)] |= ERR_L_B;
                    ds->todraw[(y+2)*(w+2)+(x+2)] |= ERR_C_TL;
                }
	    } else if (state->soln[y*w+x] > 0) {
		ds->todraw[(y+1)*(w+2)+(x+1)] |= FORWSLASH;
                ds->todraw[(y+1)*(w+2)+(x+2)] |= L_T | C_TL;
                ds->todraw[(y+2)*(w+2)+(x+1)] |= T_L | C_TL;
                if (err) {
                    ds->todraw[(y+1)*(w+2)+(x+1)] |= ERRSLASH |
			ERR_L_B | ERR_T_R;
                    ds->todraw[(y+1)*(w+2)+(x+2)] |= ERR_L_T | ERR_C_TL;
                    ds->todraw[(y+2)*(w+2)+(x+1)] |= ERR_T_L | ERR_C_TL;
                }
	    }
	}
    }

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (state->errors[y*W+x] & ERR_VERTEX) {
                ds->todraw[y*(w+2)+x] |= ERR_BR;
                ds->todraw[y*(w+2)+(x+1)] |= ERR_BL;
                ds->todraw[(y+1)*(w+2)+x] |= ERR_TR;
                ds->todraw[(y+1)*(w+2)+(x+1)] |= ERR_TL;
            }

    /*
     * Now go through and draw the grid squares.
     */
    for (y = -1; y <= h; y++) {
	for (x = -1; x <= w; x++) {
	    if (ds->todraw[(y+1)*(w+2)+(x+1)] != ds->grid[(y+1)*(w+2)+(x+1)]) {
		draw_tile(dr, ds, state->clues, x, y,
                          ds->todraw[(y+1)*(w+2)+(x+1)]);
		ds->grid[(y+1)*(w+2)+(x+1)] = ds->todraw[(y+1)*(w+2)+(x+1)];
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

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0;
    *y = ph / 100.0;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w = state->p.w, h = state->p.h, W = w+1;
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, TILESIZE / 16);
    draw_rect_outline(dr, COORD(0), COORD(0), w*TILESIZE, h*TILESIZE, ink);

    /*
     * Grid.
     */
    print_line_width(dr, TILESIZE / 24);
    for (x = 1; x < w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), ink);
    for (y = 1; y < h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), ink);

    /*
     * Solution.
     */
    print_line_width(dr, TILESIZE / 12);
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (state->soln[y*w+x]) {
		int ly, ry;
		/*
		 * To prevent nasty line-ending artefacts at
		 * corners, I'll do something slightly cunning
		 * here.
		 */
		clip(dr, COORD(x), COORD(y), TILESIZE, TILESIZE);
		if (state->soln[y*w+x] < 0)
		    ly = y-1, ry = y+2;
		else
		    ry = y-1, ly = y+2;
		draw_line(dr, COORD(x-1), COORD(ly), COORD(x+2), COORD(ry),
			  ink);
		unclip(dr);
	    }

    /*
     * Clues.
     */
    print_line_width(dr, TILESIZE / 24);
    for (y = 0; y <= h; y++)
	for (x = 0; x <= w; x++)
	    draw_clue(dr, ds, x, y, state->clues->clues[y*W+x],
		      FALSE, paper, ink);
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
    TRUE, FALSE, game_print_size, game_print,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};

#ifdef STANDALONE_SOLVER

#include <stdarg.h>

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *err;
    int grade = FALSE;
    int ret, diff, really_verbose = FALSE;
    struct solver_scratch *sc;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            really_verbose = TRUE;
        } else if (!strcmp(p, "-g")) {
            grade = TRUE;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    sc = new_scratch(p->w, p->h);

    /*
     * When solving an Easy puzzle, we don't want to bother the
     * user with Hard-level deductions. For this reason, we grade
     * the puzzle internally before doing anything else.
     */
    ret = -1;			       /* placate optimiser */
    for (diff = 0; diff < DIFFCOUNT; diff++) {
	ret = slant_solve(p->w, p->h, s->clues->clues,
			  s->soln, sc, diff);
	if (ret < 2)
	    break;
    }

    if (diff == DIFFCOUNT) {
	if (grade)
	    printf("Difficulty rating: harder than Hard, or ambiguous\n");
	else
	    printf("Unable to find a unique solution\n");
    } else {
	if (grade) {
	    if (ret == 0)
		printf("Difficulty rating: impossible (no solution exists)\n");
	    else if (ret == 1)
		printf("Difficulty rating: %s\n", slant_diffnames[diff]);
	} else {
	    verbose = really_verbose;
	    ret = slant_solve(p->w, p->h, s->clues->clues,
			      s->soln, sc, diff);
	    if (ret == 0)
		printf("Puzzle is inconsistent\n");
	    else
		fputs(game_text_format(s), stdout);
	}
    }

    return 0;
}

#endif
