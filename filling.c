/*
 * filling.c: An implementation of the Nikoli game fillomino.
 * Copyright (C) 2007 Jonas Kölker.  See LICENSE for the license.
 */

/* TODO:
 *
 *  - use a typedef instead of int for numbers on the board
 *     + replace int with something else (signed short?)
 *        - the type should be signed (for -board[i] and -SENTINEL)
 *        - the type should be somewhat big: board[i] = i
 *        - Using shorts gives us 181x181 puzzles as upper bound.
 *
 *  - in board generation, after having merged regions such that no
 *    more merges are necessary, try splitting (big) regions.
 *     + it seems that smaller regions make for better puzzles; see
 *       for instance the 7x7 puzzle in this file (grep for 7x7:).
 *
 *  - symmetric hints (solo-style)
 *     + right now that means including _many_ hints, and the puzzles
 *       won't look any nicer.  Not worth it (at the moment).
 *
 *  - make the solver do recursion/backtracking.
 *     + This is for user-submitted puzzles, not for puzzle
 *       generation (on the other hand, never say never).
 *
 *  - prove that only w=h=2 needs a special case
 *
 *  - solo-like pencil marks?
 *
 *  - a user says that the difficulty is unevenly distributed.
 *     + partition into levels?  Will they be non-crap?
 *
 *  - Allow square contents > 9?
 *     + I could use letters for digits (solo does this), but
 *       letters don't have numeric significance (normal people hate
 *       base36), which is relevant here (much more than in solo).
 *     + [click, 1, 0, enter] => [10 in clicked square]?
 *     + How much information is needed to solve?  Does one need to
 *       know the algorithm by which the largest number is set?
 *
 *  - eliminate puzzle instances with done chunks (1's in particular)?
 *     + that's what the qsort call is all about.
 *     + the 1's don't bother me that much.
 *     + but this takes a LONG time (not always possible)?
 *        - this may be affected by solver (lack of) quality.
 *        - weed them out by construction instead of post-cons check
 *           + but that interleaves make_board and new_game_desc: you
 *             have to alternate between changing the board and
 *             changing the hint set (instead of just creating the
 *             board once, then changing the hint set once -> done).
 *
 *  - use binary search when discovering the minimal sovable point
 *     + profile to show a need (but when the solver gets slower...)
 *     + 7x9 @ .011s, 9x13 @ .075s, 17x13 @ .661s (all avg with n=100)
 *     + but the hints are independent, not linear, so... what?
 */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

static bool verbose;

static void printv(const char *fmt, ...) {
#ifndef PALM
    if (verbose) {
	va_list va;
	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
    }
#endif
}

/*****************************************************************************
 * GAME CONFIGURATION AND PARAMETERS                                         *
 *****************************************************************************/

struct game_params {
    int w, h;
};

struct shared_state {
    struct game_params params;
    int *clues;
    int refcnt;
};

struct game_state {
    int *board;
    struct shared_state *shared;
    bool completed, cheated;
};

static const struct game_params filling_defaults[3] = {
    {9, 7}, {13, 9}, {17, 13}
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    *ret = filling_defaults[1]; /* struct copy */

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    char buf[64];

    if (i < 0 || i >= lenof(filling_defaults)) return false;
    *params = snew(game_params);
    **params = filling_defaults[i]; /* struct copy */
    sprintf(buf, "%dx%d", filling_defaults[i].w, filling_defaults[i].h);
    *name = dupstr(buf);

    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params; /* struct copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char) *string)) ++string;
    if (*string == 'x') ret->h = atoi(++string);
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[64];
    sprintf(buf, "%dx%d", params->w, params->h);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[64];

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
    if (params->w < 1) return "Width must be at least one";
    if (params->h < 1) return "Height must be at least one";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";

    return NULL;
}

/*****************************************************************************
 * STRINGIFICATION OF GAME STATE                                             *
 *****************************************************************************/

#define EMPTY 0

/* Example of plaintext rendering:
 *  +---+---+---+---+---+---+---+
 *  | 6 |   |   | 2 |   |   | 2 |
 *  +---+---+---+---+---+---+---+
 *  |   | 3 |   | 6 |   | 3 |   |
 *  +---+---+---+---+---+---+---+
 *  | 3 |   |   |   |   |   | 1 |
 *  +---+---+---+---+---+---+---+
 *  |   | 2 | 3 |   | 4 | 2 |   |
 *  +---+---+---+---+---+---+---+
 *  | 2 |   |   |   |   |   | 3 |
 *  +---+---+---+---+---+---+---+
 *  |   | 5 |   | 1 |   | 4 |   |
 *  +---+---+---+---+---+---+---+
 *  | 4 |   |   | 3 |   |   | 3 |
 *  +---+---+---+---+---+---+---+
 *
 * This puzzle instance is taken from the nikoli website
 * Encoded (unsolved and solved), the strings are these:
 * 7x7:6002002030603030000010230420200000305010404003003
 * 7x7:6662232336663232331311235422255544325413434443313
 */
static char *board_to_string(int *board, int w, int h) {
    const int sz = w * h;
    const int chw = (4*w + 2); /* +2 for trailing '+' and '\n' */
    const int chh = (2*h + 1); /* +1: n fence segments, n+1 posts */
    const int chlen = chw * chh;
    char *repr = snewn(chlen + 1, char);
    int i;

    assert(board);

    /* build the first line ("^(\+---){n}\+$") */
    for (i = 0; i < w; ++i) {
        repr[4*i + 0] = '+';
        repr[4*i + 1] = '-';
        repr[4*i + 2] = '-';
        repr[4*i + 3] = '-';
    }
    repr[4*i + 0] = '+';
    repr[4*i + 1] = '\n';

    /* ... and copy it onto the odd-numbered lines */
    for (i = 0; i < h; ++i) memcpy(repr + (2*i + 2) * chw, repr, chw);

    /* build the second line ("^(\|\t){n}\|$") */
    for (i = 0; i < w; ++i) {
        repr[chw + 4*i + 0] = '|';
        repr[chw + 4*i + 1] = ' ';
        repr[chw + 4*i + 2] = ' ';
        repr[chw + 4*i + 3] = ' ';
    }
    repr[chw + 4*i + 0] = '|';
    repr[chw + 4*i + 1] = '\n';

    /* ... and copy it onto the even-numbered lines */
    for (i = 1; i < h; ++i) memcpy(repr + (2*i + 1) * chw, repr + chw, chw);

    /* fill in the numbers */
    for (i = 0; i < sz; ++i) {
        const int x = i % w;
	const int y = i / w;
	if (board[i] == EMPTY) continue;
	repr[chw*(2*y + 1) + (4*x + 2)] = board[i] + '0';
    }

    repr[chlen] = '\0';
    return repr;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    const int w = state->shared->params.w;
    const int h = state->shared->params.h;
    return board_to_string(state->board, w, h);
}

/*****************************************************************************
 * GAME GENERATION AND SOLVER                                                *
 *****************************************************************************/

static const int dx[4] = {-1, 1, 0, 0};
static const int dy[4] = {0, 0, -1, 1};

struct solver_state
{
    int *dsf;
    int *board;
    int *connected;
    int nempty;

    /* Used internally by learn_bitmap_deductions; kept here to avoid
     * mallocing/freeing them every time that function is called. */
    int *bm, *bmdsf, *bmminsize;
};

static void print_board(int *board, int w, int h) {
    if (verbose) {
	char *repr = board_to_string(board, w, h);
	printv("%s\n", repr);
	free(repr);
    }
}

static game_state *new_game(midend *, const game_params *, const char *);
static void free_game(game_state *);

#define SENTINEL (sz+1)

static bool mark_region(int *board, int w, int h, int i, int n, int m) {
    int j;

    board[i] = -1;

    for (j = 0; j < 4; ++j) {
        const int x = (i % w) + dx[j], y = (i / w) + dy[j], ii = w*y + x;
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        if (board[ii] == m) return false;
        if (board[ii] != n) continue;
        if (!mark_region(board, w, h, ii, n, m)) return false;
    }
    return true;
}

static int region_size(int *board, int w, int h, int i) {
    const int sz = w * h;
    int j, size, copy;
    if (board[i] == 0) return 0;
    copy = board[i];
    mark_region(board, w, h, i, board[i], SENTINEL);
    for (size = j = 0; j < sz; ++j) {
        if (board[j] != -1) continue;
        ++size;
        board[j] = copy;
    }
    return size;
}

static void merge_ones(int *board, int w, int h)
{
    const int sz = w * h;
    const int maxsize = min(max(max(w, h), 3), 9);
    int i, j, k;
    bool change;
    do {
        change = false;
        for (i = 0; i < sz; ++i) {
            if (board[i] != 1) continue;

            for (j = 0; j < 4; ++j, board[i] = 1) {
                const int x = (i % w) + dx[j], y = (i / w) + dy[j];
                int oldsize, newsize, ii = w*y + x;
		bool ok;

                if (x < 0 || x >= w || y < 0 || y >= h) continue;
                if (board[ii] == maxsize) continue;

                oldsize = board[ii];
                board[i] = oldsize;
                newsize = region_size(board, w, h, i);

                if (newsize > maxsize) continue;

                ok = mark_region(board, w, h, i, oldsize, newsize);

                for (k = 0; k < sz; ++k)
                    if (board[k] == -1)
                        board[k] = ok ? newsize : oldsize;

                if (ok) break;
            }
            if (j < 4) change = true;
        }
    } while (change);
}

/* generate a random valid board; uses validate_board. */
static void make_board(int *board, int w, int h, random_state *rs) {
    const int sz = w * h;

    /* w=h=2 is a special case which requires a number > max(w, h) */
    /* TODO prove that this is the case ONLY for w=h=2. */
    const int maxsize = min(max(max(w, h), 3), 9);

    /* Note that if 1 in {w, h} then it's impossible to have a region
     * of size > w*h, so the special case only affects w=h=2. */

    int i, *dsf;
    bool change;

    assert(w >= 1);
    assert(h >= 1);
    assert(board);

    /* I abuse the board variable: when generating the puzzle, it
     * contains a shuffled list of numbers {0, ..., sz-1}. */
    for (i = 0; i < sz; ++i) board[i] = i;

    dsf = snewn(sz, int);
retry:
    dsf_init(dsf, sz);
    shuffle(board, sz, sizeof (int), rs);

    do {
        change = false; /* as long as the board potentially has errors */
        for (i = 0; i < sz; ++i) {
            const int square = dsf_canonify(dsf, board[i]);
            const int size = dsf_size(dsf, square);
            int merge = SENTINEL, min = maxsize - size + 1;
	    bool error = false;
            int neighbour, neighbour_size, j;
	    int directions[4];

            for (j = 0; j < 4; ++j)
		directions[j] = j;
	    shuffle(directions, 4, sizeof(int), rs);

            for (j = 0; j < 4; ++j) {
                const int x = (board[i] % w) + dx[directions[j]];
                const int y = (board[i] / w) + dy[directions[j]];
                if (x < 0 || x >= w || y < 0 || y >= h) continue;

                neighbour = dsf_canonify(dsf, w*y + x);
                if (square == neighbour) continue;

                neighbour_size = dsf_size(dsf, neighbour);
                if (size == neighbour_size) error = true;

                /* find the smallest neighbour to merge with, which
                 * wouldn't make the region too large.  (This is
                 * guaranteed by the initial value of `min'.) */
                if (neighbour_size < min && random_upto(rs, 10)) {
                    min = neighbour_size;
                    merge = neighbour;
                }
            }

            /* if this square is not in error, leave it be */
            if (!error) continue;

            /* if it is, but we can't fix it, retry the whole board.
             * Maybe we could fix it by merging the conflicting
             * neighbouring region(s) into some of their neighbours,
             * but just restarting works out fine. */
            if (merge == SENTINEL) goto retry;

            /* merge with the smallest neighbouring workable region. */
            dsf_merge(dsf, square, merge);
            change = true;
        }
    } while (change);

    for (i = 0; i < sz; ++i) board[i] = dsf_size(dsf, i);
    merge_ones(board, w, h);

    sfree(dsf);
}

static void merge(int *dsf, int *connected, int a, int b) {
    int c;
    assert(dsf);
    assert(connected);
    a = dsf_canonify(dsf, a);
    b = dsf_canonify(dsf, b);
    if (a == b) return;
    dsf_merge(dsf, a, b);
    c = connected[a];
    connected[a] = connected[b];
    connected[b] = c;
}

static void *memdup(const void *ptr, size_t len, size_t esz) {
    void *dup = smalloc(len * esz);
    assert(ptr);
    memcpy(dup, ptr, len * esz);
    return dup;
}

static void expand(struct solver_state *s, int w, int h, int t, int f) {
    int j;
    assert(s);
    assert(s->board[t] == EMPTY); /* expand to empty square */
    assert(s->board[f] != EMPTY); /* expand from non-empty square */
    printv(
	"learn: expanding %d from (%d, %d) into (%d, %d)\n",
	s->board[f], f % w, f / w, t % w, t / w);
    s->board[t] = s->board[f];
    for (j = 0; j < 4; ++j) {
        const int x = (t % w) + dx[j];
        const int y = (t / w) + dy[j];
        const int idx = w*y + x;
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        if (s->board[idx] != s->board[t]) continue;
        merge(s->dsf, s->connected, t, idx);
    }
    --s->nempty;
}

static void clear_count(int *board, int sz) {
    int i;
    for (i = 0; i < sz; ++i) {
        if (board[i] >= 0) continue;
        else if (board[i] == -SENTINEL) board[i] = EMPTY;
        else board[i] = -board[i];
    }
}

static void flood_count(int *board, int w, int h, int i, int n, int *c) {
    const int sz = w * h;
    int k;

    if (board[i] == EMPTY) board[i] = -SENTINEL;
    else if (board[i] == n) board[i] = -board[i];
    else return;

    if (--*c == 0) return;

    for (k = 0; k < 4; ++k) {
        const int x = (i % w) + dx[k];
        const int y = (i / w) + dy[k];
        const int idx = w*y + x;
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        flood_count(board, w, h, idx, n, c);
	if (*c == 0) return;
    }
}

static bool check_capacity(int *board, int w, int h, int i) {
    int n = board[i];
    flood_count(board, w, h, i, board[i], &n);
    clear_count(board, w * h);
    return n == 0;
}

static int expandsize(const int *board, int *dsf, int w, int h, int i, int n) {
    int j;
    int nhits = 0;
    int hits[4];
    int size = 1;
    for (j = 0; j < 4; ++j) {
        const int x = (i % w) + dx[j];
        const int y = (i / w) + dy[j];
        const int idx = w*y + x;
        int root;
        int m;
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        if (board[idx] != n) continue;
        root = dsf_canonify(dsf, idx);
        for (m = 0; m < nhits && root != hits[m]; ++m);
        if (m < nhits) continue;
	printv("\t  (%d, %d) contrib %d to size\n", x, y, dsf[root] >> 2);
        size += dsf_size(dsf, root);
        assert(dsf_size(dsf, root) >= 1);
        hits[nhits++] = root;
    }
    return size;
}

/*
 *  +---+---+---+---+---+---+---+
 *  | 6 |   |   | 2 |   |   | 2 |
 *  +---+---+---+---+---+---+---+
 *  |   | 3 |   | 6 |   | 3 |   |
 *  +---+---+---+---+---+---+---+
 *  | 3 |   |   |   |   |   | 1 |
 *  +---+---+---+---+---+---+---+
 *  |   | 2 | 3 |   | 4 | 2 |   |
 *  +---+---+---+---+---+---+---+
 *  | 2 |   |   |   |   |   | 3 |
 *  +---+---+---+---+---+---+---+
 *  |   | 5 |   | 1 |   | 4 |   |
 *  +---+---+---+---+---+---+---+
 *  | 4 |   |   | 3 |   |   | 3 |
 *  +---+---+---+---+---+---+---+
 */

/* Solving techniques:
 *
 * CONNECTED COMPONENT FORCED EXPANSION (too big):
 * When a CC can only be expanded in one direction, because all the
 * other ones would make the CC too big.
 *  +---+---+---+---+---+
 *  | 2 | 2 |   | 2 | _ |
 *  +---+---+---+---+---+
 *
 * CONNECTED COMPONENT FORCED EXPANSION (too small):
 * When a CC must include a particular square, because otherwise there
 * would not be enough room to complete it.  This includes squares not
 * adjacent to the CC through learn_critical_square.
 *  +---+---+
 *  | 2 | _ |
 *  +---+---+
 *
 * DROPPING IN A ONE:
 * When an empty square has no neighbouring empty squares and only a 1
 * will go into the square (or other CCs would be too big).
 *  +---+---+---+
 *  | 2 | 2 | _ |
 *  +---+---+---+
 *
 * TODO: generalise DROPPING IN A ONE: find the size of the CC of
 * empty squares and a list of all adjacent numbers.  See if only one
 * number in {1, ..., size} u {all adjacent numbers} is possible.
 * Probably this is only effective for a CC size < n for some n (4?)
 *
 * TODO: backtracking.
 */

static void filled_square(struct solver_state *s, int w, int h, int i) {
    int j;
    for (j = 0; j < 4; ++j) {
	const int x = (i % w) + dx[j];
	const int y = (i / w) + dy[j];
	const int idx = w*y + x;
	if (x < 0 || x >= w || y < 0 || y >= h) continue;
	if (s->board[i] == s->board[idx])
	    merge(s->dsf, s->connected, i, idx);
    }
}

static void init_solver_state(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    assert(s);

    s->nempty = 0;
    for (i = 0; i < sz; ++i) s->connected[i] = i;
    for (i = 0; i < sz; ++i)
        if (s->board[i] == EMPTY) ++s->nempty;
        else filled_square(s, w, h, i);
}

static bool learn_expand_or_one(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    bool learn = false;

    assert(s);

    for (i = 0; i < sz; ++i) {
	int j;
	bool one = true;

	if (s->board[i] != EMPTY) continue;

	for (j = 0; j < 4; ++j) {
	    const int x = (i % w) + dx[j];
	    const int y = (i / w) + dy[j];
	    const int idx = w*y + x;
	    if (x < 0 || x >= w || y < 0 || y >= h) continue;
	    if (s->board[idx] == EMPTY) {
		one = false;
		continue;
	    }
	    if (one &&
		(s->board[idx] == 1 ||
		 (s->board[idx] >= expandsize(s->board, s->dsf, w, h,
					      i, s->board[idx]))))
		one = false;
	    if (dsf_size(s->dsf, idx) == s->board[idx]) continue;
	    assert(s->board[i] == EMPTY);
	    s->board[i] = -SENTINEL;
	    if (check_capacity(s->board, w, h, idx)) continue;
	    assert(s->board[i] == EMPTY);
	    printv("learn: expanding in one\n");
	    expand(s, w, h, i, idx);
	    learn = true;
	    break;
	}

	if (j == 4 && one) {
	    printv("learn: one at (%d, %d)\n", i % w, i / w);
	    assert(s->board[i] == EMPTY);
	    s->board[i] = 1;
	    assert(s->nempty);
	    --s->nempty;
	    learn = true;
	}
    }
    return learn;
}

static bool learn_blocked_expansion(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    bool learn = false;

    assert(s);
    /* for every connected component */
    for (i = 0; i < sz; ++i) {
        int exp = SENTINEL;
        int j;

	if (s->board[i] == EMPTY) continue;
        j = dsf_canonify(s->dsf, i);

        /* (but only for each connected component) */
        if (i != j) continue;

        /* (and not if it's already complete) */
        if (dsf_size(s->dsf, j) == s->board[j]) continue;

        /* for each square j _in_ the connected component */
        do {
            int k;
            printv("  looking at (%d, %d)\n", j % w, j / w);

            /* for each neighbouring square (idx) */
            for (k = 0; k < 4; ++k) {
                const int x = (j % w) + dx[k];
                const int y = (j / w) + dy[k];
                const int idx = w*y + x;
                int size;
                /* int l;
                   int nhits = 0;
                   int hits[4]; */
                if (x < 0 || x >= w || y < 0 || y >= h) continue;
                if (s->board[idx] != EMPTY) continue;
                if (exp == idx) continue;
                printv("\ttrying to expand onto (%d, %d)\n", x, y);

                /* find out the would-be size of the new connected
                 * component if we actually expanded into idx */
                /*
                size = 1;
                for (l = 0; l < 4; ++l) {
                    const int lx = x + dx[l];
                    const int ly = y + dy[l];
                    const int idxl = w*ly + lx;
                    int root;
                    int m;
                    if (lx < 0 || lx >= w || ly < 0 || ly >= h) continue;
                    if (board[idxl] != board[j]) continue;
                    root = dsf_canonify(dsf, idxl);
                    for (m = 0; m < nhits && root != hits[m]; ++m);
                    if (m != nhits) continue;
                    // printv("\t  (%d, %d) contributed %d to size\n", lx, ly, dsf[root] >> 2);
                    size += dsf_size(dsf, root);
                    assert(dsf_size(dsf, root) >= 1);
                    hits[nhits++] = root;
                }
                */

                size = expandsize(s->board, s->dsf, w, h, idx, s->board[j]);

                /* ... and see if that size is too big, or if we
                 * have other expansion candidates.  Otherwise
                 * remember the (so far) only candidate. */

                printv("\tthat would give a size of %d\n", size);
                if (size > s->board[j]) continue;
                /* printv("\tnow knowing %d expansions\n", nexpand + 1); */
                if (exp != SENTINEL) goto next_i;
                assert(exp != idx);
                exp = idx;
            }

            j = s->connected[j]; /* next square in the same CC */
            assert(s->board[i] == s->board[j]);
        } while (j != i);
        /* end: for each square j _in_ the connected component */

	if (exp == SENTINEL) continue;
	printv("learning to expand\n");
	expand(s, w, h, exp, i);
	learn = true;

        next_i:
        ;
    }
    /* end: for each connected component */
    return learn;
}

static bool learn_critical_square(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    bool learn = false;
    assert(s);

    /* for each connected component */
    for (i = 0; i < sz; ++i) {
	int j, slack;
	if (s->board[i] == EMPTY) continue;
	if (i != dsf_canonify(s->dsf, i)) continue;
	slack = s->board[i] - dsf_size(s->dsf, i);
	if (slack == 0) continue;
	assert(s->board[i] != 1);
	/* for each empty square */
	for (j = 0; j < sz; ++j) {
	    if (s->board[j] == EMPTY) {
		/* if it's too far away from the CC, don't bother */
		int k = i, jx = j % w, jy = j / w;
		do {
		    int kx = k % w, ky = k / w;
		    if (abs(kx - jx) + abs(ky - jy) <= slack) break;
		    k = s->connected[k];
		} while (i != k);
		if (i == k) continue; /* not within range */
	    } else continue;
	    s->board[j] = -SENTINEL;
	    if (check_capacity(s->board, w, h, i)) continue;
	    /* if not expanding s->board[i] to s->board[j] implies
	     * that s->board[i] can't reach its full size, ... */
	    assert(s->nempty);
	    printv(
		"learn: ds %d at (%d, %d) blocking (%d, %d)\n",
		s->board[i], j % w, j / w, i % w, i / w);
	    --s->nempty;
	    s->board[j] = s->board[i];
	    filled_square(s, w, h, j);
	    learn = true;
	}
    }
    return learn;
}

#if 0
static void print_bitmap(int *bitmap, int w, int h) {
    if (verbose) {
	int x, y;
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		printv(" %03x", bm[y*w+x]);
	    }
	    printv("\n");
	}
    }
}
#endif

static bool learn_bitmap_deductions(struct solver_state *s, int w, int h)
{
    const int sz = w * h;
    int *bm = s->bm;
    int *dsf = s->bmdsf;
    int *minsize = s->bmminsize;
    int x, y, i, j, n;
    bool learn = false;

    /*
     * This function does deductions based on building up a bitmap
     * which indicates the possible numbers that can appear in each
     * grid square. If we can rule out all but one possibility for a
     * particular square, then we've found out the value of that
     * square. In particular, this is one of the few forms of
     * deduction capable of inferring the existence of a 'ghost
     * region', i.e. a region which has none of its squares filled in
     * at all.
     *
     * The reasoning goes like this. A currently unfilled square S can
     * turn out to contain digit n in exactly two ways: either S is
     * part of an n-region which also includes some currently known
     * connected component of squares with n in, or S is part of an
     * n-region separate from _all_ currently known connected
     * components. If we can rule out both possibilities, then square
     * S can't contain digit n at all.
     *
     * The former possibility: if there's a region of size n
     * containing both S and some existing component C, then that
     * means the distance from S to C must be small enough that C
     * could be extended to include S without becoming too big. So we
     * can do a breadth-first search out from all existing components
     * with n in them, to identify all the squares which could be
     * joined to any of them.
     *
     * The latter possibility: if there's a region of size n that
     * doesn't contain _any_ existing component, then it also can't
     * contain any square adjacent to an existing component either. So
     * we can identify all the EMPTY squares not adjacent to any
     * existing square with n in, and group them into connected
     * components; then any component of size less than n is ruled
     * out, because there wouldn't be room to create a completely new
     * n-region in it.
     *
     * In fact we process these possibilities in the other order.
     * First we find all the squares not adjacent to an existing
     * square with n in; then we winnow those by removing too-small
     * connected components, to get the set of squares which could
     * possibly be part of a brand new n-region; and finally we do the
     * breadth-first search to add in the set of squares which could
     * possibly be added to some existing n-region.
     */

    /*
     * Start by initialising our bitmap to 'all numbers possible in
     * all squares'.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    bm[y*w+x] = (1 << 10) - (1 << 1); /* bits 1,2,...,9 now set */
#if 0
    printv("initial bitmap:\n");
    print_bitmap(bm, w, h);
#endif

    /*
     * Now completely zero out the bitmap for squares that are already
     * filled in (we aren't interested in those anyway). Also, for any
     * filled square, eliminate its number from all its neighbours
     * (because, as discussed above, the neighbours couldn't be part
     * of a _new_ region with that number in it, and that's the case
     * we consider first).
     */
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    i = y*w+x;
	    n = s->board[i];

	    if (n != EMPTY) {
		bm[i] = 0;

		if (x > 0)
		    bm[i-1] &= ~(1 << n);
		if (x+1 < w)
		    bm[i+1] &= ~(1 << n);
		if (y > 0)
		    bm[i-w] &= ~(1 << n);
		if (y+1 < h)
		    bm[i+w] &= ~(1 << n);
	    }
	}
    }
#if 0
    printv("bitmap after filled squares:\n");
    print_bitmap(bm, w, h);
#endif

    /*
     * Now, for each n, we separately find the connected components of
     * squares for which n is still a possibility. Then discard any
     * component of size < n, because that component is too small to
     * have a completely new n-region in it.
     */
    for (n = 1; n <= 9; n++) {
	dsf_init(dsf, sz);

	/* Build the dsf */
	for (y = 0; y < h; y++)
	    for (x = 0; x+1 < w; x++)
		if (bm[y*w+x] & bm[y*w+(x+1)] & (1 << n))
		    dsf_merge(dsf, y*w+x, y*w+(x+1));
	for (y = 0; y+1 < h; y++)
	    for (x = 0; x < w; x++)
		if (bm[y*w+x] & bm[(y+1)*w+x] & (1 << n))
		    dsf_merge(dsf, y*w+x, (y+1)*w+x);

	/* Query the dsf */
	for (i = 0; i < sz; i++)
	    if ((bm[i] & (1 << n)) && dsf_size(dsf, i) < n)
		bm[i] &= ~(1 << n);
    }
#if 0
    printv("bitmap after winnowing small components:\n");
    print_bitmap(bm, w, h);
#endif

    /*
     * Now our bitmap includes every square which could be part of a
     * completely new region, of any size. Extend it to include
     * squares which could be part of an existing region.
     */
    for (n = 1; n <= 9; n++) {
	/*
	 * We're going to do a breadth-first search starting from
	 * existing connected components with cell value n, to find
	 * all cells they might possibly extend into.
	 *
	 * The quantity we compute, for each square, is 'minimum size
	 * that any existing CC would have to have if extended to
	 * include this square'. So squares already _in_ an existing
	 * CC are initialised to the size of that CC; then we search
	 * outwards using the rule that if a square's score is j, then
	 * its neighbours can't score more than j+1.
	 *
	 * Scores are capped at n+1, because if a square scores more
	 * than n then that's enough to know it can't possibly be
	 * reached by extending an existing region - we don't need to
	 * know exactly _how far_ out of reach it is.
	 */
	for (i = 0; i < sz; i++) {
	    if (s->board[i] == n) {
		/* Square is part of an existing CC. */
		minsize[i] = dsf_size(s->dsf, i);
	    } else {
		/* Otherwise, initialise to the maximum score n+1;
		 * we'll reduce this later if we find a neighbouring
		 * square with a lower score. */
		minsize[i] = n+1;
	    }
	}

	for (j = 1; j < n; j++) {
	    /*
	     * Find neighbours of cells scoring j, and set their score
	     * to at most j+1.
	     *
	     * Doing the BFS this way means we need n passes over the
	     * grid, which isn't entirely optimal but it seems to be
	     * fast enough for the moment. This could probably be
	     * improved by keeping a linked-list queue of cells in
	     * some way, but I think you'd have to be a bit careful to
	     * insert things into the right place in the queue; this
	     * way is easier not to get wrong.
	     */
	    for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
		    i = y*w+x;
		    if (minsize[i] == j) {
			if (x > 0 && minsize[i-1] > j+1)
			    minsize[i-1] = j+1;
			if (x+1 < w && minsize[i+1] > j+1)
			    minsize[i+1] = j+1;
			if (y > 0 && minsize[i-w] > j+1)
			    minsize[i-w] = j+1;
			if (y+1 < h && minsize[i+w] > j+1)
			    minsize[i+w] = j+1;
		    }
		}
	    }
	}

	/*
	 * Now, every cell scoring at most n should have its 1<<n bit
	 * in the bitmap reinstated, because we've found that it's
	 * potentially reachable by extending an existing CC.
	 */
	for (i = 0; i < sz; i++)
	    if (minsize[i] <= n)
		bm[i] |= 1<<n;
    }
#if 0
    printv("bitmap after bfs:\n");
    print_bitmap(bm, w, h);
#endif

    /*
     * Now our bitmap is complete. Look for entries with only one bit
     * set; those are squares with only one possible number, in which
     * case we can fill that number in.
     */
    for (i = 0; i < sz; i++) {
	if (bm[i] && !(bm[i] & (bm[i]-1))) { /* is bm[i] a power of two? */
	    int val = bm[i];

	    /* Integer log2, by simple binary search. */
	    n = 0;
	    if (val >> 8) { val >>= 8; n += 8; }
	    if (val >> 4) { val >>= 4; n += 4; }
	    if (val >> 2) { val >>= 2; n += 2; }
	    if (val >> 1) { val >>= 1; n += 1; }

	    /* Double-check that we ended up with a sensible
	     * answer. */
	    assert(1 <= n);
	    assert(n <= 9);
	    assert(bm[i] == (1 << n));

	    if (s->board[i] == EMPTY) {
		printv("learn: %d is only possibility at (%d, %d)\n",
		       n, i % w, i / w);
		s->board[i] = n;
		filled_square(s, w, h, i);
		assert(s->nempty);
		--s->nempty;
		learn = true;
	    }
	}
    }

    return learn;
}

static bool solver(const int *orig, int w, int h, char **solution) {
    const int sz = w * h;

    struct solver_state ss;
    ss.board = memdup(orig, sz, sizeof (int));
    ss.dsf = snew_dsf(sz); /* eqv classes: connected components */
    ss.connected = snewn(sz, int); /* connected[n] := n.next; */
    /* cyclic disjoint singly linked lists, same partitioning as dsf.
     * The lists lets you iterate over a partition given any member */
    ss.bm = snewn(sz, int);
    ss.bmdsf = snew_dsf(sz);
    ss.bmminsize = snewn(sz, int);

    printv("trying to solve this:\n");
    print_board(ss.board, w, h);

    init_solver_state(&ss, w, h);
    do {
	if (learn_blocked_expansion(&ss, w, h)) continue;
	if (learn_expand_or_one(&ss, w, h)) continue;
	if (learn_critical_square(&ss, w, h)) continue;
	if (learn_bitmap_deductions(&ss, w, h)) continue;
	break;
    } while (ss.nempty);

    printv("best guess:\n");
    print_board(ss.board, w, h);

    if (solution) {
        int i;
        *solution = snewn(sz + 2, char);
        **solution = 's';
        for (i = 0; i < sz; ++i) (*solution)[i + 1] = ss.board[i] + '0';
        (*solution)[sz + 1] = '\0';
    }

    sfree(ss.dsf);
    sfree(ss.board);
    sfree(ss.connected);
    sfree(ss.bm);
    sfree(ss.bmdsf);
    sfree(ss.bmminsize);

    return !ss.nempty;
}

static int *make_dsf(int *dsf, int *board, const int w, const int h) {
    const int sz = w * h;
    int i;

    if (!dsf)
        dsf = snew_dsf(w * h);
    else
        dsf_init(dsf, w * h);

    for (i = 0; i < sz; ++i) {
        int j;
        for (j = 0; j < 4; ++j) {
            const int x = (i % w) + dx[j];
            const int y = (i / w) + dy[j];
            const int k = w*y + x;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            if (board[i] == board[k]) dsf_merge(dsf, i, k);
        }
    }
    return dsf;
}

static void minimize_clue_set(int *board, int w, int h, random_state *rs)
{
    const int sz = w * h;
    int *shuf = snewn(sz, int), i;
    int *dsf, *next;

    for (i = 0; i < sz; ++i) shuf[i] = i;
    shuffle(shuf, sz, sizeof (int), rs);

    /*
     * First, try to eliminate an entire region at a time if possible,
     * because inferring the existence of a completely unclued region
     * is a particularly good aspect of this puzzle type and we want
     * to encourage it to happen.
     *
     * Begin by identifying the regions as linked lists of cells using
     * the 'next' array.
     */
    dsf = make_dsf(NULL, board, w, h);
    next = snewn(sz, int);
    for (i = 0; i < sz; ++i) {
	int j = dsf_canonify(dsf, i);
	if (i == j) {
	    /* First cell of a region; set next[i] = -1 to indicate
	     * end-of-list. */
	    next[i] = -1;
	} else {
	    /* Add this cell to a region which already has a
	     * linked-list head, by pointing the canonical element j
	     * at this one, and pointing this one in turn at wherever
	     * j previously pointed. (This should end up with the
	     * elements linked in the order 1,n,n-1,n-2,...,2, which
	     * is a bit weird-looking, but any order is fine.)
	     */
	    assert(j < i);
	    next[i] = next[j];
	    next[j] = i;
	}
    }

    /*
     * Now loop over the grid cells in our shuffled order, and each
     * time we encounter a region for the first time, try to remove it
     * all. Then we set next[canonical index] to -2 rather than -1, to
     * mark it as already tried.
     *
     * Doing this in a loop over _cells_, rather than extracting and
     * shuffling a list of _regions_, is intended to skew the
     * probabilities towards trying to remove larger regions first
     * (but without anything as crudely predictable as enforcing that
     * we _always_ process regions in descending size order). Region
     * removals might well be mutually exclusive, and larger ghost
     * regions are more interesting, so we want to bias towards them
     * if we can.
     */
    for (i = 0; i < sz; ++i) {
	int j = dsf_canonify(dsf, shuf[i]);
	if (next[j] != -2) {
	    int tmp = board[j];
	    int k;

	    /* Blank out the whole thing. */
	    for (k = j; k >= 0; k = next[k])
		board[k] = EMPTY;

	    if (!solver(board, w, h, NULL)) {
		/* Wasn't still solvable; reinstate it all */
		for (k = j; k >= 0; k = next[k])
		    board[k] = tmp;
	    }

	    /* Either way, don't try this region again. */
	    next[j] = -2;
	}
    }
    sfree(next);
    sfree(dsf);

    /*
     * Now go through individual cells, in the same shuffled order,
     * and try to remove each one by itself.
     */
    for (i = 0; i < sz; ++i) {
        int tmp = board[shuf[i]];
        board[shuf[i]] = EMPTY;
        if (!solver(board, w, h, NULL)) board[shuf[i]] = tmp;
    }

    sfree(shuf);
}

static int encode_run(char *buffer, int run)
{
    int i = 0;
    for (; run > 26; run -= 26)
	buffer[i++] = 'z';
    if (run)
	buffer[i++] = 'a' - 1 + run;
    return i;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    const int w = params->w, h = params->h, sz = w * h;
    int *board = snewn(sz, int), i, j, run;
    char *description = snewn(sz + 1, char);

    make_board(board, w, h, rs);
    minimize_clue_set(board, w, h, rs);

    for (run = j = i = 0; i < sz; ++i) {
        assert(board[i] >= 0);
        assert(board[i] < 10);
	if (board[i] == 0) {
	    ++run;
	} else {
	    j += encode_run(description + j, run);
	    run = 0;
	    description[j++] = board[i] + '0';
	}
    }
    j += encode_run(description + j, run);
    description[j++] = '\0';

    sfree(board);

    return sresize(description, j, char);
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const int sz = params->w * params->h;
    const char m = '0' + max(max(params->w, params->h), 3);
    int area;

    for (area = 0; *desc; ++desc) {
	if (*desc >= 'a' && *desc <= 'z') area += *desc - 'a' + 1;
	else if (*desc >= '0' && *desc <= m) ++area;
	else {
	    static char s[] =  "Invalid character '%""' in game description";
	    int n = sprintf(s, "Invalid character '%1c' in game description",
			    *desc);
	    assert(n + 1 <= lenof(s)); /* +1 for the terminating NUL */
	    return s;
	}
	if (area > sz) return "Too much data to fit in grid";
    }
    return (area < sz) ? "Not enough data to fill grid" : NULL;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
    int i;
    key_label *keys = snewn(11, key_label);

    *nkeys = 11;

    for(i = 0; i < 10; ++i)
    {
	keys[i].button = '0' + i;
	keys[i].label = NULL;
    }
    keys[10].button = '\b';
    keys[10].label = NULL;

    return keys;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int sz = params->w * params->h;
    int i;

    state->cheated = false;
    state->completed = false;
    state->shared = snew(struct shared_state);
    state->shared->refcnt = 1;
    state->shared->params = *params; /* struct copy */
    state->shared->clues = snewn(sz, int);

    for (i = 0; *desc; ++desc) {
	if (*desc >= 'a' && *desc <= 'z') {
	    int j = *desc - 'a' + 1;
	    assert(i + j <= sz);
	    for (; j; --j) state->shared->clues[i++] = 0;
	} else state->shared->clues[i++] = *desc - '0';
    }
    state->board = memdup(state->shared->clues, sz, sizeof (int));

    return state;
}

static game_state *dup_game(const game_state *state)
{
    const int sz = state->shared->params.w * state->shared->params.h;
    game_state *ret = snew(game_state);

    ret->board = memdup(state->board, sz, sizeof (int));
    ret->shared = state->shared;
    ret->cheated = state->cheated;
    ret->completed = state->completed;
    ++ret->shared->refcnt;

    return ret;
}

static void free_game(game_state *state)
{
    assert(state);
    sfree(state->board);
    if (--state->shared->refcnt == 0) {
        sfree(state->shared->clues);
        sfree(state->shared);
    }
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    if (aux == NULL) {
        const int w = state->shared->params.w;
        const int h = state->shared->params.h;
	char *new_aux;
        if (!solver(state->board, w, h, &new_aux))
            *error = "Sorry, I couldn't find a solution";
	return new_aux;
    }
    return dupstr(aux);
}

/*****************************************************************************
 * USER INTERFACE STATE AND ACTION                                           *
 *****************************************************************************/

struct game_ui {
    bool *sel; /* w*h highlighted squares, or NULL */
    int cur_x, cur_y;
    bool cur_visible, keydragging;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->sel = NULL;
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->keydragging = false;

    return ui;
}

static void free_ui(game_ui *ui)
{
    if (ui->sel)
        sfree(ui->sel);
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
    /* Clear any selection */
    if (ui->sel) {
        sfree(ui->sel);
        ui->sel = NULL;
    }
    ui->keydragging = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    const int w = state->shared->params.w;

    if (IS_CURSOR_SELECT(button) && ui->cur_visible) {
        if (button == CURSOR_SELECT) {
            if (ui->keydragging) return "Stop";
            return "Multiselect";
        }
        if (button == CURSOR_SELECT2 &&
            !state->shared->clues[w*ui->cur_y + ui->cur_x])
	    return (ui->sel[w*ui->cur_y + ui->cur_x]) ? "Deselect" : "Select";
    }
    return "";
}

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define BORDER (TILE_SIZE / 2)
#define BORDER_WIDTH (max(TILE_SIZE / 32, 1))

struct game_drawstate {
    struct game_params params;
    int tilesize;
    bool started;
    int *v, *flags;
    int *dsf_scratch, *border_scratch;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    const int w = state->shared->params.w;
    const int h = state->shared->params.h;

    const int tx = (x + TILE_SIZE - BORDER) / TILE_SIZE - 1;
    const int ty = (y + TILE_SIZE - BORDER) / TILE_SIZE - 1;

    char *move = NULL;
    int i;

    assert(ui);
    assert(ds);

    button &= ~MOD_MASK;

    if (button == LEFT_BUTTON || button == LEFT_DRAG) {
        /* A left-click anywhere will clear the current selection. */
        if (button == LEFT_BUTTON) {
            if (ui->sel) {
                sfree(ui->sel);
                ui->sel = NULL;
            }
        }
        if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
            if (!ui->sel) {
                ui->sel = snewn(w*h, bool);
                memset(ui->sel, 0, w*h*sizeof(bool));
            }
            if (!state->shared->clues[w*ty+tx])
                ui->sel[w*ty+tx] = true;
        }
        ui->cur_visible = false;
        return UI_UPDATE;
    }

    if (IS_CURSOR_MOVE(button)) {
        ui->cur_visible = true;
        move_cursor(button, &ui->cur_x, &ui->cur_y, w, h, false);
	if (ui->keydragging) goto select_square;
        return UI_UPDATE;
    }
    if (button == CURSOR_SELECT) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
	ui->keydragging = !ui->keydragging;
	if (!ui->keydragging) return UI_UPDATE;

      select_square:
        if (!ui->sel) {
            ui->sel = snewn(w*h, bool);
            memset(ui->sel, 0, w*h*sizeof(bool));
        }
	if (!state->shared->clues[w*ui->cur_y + ui->cur_x])
	    ui->sel[w*ui->cur_y + ui->cur_x] = true;
	return UI_UPDATE;
    }
    if (button == CURSOR_SELECT2) {
	if (!ui->cur_visible) {
	    ui->cur_visible = true;
	    return UI_UPDATE;
	}
        if (!ui->sel) {
            ui->sel = snewn(w*h, bool);
            memset(ui->sel, 0, w*h*sizeof(bool));
        }
	ui->keydragging = false;
	if (!state->shared->clues[w*ui->cur_y + ui->cur_x])
	    ui->sel[w*ui->cur_y + ui->cur_x] ^= 1;
	for (i = 0; i < w*h && !ui->sel[i]; i++);
	if (i == w*h) {
	    sfree(ui->sel);
	    ui->sel = NULL;
	}
	return UI_UPDATE;
    }

    if (button == '\b' || button == 27) {
	sfree(ui->sel);
	ui->sel = NULL;
	ui->keydragging = false;
	return UI_UPDATE;
    }

    if (button < '0' || button > '9') return NULL;
    button -= '0';
    if (button > (w == 2 && h == 2 ? 3 : max(w, h))) return NULL;
    ui->keydragging = false;

    for (i = 0; i < w*h; i++) {
        char buf[32];
        if ((ui->sel && ui->sel[i]) ||
            (!ui->sel && ui->cur_visible && (w*ui->cur_y+ui->cur_x) == i)) {
            if (state->shared->clues[i] != 0) continue; /* in case cursor is on clue */
            if (state->board[i] != button) {
                sprintf(buf, "%s%d", move ? "," : "", i);
                if (move) {
                    move = srealloc(move, strlen(move)+strlen(buf)+1);
                    strcat(move, buf);
                } else {
                    move = smalloc(strlen(buf)+1);
                    strcpy(move, buf);
                }
            }
        }
    }
    if (move) {
        char buf[32];
        sprintf(buf, "_%d", button);
        move = srealloc(move, strlen(move)+strlen(buf)+1);
        strcat(move, buf);
    }
    if (!ui->sel) return move ? move : NULL;
    sfree(ui->sel);
    ui->sel = NULL;
    /* Need to update UI at least, as we cleared the selection */
    return move ? move : UI_UPDATE;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *new_state = NULL;
    const int sz = state->shared->params.w * state->shared->params.h;

    if (*move == 's') {
        int i = 0;
        if (strlen(move) != sz + 1) return NULL;
        new_state = dup_game(state);
        for (++move; i < sz; ++i) new_state->board[i] = move[i] - '0';
        new_state->cheated = true;
    } else {
        int value;
        char *endptr, *delim = strchr(move, '_');
        if (!delim) goto err;
        value = strtol(delim+1, &endptr, 0);
        if (*endptr || endptr == delim+1) goto err;
        if (value < 0 || value > 9) goto err;
        new_state = dup_game(state);
        while (*move) {
            const int i = strtol(move, &endptr, 0);
            if (endptr == move) goto err;
            if (i < 0 || i >= sz) goto err;
            new_state->board[i] = value;
            if (*endptr == '_') break;
            if (*endptr != ',') goto err;
            move = endptr + 1;
        }
    }

    /*
     * Check for completion.
     */
    if (!new_state->completed) {
        const int w = new_state->shared->params.w;
        const int h = new_state->shared->params.h;
        const int sz = w * h;
        int *dsf = make_dsf(NULL, new_state->board, w, h);
        int i;
        for (i = 0; i < sz && new_state->board[i] == dsf_size(dsf, i); ++i);
        sfree(dsf);
        if (i == sz)
            new_state->completed = true;
    }

    return new_state;

err:
    if (new_state) free_game(new_state);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define FLASH_TIME 0.4F

#define COL_CLUE COL_GRID
enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_HIGHLIGHT,
    COL_CORRECT,
    COL_ERROR,
    COL_USER,
    COL_CURSOR,
    NCOLOURS
};

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = (params->w + 1) * tilesize;
    *y = (params->h + 1) * tilesize;
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

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.7F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.7F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.7F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_CORRECT * 3 + 0] = 0.9F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_CORRECT * 3 + 1] = 0.9F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_CORRECT * 3 + 2] = 0.9F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_CURSOR * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_CURSOR * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_CURSOR * 3 + 2] = 0.5F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_ERROR * 3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = PREFERRED_TILE_SIZE;
    ds->started = false;
    ds->params = state->shared->params;
    ds->v = snewn(ds->params.w * ds->params.h, int);
    ds->flags = snewn(ds->params.w * ds->params.h, int);
    for (i = 0; i < ds->params.w * ds->params.h; i++)
	ds->v[i] = ds->flags[i] = -1;
    ds->border_scratch = snewn(ds->params.w * ds->params.h, int);
    ds->dsf_scratch = NULL;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->v);
    sfree(ds->flags);
    sfree(ds->border_scratch);
    sfree(ds->dsf_scratch);
    sfree(ds);
}

#define BORDER_U   0x001
#define BORDER_D   0x002
#define BORDER_L   0x004
#define BORDER_R   0x008
#define BORDER_UR  0x010
#define BORDER_DR  0x020
#define BORDER_UL  0x040
#define BORDER_DL  0x080
#define HIGH_BG    0x100
#define CORRECT_BG 0x200
#define ERROR_BG   0x400
#define USER_COL   0x800
#define CURSOR_SQ 0x1000

static void draw_square(drawing *dr, game_drawstate *ds, int x, int y,
                        int n, int flags)
{
    assert(dr);
    assert(ds);

    /*
     * Clip to the grid square.
     */
    clip(dr, BORDER + x*TILE_SIZE, BORDER + y*TILE_SIZE,
	 TILE_SIZE, TILE_SIZE);

    /*
     * Clear the square.
     */
    draw_rect(dr,
              BORDER + x*TILE_SIZE,
              BORDER + y*TILE_SIZE,
              TILE_SIZE,
              TILE_SIZE,
              (flags & HIGH_BG ? COL_HIGHLIGHT :
               flags & ERROR_BG ? COL_ERROR :
               flags & CORRECT_BG ? COL_CORRECT : COL_BACKGROUND));

    /*
     * Draw the grid lines.
     */
    draw_line(dr, BORDER + x*TILE_SIZE, BORDER + y*TILE_SIZE,
	      BORDER + (x+1)*TILE_SIZE, BORDER + y*TILE_SIZE, COL_GRID);
    draw_line(dr, BORDER + x*TILE_SIZE, BORDER + y*TILE_SIZE,
	      BORDER + x*TILE_SIZE, BORDER + (y+1)*TILE_SIZE, COL_GRID);

    /*
     * Draw the number.
     */
    if (n) {
        char buf[2];
        buf[0] = n + '0';
        buf[1] = '\0';
        draw_text(dr,
                  (x + 1) * TILE_SIZE,
                  (y + 1) * TILE_SIZE,
                  FONT_VARIABLE,
                  TILE_SIZE / 2,
                  ALIGN_VCENTRE | ALIGN_HCENTRE,
                  flags & USER_COL ? COL_USER : COL_CLUE,
                  buf);
    }

    /*
     * Draw bold lines around the borders.
     */
    if (flags & BORDER_L)
        draw_rect(dr,
                  BORDER + x*TILE_SIZE + 1,
                  BORDER + y*TILE_SIZE + 1,
                  BORDER_WIDTH,
                  TILE_SIZE - 1,
                  COL_GRID);
    if (flags & BORDER_U)
        draw_rect(dr,
                  BORDER + x*TILE_SIZE + 1,
                  BORDER + y*TILE_SIZE + 1,
                  TILE_SIZE - 1,
                  BORDER_WIDTH,
                  COL_GRID);
    if (flags & BORDER_R)
        draw_rect(dr,
                  BORDER + (x+1)*TILE_SIZE - BORDER_WIDTH,
                  BORDER + y*TILE_SIZE + 1,
                  BORDER_WIDTH,
                  TILE_SIZE - 1,
                  COL_GRID);
    if (flags & BORDER_D)
        draw_rect(dr,
                  BORDER + x*TILE_SIZE + 1,
                  BORDER + (y+1)*TILE_SIZE - BORDER_WIDTH,
                  TILE_SIZE - 1,
                  BORDER_WIDTH,
                  COL_GRID);
    if (flags & BORDER_UL)
        draw_rect(dr,
                  BORDER + x*TILE_SIZE + 1,
                  BORDER + y*TILE_SIZE + 1,
                  BORDER_WIDTH,
                  BORDER_WIDTH,
                  COL_GRID);
    if (flags & BORDER_UR)
        draw_rect(dr,
                  BORDER + (x+1)*TILE_SIZE - BORDER_WIDTH,
                  BORDER + y*TILE_SIZE + 1,
                  BORDER_WIDTH,
                  BORDER_WIDTH,
                  COL_GRID);
    if (flags & BORDER_DL)
        draw_rect(dr,
                  BORDER + x*TILE_SIZE + 1,
                  BORDER + (y+1)*TILE_SIZE - BORDER_WIDTH,
                  BORDER_WIDTH,
                  BORDER_WIDTH,
                  COL_GRID);
    if (flags & BORDER_DR)
        draw_rect(dr,
                  BORDER + (x+1)*TILE_SIZE - BORDER_WIDTH,
                  BORDER + (y+1)*TILE_SIZE - BORDER_WIDTH,
                  BORDER_WIDTH,
                  BORDER_WIDTH,
                  COL_GRID);

    if (flags & CURSOR_SQ) {
        int coff = TILE_SIZE/8;
        draw_rect_outline(dr,
                          BORDER + x*TILE_SIZE + coff,
                          BORDER + y*TILE_SIZE + coff,
                          TILE_SIZE - coff*2,
                          TILE_SIZE - coff*2,
                          COL_CURSOR);
    }

    unclip(dr);

    draw_update(dr,
		BORDER + x*TILE_SIZE,
		BORDER + y*TILE_SIZE,
		TILE_SIZE,
		TILE_SIZE);
}

static void draw_grid(
    drawing *dr, game_drawstate *ds, const game_state *state,
    const game_ui *ui, bool flashy, bool borders, bool shading)
{
    const int w = state->shared->params.w;
    const int h = state->shared->params.h;
    int x;
    int y;

    /*
     * Build a dsf for the board in its current state, to use for
     * highlights and hints.
     */
    ds->dsf_scratch = make_dsf(ds->dsf_scratch, state->board, w, h);

    /*
     * Work out where we're putting borders between the cells.
     */
    for (y = 0; y < w*h; y++)
	ds->border_scratch[y] = 0;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int dx, dy;
            int v1, s1, v2, s2;

            for (dx = 0; dx <= 1; dx++) {
                bool border = false;

                dy = 1 - dx;

                if (x+dx >= w || y+dy >= h)
                    continue;

                v1 = state->board[y*w+x];
                v2 = state->board[(y+dy)*w+(x+dx)];
                s1 = dsf_size(ds->dsf_scratch, y*w+x);
                s2 = dsf_size(ds->dsf_scratch, (y+dy)*w+(x+dx));

                /*
                 * We only ever draw a border between two cells if
                 * they don't have the same contents.
                 */
                if (v1 != v2) {
                    /*
                     * But in that situation, we don't always draw
                     * a border. We do if the two cells both
                     * contain actual numbers...
                     */
                    if (v1 && v2)
                        border = true;

                    /*
                     * ... or if at least one of them is a
                     * completed or overfull omino.
                     */
                    if (v1 && s1 >= v1)
                        border = true;
                    if (v2 && s2 >= v2)
                        border = true;
                }

                if (border)
                    ds->border_scratch[y*w+x] |= (dx ? 1 : 2);
            }
        }

    /*
     * Actually do the drawing.
     */
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            /*
             * Determine what we need to draw in this square.
             */
            int i = y*w+x, v = state->board[i];
            int flags = 0;

            if (flashy || !shading) {
                /* clear all background flags */
            } else if (ui && ui->sel && ui->sel[i]) {
                flags |= HIGH_BG;
            } else if (v) {
                int size = dsf_size(ds->dsf_scratch, i);
                if (size == v)
                    flags |= CORRECT_BG;
                else if (size > v)
                    flags |= ERROR_BG;
		else {
		    int rt = dsf_canonify(ds->dsf_scratch, i), j;
		    for (j = 0; j < w*h; ++j) {
			int k;
			if (dsf_canonify(ds->dsf_scratch, j) != rt) continue;
			for (k = 0; k < 4; ++k) {
			    const int xx = j % w + dx[k], yy = j / w + dy[k];
			    if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
				state->board[yy*w + xx] == EMPTY)
				goto noflag;
			}
		    }
		    flags |= ERROR_BG;
		  noflag:
		    ;
		}
            }
            if (ui && ui->cur_visible && x == ui->cur_x && y == ui->cur_y)
              flags |= CURSOR_SQ;

            /*
             * Borders at the very edges of the grid are
             * independent of the `borders' flag.
             */
            if (x == 0)
                flags |= BORDER_L;
            if (y == 0)
                flags |= BORDER_U;
            if (x == w-1)
                flags |= BORDER_R;
            if (y == h-1)
                flags |= BORDER_D;

            if (borders) {
                if (x == 0 || (ds->border_scratch[y*w+(x-1)] & 1))
                    flags |= BORDER_L;
                if (y == 0 || (ds->border_scratch[(y-1)*w+x] & 2))
                    flags |= BORDER_U;
                if (x == w-1 || (ds->border_scratch[y*w+x] & 1))
                    flags |= BORDER_R;
                if (y == h-1 || (ds->border_scratch[y*w+x] & 2))
                    flags |= BORDER_D;

                if (y > 0 && x > 0 && (ds->border_scratch[(y-1)*w+(x-1)]))
                    flags |= BORDER_UL;
                if (y > 0 && x < w-1 &&
                    ((ds->border_scratch[(y-1)*w+x] & 1) ||
                     (ds->border_scratch[(y-1)*w+(x+1)] & 2)))
                    flags |= BORDER_UR;
                if (y < h-1 && x > 0 &&
                    ((ds->border_scratch[y*w+(x-1)] & 2) ||
                     (ds->border_scratch[(y+1)*w+(x-1)] & 1)))
                    flags |= BORDER_DL;
                if (y < h-1 && x < w-1 &&
                    ((ds->border_scratch[y*w+(x+1)] & 2) ||
                     (ds->border_scratch[(y+1)*w+x] & 1)))
                    flags |= BORDER_DR;
            }

            if (!state->shared->clues[y*w+x])
                flags |= USER_COL;

            if (ds->v[y*w+x] != v || ds->flags[y*w+x] != flags) {
                draw_square(dr, ds, x, y, v, flags);
                ds->v[y*w+x] = v;
                ds->flags[y*w+x] = flags;
            }
        }
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    const int w = state->shared->params.w;
    const int h = state->shared->params.h;

    const bool flashy =
        flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 || flashtime >= FLASH_TIME*2/3);

    if (!ds->started) {
	/*
	 * Black rectangle which is the main grid.
	 */
	draw_rect(dr, BORDER - BORDER_WIDTH, BORDER - BORDER_WIDTH,
		  w*TILE_SIZE + 2*BORDER_WIDTH + 1,
		  h*TILE_SIZE + 2*BORDER_WIDTH + 1,
		  COL_GRID);

        draw_update(dr, 0, 0, w*TILE_SIZE + 2*BORDER, h*TILE_SIZE + 2*BORDER);

        ds->started = true;
    }

    draw_grid(dr, ds, state, ui, flashy, true, true);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    assert(oldstate);
    assert(newstate);
    assert(newstate->shared);
    assert(oldstate->shared == newstate->shared);
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
    if(ui->cur_visible)
    {
	*x = BORDER + ui->cur_x * TILE_SIZE;
	*y = BORDER + ui->cur_y * TILE_SIZE;
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
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    const int w = state->shared->params.w;
    const int h = state->shared->params.h;
    int c, i;
    bool borders;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate *ds = game_new_drawstate(dr, state);
    game_set_size(dr, ds, NULL, tilesize);

    c = print_mono_colour(dr, 1); assert(c == COL_BACKGROUND);
    c = print_mono_colour(dr, 0); assert(c == COL_GRID);
    c = print_mono_colour(dr, 1); assert(c == COL_HIGHLIGHT);
    c = print_mono_colour(dr, 1); assert(c == COL_CORRECT);
    c = print_mono_colour(dr, 1); assert(c == COL_ERROR);
    c = print_mono_colour(dr, 0); assert(c == COL_USER);

    /*
     * Border.
     */
    draw_rect(dr, BORDER - BORDER_WIDTH, BORDER - BORDER_WIDTH,
              w*TILE_SIZE + 2*BORDER_WIDTH + 1,
              h*TILE_SIZE + 2*BORDER_WIDTH + 1,
              COL_GRID);

    /*
     * We'll draw borders between the ominoes iff the grid is not
     * pristine. So scan it to see if it is.
     */
    borders = false;
    for (i = 0; i < w*h; i++)
        if (state->board[i] && !state->shared->clues[i])
            borders = true;

    /*
     * Draw grid.
     */
    print_line_width(dr, TILE_SIZE / 64);
    draw_grid(dr, ds, state, NULL, false, borders, false);

    /*
     * Clean up.
     */
    game_free_drawstate(dr, ds);
}

#ifdef COMBINED
#define thegame filling
#endif

const struct game thegame = {
    "Filling", "games.filling", "filling",
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
    false,				   /* wants_statusbar */
    false, NULL,                       /* timing_state */
    REQUIRE_NUMPAD,		       /* flags */
};

#ifdef STANDALONE_SOLVER /* solver? hah! */

int main(int argc, char **argv) {
    while (*++argv) {
        game_params *params;
        game_state *state;
        char *par;
        char *desc;

        for (par = desc = *argv; *desc != '\0' && *desc != ':'; ++desc);
        if (*desc == '\0') {
            fprintf(stderr, "bad puzzle id: %s", par);
            continue;
        }

        *desc++ = '\0';

        params = snew(game_params);
        decode_params(params, par);
        state = new_game(NULL, params, desc);
        if (solver(state->board, params->w, params->h, NULL))
            printf("%s:%s: solvable\n", par, desc);
        else
            printf("%s:%s: not solvable\n", par, desc);
    }
    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
