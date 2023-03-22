/*
 * pattern.c: the pattern-reconstruction game known as `nonograms'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    COL_EMPTY,
    COL_FULL,
    COL_TEXT,
    COL_UNKNOWN,
    COL_GRID,
    COL_CURSOR,
    COL_ERROR,
    COL_CURSOR_GUIDE,
    NCOLOURS
};

#define PREFERRED_TILE_SIZE 24
#define TILE_SIZE (ds->tilesize)
#define BORDER (3 * TILE_SIZE / 4)
#define TLBORDER(d) ( (d) / 5 + 2 )
#define GUTTER (TILE_SIZE / 2)

#define FROMCOORD(d, x) \
        ( ((x) - (BORDER + GUTTER + TILE_SIZE * TLBORDER(d))) / TILE_SIZE )

#define SIZE(d) (2*BORDER + GUTTER + TILE_SIZE * (TLBORDER(d) + (d)))
#define GETTILESIZE(d, w) ((double)w / (2.0 + (double)TLBORDER(d) + (double)(d)))

#define TOCOORD(d, x) (BORDER + GUTTER + TILE_SIZE * (TLBORDER(d) + (x)))

struct game_params {
    int w, h;
};

#define GRID_UNKNOWN 2
#define GRID_FULL 1
#define GRID_EMPTY 0

typedef struct game_state_common {
    /* Parts of the game state that don't change during play. */
    int w, h;
    int rowsize;
    int *rowdata, *rowlen;
    bool *immutable;
    int refcount;
    enum { FS_SMALL, FS_LARGE } fontsize;
} game_state_common;

struct game_state {
    game_state_common *common;
    unsigned char *grid;
    bool completed, cheated;
};

#define FLASH_TIME 0.13F

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 15;

    return ret;
}

static const struct game_params pattern_presets[] = {
    {10, 10},
    {15, 15},
    {20, 20},
#ifndef SLOW_SYSTEM
    {25, 25},
    {30, 30},
#endif
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(pattern_presets))
        return false;

    ret = snew(game_params);
    *ret = pattern_presets[i];

    sprintf(str, "%dx%d", ret->w, ret->h);

    *name = dupstr(str);
    *params = ret;
    return true;
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
    char const *p = string;

    ret->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        ret->h = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    } else {
        ret->h = ret->w;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[400];
    int len;

    len = sprintf(ret, "%dx%d", params->w, params->h);
    assert(len < lenof(ret));
    ret[len] = '\0';

    return dupstr(ret);
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
    if (params->w <= 0 || params->h <= 0)
	return "Width and height must both be greater than zero";
    if (params->w > INT_MAX - 1 || params->h > INT_MAX - 1 ||
        params->w > INT_MAX / params->h)
        return "Puzzle must not be unreasonably large";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Puzzle generation code.
 * 
 * For this particular puzzle, it seemed important to me to ensure
 * a unique solution. I do this the brute-force way, by having a
 * solver algorithm alongside the generator, and repeatedly
 * generating a random grid until I find one whose solution is
 * unique. It turns out that this isn't too onerous on a modern PC
 * provided you keep grid size below around 30. Any offers of
 * better algorithms, however, will be very gratefully received.
 * 
 * Another annoyance of this approach is that it limits the
 * available puzzles to those solvable by the algorithm I've used.
 * My algorithm only ever considers a single row or column at any
 * one time, which means it's incapable of solving the following
 * difficult example (found by Bella Image around 1995/6, when she
 * and I were both doing maths degrees):
 * 
 *        2  1  2  1 
 *
 *      +--+--+--+--+
 * 1 1  |  |  |  |  |
 *      +--+--+--+--+
 *   2  |  |  |  |  |
 *      +--+--+--+--+
 *   1  |  |  |  |  |
 *      +--+--+--+--+
 *   1  |  |  |  |  |
 *      +--+--+--+--+
 * 
 * Obviously this cannot be solved by a one-row-or-column-at-a-time
 * algorithm (it would require at least one row or column reading
 * `2 1', `1 2', `3' or `4' to get started). However, it can be
 * proved to have a unique solution: if the top left square were
 * empty, then the only option for the top row would be to fill the
 * two squares in the 1 columns, which would imply the squares
 * below those were empty, leaving no place for the 2 in the second
 * row. Contradiction. Hence the top left square is full, and the
 * unique solution follows easily from that starting point.
 * 
 * (The game ID for this puzzle is 4x4:2/1/2/1/1.1/2/1/1 , in case
 * it's useful to anyone.)
 */

#ifndef STANDALONE_PICTURE_GENERATOR
static int float_compare(const void *av, const void *bv)
{
    const float *a = (const float *)av;
    const float *b = (const float *)bv;
    if (*a < *b)
        return -1;
    else if (*a > *b)
        return +1;
    else
        return 0;
}

static void generate(random_state *rs, int w, int h, unsigned char *retgrid)
{
    float *fgrid;
    float *fgrid2;
    int step, i, j;
    float threshold;

    fgrid = snewn(w*h, float);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            fgrid[i*w+j] = random_upto(rs, 100000000UL) / 100000000.F;
        }
    }

    /*
     * The above gives a completely random splattering of black and
     * white cells. We want to gently bias this in favour of _some_
     * reasonably thick areas of white and black, while retaining
     * some randomness and fine detail.
     * 
     * So we evolve the starting grid using a cellular automaton.
     * Currently, I'm doing something very simple indeed, which is
     * to set each square to the average of the surrounding nine
     * cells (or the average of fewer, if we're on a corner).
     */
    for (step = 0; step < 1; step++) {
        fgrid2 = snewn(w*h, float);

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float sx, xbar;
                int n, p, q;

                /*
                 * Compute the average of the surrounding cells.
                 */
                n = 0;
                sx = 0.F;
                for (p = -1; p <= +1; p++) {
                    for (q = -1; q <= +1; q++) {
                        if (i+p < 0 || i+p >= h || j+q < 0 || j+q >= w)
                            continue;
			/*
			 * An additional special case not mentioned
			 * above: if a grid dimension is 2xn then
			 * we do not average across that dimension
			 * at all. Otherwise a 2x2 grid would
			 * contain four identical squares.
			 */
			if ((h==2 && p!=0) || (w==2 && q!=0))
			    continue;
                        n++;
                        sx += fgrid[(i+p)*w+(j+q)];
                    }
                }
                xbar = sx / n;

                fgrid2[i*w+j] = xbar;
            }
        }

        sfree(fgrid);
        fgrid = fgrid2;
    }

    fgrid2 = snewn(w*h, float);
    memcpy(fgrid2, fgrid, w*h*sizeof(float));
    qsort(fgrid2, w*h, sizeof(float), float_compare);
    /* Choose a threshold that makes half the pixels black. In case of
     * an odd number of pixels, select randomly between just under and
     * just over half. */
    {
        int index = w * h / 2;
        if (w & h & 1)
            index += random_upto(rs, 2);
        if (index < w*h)
            threshold = fgrid2[index];
        else
            threshold = fgrid2[w*h-1] + 1;
    }
    sfree(fgrid2);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            retgrid[i*w+j] = (fgrid[i*w+j] >= threshold ? GRID_FULL :
                              GRID_EMPTY);
        }
    }

    sfree(fgrid);
}
#endif

static int compute_rowdata(int *ret, unsigned char *start, int len, int step)
{
    int i, n;

    n = 0;

    for (i = 0; i < len; i++) {
        if (start[i*step] == GRID_FULL) {
            int runlen = 1;
            while (i+runlen < len && start[(i+runlen)*step] == GRID_FULL)
                runlen++;
            ret[n++] = runlen;
            i += runlen;
        }

        if (i < len && start[i*step] == GRID_UNKNOWN)
            return -1;
    }

    return n;
}

#define UNKNOWN 0
#define BLOCK 1
#define DOT 2
#define STILL_UNKNOWN 3

#ifdef STANDALONE_SOLVER
static bool verbose = false;
#endif

static bool do_recurse(unsigned char *known, unsigned char *deduced,
                       unsigned char *row,
		       unsigned char *minpos_done, unsigned char *maxpos_done,
		       unsigned char *minpos_ok, unsigned char *maxpos_ok,
		       int *data, int len,
                       int freespace, int ndone, int lowest)
{
    int i, j, k;


    /* This algorithm basically tries all possible ways the given rows of
     * black blocks can be laid out in the row/column being examined.
     * Special care is taken to avoid checking the tail of a row/column
     * if the same conditions have already been checked during this recursion
     * The algorithm also takes care to cut its losses as soon as an
     * invalid (partial) solution is detected.
     */
    if (data[ndone]) {
	if (lowest >= minpos_done[ndone] && lowest <= maxpos_done[ndone]) {
	    if (lowest >= minpos_ok[ndone] && lowest <= maxpos_ok[ndone]) {
		for (i=0; i<lowest; i++)
		    deduced[i] |= row[i];
	    }
	    return lowest >= minpos_ok[ndone] && lowest <= maxpos_ok[ndone];
	} else {
	    if (lowest < minpos_done[ndone]) minpos_done[ndone] = lowest;
	    if (lowest > maxpos_done[ndone]) maxpos_done[ndone] = lowest;
	}
	for (i=0; i<=freespace; i++) {
	    j = lowest;
	    for (k=0; k<i; k++) {
		if (known[j] == BLOCK) goto next_iter;
	        row[j++] = DOT;
	    }
	    for (k=0; k<data[ndone]; k++) {
		if (known[j] == DOT) goto next_iter;
	        row[j++] = BLOCK;
	    }
	    if (j < len) {
		if (known[j] == BLOCK) goto next_iter;
	        row[j++] = DOT;
	    }
	    if (do_recurse(known, deduced, row, minpos_done, maxpos_done,
	                   minpos_ok, maxpos_ok, data, len, freespace-i, ndone+1, j)) {
	        if (lowest < minpos_ok[ndone]) minpos_ok[ndone] = lowest;
	        if (lowest + i > maxpos_ok[ndone]) maxpos_ok[ndone] = lowest + i;
	        if (lowest + i > maxpos_done[ndone]) maxpos_done[ndone] = lowest + i;
	    }
	    next_iter:
	    j++;
	}
	return lowest >= minpos_ok[ndone] && lowest <= maxpos_ok[ndone];
    } else {
	for (i=lowest; i<len; i++) {
	    if (known[i] == BLOCK) return false;
	    row[i] = DOT;
	    }
	for (i=0; i<len; i++)
	    deduced[i] |= row[i];
	return true;
    }
}


static bool do_row(unsigned char *known, unsigned char *deduced,
                   unsigned char *row,
                   unsigned char *minpos_done, unsigned char *maxpos_done,
                   unsigned char *minpos_ok, unsigned char *maxpos_ok,
                   unsigned char *start, int len, int step, int *data,
                   unsigned int *changed
#ifdef STANDALONE_SOLVER
                   , const char *rowcol, int index, int cluewid
#endif
                   )
{
    int rowlen, i, freespace;
    bool done_any;

    assert(len >= 0);   /* avoid compile warnings about the memsets below */

    freespace = len+1;
    for (rowlen = 0; data[rowlen]; rowlen++) {
	minpos_done[rowlen] = minpos_ok[rowlen] = len - 1;
	maxpos_done[rowlen] = maxpos_ok[rowlen] = 0;
	freespace -= data[rowlen]+1;
    }

    for (i = 0; i < len; i++) {
	known[i] = start[i*step];
	deduced[i] = 0;
    }
    for (i = len - 1; i >= 0 && known[i] == DOT; i--)
        freespace--;

    if (rowlen == 0) {
        memset(deduced, DOT, len);
    } else if (rowlen == 1 && data[0] == len) {
        memset(deduced, BLOCK, len);
    } else {
        do_recurse(known, deduced, row, minpos_done, maxpos_done, minpos_ok,
                   maxpos_ok, data, len, freespace, 0, 0);
    }

    done_any = false;
    for (i=0; i<len; i++)
	if (deduced[i] && deduced[i] != STILL_UNKNOWN && !known[i]) {
	    start[i*step] = deduced[i];
	    if (changed) changed[i]++;
	    done_any = true;
	}
#ifdef STANDALONE_SOLVER
    if (verbose && done_any) {
	char buf[80];
	int thiscluewid;
	printf("%s %2d: [", rowcol, index);
	for (thiscluewid = -1, i = 0; data[i]; i++)
	    thiscluewid += sprintf(buf, " %d", data[i]);
	printf("%*s", cluewid - thiscluewid, "");
	for (i = 0; data[i]; i++)
	    printf(" %d", data[i]);
	printf(" ] ");
	for (i = 0; i < len; i++)
	    putchar(known[i] == BLOCK ? '#' :
		    known[i] == DOT ? '.' : '?');
	printf(" -> ");
	for (i = 0; i < len; i++)
	    putchar(start[i*step] == BLOCK ? '#' :
		    start[i*step] == DOT ? '.' : '?');
	putchar('\n');
    }
#endif
    return done_any;
}

static bool solve_puzzle(const game_state *state, unsigned char *grid,
                         int w, int h,
                         unsigned char *matrix, unsigned char *workspace,
                         unsigned int *changed_h, unsigned int *changed_w,
                         int *rowdata
#ifdef STANDALONE_SOLVER
                         , int cluewid
#else
                         , int dummy
#endif
                         )
{
    int i, j, max;
    bool ok;
    int max_h, max_w;

    assert((state!=NULL && state->common->rowdata!=NULL) ^ (grid!=NULL));

    max = max(w, h);

    memset(matrix, 0, w*h);
    if (state) {
        for (i=0; i<w*h; i++) {
            if (state->common->immutable[i])
                matrix[i] = state->grid[i];
        }
    }

    /* For each column, compute how many squares can be deduced
     * from just the row-data and initial clues.
     * Later, changed_* will hold how many squares were changed
     * in every row/column in the previous iteration
     * Changed_* is used to choose the next rows / cols to re-examine
     */
    for (i=0; i<h; i++) {
	int freespace, rowlen;
	if (state && state->common->rowdata) {
            memcpy(rowdata, state->common->rowdata + state->common->rowsize*(w+i), max*sizeof(int));
	    rowlen = state->common->rowlen[w+i];
	} else {
	    rowlen = compute_rowdata(rowdata, grid+i*w, w, 1);
	}
        rowdata[rowlen] = 0;
        if (rowlen == 0) {
            changed_h[i] = w;
        } else {
            for (j=0, freespace=w+1; rowdata[j]; j++)
                freespace -= rowdata[j] + 1;
            for (j=0, changed_h[i]=0; rowdata[j]; j++)
                if (rowdata[j] > freespace)
                    changed_h[i] += rowdata[j] - freespace;
        }
        for (j = 0; j < w; j++)
            if (matrix[i*w+j])
                changed_h[i]++;
    }
    for (i=0,max_h=0; i<h; i++)
	if (changed_h[i] > max_h)
	    max_h = changed_h[i];
    for (i=0; i<w; i++) {
	int freespace, rowlen;
	if (state && state->common->rowdata) {
	    memcpy(rowdata, state->common->rowdata + state->common->rowsize*i, max*sizeof(int));
	    rowlen = state->common->rowlen[i];
	} else {
            rowlen = compute_rowdata(rowdata, grid+i, h, w);
	}
        rowdata[rowlen] = 0;
        if (rowlen == 0) {
            changed_w[i] = h;
        } else {
            for (j=0, freespace=h+1; rowdata[j]; j++)
                freespace -= rowdata[j] + 1;
            for (j=0, changed_w[i]=0; rowdata[j]; j++)
                if (rowdata[j] > freespace)
                    changed_w[i] += rowdata[j] - freespace;
        }
        for (j = 0; j < h; j++)
            if (matrix[j*w+i])
                changed_w[i]++;
    }
    for (i=0,max_w=0; i<w; i++)
	if (changed_w[i] > max_w)
	    max_w = changed_w[i];

    /* Solve the puzzle.
     * Process rows/columns individually. Deductions involving more than one
     * row and/or column at a time are not supported.
     * Take care to only process rows/columns which have been changed since they
     * were previously processed.
     * Also, prioritize rows/columns which have had the most changes since their
     * previous processing, as they promise the greatest benefit.
     * Extremely rectangular grids (e.g. 10x20, 15x40, etc.) are not treated specially.
     */
    do {
	for (; max_h && max_h >= max_w; max_h--) {
	    for (i=0; i<h; i++) {
		if (changed_h[i] >= max_h) {
		    if (state && state->common->rowdata) {
			memcpy(rowdata, state->common->rowdata + state->common->rowsize*(w+i), max*sizeof(int));
			rowdata[state->common->rowlen[w+i]] = 0;
		    } else {
			rowdata[compute_rowdata(rowdata, grid+i*w, w, 1)] = 0;
		    }
		    do_row(workspace, workspace+max, workspace+2*max,
			   workspace+3*max, workspace+4*max,
			   workspace+5*max, workspace+6*max,
			   matrix+i*w, w, 1, rowdata, changed_w
#ifdef STANDALONE_SOLVER
			   , "row", i+1, cluewid
#endif
			   );
		    changed_h[i] = 0;
		}
	    }
	    for (i=0,max_w=0; i<w; i++)
		if (changed_w[i] > max_w)
		    max_w = changed_w[i];
	}
	for (; max_w && max_w >= max_h; max_w--) {
	    for (i=0; i<w; i++) {
		if (changed_w[i] >= max_w) {
		    if (state && state->common->rowdata) {
			memcpy(rowdata, state->common->rowdata + state->common->rowsize*i, max*sizeof(int));
			rowdata[state->common->rowlen[i]] = 0;
		    } else {
			rowdata[compute_rowdata(rowdata, grid+i, h, w)] = 0;
		    }
		    do_row(workspace, workspace+max, workspace+2*max,
			   workspace+3*max, workspace+4*max,
			   workspace+5*max, workspace+6*max,
			   matrix+i, h, w, rowdata, changed_h
#ifdef STANDALONE_SOLVER
			   , "col", i+1, cluewid
#endif
			   );
		    changed_w[i] = 0;
		}
	    }
	    for (i=0,max_h=0; i<h; i++)
		if (changed_h[i] > max_h)
		    max_h = changed_h[i];
	}
    } while (max_h>0 || max_w>0);

    ok = true;
    for (i=0; i<h; i++) {
	for (j=0; j<w; j++) {
	    if (matrix[i*w+j] == UNKNOWN)
		ok = false;
	}
    }

    return ok;
}

#ifndef STANDALONE_PICTURE_GENERATOR
static unsigned char *generate_soluble(random_state *rs, int w, int h)
{
    int i, j, ntries, max;
    bool ok;
    unsigned char *grid, *matrix, *workspace;
    unsigned int *changed_h, *changed_w;
    int *rowdata;

    max = max(w, h);

    grid = snewn(w*h, unsigned char);
    /* Allocate this here, to avoid having to reallocate it again for every geneerated grid */
    matrix = snewn(w*h, unsigned char);
    workspace = snewn(max*7, unsigned char);
    changed_h = snewn(max+1, unsigned int);
    changed_w = snewn(max+1, unsigned int);
    rowdata = snewn(max+1, int);

    ntries = 0;

    do {
        ntries++;

        generate(rs, w, h, grid);

        /*
         * The game is a bit too easy if any row or column is
         * completely black or completely white. An exception is
         * made for rows/columns that are under 3 squares,
         * otherwise nothing will ever be successfully generated.
         */
        ok = true;
        if (w > 2) {
            for (i = 0; i < h; i++) {
                int colours = 0;
                for (j = 0; j < w; j++)
                    colours |= (grid[i*w+j] == GRID_FULL ? 2 : 1);
                if (colours != 3)
                    ok = false;
            }
        }
        if (h > 2) {
            for (j = 0; j < w; j++) {
                int colours = 0;
                for (i = 0; i < h; i++)
                    colours |= (grid[i*w+j] == GRID_FULL ? 2 : 1);
                if (colours != 3)
                    ok = false;
            }
        }
        if (!ok)
            continue;

	ok = solve_puzzle(NULL, grid, w, h, matrix, workspace,
			  changed_h, changed_w, rowdata, 0);
    } while (!ok);

    sfree(matrix);
    sfree(workspace);
    sfree(changed_h);
    sfree(changed_w);
    sfree(rowdata);
    return grid;
}
#endif

#ifdef STANDALONE_PICTURE_GENERATOR
static unsigned char *picture;
#endif

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    unsigned char *grid;
    int i, j, max, rowlen, *rowdata;
    char intbuf[80], *desc;
    int desclen, descpos;
#ifdef STANDALONE_PICTURE_GENERATOR
    game_state *state;
    int *index;
#endif

    max = max(params->w, params->h);

#ifdef STANDALONE_PICTURE_GENERATOR
    /*
     * Fixed input picture.
     */
    grid = snewn(params->w * params->h, unsigned char);
    memcpy(grid, picture, params->w * params->h);

    /*
     * Now winnow the immutable square set as far as possible.
     */
    state = snew(game_state);
    state->grid = grid;
    state->common = snew(game_state_common);
    state->common->rowdata = NULL;
    state->common->immutable = snewn(params->w * params->h, bool);
    for (i = 0; i < params->w * params->h; i++)
        state->common->immutable[i] = true;

    index = snewn(params->w * params->h, int);
    for (i = 0; i < params->w * params->h; i++)
        index[i] = i;
    shuffle(index, params->w * params->h, sizeof(*index), rs);

    {
        unsigned char *matrix = snewn(params->w*params->h, unsigned char);
        unsigned char *workspace = snewn(max*7, unsigned char);
        unsigned int *changed_h = snewn(max+1, unsigned int);
        unsigned int *changed_w = snewn(max+1, unsigned int);
        int *rowdata = snewn(max+1, int);
        for (i = 0; i < params->w * params->h; i++) {
            state->common->immutable[index[i]] = false;
            if (!solve_puzzle(state, grid, params->w, params->h,
                              matrix, workspace, changed_h, changed_w,
                              rowdata, 0))
                state->common->immutable[index[i]] = true;
        }
        sfree(workspace);
        sfree(changed_h);
        sfree(changed_w);
        sfree(rowdata);
        sfree(matrix);
    }
#else
    grid = generate_soluble(rs, params->w, params->h);
#endif
    rowdata = snewn(max, int);

    /*
     * Save the solved game in aux.
     */
    if (aux) {
	char *ai = snewn(params->w * params->h + 2, char);

        /*
         * String format is exactly the same as a solve move, so we
         * can just dupstr this in solve_game().
         */

        ai[0] = 'S';

        for (i = 0; i < params->w * params->h; i++)
            ai[i+1] = grid[i] ? '1' : '0';

        ai[params->w * params->h + 1] = '\0';

	*aux = ai;
    }

    /*
     * Seed is a slash-separated list of row contents; each row
     * contents section is a dot-separated list of integers. Row
     * contents are listed in the order (columns left to right,
     * then rows top to bottom).
     * 
     * Simplest way to handle memory allocation is to make two
     * passes, first computing the seed size and then writing it
     * out.
     */
    desclen = 0;
    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowlen = compute_rowdata(rowdata, grid+i, params->h, params->w);
        else
            rowlen = compute_rowdata(rowdata, grid+(i-params->w)*params->w,
                                     params->w, 1);
        if (rowlen > 0) {
            for (j = 0; j < rowlen; j++) {
                desclen += 1 + sprintf(intbuf, "%d", rowdata[j]);
            }
        } else {
            desclen++;
        }
    }
    desc = snewn(desclen, char);
    descpos = 0;
    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowlen = compute_rowdata(rowdata, grid+i, params->h, params->w);
        else
            rowlen = compute_rowdata(rowdata, grid+(i-params->w)*params->w,
                                     params->w, 1);
        if (rowlen > 0) {
            for (j = 0; j < rowlen; j++) {
                int len = sprintf(desc+descpos, "%d", rowdata[j]);
                if (j+1 < rowlen)
                    desc[descpos + len] = '.';
                else
                    desc[descpos + len] = '/';
                descpos += len+1;
            }
        } else {
            desc[descpos++] = '/';
        }
    }
    assert(descpos == desclen);
    assert(desc[desclen-1] == '/');
    desc[desclen-1] = '\0';
#ifdef STANDALONE_PICTURE_GENERATOR
    for (i = 0; i < params->w * params->h; i++)
        if (state->common->immutable[i])
            break;
    if (i < params->w * params->h) {
        /*
         * At least one immutable square, so we need a suffix.
         */
        int run;

        desc = sresize(desc, desclen + params->w * params->h + 3, char);
        desc[descpos-1] = ',';

        run = 0;
        for (i = 0; i < params->w * params->h; i++) {
            if (!state->common->immutable[i]) {
                run++;
                if (run == 25) {
                    desc[descpos++] = 'z';
                    run = 0;
                }
            } else {
                desc[descpos++] = run + (grid[i] == GRID_FULL ? 'A' : 'a');
                run = 0;
            }
        }
        if (run > 0)
            desc[descpos++] = run + 'a';
        desc[descpos] = '\0';
    }
    sfree(state->common->immutable);
    sfree(state->common);
    sfree(state);
#endif
    sfree(rowdata);
    sfree(grid);
    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int i, n, rowspace;
    const char *p;

    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowspace = params->h + 1;
        else
            rowspace = params->w + 1;

        if (*desc && isdigit((unsigned char)*desc)) {
            do {
                p = desc;
                while (*desc && isdigit((unsigned char)*desc)) desc++;
                n = atoi(p);
                if (n < 0)
                    return "at least one clue is negative";
                if (n > INT_MAX - 1)
                    return "at least one clue is grossly excessive";
                rowspace -= n+1;

                if (rowspace < 0) {
                    if (i < params->w)
                        return "at least one column contains more numbers than will fit";
                    else
                        return "at least one row contains more numbers than will fit";
                }
            } while (*desc++ == '.');
        } else {
            desc++;                    /* expect a slash immediately */
        }

        if (desc[-1] == '/') {
            if (i+1 == params->w + params->h)
                return "too many row/column specifications";
        } else if (desc[-1] == '\0' || desc[-1] == ',') {
            if (i+1 < params->w + params->h)
                return "too few row/column specifications";
        } else
            return "unrecognised character in game specification";
    }

    if (desc[-1] == ',') {
        /*
         * Optional extra piece of game description which fills in
         * some grid squares as extra clues.
         */
        i = 0;
        while (i < params->w * params->h) {
            int c = (unsigned char)*desc++;
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z')) {
                int len = tolower(c) - 'a';
                i += len;
                if (len < 25 && i < params->w*params->h)
                    i++;
                if (i > params->w * params->h) {
                    return "too much data in clue-squares section";
                }
            } else if (!c) {
                return "too little data in clue-squares section";
            } else {
                return "unrecognised character in clue-squares section";
            }
        }
        if (*desc) {
            return "too much data in clue-squares section";
        }
    }

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int i, j;
    const char *p;
    game_state *state = snew(game_state);

    state->common = snew(game_state_common);
    state->common->refcount = 1;

    state->common->w = params->w;
    state->common->h = params->h;

    state->grid = snewn(state->common->w * state->common->h, unsigned char);
    memset(state->grid, GRID_UNKNOWN, state->common->w * state->common->h);

    state->common->immutable = snewn(state->common->w * state->common->h,
                                     bool);
    memset(state->common->immutable, 0,
           state->common->w * state->common->h * sizeof(bool));

    state->common->rowsize = max(state->common->w, state->common->h);
    state->common->rowdata = snewn(state->common->rowsize * (state->common->w + state->common->h), int);
    state->common->rowlen = snewn(state->common->w + state->common->h, int);

    state->completed = state->cheated = false;

    for (i = 0; i < params->w + params->h; i++) {
        state->common->rowlen[i] = 0;
        if (*desc && isdigit((unsigned char)*desc)) {
            do {
                p = desc;
                while (*desc && isdigit((unsigned char)*desc)) desc++;
                state->common->rowdata[state->common->rowsize * i + state->common->rowlen[i]++] =
                    atoi(p);
            } while (*desc++ == '.');
        } else {
            desc++;                    /* expect a slash immediately */
        }
    }

    /*
     * Choose a font size based on the clues.  If any column clue is
     * more than one digit, switch to the smaller size.
     */
    state->common->fontsize = FS_LARGE;
    for (i = 0; i < params->w; i++)
        for (j = 0; j < state->common->rowlen[i]; j++)
            if (state->common->rowdata[state->common->rowsize * i + j] >= 10)
                state->common->fontsize = FS_SMALL;

    if (desc[-1] == ',') {
        /*
         * Optional extra piece of game description which fills in
         * some grid squares as extra clues.
         */
        i = 0;
        while (i < params->w * params->h) {
            int c = (unsigned char)*desc++;
            bool full = isupper(c);
            int len = tolower(c) - 'a';
            i += len;
            if (len < 25 && i < params->w*params->h) {
                state->grid[i] = full ? GRID_FULL : GRID_EMPTY;
                state->common->immutable[i] = true;
                i++;
            }
        }
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->common = state->common;
    ret->common->refcount++;

    ret->grid = snewn(ret->common->w * ret->common->h, unsigned char);
    memcpy(ret->grid, state->grid, ret->common->w * ret->common->h);

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->common->refcount == 0) {
        sfree(state->common->rowdata);
        sfree(state->common->rowlen);
        sfree(state->common->immutable);
        sfree(state->common);
    }
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *ai, const char **error)
{
    unsigned char *matrix;
    int w = state->common->w, h = state->common->h;
    int i;
    char *ret;
    int max;
    bool ok;
    unsigned char *workspace;
    unsigned int *changed_h, *changed_w;
    int *rowdata;

    /*
     * If we already have the solved state in ai, copy it out.
     */
    if (ai)
        return dupstr(ai);

    max = max(w, h);
    matrix = snewn(w*h, unsigned char);
    workspace = snewn(max*7, unsigned char);
    changed_h = snewn(max+1, unsigned int);
    changed_w = snewn(max+1, unsigned int);
    rowdata = snewn(max+1, int);

    ok = solve_puzzle(state, NULL, w, h, matrix, workspace,
		      changed_h, changed_w, rowdata, 0);

    sfree(workspace);
    sfree(changed_h);
    sfree(changed_w);
    sfree(rowdata);

    if (!ok) {
	sfree(matrix);
	*error = "Solving algorithm cannot complete this puzzle";
	return NULL;
    }

    ret = snewn(w*h+2, char);
    ret[0] = 'S';
    for (i = 0; i < w*h; i++) {
	assert(matrix[i] == BLOCK || matrix[i] == DOT);
	ret[i+1] = (matrix[i] == BLOCK ? '1' : '0');
    }
    ret[w*h+1] = '\0';

    sfree(matrix);

    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->common->w, h = state->common->h, i, j;
    int left_gap = 0, top_gap = 0, ch = 2, cw = 1, limit = 1;

    int len, topleft, lw, lh, gw, gh; /* {line,grid}_{width,height} */
    char *board, *buf;

    for (i = 0; i < w; ++i) {
	top_gap = max(top_gap, state->common->rowlen[i]);
	for (j = 0; j < state->common->rowlen[i]; ++j)
	    while (state->common->rowdata[i*state->common->rowsize + j] >= limit) {
		++cw;
		limit *= 10;
	    }
    }
    for (i = 0; i < h; ++i) {
	int rowlen = 0;
        bool predecessors = false;
	for (j = 0; j < state->common->rowlen[i+w]; ++j) {
	    int copy = state->common->rowdata[(i+w)*state->common->rowsize + j];
	    rowlen += predecessors;
	    predecessors = true;
	    do ++rowlen; while (copy /= 10);
	}
	left_gap = max(left_gap, rowlen);
    }

    cw = max(cw, 3);

    gw = w*cw + 2;
    gh = h*ch + 1;
    lw = gw + left_gap;
    lh = gh + top_gap;
    len = lw * lh;
    topleft = lw * top_gap + left_gap;

    board = snewn(len + 1, char);
    sprintf(board, "%*s\n", len - 2, "");

    for (i = 0; i < lh; ++i) {
	board[lw - 1 + i*lw] = '\n';
	if (i < top_gap) continue;
	board[lw - 2 + i*lw] = ((i - top_gap) % ch ? '|' : '+');
    }

    for (i = 0; i < w; ++i) {
	for (j = 0; j < state->common->rowlen[i]; ++j) {
	    int cell = topleft + i*cw + 1 + lw*(j - state->common->rowlen[i]);
	    int nch = sprintf(board + cell, "%*d", cw - 1,
			      state->common->rowdata[i*state->common->rowsize + j]);
	    board[cell + nch] = ' '; /* de-NUL-ify */
	}
    }

    buf = snewn(left_gap, char);
    for (i = 0; i < h; ++i) {
	char *p = buf, *start = board + top_gap*lw + left_gap + (i*ch+1)*lw;
	for (j = 0; j < state->common->rowlen[i+w]; ++j) {
	    if (p > buf) *p++ = ' ';
	    p += sprintf(p, "%d", state->common->rowdata[(i+w)*state->common->rowsize + j]);
	}
	memcpy(start - (p - buf), buf, p - buf);
    }

    for (i = 0; i < w; ++i) {
	for (j = 0; j < h; ++j) {
	    int cell = topleft + i*cw + j*ch*lw;
	    int center = cell + cw/2 + (ch/2)*lw;
	    int dx, dy;
	    board[cell] = false ? center : '+';
	    for (dx = 1; dx < cw; ++dx) board[cell + dx] = '-';
	    for (dy = 1; dy < ch; ++dy) board[cell + dy*lw] = '|';
	    if (state->grid[i*w+j] == GRID_UNKNOWN) continue;
	    for (dx = 1; dx < cw; ++dx)
		for (dy = 1; dy < ch; ++dy)
		    board[cell + dx + dy*lw] =
			state->grid[i*w+j] == GRID_FULL ? '#' : '.';
	}
    }

    memcpy(board + topleft + h*ch*lw, board + topleft, gw - 1);

    sfree(buf);

    return board;
}

struct game_ui {
    bool dragging;
    int drag_start_x;
    int drag_start_y;
    int drag_end_x;
    int drag_end_y;
    int drag, release, state;
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ret;

    ret = snew(game_ui);
    ret->dragging = false;
    ret->cur_x = ret->cur_y = 0;
    ret->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);

    return ret;
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
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) return "";
        switch (state->grid[ui->cur_y * state->common->w + ui->cur_x]) {
          case GRID_UNKNOWN:
            return button == CURSOR_SELECT ? "Black" : "White";
          case GRID_FULL:
            return button == CURSOR_SELECT ? "White" : "Grey";
          case GRID_EMPTY:
            return button == CURSOR_SELECT ? "Grey" : "Black";
        }
    }
    return "";
}

struct game_drawstate {
    bool started;
    int w, h;
    int tilesize;
    unsigned char *visible, *numcolours;
    int cur_x, cur_y;
    char *strbuf; /* Used for formatting clues. */
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    bool control = button & MOD_CTRL, shift = button & MOD_SHFT;
    button &= ~MOD_MASK;

    x = FROMCOORD(state->common->w, x);
    y = FROMCOORD(state->common->h, y);

    if (x >= 0 && x < state->common->w && y >= 0 && y < state->common->h &&
        (button == LEFT_BUTTON || button == RIGHT_BUTTON ||
         button == MIDDLE_BUTTON)) {
#ifdef STYLUS_BASED
        int currstate = state->grid[y * state->common->w + x];
#endif

        ui->dragging = true;

        if (button == LEFT_BUTTON) {
            ui->drag = LEFT_DRAG;
            ui->release = LEFT_RELEASE;
#ifdef STYLUS_BASED
            ui->state = (currstate + 2) % 3; /* FULL -> EMPTY -> UNKNOWN */
#else
            ui->state = GRID_FULL;
#endif
        } else if (button == RIGHT_BUTTON) {
            ui->drag = RIGHT_DRAG;
            ui->release = RIGHT_RELEASE;
#ifdef STYLUS_BASED
            ui->state = (currstate + 1) % 3; /* EMPTY -> FULL -> UNKNOWN */
#else
            ui->state = GRID_EMPTY;
#endif
        } else /* if (button == MIDDLE_BUTTON) */ {
            ui->drag = MIDDLE_DRAG;
            ui->release = MIDDLE_RELEASE;
            ui->state = GRID_UNKNOWN;
        }

        ui->drag_start_x = ui->drag_end_x = x;
        ui->drag_start_y = ui->drag_end_y = y;
        ui->cur_visible = false;

        return UI_UPDATE;
    }

    if (ui->dragging && button == ui->drag) {
        /*
         * There doesn't seem much point in allowing a rectangle
         * drag; people will generally only want to drag a single
         * horizontal or vertical line, so we make that easy by
         * snapping to it.
         * 
         * Exception: if we're _middle_-button dragging to tag
         * things as UNKNOWN, we may well want to trash an entire
         * area and start over!
         */
        if (ui->state != GRID_UNKNOWN) {
            if (abs(x - ui->drag_start_x) > abs(y - ui->drag_start_y))
                y = ui->drag_start_y;
            else
                x = ui->drag_start_x;
        }

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= state->common->w) x = state->common->w - 1;
        if (y >= state->common->h) y = state->common->h - 1;

        ui->drag_end_x = x;
        ui->drag_end_y = y;

        return UI_UPDATE;
    }

    if (ui->dragging && button == ui->release) {
        int x1, x2, y1, y2, xx, yy;
        bool move_needed = false;

        x1 = min(ui->drag_start_x, ui->drag_end_x);
        x2 = max(ui->drag_start_x, ui->drag_end_x);
        y1 = min(ui->drag_start_y, ui->drag_end_y);
        y2 = max(ui->drag_start_y, ui->drag_end_y);

        for (yy = y1; yy <= y2; yy++)
            for (xx = x1; xx <= x2; xx++)
                if (!state->common->immutable[yy * state->common->w + xx] &&
                    state->grid[yy * state->common->w + xx] != ui->state)
                    move_needed = true;

        ui->dragging = false;

        if (move_needed) {
	    char buf[80];
	    sprintf(buf, "%c%d,%d,%d,%d",
		    (char)(ui->state == GRID_FULL ? 'F' :
		           ui->state == GRID_EMPTY ? 'E' : 'U'),
		    x1, y1, x2-x1+1, y2-y1+1);
	    return dupstr(buf);
        } else
            return UI_UPDATE;
    }

    if (IS_CURSOR_MOVE(button)) {
	int x = ui->cur_x, y = ui->cur_y, newstate;
	char buf[80];
        move_cursor(button, &ui->cur_x, &ui->cur_y, state->common->w, state->common->h, false);
        ui->cur_visible = true;
	if (!control && !shift) return UI_UPDATE;

	newstate = control ? shift ? GRID_UNKNOWN : GRID_FULL : GRID_EMPTY;
	if (state->grid[y * state->common->w + x] == newstate &&
	    state->grid[ui->cur_y * state->common->w + ui->cur_x] == newstate)
	    return UI_UPDATE;

	sprintf(buf, "%c%d,%d,%d,%d", control ? shift ? 'U' : 'F' : 'E',
		min(x, ui->cur_x), min(y, ui->cur_y),
		abs(x - ui->cur_x) + 1, abs(y - ui->cur_y) + 1);
	return dupstr(buf);
    }

    if (IS_CURSOR_SELECT(button)) {
        int currstate = state->grid[ui->cur_y * state->common->w + ui->cur_x];
        int newstate;
        char buf[80];

        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }

        if (button == CURSOR_SELECT2)
            newstate = currstate == GRID_UNKNOWN ? GRID_EMPTY :
                currstate == GRID_EMPTY ? GRID_FULL : GRID_UNKNOWN;
        else
            newstate = currstate == GRID_UNKNOWN ? GRID_FULL :
                currstate == GRID_FULL ? GRID_EMPTY : GRID_UNKNOWN;

        sprintf(buf, "%c%d,%d,%d,%d",
                (char)(newstate == GRID_FULL ? 'F' :
		       newstate == GRID_EMPTY ? 'E' : 'U'),
                ui->cur_x, ui->cur_y, 1, 1);
        return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    int x1, x2, y1, y2, xx, yy;
    int val;

    if (move[0] == 'S' &&
        strlen(move) == from->common->w * from->common->h + 1) {
	int i;

	ret = dup_game(from);

	for (i = 0; i < ret->common->w * ret->common->h; i++)
	    ret->grid[i] = (move[i+1] == '1' ? GRID_FULL : GRID_EMPTY);

	ret->completed = ret->cheated = true;

	return ret;
    } else if ((move[0] == 'F' || move[0] == 'E' || move[0] == 'U') &&
	sscanf(move+1, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4 &&
	x1 >= 0 && x2 >= 0 && x1+x2 <= from->common->w &&
	y1 >= 0 && y2 >= 0 && y1+y2 <= from->common->h) {

	x2 += x1;
	y2 += y1;
	val = (move[0] == 'F' ? GRID_FULL :
		 move[0] == 'E' ? GRID_EMPTY : GRID_UNKNOWN);

	ret = dup_game(from);
	for (yy = y1; yy < y2; yy++)
	    for (xx = x1; xx < x2; xx++)
                if (!ret->common->immutable[yy * ret->common->w + xx])
                    ret->grid[yy * ret->common->w + xx] = val;

	/*
	 * An actual change, so check to see if we've completed the
	 * game.
	 */
	if (!ret->completed) {
	    int *rowdata = snewn(ret->common->rowsize, int);
	    int i, len;

	    ret->completed = true;

	    for (i=0; i<ret->common->w; i++) {
		len = compute_rowdata(rowdata, ret->grid+i,
                                      ret->common->h, ret->common->w);
		if (len != ret->common->rowlen[i] ||
		    memcmp(ret->common->rowdata+i*ret->common->rowsize,
                           rowdata, len * sizeof(int))) {
		    ret->completed = false;
		    break;
		}
	    }
	    for (i=0; i<ret->common->h; i++) {
		len = compute_rowdata(rowdata, ret->grid+i*ret->common->w,
                                      ret->common->w, 1);
		if (len != ret->common->rowlen[i+ret->common->w] ||
		    memcmp(ret->common->rowdata +
                           (i+ret->common->w)*ret->common->rowsize,
                           rowdata, len * sizeof(int))) {
		    ret->completed = false;
		    break;
		}
	    }

	    sfree(rowdata);
	}

	return ret;
    } else
	return NULL;
}

/* ----------------------------------------------------------------------
 * Error-checking during gameplay.
 */

/*
 * The difficulty in error-checking Pattern is to make the error check
 * _weak_ enough. The most obvious way would be to check each row and
 * column by calling (a modified form of) do_row() to recursively
 * analyse the row contents against the clue set and see if the
 * GRID_UNKNOWNs could be filled in in any way that would end up
 * correct. However, this turns out to be such a strong error check as
 * to constitute a spoiler in many situations: you make a typo while
 * trying to fill in one row, and not only does the row light up to
 * indicate an error, but several columns crossed by the move also
 * light up and draw your attention to deductions you hadn't even
 * noticed you could make.
 *
 * So instead I restrict error-checking to 'complete runs' within a
 * row, by which I mean contiguous sequences of GRID_FULL bounded at
 * both ends by either GRID_EMPTY or the ends of the row. We identify
 * all the complete runs in a row, and verify that _those_ are
 * consistent with the row's clue list. Sequences of complete runs
 * separated by solid GRID_EMPTY are required to match contiguous
 * sequences in the clue list, whereas if there's at least one
 * GRID_UNKNOWN between any two complete runs then those two need not
 * be contiguous in the clue list.
 *
 * To simplify the edge cases, I pretend that the clue list for the
 * row is extended with a 0 at each end, and I also pretend that the
 * grid data for the row is extended with a GRID_EMPTY and a
 * zero-length run at each end. This permits the contiguity checker to
 * handle the fiddly end effects (e.g. if the first contiguous
 * sequence of complete runs in the grid matches _something_ in the
 * clue list but not at the beginning, this is allowable iff there's a
 * GRID_UNKNOWN before the first one) with minimal faff, since the end
 * effects just drop out as special cases of the normal inter-run
 * handling (in this code the above case is not 'at the end of the
 * clue list' at all, but between the implicit initial zero run and
 * the first nonzero one).
 *
 * We must also be a little careful about how we search for a
 * contiguous sequence of runs. In the clue list (1 1 2 1 2 3),
 * suppose we see a GRID_UNKNOWN and then a length-1 run. We search
 * for 1 in the clue list and find it at the very beginning. But now
 * suppose we find a length-2 run with no GRID_UNKNOWN before it. We
 * can't naively look at the next clue from the 1 we found, because
 * that'll be the second 1 and won't match. Instead, we must backtrack
 * by observing that the 2 we've just found must be contiguous with
 * the 1 we've already seen, so we search for the sequence (1 2) and
 * find it starting at the second 1. Now if we see a 3, we must
 * rethink again and search for (1 2 3).
 */

struct errcheck_state {
    /*
     * rowdata and rowlen point at the clue data for this row in the
     * game state.
     */
    int *rowdata;
    int rowlen;
    /*
     * rowpos indicates the lowest position where it would be valid to
     * see our next run length. It might be equal to rowlen,
     * indicating that the next run would have to be the terminating 0.
     */
    int rowpos;
    /*
     * ncontig indicates how many runs we've seen in a contiguous
     * block. This is taken into account when searching for the next
     * run we find, unless ncontig is zeroed out first by encountering
     * a GRID_UNKNOWN.
     */
    int ncontig;
};

static bool errcheck_found_run(struct errcheck_state *es, int r)
{
/* Macro to handle the pretence that rowdata has a 0 at each end */
#define ROWDATA(k) ((k)<0 || (k)>=es->rowlen ? 0 : es->rowdata[(k)])

    /*
     * See if we can find this new run length at a position where it
     * also matches the last 'ncontig' runs we've seen.
     */
    int i, newpos;
    for (newpos = es->rowpos; newpos <= es->rowlen; newpos++) {

        if (ROWDATA(newpos) != r)
            goto notfound;

        for (i = 1; i <= es->ncontig; i++)
            if (ROWDATA(newpos - i) != ROWDATA(es->rowpos - i))
                goto notfound;

        es->rowpos = newpos+1;
        es->ncontig++;
        return true;

      notfound:;
    }

    return false;

#undef ROWDATA
}

static bool check_errors(const game_state *state, int i)
{
    int start, step, end, j;
    int val, runlen;
    struct errcheck_state aes, *es = &aes;

    es->rowlen = state->common->rowlen[i];
    es->rowdata = state->common->rowdata + state->common->rowsize * i;
    /* Pretend that we've already encountered the initial zero run */
    es->ncontig = 1;
    es->rowpos = 0;

    if (i < state->common->w) {
        start = i;
        step = state->common->w;
        end = start + step * state->common->h;
    } else {
        start = (i - state->common->w) * state->common->w;
        step = 1;
        end = start + step * state->common->w;
    }

    runlen = -1;
    for (j = start - step; j <= end; j += step) {
        if (j < start || j == end)
            val = GRID_EMPTY;
        else
            val = state->grid[j];

        if (val == GRID_UNKNOWN) {
            runlen = -1;
            es->ncontig = 0;
        } else if (val == GRID_FULL) {
            if (runlen >= 0)
                runlen++;
        } else if (val == GRID_EMPTY) {
            if (runlen > 0) {
                if (!errcheck_found_run(es, runlen))
                    return true;       /* error! */
            }
            runlen = 0;
        }
    }

    /* Signal end-of-row by sending errcheck_found_run the terminating
     * zero run, which will be marked as contiguous with the previous
     * run if and only if there hasn't been a GRID_UNKNOWN before. */
    if (!errcheck_found_run(es, 0))
        return true;                   /* error at the last minute! */

    return false;                      /* no error */
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

    *x = SIZE(params->w);
    *y = SIZE(params->h);
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

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    for (i = 0; i < 3; i++) {
        ret[COL_GRID         * 3 + i] = 0.3F;
        ret[COL_UNKNOWN      * 3 + i] = 0.5F;
        ret[COL_TEXT         * 3 + i] = 0.0F;
        ret[COL_FULL         * 3 + i] = 0.0F;
        ret[COL_EMPTY        * 3 + i] = 1.0F;
        ret[COL_CURSOR_GUIDE * 3 + i] = 0.5F;
    }
    ret[COL_CURSOR * 3 + 0] = 1.0F;
    ret[COL_CURSOR * 3 + 1] = 0.25F;
    ret[COL_CURSOR * 3 + 2] = 0.25F;
    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->started = false;
    ds->w = state->common->w;
    ds->h = state->common->h;
    ds->visible = snewn(ds->w * ds->h, unsigned char);
    ds->tilesize = 0;                  /* not decided yet */
    memset(ds->visible, 255, ds->w * ds->h);
    ds->numcolours = snewn(ds->w + ds->h, unsigned char);
    memset(ds->numcolours, 255, ds->w + ds->h);
    ds->cur_x = ds->cur_y = 0;
    ds->strbuf = snewn(state->common->rowsize *
                       MAX_DIGITS(*state->common->rowdata) + 1, char);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds->numcolours);
    sfree(ds->strbuf);
    sfree(ds);
}

static void grid_square(drawing *dr, game_drawstate *ds,
                        int y, int x, int state, bool cur)
{
    int xl, xr, yt, yb, dx, dy, dw, dh;

    draw_rect(dr, TOCOORD(ds->w, x), TOCOORD(ds->h, y),
              TILE_SIZE, TILE_SIZE, COL_GRID);

    xl = (x % 5 == 0 ? 1 : 0);
    yt = (y % 5 == 0 ? 1 : 0);
    xr = (x % 5 == 4 || x == ds->w-1 ? 1 : 0);
    yb = (y % 5 == 4 || y == ds->h-1 ? 1 : 0);

    dx = TOCOORD(ds->w, x) + 1 + xl;
    dy = TOCOORD(ds->h, y) + 1 + yt;
    dw = TILE_SIZE - xl - xr - 1;
    dh = TILE_SIZE - yt - yb - 1;

    draw_rect(dr, dx, dy, dw, dh,
              (state == GRID_FULL ? COL_FULL :
               state == GRID_EMPTY ? COL_EMPTY : COL_UNKNOWN));
    if (cur) {
        draw_rect_outline(dr, dx, dy, dw, dh, COL_CURSOR);
        draw_rect_outline(dr, dx+1, dy+1, dw-2, dh-2, COL_CURSOR);
    }

    draw_update(dr, TOCOORD(ds->w, x), TOCOORD(ds->h, y),
                TILE_SIZE, TILE_SIZE);
}

/*
 * Draw the numbers for a single row or column.
 */
static void draw_numbers(
    drawing *dr, game_drawstate *ds, const game_state *state,
    int i, bool erase, int colour)
{
    int rowlen = state->common->rowlen[i];
    int *rowdata = state->common->rowdata + state->common->rowsize * i;
    int nfit;
    int j;
    int rx, ry, rw, rh;
    int fontsize;

    if (i < state->common->w) {
        rx = TOCOORD(state->common->w, i);
        ry = 0;
        rw = TILE_SIZE;
        rh = BORDER + TLBORDER(state->common->h) * TILE_SIZE;
    } else {
        rx = 0;
        ry = TOCOORD(state->common->h, i - state->common->w);
        rw = BORDER + TLBORDER(state->common->w) * TILE_SIZE;
        rh = TILE_SIZE;
    }

    clip(dr, rx, ry, rw, rh);
    if (erase)
        draw_rect(dr, rx, ry, rw, rh, COL_BACKGROUND);

    /*
     * Choose a font size that's suitable for the lengths of clue.
     * Only column clues are interesting because row clues can be
     * spaced out independent of the tile size.  For column clues, we
     * want to go as large as practical while leaving decent space
     * between horizintally adjacent clues.  We currently distinguish
     * two cases: FS_LARGE is when all column clues are single digits,
     * and FS_SMALL in all other cases.
     *
     * If we assume that a digit is about 0.6em wide, and we want
     * about that space between clues, then FS_SMALL should be
     * TILESIZE/1.2.  If we also assume that clues are at most two
     * digits long then the case where adjacent clues are two digits
     * long requries FS_LARGE to be TILESIZE/1.8.
     */
    fontsize = (TILE_SIZE + 0.5F) /
        (state->common->fontsize == FS_LARGE ? 1.2F : 1.8F);

    /*
     * Normally I space the numbers out by the same distance as the
     * tile size. However, if there are more numbers than available
     * spaces, I have to squash them up a bit.
     */
    if (i < state->common->w)
        nfit = TLBORDER(state->common->h);
    else
        nfit = TLBORDER(state->common->w);
    nfit = max(rowlen, nfit) - 1;
    assert(nfit > 0);

    if (i < state->common->w) {
        for (j = 0; j < rowlen; j++) {
            int x, y;
            char str[MAX_DIGITS(*rowdata) + 1];

            x = rx;
            y = BORDER + TILE_SIZE * (TLBORDER(state->common->h)-1);
            y -= ((rowlen-j-1)*TILE_SIZE) * (TLBORDER(state->common->h)-1) / nfit;
            sprintf(str, "%d", rowdata[j]);
            draw_text(dr, x+TILE_SIZE/2, y+TILE_SIZE/2, FONT_VARIABLE,
                      fontsize, ALIGN_HCENTRE | ALIGN_VCENTRE, colour, str);
        }
    } else {
        int x, y;
        size_t off = 0;

        assert(rowlen <= state->common->rowsize);
        *ds->strbuf = '\0';
        for (j = 0; j < rowlen; j++)
            off += sprintf(ds->strbuf + off, "%s%d", j ? "  " : "", rowdata[j]);
        y = ry;
        x = BORDER + TILE_SIZE * (TLBORDER(state->common->w)-1);
        draw_text(dr, x+TILE_SIZE, y+TILE_SIZE/2, FONT_VARIABLE,
                  fontsize, ALIGN_HRIGHT | ALIGN_VCENTRE, colour, ds->strbuf);
    }

    unclip(dr);
    draw_update(dr, rx, ry, rw, rh);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, j;
    int x1, x2, y1, y2;
    int cx, cy;
    bool cmoved;

    if (!ds->started) {
        /*
         * Draw the grid outline.
         */
        draw_rect(dr, TOCOORD(ds->w, 0) - 1, TOCOORD(ds->h, 0) - 1,
                  ds->w * TILE_SIZE + 3, ds->h * TILE_SIZE + 3,
                  COL_GRID);

        ds->started = true;

	draw_update(dr, 0, 0, SIZE(ds->w), SIZE(ds->h));
    }

    if (ui->dragging) {
        x1 = min(ui->drag_start_x, ui->drag_end_x);
        x2 = max(ui->drag_start_x, ui->drag_end_x);
        y1 = min(ui->drag_start_y, ui->drag_end_y);
        y2 = max(ui->drag_start_y, ui->drag_end_y);
    } else {
        x1 = x2 = y1 = y2 = -1;        /* placate gcc warnings */
    }

    if (ui->cur_visible) {
        cx = ui->cur_x; cy = ui->cur_y;
    } else {
        cx = cy = -1;
    }
    cmoved = (cx != ds->cur_x || cy != ds->cur_y);

    /*
     * Now draw any grid squares which have changed since last
     * redraw.
     */
    for (i = 0; i < ds->h; i++) {
        for (j = 0; j < ds->w; j++) {
            int val;
            bool cc = false;

            /*
             * Work out what state this square should be drawn in,
             * taking any current drag operation into account.
             */
            if (ui->dragging && x1 <= j && j <= x2 && y1 <= i && i <= y2 &&
                !state->common->immutable[i * state->common->w + j])
                val = ui->state;
            else
                val = state->grid[i * state->common->w + j];

            if (cmoved) {
                /* the cursor has moved; if we were the old or
                 * the new cursor position we need to redraw. */
                if (j == cx && i == cy) cc = true;
                if (j == ds->cur_x && i == ds->cur_y) cc = true;
            }

            /*
             * Briefly invert everything twice during a completion
             * flash.
             */
            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 || flashtime >= FLASH_TIME*2/3) &&
                val != GRID_UNKNOWN)
                val = (GRID_FULL ^ GRID_EMPTY) ^ val;

            if (ds->visible[i * ds->w + j] != val || cc) {
                grid_square(dr, ds, i, j, val,
                            (j == cx && i == cy));
                ds->visible[i * ds->w + j] = val;
            }
        }
    }
    ds->cur_x = cx; ds->cur_y = cy;

    /*
     * Redraw any numbers which have changed their colour due to error
     * indication.
     */
    for (i = 0; i < state->common->w + state->common->h; i++) {
        int colour = check_errors(state, i) ? COL_ERROR : COL_TEXT;
        if (colour == COL_TEXT && ((cx >= 0 && i == cx) || (cy >= 0 && i == cy + ds->w))) {
            colour = COL_CURSOR_GUIDE;
        }
        if (ds->numcolours[i] != colour) {
            draw_numbers(dr, ds, state, i, true, colour);
            ds->numcolours[i] = colour;
        }
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
    if(ui->cur_visible) {
        *x = TOCOORD(ds->w, ui->cur_x);
        *y = TOCOORD(ds->h, ui->cur_y);
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
     * I'll use 5mm squares by default.
     */
    game_compute_size(params, 500, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->common->w, h = state->common->h;
    int ink = print_mono_colour(dr, 0);
    int x, y, i;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, TILE_SIZE / 16);
    draw_rect_outline(dr, TOCOORD(w, 0), TOCOORD(h, 0),
		      w*TILE_SIZE, h*TILE_SIZE, ink);

    /*
     * Grid.
     */
    for (x = 1; x < w; x++) {
	print_line_width(dr, TILE_SIZE / (x % 5 ? 128 : 24));
	draw_line(dr, TOCOORD(w, x), TOCOORD(h, 0),
		  TOCOORD(w, x), TOCOORD(h, h), ink);
    }
    for (y = 1; y < h; y++) {
	print_line_width(dr, TILE_SIZE / (y % 5 ? 128 : 24));
	draw_line(dr, TOCOORD(w, 0), TOCOORD(h, y),
		  TOCOORD(w, w), TOCOORD(h, y), ink);
    }

    /*
     * Clues.
     */
    for (i = 0; i < state->common->w + state->common->h; i++)
        draw_numbers(dr, ds, state, i, false, ink);

    /*
     * Solution.
     */
    print_line_width(dr, TILE_SIZE / 128);
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    if (state->grid[y*w+x] == GRID_FULL)
		draw_rect(dr, TOCOORD(w, x), TOCOORD(h, y),
			  TILE_SIZE, TILE_SIZE, ink);
	    else if (state->grid[y*w+x] == GRID_EMPTY)
		draw_circle(dr, TOCOORD(w, x) + TILE_SIZE/2,
			    TOCOORD(h, y) + TILE_SIZE/2,
			    TILE_SIZE/12, ink, ink);
	}
}

#ifdef COMBINED
#define thegame pattern
#endif

const struct game thegame = {
    "Pattern", "games.pattern", "pattern",
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
    REQUIRE_RBUTTON,		       /* flags */
};

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc;
    const char *err;

    while (--argc > 0) {
        char *p = *++argv;
	if (*p == '-') {
	    if (!strcmp(p, "-v")) {
		verbose = true;
	    } else {
		fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
		return 1;
	    }
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s <game_id>\n", argv[0]);
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

    {
	int w = p->w, h = p->h, i, j, max, cluewid = 0;
	unsigned char *matrix, *workspace;
	unsigned int *changed_h, *changed_w;
	int *rowdata;

	matrix = snewn(w*h, unsigned char);
	max = max(w, h);
	workspace = snewn(max*7, unsigned char);
	changed_h = snewn(max+1, unsigned int);
	changed_w = snewn(max+1, unsigned int);
	rowdata = snewn(max+1, int);

	if (verbose) {
	    int thiswid;
	    /*
	     * Work out the maximum text width of the clue numbers
	     * in a row or column, so we can print the solver's
	     * working in a nicely lined up way.
	     */
	    for (i = 0; i < (w+h); i++) {
		char buf[80];
		for (thiswid = -1, j = 0; j < s->common->rowlen[i]; j++)
		    thiswid += sprintf
                        (buf, " %d",
                         s->common->rowdata[s->common->rowsize*i+j]);
		if (cluewid < thiswid)
		    cluewid = thiswid;
	    }
	}

	solve_puzzle(s, NULL, w, h, matrix, workspace,
		     changed_h, changed_w, rowdata, cluewid);

	for (i = 0; i < h; i++) {
	    for (j = 0; j < w; j++) {
		int c = (matrix[i*w+j] == UNKNOWN ? '?' :
			 matrix[i*w+j] == BLOCK ? '#' :
			 matrix[i*w+j] == DOT ? '.' :
			 '!');
		putchar(c);
	    }
	    printf("\n");
	}
    }

    return 0;
}

#endif

#ifdef STANDALONE_PICTURE_GENERATOR

/*
 * Main program for the standalone picture generator. To use it,
 * simply provide it with an XBM-format bitmap file (note XBM, not
 * XPM) on standard input, and it will output a game ID in return.
 * For example:
 *
 *   $ ./patternpicture < calligraphic-A.xbm
 *   15x15:2/4/2/2/2/3/3/3.1/3.1/3.1/11/14/12/6/1/2/2/3/4/5/1.3/2.3/1.3/2.3/1.4/9/1.1.3/2.2.3/5.4/3.2
 *
 * That looks easy, of course - all the program has done is to count
 * up the clue numbers! But in fact, it's done more than that: it's
 * also checked that the result is uniquely soluble from just the
 * numbers. If it hadn't been, then it would have also left some
 * filled squares in the playing area as extra clues.
 *
 *   $ ./patternpicture < cube.xbm
 *   15x15:10/2.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.10/1.1.1/1.1.1/1.1.1/2.1/10/10/1.2/1.1.1/1.1.1/1.1.1/10.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.1.1/1.2/10,TNINzzzzGNzw
 *
 * This enables a reasonably convenient design workflow for coming up
 * with pictorial Pattern puzzles which _are_ uniquely soluble without
 * those inelegant pre-filled squares. Fire up a bitmap editor (X11
 * bitmap(1) is good enough), save a trial .xbm, and then test it by
 * running a command along the lines of
 *
 *   $ ./pattern $(./patternpicture < test.xbm)
 *
 * If the resulting window pops up with some pre-filled squares, then
 * that tells you which parts of the image are giving rise to
 * ambiguities, so try making tweaks in those areas, try the test
 * command again, and see if it helps. Once you have a design for
 * which the Pattern starting grid comes out empty, there's your game
 * ID.
 */

#include <time.h>

int main(int argc, char **argv)
{
    game_params *par;
    char *params, *desc;
    random_state *rs;
    time_t seed = time(NULL);
    char buf[4096];
    int i;
    int x, y;

    par = default_params();
    if (argc > 1)
	decode_params(par, argv[1]);   /* get difficulty */
    par->w = par->h = -1;

    /*
     * Now read an XBM file from standard input. This is simple and
     * hacky and will do very little error detection, so don't feed
     * it bogus data.
     */
    picture = NULL;
    x = y = 0;
    while (fgets(buf, sizeof(buf), stdin)) {
	buf[strcspn(buf, "\r\n")] = '\0';
	if (!strncmp(buf, "#define", 7)) {
	    /*
	     * Lines starting `#define' give the width and height.
	     */
	    char *num = buf + strlen(buf);
	    char *symend;

	    while (num > buf && isdigit((unsigned char)num[-1]))
		num--;
	    symend = num;
	    while (symend > buf && isspace((unsigned char)symend[-1]))
		symend--;

	    if (symend-5 >= buf && !strncmp(symend-5, "width", 5))
		par->w = atoi(num);
	    else if (symend-6 >= buf && !strncmp(symend-6, "height", 6))
		par->h = atoi(num);
	} else {
	    /*
	     * Otherwise, break the string up into words and take
	     * any word of the form `0x' plus hex digits to be a
	     * byte.
	     */
	    char *p, *wordstart;

	    if (!picture) {
		if (par->w < 0 || par->h < 0) {
		    printf("failed to read width and height\n");
		    return 1;
		}
		picture = snewn(par->w * par->h, unsigned char);
		for (i = 0; i < par->w * par->h; i++)
		    picture[i] = GRID_UNKNOWN;
	    }

	    p = buf;
	    while (*p) {
		while (*p && (*p == ',' || isspace((unsigned char)*p)))
		    p++;
		wordstart = p;
		while (*p && !(*p == ',' || *p == '}' ||
			       isspace((unsigned char)*p)))
		    p++;
		if (*p)
		    *p++ = '\0';

		if (wordstart[0] == '0' &&
		    (wordstart[1] == 'x' || wordstart[1] == 'X') &&
		    !wordstart[2 + strspn(wordstart+2,
					  "0123456789abcdefABCDEF")]) {
		    unsigned long byte = strtoul(wordstart+2, NULL, 16);
		    for (i = 0; i < 8; i++) {
			int bit = (byte >> i) & 1;
			if (y < par->h && x < par->w)
			    picture[y * par->w + x] =
                                bit ? GRID_FULL : GRID_EMPTY;
			x++;
		    }

		    if (x >= par->w) {
			x = 0;
			y++;
		    }
		}
	    }
	}
    }

    for (i = 0; i < par->w * par->h; i++)
	if (picture[i] == GRID_UNKNOWN) {
	    fprintf(stderr, "failed to read enough bitmap data\n");
	    return 1;
	}

    rs = random_new((void*)&seed, sizeof(time_t));

    desc = new_game_desc(par, rs, NULL, false);
    params = encode_params(par, false);
    printf("%s:%s\n", params, desc);

    sfree(desc);
    sfree(params);
    free_params(par);
    random_free(rs);

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
