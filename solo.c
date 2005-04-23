/*
 * solo.c: the number-placing puzzle most popularly known as `Sudoku'.
 *
 * TODO:
 *
 *  - finalise game name
 *
 *  - can we do anything about nasty centring of text in GTK? It
 *    seems to be taking ascenders/descenders into account when
 *    centring. Ick.
 *
 *  - implement stronger modes of reasoning in nsolve, thus
 *    enabling harder puzzles
 *
 *  - configurable difficulty levels
 *
 *  - vary the symmetry (rotational or none)?
 *
 *  - try for cleverer ways of reducing the solved grid; they seem
 *    to be coming out a bit full for the most part, and in
 *    particular it's inexcusable to leave a grid with an entire
 *    block (or presumably row or column) filled! I _hope_ we can
 *    do this simply by better prioritising (somehow) the possible
 *    removals.
 *     + one simple option might be to work the other way: start
 * 	 with an empty grid and gradually _add_ numbers until it
 * 	 becomes solvable? Perhaps there might be some heuristic
 * 	 which enables us to pinpoint the most critical clues and
 * 	 thus add as few as possible.
 *
 *  - alternative interface modes
 *     + sudoku.com's Windows program has a palette of possible
 * 	 entries; you select a palette entry first and then click
 * 	 on the square you want it to go in, thus enabling
 * 	 mouse-only play. Useful for PDAs! I don't think it's
 * 	 actually incompatible with the current highlight-then-type
 * 	 approach: you _either_ highlight a palette entry and then
 * 	 click, _or_ you highlight a square and then type. At most
 * 	 one thing is ever highlighted at a time, so there's no way
 * 	 to confuse the two.
 *     + `pencil marks' might be useful for more subtle forms of
 * 	 deduction, once we implement creation of puzzles that
 * 	 require it.
 */

/*
 * Solo puzzles need to be square overall (since each row and each
 * column must contain one of every digit), but they need not be
 * subdivided the same way internally. I am going to adopt a
 * convention whereby I _always_ refer to `r' as the number of rows
 * of _big_ divisions, and `c' as the number of columns of _big_
 * divisions. Thus, a 2c by 3r puzzle looks something like this:
 *
 *   4 5 1 | 2 6 3
 *   6 3 2 | 5 4 1
 *   ------+------     (Of course, you can't subdivide it the other way
 *   1 4 5 | 6 3 2     or you'll get clashes; observe that the 4 in the
 *   3 2 6 | 4 1 5     top left would conflict with the 4 in the second
 *   ------+------     box down on the left-hand side.)
 *   5 1 4 | 3 2 6
 *   2 6 3 | 1 5 4
 *
 * The need for a strong naming convention should now be clear:
 * each small box is two rows of digits by three columns, while the
 * overall puzzle has three rows of small boxes by two columns. So
 * I will (hopefully) consistently use `r' to denote the number of
 * rows _of small boxes_ (here 3), which is also the number of
 * columns of digits in each small box; and `c' vice versa (here
 * 2).
 *
 * I'm also going to choose arbitrarily to list c first wherever
 * possible: the above is a 2x3 puzzle, not a 3x2 one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/*
 * To save space, I store digits internally as unsigned char. This
 * imposes a hard limit of 255 on the order of the puzzle. Since
 * even a 5x5 takes unacceptably long to generate, I don't see this
 * as a serious limitation unless something _really_ impressive
 * happens in computing technology; but here's a typedef anyway for
 * general good practice.
 */
typedef unsigned char digit;
#define ORDER_MAX 255

#define TILE_SIZE 32
#define BORDER 18

#define FLASH_TIME 0.4F

enum {
    COL_BACKGROUND,
	COL_GRID,
	COL_CLUE,
	COL_USER,
	COL_HIGHLIGHT,
	NCOLOURS
};

struct game_params {
    int c, r;
};

struct game_state {
    int c, r;
    digit *grid;
    unsigned char *immutable;	       /* marks which digits are clues */
    int completed;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->c = ret->r = 3;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int c, r;
    char buf[80];

    switch (i) {
      case 0: c = 2, r = 2; break;
      case 1: c = 2, r = 3; break;
      case 2: c = 3, r = 3; break;
      case 3: c = 3, r = 4; break;
      case 4: c = 4, r = 4; break;
      default: return FALSE;
    }

    sprintf(buf, "%dx%d", c, r);
    *name = dupstr(buf);
    *params = ret = snew(game_params);
    ret->c = c;
    ret->r = r;
    /* FIXME: difficulty presets? */
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

static game_params *decode_params(char const *string)
{
    game_params *ret = default_params();

    ret->c = ret->r = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->r = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    /* FIXME: difficulty levels */

    return ret;
}

static char *encode_params(game_params *params)
{
    char str[80];

    sprintf(str, "%dx%d", params->c, params->r);
    return dupstr(str);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Columns of sub-blocks";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->c);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Rows of sub-blocks";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->r);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    /*
     * FIXME: difficulty level.
     */

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->c = atoi(cfg[0].sval);
    ret->r = atoi(cfg[1].sval);

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->c < 2 || params->r < 2)
	return "Both dimensions must be at least 2";
    if (params->c > ORDER_MAX || params->r > ORDER_MAX)
	return "Dimensions greater than "STR(ORDER_MAX)" are not supported";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Full recursive Solo solver.
 *
 * The algorithm for this solver is shamelessly copied from a
 * Python solver written by Andrew Wilkinson (which is GPLed, but
 * I've reused only ideas and no code). It mostly just does the
 * obvious recursive thing: pick an empty square, put one of the
 * possible digits in it, recurse until all squares are filled,
 * backtrack and change some choices if necessary.
 *
 * The clever bit is that every time it chooses which square to
 * fill in next, it does so by counting the number of _possible_
 * numbers that can go in each square, and it prioritises so that
 * it picks a square with the _lowest_ number of possibilities. The
 * idea is that filling in lots of the obvious bits (particularly
 * any squares with only one possibility) will cut down on the list
 * of possibilities for other squares and hence reduce the enormous
 * search space as much as possible as early as possible.
 *
 * In practice the algorithm appeared to work very well; run on
 * sample problems from the Times it completed in well under a
 * second on my G5 even when written in Python, and given an empty
 * grid (so that in principle it would enumerate _all_ solved
 * grids!) it found the first valid solution just as quickly. So
 * with a bit more randomisation I see no reason not to use this as
 * my grid generator.
 */

/*
 * Internal data structure used in solver to keep track of
 * progress.
 */
struct rsolve_coord { int x, y, r; };
struct rsolve_usage {
    int c, r, cr;		       /* cr == c*r */
    /* grid is a copy of the input grid, modified as we go along */
    digit *grid;
    /* row[y*cr+n-1] TRUE if digit n has been placed in row y */
    unsigned char *row;
    /* col[x*cr+n-1] TRUE if digit n has been placed in row x */
    unsigned char *col;
    /* blk[(y*c+x)*cr+n-1] TRUE if digit n has been placed in block (x,y) */
    unsigned char *blk;
    /* This lists all the empty spaces remaining in the grid. */
    struct rsolve_coord *spaces;
    int nspaces;
    /* If we need randomisation in the solve, this is our random state. */
    random_state *rs;
    /* Number of solutions so far found, and maximum number we care about. */
    int solns, maxsolns;
};

/*
 * The real recursive step in the solving function.
 */
static void rsolve_real(struct rsolve_usage *usage, digit *grid)
{
    int c = usage->c, r = usage->r, cr = usage->cr;
    int i, j, n, sx, sy, bestm, bestr;
    int *digits;

    /*
     * Firstly, check for completion! If there are no spaces left
     * in the grid, we have a solution.
     */
    if (usage->nspaces == 0) {
	if (!usage->solns) {
	    /*
	     * This is our first solution, so fill in the output grid.
	     */
	    memcpy(grid, usage->grid, cr * cr);
	}
	usage->solns++;
	return;
    }

    /*
     * Otherwise, there must be at least one space. Find the most
     * constrained space, using the `r' field as a tie-breaker.
     */
    bestm = cr+1;		       /* so that any space will beat it */
    bestr = 0;
    i = sx = sy = -1;
    for (j = 0; j < usage->nspaces; j++) {
	int x = usage->spaces[j].x, y = usage->spaces[j].y;
	int m;

	/*
	 * Find the number of digits that could go in this space.
	 */
	m = 0;
	for (n = 0; n < cr; n++)
	    if (!usage->row[y*cr+n] && !usage->col[x*cr+n] &&
		!usage->blk[((y/c)*c+(x/r))*cr+n])
		m++;

	if (m < bestm || (m == bestm && usage->spaces[j].r < bestr)) {
	    bestm = m;
	    bestr = usage->spaces[j].r;
	    sx = x;
	    sy = y;
	    i = j;
	}
    }

    /*
     * Swap that square into the final place in the spaces array,
     * so that decrementing nspaces will remove it from the list.
     */
    if (i != usage->nspaces-1) {
	struct rsolve_coord t;
	t = usage->spaces[usage->nspaces-1];
	usage->spaces[usage->nspaces-1] = usage->spaces[i];
	usage->spaces[i] = t;
    }

    /*
     * Now we've decided which square to start our recursion at,
     * simply go through all possible values, shuffling them
     * randomly first if necessary.
     */
    digits = snewn(bestm, int);
    j = 0;
    for (n = 0; n < cr; n++)
	if (!usage->row[sy*cr+n] && !usage->col[sx*cr+n] &&
	    !usage->blk[((sy/c)*c+(sx/r))*cr+n]) {
	    digits[j++] = n+1;
	}

    if (usage->rs) {
	/* shuffle */
	for (i = j; i > 1; i--) {
	    int p = random_upto(usage->rs, i);
	    if (p != i-1) {
		int t = digits[p];
		digits[p] = digits[i-1];
		digits[i-1] = t;
	    }
	}
    }

    /* And finally, go through the digit list and actually recurse. */
    for (i = 0; i < j; i++) {
	n = digits[i];

	/* Update the usage structure to reflect the placing of this digit. */
	usage->row[sy*cr+n-1] = usage->col[sx*cr+n-1] =
	    usage->blk[((sy/c)*c+(sx/r))*cr+n-1] = TRUE;
	usage->grid[sy*cr+sx] = n;
	usage->nspaces--;

	/* Call the solver recursively. */
	rsolve_real(usage, grid);

	/*
	 * If we have seen as many solutions as we need, terminate
	 * all processing immediately.
	 */
	if (usage->solns >= usage->maxsolns)
	    break;

	/* Revert the usage structure. */
	usage->row[sy*cr+n-1] = usage->col[sx*cr+n-1] =
	    usage->blk[((sy/c)*c+(sx/r))*cr+n-1] = FALSE;
	usage->grid[sy*cr+sx] = 0;
	usage->nspaces++;
    }

    sfree(digits);
}

/*
 * Entry point to solver. You give it dimensions and a starting
 * grid, which is simply an array of N^4 digits. In that array, 0
 * means an empty square, and 1..N mean a clue square.
 *
 * Return value is the number of solutions found; searching will
 * stop after the provided `max'. (Thus, you can pass max==1 to
 * indicate that you only care about finding _one_ solution, or
 * max==2 to indicate that you want to know the difference between
 * a unique and non-unique solution.) The input parameter `grid' is
 * also filled in with the _first_ (or only) solution found by the
 * solver.
 */
static int rsolve(int c, int r, digit *grid, random_state *rs, int max)
{
    struct rsolve_usage *usage;
    int x, y, cr = c*r;
    int ret;

    /*
     * Create an rsolve_usage structure.
     */
    usage = snew(struct rsolve_usage);

    usage->c = c;
    usage->r = r;
    usage->cr = cr;

    usage->grid = snewn(cr * cr, digit);
    memcpy(usage->grid, grid, cr * cr);

    usage->row = snewn(cr * cr, unsigned char);
    usage->col = snewn(cr * cr, unsigned char);
    usage->blk = snewn(cr * cr, unsigned char);
    memset(usage->row, FALSE, cr * cr);
    memset(usage->col, FALSE, cr * cr);
    memset(usage->blk, FALSE, cr * cr);

    usage->spaces = snewn(cr * cr, struct rsolve_coord);
    usage->nspaces = 0;

    usage->solns = 0;
    usage->maxsolns = max;

    usage->rs = rs;

    /*
     * Now fill it in with data from the input grid.
     */
    for (y = 0; y < cr; y++) {
	for (x = 0; x < cr; x++) {
	    int v = grid[y*cr+x];
	    if (v == 0) {
		usage->spaces[usage->nspaces].x = x;
		usage->spaces[usage->nspaces].y = y;
		if (rs)
		    usage->spaces[usage->nspaces].r = random_bits(rs, 31);
		else
		    usage->spaces[usage->nspaces].r = usage->nspaces;
		usage->nspaces++;
	    } else {
		usage->row[y*cr+v-1] = TRUE;
		usage->col[x*cr+v-1] = TRUE;
		usage->blk[((y/c)*c+(x/r))*cr+v-1] = TRUE;
	    }
	}
    }

    /*
     * Run the real recursive solving function.
     */
    rsolve_real(usage, grid);
    ret = usage->solns;

    /*
     * Clean up the usage structure now we have our answer.
     */
    sfree(usage->spaces);
    sfree(usage->blk);
    sfree(usage->col);
    sfree(usage->row);
    sfree(usage->grid);
    sfree(usage);

    /*
     * And return.
     */
    return ret;
}

/* ----------------------------------------------------------------------
 * End of recursive solver code.
 */

/* ----------------------------------------------------------------------
 * Less capable non-recursive solver. This one is used to check
 * solubility of a grid as we gradually remove numbers from it: by
 * verifying a grid using this solver we can ensure it isn't _too_
 * hard (e.g. does not actually require guessing and backtracking).
 *
 * It supports a variety of specific modes of reasoning. By
 * enabling or disabling subsets of these modes we can arrange a
 * range of difficulty levels.
 */

/*
 * Modes of reasoning currently supported:
 *
 *  - Positional elimination: a number must go in a particular
 *    square because all the other empty squares in a given
 *    row/col/blk are ruled out.
 *
 *  - Numeric elimination: a square must have a particular number
 *    in because all the other numbers that could go in it are
 *    ruled out.
 *
 * More advanced modes of reasoning I'd like to support in future:
 *
 *  - Intersectional elimination: given two domains which overlap
 *    (hence one must be a block, and the other can be a row or
 *    col), if the possible locations for a particular number in
 *    one of the domains can be narrowed down to the overlap, then
 *    that number can be ruled out everywhere but the overlap in
 *    the other domain too.
 *
 *  - Setwise numeric elimination: if there is a subset of the
 *    empty squares within a domain such that the union of the
 *    possible numbers in that subset has the same size as the
 *    subset itself, then those numbers can be ruled out everywhere
 *    else in the domain. (For example, if there are five empty
 *    squares and the possible numbers in each are 12, 23, 13, 134
 *    and 1345, then the first three empty squares form such a
 *    subset: the numbers 1, 2 and 3 _must_ be in those three
 *    squares in some permutation, and hence we can deduce none of
 *    them can be in the fourth or fifth squares.)
 */

struct nsolve_usage {
    int c, r, cr;
    /*
     * We set up a cubic array, indexed by x, y and digit; each
     * element of this array is TRUE or FALSE according to whether
     * or not that digit _could_ in principle go in that position.
     *
     * The way to index this array is cube[(x*cr+y)*cr+n-1].
     */
    unsigned char *cube;
    /*
     * This is the grid in which we write down our final
     * deductions.
     */
    digit *grid;
    /*
     * Now we keep track, at a slightly higher level, of what we
     * have yet to work out, to prevent doing the same deduction
     * many times.
     */
    /* row[y*cr+n-1] TRUE if digit n has been placed in row y */
    unsigned char *row;
    /* col[x*cr+n-1] TRUE if digit n has been placed in row x */
    unsigned char *col;
    /* blk[(y*c+x)*cr+n-1] TRUE if digit n has been placed in block (x,y) */
    unsigned char *blk;
};
#define cube(x,y,n) (usage->cube[((x)*usage->cr+(y))*usage->cr+(n)-1])

/*
 * Function called when we are certain that a particular square has
 * a particular number in it.
 */
static void nsolve_place(struct nsolve_usage *usage, int x, int y, int n)
{
    int c = usage->c, r = usage->r, cr = usage->cr;
    int i, j, bx, by;

    assert(cube(x,y,n));

    /*
     * Rule out all other numbers in this square.
     */
    for (i = 1; i <= cr; i++)
	if (i != n)
	    cube(x,y,i) = FALSE;

    /*
     * Rule out this number in all other positions in the row.
     */
    for (i = 0; i < cr; i++)
	if (i != y)
	    cube(x,i,n) = FALSE;

    /*
     * Rule out this number in all other positions in the column.
     */
    for (i = 0; i < cr; i++)
	if (i != x)
	    cube(i,y,n) = FALSE;

    /*
     * Rule out this number in all other positions in the block.
     */
    bx = (x/r)*r;
    by = (y/c)*c;
    for (i = 0; i < r; i++)
	for (j = 0; j < c; j++)
	    if (bx+i != x || by+j != y)
		cube(bx+i,by+j,n) = FALSE;

    /*
     * Enter the number in the result grid.
     */
    usage->grid[y*cr+x] = n;

    /*
     * Cross out this number from the list of numbers left to place
     * in its row, its column and its block.
     */
    usage->row[y*cr+n-1] = usage->col[x*cr+n-1] =
	usage->blk[((y/c)*c+(x/r))*cr+n-1] = TRUE;
}

static int nsolve_blk_pos_elim(struct nsolve_usage *usage,
			       int x, int y, int n)
{
    int c = usage->c, r = usage->r;
    int i, j, fx, fy, m;

    x *= r;
    y *= c;

    /*
     * Count the possible positions within this block where this
     * number could appear.
     */
    m = 0;
    fx = fy = -1;
    for (i = 0; i < r; i++)
	for (j = 0; j < c; j++)
	    if (cube(x+i,y+j,n)) {
		fx = x+i;
		fy = y+j;
		m++;
	    }

    if (m == 1) {
	assert(fx >= 0 && fy >= 0);
	nsolve_place(usage, fx, fy, n);
	return TRUE;
    }

    return FALSE;
}

static int nsolve_row_pos_elim(struct nsolve_usage *usage,
			       int y, int n)
{
    int cr = usage->cr;
    int x, fx, m;

    /*
     * Count the possible positions within this row where this
     * number could appear.
     */
    m = 0;
    fx = -1;
    for (x = 0; x < cr; x++)
	if (cube(x,y,n)) {
	    fx = x;
	    m++;
	}

    if (m == 1) {
	assert(fx >= 0);
	nsolve_place(usage, fx, y, n);
	return TRUE;
    }

    return FALSE;
}

static int nsolve_col_pos_elim(struct nsolve_usage *usage,
			       int x, int n)
{
    int cr = usage->cr;
    int y, fy, m;

    /*
     * Count the possible positions within this column where this
     * number could appear.
     */
    m = 0;
    fy = -1;
    for (y = 0; y < cr; y++)
	if (cube(x,y,n)) {
	    fy = y;
	    m++;
	}

    if (m == 1) {
	assert(fy >= 0);
	nsolve_place(usage, x, fy, n);
	return TRUE;
    }

    return FALSE;
}

static int nsolve_num_elim(struct nsolve_usage *usage,
			   int x, int y)
{
    int cr = usage->cr;
    int n, fn, m;

    /*
     * Count the possible numbers that could appear in this square.
     */
    m = 0;
    fn = -1;
    for (n = 1; n <= cr; n++)
	if (cube(x,y,n)) {
	    fn = n;
	    m++;
	}

    if (m == 1) {
	assert(fn > 0);
	nsolve_place(usage, x, y, fn);
	return TRUE;
    }

    return FALSE;
}

static int nsolve(int c, int r, digit *grid)
{
    struct nsolve_usage *usage;
    int cr = c*r;
    int x, y, n;

    /*
     * Set up a usage structure as a clean slate (everything
     * possible).
     */
    usage = snew(struct nsolve_usage);
    usage->c = c;
    usage->r = r;
    usage->cr = cr;
    usage->cube = snewn(cr*cr*cr, unsigned char);
    usage->grid = grid;		       /* write straight back to the input */
    memset(usage->cube, TRUE, cr*cr*cr);

    usage->row = snewn(cr * cr, unsigned char);
    usage->col = snewn(cr * cr, unsigned char);
    usage->blk = snewn(cr * cr, unsigned char);
    memset(usage->row, FALSE, cr * cr);
    memset(usage->col, FALSE, cr * cr);
    memset(usage->blk, FALSE, cr * cr);

    /*
     * Place all the clue numbers we are given.
     */
    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++)
	    if (grid[y*cr+x])
		nsolve_place(usage, x, y, grid[y*cr+x]);

    /*
     * Now loop over the grid repeatedly trying all permitted modes
     * of reasoning. The loop terminates if we complete an
     * iteration without making any progress; we then return
     * failure or success depending on whether the grid is full or
     * not.
     */
    while (1) {
	/*
	 * Blockwise positional elimination.
	 */
	for (x = 0; x < c; x++)
	    for (y = 0; y < r; y++)
		for (n = 1; n <= cr; n++)
		    if (!usage->blk[((y/c)*c+(x/r))*cr+n-1] &&
			nsolve_blk_pos_elim(usage, x, y, n))
			continue;

	/*
	 * Row-wise positional elimination.
	 */
	for (y = 0; y < cr; y++)
	    for (n = 1; n <= cr; n++)
		if (!usage->row[y*cr+n-1] &&
		    nsolve_row_pos_elim(usage, y, n))
		    continue;
	/*
	 * Column-wise positional elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (n = 1; n <= cr; n++)
		if (!usage->col[x*cr+n-1] &&
		    nsolve_col_pos_elim(usage, x, n))
		    continue;

	/*
	 * Numeric elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (y = 0; y < cr; y++)
		if (!usage->grid[y*cr+x] &&
		    nsolve_num_elim(usage, x, y))
		    continue;

	/*
	 * If we reach here, we have made no deductions in this
	 * iteration, so the algorithm terminates.
	 */
	break;
    }

    sfree(usage->cube);
    sfree(usage->row);
    sfree(usage->col);
    sfree(usage->blk);
    sfree(usage);

    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++)
	    if (!grid[y*cr+x])
		return FALSE;
    return TRUE;
}

/* ----------------------------------------------------------------------
 * End of non-recursive solver code.
 */

/*
 * Check whether a grid contains a valid complete puzzle.
 */
static int check_valid(int c, int r, digit *grid)
{
    int cr = c*r;
    unsigned char *used;
    int x, y, n;

    used = snewn(cr, unsigned char);

    /*
     * Check that each row contains precisely one of everything.
     */
    for (y = 0; y < cr; y++) {
	memset(used, FALSE, cr);
	for (x = 0; x < cr; x++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = TRUE;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return FALSE;
	    }
    }

    /*
     * Check that each column contains precisely one of everything.
     */
    for (x = 0; x < cr; x++) {
	memset(used, FALSE, cr);
	for (y = 0; y < cr; y++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = TRUE;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return FALSE;
	    }
    }

    /*
     * Check that each block contains precisely one of everything.
     */
    for (x = 0; x < cr; x += r) {
	for (y = 0; y < cr; y += c) {
	    int xx, yy;
	    memset(used, FALSE, cr);
	    for (xx = x; xx < x+r; xx++)
		for (yy = 0; yy < y+c; yy++)
		    if (grid[yy*cr+xx] > 0 && grid[yy*cr+xx] <= cr)
			used[grid[yy*cr+xx]-1] = TRUE;
	    for (n = 0; n < cr; n++)
		if (!used[n]) {
		    sfree(used);
		    return FALSE;
		}
	}
    }

    sfree(used);
    return TRUE;
}

static char *new_game_seed(game_params *params, random_state *rs)
{
    int c = params->c, r = params->r, cr = c*r;
    int area = cr*cr;
    digit *grid, *grid2;
    struct xy { int x, y; } *locs;
    int nlocs;
    int ret;
    char *seed;

    /*
     * Start the recursive solver with an empty grid to generate a
     * random solved state.
     */
    grid = snewn(area, digit);
    memset(grid, 0, area);
    ret = rsolve(c, r, grid, rs, 1);
    assert(ret == 1);
    assert(check_valid(c, r, grid));

#ifdef DEBUG
    memcpy(grid,
           "\x0\x1\x0\x0\x6\x0\x0\x0\x0"
           "\x5\x0\x0\x7\x0\x4\x0\x2\x0"
           "\x0\x0\x6\x1\x0\x0\x0\x0\x0"
           "\x8\x9\x7\x0\x0\x0\x0\x0\x0"
           "\x0\x0\x3\x0\x4\x0\x9\x0\x0"
           "\x0\x0\x0\x0\x0\x0\x8\x7\x6"
           "\x0\x0\x0\x0\x0\x9\x1\x0\x0"
           "\x0\x3\x0\x6\x0\x5\x0\x0\x7"
           "\x0\x0\x0\x0\x8\x0\x0\x5\x0"
	   , area);

    {
	int y, x;
	for (y = 0; y < cr; y++) {
	    for (x = 0; x < cr; x++) {
		printf("%2.0d", grid[y*cr+x]);
	    }
	    printf("\n");
	}
	printf("\n");
    }

    nsolve(c, r, grid);

    {
	int y, x;
	for (y = 0; y < cr; y++) {
	    for (x = 0; x < cr; x++) {
		printf("%2.0d", grid[y*cr+x]);
	    }
	    printf("\n");
	}
	printf("\n");
    }
#endif

    /*
     * Now we have a solved grid, start removing things from it
     * while preserving solubility.
     */
    locs = snewn((cr+1)/2 * (cr+1)/2, struct xy);
    grid2 = snewn(area, digit);
    while (1) {
	int x, y, i;

	/*
	 * Iterate over the top left corner of the grid and
	 * enumerate all the filled squares we could empty.
	 */
	nlocs = 0;

	for (x = 0; 2*x < cr; x++)
	    for (y = 0; 2*y < cr; y++)
		if (grid[y*cr+x]) {
		    locs[nlocs].x = x;
		    locs[nlocs].y = y;
		    nlocs++;
		}

	/*
	 * Now shuffle that list.
	 */
	for (i = nlocs; i > 1; i--) {
	    int p = random_upto(rs, i);
	    if (p != i-1) {
		struct xy t = locs[p];
		locs[p] = locs[i-1];
		locs[i-1] = t;
	    }
	}

	/*
	 * Now loop over the shuffled list and, for each element,
	 * see whether removing that element (and its reflections)
	 * from the grid will still leave the grid soluble by
	 * nsolve.
	 */
	for (i = 0; i < nlocs; i++) {
	    x = locs[i].x;
	    y = locs[i].y;

	    memcpy(grid2, grid, area);
	    grid2[y*cr+x] = 0;
	    grid2[y*cr+cr-1-x] = 0;
	    grid2[(cr-1-y)*cr+x] = 0;
	    grid2[(cr-1-y)*cr+cr-1-x] = 0;

	    if (nsolve(c, r, grid2)) {
		grid[y*cr+x] = 0;
		grid[y*cr+cr-1-x] = 0;
		grid[(cr-1-y)*cr+x] = 0;
		grid[(cr-1-y)*cr+cr-1-x] = 0;
		break;
	    }
	}

	if (i == nlocs) {
	    /*
	     * There was nothing we could remove without destroying
	     * solvability.
	     */
	    break;
	}
    }
    sfree(grid2);
    sfree(locs);

#ifdef DEBUG
    {
	int y, x;
	for (y = 0; y < cr; y++) {
	    for (x = 0; x < cr; x++) {
		printf("%2.0d", grid[y*cr+x]);
	    }
	    printf("\n");
	}
	printf("\n");
    }
#endif

    /*
     * Now we have the grid as it will be presented to the user.
     * Encode it in a game seed.
     */
    {
	char *p;
	int run, i;

	seed = snewn(5 * area, char);
	p = seed;
	run = 0;
	for (i = 0; i <= area; i++) {
	    int n = (i < area ? grid[i] : -1);

	    if (!n)
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
		} else {
		    /*
		     * If there's a number in the very top left or
		     * bottom right, there's no point putting an
		     * unnecessary _ before or after it.
		     */
		    if (p > seed && n > 0)
			*p++ = '_';
		}
		if (n > 0)
		    p += sprintf(p, "%d", n);
		run = 0;
	    }
	}
	assert(p - seed < 5 * area);
	*p++ = '\0';
	seed = sresize(seed, p - seed, char);
    }

    sfree(grid);

    return seed;
}

static char *validate_seed(game_params *params, char *seed)
{
    int area = params->r * params->r * params->c * params->c;
    int squares = 0;

    while (*seed) {
        int n = *seed++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            squares++;
            while (*seed >= '0' && *seed <= '9')
                seed++;
        } else
            return "Invalid character in game specification";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";

    return NULL;
}

static game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);
    int c = params->c, r = params->r, cr = c*r, area = cr * cr;
    int i;

    state->c = params->c;
    state->r = params->r;

    state->grid = snewn(area, digit);
    state->immutable = snewn(area, unsigned char);
    memset(state->immutable, FALSE, area);

    state->completed = FALSE;

    i = 0;
    while (*seed) {
        int n = *seed++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                state->grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
	    state->immutable[i] = TRUE;
            state->grid[i++] = atoi(seed-1);
            while (*seed >= '0' && *seed <= '9')
                seed++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);
    int c = state->c, r = state->r, cr = c*r, area = cr * cr;

    ret->c = state->c;
    ret->r = state->r;

    ret->grid = snewn(area, digit);
    memcpy(ret->grid, state->grid, area);

    ret->immutable = snewn(area, unsigned char);
    memcpy(ret->immutable, state->immutable, area);

    ret->completed = state->completed;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->immutable);
    sfree(state->grid);
    sfree(state);
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, or -1,-1 if there isn't one. When there
     * is, pressing a valid number or letter key or Space will
     * enter that number or letter in the grid.
     */
    int hx, hy;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = -1;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static game_state *make_move(game_state *from, game_ui *ui, int x, int y,
			     int button)
{
    int c = from->c, r = from->r, cr = c*r;
    int tx, ty;
    game_state *ret;

    tx = (x - BORDER) / TILE_SIZE;
    ty = (y - BORDER) / TILE_SIZE;

    if (tx >= 0 && tx < cr && ty >= 0 && ty < cr && button == LEFT_BUTTON) {
	if (tx == ui->hx && ty == ui->hy) {
	    ui->hx = ui->hy = -1;
	} else {
	    ui->hx = tx;
	    ui->hy = ty;
	}
	return from;		       /* UI activity occurred */
    }

    if (ui->hx != -1 && ui->hy != -1 &&
	((button >= '1' && button <= '9' && button - '0' <= cr) ||
	 (button >= 'a' && button <= 'z' && button - 'a' + 10 <= cr) ||
	 (button >= 'A' && button <= 'Z' && button - 'A' + 10 <= cr) ||
	 button == ' ')) {
	int n = button - '0';
	if (button >= 'A' && button <= 'Z')
	    n = button - 'A' + 10;
	if (button >= 'a' && button <= 'z')
	    n = button - 'a' + 10;
	if (button == ' ')
	    n = 0;

	if (from->immutable[ui->hy*cr+ui->hx])
	    return NULL;	       /* can't overwrite this square */

	ret = dup_game(from);
	ret->grid[ui->hy*cr+ui->hx] = n;
	ui->hx = ui->hy = -1;

	/*
	 * We've made a real change to the grid. Check to see
	 * if the game has been completed.
	 */
	if (!ret->completed && check_valid(c, r, ret->grid)) {
	    ret->completed = TRUE;
	}

	return ret;		       /* made a valid move */
    }

    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct game_drawstate {
    int started;
    int c, r, cr;
    digit *grid;
    unsigned char *hl;
};

#define XSIZE(cr) ((cr) * TILE_SIZE + 2*BORDER + 1)
#define YSIZE(cr) ((cr) * TILE_SIZE + 2*BORDER + 1)

static void game_size(game_params *params, int *x, int *y)
{
    int c = params->c, r = params->r, cr = c*r;

    *x = XSIZE(cr);
    *y = YSIZE(cr);
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_CLUE * 3 + 0] = 0.0F;
    ret[COL_CLUE * 3 + 1] = 0.0F;
    ret[COL_CLUE * 3 + 2] = 0.0F;

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.85F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int c = state->c, r = state->r, cr = c*r;

    ds->started = FALSE;
    ds->c = c;
    ds->r = r;
    ds->cr = cr;
    ds->grid = snewn(cr*cr, digit);
    memset(ds->grid, 0, cr*cr);
    ds->hl = snewn(cr*cr, unsigned char);
    memset(ds->hl, 0, cr*cr);

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->hl);
    sfree(ds->grid);
    sfree(ds);
}

static void draw_number(frontend *fe, game_drawstate *ds, game_state *state,
			int x, int y, int hl)
{
    int c = state->c, r = state->r, cr = c*r;
    int tx, ty;
    int cx, cy, cw, ch;
    char str[2];

    if (ds->grid[y*cr+x] == state->grid[y*cr+x] && ds->hl[y*cr+x] == hl)
	return;			       /* no change required */

    tx = BORDER + x * TILE_SIZE + 2;
    ty = BORDER + y * TILE_SIZE + 2;

    cx = tx;
    cy = ty;
    cw = TILE_SIZE-3;
    ch = TILE_SIZE-3;

    if (x % r)
	cx--, cw++;
    if ((x+1) % r)
	cw++;
    if (y % c)
	cy--, ch++;
    if ((y+1) % c)
	ch++;

    clip(fe, cx, cy, cw, ch);

    /* background needs erasing? */
    if (ds->grid[y*cr+x] || ds->hl[y*cr+x] != hl)
	draw_rect(fe, cx, cy, cw, ch, hl ? COL_HIGHLIGHT : COL_BACKGROUND);

    /* new number needs drawing? */
    if (state->grid[y*cr+x]) {
	str[1] = '\0';
	str[0] = state->grid[y*cr+x] + '0';
	if (str[0] > '9')
	    str[0] += 'a' - ('9'+1);
	draw_text(fe, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
		  FONT_VARIABLE, TILE_SIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  state->immutable[y*cr+x] ? COL_CLUE : COL_USER, str);
    }

    unclip(fe);

    draw_update(fe, cx, cy, cw, ch);

    ds->grid[y*cr+x] = state->grid[y*cr+x];
    ds->hl[y*cr+x] = hl;
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int c = state->c, r = state->r, cr = c*r;
    int x, y;

    if (!ds->started) {
	/*
	 * The initial contents of the window are not guaranteed
	 * and can vary with front ends. To be on the safe side,
	 * all games should start by drawing a big
	 * background-colour rectangle covering the whole window.
	 */
	draw_rect(fe, 0, 0, XSIZE(cr), YSIZE(cr), COL_BACKGROUND);

	/*
	 * Draw the grid.
	 */
	for (x = 0; x <= cr; x++) {
	    int thick = (x % r ? 0 : 1);
	    draw_rect(fe, BORDER + x*TILE_SIZE - thick, BORDER-1,
		      1+2*thick, cr*TILE_SIZE+3, COL_GRID);
	}
	for (y = 0; y <= cr; y++) {
	    int thick = (y % c ? 0 : 1);
	    draw_rect(fe, BORDER-1, BORDER + y*TILE_SIZE - thick,
		      cr*TILE_SIZE+3, 1+2*thick, COL_GRID);
	}
    }

    /*
     * Draw any numbers which need redrawing.
     */
    for (x = 0; x < cr; x++) {
	for (y = 0; y < cr; y++) {
	    draw_number(fe, ds, state, x, y,
			(x == ui->hx && y == ui->hy) ||
			(flashtime > 0 &&
			 (flashtime <= FLASH_TIME/3 ||
			  flashtime >= FLASH_TIME*2/3)));
	}
    }

    /*
     * Update the _entire_ grid if necessary.
     */
    if (!ds->started) {
	draw_update(fe, 0, 0, XSIZE(cr), YSIZE(cr));
	ds->started = TRUE;
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir)
{
    if (!oldstate->completed && newstate->completed)
        return FLASH_TIME;
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

#ifdef COMBINED
#define thegame solo
#endif

const struct game thegame = {
    "Solo", "games.solo", TRUE,
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
