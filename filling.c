/* -*- tab-width: 8; indent-tabs-mode: t -*-
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
 *  - make a somewhat more clever solver
 *     + enable "ghost regions" of size > 1
 *        - one can put an upper bound on the size of a ghost region
 *          by considering the board size and summing present hints.
 *     + for each square, for i=1..n, what is the distance to a region
 *       containing i?  How full is the region?  How is this useful?
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

static unsigned char verbose;

static void printv(char *fmt, ...) {
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
    int h, w;
};

struct shared_state {
    struct game_params params;
    int *clues;
    int refcnt;
};

struct game_state {
    int *board;
    struct shared_state *shared;
    int completed, cheated;
};

static const struct game_params filling_defaults[3] = {{7, 9}, {9, 13}, {13, 17}};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    *ret = filling_defaults[1]; /* struct copy */

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    char buf[64];

    if (i < 0 || i >= lenof(filling_defaults)) return FALSE;
    *params = snew(game_params);
    **params = filling_defaults[i]; /* struct copy */
    sprintf(buf, "%dx%d", filling_defaults[i].h, filling_defaults[i].w);
    *name = dupstr(buf);

    return TRUE;
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

static char *encode_params(const game_params *params, int full)
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

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->w < 1) return "Width must be at least one";
    if (params->h < 1) return "Height must be at least one";

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

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
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

#define SENTINEL sz

/* generate a random valid board; uses validate_board. */
static void make_board(int *board, int w, int h, random_state *rs) {
    int *dsf;

    const unsigned int sz = w * h;

    /* w=h=2 is a special case which requires a number > max(w, h) */
    /* TODO prove that this is the case ONLY for w=h=2. */
    const int maxsize = min(max(max(w, h), 3), 9);

    /* Note that if 1 in {w, h} then it's impossible to have a region
     * of size > w*h, so the special case only affects w=h=2. */

    int nboards = 0;
    int i;

    assert(w >= 1);
    assert(h >= 1);

    assert(board);

    dsf = snew_dsf(sz); /* implicit dsf_init */

    /* I abuse the board variable: when generating the puzzle, it
     * contains a shuffled list of numbers {0, ..., nsq-1}. */
    for (i = 0; i < (int)sz; ++i) board[i] = i;

    while (1) {
	int change;
	++nboards;
	shuffle(board, sz, sizeof (int), rs);
	/* while the board can in principle be fixed */
	do {
	    change = FALSE;
	    for (i = 0; i < (int)sz; ++i) {
		int a = SENTINEL;
		int b = SENTINEL;
		int c = SENTINEL;
		const int aa = dsf_canonify(dsf, board[i]);
		int cc = sz;
		int j;
		for (j = 0; j < 4; ++j) {
		    const int x = (board[i] % w) + dx[j];
		    const int y = (board[i] / w) + dy[j];
		    int bb;
		    if (x < 0 || x >= w || y < 0 || y >= h) continue;
		    bb = dsf_canonify(dsf, w*y + x);
		    if (aa == bb) continue;
		    else if (dsf_size(dsf, aa) == dsf_size(dsf, bb)) {
			a = aa;
			b = bb;
			c = cc;
		    } else if (cc == sz) c = cc = bb;
		}
		if (a != SENTINEL) {
		    a = dsf_canonify(dsf, a);
		    assert(a != dsf_canonify(dsf, b));
		    if (c != sz) assert(a != dsf_canonify(dsf, c));
		    dsf_merge(dsf, a, c == sz? b: c);
		    /* if repair impossible; make a new board */
		    if (dsf_size(dsf, a) > maxsize) goto retry;
		    change = TRUE;
		}
	    }
	} while (change);

	for (i = 0; i < (int)sz; ++i) board[i] = dsf_size(dsf, i);

	sfree(dsf);
	printv("returning board number %d\n", nboards);
	return;

    retry:
	dsf_init(dsf, sz);
    }
    assert(FALSE); /* unreachable */
}

static int rhofree(int *hop, int start) {
    int turtle = start, rabbit = hop[start];
    while (rabbit != turtle) { /* find a cycle */
        turtle = hop[turtle];
        rabbit = hop[hop[rabbit]];
    }
    do { /* check that start is in the cycle */
        rabbit = hop[rabbit];
        if (start == rabbit) return 1;
    } while (rabbit != turtle);
    return 0;
}

static void merge(int *dsf, int *connected, int a, int b) {
    int c;
    assert(dsf);
    assert(connected);
    assert(rhofree(connected, a));
    assert(rhofree(connected, b));
    a = dsf_canonify(dsf, a);
    b = dsf_canonify(dsf, b);
    if (a == b) return;
    dsf_merge(dsf, a, b);
    c = connected[a];
    connected[a] = connected[b];
    connected[b] = c;
    assert(rhofree(connected, a));
    assert(rhofree(connected, b));
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

static int check_capacity(int *board, int w, int h, int i) {
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

static int learn_expand_or_one(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    int learn = FALSE;

    assert(s);

    for (i = 0; i < sz; ++i) {
	int j;
	int one = TRUE;

	if (s->board[i] != EMPTY) continue;

	for (j = 0; j < 4; ++j) {
	    const int x = (i % w) + dx[j];
	    const int y = (i / w) + dy[j];
	    const int idx = w*y + x;
	    if (x < 0 || x >= w || y < 0 || y >= h) continue;
	    if (s->board[idx] == EMPTY) {
		one = FALSE;
		continue;
	    }
	    if (one &&
		(s->board[idx] == 1 ||
		 (s->board[idx] >= expandsize(s->board, s->dsf, w, h,
					      i, s->board[idx]))))
		one = FALSE;
	    assert(s->board[i] == EMPTY);
	    s->board[i] = -SENTINEL;
	    if (check_capacity(s->board, w, h, idx)) continue;
	    assert(s->board[i] == EMPTY);
	    printv("learn: expanding in one\n");
	    expand(s, w, h, i, idx);
	    learn = TRUE;
	    break;
	}

	if (j == 4 && one) {
	    printv("learn: one at (%d, %d)\n", i % w, i / w);
	    assert(s->board[i] == EMPTY);
	    s->board[i] = 1;
	    assert(s->nempty);
	    --s->nempty;
	    learn = TRUE;
	}
    }
    return learn;
}

static int learn_blocked_expansion(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    int learn = FALSE;

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
	learn = TRUE;

        next_i:
        ;
    }
    /* end: for each connected component */
    return learn;
}

static int learn_critical_square(struct solver_state *s, int w, int h) {
    const int sz = w * h;
    int i;
    int learn = FALSE;
    assert(s);

    /* for each connected component */
    for (i = 0; i < sz; ++i) {
	int j;
	if (s->board[i] == EMPTY) continue;
	if (i != dsf_canonify(s->dsf, i)) continue;
	if (dsf_size(s->dsf, i) == s->board[i]) continue;
	assert(s->board[i] != 1);
	/* for each empty square */
	for (j = 0; j < sz; ++j) {
	    if (s->board[j] != EMPTY) continue;
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
	    learn = TRUE;
	}
    }
    return learn;
}

static int solver(const int *orig, int w, int h, char **solution) {
    const int sz = w * h;

    struct solver_state ss;
    ss.board = memdup(orig, sz, sizeof (int));
    ss.dsf = snew_dsf(sz); /* eqv classes: connected components */
    ss.connected = snewn(sz, int); /* connected[n] := n.next; */
    /* cyclic disjoint singly linked lists, same partitioning as dsf.
     * The lists lets you iterate over a partition given any member */

    printv("trying to solve this:\n");
    print_board(ss.board, w, h);

    init_solver_state(&ss, w, h);
    do {
	if (learn_blocked_expansion(&ss, w, h)) continue;
	if (learn_expand_or_one(&ss, w, h)) continue;
	if (learn_critical_square(&ss, w, h)) continue;
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
        /* We don't need the \0 for execute_move (the only user)
         * I'm just being printf-friendly in case I wanna print */
    }

    sfree(ss.dsf);
    sfree(ss.board);
    sfree(ss.connected);

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

/*
static int filled(int *board, int *randomize, int k, int n) {
    int i;
    if (board == NULL) return FALSE;
    if (randomize == NULL) return FALSE;
    if (k > n) return FALSE;
    for (i = 0; i < k; ++i) if (board[randomize[i]] == 0) return FALSE;
    for (; i < n; ++i) if (board[randomize[i]] != 0) return FALSE;
    return TRUE;
}
*/

static int *g_board;
static int compare(const void *pa, const void *pb) {
    if (!g_board) return 0;
    return g_board[*(const int *)pb] - g_board[*(const int *)pa];
}

static void minimize_clue_set(int *board, int w, int h, int *randomize) {
    const int sz = w * h;
    int i;
    int *board_cp = snewn(sz, int);
    memcpy(board_cp, board, sz * sizeof (int));

    /* since more clues only helps and never hurts, one pass will do
     * just fine: if we can remove clue n with k clues of index > n,
     * we could have removed clue n with >= k clues of index > n.
     * So an additional pass wouldn't do anything [use induction]. */
    for (i = 0; i < sz; ++i) {
	if (board[randomize[i]] == EMPTY) continue;
        board[randomize[i]] = EMPTY;
	/* (rot.) symmetry tends to include _way_ too many hints */
	/* board[sz - randomize[i] - 1] = EMPTY; */
        if (!solver(board, w, h, NULL)) {
            board[randomize[i]] = board_cp[randomize[i]];
	    /* board[sz - randomize[i] - 1] =
	       board_cp[sz - randomize[i] - 1]; */
	}
    }

    sfree(board_cp);
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    const int w = params->w;
    const int h = params->h;
    const int sz = w * h;
    int *board = snewn(sz, int);
    int *randomize = snewn(sz, int);
    char *game_description = snewn(sz + 1, char);
    int i;

    for (i = 0; i < sz; ++i) {
        board[i] = EMPTY;
        randomize[i] = i;
    }

    make_board(board, w, h, rs);
    g_board = board;
    qsort(randomize, sz, sizeof (int), compare);
    minimize_clue_set(board, w, h, randomize);

    for (i = 0; i < sz; ++i) {
        assert(board[i] >= 0);
        assert(board[i] < 10);
        game_description[i] = board[i] + '0';
    }
    game_description[sz] = '\0';

/*
    solver(board, w, h, aux);
    print_board(board, w, h);
*/

    sfree(randomize);
    sfree(board);

    return game_description;
}

static char *validate_desc(const game_params *params, const char *desc)
{
    int i;
    const int sz = params->w * params->h;
    const char m = '0' + max(max(params->w, params->h), 3);

    printv("desc = '%s'; sz = %d\n", desc, sz);

    for (i = 0; desc[i] && i < sz; ++i)
        if (!isdigit((unsigned char) *desc))
	    return "non-digit in string";
	else if (desc[i] > m)
	    return "too large digit in string";
    if (desc[i]) return "string too long";
    else if (i < sz) return "string too short";
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int sz = params->w * params->h;
    int i;

    state->cheated = state->completed = FALSE;
    state->shared = snew(struct shared_state);
    state->shared->refcnt = 1;
    state->shared->params = *params; /* struct copy */
    state->shared->clues = snewn(sz, int);
    for (i = 0; i < sz; ++i) state->shared->clues[i] = desc[i] - '0';
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
                        const char *aux, char **error)
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
    int *sel; /* w*h highlighted squares, or NULL */
    int cur_x, cur_y, cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->sel = NULL;
    ui->cur_x = ui->cur_y = ui->cur_visible = 0;

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
}

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define BORDER (TILE_SIZE / 2)
#define BORDER_WIDTH (max(TILE_SIZE / 32, 1))

struct game_drawstate {
    struct game_params params;
    int tilesize;
    int started;
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
                ui->sel = snewn(w*h, int);
                memset(ui->sel, 0, w*h*sizeof(int));
            }
            if (!state->shared->clues[w*ty+tx])
                ui->sel[w*ty+tx] = 1;
        }
        ui->cur_visible = 0;
        return ""; /* redraw */
    }

    if (IS_CURSOR_MOVE(button)) {
        ui->cur_visible = 1;
        move_cursor(button, &ui->cur_x, &ui->cur_y, w, h, 0);
        return "";
    }
    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = 1;
            return "";
        }
        if (!ui->sel) {
            ui->sel = snewn(w*h, int);
            memset(ui->sel, 0, w*h*sizeof(int));
        }
        if (state->shared->clues[w*ui->cur_y + ui->cur_x] == 0)
            ui->sel[w*ui->cur_y + ui->cur_x] ^= 1;
        return "";
    }

    switch (button) {
      case ' ':
      case '\r':
      case '\n':
      case '\b':
        button = 0;
        break;
      default:
        if (button < '0' || button > '9') return NULL;
        button -= '0';
        if (button > (w == 2 && h == 2? 3: max(w, h))) return NULL;
    }

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
    return move ? move : "";
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *new_state = NULL;
    const int sz = state->shared->params.w * state->shared->params.h;

    if (*move == 's') {
        int i = 0;
        new_state = dup_game(state);
        for (++move; i < sz; ++i) new_state->board[i] = move[i] - '0';
        new_state->cheated = TRUE;
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
            new_state->completed = TRUE;
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

    ret[COL_HIGHLIGHT * 3 + 0] = 0.85F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];

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
    ds->started = 0;
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

static void draw_grid(drawing *dr, game_drawstate *ds, const game_state *state,
                      const game_ui *ui, int flashy, int borders, int shading)
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
                int border = FALSE;

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
                        border = TRUE;

                    /*
                     * ... or if at least one of them is a
                     * completed or overfull omino.
                     */
                    if (v1 && s1 >= v1)
                        border = TRUE;
                    if (v2 && s2 >= v2)
                        border = TRUE;
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

    const int flashy =
        flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 || flashtime >= FLASH_TIME*2/3);

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed and
         * can vary with front ends. To be on the safe side, all games
         * should start by drawing a big background-colour rectangle
         * covering the whole window.
         */
        draw_rect(dr, 0, 0, w*TILE_SIZE + 2*BORDER, h*TILE_SIZE + 2*BORDER,
                  COL_BACKGROUND);

	/*
	 * Smaller black rectangle which is the main grid.
	 */
	draw_rect(dr, BORDER - BORDER_WIDTH, BORDER - BORDER_WIDTH,
		  w*TILE_SIZE + 2*BORDER_WIDTH + 1,
		  h*TILE_SIZE + 2*BORDER_WIDTH + 1,
		  COL_GRID);

        draw_update(dr, 0, 0, w*TILE_SIZE + 2*BORDER, h*TILE_SIZE + 2*BORDER);

        ds->started = TRUE;
    }

    draw_grid(dr, ds, state, ui, flashy, TRUE, TRUE);
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

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
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
    int c, i, borders;

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
    borders = FALSE;
    for (i = 0; i < w*h; i++)
        if (state->board[i] && !state->shared->clues[i])
            borders = TRUE;

    /*
     * Draw grid.
     */
    print_line_width(dr, TILE_SIZE / 64);
    draw_grid(dr, ds, state, NULL, FALSE, borders, FALSE);

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
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    TRUE, FALSE, game_print_size, game_print,
    FALSE,				   /* wants_statusbar */
    FALSE, game_timing_state,
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
