/*
 * separate.c: Implementation of `Block Puzzle', a Japanese-only
 * Nikoli puzzle seen at
 *   http://www.nikoli.co.jp/ja/puzzles/block_puzzle/
 * 
 * It's difficult to be absolutely sure of the rules since online
 * Japanese translators are so bad, but looking at the sample
 * puzzle it seems fairly clear that the rules of this one are
 * very simple. You have an mxn grid in which every square
 * contains a letter, there are k distinct letters with k dividing
 * mn, and every letter occurs the same number of times; your aim
 * is to find a partition of the grid into disjoint k-ominoes such
 * that each k-omino contains exactly one of each letter.
 * 
 * (It may be that Nikoli always have m,n,k equal to one another.
 * However, I don't see that that's critical to the puzzle; k|mn
 * is the only really important constraint, and even that could
 * probably be dispensed with if some squares were marked as
 * unused.)
 */

/*
 * Current status: only the solver/generator is yet written, and
 * although working in principle it's _very_ slow. It generates
 * 5x5n5 or 6x6n4 readily enough, 6x6n6 with a bit of effort, and
 * 7x7n7 only with a serious strain. I haven't dared try it higher
 * than that yet.
 * 
 * One idea to speed it up is to implement more of the solver.
 * Ideas I've so far had include:
 * 
 *  - Generalise the deduction currently expressed as `an
 *    undersized chain with only one direction to extend must take
 *    it'. More generally, the deduction should say `if all the
 *    possible k-ominoes containing a given chain also contain
 *    square x, then mark square x as part of that k-omino'.
 *     + For example, consider this case:
 * 
 *         a ? b    This represents the top left of a board; the letters
 *         ? ? ?    a,b,c do not represent the letters used in the puzzle,
 *         c ? ?    but indicate that those three squares are known to be
 *                  of different ominoes. Now if k >= 4, we can immediately
 *         deduce that the square midway between b and c belongs to the
 *         same omino as a, because there is no way we can make a 4-or-
 *         more-omino containing a which does not also contain that square.
 *         (Most easily seen by imagining cutting that square out of the 
 *         grid; then, clearly, the omino containing a has only two
 *         squares to expand into, and needs at least three.)
 * 
 *    The key difficulty with this mode of reasoning is
 *    identifying such squares. I can't immediately think of a
 *    simple algorithm for finding them on a wholesale basis.
 * 
 *  - Bfs out from a chain looking for the letters it lacks. For
 *    example, in this situation (top three rows of a 7x7n7 grid):
 * 
 *        +-----------+-+
 *        |E-A-F-B-C D|D|
 *        +-------     ||
 *        |E-C-G-D G|G E|
 *        +-+---        |
 *        |E|E G A B F A|
 *
 *    In this situation we can be sure that the top left chain
 *    E-A-F-B-C does extend rightwards to the D, because there is
 *    no other D within reach of that chain. Note also that the
 *    bfs can skip squares which are known to belong to other
 *    ominoes than this one.
 * 
 *    (This deduction, I fear, should only be used in an
 *    emergency, because it relies on _all_ squares within range
 *    of the bfs having particular values and so using it during
 *    incremental generation rather nails down a lot of the grid.)
 * 
 * It's conceivable that another thing we could do would be to
 * increase the flexibility in the grid generator: instead of
 * nailing down the _value_ of any square depended on, merely nail
 * down its equivalence to other squares. Unfortunately this turns
 * the letter-selection phase of generation into a general graph
 * colouring problem (we must draw a graph with equivalence
 * classes of squares as the vertices, and an edge between any two
 * vertices representing equivalence classes which contain squares
 * that share an omino, and then k-colour the result) and hence
 * requires recursion, which bodes ill for something we're doing
 * that many times per generation.
 * 
 * I suppose a simple thing I could try would be tuning the retry
 * count, just in case it's set too high or too low for efficient
 * generation.
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
    NCOLOURS
};

struct game_params {
    int w, h, k;
};

struct game_state {
    int FIXME;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = ret->k = 5;      /* FIXME: a bit bigger? */

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    return false;
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

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = params->k = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'n') {
        string++;
        params->k = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%dn%d", params->w, params->h, params->k);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    return NULL;
}

static game_params *custom_params(const config_item *cfg)
{
    return NULL;
}

static const char *validate_params(const game_params *params, bool full)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver and generator.
 */

struct solver_scratch {
    int w, h, k;

    /*
     * Tracks connectedness between squares.
     */
    int *dsf;

    /*
     * size[dsf_canonify(dsf, yx)] tracks the size of the
     * connected component containing yx.
     */
    int *size;

    /*
     * contents[dsf_canonify(dsf, yx)*k+i] tracks whether or not
     * the connected component containing yx includes letter i. If
     * the value is -1, it doesn't; otherwise its value is the
     * index in the main grid of the square which contributes that
     * letter to the component.
     */
    int *contents;

    /*
     * disconnect[dsf_canonify(dsf, yx1)*w*h + dsf_canonify(dsf, yx2)]
     * tracks whether or not the connected components containing
     * yx1 and yx2 are known to be distinct.
     */
    bool *disconnect;

    /*
     * Temporary space used only inside particular solver loops.
     */
    int *tmp;
};

static struct solver_scratch *solver_scratch_new(int w, int h, int k)
{
    int wh = w*h;
    struct solver_scratch *sc = snew(struct solver_scratch);

    sc->w = w;
    sc->h = h;
    sc->k = k;

    sc->dsf = snew_dsf(wh);
    sc->size = snewn(wh, int);
    sc->contents = snewn(wh * k, int);
    sc->disconnect = snewn(wh*wh, bool);
    sc->tmp = snewn(wh, int);

    return sc;
}

static void solver_scratch_free(struct solver_scratch *sc)
{
    sfree(sc->dsf);
    sfree(sc->size);
    sfree(sc->contents);
    sfree(sc->disconnect);
    sfree(sc->tmp);
    sfree(sc);
}

static void solver_connect(struct solver_scratch *sc, int yx1, int yx2)
{
    int w = sc->w, h = sc->h, k = sc->k;
    int wh = w*h;
    int i, yxnew;

    yx1 = dsf_canonify(sc->dsf, yx1);
    yx2 = dsf_canonify(sc->dsf, yx2);
    assert(yx1 != yx2);

    /*
     * To connect two components together into a bigger one, we
     * start by merging them in the dsf itself.
     */
    dsf_merge(sc->dsf, yx1, yx2);
    yxnew = dsf_canonify(sc->dsf, yx2);

    /*
     * The size of the new component is the sum of the sizes of the
     * old ones.
     */
    sc->size[yxnew] = sc->size[yx1] + sc->size[yx2];

    /*
     * The contents bitmap of the new component is the union of the
     * contents of the old ones.
     * 
     * Given two numbers at most one of which is not -1, we can
     * find the other one by adding the two and adding 1; this
     * will yield -1 if both were -1 to begin with, otherwise the
     * other.
     * 
     * (A neater approach would be to take their bitwise AND, but
     * this is unfortunately not well-defined standard C when done
     * to signed integers.)
     */
    for (i = 0; i < k; i++) {
	assert(sc->contents[yx1*k+i] < 0 || sc->contents[yx2*k+i] < 0);
	sc->contents[yxnew*k+i] = (sc->contents[yx1*k+i] +
				   sc->contents[yx2*k+i] + 1);
    }

    /*
     * We must combine the rows _and_ the columns in the disconnect
     * matrix.
     */
    for (i = 0; i < wh; i++)
	sc->disconnect[yxnew*wh+i] = (sc->disconnect[yx1*wh+i] ||
				      sc->disconnect[yx2*wh+i]);
    for (i = 0; i < wh; i++)
	sc->disconnect[i*wh+yxnew] = (sc->disconnect[i*wh+yx1] ||
				      sc->disconnect[i*wh+yx2]);
}

static void solver_disconnect(struct solver_scratch *sc, int yx1, int yx2)
{
    int w = sc->w, h = sc->h;
    int wh = w*h;

    yx1 = dsf_canonify(sc->dsf, yx1);
    yx2 = dsf_canonify(sc->dsf, yx2);
    assert(yx1 != yx2);
    assert(!sc->disconnect[yx1*wh+yx2]);
    assert(!sc->disconnect[yx2*wh+yx1]);

    /*
     * Mark the components as disconnected from each other in the
     * disconnect matrix.
     */
    sc->disconnect[yx1*wh+yx2] = true;
    sc->disconnect[yx2*wh+yx1] = true;
}

static void solver_init(struct solver_scratch *sc)
{
    int w = sc->w, h = sc->h;
    int wh = w*h;
    int i;

    /*
     * Set up most of the scratch space. We don't set up the
     * contents array, however, because this will change if we
     * adjust the letter arrangement and re-run the solver.
     */
    dsf_init(sc->dsf, wh);
    for (i = 0; i < wh; i++) sc->size[i] = 1;
    memset(sc->disconnect, 0, wh*wh * sizeof(bool));
}

static int solver_attempt(struct solver_scratch *sc, const unsigned char *grid,
                          bool *gen_lock)
{
    int w = sc->w, h = sc->h, k = sc->k;
    int wh = w*h;
    int i, x, y;
    bool done_something_overall = false;

    /*
     * Set up the contents array from the grid.
     */
    for (i = 0; i < wh*k; i++)
	sc->contents[i] = -1;
    for (i = 0; i < wh; i++)
	sc->contents[dsf_canonify(sc->dsf, i)*k+grid[i]] = i;

    while (1) {
	bool done_something = false;

	/*
	 * Go over the grid looking for reasons to add to the
	 * disconnect matrix. We're after pairs of squares which:
	 * 
	 *  - are adjacent in the grid
	 *  - belong to distinct dsf components
	 *  - their components are not already marked as
	 *    disconnected
	 *  - their components share a letter in common.
	 */
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int dir;
		for (dir = 0; dir < 2; dir++) {
		    int x2 = x + dir, y2 = y + 1 - dir;
		    int yx = y*w+x, yx2 = y2*w+x2;

		    if (x2 >= w || y2 >= h)
			continue;      /* one square is outside the grid */

		    yx = dsf_canonify(sc->dsf, yx);
		    yx2 = dsf_canonify(sc->dsf, yx2);
		    if (yx == yx2)
			continue;      /* same dsf component */

		    if (sc->disconnect[yx*wh+yx2])
			continue;      /* already known disconnected */

		    for (i = 0; i < k; i++)
			if (sc->contents[yx*k+i] >= 0 &&
			    sc->contents[yx2*k+i] >= 0)
			    break;
		    if (i == k)
			continue;      /* no letter in common */

		    /*
		     * We've found one. Mark yx and yx2 as
		     * disconnected from each other.
		     */
#ifdef SOLVER_DIAGNOSTICS
		    printf("Disconnecting %d and %d (%c)\n", yx, yx2, 'A'+i);
#endif
		    solver_disconnect(sc, yx, yx2);
		    done_something = done_something_overall = true;

		    /*
		     * We have just made a deduction which hinges
		     * on two particular grid squares being the
		     * same. If we are feeding back to a generator
		     * loop, we must therefore mark those squares
		     * as fixed in the generator, so that future
		     * rearrangement of the grid will not break
		     * the information on which we have already
		     * based deductions.
		     */
		    if (gen_lock) {
			gen_lock[sc->contents[yx*k+i]] = true;
			gen_lock[sc->contents[yx2*k+i]] = true;
		    }
		}
	    }
	}

	/*
	 * Now go over the grid looking for dsf components which
	 * are below maximum size and only have one way to extend,
	 * and extending them.
	 */
	for (i = 0; i < wh; i++)
	    sc->tmp[i] = -1;
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int yx = dsf_canonify(sc->dsf, y*w+x);
		int dir;

		if (sc->size[yx] == k)
		    continue;

		for (dir = 0; dir < 4; dir++) {
		    int x2 = x + (dir==0 ? -1 : dir==2 ? 1 : 0);
		    int y2 = y + (dir==1 ? -1 : dir==3 ? 1 : 0);
		    int yx2, yx2c;

		    if (y2 < 0 || y2 >= h || x2 < 0 || x2 >= w)
			continue;
		    yx2 = y2*w+x2;
		    yx2c = dsf_canonify(sc->dsf, yx2);

		    if (yx2c != yx && !sc->disconnect[yx2c*wh+yx]) {
			/*
			 * Component yx can be extended into square
			 * yx2.
			 */
			if (sc->tmp[yx] == -1)
			    sc->tmp[yx] = yx2;
			else if (sc->tmp[yx] != yx2)
			    sc->tmp[yx] = -2;   /* multiple choices found */
		    }
		}
	    }
	}
	for (i = 0; i < wh; i++) {
	    if (sc->tmp[i] >= 0) {
		/*
		 * Make sure we haven't connected the two already
		 * during this loop (which could happen if for
		 * _both_ components this was the only way to
		 * extend them).
		 */
		if (dsf_canonify(sc->dsf, i) ==
		    dsf_canonify(sc->dsf, sc->tmp[i]))
		    continue;

#ifdef SOLVER_DIAGNOSTICS
		printf("Connecting %d and %d\n", i, sc->tmp[i]);
#endif
		solver_connect(sc, i, sc->tmp[i]);
		done_something = done_something_overall = true;
		break;
	    }
	}

	if (!done_something)
	    break;
    }

    /*
     * Return 0 if we haven't made any progress; 1 if we've done
     * something but not solved it completely; 2 if we've solved
     * it completely.
     */
    for (i = 0; i < wh; i++)
	if (sc->size[dsf_canonify(sc->dsf, i)] != k)
	    break;
    if (i == wh)
	return 2;
    if (done_something_overall)
	return 1;
    return 0;
}

static unsigned char *generate(int w, int h, int k, random_state *rs)
{
    int wh = w*h;
    int n = wh/k;
    struct solver_scratch *sc;
    unsigned char *grid;
    unsigned char *shuffled;
    int i, j, m, retries;
    int *permutation;
    bool *gen_lock;
    extern int *divvy_rectangle(int w, int h, int k, random_state *rs);

    sc = solver_scratch_new(w, h, k);
    grid = snewn(wh, unsigned char);
    shuffled = snewn(k, unsigned char);
    permutation = snewn(wh, int);
    gen_lock = snewn(wh, bool);

    do {
	int *dsf = divvy_rectangle(w, h, k, rs);

	/*
	 * Go through the dsf and find the indices of all the
	 * squares involved in each omino, in a manner conducive
	 * to per-omino indexing. We set permutation[i*k+j] to be
	 * the index of the jth square (ordered arbitrarily) in
	 * omino i.
	 */
	for (i = j = 0; i < wh; i++)
	    if (dsf_canonify(dsf, i) == i) {
		sc->tmp[i] = j;
		/*
		 * During this loop and the following one, we use
		 * the last element of each row of permutation[]
		 * as a counter of the number of indices so far
		 * placed in it. When we place the final index of
		 * an omino, that counter is overwritten, but that
		 * doesn't matter because we'll never use it
		 * again. Of course this depends critically on
		 * divvy_rectangle() having returned correct
		 * results, or else chaos would ensue.
		 */
		permutation[j*k+k-1] = 0;
		j++;
	    }
	for (i = 0; i < wh; i++) {
	    j = sc->tmp[dsf_canonify(dsf, i)];
	    m = permutation[j*k+k-1]++;
	    permutation[j*k+m] = i;
	}

	/*
	 * Track which squares' letters we have already depended
	 * on for deductions. This is gradually updated by
	 * solver_attempt().
	 */
	memset(gen_lock, 0, wh * sizeof(bool));

	/*
	 * Now repeatedly fill the grid with letters, and attempt
	 * to solve it. If the solver makes progress but does not
	 * fail completely, then gen_lock will have been updated
	 * and we try again. On a complete failure, though, we
	 * have no option but to give up and abandon this set of
	 * ominoes.
	 */
	solver_init(sc);
	retries = k*k;
	while (1) {
	    /*
	     * Fill the grid with letters. We can safely use
	     * sc->tmp to hold the set of letters required at each
	     * stage, since it's at least size k and is currently
	     * unused.
	     */
	    for (i = 0; i < n; i++) {
		/*
		 * First, determine the set of letters already
		 * placed in this omino by gen_lock.
		 */
		for (j = 0; j < k; j++)
		    sc->tmp[j] = j;
		for (j = 0; j < k; j++) {
		    int index = permutation[i*k+j];
		    int letter = grid[index];
		    if (gen_lock[index])
			sc->tmp[letter] = -1;
		}
		/*
		 * Now collect together all the remaining letters
		 * and randomly shuffle them.
		 */
		for (j = m = 0; j < k; j++)
		    if (sc->tmp[j] >= 0)
			sc->tmp[m++] = sc->tmp[j];
		shuffle(sc->tmp, m, sizeof(*sc->tmp), rs);
		/*
		 * Finally, write the shuffled letters into the
		 * grid.
		 */
		for (j = 0; j < k; j++) {
		    int index = permutation[i*k+j];
		    if (!gen_lock[index])
			grid[index] = sc->tmp[--m];
		}
		assert(m == 0);
	    }

	    /*
	     * Now we have a candidate grid. Attempt to progress
	     * the solution.
	     */
	    m = solver_attempt(sc, grid, gen_lock);
	    if (m == 2 ||	       /* success */
		(m == 0 && retries-- <= 0))   /* failure */
		break;
	    if (m == 1)
		retries = k*k;	       /* reset this counter, and continue */
	}

	sfree(dsf);
    } while (m == 0);

    sfree(gen_lock);
    sfree(permutation);
    sfree(shuffled);
    solver_scratch_free(sc);

    return grid;
}

/* ----------------------------------------------------------------------
 * End of solver/generator code.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h, wh = w*h, k = params->k;
    unsigned char *grid;
    char *desc;
    int i;

    grid = generate(w, h, k, rs);

    desc = snewn(wh+1, char);
    for (i = 0; i < wh; i++)
	desc[i] = 'A' + grid[i];
    desc[wh] = '\0';

    sfree(grid);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);

    state->FIXME = 0;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->FIXME = state->FIXME;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return NULL;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
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
    int tilesize;
    int FIXME;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = *y = 10 * tilesize;	       /* FIXME */
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->FIXME = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
}

static int game_status(const game_state *state)
{
    return 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return true;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame separate
#endif

const struct game thegame = {
    "Separate", NULL, NULL,
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    false, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    false, solve_game,
    false, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    20 /* FIXME */, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
};
