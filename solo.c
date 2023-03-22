/*
 * solo.c: the number-placing puzzle most popularly known as `Sudoku'.
 *
 * TODO:
 *
 *  - reports from users are that `Trivial'-mode puzzles are still
 *    rather hard compared to newspapers' easy ones, so some better
 *    low-end difficulty grading would be nice
 *     + it's possible that really easy puzzles always have
 *       _several_ things you can do, so don't make you hunt too
 *       hard for the one deduction you can currently make
 *     + it's also possible that easy puzzles require fewer
 *       cross-eliminations: perhaps there's a higher incidence of
 *       things you can deduce by looking only at (say) rows,
 *       rather than things you have to check both rows and columns
 *       for
 *     + but really, what I need to do is find some really easy
 *       puzzles and _play_ them, to see what's actually easy about
 *       them
 *     + while I'm revamping this area, filling in the _last_
 *       number in a nearly-full row or column should certainly be
 *       permitted even at the lowest difficulty level.
 *     + also Alex noticed that `Basic' grids requiring numeric
 *       elimination are actually very hard, so I wonder if a
 *       difficulty gradation between that and positional-
 *       elimination-only might be in order
 *     + but it's not good to have _too_ many difficulty levels, or
 *       it'll take too long to randomly generate a given level.
 * 
 *  - it might still be nice to do some prioritisation on the
 *    removal of numbers from the grid
 *     + one possibility is to try to minimise the maximum number
 * 	 of filled squares in any block, which in particular ought
 * 	 to enforce never leaving a completely filled block in the
 * 	 puzzle as presented.
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
 *     + then again, I don't actually like sudoku.com's interface;
 *       it's too much like a paint package whereas I prefer to
 *       think of Solo as a text editor.
 *     + another PDA-friendly possibility is a drag interface:
 *       _drag_ numbers from the palette into the grid squares.
 *       Thought experiments suggest I'd prefer that to the
 *       sudoku.com approach, but I haven't actually tried it.
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

#ifdef STANDALONE_SOLVER
#include <stdarg.h>
static int solver_show_working, solver_recurse_depth;
#endif

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

#define PREFERRED_TILE_SIZE 48
#define TILE_SIZE (ds->tilesize)
#define BORDER (TILE_SIZE / 2)
#define GRIDEXTRA max((TILE_SIZE / 32),1)

#define FLASH_TIME 0.4F

enum { SYMM_NONE, SYMM_ROT2, SYMM_ROT4, SYMM_REF2, SYMM_REF2D, SYMM_REF4,
       SYMM_REF4D, SYMM_REF8 };

enum { DIFF_BLOCK,
       DIFF_SIMPLE, DIFF_INTERSECT, DIFF_SET, DIFF_EXTREME, DIFF_RECURSIVE,
       DIFF_AMBIGUOUS, DIFF_IMPOSSIBLE };

enum { DIFF_KSINGLE, DIFF_KMINMAX, DIFF_KSUMS, DIFF_KINTERSECT };

enum {
    COL_BACKGROUND,
    COL_XDIAGONALS,
    COL_GRID,
    COL_CLUE,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    COL_KILLER,
    NCOLOURS
};

/*
 * To determine all possible ways to reach a given sum by adding two or
 * three numbers from 1..9, each of which occurs exactly once in the sum,
 * these arrays contain a list of bitmasks for each sum value, where if
 * bit N is set, it means that N occurs in the sum.  Each list is
 * terminated by a zero if it is shorter than the size of the array.
 */
#define MAX_2SUMS 5
#define MAX_3SUMS 8
#define MAX_4SUMS 12
static unsigned long sum_bits2[18][MAX_2SUMS];
static unsigned long sum_bits3[25][MAX_3SUMS];
static unsigned long sum_bits4[31][MAX_4SUMS];

static int find_sum_bits(unsigned long *array, int idx, int value_left,
			 int addends_left, int min_addend,
			 unsigned long bitmask_so_far)
{
    int i;
    assert(addends_left >= 2);

    for (i = min_addend; i < value_left; i++) {
	unsigned long new_bitmask = bitmask_so_far | (1L << i);
	assert(bitmask_so_far != new_bitmask);

	if (addends_left == 2) {
	    int j = value_left - i;
	    if (j <= i)
		break;
	    if (j > 9)
		continue;
	    array[idx++] = new_bitmask | (1L << j);
	} else
	    idx = find_sum_bits(array, idx, value_left - i,
				addends_left - 1, i + 1,
				new_bitmask);
    }
    return idx;
}

static void precompute_sum_bits(void)
{
    int i;
    for (i = 3; i < 31; i++) {
	int j;
	if (i < 18) {
	    j = find_sum_bits(sum_bits2[i], 0, i, 2, 1, 0);
	    assert (j <= MAX_2SUMS);
	    if (j < MAX_2SUMS)
		sum_bits2[i][j] = 0;
	}
	if (i < 25) {
	    j = find_sum_bits(sum_bits3[i], 0, i, 3, 1, 0);
	    assert (j <= MAX_3SUMS);
	    if (j < MAX_3SUMS)
		sum_bits3[i][j] = 0;
	}
	j = find_sum_bits(sum_bits4[i], 0, i, 4, 1, 0);
	assert (j <= MAX_4SUMS);
	if (j < MAX_4SUMS)
	    sum_bits4[i][j] = 0;
    }
}

struct game_params {
    /*
     * For a square puzzle, `c' and `r' indicate the puzzle
     * parameters as described above.
     * 
     * A jigsaw-style puzzle is indicated by r==1, in which case c
     * can be whatever it likes (there is no constraint on
     * compositeness - a 7x7 jigsaw sudoku makes perfect sense).
     */
    int c, r, symm, diff, kdiff;
    bool xtype;                /* require all digits in X-diagonals */
    bool killer;
};

struct block_structure {
    int refcount;

    /*
     * For text formatting, we do need c and r here.
     */
    int c, r, area;

    /*
     * For any square index, whichblock[i] gives its block index.
     *
     * For 0 <= b,i < cr, blocks[b][i] gives the index of the ith
     * square in block b.  nr_squares[b] gives the number of squares
     * in block b (also the number of valid elements in blocks[b]).
     *
     * blocks_data holds the data pointed to by blocks.
     *
     * nr_squares may be NULL for block structures where all blocks are
     * the same size.
     */
    int *whichblock, **blocks, *nr_squares, *blocks_data;
    int nr_blocks, max_nr_squares;

#ifdef STANDALONE_SOLVER
    /*
     * Textual descriptions of each block. For normal Sudoku these
     * are of the form "(1,3)"; for jigsaw they are "starting at
     * (5,7)". So the sensible usage in both cases is to say
     * "elimination within block %s" with one of these strings.
     * 
     * Only blocknames itself needs individually freeing; it's all
     * one block.
     */
    char **blocknames;
#endif
};

struct game_state {
    /*
     * For historical reasons, I use `cr' to denote the overall
     * width/height of the puzzle. It was a natural notation when
     * all puzzles were divided into blocks in a grid, but doesn't
     * really make much sense given jigsaw puzzles. However, the
     * obvious `n' is heavily used in the solver to describe the
     * index of a number being placed, so `cr' will have to stay.
     */
    int cr;
    struct block_structure *blocks;
    struct block_structure *kblocks;   /* Blocks for killer puzzles.  */
    bool xtype, killer;
    digit *grid, *kgrid;
    bool *pencil;                      /* c*r*c*r elements */
    bool *immutable;                   /* marks which digits are clues */
    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->c = ret->r = 3;
    ret->xtype = false;
    ret->killer = false;
    ret->symm = SYMM_ROT2;	       /* a plausible default */
    ret->diff = DIFF_BLOCK;	       /* so is this */
    ret->kdiff = DIFF_KINTERSECT;      /* so is this */

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

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    static struct {
        const char *title;
        game_params params;
    } const presets[] = {
        { "2x2 Trivial", { 2, 2, SYMM_ROT2, DIFF_BLOCK, DIFF_KMINMAX, false, false } },
        { "2x3 Basic", { 2, 3, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, false, false } },
        { "3x3 Trivial", { 3, 3, SYMM_ROT2, DIFF_BLOCK, DIFF_KMINMAX, false, false } },
        { "3x3 Basic", { 3, 3, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, false, false } },
        { "3x3 Basic X", { 3, 3, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, true } },
        { "3x3 Intermediate", { 3, 3, SYMM_ROT2, DIFF_INTERSECT, DIFF_KMINMAX, false, false } },
        { "3x3 Advanced", { 3, 3, SYMM_ROT2, DIFF_SET, DIFF_KMINMAX, false, false } },
        { "3x3 Advanced X", { 3, 3, SYMM_ROT2, DIFF_SET, DIFF_KMINMAX, true } },
        { "3x3 Extreme", { 3, 3, SYMM_ROT2, DIFF_EXTREME, DIFF_KMINMAX, false, false } },
        { "3x3 Unreasonable", { 3, 3, SYMM_ROT2, DIFF_RECURSIVE, DIFF_KMINMAX, false, false } },
        { "3x3 Killer", { 3, 3, SYMM_NONE, DIFF_BLOCK, DIFF_KINTERSECT, false, true } },
        { "9 Jigsaw Basic", { 9, 1, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, false, false } },
        { "9 Jigsaw Basic X", { 9, 1, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, true } },
        { "9 Jigsaw Advanced", { 9, 1, SYMM_ROT2, DIFF_SET, DIFF_KMINMAX, false, false } },
#ifndef SLOW_SYSTEM
        { "3x4 Basic", { 3, 4, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, false, false } },
        { "4x4 Basic", { 4, 4, SYMM_ROT2, DIFF_SIMPLE, DIFF_KMINMAX, false, false } },
#endif
    };

    if (i < 0 || i >= lenof(presets))
        return false;

    *name = dupstr(presets[i].title);
    *params = dup_params(&presets[i].params);

    return true;
}

static void decode_params(game_params *ret, char const *string)
{
    bool seen_r = false;

    ret->c = ret->r = atoi(string);
    ret->xtype = false;
    ret->killer = false;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->r = atoi(string);
	seen_r = true;
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    while (*string) {
        if (*string == 'j') {
	    string++;
	    if (seen_r)
		ret->c *= ret->r;
	    ret->r = 1;
	} else if (*string == 'x') {
	    string++;
	    ret->xtype = true;
	} else if (*string == 'k') {
	    string++;
	    ret->killer = true;
	} else if (*string == 'r' || *string == 'm' || *string == 'a') {
            int sn, sc;
            bool sd;
            sc = *string++;
            if (sc == 'm' && *string == 'd') {
                sd = true;
                string++;
            } else {
                sd = false;
            }
            sn = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
            if (sc == 'm' && sn == 8)
                ret->symm = SYMM_REF8;
            if (sc == 'm' && sn == 4)
                ret->symm = sd ? SYMM_REF4D : SYMM_REF4;
            if (sc == 'm' && sn == 2)
                ret->symm = sd ? SYMM_REF2D : SYMM_REF2;
            if (sc == 'r' && sn == 4)
                ret->symm = SYMM_ROT4;
            if (sc == 'r' && sn == 2)
                ret->symm = SYMM_ROT2;
            if (sc == 'a')
                ret->symm = SYMM_NONE;
        } else if (*string == 'd') {
            string++;
            if (*string == 't')        /* trivial */
                string++, ret->diff = DIFF_BLOCK;
            else if (*string == 'b')   /* basic */
                string++, ret->diff = DIFF_SIMPLE;
            else if (*string == 'i')   /* intermediate */
                string++, ret->diff = DIFF_INTERSECT;
            else if (*string == 'a')   /* advanced */
                string++, ret->diff = DIFF_SET;
            else if (*string == 'e')   /* extreme */
                string++, ret->diff = DIFF_EXTREME;
            else if (*string == 'u')   /* unreasonable */
                string++, ret->diff = DIFF_RECURSIVE;
        } else
            string++;                  /* eat unknown character */
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char str[80];

    if (params->r > 1)
	sprintf(str, "%dx%d", params->c, params->r);
    else
	sprintf(str, "%dj", params->c);
    if (params->xtype)
	strcat(str, "x");
    if (params->killer)
	strcat(str, "k");

    if (full) {
        switch (params->symm) {
          case SYMM_REF8: strcat(str, "m8"); break;
          case SYMM_REF4: strcat(str, "m4"); break;
          case SYMM_REF4D: strcat(str, "md4"); break;
          case SYMM_REF2: strcat(str, "m2"); break;
          case SYMM_REF2D: strcat(str, "md2"); break;
          case SYMM_ROT4: strcat(str, "r4"); break;
          /* case SYMM_ROT2: strcat(str, "r2"); break; [default] */
          case SYMM_NONE: strcat(str, "a"); break;
        }
        switch (params->diff) {
          /* case DIFF_BLOCK: strcat(str, "dt"); break; [default] */
          case DIFF_SIMPLE: strcat(str, "db"); break;
          case DIFF_INTERSECT: strcat(str, "di"); break;
          case DIFF_SET: strcat(str, "da"); break;
          case DIFF_EXTREME: strcat(str, "de"); break;
          case DIFF_RECURSIVE: strcat(str, "du"); break;
        }
    }
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(8, config_item);

    ret[0].name = "Columns of sub-blocks";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->c);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Rows of sub-blocks";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->r);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "\"X\" (require every number in each main diagonal)";
    ret[2].type = C_BOOLEAN;
    ret[2].u.boolean.bval = params->xtype;

    ret[3].name = "Jigsaw (irregularly shaped sub-blocks)";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = (params->r == 1);

    ret[4].name = "Killer (digit sums)";
    ret[4].type = C_BOOLEAN;
    ret[4].u.boolean.bval = params->killer;

    ret[5].name = "Symmetry";
    ret[5].type = C_CHOICES;
    ret[5].u.choices.choicenames = ":None:2-way rotation:4-way rotation:2-way mirror:"
        "2-way diagonal mirror:4-way mirror:4-way diagonal mirror:"
        "8-way mirror";
    ret[5].u.choices.selected = params->symm;

    ret[6].name = "Difficulty";
    ret[6].type = C_CHOICES;
    ret[6].u.choices.choicenames = ":Trivial:Basic:Intermediate:Advanced:Extreme:Unreasonable";
    ret[6].u.choices.selected = params->diff;

    ret[7].name = NULL;
    ret[7].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->c = atoi(cfg[0].u.string.sval);
    ret->r = atoi(cfg[1].u.string.sval);
    ret->xtype = cfg[2].u.boolean.bval;
    if (cfg[3].u.boolean.bval) {
	ret->c *= ret->r;
	ret->r = 1;
    }
    ret->killer = cfg[4].u.boolean.bval;
    ret->symm = cfg[5].u.choices.selected;
    ret->diff = cfg[6].u.choices.selected;
    ret->kdiff = DIFF_KINTERSECT;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->c < 2)
	return "Both dimensions must be at least 2";
    if (params->c > ORDER_MAX || params->r > ORDER_MAX)
	return "Dimensions greater than "STR(ORDER_MAX)" are not supported";
    if ((params->c * params->r) > 31)
        return "Unable to support more than 31 distinct symbols in a puzzle";
    if (params->killer && params->c * params->r > 9)
        return "Killer puzzle dimensions must be smaller than 10";
    if (params->xtype && params->c * params->r < 4)
        return "X-type puzzle dimensions must be larger than 3";
    return NULL;
}

/*
 * ----------------------------------------------------------------------
 * Block structure functions.
 */

static struct block_structure *alloc_block_structure(int c, int r, int area,
						     int max_nr_squares,
						     int nr_blocks)
{
    int i;
    struct block_structure *b = snew(struct block_structure);

    b->refcount = 1;
    b->nr_blocks = nr_blocks;
    b->max_nr_squares = max_nr_squares;
    b->c = c; b->r = r; b->area = area;
    b->whichblock = snewn(area, int);
    b->blocks_data = snewn(nr_blocks * max_nr_squares, int);
    b->blocks = snewn(nr_blocks, int *);
    b->nr_squares = snewn(nr_blocks, int);

    for (i = 0; i < nr_blocks; i++)
	b->blocks[i] = b->blocks_data + i*max_nr_squares;

#ifdef STANDALONE_SOLVER
    b->blocknames = (char **)smalloc(c*r*(sizeof(char *)+80));
    for (i = 0; i < c * r; i++)
	b->blocknames[i] = NULL;
#endif
    return b;
}

static void free_block_structure(struct block_structure *b)
{
    if (--b->refcount == 0) {
	sfree(b->whichblock);
	sfree(b->blocks);
	sfree(b->blocks_data);
#ifdef STANDALONE_SOLVER
	sfree(b->blocknames);
#endif
	sfree(b->nr_squares);
	sfree(b);
    }
}

static struct block_structure *dup_block_structure(struct block_structure *b)
{
    struct block_structure *nb;
    int i;

    nb = alloc_block_structure(b->c, b->r, b->area, b->max_nr_squares,
			       b->nr_blocks);
    memcpy(nb->nr_squares, b->nr_squares, b->nr_blocks * sizeof *b->nr_squares);
    memcpy(nb->whichblock, b->whichblock, b->area * sizeof *b->whichblock);
    memcpy(nb->blocks_data, b->blocks_data,
	   b->nr_blocks * b->max_nr_squares * sizeof *b->blocks_data);
    for (i = 0; i < b->nr_blocks; i++)
	nb->blocks[i] = nb->blocks_data + i*nb->max_nr_squares;

#ifdef STANDALONE_SOLVER
    memcpy(nb->blocknames, b->blocknames, b->c * b->r *(sizeof(char *)+80));
    {
	int i;
	for (i = 0; i < b->c * b->r; i++)
	    if (b->blocknames[i] == NULL)
		nb->blocknames[i] = NULL;
	    else
		nb->blocknames[i] = ((char *)nb->blocknames) + (b->blocknames[i] - (char *)b->blocknames);
    }
#endif
    return nb;
}

static void split_block(struct block_structure *b, int *squares, int nr_squares)
{
    int i, j;
    int previous_block = b->whichblock[squares[0]];
    int newblock = b->nr_blocks;

    assert(b->max_nr_squares >= nr_squares);
    assert(b->nr_squares[previous_block] > nr_squares);

    b->nr_blocks++;
    b->blocks_data = sresize(b->blocks_data,
			     b->nr_blocks * b->max_nr_squares, int);
    b->nr_squares = sresize(b->nr_squares, b->nr_blocks, int);
    sfree(b->blocks);
    b->blocks = snewn(b->nr_blocks, int *);
    for (i = 0; i < b->nr_blocks; i++)
	b->blocks[i] = b->blocks_data + i*b->max_nr_squares;
    for (i = 0; i < nr_squares; i++) {
	assert(b->whichblock[squares[i]] == previous_block);
	b->whichblock[squares[i]] = newblock;
	b->blocks[newblock][i] = squares[i];
    }
    for (i = j = 0; i < b->nr_squares[previous_block]; i++) {
	int k;
	int sq = b->blocks[previous_block][i];
	for (k = 0; k < nr_squares; k++)
	    if (squares[k] == sq)
		break;
	if (k == nr_squares)
	    b->blocks[previous_block][j++] = sq;
    }
    b->nr_squares[previous_block] -= nr_squares;
    b->nr_squares[newblock] = nr_squares;
}

static void remove_from_block(struct block_structure *blocks, int b, int n)
{
    int i, j;
    blocks->whichblock[n] = -1;
    for (i = j = 0; i < blocks->nr_squares[b]; i++)
	if (blocks->blocks[b][i] != n)
	    blocks->blocks[b][j++] = blocks->blocks[b][i];
    assert(j+1 == i);
    blocks->nr_squares[b]--;
}

/* ----------------------------------------------------------------------
 * Solver.
 * 
 * This solver is used for two purposes:
 *  + to check solubility of a grid as we gradually remove numbers
 *    from it
 *  + to solve an externally generated puzzle when the user selects
 *    `Solve'.
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
 *  - Killer minmax elimination: for killer-type puzzles, a number
 *    is impossible if choosing it would cause the sum in a killer
 *    region to be guaranteed to be too large or too small.
 *
 *  - Numeric elimination: a square must have a particular number
 *    in because all the other numbers that could go in it are
 *    ruled out.
 *
 *  - Intersectional analysis: given two domains which overlap
 *    (hence one must be a block, and the other can be a row or
 *    col), if the possible locations for a particular number in
 *    one of the domains can be narrowed down to the overlap, then
 *    that number can be ruled out everywhere but the overlap in
 *    the other domain too.
 *
 *  - Set elimination: if there is a subset of the empty squares
 *    within a domain such that the union of the possible numbers
 *    in that subset has the same size as the subset itself, then
 *    those numbers can be ruled out everywhere else in the domain.
 *    (For example, if there are five empty squares and the
 *    possible numbers in each are 12, 23, 13, 134 and 1345, then
 *    the first three empty squares form such a subset: the numbers
 *    1, 2 and 3 _must_ be in those three squares in some
 *    permutation, and hence we can deduce none of them can be in
 *    the fourth or fifth squares.)
 *     + You can also see this the other way round, concentrating
 *       on numbers rather than squares: if there is a subset of
 *       the unplaced numbers within a domain such that the union
 *       of all their possible positions has the same size as the
 *       subset itself, then all other numbers can be ruled out for
 *       those positions. However, it turns out that this is
 *       exactly equivalent to the first formulation at all times:
 *       there is a 1-1 correspondence between suitable subsets of
 *       the unplaced numbers and suitable subsets of the unfilled
 *       places, found by taking the _complement_ of the union of
 *       the numbers' possible positions (or the spaces' possible
 *       contents).
 * 
 *  - Forcing chains (see comment for solver_forcing().)
 * 
 *  - Recursion. If all else fails, we pick one of the currently
 *    most constrained empty squares and take a random guess at its
 *    contents, then continue solving on that basis and see if we
 *    get any further.
 */

struct solver_usage {
    int cr;
    struct block_structure *blocks, *kblocks, *extra_cages;
    /*
     * We set up a cubic array, indexed by x, y and digit; each
     * element of this array is true or false according to whether
     * or not that digit _could_ in principle go in that position.
     *
     * The way to index this array is cube[(y*cr+x)*cr+n-1]; there
     * are macros below to help with this.
     */
    bool *cube;
    /*
     * This is the grid in which we write down our final
     * deductions. y-coordinates in here are _not_ transformed.
     */
    digit *grid;
    /*
     * For killer-type puzzles, kclues holds the secondary clue for
     * each cage.  For derived cages, the clue is in extra_clues.
     */
    digit *kclues, *extra_clues;
    /*
     * Now we keep track, at a slightly higher level, of what we
     * have yet to work out, to prevent doing the same deduction
     * many times.
     */
    /* row[y*cr+n-1] true if digit n has been placed in row y */
    bool *row;
    /* col[x*cr+n-1] true if digit n has been placed in row x */
    bool *col;
    /* blk[i*cr+n-1] true if digit n has been placed in block i */
    bool *blk;
    /* diag[i*cr+n-1] true if digit n has been placed in diagonal i */
    bool *diag;                        /* diag 0 is \, 1 is / */

    int *regions;
    int nr_regions;
    int **sq2region;
};
#define cubepos2(xy,n) ((xy)*usage->cr+(n)-1)
#define cubepos(x,y,n) cubepos2((y)*usage->cr+(x),n)
#define cube(x,y,n) (usage->cube[cubepos(x,y,n)])
#define cube2(xy,n) (usage->cube[cubepos2(xy,n)])

#define ondiag0(xy) ((xy) % (cr+1) == 0)
#define ondiag1(xy) ((xy) % (cr-1) == 0 && (xy) > 0 && (xy) < cr*cr-1)
#define diag0(i) ((i) * (cr+1))
#define diag1(i) ((i+1) * (cr-1))

/*
 * Function called when we are certain that a particular square has
 * a particular number in it. The y-coordinate passed in here is
 * transformed.
 */
static void solver_place(struct solver_usage *usage, int x, int y, int n)
{
    int cr = usage->cr;
    int sqindex = y*cr+x;
    int i, bi;

    assert(cube(x,y,n));

    /*
     * Rule out all other numbers in this square.
     */
    for (i = 1; i <= cr; i++)
	if (i != n)
	    cube(x,y,i) = false;

    /*
     * Rule out this number in all other positions in the row.
     */
    for (i = 0; i < cr; i++)
	if (i != y)
	    cube(x,i,n) = false;

    /*
     * Rule out this number in all other positions in the column.
     */
    for (i = 0; i < cr; i++)
	if (i != x)
	    cube(i,y,n) = false;

    /*
     * Rule out this number in all other positions in the block.
     */
    bi = usage->blocks->whichblock[sqindex];
    for (i = 0; i < cr; i++) {
	int bp = usage->blocks->blocks[bi][i];
	if (bp != sqindex)
	    cube2(bp,n) = false;
    }

    /*
     * Enter the number in the result grid.
     */
    usage->grid[sqindex] = n;

    /*
     * Cross out this number from the list of numbers left to place
     * in its row, its column and its block.
     */
    usage->row[y*cr+n-1] = usage->col[x*cr+n-1] =
	usage->blk[bi*cr+n-1] = true;

    if (usage->diag) {
	if (ondiag0(sqindex)) {
	    for (i = 0; i < cr; i++)
		if (diag0(i) != sqindex)
		    cube2(diag0(i),n) = false;
	    usage->diag[n-1] = true;
	}
	if (ondiag1(sqindex)) {
	    for (i = 0; i < cr; i++)
		if (diag1(i) != sqindex)
		    cube2(diag1(i),n) = false;
	    usage->diag[cr+n-1] = true;
	}
    }
}

#if defined STANDALONE_SOLVER && defined __GNUC__
/*
 * Forward-declare the functions taking printf-like format arguments
 * with __attribute__((format)) so as to ensure the argument syntax
 * gets debugged.
 */
struct solver_scratch;
static int solver_elim(struct solver_usage *usage, int *indices,
                       const char *fmt, ...)
    __attribute__((format(printf,3,4)));
static int solver_intersect(struct solver_usage *usage,
                            int *indices1, int *indices2, const char *fmt, ...)
    __attribute__((format(printf,4,5)));
static int solver_set(struct solver_usage *usage,
                      struct solver_scratch *scratch,
                      int *indices, const char *fmt, ...)
    __attribute__((format(printf,4,5)));
#endif

static int solver_elim(struct solver_usage *usage, int *indices
#ifdef STANDALONE_SOLVER
                       , const char *fmt, ...
#endif
                       )
{
    int cr = usage->cr;
    int fpos, m, i;

    /*
     * Count the number of set bits within this section of the
     * cube.
     */
    m = 0;
    fpos = -1;
    for (i = 0; i < cr; i++)
	if (usage->cube[indices[i]]) {
	    fpos = indices[i];
	    m++;
	}

    if (m == 1) {
	int x, y, n;
	assert(fpos >= 0);

	n = 1 + fpos % cr;
	x = fpos / cr;
	y = x / cr;
	x %= cr;

        if (!usage->grid[y*cr+x]) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working) {
                va_list ap;
		printf("%*s", solver_recurse_depth*4, "");
                va_start(ap, fmt);
                vprintf(fmt, ap);
                va_end(ap);
                printf(":\n%*s  placing %d at (%d,%d)\n",
                       solver_recurse_depth*4, "", n, 1+x, 1+y);
            }
#endif
            solver_place(usage, x, y, n);
            return +1;
        }
    } else if (m == 0) {
#ifdef STANDALONE_SOLVER
	if (solver_show_working) {
	    va_list ap;
	    printf("%*s", solver_recurse_depth*4, "");
	    va_start(ap, fmt);
	    vprintf(fmt, ap);
	    va_end(ap);
	    printf(":\n%*s  no possibilities available\n",
		   solver_recurse_depth*4, "");
	}
#endif
        return -1;
    }

    return 0;
}

static int solver_intersect(struct solver_usage *usage,
                            int *indices1, int *indices2
#ifdef STANDALONE_SOLVER
                            , const char *fmt, ...
#endif
                            )
{
    int cr = usage->cr;
    int ret, i, j;

    /*
     * Loop over the first domain and see if there's any set bit
     * not also in the second.
     */
    for (i = j = 0; i < cr; i++) {
        int p = indices1[i];
	while (j < cr && indices2[j] < p)
	    j++;
        if (usage->cube[p]) {
	    if (j < cr && indices2[j] == p)
		continue;	       /* both domains contain this index */
	    else
		return 0;	       /* there is, so we can't deduce */
	}
    }

    /*
     * We have determined that all set bits in the first domain are
     * within its overlap with the second. So loop over the second
     * domain and remove all set bits that aren't also in that
     * overlap; return +1 iff we actually _did_ anything.
     */
    ret = 0;
    for (i = j = 0; i < cr; i++) {
        int p = indices2[i];
	while (j < cr && indices1[j] < p)
	    j++;
        if (usage->cube[p] && (j >= cr || indices1[j] != p)) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working) {
                int px, py, pn;

                if (!ret) {
                    va_list ap;
		    printf("%*s", solver_recurse_depth*4, "");
                    va_start(ap, fmt);
                    vprintf(fmt, ap);
                    va_end(ap);
                    printf(":\n");
                }

                pn = 1 + p % cr;
                px = p / cr;
                py = px / cr;
                px %= cr;

                printf("%*s  ruling out %d at (%d,%d)\n",
                       solver_recurse_depth*4, "", pn, 1+px, 1+py);
            }
#endif
            ret = +1;		       /* we did something */
            usage->cube[p] = false;
        }
    }

    return ret;
}

struct solver_scratch {
    unsigned char *grid, *rowidx, *colidx, *set;
    int *neighbours, *bfsqueue;
    int *indexlist, *indexlist2;
#ifdef STANDALONE_SOLVER
    int *bfsprev;
#endif
};

static int solver_set(struct solver_usage *usage,
                      struct solver_scratch *scratch,
                      int *indices
#ifdef STANDALONE_SOLVER
                      , const char *fmt, ...
#endif
                      )
{
    int cr = usage->cr;
    int i, j, n, count;
    unsigned char *grid = scratch->grid;
    unsigned char *rowidx = scratch->rowidx;
    unsigned char *colidx = scratch->colidx;
    unsigned char *set = scratch->set;

    /*
     * We are passed a cr-by-cr matrix of booleans. Our first job
     * is to winnow it by finding any definite placements - i.e.
     * any row with a solitary 1 - and discarding that row and the
     * column containing the 1.
     */
    memset(rowidx, 1, cr);
    memset(colidx, 1, cr);
    for (i = 0; i < cr; i++) {
        int count = 0, first = -1;
        for (j = 0; j < cr; j++)
            if (usage->cube[indices[i*cr+j]])
                first = j, count++;

	/*
	 * If count == 0, then there's a row with no 1s at all and
	 * the puzzle is internally inconsistent.
	 */
        if (count == 0) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working) {
                va_list ap;
                printf("%*s", solver_recurse_depth*4,
                       "");
                va_start(ap, fmt);
                vprintf(fmt, ap);
                va_end(ap);
                printf(":\n%*s  solver_set: impossible on entry\n",
                       solver_recurse_depth*4, "");
            }
#endif
            return -1;
        }
        if (count == 1)
            rowidx[i] = colidx[first] = 0;
    }

    /*
     * Convert each of rowidx/colidx from a list of 0s and 1s to a
     * list of the indices of the 1s.
     */
    for (i = j = 0; i < cr; i++)
        if (rowidx[i])
            rowidx[j++] = i;
    n = j;
    for (i = j = 0; i < cr; i++)
        if (colidx[i])
            colidx[j++] = i;
    assert(n == j);

    /*
     * And create the smaller matrix.
     */
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            grid[i*cr+j] = usage->cube[indices[rowidx[i]*cr+colidx[j]]];

    /*
     * Having done that, we now have a matrix in which every row
     * has at least two 1s in. Now we search to see if we can find
     * a rectangle of zeroes (in the set-theoretic sense of
     * `rectangle', i.e. a subset of rows crossed with a subset of
     * columns) whose width and height add up to n.
     */

    memset(set, 0, n);
    count = 0;
    while (1) {
        /*
         * We have a candidate set. If its size is <=1 or >=n-1
         * then we move on immediately.
         */
        if (count > 1 && count < n-1) {
            /*
             * The number of rows we need is n-count. See if we can
             * find that many rows which each have a zero in all
             * the positions listed in `set'.
             */
            int rows = 0;
            for (i = 0; i < n; i++) {
                bool ok = true;
                for (j = 0; j < n; j++)
                    if (set[j] && grid[i*cr+j]) {
                        ok = false;
                        break;
                    }
                if (ok)
                    rows++;
            }

            /*
             * We expect never to be able to get _more_ than
             * n-count suitable rows: this would imply that (for
             * example) there are four numbers which between them
             * have at most three possible positions, and hence it
             * indicates a faulty deduction before this point or
             * even a bogus clue.
             */
            if (rows > n - count) {
#ifdef STANDALONE_SOLVER
		if (solver_show_working) {
		    va_list ap;
		    printf("%*s", solver_recurse_depth*4,
			   "");
		    va_start(ap, fmt);
		    vprintf(fmt, ap);
		    va_end(ap);
		    printf(":\n%*s  contradiction reached\n",
			   solver_recurse_depth*4, "");
		}
#endif
		return -1;
	    }

            if (rows >= n - count) {
                bool progress = false;

                /*
                 * We've got one! Now, for each row which _doesn't_
                 * satisfy the criterion, eliminate all its set
                 * bits in the positions _not_ listed in `set'.
                 * Return +1 (meaning progress has been made) if we
                 * successfully eliminated anything at all.
                 * 
                 * This involves referring back through
                 * rowidx/colidx in order to work out which actual
                 * positions in the cube to meddle with.
                 */
                for (i = 0; i < n; i++) {
                    bool ok = true;
                    for (j = 0; j < n; j++)
                        if (set[j] && grid[i*cr+j]) {
                            ok = false;
                            break;
                        }
                    if (!ok) {
                        for (j = 0; j < n; j++)
                            if (!set[j] && grid[i*cr+j]) {
                                int fpos = indices[rowidx[i]*cr+colidx[j]];
#ifdef STANDALONE_SOLVER
                                if (solver_show_working) {
                                    int px, py, pn;

                                    if (!progress) {
                                        va_list ap;
					printf("%*s", solver_recurse_depth*4,
					       "");
                                        va_start(ap, fmt);
                                        vprintf(fmt, ap);
                                        va_end(ap);
                                        printf(":\n");
                                    }

                                    pn = 1 + fpos % cr;
                                    px = fpos / cr;
                                    py = px / cr;
                                    px %= cr;

                                    printf("%*s  ruling out %d at (%d,%d)\n",
					   solver_recurse_depth*4, "",
                                           pn, 1+px, 1+py);
                                }
#endif
                                progress = true;
                                usage->cube[fpos] = false;
                            }
                    }
                }

                if (progress) {
                    return +1;
                }
            }
        }

        /*
         * Binary increment: change the rightmost 0 to a 1, and
         * change all 1s to the right of it to 0s.
         */
        i = n;
        while (i > 0 && set[i-1])
            set[--i] = 0, count--;
        if (i > 0)
            set[--i] = 1, count++;
        else
            break;                     /* done */
    }

    return 0;
}

/*
 * Look for forcing chains. A forcing chain is a path of
 * pairwise-exclusive squares (i.e. each pair of adjacent squares
 * in the path are in the same row, column or block) with the
 * following properties:
 *
 *  (a) Each square on the path has precisely two possible numbers.
 *
 *  (b) Each pair of squares which are adjacent on the path share
 * 	at least one possible number in common.
 *
 *  (c) Each square in the middle of the path shares _both_ of its
 * 	numbers with at least one of its neighbours (not the same
 * 	one with both neighbours).
 *
 * These together imply that at least one of the possible number
 * choices at one end of the path forces _all_ the rest of the
 * numbers along the path. In order to make real use of this, we
 * need further properties:
 *
 *  (c) Ruling out some number N from the square at one end of the
 * 	path forces the square at the other end to take the same
 * 	number N.
 *
 *  (d) The two end squares are both in line with some third
 * 	square.
 *
 *  (e) That third square currently has N as a possibility.
 *
 * If we can find all of that lot, we can deduce that at least one
 * of the two ends of the forcing chain has number N, and that
 * therefore the mutually adjacent third square does not.
 *
 * To find forcing chains, we're going to start a bfs at each
 * suitable square, once for each of its two possible numbers.
 */
static int solver_forcing(struct solver_usage *usage,
                          struct solver_scratch *scratch)
{
    int cr = usage->cr;
    int *bfsqueue = scratch->bfsqueue;
#ifdef STANDALONE_SOLVER
    int *bfsprev = scratch->bfsprev;
#endif
    unsigned char *number = scratch->grid;
    int *neighbours = scratch->neighbours;
    int x, y;

    for (y = 0; y < cr; y++)
        for (x = 0; x < cr; x++) {
            int count, t, n;

            /*
             * If this square doesn't have exactly two candidate
             * numbers, don't try it.
             * 
             * In this loop we also sum the candidate numbers,
             * which is a nasty hack to allow us to quickly find
             * `the other one' (since we will shortly know there
             * are exactly two).
             */
            for (count = t = 0, n = 1; n <= cr; n++)
                if (cube(x, y, n))
                    count++, t += n;
            if (count != 2)
                continue;

            /*
             * Now attempt a bfs for each candidate.
             */
            for (n = 1; n <= cr; n++)
                if (cube(x, y, n)) {
                    int orign, currn, head, tail;

                    /*
                     * Begin a bfs.
                     */
                    orign = n;

                    memset(number, cr+1, cr*cr);
                    head = tail = 0;
                    bfsqueue[tail++] = y*cr+x;
#ifdef STANDALONE_SOLVER
                    bfsprev[y*cr+x] = -1;
#endif
                    number[y*cr+x] = t - n;

                    while (head < tail) {
                        int xx, yy, nneighbours, xt, yt, i;

                        xx = bfsqueue[head++];
                        yy = xx / cr;
                        xx %= cr;

                        currn = number[yy*cr+xx];

                        /*
                         * Find neighbours of yy,xx.
                         */
                        nneighbours = 0;
                        for (yt = 0; yt < cr; yt++)
                            neighbours[nneighbours++] = yt*cr+xx;
                        for (xt = 0; xt < cr; xt++)
                            neighbours[nneighbours++] = yy*cr+xt;
                        xt = usage->blocks->whichblock[yy*cr+xx];
                        for (yt = 0; yt < cr; yt++)
			    neighbours[nneighbours++] = usage->blocks->blocks[xt][yt];
			if (usage->diag) {
			    int sqindex = yy*cr+xx;
			    if (ondiag0(sqindex)) {
				for (i = 0; i < cr; i++)
				    neighbours[nneighbours++] = diag0(i);
			    }
			    if (ondiag1(sqindex)) {
				for (i = 0; i < cr; i++)
				    neighbours[nneighbours++] = diag1(i);
			    }
			}

                        /*
                         * Try visiting each of those neighbours.
                         */
                        for (i = 0; i < nneighbours; i++) {
                            int cc, tt, nn;

                            xt = neighbours[i] % cr;
                            yt = neighbours[i] / cr;

                            /*
                             * We need this square to not be
                             * already visited, and to include
                             * currn as a possible number.
                             */
                            if (number[yt*cr+xt] <= cr)
                                continue;
                            if (!cube(xt, yt, currn))
                                continue;

                            /*
                             * Don't visit _this_ square a second
                             * time!
                             */
                            if (xt == xx && yt == yy)
                                continue;

                            /*
                             * To continue with the bfs, we need
                             * this square to have exactly two
                             * possible numbers.
                             */
                            for (cc = tt = 0, nn = 1; nn <= cr; nn++)
                                if (cube(xt, yt, nn))
                                    cc++, tt += nn;
                            if (cc == 2) {
                                bfsqueue[tail++] = yt*cr+xt;
#ifdef STANDALONE_SOLVER
                                bfsprev[yt*cr+xt] = yy*cr+xx;
#endif
                                number[yt*cr+xt] = tt - currn;
                            }

                            /*
                             * One other possibility is that this
                             * might be the square in which we can
                             * make a real deduction: if it's
                             * adjacent to x,y, and currn is equal
                             * to the original number we ruled out.
                             */
                            if (currn == orign &&
                                (xt == x || yt == y ||
                                 (usage->blocks->whichblock[yt*cr+xt] == usage->blocks->whichblock[y*cr+x]) ||
				 (usage->diag && ((ondiag0(yt*cr+xt) && ondiag0(y*cr+x)) ||
						  (ondiag1(yt*cr+xt) && ondiag1(y*cr+x)))))) {
#ifdef STANDALONE_SOLVER
                                if (solver_show_working) {
                                    const char *sep = "";
                                    int xl, yl;
                                    printf("%*sforcing chain, %d at ends of ",
                                           solver_recurse_depth*4, "", orign);
                                    xl = xx;
                                    yl = yy;
                                    while (1) {
                                        printf("%s(%d,%d)", sep, 1+xl,
                                               1+yl);
                                        xl = bfsprev[yl*cr+xl];
                                        if (xl < 0)
                                            break;
                                        yl = xl / cr;
                                        xl %= cr;
                                        sep = "-";
                                    }
                                    printf("\n%*s  ruling out %d at (%d,%d)\n",
                                           solver_recurse_depth*4, "",
                                           orign, 1+xt, 1+yt);
                                }
#endif
                                cube(xt, yt, orign) = false;
                                return 1;
                            }
                        }
                    }
                }
        }

    return 0;
}

static int solver_killer_minmax(struct solver_usage *usage,
				struct block_structure *cages, digit *clues,
				int b
#ifdef STANDALONE_SOLVER
				, const char *extra
#endif
				)
{
    int cr = usage->cr;
    int i;
    int ret = 0;
    int nsquares = cages->nr_squares[b];

    if (clues[b] == 0)
	return 0;

    for (i = 0; i < nsquares; i++) {
	int n, x = cages->blocks[b][i];

	for (n = 1; n <= cr; n++)
	    if (cube2(x, n)) {
		int maxval = 0, minval = 0;
		int j;
		for (j = 0; j < nsquares; j++) {
		    int m;
		    int y = cages->blocks[b][j];
		    if (i == j)
			continue;
		    for (m = 1; m <= cr; m++)
			if (cube2(y, m)) {
			    minval += m;
			    break;
			}
		    for (m = cr; m > 0; m--)
			if (cube2(y, m)) {
			    maxval += m;
			    break;
			}
		}
		if (maxval + n < clues[b]) {
		    cube2(x, n) = false;
		    ret = 1;
#ifdef STANDALONE_SOLVER
		    if (solver_show_working)
			printf("%*s  ruling out %d at (%d,%d) as too low %s\n",
			       solver_recurse_depth*4, "killer minmax analysis",
			       n, 1 + x%cr, 1 + x/cr, extra);
#endif
		}
		if (minval + n > clues[b]) {
		    cube2(x, n) = false;
		    ret = 1;
#ifdef STANDALONE_SOLVER
		    if (solver_show_working)
			printf("%*s  ruling out %d at (%d,%d) as too high %s\n",
			       solver_recurse_depth*4, "killer minmax analysis",
			       n, 1 + x%cr, 1 + x/cr, extra);
#endif
		}
	    }
    }
    return ret;
}

static int solver_killer_sums(struct solver_usage *usage, int b,
			      struct block_structure *cages, int clue,
			      bool cage_is_region
#ifdef STANDALONE_SOLVER
			      , const char *cage_type
#endif
			      )
{
    int cr = usage->cr;
    int i, ret, max_sums;
    int nsquares = cages->nr_squares[b];
    unsigned long *sumbits, possible_addends;

    if (clue == 0) {
	assert(nsquares == 0);
	return 0;
    }
    if (nsquares == 0) {
#ifdef STANDALONE_SOLVER
        if (solver_show_working)
            printf("%*skiller: cage has no usable squares left\n",
                   solver_recurse_depth*4, "");
#endif
        return -1;
    }

    if (nsquares < 2 || nsquares > 4)
	return 0;

    if (!cage_is_region) {
	int known_row = -1, known_col = -1, known_block = -1;
	/*
	 * Verify that the cage lies entirely within one region,
	 * so that using the precomputed sums is valid.
	 */
	for (i = 0; i < nsquares; i++) {
	    int x = cages->blocks[b][i];

	    assert(usage->grid[x] == 0);

	    if (i == 0) {
		known_row = x/cr;
		known_col = x%cr;
		known_block = usage->blocks->whichblock[x];
	    } else {
		if (known_row != x/cr)
		    known_row = -1;
		if (known_col != x%cr)
		    known_col = -1;
		if (known_block != usage->blocks->whichblock[x])
		    known_block = -1;
	    }
	}
	if (known_block == -1 && known_col == -1 && known_row == -1)
	    return 0;
    }
    if (nsquares == 2) {
	if (clue < 3 || clue > 17)
	    return -1;

	sumbits = sum_bits2[clue];
	max_sums = MAX_2SUMS;
    } else if (nsquares == 3) {
	if (clue < 6 || clue > 24)
	    return -1;

	sumbits = sum_bits3[clue];
	max_sums = MAX_3SUMS;
    } else {
	if (clue < 10 || clue > 30)
	    return -1;

	sumbits = sum_bits4[clue];
	max_sums = MAX_4SUMS;
    }
    /*
     * For every possible way to get the sum, see if there is
     * one square in the cage that disallows all the required
     * addends.  If we find one such square, this way to compute
     * the sum is impossible.
     */
    possible_addends = 0;
    for (i = 0; i < max_sums; i++) {
	int j;
	unsigned long bits = sumbits[i];

	if (bits == 0)
	    break;

	for (j = 0; j < nsquares; j++) {
	    int n;
	    unsigned long square_bits = bits;
	    int x = cages->blocks[b][j];
	    for (n = 1; n <= cr; n++)
		if (!cube2(x, n))
		    square_bits &= ~(1L << n);
	    if (square_bits == 0) {
		break;
	    }
	}
	if (j == nsquares)
	    possible_addends |= bits;
    }
    /*
     * Now we know which addends can possibly be used to
     * compute the sum.  Remove all other digits from the
     * set of possibilities.
     */
    if (possible_addends == 0)
	return -1;

    ret = 0;
    for (i = 0; i < nsquares; i++) {
	int n;
	int x = cages->blocks[b][i];
	for (n = 1; n <= cr; n++) {
	    if (!cube2(x, n))
		continue;
	    if ((possible_addends & (1 << n)) == 0) {
		cube2(x, n) = false;
		ret = 1;
#ifdef STANDALONE_SOLVER
		if (solver_show_working) {
		    printf("%*s  using %s\n",
			   solver_recurse_depth*4, "killer sums analysis",
			   cage_type);
		    printf("%*s  ruling out %d at (%d,%d) due to impossible %d-sum\n",
			   solver_recurse_depth*4, "",
			   n, 1 + x%cr, 1 + x/cr, nsquares);
		}
#endif
	    }
	}
    }
    return ret;
}

static int filter_whole_cages(struct solver_usage *usage, int *squares, int n,
			      int *filtered_sum)
{
    int b, i, j, off;
    *filtered_sum = 0;

    /* First, filter squares with a clue.  */
    for (i = j = 0; i < n; i++)
	if (usage->grid[squares[i]])
	    *filtered_sum += usage->grid[squares[i]];
	else
	    squares[j++] = squares[i];
    n = j;

    /*
     * Filter all cages that are covered entirely by the list of
     * squares.
     */
    off = 0;
    for (b = 0; b < usage->kblocks->nr_blocks && off < n; b++) {
	int b_squares = usage->kblocks->nr_squares[b];
	int matched = 0;

	if (b_squares == 0)
	    continue;

	/*
	 * Find all squares of block b that lie in our list,
	 * and make them contiguous at off, which is the current position
	 * in the output list.
	 */
	for (i = 0; i < b_squares; i++) {
	    for (j = off; j < n; j++)
		if (squares[j] == usage->kblocks->blocks[b][i]) {
		    int t = squares[off + matched];
		    squares[off + matched] = squares[j];
		    squares[j] = t;
		    matched++;
		    break;
		}
	}
	/* If so, filter out all squares of b from the list.  */
	if (matched != usage->kblocks->nr_squares[b]) {
	    off += matched;
	    continue;
	}
	memmove(squares + off, squares + off + matched,
		(n - off - matched) * sizeof *squares);
	n -= matched;

	*filtered_sum += usage->kclues[b];
    }
    assert(off == n);
    return off;
}

static struct solver_scratch *solver_new_scratch(struct solver_usage *usage)
{
    struct solver_scratch *scratch = snew(struct solver_scratch);
    int cr = usage->cr;
    scratch->grid = snewn(cr*cr, unsigned char);
    scratch->rowidx = snewn(cr, unsigned char);
    scratch->colidx = snewn(cr, unsigned char);
    scratch->set = snewn(cr, unsigned char);
    scratch->neighbours = snewn(5*cr, int);
    scratch->bfsqueue = snewn(cr*cr, int);
#ifdef STANDALONE_SOLVER
    scratch->bfsprev = snewn(cr*cr, int);
#endif
    scratch->indexlist = snewn(cr*cr, int);   /* used for set elimination */
    scratch->indexlist2 = snewn(cr, int);   /* only used for intersect() */
    return scratch;
}

static void solver_free_scratch(struct solver_scratch *scratch)
{
#ifdef STANDALONE_SOLVER
    sfree(scratch->bfsprev);
#endif
    sfree(scratch->bfsqueue);
    sfree(scratch->neighbours);
    sfree(scratch->set);
    sfree(scratch->colidx);
    sfree(scratch->rowidx);
    sfree(scratch->grid);
    sfree(scratch->indexlist);
    sfree(scratch->indexlist2);
    sfree(scratch);
}

/*
 * Used for passing information about difficulty levels between the solver
 * and its callers.
 */
struct difficulty {
    /* Maximum levels allowed.  */
    int maxdiff, maxkdiff;
    /* Levels reached by the solver.  */
    int diff, kdiff;
};

static void solver(int cr, struct block_structure *blocks,
		  struct block_structure *kblocks, bool xtype,
		  digit *grid, digit *kgrid, struct difficulty *dlev)
{
    struct solver_usage *usage;
    struct solver_scratch *scratch;
    int x, y, b, i, n, ret;
    int diff = DIFF_BLOCK;
    int kdiff = DIFF_KSINGLE;

    /*
     * Set up a usage structure as a clean slate (everything
     * possible).
     */
    usage = snew(struct solver_usage);
    usage->cr = cr;
    usage->blocks = blocks;
    if (kblocks) {
	usage->kblocks = dup_block_structure(kblocks);
	usage->extra_cages = alloc_block_structure (kblocks->c, kblocks->r,
						    cr * cr, cr, cr * cr);
	usage->extra_clues = snewn(cr*cr, digit);
    } else {
	usage->kblocks = usage->extra_cages = NULL;
	usage->extra_clues = NULL;
    }
    usage->cube = snewn(cr*cr*cr, bool);
    usage->grid = grid;		       /* write straight back to the input */
    if (kgrid) {
	int nclues;

        assert(kblocks);
        nclues = kblocks->nr_blocks;
	/*
	 * Allow for expansion of the killer regions, the absolute
	 * limit is obviously one region per square.
	 */
	usage->kclues = snewn(cr*cr, digit);
	for (i = 0; i < nclues; i++) {
	    for (n = 0; n < kblocks->nr_squares[i]; n++)
		if (kgrid[kblocks->blocks[i][n]] != 0)
		    usage->kclues[i] = kgrid[kblocks->blocks[i][n]];
	    assert(usage->kclues[i] > 0);
	}
	memset(usage->kclues + nclues, 0, cr*cr - nclues);
    } else {
	usage->kclues = NULL;
    }

    for (i = 0; i < cr*cr*cr; i++)
        usage->cube[i] = true;

    usage->row = snewn(cr * cr, bool);
    usage->col = snewn(cr * cr, bool);
    usage->blk = snewn(cr * cr, bool);
    memset(usage->row, 0, cr * cr * sizeof(bool));
    memset(usage->col, 0, cr * cr * sizeof(bool));
    memset(usage->blk, 0, cr * cr * sizeof(bool));

    if (xtype) {
	usage->diag = snewn(cr * 2, bool);
	memset(usage->diag, 0, cr * 2 * sizeof(bool));
    } else
	usage->diag = NULL; 

    usage->nr_regions = cr * 3 + (xtype ? 2 : 0);
    usage->regions = snewn(cr * usage->nr_regions, int);
    usage->sq2region = snewn(cr * cr * 3, int *);

    for (n = 0; n < cr; n++) {
	for (i = 0; i < cr; i++) {
	    x = n*cr+i;
	    y = i*cr+n;
	    b = usage->blocks->blocks[n][i];
	    usage->regions[cr*n*3 + i] = x;
	    usage->regions[cr*n*3 + cr + i] = y;
	    usage->regions[cr*n*3 + 2*cr + i] = b;
	    usage->sq2region[x*3] = usage->regions + cr*n*3;
	    usage->sq2region[y*3 + 1] = usage->regions + cr*n*3 + cr;
	    usage->sq2region[b*3 + 2] = usage->regions + cr*n*3 + 2*cr;
	}
    }

    scratch = solver_new_scratch(usage);

    /*
     * Place all the clue numbers we are given.
     */
    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++) {
            int n = grid[y*cr+x];
	    if (n) {
                if (!cube(x,y,n)) {
                    diff = DIFF_IMPOSSIBLE;
                    goto got_result;
                }
		solver_place(usage, x, y, grid[y*cr+x]);
            }
        }

    /*
     * Now loop over the grid repeatedly trying all permitted modes
     * of reasoning. The loop terminates if we complete an
     * iteration without making any progress; we then return
     * failure or success depending on whether the grid is full or
     * not.
     */
    while (1) {
        /*
         * I'd like to write `continue;' inside each of the
         * following loops, so that the solver returns here after
         * making some progress. However, I can't specify that I
         * want to continue an outer loop rather than the innermost
         * one, so I'm apologetically resorting to a goto.
         */
        cont:

	/*
	 * Blockwise positional elimination.
	 */
	for (b = 0; b < cr; b++)
	    for (n = 1; n <= cr; n++)
		if (!usage->blk[b*cr+n-1]) {
		    for (i = 0; i < cr; i++)
			scratch->indexlist[i] = cubepos2(usage->blocks->blocks[b][i],n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in block %s", n,
				      usage->blocks->blocknames[b]
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_BLOCK);
			goto cont;
		    }
		}

	if (usage->kclues != NULL) {
	    bool changed = false;

	    /*
	     * First, bring the kblocks into a more useful form: remove
	     * all filled-in squares, and reduce the sum by their values.
	     * Walk in reverse order, since otherwise remove_from_block
	     * can move element past our loop counter.
	     */
	    for (b = 0; b < usage->kblocks->nr_blocks; b++)
		for (i = usage->kblocks->nr_squares[b] -1; i >= 0; i--) {
		    int x = usage->kblocks->blocks[b][i];
		    int t = usage->grid[x];

		    if (t == 0)
			continue;
		    remove_from_block(usage->kblocks, b, x);
		    if (t > usage->kclues[b]) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    }
		    usage->kclues[b] -= t;
		    /*
		     * Since cages are regions, this tells us something
		     * about the other squares in the cage.
		     */
		    for (n = 0; n < usage->kblocks->nr_squares[b]; n++) {
			cube2(usage->kblocks->blocks[b][n], t) = false;
		    }
		}

	    /*
	     * The most trivial kind of solver for killer puzzles: fill
	     * single-square cages.
	     */
	    for (b = 0; b < usage->kblocks->nr_blocks; b++) {
		int squares = usage->kblocks->nr_squares[b];
		if (squares == 1) {
		    int v = usage->kclues[b];
		    if (v < 1 || v > cr) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    }
		    x = usage->kblocks->blocks[b][0] % cr;
		    y = usage->kblocks->blocks[b][0] / cr;
		    if (!cube(x, y, v)) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    }
		    solver_place(usage, x, y, v);

#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*s  placing %d at (%d,%d)\n",
			       solver_recurse_depth*4, "killer single-square cage",
			       v, 1 + x%cr, 1 + x/cr);
		    }
#endif
		    changed = true;
		}
	    }

	    if (changed) {
		kdiff = max(kdiff, DIFF_KSINGLE);
		goto cont;
	    }
	}
	if (dlev->maxkdiff >= DIFF_KINTERSECT && usage->kclues != NULL) {
	    bool changed = false;
	    /*
	     * Now, create the extra_cages information.  Every full region
	     * (row, column, or block) has the same sum total (45 for 3x3
	     * puzzles.  After we try to cover these regions with cages that
	     * lie entirely within them, any squares that remain must bring
	     * the total to this known value, and so they form additional
	     * cages which aren't immediately evident in the displayed form
	     * of the puzzle.
	     */
	    usage->extra_cages->nr_blocks = 0;
	    for (i = 0; i < 3; i++) {
		for (n = 0; n < cr; n++) {
		    int *region = usage->regions + cr*n*3 + i*cr;
		    int sum = cr * (cr + 1) / 2;
		    int nsquares = cr;
		    int filtered;
		    int n_extra = usage->extra_cages->nr_blocks;
		    int *extra_list = usage->extra_cages->blocks[n_extra];
		    memcpy(extra_list, region, cr * sizeof *extra_list);

		    nsquares = filter_whole_cages(usage, extra_list, nsquares, &filtered);
		    sum -= filtered;
		    if (nsquares == cr || nsquares == 0)
			continue;
		    if (dlev->maxdiff >= DIFF_RECURSIVE) {
			if (sum <= 0) {
			    dlev->diff = DIFF_IMPOSSIBLE;
			    goto got_result;
			}
		    }
		    assert(sum > 0);

		    if (nsquares == 1) {
			if (sum > cr) {
			    diff = DIFF_IMPOSSIBLE;
			    goto got_result;
			}
			x = extra_list[0] % cr;
			y = extra_list[0] / cr;
			if (!cube(x, y, sum)) {
			    diff = DIFF_IMPOSSIBLE;
			    goto got_result;
			}
			solver_place(usage, x, y, sum);
			changed = true;
#ifdef STANDALONE_SOLVER
			if (solver_show_working) {
			    printf("%*s  placing %d at (%d,%d)\n",
				   solver_recurse_depth*4, "killer single-square deduced cage",
				   sum, 1 + x, 1 + y);
			}
#endif
		    }

		    b = usage->kblocks->whichblock[extra_list[0]];
		    for (x = 1; x < nsquares; x++)
			if (usage->kblocks->whichblock[extra_list[x]] != b)
			    break;
		    if (x == nsquares) {
			assert(usage->kblocks->nr_squares[b] > nsquares);
			split_block(usage->kblocks, extra_list, nsquares);
			assert(usage->kblocks->nr_squares[usage->kblocks->nr_blocks - 1] == nsquares);
			usage->kclues[usage->kblocks->nr_blocks - 1] = sum;
			usage->kclues[b] -= sum;
		    } else {
			usage->extra_cages->nr_squares[n_extra] = nsquares;
			usage->extra_cages->nr_blocks++;
			usage->extra_clues[n_extra] = sum;
		    }
		}
	    }
	    if (changed) {
		kdiff = max(kdiff, DIFF_KINTERSECT);
		goto cont;
	    }
	}

	/*
	 * Another simple killer-type elimination.  For every square in a
	 * cage, find the minimum and maximum possible sums of all the
	 * other squares in the same cage, and rule out possibilities
	 * for the given square based on whether they are guaranteed to
	 * cause the sum to be either too high or too low.
	 * This is a special case of trying all possible sums across a
	 * region, which is a recursive algorithm.  We should probably
	 * implement it for a higher difficulty level.
	 */
	if (dlev->maxkdiff >= DIFF_KMINMAX && usage->kclues != NULL) {
	    bool changed = false;
	    for (b = 0; b < usage->kblocks->nr_blocks; b++) {
		int ret = solver_killer_minmax(usage, usage->kblocks,
					       usage->kclues, b
#ifdef STANDALONE_SOLVER
					     , ""
#endif
					       );
		if (ret < 0) {
		    diff = DIFF_IMPOSSIBLE;
		    goto got_result;
		} else if (ret > 0)
		    changed = true;
	    }
	    for (b = 0; b < usage->extra_cages->nr_blocks; b++) {
		int ret = solver_killer_minmax(usage, usage->extra_cages,
					       usage->extra_clues, b
#ifdef STANDALONE_SOLVER
					       , "using deduced cages"
#endif
					       );
		if (ret < 0) {
		    diff = DIFF_IMPOSSIBLE;
		    goto got_result;
		} else if (ret > 0)
		    changed = true;
	    }
	    if (changed) {
		kdiff = max(kdiff, DIFF_KMINMAX);
		goto cont;
	    }
	}

	/*
	 * Try to use knowledge of which numbers can be used to generate
	 * a given sum.
	 * This can only be used if a cage lies entirely within a region.
	 */
	if (dlev->maxkdiff >= DIFF_KSUMS && usage->kclues != NULL) {
	    bool changed = false;

	    for (b = 0; b < usage->kblocks->nr_blocks; b++) {
		int ret = solver_killer_sums(usage, b, usage->kblocks,
					     usage->kclues[b], true
#ifdef STANDALONE_SOLVER
					     , "regular clues"
#endif
					     );
		if (ret > 0) {
		    changed = true;
		    kdiff = max(kdiff, DIFF_KSUMS);
		} else if (ret < 0) {
		    diff = DIFF_IMPOSSIBLE;
		    goto got_result;
		}
	    }

	    for (b = 0; b < usage->extra_cages->nr_blocks; b++) {
		int ret = solver_killer_sums(usage, b, usage->extra_cages,
					     usage->extra_clues[b], false
#ifdef STANDALONE_SOLVER
					     , "deduced clues"
#endif
					     );
		if (ret > 0) {
		    changed = true;
		    kdiff = max(kdiff, DIFF_KSUMS);
		} else if (ret < 0) {
		    diff = DIFF_IMPOSSIBLE;
		    goto got_result;
		}
	    }

	    if (changed)
		goto cont;
	}

	if (dlev->maxdiff <= DIFF_BLOCK)
	    break;

	/*
	 * Row-wise positional elimination.
	 */
	for (y = 0; y < cr; y++)
	    for (n = 1; n <= cr; n++)
		if (!usage->row[y*cr+n-1]) {
		    for (x = 0; x < cr; x++)
			scratch->indexlist[x] = cubepos(x, y, n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in row %d", n, 1+y
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }
	/*
	 * Column-wise positional elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (n = 1; n <= cr; n++)
		if (!usage->col[x*cr+n-1]) {
		    for (y = 0; y < cr; y++)
			scratch->indexlist[y] = cubepos(x, y, n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in column %d", n, 1+x
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }

	/*
	 * X-diagonal positional elimination.
	 */
	if (usage->diag) {
	    for (n = 1; n <= cr; n++)
		if (!usage->diag[n-1]) {
		    for (i = 0; i < cr; i++)
			scratch->indexlist[i] = cubepos2(diag0(i), n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in \\-diagonal", n
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }
	    for (n = 1; n <= cr; n++)
		if (!usage->diag[cr+n-1]) {
		    for (i = 0; i < cr; i++)
			scratch->indexlist[i] = cubepos2(diag1(i), n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "positional elimination,"
				      " %d in /-diagonal", n
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }
	}

	/*
	 * Numeric elimination.
	 */
	for (x = 0; x < cr; x++)
	    for (y = 0; y < cr; y++)
		if (!usage->grid[y*cr+x]) {
		    for (n = 1; n <= cr; n++)
			scratch->indexlist[n-1] = cubepos(x, y, n);
		    ret = solver_elim(usage, scratch->indexlist
#ifdef STANDALONE_SOLVER
				      , "numeric elimination at (%d,%d)",
				      1+x, 1+y
#endif
				      );
		    if (ret < 0) {
			diff = DIFF_IMPOSSIBLE;
			goto got_result;
		    } else if (ret > 0) {
			diff = max(diff, DIFF_SIMPLE);
			goto cont;
		    }
                }

	if (dlev->maxdiff <= DIFF_SIMPLE)
	    break;

        /*
         * Intersectional analysis, rows vs blocks.
         */
        for (y = 0; y < cr; y++)
            for (b = 0; b < cr; b++)
                for (n = 1; n <= cr; n++) {
                    if (usage->row[y*cr+n-1] ||
                        usage->blk[b*cr+n-1])
			continue;
		    for (i = 0; i < cr; i++) {
			scratch->indexlist[i] = cubepos(i, y, n);
			scratch->indexlist2[i] = cubepos2(usage->blocks->blocks[b][i], n);
		    }
		    /*
		     * solver_intersect() never returns -1.
		     */
		    if (solver_intersect(usage, scratch->indexlist,
					 scratch->indexlist2
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in row %d vs block %s",
                                          n, 1+y, usage->blocks->blocknames[b]
#endif
                                          ) ||
                         solver_intersect(usage, scratch->indexlist2,
					 scratch->indexlist
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block %s vs row %d",
                                          n, usage->blocks->blocknames[b], 1+y
#endif
                                          )) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }
		}

        /*
         * Intersectional analysis, columns vs blocks.
         */
        for (x = 0; x < cr; x++)
            for (b = 0; b < cr; b++)
                for (n = 1; n <= cr; n++) {
                    if (usage->col[x*cr+n-1] ||
                        usage->blk[b*cr+n-1])
			continue;
		    for (i = 0; i < cr; i++) {
			scratch->indexlist[i] = cubepos(x, i, n);
			scratch->indexlist2[i] = cubepos2(usage->blocks->blocks[b][i], n);
		    }
		    if (solver_intersect(usage, scratch->indexlist,
					 scratch->indexlist2
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in column %d vs block %s",
                                          n, 1+x, usage->blocks->blocknames[b]
#endif
                                          ) ||
                         solver_intersect(usage, scratch->indexlist2,
					 scratch->indexlist
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block %s vs column %d",
                                          n, usage->blocks->blocknames[b], 1+x
#endif
                                          )) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }
		}

	if (usage->diag) {
	    /*
	     * Intersectional analysis, \-diagonal vs blocks.
	     */
            for (b = 0; b < cr; b++)
                for (n = 1; n <= cr; n++) {
                    if (usage->diag[n-1] ||
                        usage->blk[b*cr+n-1])
			continue;
		    for (i = 0; i < cr; i++) {
			scratch->indexlist[i] = cubepos2(diag0(i), n);
			scratch->indexlist2[i] = cubepos2(usage->blocks->blocks[b][i], n);
		    }
		    if (solver_intersect(usage, scratch->indexlist,
					 scratch->indexlist2
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in \\-diagonal vs block %s",
                                          n, usage->blocks->blocknames[b]
#endif
                                          ) ||
                         solver_intersect(usage, scratch->indexlist2,
					 scratch->indexlist
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block %s vs \\-diagonal",
                                          n, usage->blocks->blocknames[b]
#endif
                                          )) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }
		}

	    /*
	     * Intersectional analysis, /-diagonal vs blocks.
	     */
            for (b = 0; b < cr; b++)
                for (n = 1; n <= cr; n++) {
                    if (usage->diag[cr+n-1] ||
                        usage->blk[b*cr+n-1])
			continue;
		    for (i = 0; i < cr; i++) {
			scratch->indexlist[i] = cubepos2(diag1(i), n);
			scratch->indexlist2[i] = cubepos2(usage->blocks->blocks[b][i], n);
		    }
		    if (solver_intersect(usage, scratch->indexlist,
					 scratch->indexlist2
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in /-diagonal vs block %s",
                                          n, usage->blocks->blocknames[b]
#endif
                                          ) ||
                         solver_intersect(usage, scratch->indexlist2,
					 scratch->indexlist
#ifdef STANDALONE_SOLVER
                                          , "intersectional analysis,"
                                          " %d in block %s vs /-diagonal",
                                          n, usage->blocks->blocknames[b]
#endif
                                          )) {
                        diff = max(diff, DIFF_INTERSECT);
                        goto cont;
                    }
		}
	}

	if (dlev->maxdiff <= DIFF_INTERSECT)
	    break;

	/*
	 * Blockwise set elimination.
	 */
	for (b = 0; b < cr; b++) {
	    for (i = 0; i < cr; i++)
		for (n = 1; n <= cr; n++)
		    scratch->indexlist[i*cr+n-1] = cubepos2(usage->blocks->blocks[b][i], n);
	    ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "set elimination, block %s",
			     usage->blocks->blocknames[b]
#endif
				 );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	/*
	 * Row-wise set elimination.
	 */
	for (y = 0; y < cr; y++) {
	    for (x = 0; x < cr; x++)
		for (n = 1; n <= cr; n++)
		    scratch->indexlist[x*cr+n-1] = cubepos(x, y, n);
	    ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "set elimination, row %d", 1+y
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	/*
	 * Column-wise set elimination.
	 */
	for (x = 0; x < cr; x++) {
	    for (y = 0; y < cr; y++)
		for (n = 1; n <= cr; n++)
		    scratch->indexlist[y*cr+n-1] = cubepos(x, y, n);
            ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "set elimination, column %d", 1+x
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	if (usage->diag) {
	    /*
	     * \-diagonal set elimination.
	     */
	    for (i = 0; i < cr; i++)
		for (n = 1; n <= cr; n++)
		    scratch->indexlist[i*cr+n-1] = cubepos2(diag0(i), n);
            ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "set elimination, \\-diagonal"
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }

	    /*
	     * /-diagonal set elimination.
	     */
	    for (i = 0; i < cr; i++)
		for (n = 1; n <= cr; n++)
		    scratch->indexlist[i*cr+n-1] = cubepos2(diag1(i), n);
            ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "set elimination, /-diagonal"
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_SET);
		goto cont;
	    }
	}

	if (dlev->maxdiff <= DIFF_SET)
	    break;

	/*
	 * Row-vs-column set elimination on a single number.
	 */
	for (n = 1; n <= cr; n++) {
	    for (y = 0; y < cr; y++)
		for (x = 0; x < cr; x++)
		    scratch->indexlist[y*cr+x] = cubepos(x, y, n);
            ret = solver_set(usage, scratch, scratch->indexlist
#ifdef STANDALONE_SOLVER
			     , "positional set elimination, number %d", n
#endif
			     );
	    if (ret < 0) {
		diff = DIFF_IMPOSSIBLE;
		goto got_result;
	    } else if (ret > 0) {
		diff = max(diff, DIFF_EXTREME);
		goto cont;
	    }
	}

        /*
         * Forcing chains.
         */
        if (solver_forcing(usage, scratch)) {
            diff = max(diff, DIFF_EXTREME);
            goto cont;
        }

	/*
	 * If we reach here, we have made no deductions in this
	 * iteration, so the algorithm terminates.
	 */
	break;
    }

    /*
     * Last chance: if we haven't fully solved the puzzle yet, try
     * recursing based on guesses for a particular square. We pick
     * one of the most constrained empty squares we can find, which
     * has the effect of pruning the search tree as much as
     * possible.
     */
    if (dlev->maxdiff >= DIFF_RECURSIVE) {
	int best, bestcount;

	best = -1;
	bestcount = cr+1;

	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		if (!grid[y*cr+x]) {
		    int count;

		    /*
		     * An unfilled square. Count the number of
		     * possible digits in it.
		     */
		    count = 0;
		    for (n = 1; n <= cr; n++)
			if (cube(x,y,n))
			    count++;

		    /*
		     * We should have found any impossibilities
		     * already, so this can safely be an assert.
		     */
		    assert(count > 1);

		    if (count < bestcount) {
			bestcount = count;
			best = y*cr+x;
		    }
		}

	if (best != -1) {
	    int i, j;
	    digit *list, *ingrid, *outgrid;

	    diff = DIFF_IMPOSSIBLE;    /* no solution found yet */

	    /*
	     * Attempt recursion.
	     */
	    y = best / cr;
	    x = best % cr;

	    list = snewn(cr, digit);
	    ingrid = snewn(cr * cr, digit);
	    outgrid = snewn(cr * cr, digit);
	    memcpy(ingrid, grid, cr * cr);

	    /* Make a list of the possible digits. */
	    for (j = 0, n = 1; n <= cr; n++)
		if (cube(x,y,n))
		    list[j++] = n;

#ifdef STANDALONE_SOLVER
	    if (solver_show_working) {
		const char *sep = "";
		printf("%*srecursing on (%d,%d) [",
		       solver_recurse_depth*4, "", x + 1, y + 1);
		for (i = 0; i < j; i++) {
		    printf("%s%d", sep, list[i]);
		    sep = " or ";
		}
		printf("]\n");
	    }
#endif

	    /*
	     * And step along the list, recursing back into the
	     * main solver at every stage.
	     */
	    for (i = 0; i < j; i++) {
		memcpy(outgrid, ingrid, cr * cr);
		outgrid[y*cr+x] = list[i];

#ifdef STANDALONE_SOLVER
		if (solver_show_working)
		    printf("%*sguessing %d at (%d,%d)\n",
			   solver_recurse_depth*4, "", list[i], x + 1, y + 1);
		solver_recurse_depth++;
#endif

		solver(cr, blocks, kblocks, xtype, outgrid, kgrid, dlev);

#ifdef STANDALONE_SOLVER
		solver_recurse_depth--;
		if (solver_show_working) {
		    printf("%*sretracting %d at (%d,%d)\n",
			   solver_recurse_depth*4, "", list[i], x + 1, y + 1);
		}
#endif

		/*
		 * If we have our first solution, copy it into the
		 * grid we will return.
		 */
		if (diff == DIFF_IMPOSSIBLE && dlev->diff != DIFF_IMPOSSIBLE)
		    memcpy(grid, outgrid, cr*cr);

		if (dlev->diff == DIFF_AMBIGUOUS)
		    diff = DIFF_AMBIGUOUS;
		else if (dlev->diff == DIFF_IMPOSSIBLE)
		    /* do not change our return value */;
		else {
		    /* the recursion turned up exactly one solution */
		    if (diff == DIFF_IMPOSSIBLE)
			diff = DIFF_RECURSIVE;
		    else
			diff = DIFF_AMBIGUOUS;
		}

		/*
		 * As soon as we've found more than one solution,
		 * give up immediately.
		 */
		if (diff == DIFF_AMBIGUOUS)
		    break;
	    }

	    sfree(outgrid);
	    sfree(ingrid);
	    sfree(list);
	}

    } else {
        /*
         * We're forbidden to use recursion, so we just see whether
         * our grid is fully solved, and return DIFF_IMPOSSIBLE
         * otherwise.
         */
	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		if (!grid[y*cr+x])
                    diff = DIFF_IMPOSSIBLE;
    }

    got_result:
    dlev->diff = diff;
    dlev->kdiff = kdiff;

#ifdef STANDALONE_SOLVER
    if (solver_show_working)
	printf("%*s%s found\n",
	       solver_recurse_depth*4, "",
	       diff == DIFF_IMPOSSIBLE ? "no solution" :
	       diff == DIFF_AMBIGUOUS ? "multiple solutions" :
	       "one solution");
#endif

    sfree(usage->sq2region);
    sfree(usage->regions);
    sfree(usage->cube);
    sfree(usage->row);
    sfree(usage->col);
    sfree(usage->blk);
    if (usage->kblocks) {
	free_block_structure(usage->kblocks);
	free_block_structure(usage->extra_cages);
	sfree(usage->extra_clues);
    }
    if (usage->kclues) sfree(usage->kclues);
    sfree(usage);

    solver_free_scratch(scratch);
}

/* ----------------------------------------------------------------------
 * End of solver code.
 */

/* ----------------------------------------------------------------------
 * Killer set generator.
 */

/* ----------------------------------------------------------------------
 * Solo filled-grid generator.
 *
 * This grid generator works by essentially trying to solve a grid
 * starting from no clues, and not worrying that there's more than
 * one possible solution. Unfortunately, it isn't computationally
 * feasible to do this by calling the above solver with an empty
 * grid, because that one needs to allocate a lot of scratch space
 * at every recursion level. Instead, I have a much simpler
 * algorithm which I shamelessly copied from a Python solver
 * written by Andrew Wilkinson (which is GPLed, but I've reused
 * only ideas and no code). It mostly just does the obvious
 * recursive thing: pick an empty square, put one of the possible
 * digits in it, recurse until all squares are filled, backtrack
 * and change some choices if necessary.
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
 * The use of bit sets implies that we support puzzles up to a size of
 * 32x32 (less if anyone finds a 16-bit machine to compile this on).
 */

/*
 * Internal data structure used in gridgen to keep track of
 * progress.
 */
struct gridgen_coord { int x, y, r; };
struct gridgen_usage {
    int cr;
    struct block_structure *blocks, *kblocks;
    /* grid is a copy of the input grid, modified as we go along */
    digit *grid;
    /*
     * Bitsets.  In each of them, bit n is set if digit n has been placed
     * in the corresponding region.  row, col and blk are used for all
     * puzzles.  cge is used only for killer puzzles, and diag is used
     * only for x-type puzzles.
     * All of these have cr entries, except diag which only has 2,
     * and cge, which has as many entries as kblocks.
     */
    unsigned int *row, *col, *blk, *cge, *diag;
    /* This lists all the empty spaces remaining in the grid. */
    struct gridgen_coord *spaces;
    int nspaces;
    /* If we need randomisation in the solve, this is our random state. */
    random_state *rs;
};

static void gridgen_place(struct gridgen_usage *usage, int x, int y, digit n)
{
    unsigned int bit = 1 << n;
    int cr = usage->cr;
    usage->row[y] |= bit;
    usage->col[x] |= bit;
    usage->blk[usage->blocks->whichblock[y*cr+x]] |= bit;
    if (usage->cge)
	usage->cge[usage->kblocks->whichblock[y*cr+x]] |= bit;
    if (usage->diag) {
	if (ondiag0(y*cr+x))
	    usage->diag[0] |= bit;
	if (ondiag1(y*cr+x))
	    usage->diag[1] |= bit;
    }
    usage->grid[y*cr+x] = n;
}

static void gridgen_remove(struct gridgen_usage *usage, int x, int y, digit n)
{
    unsigned int mask = ~(1 << n);
    int cr = usage->cr;
    usage->row[y] &= mask;
    usage->col[x] &= mask;
    usage->blk[usage->blocks->whichblock[y*cr+x]] &= mask;
    if (usage->cge)
	usage->cge[usage->kblocks->whichblock[y*cr+x]] &= mask;
    if (usage->diag) {
	if (ondiag0(y*cr+x))
	    usage->diag[0] &= mask;
	if (ondiag1(y*cr+x))
	    usage->diag[1] &= mask;
    }
    usage->grid[y*cr+x] = 0;
}

#define N_SINGLE 32

/*
 * The real recursive step in the generating function.
 *
 * Return values: 1 means solution found, 0 means no solution
 * found on this branch.
 */
static bool gridgen_real(struct gridgen_usage *usage, digit *grid, int *steps)
{
    int cr = usage->cr;
    int i, j, n, sx, sy, bestm, bestr;
    bool ret;
    int *digits;
    unsigned int used;

    /*
     * Firstly, check for completion! If there are no spaces left
     * in the grid, we have a solution.
     */
    if (usage->nspaces == 0)
	return true;

    /*
     * Next, abandon generation if we went over our steps limit.
     */
    if (*steps <= 0)
	return false;
    (*steps)--;

    /*
     * Otherwise, there must be at least one space. Find the most
     * constrained space, using the `r' field as a tie-breaker.
     */
    bestm = cr+1;		       /* so that any space will beat it */
    bestr = 0;
    used = ~0;
    i = sx = sy = -1;
    for (j = 0; j < usage->nspaces; j++) {
	int x = usage->spaces[j].x, y = usage->spaces[j].y;
	unsigned int used_xy;
	int m;

	m = usage->blocks->whichblock[y*cr+x];
	used_xy = usage->row[y] | usage->col[x] | usage->blk[m];
	if (usage->cge != NULL)
	    used_xy |= usage->cge[usage->kblocks->whichblock[y*cr+x]];
	if (usage->cge != NULL)
	    used_xy |= usage->cge[usage->kblocks->whichblock[y*cr+x]];
	if (usage->diag != NULL) {
	    if (ondiag0(y*cr+x))
		used_xy |= usage->diag[0];
	    if (ondiag1(y*cr+x))
		used_xy |= usage->diag[1];
	}

	/*
	 * Find the number of digits that could go in this space.
	 */
	m = 0;
	for (n = 1; n <= cr; n++) {
	    unsigned int bit = 1 << n;
	    if ((used_xy & bit) == 0)
		m++;
	}
	if (m < bestm || (m == bestm && usage->spaces[j].r < bestr)) {
	    bestm = m;
	    bestr = usage->spaces[j].r;
	    sx = x;
	    sy = y;
	    i = j;
	    used = used_xy;
	}
    }

    /*
     * Swap that square into the final place in the spaces array,
     * so that decrementing nspaces will remove it from the list.
     */
    if (i != usage->nspaces-1) {
	struct gridgen_coord t;
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
    for (n = 1; n <= cr; n++) {
	unsigned int bit = 1 << n;

	if ((used & bit) == 0)
	    digits[j++] = n;
    }

    if (usage->rs)
	shuffle(digits, j, sizeof(*digits), usage->rs);

    /* And finally, go through the digit list and actually recurse. */
    ret = false;
    for (i = 0; i < j; i++) {
	n = digits[i];

	/* Update the usage structure to reflect the placing of this digit. */
	gridgen_place(usage, sx, sy, n);
	usage->nspaces--;

	/* Call the solver recursively. Stop when we find a solution. */
	if (gridgen_real(usage, grid, steps)) {
            ret = true;
	    break;
	}

	/* Revert the usage structure. */
	gridgen_remove(usage, sx, sy, n);
	usage->nspaces++;
    }

    sfree(digits);
    return ret;
}

/*
 * Entry point to generator. You give it parameters and a starting
 * grid, which is simply an array of cr*cr digits.
 */
static bool gridgen(int cr, struct block_structure *blocks,
                    struct block_structure *kblocks, bool xtype,
                    digit *grid, random_state *rs, int maxsteps)
{
    struct gridgen_usage *usage;
    int x, y;
    bool ret;

    /*
     * Clear the grid to start with.
     */
    memset(grid, 0, cr*cr);

    /*
     * Create a gridgen_usage structure.
     */
    usage = snew(struct gridgen_usage);

    usage->cr = cr;
    usage->blocks = blocks;

    usage->grid = grid;

    usage->row = snewn(cr, unsigned int);
    usage->col = snewn(cr, unsigned int);
    usage->blk = snewn(cr, unsigned int);
    if (kblocks != NULL) {
	usage->kblocks = kblocks;
	usage->cge = snewn(usage->kblocks->nr_blocks, unsigned int);
	memset(usage->cge, 0, kblocks->nr_blocks * sizeof *usage->cge);
    } else {
	usage->cge = NULL;
    }

    memset(usage->row, 0, cr * sizeof *usage->row);
    memset(usage->col, 0, cr * sizeof *usage->col);
    memset(usage->blk, 0, cr * sizeof *usage->blk);

    if (xtype) {
	usage->diag = snewn(2, unsigned int);
	memset(usage->diag, 0, 2 * sizeof *usage->diag);
    } else {
	usage->diag = NULL;
    }

    /*
     * Begin by filling in the whole top row with randomly chosen
     * numbers. This cannot introduce any bias or restriction on
     * the available grids, since we already know those numbers
     * are all distinct so all we're doing is choosing their
     * labels.
     */
    for (x = 0; x < cr; x++)
	grid[x] = x+1;
    shuffle(grid, cr, sizeof(*grid), rs);
    for (x = 0; x < cr; x++)
	gridgen_place(usage, x, 0, grid[x]);

    usage->spaces = snewn(cr * cr, struct gridgen_coord);
    usage->nspaces = 0;

    usage->rs = rs;

    /*
     * Initialise the list of grid spaces, taking care to leave
     * out the row I've already filled in above.
     */
    for (y = 1; y < cr; y++) {
	for (x = 0; x < cr; x++) {
            usage->spaces[usage->nspaces].x = x;
            usage->spaces[usage->nspaces].y = y;
            usage->spaces[usage->nspaces].r = random_bits(rs, 31);
            usage->nspaces++;
	}
    }

    /*
     * Run the real generator function.
     */
    ret = gridgen_real(usage, grid, &maxsteps);

    /*
     * Clean up the usage structure now we have our answer.
     */
    sfree(usage->spaces);
    sfree(usage->cge);
    sfree(usage->blk);
    sfree(usage->col);
    sfree(usage->row);
    sfree(usage);

    return ret;
}

/* ----------------------------------------------------------------------
 * End of grid generator code.
 */

static int check_killer_cage_sum(struct block_structure *kblocks,
                                 digit *kgrid, digit *grid, int blk)
{
    /*
     * Returns: -1 if the cage has any empty square; 0 if all squares
     * are full but the sum is wrong; +1 if all squares are full and
     * they have the right sum.
     *
     * Does not check uniqueness of numbers within the cage; that's
     * done elsewhere (because in error highlighting it needs to be
     * detected separately so as to flag the error in a visually
     * different way).
     */
    int n_squares = kblocks->nr_squares[blk];
    int sum = 0, clue = 0;
    int i;

    for (i = 0; i < n_squares; i++) {
        int xy = kblocks->blocks[blk][i];

        if (grid[xy] == 0)
            return -1;
        sum += grid[xy];

        if (kgrid[xy]) {
            assert(clue == 0);
            clue = kgrid[xy];
        }
    }

    assert(clue != 0);
    return sum == clue;
}

/*
 * Check whether a grid contains a valid complete puzzle.
 */
static bool check_valid(int cr, struct block_structure *blocks,
                        struct block_structure *kblocks,
                        digit *kgrid, bool xtype, digit *grid)
{
    bool *used;
    int x, y, i, j, n;

    used = snewn(cr, bool);

    /*
     * Check that each row contains precisely one of everything.
     */
    for (y = 0; y < cr; y++) {
	memset(used, 0, cr * sizeof(bool));
	for (x = 0; x < cr; x++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = true;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return false;
	    }
    }

    /*
     * Check that each column contains precisely one of everything.
     */
    for (x = 0; x < cr; x++) {
	memset(used, 0, cr * sizeof(bool));
	for (y = 0; y < cr; y++)
	    if (grid[y*cr+x] > 0 && grid[y*cr+x] <= cr)
		used[grid[y*cr+x]-1] = true;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return false;
	    }
    }

    /*
     * Check that each block contains precisely one of everything.
     */
    for (i = 0; i < cr; i++) {
	memset(used, 0, cr * sizeof(bool));
	for (j = 0; j < cr; j++)
	    if (grid[blocks->blocks[i][j]] > 0 &&
		grid[blocks->blocks[i][j]] <= cr)
		used[grid[blocks->blocks[i][j]]-1] = true;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return false;
	    }
    }

    /*
     * Check that each Killer cage, if any, contains at most one of
     * everything. If we also know the clues for those cages (which we
     * might not, when this function is called early in puzzle
     * generation), we also check that they all have the right sum.
     */
    if (kblocks) {
	for (i = 0; i < kblocks->nr_blocks; i++) {
            memset(used, 0, cr * sizeof(bool));
	    for (j = 0; j < kblocks->nr_squares[i]; j++)
		if (grid[kblocks->blocks[i][j]] > 0 &&
		    grid[kblocks->blocks[i][j]] <= cr) {
		    if (used[grid[kblocks->blocks[i][j]]-1]) {
			sfree(used);
			return false;
		    }
		    used[grid[kblocks->blocks[i][j]]-1] = true;
		}

            if (kgrid && check_killer_cage_sum(kblocks, kgrid, grid, i) != 1) {
                sfree(used);
                return false;
            }
	}
    }

    /*
     * Check that each diagonal contains precisely one of everything.
     */
    if (xtype) {
        memset(used, 0, cr * sizeof(bool));
	for (i = 0; i < cr; i++)
	    if (grid[diag0(i)] > 0 && grid[diag0(i)] <= cr)
		used[grid[diag0(i)]-1] = true;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return false;
	    }

        memset(used, 0, cr * sizeof(bool));
	for (i = 0; i < cr; i++)
	    if (grid[diag1(i)] > 0 && grid[diag1(i)] <= cr)
		used[grid[diag1(i)]-1] = true;
	for (n = 0; n < cr; n++)
	    if (!used[n]) {
		sfree(used);
		return false;
	    }
    }

    sfree(used);
    return true;
}

static int symmetries(const game_params *params, int x, int y,
                      int *output, int s)
{
    int c = params->c, r = params->r, cr = c*r;
    int i = 0;

#define ADD(x,y) (*output++ = (x), *output++ = (y), i++)

    ADD(x, y);

    switch (s) {
      case SYMM_NONE:
	break;			       /* just x,y is all we need */
      case SYMM_ROT2:
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_ROT4:
        ADD(cr - 1 - y, x);
        ADD(y, cr - 1 - x);
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_REF2:
        ADD(cr - 1 - x, y);
        break;
      case SYMM_REF2D:
        ADD(y, x);
        break;
      case SYMM_REF4:
        ADD(cr - 1 - x, y);
        ADD(x, cr - 1 - y);
        ADD(cr - 1 - x, cr - 1 - y);
        break;
      case SYMM_REF4D:
        ADD(y, x);
        ADD(cr - 1 - x, cr - 1 - y);
        ADD(cr - 1 - y, cr - 1 - x);
        break;
      case SYMM_REF8:
        ADD(cr - 1 - x, y);
        ADD(x, cr - 1 - y);
        ADD(cr - 1 - x, cr - 1 - y);
        ADD(y, x);
        ADD(y, cr - 1 - x);
        ADD(cr - 1 - y, x);
        ADD(cr - 1 - y, cr - 1 - x);
        break;
    }

#undef ADD

    return i;
}

static char *encode_solve_move(int cr, digit *grid)
{
    int i, len;
    char *ret, *p;
    const char *sep;

    /*
     * It's surprisingly easy to work out _exactly_ how long this
     * string needs to be. To decimal-encode all the numbers from 1
     * to n:
     * 
     *  - every number has a units digit; total is n.
     *  - all numbers above 9 have a tens digit; total is max(n-9,0).
     *  - all numbers above 99 have a hundreds digit; total is max(n-99,0).
     *  - and so on.
     */
    len = 0;
    for (i = 1; i <= cr; i *= 10)
	len += max(cr - i + 1, 0);
    len += cr;		       /* don't forget the commas */
    len *= cr;		       /* there are cr rows of these */

    /*
     * Now len is one bigger than the total size of the
     * comma-separated numbers (because we counted an
     * additional leading comma). We need to have a leading S
     * and a trailing NUL, so we're off by one in total.
     */
    len++;

    ret = snewn(len, char);
    p = ret;
    *p++ = 'S';
    sep = "";
    for (i = 0; i < cr*cr; i++) {
	p += sprintf(p, "%s%d", sep, grid[i]);
	sep = ",";
    }
    *p++ = '\0';
    assert(p - ret == len);

    return ret;
}

static void dsf_to_blocks(int *dsf, struct block_structure *blocks,
			  int min_expected, int max_expected)
{
    int cr = blocks->c * blocks->r, area = cr * cr;
    int i, nb = 0;

    for (i = 0; i < area; i++)
	blocks->whichblock[i] = -1;
    for (i = 0; i < area; i++) {
	int j = dsf_canonify(dsf, i);
	if (blocks->whichblock[j] < 0)
	    blocks->whichblock[j] = nb++;
	blocks->whichblock[i] = blocks->whichblock[j];
    }
    assert(nb >= min_expected && nb <= max_expected);
    blocks->nr_blocks = nb;
}

static void make_blocks_from_whichblock(struct block_structure *blocks)
{
    int i;

    for (i = 0; i < blocks->nr_blocks; i++) {
	blocks->blocks[i][blocks->max_nr_squares-1] = 0;
	blocks->nr_squares[i] = 0;
    }
    for (i = 0; i < blocks->area; i++) {
	int b = blocks->whichblock[i];
	int j = blocks->blocks[b][blocks->max_nr_squares-1]++;
	assert(j < blocks->max_nr_squares);
	blocks->blocks[b][j] = i;
	blocks->nr_squares[b]++;
    }
}

static char *encode_block_structure_desc(char *p, struct block_structure *blocks)
{
    int i, currrun = 0;
    int c = blocks->c, r = blocks->r, cr = c * r;

    /*
     * Encode the block structure. We do this by encoding
     * the pattern of dividing lines: first we iterate
     * over the cr*(cr-1) internal vertical grid lines in
     * ordinary reading order, then over the cr*(cr-1)
     * internal horizontal ones in transposed reading
     * order.
     * 
     * We encode the number of non-lines between the
     * lines; _ means zero (two adjacent divisions), a
     * means 1, ..., y means 25, and z means 25 non-lines
     * _and no following line_ (so that za means 26, zb 27
     * etc).
     */
    for (i = 0; i <= 2*cr*(cr-1); i++) {
	int x, y, p0, p1;
        bool edge;

	if (i == 2*cr*(cr-1)) {
	    edge = true;       /* terminating virtual edge */
	} else {
	    if (i < cr*(cr-1)) {
		y = i/(cr-1);
		x = i%(cr-1);
		p0 = y*cr+x;
		p1 = y*cr+x+1;
	    } else {
		x = i/(cr-1) - cr;
		y = i%(cr-1);
		p0 = y*cr+x;
		p1 = (y+1)*cr+x;
	    }
	    edge = (blocks->whichblock[p0] != blocks->whichblock[p1]);
	}

	if (edge) {
	    while (currrun > 25)
		*p++ = 'z', currrun -= 25;
	    if (currrun)
		*p++ = 'a'-1 + currrun;
	    else
		*p++ = '_';
	    currrun = 0;
	} else
	    currrun++;
    }
    return p;
}

static char *encode_grid(char *desc, digit *grid, int area)
{
    int run, i;
    char *p = desc;

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
		if (p > desc && n > 0)
		    *p++ = '_';
	    }
	    if (n > 0)
		p += sprintf(p, "%d", n);
	    run = 0;
	}
    }
    return p;
}

/*
 * Conservatively stimate the number of characters required for
 * encoding a grid of a certain area.
 */
static int grid_encode_space (int area)
{
    int t, count;
    for (count = 1, t = area; t > 26; t -= 26)
	count++;
    return count * area;
}

/*
 * Conservatively stimate the number of characters required for
 * encoding a given blocks structure.
 */
static int blocks_encode_space(struct block_structure *blocks)
{
    int cr = blocks->c * blocks->r, area = cr * cr;
    return grid_encode_space(area);
}

static char *encode_puzzle_desc(const game_params *params, digit *grid,
				struct block_structure *blocks,
				digit *kgrid,
				struct block_structure *kblocks)
{
    int c = params->c, r = params->r, cr = c*r;
    int area = cr*cr;
    char *p, *desc;
    int space;

    space = grid_encode_space(area) + 1;
    if (r == 1)
	space += blocks_encode_space(blocks) + 1;
    if (params->killer) {
	space += blocks_encode_space(kblocks) + 1;
	space += grid_encode_space(area) + 1;
    }
    desc = snewn(space, char);
    p = encode_grid(desc, grid, area);

    if (r == 1) {
	*p++ = ',';
	p = encode_block_structure_desc(p, blocks);
    }
    if (params->killer) {
	*p++ = ',';
	p = encode_block_structure_desc(p, kblocks);
	*p++ = ',';
	p = encode_grid(p, kgrid, area);
    }
    assert(p - desc < space);
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    return desc;
}

static void merge_blocks(struct block_structure *b, int n1, int n2)
{
    int i;
    /* Move data towards the lower block number.  */
    if (n2 < n1) {
	int t = n2;
	n2 = n1;
	n1 = t;
    }

    /* Merge n2 into n1, and move the last block into n2's position.  */
    for (i = 0; i < b->nr_squares[n2]; i++)
	b->whichblock[b->blocks[n2][i]] = n1;
    memcpy(b->blocks[n1] + b->nr_squares[n1], b->blocks[n2],
	   b->nr_squares[n2] * sizeof **b->blocks);
    b->nr_squares[n1] += b->nr_squares[n2];

    n1 = b->nr_blocks - 1;
    if (n2 != n1) {
	memcpy(b->blocks[n2], b->blocks[n1],
	       b->nr_squares[n1] * sizeof **b->blocks);
	for (i = 0; i < b->nr_squares[n1]; i++)
	    b->whichblock[b->blocks[n1][i]] = n2;
	b->nr_squares[n2] = b->nr_squares[n1];
    }
    b->nr_blocks = n1;
}

static bool merge_some_cages(struct block_structure *b, int cr, int area,
			     digit *grid, random_state *rs)
{
    /*
     * Make a list of all the pairs of adjacent blocks.
     */
    int i, j, k;
    struct pair {
	int b1, b2;
    } *pairs;
    int npairs;

    pairs = snewn(b->nr_blocks * b->nr_blocks, struct pair);
    npairs = 0;

    for (i = 0; i < b->nr_blocks; i++) {
	for (j = i+1; j < b->nr_blocks; j++) {

	    /*
	     * Rule the merger out of consideration if it's
	     * obviously not viable.
	     */
	    if (b->nr_squares[i] + b->nr_squares[j] > b->max_nr_squares)
		continue;	       /* we couldn't merge these anyway */

	    /*
	     * See if these two blocks have a pair of squares
	     * adjacent to each other.
	     */
	    for (k = 0; k < b->nr_squares[i]; k++) {
		int xy = b->blocks[i][k];
		int y = xy / cr, x = xy % cr;
		if ((y   > 0  && b->whichblock[xy - cr] == j) ||
		    (y+1 < cr && b->whichblock[xy + cr] == j) ||
		    (x   > 0  && b->whichblock[xy -  1] == j) ||
		    (x+1 < cr && b->whichblock[xy +  1] == j)) {
		    /*
		     * Yes! Add this pair to our list.
		     */
		    pairs[npairs].b1 = i;
		    pairs[npairs].b2 = j;
		    break;
		}
	    }
	}
    }

    /*
     * Now go through that list in random order until we find a pair
     * of blocks we can merge.
     */
    while (npairs > 0) {
	int n1, n2;
	unsigned int digits_found;

	/*
	 * Pick a random pair, and remove it from the list.
	 */
	i = random_upto(rs, npairs);
	n1 = pairs[i].b1;
	n2 = pairs[i].b2;
	if (i != npairs-1)
	    pairs[i] = pairs[npairs-1];
	npairs--;

	/* Guarantee that the merged cage would still be a region.  */
	digits_found = 0;
	for (i = 0; i < b->nr_squares[n1]; i++)
	    digits_found |= 1 << grid[b->blocks[n1][i]];
	for (i = 0; i < b->nr_squares[n2]; i++)
	    if (digits_found & (1 << grid[b->blocks[n2][i]]))
		break;
	if (i != b->nr_squares[n2])
	    continue;

	/*
	 * Got one! Do the merge.
	 */
	merge_blocks(b, n1, n2);
	sfree(pairs);
	return true;
    }

    sfree(pairs);
    return false;
}

static void compute_kclues(struct block_structure *cages, digit *kclues,
			   digit *grid, int area)
{
    int i;
    memset(kclues, 0, area * sizeof *kclues);
    for (i = 0; i < cages->nr_blocks; i++) {
	int j, sum = 0;
	for (j = 0; j < area; j++)
	    if (cages->whichblock[j] == i)
		sum += grid[j];
	for (j = 0; j < area; j++)
	    if (cages->whichblock[j] == i)
		break;
	assert (j != area);
	kclues[j] = sum;
    }
}

static struct block_structure *gen_killer_cages(int cr, random_state *rs,
						bool remove_singletons)
{
    int nr;
    int x, y, area = cr * cr;
    int n_singletons = 0;
    struct block_structure *b = alloc_block_structure (1, cr, area, cr, area);

    for (x = 0; x < area; x++)
	b->whichblock[x] = -1;
    nr = 0;
    for (y = 0; y < cr; y++)
	for (x = 0; x < cr; x++) {
	    int rnd;
	    int xy = y*cr+x;
	    if (b->whichblock[xy] != -1)
		continue;
	    b->whichblock[xy] = nr;

	    rnd = random_bits(rs, 4);
	    if (xy + 1 < area && (rnd >= 4 || (!remove_singletons && rnd >= 1))) {
		int xy2 = xy + 1;
		if (x + 1 == cr || b->whichblock[xy2] != -1 ||
		    (xy + cr < area && random_bits(rs, 1) == 0))
		    xy2 = xy + cr;
		if (xy2 >= area)
		    n_singletons++;
		else
		    b->whichblock[xy2] = nr;
	    } else
		n_singletons++;
	    nr++;
	}

    b->nr_blocks = nr;
    make_blocks_from_whichblock(b);

    for (x = y = 0; x < b->nr_blocks; x++)
	if (b->nr_squares[x] == 1)
	    y++;
    assert(y == n_singletons);

    if (n_singletons > 0 && remove_singletons) {
	int n;
	for (n = 0; n < b->nr_blocks;) {
	    int xy, x, y, xy2, other;
	    if (b->nr_squares[n] > 1) {
		n++;
		continue;
	    }
	    xy = b->blocks[n][0];
	    x = xy % cr;
	    y = xy / cr;
	    if (xy + 1 == area)
		xy2 = xy - 1;
	    else if (x + 1 < cr && (y + 1 == cr || random_bits(rs, 1) == 0))
		xy2 = xy + 1;
	    else
		xy2 = xy + cr;
	    other = b->whichblock[xy2];

	    if (b->nr_squares[other] == 1)
		n_singletons--;
	    n_singletons--;
	    merge_blocks(b, n, other);
	    if (n < other)
		n++;
	}
	assert(n_singletons == 0);
    }
    return b;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
    int i;
    int cr = params->c * params->r;
    key_label *keys = snewn(cr+1, key_label);
    *nkeys = cr + 1;

    for (i = 0; i < cr; i++) {
        if (i<9) keys[i].button = '1' + i;
        else keys[i].button = 'a' + i - 9;

        keys[i].label = NULL;
    }
    keys[cr].button = '\b';
    keys[cr].label = NULL;


    return keys;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int c = params->c, r = params->r, cr = c*r;
    int area = cr*cr;
    struct block_structure *blocks, *kblocks;
    digit *grid, *grid2, *kgrid;
    struct xy { int x, y; } *locs;
    int nlocs;
    char *desc;
    int coords[16], ncoords;
    int x, y, i, j;
    struct difficulty dlev;

    precompute_sum_bits();

    /*
     * Adjust the maximum difficulty level to be consistent with
     * the puzzle size: all 2x2 puzzles appear to be Trivial
     * (DIFF_BLOCK) so we cannot hold out for even a Basic
     * (DIFF_SIMPLE) one.
     * Jigsaw puzzles of size 2 and 3 are also all trivial.
     */
    dlev.maxdiff = params->diff;
    dlev.maxkdiff = params->kdiff;
    if ((c == 2 && r == 2) || (r == 1 && c < 4))
        dlev.maxdiff = DIFF_BLOCK;

    grid = snewn(area, digit);
    locs = snewn(area, struct xy);
    grid2 = snewn(area, digit);

    blocks = alloc_block_structure (c, r, area, cr, cr);

    kblocks = NULL;
    kgrid = (params->killer) ? snewn(area, digit) : NULL;

#ifdef STANDALONE_SOLVER
    assert(!"This should never happen, so we don't need to create blocknames");
#endif

    /*
     * Loop until we get a grid of the required difficulty. This is
     * nasty, but it seems to be unpleasantly hard to generate
     * difficult grids otherwise.
     */
    while (1) {
        /*
         * Generate a random solved state, starting by
         * constructing the block structure.
         */
	if (r == 1) {		       /* jigsaw mode */
	    int *dsf = divvy_rectangle(cr, cr, cr, rs);

	    dsf_to_blocks (dsf, blocks, cr, cr);

	    sfree(dsf);
	} else {		       /* basic Sudoku mode */
	    for (y = 0; y < cr; y++)
		for (x = 0; x < cr; x++)
		    blocks->whichblock[y*cr+x] = (y/c) * c + (x/r);
	}
	make_blocks_from_whichblock(blocks);

	if (params->killer) {
            if (kblocks) free_block_structure(kblocks);
	    kblocks = gen_killer_cages(cr, rs, params->kdiff > DIFF_KSINGLE);
	}

        if (!gridgen(cr, blocks, kblocks, params->xtype, grid, rs, area*area))
	    continue;
        assert(check_valid(cr, blocks, kblocks, NULL, params->xtype, grid));

	/*
	 * Save the solved grid in aux.
	 */
	{
	    /*
	     * We might already have written *aux the last time we
	     * went round this loop, in which case we should free
	     * the old aux before overwriting it with the new one.
	     */
            if (*aux) {
		sfree(*aux);
            }

            *aux = encode_solve_move(cr, grid);
	}

	/*
	 * Now we have a solved grid. For normal puzzles, we start removing
	 * things from it while preserving solubility.  Killer puzzles are
	 * different: we just pass the empty grid to the solver, and use
	 * the puzzle if it comes back solved.
	 */

	if (params->killer) {
	    struct block_structure *good_cages = NULL;
	    struct block_structure *last_cages = NULL;
	    int ntries = 0;

            memcpy(grid2, grid, area);

	    for (;;) {
		compute_kclues(kblocks, kgrid, grid2, area);

		memset(grid, 0, area * sizeof *grid);
		solver(cr, blocks, kblocks, params->xtype, grid, kgrid, &dlev);
		if (dlev.diff == dlev.maxdiff && dlev.kdiff == dlev.maxkdiff) {
		    /*
		     * We have one that matches our difficulty.  Store it for
		     * later, but keep going.
		     */
		    if (good_cages)
			free_block_structure(good_cages);
		    ntries = 0;
		    good_cages = dup_block_structure(kblocks);
		    if (!merge_some_cages(kblocks, cr, area, grid2, rs))
			break;
		} else if (dlev.diff > dlev.maxdiff || dlev.kdiff > dlev.maxkdiff) {
		    /*
		     * Give up after too many tries and either use the good one we
		     * found, or generate a new grid.
		     */
		    if (++ntries > 50)
			break;
		    /*
		     * The difficulty level got too high.  If we have a good
		     * one, use it, otherwise go back to the last one that
		     * was at a lower difficulty and restart the process from
		     * there.
		     */
		    if (good_cages != NULL) {
			free_block_structure(kblocks);
			kblocks = dup_block_structure(good_cages);
			if (!merge_some_cages(kblocks, cr, area, grid2, rs))
			    break;
		    } else {
			if (last_cages == NULL)
			    break;
			free_block_structure(kblocks);
			kblocks = last_cages;
			last_cages = NULL;
		    }
		} else {
		    if (last_cages)
			free_block_structure(last_cages);
		    last_cages = dup_block_structure(kblocks);
		    if (!merge_some_cages(kblocks, cr, area, grid2, rs))
			break;
		}
	    }
	    if (last_cages)
		free_block_structure(last_cages);
	    if (good_cages != NULL) {
		free_block_structure(kblocks);
		kblocks = good_cages;
		compute_kclues(kblocks, kgrid, grid2, area);
		memset(grid, 0, area * sizeof *grid);
		break;
	    }
	    continue;
	}

        /*
         * Find the set of equivalence classes of squares permitted
         * by the selected symmetry. We do this by enumerating all
         * the grid squares which have no symmetric companion
         * sorting lower than themselves.
         */
        nlocs = 0;
        for (y = 0; y < cr; y++)
            for (x = 0; x < cr; x++) {
                int i = y*cr+x;
                int j;

                ncoords = symmetries(params, x, y, coords, params->symm);
                for (j = 0; j < ncoords; j++)
                    if (coords[2*j+1]*cr+coords[2*j] < i)
                        break;
                if (j == ncoords) {
                    locs[nlocs].x = x;
                    locs[nlocs].y = y;
                    nlocs++;
                }
            }

        /*
         * Now shuffle that list.
         */
        shuffle(locs, nlocs, sizeof(*locs), rs);

        /*
         * Now loop over the shuffled list and, for each element,
         * see whether removing that element (and its reflections)
         * from the grid will still leave the grid soluble.
         */
        for (i = 0; i < nlocs; i++) {
            x = locs[i].x;
            y = locs[i].y;

            memcpy(grid2, grid, area);
            ncoords = symmetries(params, x, y, coords, params->symm);
            for (j = 0; j < ncoords; j++)
                grid2[coords[2*j+1]*cr+coords[2*j]] = 0;

            solver(cr, blocks, kblocks, params->xtype, grid2, kgrid, &dlev);
            if (dlev.diff <= dlev.maxdiff &&
		(!params->killer || dlev.kdiff <= dlev.maxkdiff)) {
                for (j = 0; j < ncoords; j++)
                    grid[coords[2*j+1]*cr+coords[2*j]] = 0;
            }
        }

        memcpy(grid2, grid, area);

	solver(cr, blocks, kblocks, params->xtype, grid2, kgrid, &dlev);
	if (dlev.diff == dlev.maxdiff &&
	    (!params->killer || dlev.kdiff == dlev.maxkdiff))
	    break;		       /* found one! */
    }

    sfree(grid2);
    sfree(locs);

    /*
     * Now we have the grid as it will be presented to the user.
     * Encode it in a game desc.
     */
    desc = encode_puzzle_desc(params, grid, blocks, kgrid, kblocks);

    sfree(grid);
    free_block_structure(blocks);
    if (params->killer) {
        free_block_structure(kblocks);
        sfree(kgrid);
    }

    return desc;
}

static const char *spec_to_grid(const char *desc, digit *grid, int area)
{
    int i = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
            grid[i++] = atoi(desc-1);
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);
    return desc;
}

/*
 * Create a DSF from a spec found in *pdesc. Update this to point past the
 * end of the block spec, and return an error string or NULL if everything
 * is OK. The DSF is stored in *PDSF.
 */
static const char *spec_to_dsf(const char **pdesc, int **pdsf,
                               int cr, int area)
{
    const char *desc = *pdesc;
    int pos = 0;
    int *dsf;

    *pdsf = dsf = snew_dsf(area);

    while (*desc && *desc != ',') {
	int c;
        bool adv;

	if (*desc == '_')
	    c = 0;
	else if (*desc >= 'a' && *desc <= 'z')
	    c = *desc - 'a' + 1;
	else {
	    sfree(dsf);
	    return "Invalid character in game description";
	}
	desc++;

	adv = (c != 26);	       /* 'z' is a special case */

	while (c-- > 0) {
	    int p0, p1;

	    /*
	     * Non-edge; merge the two dsf classes on either
	     * side of it.
	     */
	    if (pos >= 2*cr*(cr-1)) {
                sfree(dsf);
                return "Too much data in block structure specification";
            }

	    if (pos < cr*(cr-1)) {
		int y = pos/(cr-1);
		int x = pos%(cr-1);
		p0 = y*cr+x;
		p1 = y*cr+x+1;
	    } else {
		int x = pos/(cr-1) - cr;
		int y = pos%(cr-1);
		p0 = y*cr+x;
		p1 = (y+1)*cr+x;
	    }
	    dsf_merge(dsf, p0, p1);

	    pos++;
	}
	if (adv)
	    pos++;
    }
    *pdesc = desc;

    /*
     * When desc is exhausted, we expect to have gone exactly
     * one space _past_ the end of the grid, due to the dummy
     * edge at the end.
     */
    if (pos != 2*cr*(cr-1)+1) {
	sfree(dsf);
	return "Not enough data in block structure specification";
    }

    return NULL;
}

static const char *validate_grid_desc(const char **pdesc, int range, int area)
{
    const char *desc = *pdesc;
    int squares = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            int val = atoi(desc-1);
            if (val < 1 || val > range)
                return "Out-of-range number in game description";
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";
    *pdesc = desc;
    return NULL;
}

static const char *validate_block_desc(const char **pdesc, int cr, int area,
                                       int min_nr_blocks, int max_nr_blocks,
                                       int min_nr_squares, int max_nr_squares)
{
    const char *err;
    int *dsf;

    err = spec_to_dsf(pdesc, &dsf, cr, area);
    if (err) {
	return err;
    }

    if (min_nr_squares == max_nr_squares) {
	assert(min_nr_blocks == max_nr_blocks);
	assert(min_nr_blocks * min_nr_squares == area);
    }
    /*
     * Now we've got our dsf. Verify that it matches
     * expectations.
     */
    {
	int *canons, *counts;
	int i, j, c, ncanons = 0;

	canons = snewn(max_nr_blocks, int);
	counts = snewn(max_nr_blocks, int);

	for (i = 0; i < area; i++) {
	    j = dsf_canonify(dsf, i);

	    for (c = 0; c < ncanons; c++)
		if (canons[c] == j) {
		    counts[c]++;
		    if (counts[c] > max_nr_squares) {
			sfree(dsf);
			sfree(canons);
			sfree(counts);
			return "A jigsaw block is too big";
		    }
		    break;
		}

	    if (c == ncanons) {
		if (ncanons >= max_nr_blocks) {
		    sfree(dsf);
		    sfree(canons);
		    sfree(counts);
		    return "Too many distinct jigsaw blocks";
		}
		canons[ncanons] = j;
		counts[ncanons] = 1;
		ncanons++;
	    }
	}

	if (ncanons < min_nr_blocks) {
	    sfree(dsf);
	    sfree(canons);
	    sfree(counts);
	    return "Not enough distinct jigsaw blocks";
	}
	for (c = 0; c < ncanons; c++) {
	    if (counts[c] < min_nr_squares) {
		sfree(dsf);
		sfree(canons);
		sfree(counts);
		return "A jigsaw block is too small";
	    }
	}
	sfree(canons);
	sfree(counts);
    }

    sfree(dsf);
    return NULL;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int cr = params->c * params->r, area = cr*cr;
    const char *err;

    err = validate_grid_desc(&desc, cr, area);
    if (err)
	return err;

    if (params->r == 1) {
	/*
	 * Now we expect a suffix giving the jigsaw block
	 * structure. Parse it and validate that it divides the
	 * grid into the right number of regions which are the
	 * right size.
	 */
	if (*desc != ',')
	    return "Expected jigsaw block structure in game description";
	desc++;
	err = validate_block_desc(&desc, cr, area, cr, cr, cr, cr);
	if (err)
	    return err;

    }
    if (params->killer) {
	if (*desc != ',')
	    return "Expected killer block structure in game description";
	desc++;
	err = validate_block_desc(&desc, cr, area, cr, area, 2, cr);
	if (err)
	    return err;
	if (*desc != ',')
	    return "Expected killer clue grid in game description";
	desc++;
	err = validate_grid_desc(&desc, cr * area, area);
	if (err)
	    return err;
    }
    if (*desc)
	return "Unexpected data at end of game description";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int c = params->c, r = params->r, cr = c*r, area = cr * cr;
    int i;

    precompute_sum_bits();

    state->cr = cr;
    state->xtype = params->xtype;
    state->killer = params->killer;

    state->grid = snewn(area, digit);
    state->pencil = snewn(area * cr, bool);
    memset(state->pencil, 0, area * cr * sizeof(bool));
    state->immutable = snewn(area, bool);
    memset(state->immutable, 0, area * sizeof(bool));

    state->blocks = alloc_block_structure (c, r, area, cr, cr);

    if (params->killer) {
	state->kblocks = alloc_block_structure (c, r, area, cr, area);
	state->kgrid = snewn(area, digit);
    } else {
	state->kblocks = NULL;
	state->kgrid = NULL;
    }
    state->completed = state->cheated = false;

    desc = spec_to_grid(desc, state->grid, area);
    for (i = 0; i < area; i++)
	if (state->grid[i] != 0)
	    state->immutable[i] = true;

    if (r == 1) {
	const char *err;
	int *dsf;
	assert(*desc == ',');
	desc++;
	err = spec_to_dsf(&desc, &dsf, cr, area);
	assert(err == NULL);
	dsf_to_blocks(dsf, state->blocks, cr, cr);
	sfree(dsf);
    } else {
	int x, y;

	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		state->blocks->whichblock[y*cr+x] = (y/c) * c + (x/r);
    }
    make_blocks_from_whichblock(state->blocks);

    if (params->killer) {
	const char *err;
	int *dsf;
	assert(*desc == ',');
	desc++;
	err = spec_to_dsf(&desc, &dsf, cr, area);
	assert(err == NULL);
	dsf_to_blocks(dsf, state->kblocks, cr, area);
	sfree(dsf);
	make_blocks_from_whichblock(state->kblocks);

	assert(*desc == ',');
	desc++;
	desc = spec_to_grid(desc, state->kgrid, area);
    }
    assert(!*desc);

#ifdef STANDALONE_SOLVER
    /*
     * Set up the block names for solver diagnostic output.
     */
    {
	char *p = (char *)(state->blocks->blocknames + cr);

	if (r == 1) {
	    for (i = 0; i < area; i++) {
		int j = state->blocks->whichblock[i];
		if (!state->blocks->blocknames[j]) {
		    state->blocks->blocknames[j] = p;
		    p += 1 + sprintf(p, "starting at (%d,%d)",
				     1 + i%cr, 1 + i/cr);
		}
	    }
	} else {
	    int bx, by;
	    for (by = 0; by < r; by++)
		for (bx = 0; bx < c; bx++) {
		    state->blocks->blocknames[by*c+bx] = p;
		    p += 1 + sprintf(p, "(%d,%d)", bx+1, by+1);
		}
	}
	assert(p - (char *)state->blocks->blocknames < (int)(cr*(sizeof(char *)+80)));
	for (i = 0; i < cr; i++)
	    assert(state->blocks->blocknames[i]);
    }
#endif

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int cr = state->cr, area = cr * cr;

    ret->cr = state->cr;
    ret->xtype = state->xtype;
    ret->killer = state->killer;

    ret->blocks = state->blocks;
    ret->blocks->refcount++;

    ret->kblocks = state->kblocks;
    if (ret->kblocks)
	ret->kblocks->refcount++;

    ret->grid = snewn(area, digit);
    memcpy(ret->grid, state->grid, area);

    if (state->killer) {
	ret->kgrid = snewn(area, digit);
	memcpy(ret->kgrid, state->kgrid, area);
    } else
	ret->kgrid = NULL;

    ret->pencil = snewn(area * cr, bool);
    memcpy(ret->pencil, state->pencil, area * cr * sizeof(bool));

    ret->immutable = snewn(area, bool);
    memcpy(ret->immutable, state->immutable, area * sizeof(bool));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    free_block_structure(state->blocks);
    if (state->kblocks)
	free_block_structure(state->kblocks);

    sfree(state->immutable);
    sfree(state->pencil);
    sfree(state->grid);
    if (state->kgrid) sfree(state->kgrid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *ai, const char **error)
{
    int cr = state->cr;
    char *ret;
    digit *grid;
    struct difficulty dlev;

    /*
     * If we already have the solution in ai, save ourselves some
     * time.
     */
    if (ai)
        return dupstr(ai);

    grid = snewn(cr*cr, digit);
    memcpy(grid, state->grid, cr*cr);
    dlev.maxdiff = DIFF_RECURSIVE;
    dlev.maxkdiff = DIFF_KINTERSECT;
    solver(cr, state->blocks, state->kblocks, state->xtype, grid,
	   state->kgrid, &dlev);

    *error = NULL;

    if (dlev.diff == DIFF_IMPOSSIBLE)
	*error = "No solution exists for this puzzle";
    else if (dlev.diff == DIFF_AMBIGUOUS)
	*error = "Multiple solutions exist for this puzzle";

    if (*error) {
        sfree(grid);
        return NULL;
    }

    ret = encode_solve_move(cr, grid);

    sfree(grid);

    return ret;
}

static char *grid_text_format(int cr, struct block_structure *blocks,
			      bool xtype, digit *grid)
{
    int vmod, hmod;
    int x, y;
    int totallen, linelen, nlines;
    char *ret, *p, ch;

    /*
     * For non-jigsaw Sudoku, we format in the way we always have,
     * by having the digits unevenly spaced so that the dividing
     * lines can fit in:
     *
     * . . | . .
     * . . | . .
     * ----+----
     * . . | . .
     * . . | . .
     *
     * For jigsaw puzzles, however, we must leave space between
     * _all_ pairs of digits for an optional dividing line, so we
     * have to move to the rather ugly
     * 
     * .   .   .   .
     * ------+------
     * .   . | .   .
     *       +---+  
     * .   . | . | .
     * ------+   |  
     * .   .   . | .
     * 
     * We deal with both cases using the same formatting code; we
     * simply invent a vmod value such that there's a vertical
     * dividing line before column i iff i is divisible by vmod
     * (so it's r in the first case and 1 in the second), and hmod
     * likewise for horizontal dividing lines.
     */

    if (blocks->r != 1) {
	vmod = blocks->r;
	hmod = blocks->c;
    } else {
	vmod = hmod = 1;
    }

    /*
     * Line length: we have cr digits, each with a space after it,
     * and (cr-1)/vmod dividing lines, each with a space after it.
     * The final space is replaced by a newline, but that doesn't
     * affect the length.
     */
    linelen = 2*(cr + (cr-1)/vmod);

    /*
     * Number of lines: we have cr rows of digits, and (cr-1)/hmod
     * dividing rows.
     */
    nlines = cr + (cr-1)/hmod;

    /*
     * Allocate the space.
     */
    totallen = linelen * nlines;
    ret = snewn(totallen+1, char);     /* leave room for terminating NUL */

    /*
     * Write the text.
     */
    p = ret;
    for (y = 0; y < cr; y++) {
	/*
	 * Row of digits.
	 */
	for (x = 0; x < cr; x++) {
	    /*
	     * Digit.
	     */
	    digit d = grid[y*cr+x];

            if (d == 0) {
		/*
		 * Empty space: we usually write a dot, but we'll
		 * highlight spaces on the X-diagonals (in X mode)
		 * by using underscores instead.
		 */
		if (xtype && (ondiag0(y*cr+x) || ondiag1(y*cr+x)))
		    ch = '_';
		else
		    ch = '.';
	    } else if (d <= 9) {
                ch = '0' + d;
	    } else {
                ch = 'a' + d-10;
	    }

	    *p++ = ch;
	    if (x == cr-1) {
		*p++ = '\n';
		continue;
	    }
	    *p++ = ' ';

	    if ((x+1) % vmod)
		continue;

	    /*
	     * Optional dividing line.
	     */
	    if (blocks->whichblock[y*cr+x] != blocks->whichblock[y*cr+x+1])
		ch = '|';
	    else
		ch = ' ';
	    *p++ = ch;
	    *p++ = ' ';
	}
	if (y == cr-1 || (y+1) % hmod)
	    continue;

	/*
	 * Dividing row.
	 */
	for (x = 0; x < cr; x++) {
	    int dwid;
	    int tl, tr, bl, br;

	    /*
	     * Division between two squares. This varies
	     * complicatedly in length.
	     */
	    dwid = 2;		       /* digit and its following space */
	    if (x == cr-1)
		dwid--;		       /* no following space at end of line */
	    if (x > 0 && x % vmod == 0)
		dwid++;		       /* preceding space after a divider */

	    if (blocks->whichblock[y*cr+x] != blocks->whichblock[(y+1)*cr+x])
		ch = '-';
	    else
		ch = ' ';

	    while (dwid-- > 0)
		*p++ = ch;

	    if (x == cr-1) {
		*p++ = '\n';
		break;
	    }

	    if ((x+1) % vmod)
		continue;

	    /*
	     * Corner square. This is:
	     * 	- a space if all four surrounding squares are in
	     * 	  the same block
	     * 	- a vertical line if the two left ones are in one
	     * 	  block and the two right in another
	     * 	- a horizontal line if the two top ones are in one
	     * 	  block and the two bottom in another
	     * 	- a plus sign in all other cases. (If we had a
	     * 	  richer character set available we could break
	     * 	  this case up further by doing fun things with
	     * 	  line-drawing T-pieces.)
	     */
	    tl = blocks->whichblock[y*cr+x];
	    tr = blocks->whichblock[y*cr+x+1];
	    bl = blocks->whichblock[(y+1)*cr+x];
	    br = blocks->whichblock[(y+1)*cr+x+1];

	    if (tl == tr && tr == bl && bl == br)
		ch = ' ';
	    else if (tl == bl && tr == br)
		ch = '|';
	    else if (tl == tr && bl == br)
		ch = '-';
	    else
		ch = '+';

	    *p++ = ch;
	}
    }

    assert(p - ret == totallen);
    *p = '\0';
    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    /*
     * Formatting Killer puzzles as text is currently unsupported. I
     * can't think of any sensible way of doing it which doesn't
     * involve expanding the puzzle to such a large scale as to make
     * it unusable.
     */
    if (params->killer)
        return false;
    return true;
}

static char *game_text_format(const game_state *state)
{
    assert(!state->kblocks);
    return grid_text_format(state->cr, state->blocks, state->xtype,
			    state->grid);
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, if hshow = 1.
     */
    int hx, hy;
    /*
     * This indicates whether the current highlight is a
     * pencil-mark one or a real one.
     */
    bool hpencil;
    /*
     * This indicates whether or not we're showing the highlight
     * (used to be hx = hy = -1); important so that when we're
     * using the cursor keys it doesn't keep coming back at a
     * fixed position. When hshow is true, pressing a valid number
     * or letter key or Space will enter that number or letter in the grid.
     */
    bool hshow;
    /*
     * This indicates whether we're using the highlight as a cursor;
     * it means that it doesn't vanish on a keypress, and that it is
     * allowed on immutable squares.
     */
    bool hcursor;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = 0;
    ui->hpencil = false;
    ui->hshow = ui->hcursor = getenv_bool("PUZZLES_SHOW_CURSOR", false);

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
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
    int cr = newstate->cr;
    /*
     * We prevent pencil-mode highlighting of a filled square, unless
     * we're using the cursor keys. So if the user has just filled in
     * a square which we had a pencil-mode highlight in (by Undo, or
     * by Redo, or by Solve), then we cancel the highlight.
     */
    if (ui->hshow && ui->hpencil && !ui->hcursor &&
        newstate->grid[ui->hy * cr + ui->hx] != 0) {
        ui->hshow = false;
    }
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->hshow && (button == CURSOR_SELECT))
        return ui->hpencil ? "Ink" : "Pencil";
    return "";
}

struct game_drawstate {
    bool started, xtype;
    int cr;
    int tilesize;
    digit *grid;
    unsigned char *pencil;
    unsigned char *hl;
    /* This is scratch space used within a single call to game_redraw. */
    int nregions, *entered_items;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int cr = state->cr;
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = (x + TILE_SIZE - BORDER) / TILE_SIZE - 1;
    ty = (y + TILE_SIZE - BORDER) / TILE_SIZE - 1;

    if (tx >= 0 && tx < cr && ty >= 0 && ty < cr) {
        if (button == LEFT_BUTTON) {
            if (state->immutable[ty*cr+tx]) {
                ui->hshow = false;
            } else if (tx == ui->hx && ty == ui->hy &&
                       ui->hshow && !ui->hpencil) {
                ui->hshow = false;
            } else {
                ui->hx = tx;
                ui->hy = ty;
                ui->hshow = true;
                ui->hpencil = false;
            }
            ui->hcursor = false;
            return UI_UPDATE;
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*cr+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy &&
                    ui->hshow && ui->hpencil) {
                    ui->hshow = false;
                } else {
                    ui->hpencil = true;
                    ui->hx = tx;
                    ui->hy = ty;
                    ui->hshow = true;
                }
            } else {
                ui->hshow = false;
            }
            ui->hcursor = false;
            return UI_UPDATE;
        }
    }
    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->hx, &ui->hy, cr, cr, false);
        ui->hshow = true;
        ui->hcursor = true;
        return UI_UPDATE;
    }
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil = !ui->hpencil;
        ui->hcursor = true;
        return UI_UPDATE;
    }

    if (ui->hshow &&
	((button >= '0' && button <= '9' && button - '0' <= cr) ||
	 (button >= 'a' && button <= 'z' && button - 'a' + 10 <= cr) ||
	 (button >= 'A' && button <= 'Z' && button - 'A' + 10 <= cr) ||
	 button == CURSOR_SELECT2 || button == '\b')) {
	int n = button - '0';
	if (button >= 'A' && button <= 'Z')
	    n = button - 'A' + 10;
	if (button >= 'a' && button <= 'z')
	    n = button - 'a' + 10;
	if (button == CURSOR_SELECT2 || button == '\b')
	    n = 0;

        /*
         * Can't overwrite this square. This can only happen here
         * if we're using the cursor keys.
         */
	if (state->immutable[ui->hy*cr+ui->hx])
	    return NULL;

        /*
         * Can't make pencil marks in a filled square. Again, this
         * can only become highlighted if we're using cursor keys.
         */
        if (ui->hpencil && state->grid[ui->hy*cr+ui->hx])
            return NULL;

        /*
         * If you ask to fill a square with what it already contains,
         * or blank it when it's already empty, that has no effect...
         */
        if ((!ui->hpencil || n == 0) && state->grid[ui->hy*cr+ui->hx] == n) {
            bool anypencil = false;
            int i;
            for (i = 0; i < cr; i++)
                anypencil = anypencil ||
                    state->pencil[(ui->hy*cr+ui->hx) * cr + i];
            if (!anypencil) {
                /* ... expect to remove the cursor in mouse mode. */
                if (!ui->hcursor) {
                    ui->hshow = false;
                    return UI_UPDATE;
                }
                return NULL;
            }
        }

	sprintf(buf, "%c%d,%d,%d",
		(char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        if (!ui->hcursor) ui->hshow = false;

	return dupstr(buf);
    }

    if (button == 'M' || button == 'm')
        return dupstr("M");

    return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int cr = from->cr;
    game_state *ret;
    int x, y, n;

    if (move[0] == 'S') {
	const char *p;

	ret = dup_game(from);
	ret->completed = ret->cheated = true;

	p = move+1;
	for (n = 0; n < cr*cr; n++) {
	    ret->grid[n] = atoi(p);

	    if (!*p || ret->grid[n] < 1 || ret->grid[n] > cr) {
		free_game(ret);
		return NULL;
	    }

	    while (*p && isdigit((unsigned char)*p)) p++;
	    if (*p == ',') p++;
	}

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < cr && y >= 0 && y < cr && n >= 0 && n <= cr) {

	ret = dup_game(from);
        if (move[0] == 'P' && n > 0) {
            int index = (y*cr+x) * cr + (n-1);
            ret->pencil[index] = !ret->pencil[index];
        } else {
            ret->grid[y*cr+x] = n;
            memset(ret->pencil + (y*cr+x)*cr, 0, cr);

            /*
             * We've made a real change to the grid. Check to see
             * if the game has been completed.
             */
            if (!ret->completed && check_valid(
                    cr, ret->blocks, ret->kblocks, ret->kgrid,
                    ret->xtype, ret->grid)) {
                ret->completed = true;
            }
        }
	return ret;
    } else if (move[0] == 'M') {
	/*
	 * Fill in absolutely all pencil marks in unfilled squares,
	 * for those who like to play by the rigorous approach of
	 * starting off in that state and eliminating things.
	 */
	ret = dup_game(from);
        for (y = 0; y < cr; y++) {
            for (x = 0; x < cr; x++) {
                if (!ret->grid[y*cr+x]) {
                    int i;
                    for (i = 0; i < cr; i++)
                        ret->pencil[(y*cr+x)*cr + i] = true;
                }
            }
        }
	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define SIZE(cr) ((cr) * TILE_SIZE + 2*BORDER + 1)
#define GETTILESIZE(cr, w) ( (double)(w-1) / (double)(cr+1) )

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = SIZE(params->c * params->r);
    *y = SIZE(params->c * params->r);
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

    ret[COL_XDIAGONALS * 3 + 0] = 0.9F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_XDIAGONALS * 3 + 1] = 0.9F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_XDIAGONALS * 3 + 2] = 0.9F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_CLUE * 3 + 0] = 0.0F;
    ret[COL_CLUE * 3 + 1] = 0.0F;
    ret[COL_CLUE * 3 + 2] = 0.0F;

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    ret[COL_KILLER * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_KILLER * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_KILLER * 3 + 2] = 0.1F * ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int cr = state->cr;

    ds->started = false;
    ds->cr = cr;
    ds->xtype = state->xtype;
    ds->grid = snewn(cr*cr, digit);
    memset(ds->grid, cr+2, cr*cr);
    ds->pencil = snewn(cr*cr*cr, digit);
    memset(ds->pencil, 0, cr*cr*cr);
    ds->hl = snewn(cr*cr, unsigned char);
    memset(ds->hl, 0, cr*cr);
    /*
     * ds->entered_items needs one row of cr entries per entity in
     * which digits may not be duplicated. That's one for each row,
     * each column, each block, each diagonal, and each Killer cage.
     */
    ds->nregions = cr*3 + 2;
    if (state->kblocks)
	ds->nregions += state->kblocks->nr_blocks;
    ds->entered_items = snewn(cr * ds->nregions, int);
    ds->tilesize = 0;                  /* not decided yet */
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->hl);
    sfree(ds->pencil);
    sfree(ds->grid);
    sfree(ds->entered_items);
    sfree(ds);
}

static void draw_number(drawing *dr, game_drawstate *ds,
                        const game_state *state, int x, int y, int hl)
{
    int cr = state->cr;
    int tx, ty, tw, th;
    int cx, cy, cw, ch;
    int col_killer = (hl & 32 ? COL_ERROR : COL_KILLER);
    char str[20];

    if (ds->grid[y*cr+x] == state->grid[y*cr+x] &&
        ds->hl[y*cr+x] == hl &&
        !memcmp(ds->pencil+(y*cr+x)*cr, state->pencil+(y*cr+x)*cr, cr))
	return;			       /* no change required */

    tx = BORDER + x * TILE_SIZE + 1 + GRIDEXTRA;
    ty = BORDER + y * TILE_SIZE + 1 + GRIDEXTRA;

    cx = tx;
    cy = ty;
    cw = tw = TILE_SIZE-1-2*GRIDEXTRA;
    ch = th = TILE_SIZE-1-2*GRIDEXTRA;

    if (x > 0 && state->blocks->whichblock[y*cr+x] == state->blocks->whichblock[y*cr+x-1])
	cx -= GRIDEXTRA, cw += GRIDEXTRA;
    if (x+1 < cr && state->blocks->whichblock[y*cr+x] == state->blocks->whichblock[y*cr+x+1])
	cw += GRIDEXTRA;
    if (y > 0 && state->blocks->whichblock[y*cr+x] == state->blocks->whichblock[(y-1)*cr+x])
	cy -= GRIDEXTRA, ch += GRIDEXTRA;
    if (y+1 < cr && state->blocks->whichblock[y*cr+x] == state->blocks->whichblock[(y+1)*cr+x])
	ch += GRIDEXTRA;

    clip(dr, cx, cy, cw, ch);

    /* background needs erasing */
    draw_rect(dr, cx, cy, cw, ch,
	      ((hl & 15) == 1 ? COL_HIGHLIGHT :
	       (ds->xtype && (ondiag0(y*cr+x) || ondiag1(y*cr+x))) ? COL_XDIAGONALS :
	       COL_BACKGROUND));

    /*
     * Draw the corners of thick lines in corner-adjacent squares,
     * which jut into this square by one pixel.
     */
    if (x > 0 && y > 0 && state->blocks->whichblock[y*cr+x] != state->blocks->whichblock[(y-1)*cr+x-1])
	draw_rect(dr, tx-GRIDEXTRA, ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x+1 < cr && y > 0 && state->blocks->whichblock[y*cr+x] != state->blocks->whichblock[(y-1)*cr+x+1])
	draw_rect(dr, tx+TILE_SIZE-1-2*GRIDEXTRA, ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x > 0 && y+1 < cr && state->blocks->whichblock[y*cr+x] != state->blocks->whichblock[(y+1)*cr+x-1])
	draw_rect(dr, tx-GRIDEXTRA, ty+TILE_SIZE-1-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x+1 < cr && y+1 < cr && state->blocks->whichblock[y*cr+x] != state->blocks->whichblock[(y+1)*cr+x+1])
	draw_rect(dr, tx+TILE_SIZE-1-2*GRIDEXTRA, ty+TILE_SIZE-1-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);

    /* pencil-mode highlight */
    if ((hl & 15) == 2) {
        int coords[6];
        coords[0] = cx;
        coords[1] = cy;
        coords[2] = cx+cw/2;
        coords[3] = cy;
        coords[4] = cx;
        coords[5] = cy+ch/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    if (state->kblocks) {
	int t = GRIDEXTRA * 3;
	int kcx, kcy, kcw, kch;
	int kl, kt, kr, kb;
	bool has_left = false, has_right = false;
        bool has_top = false, has_bottom = false;

	/*
	 * In non-jigsaw mode, the Killer cages are placed at a
	 * fixed offset from the outer edge of the cell dividing
	 * lines, so that they look right whether those lines are
	 * thick or thin. In jigsaw mode, however, doing this will
	 * sometimes cause the cage outlines in adjacent squares to
	 * fail to match up with each other, so we must offset a
	 * fixed amount from the _centre_ of the cell dividing
	 * lines.
	 */
	if (state->blocks->r == 1) {
	    kcx = tx;
	    kcy = ty;
	    kcw = tw;
	    kch = th;
	} else {
	    kcx = cx;
	    kcy = cy;
	    kcw = cw;
	    kch = ch;
	}
	kl = kcx - 1;
	kt = kcy - 1;
	kr = kcx + kcw;
	kb = kcy + kch;

	/*
	 * First, draw the lines dividing this area from neighbouring
	 * different areas.
	 */
	if (x == 0 || state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[y*cr+x-1])
	    has_left = true, kl += t;
	if (x+1 >= cr || state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[y*cr+x+1])
	    has_right = true, kr -= t;
	if (y == 0 || state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y-1)*cr+x])
	    has_top = true, kt += t;
	if (y+1 >= cr || state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y+1)*cr+x])
	    has_bottom = true, kb -= t;
	if (has_top)
	    draw_line(dr, kl, kt, kr, kt, col_killer);
	if (has_bottom)
	    draw_line(dr, kl, kb, kr, kb, col_killer);
	if (has_left)
	    draw_line(dr, kl, kt, kl, kb, col_killer);
	if (has_right)
	    draw_line(dr, kr, kt, kr, kb, col_killer);
	/*
	 * Now, take care of the corners (just as for the normal borders).
	 * We only need a corner if there wasn't a full edge.
	 */
	if (x > 0 && y > 0 && !has_left && !has_top
	    && state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y-1)*cr+x-1])
	{
	    draw_line(dr, kl, kt + t, kl + t, kt + t, col_killer);
	    draw_line(dr, kl + t, kt, kl + t, kt + t, col_killer);
	}
	if (x+1 < cr && y > 0 && !has_right && !has_top
	    && state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y-1)*cr+x+1])
	{
	    draw_line(dr, kcx + kcw - t, kt + t, kcx + kcw, kt + t, col_killer);
	    draw_line(dr, kcx + kcw - t, kt, kcx + kcw - t, kt + t, col_killer);
	}
	if (x > 0 && y+1 < cr && !has_left && !has_bottom
	    && state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y+1)*cr+x-1])
	{
	    draw_line(dr, kl, kcy + kch - t, kl + t, kcy + kch - t, col_killer);
	    draw_line(dr, kl + t, kcy + kch - t, kl + t, kcy + kch, col_killer);
	}
	if (x+1 < cr && y+1 < cr && !has_right && !has_bottom
	    && state->kblocks->whichblock[y*cr+x] != state->kblocks->whichblock[(y+1)*cr+x+1])
	{
	    draw_line(dr, kcx + kcw - t, kcy + kch - t, kcx + kcw - t, kcy + kch, col_killer);
	    draw_line(dr, kcx + kcw - t, kcy + kch - t, kcx + kcw, kcy + kch - t, col_killer);
	}

    }

    if (state->killer && state->kgrid[y*cr+x]) {
	sprintf (str, "%d", state->kgrid[y*cr+x]);
	draw_text(dr, tx + GRIDEXTRA * 4, ty + GRIDEXTRA * 4 + TILE_SIZE/4,
		  FONT_VARIABLE, TILE_SIZE/4, ALIGN_VNORMAL | ALIGN_HLEFT,
		  col_killer, str);
    }

    /* new number needs drawing? */
    if (state->grid[y*cr+x]) {
	str[1] = '\0';
	str[0] = state->grid[y*cr+x] + '0';
	if (str[0] > '9')
	    str[0] += 'a' - ('9'+1);
	draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
		  FONT_VARIABLE, TILE_SIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  state->immutable[y*cr+x] ? COL_CLUE : (hl & 16) ? COL_ERROR : COL_USER, str);
    } else {
        int i, j, npencil;
	int pl, pr, pt, pb;
	float bestsize;
	int pw, ph, minph, pbest, fontsize;

        /* Count the pencil marks required. */
        for (i = npencil = 0; i < cr; i++)
            if (state->pencil[(y*cr+x)*cr+i])
		npencil++;
	if (npencil) {

	    minph = 2;

	    /*
	     * Determine the bounding rectangle within which we're going
	     * to put the pencil marks.
	     */
	    /* Start with the whole square */
	    pl = tx + GRIDEXTRA;
	    pr = pl + TILE_SIZE - GRIDEXTRA;
	    pt = ty + GRIDEXTRA;
	    pb = pt + TILE_SIZE - GRIDEXTRA;
	    if (state->killer) {
		/*
		 * Make space for the Killer cages. We do this
		 * unconditionally, for uniformity between squares,
		 * rather than making it depend on whether a Killer
		 * cage edge is actually present on any given side.
		 */
		pl += GRIDEXTRA * 3;
		pr -= GRIDEXTRA * 3;
		pt += GRIDEXTRA * 3;
		pb -= GRIDEXTRA * 3;
		if (state->kgrid[y*cr+x] != 0) {
		    /* Make further space for the Killer number. */
		    pt += TILE_SIZE/4;
		    /* minph--; */
		}
	    }

	    /*
	     * We arrange our pencil marks in a grid layout, with
	     * the number of rows and columns adjusted to allow the
	     * maximum font size.
	     *
	     * So now we work out what the grid size ought to be.
	     */
	    bestsize = 0.0;
	    pbest = 0;
	    /* Minimum */
	    for (pw = 3; pw < max(npencil,4); pw++) {
		float fw, fh, fs;

		ph = (npencil + pw - 1) / pw;
		ph = max(ph, minph);
		fw = (pr - pl) / (float)pw;
		fh = (pb - pt) / (float)ph;
		fs = min(fw, fh);
		if (fs >= bestsize) {
		    bestsize = fs;
		    pbest = pw;
		}
	    }
	    assert(pbest > 0);
	    pw = pbest;
	    ph = (npencil + pw - 1) / pw;
	    ph = max(ph, minph);

	    /*
	     * Now we've got our grid dimensions, work out the pixel
	     * size of a grid element, and round it to the nearest
	     * pixel. (We don't want rounding errors to make the
	     * grid look uneven at low pixel sizes.)
	     */
	    fontsize = min((pr - pl) / pw, (pb - pt) / ph);

	    /*
	     * Centre the resulting figure in the square.
	     */
	    pl = tx + (TILE_SIZE - fontsize * pw) / 2;
	    pt = ty + (TILE_SIZE - fontsize * ph) / 2;

	    /*
	     * And move it down a bit if it's collided with the
	     * Killer cage number.
	     */
	    if (state->killer && state->kgrid[y*cr+x] != 0) {
		pt = max(pt, ty + GRIDEXTRA * 3 + TILE_SIZE/4);
	    }

	    /*
	     * Now actually draw the pencil marks.
	     */
	    for (i = j = 0; i < cr; i++)
		if (state->pencil[(y*cr+x)*cr+i]) {
		    int dx = j % pw, dy = j / pw;

		    str[1] = '\0';
		    str[0] = i + '1';
		    if (str[0] > '9')
			str[0] += 'a' - ('9'+1);
		    draw_text(dr, pl + fontsize * (2*dx+1) / 2,
			      pt + fontsize * (2*dy+1) / 2,
			      FONT_VARIABLE, fontsize,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
		    j++;
		}
	}
    }

    unclip(dr);

    draw_update(dr, cx, cy, cw, ch);

    ds->grid[y*cr+x] = state->grid[y*cr+x];
    memcpy(ds->pencil+(y*cr+x)*cr, state->pencil+(y*cr+x)*cr, cr);
    ds->hl[y*cr+x] = hl;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int cr = state->cr;
    int x, y;

    if (!ds->started) {
	/*
	 * Draw the grid. We draw it as a big thick rectangle of
	 * COL_GRID initially; individual calls to draw_number()
	 * will poke the right-shaped holes in it.
	 */
	draw_rect(dr, BORDER-GRIDEXTRA, BORDER-GRIDEXTRA,
		  cr*TILE_SIZE+1+2*GRIDEXTRA, cr*TILE_SIZE+1+2*GRIDEXTRA,
		  COL_GRID);
    }

    /*
     * This array is used to keep track of rows, columns and boxes
     * which contain a number more than once.
     */
    for (x = 0; x < cr * ds->nregions; x++)
	ds->entered_items[x] = 0;
    for (x = 0; x < cr; x++)
	for (y = 0; y < cr; y++) {
	    digit d = state->grid[y*cr+x];
	    if (d) {
		int box, kbox;

		/* Rows */
 		ds->entered_items[x*cr+d-1]++;

		/* Columns */
		ds->entered_items[(y+cr)*cr+d-1]++;

		/* Blocks */
		box = state->blocks->whichblock[y*cr+x];
		ds->entered_items[(box+2*cr)*cr+d-1]++;

		/* Diagonals */
		if (ds->xtype) {
		    if (ondiag0(y*cr+x))
			ds->entered_items[(3*cr)*cr+d-1]++;
		    if (ondiag1(y*cr+x))
			ds->entered_items[(3*cr+1)*cr+d-1]++;
		}

		/* Killer cages */
		if (state->kblocks) {
		    kbox = state->kblocks->whichblock[y*cr+x];
		    ds->entered_items[(kbox+3*cr+2)*cr+d-1]++;
		}
	    }
	}

    /*
     * Draw any numbers which need redrawing.
     */
    for (x = 0; x < cr; x++) {
	for (y = 0; y < cr; y++) {
            int highlight = 0;
            digit d = state->grid[y*cr+x];

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                highlight = 1;

            /* Highlight active input areas. */
            if (x == ui->hx && y == ui->hy && ui->hshow)
                highlight = ui->hpencil ? 2 : 1;

	    /* Mark obvious errors (ie, numbers which occur more than once
	     * in a single row, column, or box). */
	    if (d && (ds->entered_items[x*cr+d-1] > 1 ||
		      ds->entered_items[(y+cr)*cr+d-1] > 1 ||
		      ds->entered_items[(state->blocks->whichblock[y*cr+x]
					 +2*cr)*cr+d-1] > 1 ||
		      (ds->xtype && ((ondiag0(y*cr+x) &&
				      ds->entered_items[(3*cr)*cr+d-1] > 1) ||
				     (ondiag1(y*cr+x) &&
				      ds->entered_items[(3*cr+1)*cr+d-1]>1)))||
		      (state->kblocks &&
		       ds->entered_items[(state->kblocks->whichblock[y*cr+x]
					  +3*cr+2)*cr+d-1] > 1)))
		highlight |= 16;

	    if (d && state->kblocks) {
                if (check_killer_cage_sum(
                        state->kblocks, state->kgrid, state->grid,
                        state->kblocks->whichblock[y*cr+x]) == 0)
                    highlight |= 32;
	    }

	    draw_number(dr, ds, state, x, y, highlight);
	}
    }

    /*
     * Update the _entire_ grid if necessary.
     */
    if (!ds->started) {
	draw_update(dr, 0, 0, SIZE(cr), SIZE(cr));
	ds->started = true;
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->hshow) {
        *x = BORDER + ui->hx * TILE_SIZE + 1 + GRIDEXTRA;
        *y = BORDER + ui->hy * TILE_SIZE + 1 + GRIDEXTRA;
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 9mm squares by default. They should be quite big
     * for this game, because players will want to jot down no end
     * of pencil marks in the squares.
     */
    game_compute_size(params, 900, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

/*
 * Subfunction to draw the thick lines between cells. In order to do
 * this using the line-drawing rather than rectangle-drawing API (so
 * as to get line thicknesses to scale correctly) and yet have
 * correctly mitred joins between lines, we must do this by tracing
 * the boundary of each sub-block and drawing it in one go as a
 * single polygon.
 *
 * This subfunction is also reused with thinner dotted lines to
 * outline the Killer cages, this time offsetting the outline toward
 * the interior of the affected squares.
 */
static void outline_block_structure(drawing *dr, game_drawstate *ds,
				    const game_state *state,
				    struct block_structure *blocks,
				    int ink, int inset)
{
    int cr = state->cr;
    int *coords;
    int bi, i, n;
    int x, y, dx, dy, sx, sy, sdx, sdy;

    /*
     * Maximum perimeter of a k-omino is 2k+2. (Proof: start
     * with k unconnected squares, with total perimeter 4k.
     * Now repeatedly join two disconnected components
     * together into a larger one; every time you do so you
     * remove at least two unit edges, and you require k-1 of
     * these operations to create a single connected piece, so
     * you must have at most 4k-2(k-1) = 2k+2 unit edges left
     * afterwards.)
     */
    coords = snewn(4*cr+4, int);   /* 2k+2 points, 2 coords per point */

    /*
     * Iterate over all the blocks.
     */
    for (bi = 0; bi < blocks->nr_blocks; bi++) {
	if (blocks->nr_squares[bi] == 0)
	    continue;

	/*
	 * For each block, find a starting square within it
	 * which has a boundary at the left.
	 */
	for (i = 0; i < cr; i++) {
	    int j = blocks->blocks[bi][i];
	    if (j % cr == 0 || blocks->whichblock[j-1] != bi)
		break;
	}
	assert(i < cr); /* every block must have _some_ leftmost square */
	x = blocks->blocks[bi][i] % cr;
	y = blocks->blocks[bi][i] / cr;
	dx = -1;
	dy = 0;

	/*
	 * Now begin tracing round the perimeter. At all
	 * times, (x,y) describes some square within the
	 * block, and (x+dx,y+dy) is some adjacent square
	 * outside it; so the edge between those two squares
	 * is always an edge of the block.
	 */
	sx = x, sy = y, sdx = dx, sdy = dy;   /* save starting position */
	n = 0;
	do {
	    int cx, cy, tx, ty, nin;

	    /*
	     * Advance to the next edge, by looking at the two
	     * squares beyond it. If they're both outside the block,
	     * we turn right (by leaving x,y the same and rotating
	     * dx,dy clockwise); if they're both inside, we turn
	     * left (by rotating dx,dy anticlockwise and contriving
	     * to leave x+dx,y+dy unchanged); if one of each, we go
	     * straight on (and may enforce by assertion that
	     * they're one of each the _right_ way round).
	     */
	    nin = 0;
	    tx = x - dy + dx;
	    ty = y + dx + dy;
	    nin += (tx >= 0 && tx < cr && ty >= 0 && ty < cr &&
		    blocks->whichblock[ty*cr+tx] == bi);
	    tx = x - dy;
	    ty = y + dx;
	    nin += (tx >= 0 && tx < cr && ty >= 0 && ty < cr &&
		    blocks->whichblock[ty*cr+tx] == bi);
	    if (nin == 0) {
		/*
		 * Turn right.
		 */
		int tmp;
		tmp = dx;
		dx = -dy;
		dy = tmp;
	    } else if (nin == 2) {
		/*
		 * Turn left.
		 */
		int tmp;

		x += dx;
		y += dy;

		tmp = dx;
		dx = dy;
		dy = -tmp;

		x -= dx;
		y -= dy;
	    } else {
		/*
		 * Go straight on.
		 */
		x -= dy;
		y += dx;
	    }

	    /*
	     * Now enforce by assertion that we ended up
	     * somewhere sensible.
	     */
	    assert(x >= 0 && x < cr && y >= 0 && y < cr &&
		   blocks->whichblock[y*cr+x] == bi);
	    assert(x+dx < 0 || x+dx >= cr || y+dy < 0 || y+dy >= cr ||
		   blocks->whichblock[(y+dy)*cr+(x+dx)] != bi);

	    /*
	     * Record the point we just went past at one end of the
	     * edge. To do this, we translate (x,y) down and right
	     * by half a unit (so they're describing a point in the
	     * _centre_ of the square) and then translate back again
	     * in a manner rotated by dy and dx.
	     */
	    assert(n < 2*cr+2);
	    cx = ((2*x+1) + dy + dx) / 2;
	    cy = ((2*y+1) - dx + dy) / 2;
	    coords[2*n+0] = BORDER + cx * TILE_SIZE;
	    coords[2*n+1] = BORDER + cy * TILE_SIZE;
	    coords[2*n+0] -= dx * inset;
	    coords[2*n+1] -= dy * inset;
	    if (nin == 0) {
		/*
		 * We turned right, so inset this corner back along
		 * the edge towards the centre of the square.
		 */
		coords[2*n+0] -= dy * inset;
		coords[2*n+1] += dx * inset;
	    } else if (nin == 2) {
		/*
		 * We turned left, so inset this corner further
		 * _out_ along the edge into the next square.
		 */
		coords[2*n+0] += dy * inset;
		coords[2*n+1] -= dx * inset;
	    }
	    n++;

	} while (x != sx || y != sy || dx != sdx || dy != sdy);

	/*
	 * That's our polygon; now draw it.
	 */
	draw_polygon(dr, coords, n, -1, ink);
    }

    sfree(coords);
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int cr = state->cr;
    int ink = print_mono_colour(dr, 0);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, 3 * TILE_SIZE / 40);
    draw_rect_outline(dr, BORDER, BORDER, cr*TILE_SIZE, cr*TILE_SIZE, ink);

    /*
     * Highlight X-diagonal squares.
     */
    if (state->xtype) {
	int i;
	int xhighlight = print_grey_colour(dr, 0.90F);

	for (i = 0; i < cr; i++)
	    draw_rect(dr, BORDER + i*TILE_SIZE, BORDER + i*TILE_SIZE,
		      TILE_SIZE, TILE_SIZE, xhighlight);
	for (i = 0; i < cr; i++)
	    if (i*2 != cr-1)  /* avoid redoing centre square, just for fun */
		draw_rect(dr, BORDER + i*TILE_SIZE,
			  BORDER + (cr-1-i)*TILE_SIZE,
			  TILE_SIZE, TILE_SIZE, xhighlight);
    }

    /*
     * Main grid.
     */
    for (x = 1; x < cr; x++) {
	print_line_width(dr, TILE_SIZE / 40);
	draw_line(dr, BORDER+x*TILE_SIZE, BORDER,
		  BORDER+x*TILE_SIZE, BORDER+cr*TILE_SIZE, ink);
    }
    for (y = 1; y < cr; y++) {
	print_line_width(dr, TILE_SIZE / 40);
	draw_line(dr, BORDER, BORDER+y*TILE_SIZE,
		  BORDER+cr*TILE_SIZE, BORDER+y*TILE_SIZE, ink);
    }

    /*
     * Thick lines between cells.
     */
    print_line_width(dr, 3 * TILE_SIZE / 40);
    outline_block_structure(dr, ds, state, state->blocks, ink, 0);

    /*
     * Killer cages and their totals.
     */
    if (state->kblocks) {
	print_line_width(dr, TILE_SIZE / 40);
	print_line_dotted(dr, true);
	outline_block_structure(dr, ds, state, state->kblocks, ink,
				5 * TILE_SIZE / 40);
	print_line_dotted(dr, false);
	for (y = 0; y < cr; y++)
	    for (x = 0; x < cr; x++)
		if (state->kgrid[y*cr+x]) {
		    char str[20];
		    sprintf(str, "%d", state->kgrid[y*cr+x]);
		    draw_text(dr,
			      BORDER+x*TILE_SIZE + 7*TILE_SIZE/40,
			      BORDER+y*TILE_SIZE + 16*TILE_SIZE/40,
			      FONT_VARIABLE, TILE_SIZE/4,
			      ALIGN_VNORMAL | ALIGN_HLEFT,
			      ink, str);
		}
    }

    /*
     * Standard (non-Killer) clue numbers.
     */
    for (y = 0; y < cr; y++)
	for (x = 0; x < cr; x++)
	    if (state->grid[y*cr+x]) {
		char str[2];
		str[1] = '\0';
		str[0] = state->grid[y*cr+x] + '0';
		if (str[0] > '9')
		    str[0] += 'a' - ('9'+1);
		draw_text(dr, BORDER + x*TILE_SIZE + TILE_SIZE/2,
			  BORDER + y*TILE_SIZE + TILE_SIZE/2,
			  FONT_VARIABLE, TILE_SIZE/2,
			  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
	    }
}

#ifdef COMBINED
#define thegame solo
#endif

const struct game thegame = {
    "Solo", "games.solo", "solo",
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
    game_request_keys,
    game_changed_state,
    current_key_label,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    REQUIRE_RBUTTON | REQUIRE_NUMPAD,  /* flags */
};

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc;
    const char *err;
    bool grade = false;
    struct difficulty dlev;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            solver_show_working = true;
        } else if (!strcmp(p, "-g")) {
            grade = true;
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

    dlev.maxdiff = DIFF_RECURSIVE;
    dlev.maxkdiff = DIFF_KINTERSECT;
    solver(s->cr, s->blocks, s->kblocks, s->xtype, s->grid, s->kgrid, &dlev);
    if (grade) {
	printf("Difficulty rating: %s\n",
	       dlev.diff==DIFF_BLOCK ? "Trivial (blockwise positional elimination only)":
	       dlev.diff==DIFF_SIMPLE ? "Basic (row/column/number elimination required)":
	       dlev.diff==DIFF_INTERSECT ? "Intermediate (intersectional analysis required)":
	       dlev.diff==DIFF_SET ? "Advanced (set elimination required)":
	       dlev.diff==DIFF_EXTREME ? "Extreme (complex non-recursive techniques required)":
	       dlev.diff==DIFF_RECURSIVE ? "Unreasonable (guesswork and backtracking required)":
	       dlev.diff==DIFF_AMBIGUOUS ? "Ambiguous (multiple solutions exist)":
	       dlev.diff==DIFF_IMPOSSIBLE ? "Impossible (no solution exists)":
	       "INTERNAL ERROR: unrecognised difficulty code");
	if (p->killer)
	    printf("Killer difficulty: %s\n",
		   dlev.kdiff==DIFF_KSINGLE ? "Trivial (single square cages only)":
		   dlev.kdiff==DIFF_KMINMAX ? "Simple (maximum sum analysis required)":
		   dlev.kdiff==DIFF_KSUMS ? "Intermediate (sum possibilities)":
		   dlev.kdiff==DIFF_KINTERSECT ? "Advanced (sum region intersections)":
		   "INTERNAL ERROR: unrecognised difficulty code");
    } else {
        printf("%s\n", grid_text_format(s->cr, s->blocks, s->xtype, s->grid));
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
