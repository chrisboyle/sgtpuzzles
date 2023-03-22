/*
 * 'same game' -- try to remove all the coloured squares by
 *                selecting regions of contiguous colours.
 */

/*
 * TODO on grid generation:
 * 
 *  - Generation speed could still be improved.
 *     * 15x10c3 is the only really difficult one of the existing
 *       presets. The others are all either small enough, or have
 *       the great flexibility given by four colours, that they
 *       don't take long at all.
 *     * I still suspect many problems arise from separate
 * 	 subareas. I wonder if we can also somehow prioritise left-
 * 	 or rightmost insertions so as to avoid area splitting at
 * 	 all where feasible? It's not easy, though, because the
 * 	 current shuffle-then-try-all-options approach to move
 * 	 choice doesn't leave room for `soft' probabilistic
 * 	 prioritisation: we either try all class A moves before any
 * 	 class B ones, or we don't.
 *
 *  - The current generation algorithm inserts exactly two squares
 *    at a time, with a single exception at the beginning of
 *    generation for grids of odd overall size. An obvious
 *    extension would be to permit larger inverse moves during
 *    generation.
 *     * this might reduce the number of failed generations by
 *       making the insertion algorithm more flexible
 *     * on the other hand, it would be significantly more complex
 *     * if I do this I'll need to take out the odd-subarea
 *       avoidance
 *     * a nice feature of the current algorithm is that the
 *       computer's `intended' solution always receives the minimum
 *       possible score, so that pretty much the player's entire
 *       score represents how much better they did than the
 *       computer.
 *
 *  - Is it possible we can _temporarily_ tolerate neighbouring
 *    squares of the same colour, until we've finished setting up
 *    our inverse move?
 *     * or perhaps even not choose the colour of our inserted
 *       region until we have finished placing it, and _then_ look
 *       at what colours border on it?
 *     * I don't think this is currently meaningful unless we're
 *       placing more than a domino at a time.
 *
 *  - possibly write out a full solution so that Solve can somehow
 *    show it step by step?
 *     * aux_info would have to encode the click points
 *     * solve_game() would have to encode not only those click
 * 	 points but also give a move string which reconstructed the
 * 	 initial state
 *     * the game_state would include a pointer to a solution move
 * 	 list, plus an index into that list
 *     * game_changed_state would auto-select the next move if
 * 	 handed a new state which had a solution move list active
 *     * execute_move, if passed such a state as input, would check
 * 	 to see whether the move being made was the same as the one
 * 	 stated by the solution, and if so would advance the move
 * 	 index. Failing that it would return a game_state without a
 * 	 solution move list active at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

#define TILE_INNER (ds->tileinner)
#define TILE_GAP (ds->tilegap)
#define TILE_SIZE (TILE_INNER + TILE_GAP)
#define PREFERRED_TILE_SIZE 32
#define BORDER (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH 2

#define FLASH_FRAME 0.13F

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define X(state, i) ( (i) % (state)->params.w )
#define Y(state, i) ( (i) / (state)->params.w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

enum {
    COL_BACKGROUND,
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8, COL_9,
    COL_IMPOSSIBLE, COL_SEL, COL_HIGHLIGHT, COL_LOWLIGHT,
    NCOLOURS
};

/* scoresub is 1 or 2 (for (n-1)^2 or (n-2)^2) */
struct game_params {
    int w, h, ncols, scoresub;
    bool soluble;                    /* choose generation algorithm */
};

/* These flags must be unique across all uses; in the game_state,
 * the game_ui, and the drawstate (as they all get combined in the
 * drawstate). */
#define TILE_COLMASK    0x00ff
#define TILE_SELECTED   0x0100 /* used in ui and drawstate */
#define TILE_JOINRIGHT  0x0200 /* used in drawstate */
#define TILE_JOINDOWN   0x0400 /* used in drawstate */
#define TILE_JOINDIAG   0x0800 /* used in drawstate */
#define TILE_HASSEL     0x1000 /* used in drawstate */
#define TILE_IMPOSSIBLE 0x2000 /* used in drawstate */

#define TILE(gs,x,y) ((gs)->tiles[(gs)->params.w*(y)+(x)])
#define COL(gs,x,y) (TILE(gs,x,y) & TILE_COLMASK)
#define ISSEL(gs,x,y) (TILE(gs,x,y) & TILE_SELECTED)

#define SWAPTILE(gs,x1,y1,x2,y2) do {   \
    int t = TILE(gs,x1,y1);               \
    TILE(gs,x1,y1) = TILE(gs,x2,y2);      \
    TILE(gs,x2,y2) = t;                   \
} while (0)

static int npoints(const game_params *params, int nsel)
{
    int sdiff = nsel - params->scoresub;
    return (sdiff > 0) ? sdiff * sdiff : 0;
}

struct game_state {
    struct game_params params;
    int n;
    int *tiles; /* colour only */
    int score;
    bool complete, impossible;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    ret->w = 5;
    ret->h = 5;
    ret->ncols = 3;
    ret->scoresub = 2;
    ret->soluble = true;
    return ret;
}

static const struct game_params samegame_presets[] = {
    { 5, 5, 3, 2, true },
    { 10, 5, 3, 2, true },
#ifdef SLOW_SYSTEM
    { 10, 10, 3, 2, true },
#else
    { 15, 10, 3, 2, true },
#endif
    { 15, 10, 4, 2, true },
    { 20, 15, 4, 2, true }
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(samegame_presets))
	return false;

    ret = snew(game_params);
    *ret = samegame_presets[i];

    sprintf(str, "%dx%d, %d colours", ret->w, ret->h, ret->ncols);

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

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
	p++;
	params->h = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->h = params->w;
    }
    if (*p == 'c') {
	p++;
	params->ncols = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->ncols = 3;
    }
    if (*p == 's') {
	p++;
	params->scoresub = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    } else {
	params->scoresub = 2;
    }
    if (*p == 'r') {
	p++;
	params->soluble = false;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[80];

    sprintf(ret, "%dx%dc%ds%d%s",
	    params->w, params->h, params->ncols, params->scoresub,
	    full && !params->soluble ? "r" : "");
    return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "No. of colours";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->ncols);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Scoring system";
    ret[3].type = C_CHOICES;
    ret[3].u.choices.choicenames = ":(n-1)^2:(n-2)^2";
    ret[3].u.choices.selected = params->scoresub-1;

    ret[4].name = "Ensure solubility";
    ret[4].type = C_BOOLEAN;
    ret[4].u.boolean.bval = params->soluble;

    ret[5].name = NULL;
    ret[5].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->ncols = atoi(cfg[2].u.string.sval);
    ret->scoresub = cfg[3].u.choices.selected + 1;
    ret->soluble = cfg[4].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 1 || params->h < 1)
	return "Width and height must both be positive";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";

    if (params->ncols > 9)
	return "Maximum of 9 colours";

    if (params->soluble) {
	if (params->ncols < 3)
	    return "Number of colours must be at least three";
	if (params->w * params->h <= 1)
	    return "Grid area must be greater than 1";
    } else {
	if (params->ncols < 2)
	    return "Number of colours must be at least three";
	/* ...and we must make sure we can generate at least 2 squares
	 * of each colour so it's theoretically soluble. */
	if ((params->w * params->h) < (params->ncols * 2))
	    return "Too many colours makes given grid size impossible";
    }

    if ((params->scoresub < 1) || (params->scoresub > 2))
	return "Scoring system not recognised";

    return NULL;
}

/*
 * Guaranteed-soluble grid generator.
 */
static void gen_grid(int w, int h, int nc, int *grid, random_state *rs)
{
    int wh = w*h, tc = nc+1;
    int i, j, k, c, x, y, pos, n;
    int *list, *grid2;
    bool ok;
    int failures = 0;

    /*
     * We'll use `list' to track the possible places to put our
     * next insertion. There are up to h places to insert in each
     * column: in a column of height n there are n+1 places because
     * we can insert at the very bottom or the very top, but a
     * column of height h can't have anything at all inserted in it
     * so we have up to h in each column. Likewise, with n columns
     * present there are n+1 places to fit a new one in between but
     * we can't insert a column if there are already w; so there
     * are a maximum of w new columns too. Total is wh + w.
     */
    list = snewn(wh + w, int);
    grid2 = snewn(wh, int);

    do {
        /*
         * Start with two or three squares - depending on parity of w*h
         * - of a random colour.
         */
        for (i = 0; i < wh; i++)
            grid[i] = 0;
        j = 2 + (wh % 2);
        c = 1 + random_upto(rs, nc);
	if (j <= w) {
	    for (i = 0; i < j; i++)
		grid[(h-1)*w+i] = c;
	} else {
	    assert(j <= h);
	    for (i = 0; i < j; i++)
		grid[(h-1-i)*w] = c;
	}

        /*
         * Now repeatedly insert a two-square blob in the grid, of
         * whatever colour will go at the position we chose.
         */
        while (1) {
            n = 0;

            /*
             * Build up a list of insertion points. Each point is
             * encoded as y*w+x; insertion points between columns are
             * encoded as h*w+x.
             */

            if (grid[wh - 1] == 0) {
                /*
                 * The final column is empty, so we can insert new
                 * columns.
                 */
                for (i = 0; i < w; i++) {
                    list[n++] = wh + i;
                    if (grid[(h-1)*w + i] == 0)
                        break;
                }
            }

            /*
             * Now look for places to insert within columns.
             */
            for (i = 0; i < w; i++) {
                if (grid[(h-1)*w+i] == 0)
                    break;		       /* no more columns */

                if (grid[i] != 0)
                    continue;	       /* this column is full */

                for (j = h; j-- > 0 ;) {
                    list[n++] = j*w+i;
                    if (grid[j*w+i] == 0)
                        break;	       /* this column is exhausted */
                }
            }

            if (n == 0)
                break;		       /* we're done */

#ifdef GENERATION_DIAGNOSTICS
            printf("initial grid:\n");
            {
                int x,y;
                for (y = 0; y < h; y++) {
                    for (x = 0; x < w; x++) {
                        if (grid[y*w+x] == 0)
                            printf("-");
                        else
                            printf("%d", grid[y*w+x]);
                    }
                    printf("\n");
                }
            }
#endif

            /*
             * Now go through the list one element at a time in
             * random order, and actually attempt to insert
             * something there.
             */
            while (n-- > 0) {
                int dirs[4], ndirs, dir;

                i = random_upto(rs, n+1);
                pos = list[i];
                list[i] = list[n];

                x = pos % w;
                y = pos / w;

                memcpy(grid2, grid, wh * sizeof(int));

                if (y == h) {
                    /*
                     * Insert a column at position x.
                     */
                    for (i = w-1; i > x; i--)
                        for (j = 0; j < h; j++)
                            grid2[j*w+i] = grid2[j*w+(i-1)];
                    /*
                     * Clear the new column.
                     */
                    for (j = 0; j < h; j++)
                        grid2[j*w+x] = 0;
                    /*
                     * Decrement y so that our first square is actually
                     * inserted _in_ the grid rather than just below it.
                     */
                    y--;
                }

                /*
                 * Insert a square within column x at position y.
                 */
                for (i = 0; i+1 <= y; i++)
                    grid2[i*w+x] = grid2[(i+1)*w+x];

#ifdef GENERATION_DIAGNOSTICS
                printf("trying at n=%d (%d,%d)\n", n, x, y);
                grid2[y*w+x] = tc;
                {
                    int x,y;
                    for (y = 0; y < h; y++) {
                        for (x = 0; x < w; x++) {
                            if (grid2[y*w+x] == 0)
                                printf("-");
                            else if (grid2[y*w+x] <= nc)
                                printf("%d", grid2[y*w+x]);
                            else
                                printf("*");
                        }
                        printf("\n");
                    }
                }
#endif

                /*
                 * Pick our square colour so that it doesn't match any
                 * of its neighbours.
                 */
                {
                    int wrongcol[4], nwrong = 0;

                    /*
                     * List the neighbouring colours.
                     */
                    if (x > 0)
                        wrongcol[nwrong++] = grid2[y*w+(x-1)];
                    if (x+1 < w)
                        wrongcol[nwrong++] = grid2[y*w+(x+1)];
                    if (y > 0)
                        wrongcol[nwrong++] = grid2[(y-1)*w+x];
                    if (y+1 < h)
                        wrongcol[nwrong++] = grid2[(y+1)*w+x];

                    /*
                     * Eliminate duplicates. We can afford a shoddy
                     * algorithm here because the problem size is
                     * bounded.
                     */
                    for (i = j = 0 ;; i++) {
                        int pos = -1, min = 0;
                        if (j > 0)
                            min = wrongcol[j-1];
                        for (k = i; k < nwrong; k++)
                            if (wrongcol[k] > min &&
                                (pos == -1 || wrongcol[k] < wrongcol[pos]))
                                pos = k;
                        if (pos >= 0) {
                            int v = wrongcol[pos];
                            wrongcol[pos] = wrongcol[j];
                            wrongcol[j++] = v;
                        } else
                            break;
                    }
                    nwrong = j;

                    /*
                     * If no colour will go here, stop trying.
                     */
                    if (nwrong == nc)
                        continue;

                    /*
                     * Otherwise, pick a colour from the remaining
                     * ones.
                     */
                    c = 1 + random_upto(rs, nc - nwrong);
                    for (i = 0; i < nwrong; i++) {
                        if (c >= wrongcol[i])
                            c++;
                        else
                            break;
                    }
                }

                /*
                 * Place the new square.
                 * 
                 * Although I've _chosen_ the new region's colour
                 * (so that we can check adjacency), I'm going to
                 * actually place it as an invalid colour (tc)
                 * until I'm sure it's viable. This is so that I
                 * can conveniently check that I really have made a
                 * _valid_ inverse move later on.
                 */
#ifdef GENERATION_DIAGNOSTICS
                printf("picked colour %d\n", c);
#endif
                grid2[y*w+x] = tc;

                /*
                 * Now attempt to extend it in one of three ways: left,
                 * right or up.
                 */
                ndirs = 0;
                if (x > 0 &&
                    grid2[y*w+(x-1)] != c &&
                    grid2[x-1] == 0 &&
                    (y+1 >= h || grid2[(y+1)*w+(x-1)] != c) &&
                    (y+1 >= h || grid2[(y+1)*w+(x-1)] != 0) &&
                    (x <= 1 || grid2[y*w+(x-2)] != c))
                    dirs[ndirs++] = -1;    /* left */
                if (x+1 < w &&
                    grid2[y*w+(x+1)] != c &&
                    grid2[x+1] == 0 &&
                    (y+1 >= h || grid2[(y+1)*w+(x+1)] != c) &&
                    (y+1 >= h || grid2[(y+1)*w+(x+1)] != 0) &&
                    (x+2 >= w || grid2[y*w+(x+2)] != c))
                    dirs[ndirs++] = +1;    /* right */
                if (y > 0 &&
                    grid2[x] == 0 &&
                    (x <= 0 || grid2[(y-1)*w+(x-1)] != c) &&
                    (x+1 >= w || grid2[(y-1)*w+(x+1)] != c)) {
                    /*
                     * We add this possibility _twice_, so that the
                     * probability of placing a vertical domino is
                     * about the same as that of a horizontal. This
                     * should yield less bias in the generated
                     * grids.
                     */
                    dirs[ndirs++] = 0;     /* up */
                    dirs[ndirs++] = 0;     /* up */
                }

                if (ndirs == 0)
                    continue;

                dir = dirs[random_upto(rs, ndirs)];

#ifdef GENERATION_DIAGNOSTICS
                printf("picked dir %d\n", dir);
#endif

                /*
                 * Insert a square within column (x+dir) at position y.
                 */
                for (i = 0; i+1 <= y; i++)
                    grid2[i*w+x+dir] = grid2[(i+1)*w+x+dir];
                grid2[y*w+x+dir] = tc;

                /*
                 * See if we've divided the remaining grid squares
                 * into sub-areas. If so, we need every sub-area to
                 * have an even area or we won't be able to
                 * complete generation.
                 * 
                 * If the height is odd and not all columns are
                 * present, we can increase the area of a subarea
                 * by adding a new column in it, so in that
                 * situation we don't mind having as many odd
                 * subareas as there are spare columns.
                 * 
                 * If the height is even, we can't fix it at all.
                 */
                {
                    int nerrs = 0, nfix = 0;
                    k = 0;             /* current subarea size */
                    for (i = 0; i < w; i++) {
                        if (grid2[(h-1)*w+i] == 0) {
                            if (h % 2)
                                nfix++;
                            continue;
                        }
                        for (j = 0; j < h && grid2[j*w+i] == 0; j++);
                        assert(j < h);
                        if (j == 0) {
                            /*
                             * End of previous subarea.
                             */
                            if (k % 2)
                                nerrs++;
                            k = 0;
                        } else {
                            k += j;
                        }
                    }
                    if (k % 2)
                        nerrs++;
                    if (nerrs > nfix)
                        continue;      /* try a different placement */
                }

                /*
                 * We've made a move. Verify that it is a valid
                 * move and that if made it would indeed yield the
                 * previous grid state. The criteria are:
                 * 
                 *  (a) removing all the squares of colour tc (and
                 *      shuffling the columns up etc) from grid2
                 *      would yield grid
                 *  (b) no square of colour tc is adjacent to one
                 *      of colour c
                 *  (c) all the squares of colour tc form a single
                 *      connected component
                 * 
                 * We verify the latter property at the same time
                 * as checking that removing all the tc squares
                 * would yield the previous grid. Then we colour
                 * the tc squares in colour c by breadth-first
                 * search, which conveniently permits us to test
                 * that they're all connected.
                 */
                {
                    int x1, x2, y1, y2;
                    bool ok = true;
                    int fillstart = -1, ntc = 0;

#ifdef GENERATION_DIAGNOSTICS
                    {
                        int x,y;
                        printf("testing move (new, old):\n");
                        for (y = 0; y < h; y++) {
                            for (x = 0; x < w; x++) {
                                if (grid2[y*w+x] == 0)
                                    printf("-");
                                else if (grid2[y*w+x] <= nc)
                                    printf("%d", grid2[y*w+x]);
                                else
                                    printf("*");
                            }
                            printf("   ");
                            for (x = 0; x < w; x++) {
                                if (grid[y*w+x] == 0)
                                    printf("-");
                                else
                                    printf("%d", grid[y*w+x]);
                            }
                            printf("\n");
                        }
                    }
#endif

                    for (x1 = x2 = 0; x2 < w; x2++) {
                        bool usedcol = false;

                        for (y1 = y2 = h-1; y2 >= 0; y2--) {
                            if (grid2[y2*w+x2] == tc) {
                                ntc++;
                                if (fillstart == -1)
                                    fillstart = y2*w+x2;
                                if ((y2+1 < h && grid2[(y2+1)*w+x2] == c) ||
                                    (y2-1 >= 0 && grid2[(y2-1)*w+x2] == c) ||
                                    (x2+1 < w && grid2[y2*w+x2+1] == c) ||
                                    (x2-1 >= 0 && grid2[y2*w+x2-1] == c)) {
#ifdef GENERATION_DIAGNOSTICS
                                    printf("adjacency failure at %d,%d\n",
                                           x2, y2);
#endif
                                    ok = false;
                                }
                                continue;
                            }
                            if (grid2[y2*w+x2] == 0)
                                break;
                            usedcol = true;
                            if (grid2[y2*w+x2] != grid[y1*w+x1]) {
#ifdef GENERATION_DIAGNOSTICS
                                printf("matching failure at %d,%d vs %d,%d\n",
                                       x2, y2, x1, y1);
#endif
                                ok = false;
                            }
                            y1--;
                        }

                        /*
                         * If we've reached the top of the column
                         * in grid2, verify that we've also reached
                         * the top of the column in `grid'.
                         */
                        if (usedcol) {
                            while (y1 >= 0) {
                                if (grid[y1*w+x1] != 0) {
#ifdef GENERATION_DIAGNOSTICS
                                    printf("junk at column top (%d,%d)\n",
                                           x1, y1);
#endif
                                    ok = false;
                                }
                                y1--;
                            }
                        }

                        if (!ok)
                            break;

                        if (usedcol)
                            x1++;
                    }

                    if (!ok) {
                        assert(!"This should never happen");

                        /*
                         * If this game is compiled NDEBUG so that
                         * the assertion doesn't bring it to a
                         * crashing halt, the only thing we can do
                         * is to give up, loop round again, and
                         * hope to randomly avoid making whatever
                         * type of move just caused this failure.
                         */
                        continue;
                    }

                    /*
                     * Now use bfs to fill in the tc section as
                     * colour c. We use `list' to store the set of
                     * squares we have to process.
                     */
                    i = j = 0;
                    assert(fillstart >= 0);
                    list[i++] = fillstart;
#ifdef OUTPUT_SOLUTION
                    printf("M");
#endif
                    while (j < i) {
                        k = list[j];
                        x = k % w;
                        y = k / w;
#ifdef OUTPUT_SOLUTION
                        printf("%s%d", j ? "," : "", k);
#endif
                        j++;

                        assert(grid2[k] == tc);
                        grid2[k] = c;

                        if (x > 0 && grid2[k-1] == tc)
                            list[i++] = k-1;
                        if (x+1 < w && grid2[k+1] == tc)
                            list[i++] = k+1;
                        if (y > 0 && grid2[k-w] == tc)
                            list[i++] = k-w;
                        if (y+1 < h && grid2[k+w] == tc)
                            list[i++] = k+w;
                    }
#ifdef OUTPUT_SOLUTION
                    printf("\n");
#endif

                    /*
                     * Check that we've filled the same number of
                     * tc squares as we originally found.
                     */
                    assert(j == ntc);
                }

                memcpy(grid, grid2, wh * sizeof(int));

                break;		       /* done it! */
            }

#ifdef GENERATION_DIAGNOSTICS
            {
                int x,y;
                printf("n=%d\n", n);
                for (y = 0; y < h; y++) {
                    for (x = 0; x < w; x++) {
                        if (grid[y*w+x] == 0)
                            printf("-");
                        else
                            printf("%d", grid[y*w+x]);
                    }
                    printf("\n");
                }
            }
#endif

            if (n < 0)
                break;
        }

        ok = true;
        for (i = 0; i < wh; i++)
            if (grid[i] == 0) {
                ok = false;
                failures++;
#if defined GENERATION_DIAGNOSTICS || defined SHOW_INCOMPLETE
                {
                    int x,y;
                    printf("incomplete grid:\n");
                    for (y = 0; y < h; y++) {
                        for (x = 0; x < w; x++) {
                            if (grid[y*w+x] == 0)
                                printf("-");
                            else
                                printf("%d", grid[y*w+x]);
                        }
                        printf("\n");
                    }
                }
#endif
                break;
            }

    } while (!ok);

#if defined GENERATION_DIAGNOSTICS || defined COUNT_FAILURES
    printf("%d failures\n", failures);
#endif
#ifdef GENERATION_DIAGNOSTICS
    {
        int x,y;
        printf("final grid:\n");
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                printf("%d", grid[y*w+x]);
            }
            printf("\n");
        }
    }
#endif

    sfree(grid2);
    sfree(list);
}

/*
 * Not-guaranteed-soluble grid generator; kept as a legacy, and in
 * case someone finds the slightly odd quality of the guaranteed-
 * soluble grids to be aesthetically displeasing or finds its CPU
 * utilisation to be excessive.
 */
static void gen_grid_random(int w, int h, int nc, int *grid, random_state *rs)
{
    int i, j, c;
    int n = w * h;

    for (i = 0; i < n; i++)
	grid[i] = 0;

    /*
     * Our sole concession to not gratuitously generating insoluble
     * grids is to ensure we have at least two of every colour.
     */
    for (c = 1; c <= nc; c++) {
	for (j = 0; j < 2; j++) {
	    do {
		i = (int)random_upto(rs, n);
	    } while (grid[i] != 0);
	    grid[i] = c;
	}
    }

    /*
     * Fill in the rest of the grid at random.
     */
    for (i = 0; i < n; i++) {
	if (grid[i] == 0)
	    grid[i] = (int)random_upto(rs, nc)+1;
    }
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    char *ret;
    int n, i, retlen, *tiles;

    n = params->w * params->h;
    tiles = snewn(n, int);

    if (params->soluble)
	gen_grid(params->w, params->h, params->ncols, tiles, rs);
    else
	gen_grid_random(params->w, params->h, params->ncols, tiles, rs);

    ret = NULL;
    retlen = 0;
    for (i = 0; i < n; i++) {
	char buf[80];
	int k;

	k = sprintf(buf, "%d,", tiles[i]);
	ret = sresize(ret, retlen + k + 1, char);
	strcpy(ret + retlen, buf);
	retlen += k;
    }
    ret[retlen-1] = '\0'; /* delete last comma */

    sfree(tiles);
    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int area = params->w * params->h, i;
    const char *p = desc;

    for (i = 0; i < area; i++) {
	const char *q = p;
	int n;

	if (!isdigit((unsigned char)*p))
	    return "Not enough numbers in string";
	while (isdigit((unsigned char)*p)) p++;

	if (i < area-1 && *p != ',')
	    return "Expected comma after number";
	else if (i == area-1 && *p)
	    return "Excess junk at end of string";

	n = atoi(q);
	if (n < 0 || n > params->ncols)
	    return "Colour out of range";

	if (*p) p++; /* eat comma */
    }
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    const char *p = desc;
    int i;

    state->params = *params; /* struct copy */
    state->n = state->params.w * state->params.h;
    state->tiles = snewn(state->n, int);

    for (i = 0; i < state->n; i++) {
	assert(*p);
	state->tiles[i] = atoi(p);
	while (*p && *p != ',')
            p++;
        if (*p) p++;                   /* eat comma */
    }
    state->complete = false;
    state->impossible = false;
    state->score = 0;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    *ret = *state; /* structure copy, except... */

    ret->tiles = snewn(state->n, int);
    memcpy(ret->tiles, state->tiles, state->n * sizeof(int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state);
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char *ret, *p;
    int x, y, maxlen;

    maxlen = state->params.h * (state->params.w + 1);
    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->params.h; y++) {
	for (x = 0; x < state->params.w; x++) {
	    int t = TILE(state,x,y);
	    if (t <= 0)      *p++ = ' ';
	    else if (t < 10) *p++ = '0'+t;
	    else             *p++ = 'a'+(t-10);
	}
	*p++ = '\n';
    }
    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

struct game_ui {
    struct game_params params;
    int *tiles; /* selected-ness only */
    int nselected;
    int xsel, ysel;
    bool displaysel;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->params = state->params; /* structure copy */
    ui->tiles = snewn(state->n, int);
    memset(ui->tiles, 0, state->n*sizeof(int));
    ui->nselected = 0;

    ui->xsel = ui->ysel = 0;
    ui->displaysel = getenv_bool("PUZZLES_SHOW_CURSOR", false);

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui->tiles);
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void sel_clear(game_ui *ui, const game_state *state)
{
    int i;

    for (i = 0; i < state->n; i++)
	ui->tiles[i] &= ~TILE_SELECTED;
    ui->nselected = 0;
}


static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    sel_clear(ui, newstate);

    /*
     * If the game state has just changed into an unplayable one
     * (either completed or impossible), we vanish the keyboard-
     * control cursor.
     */
    if (newstate->complete || newstate->impossible)
	ui->displaysel = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button)) {
        int x = ui->xsel, y = ui->ysel, c = COL(state,x,y);
        if (c == 0) return "";
        if (ISSEL(ui, x, y))
            return button == CURSOR_SELECT2 ? "Unselect" : "Remove";
        if ((x > 0 && COL(state,x-1,y) == c) ||
            (x+1 < state->params.w && COL(state,x+1,y) == c) ||
            (y > 0 && COL(state,x,y-1) == c) ||
            (y+1 < state->params.h && COL(state,x,y+1) == c))
            return "Select";
        /* Cursor is over a lone square, so we can't select it. */
        if (ui->nselected) return "Unselect";
    }
    return "";
}

static char *sel_movedesc(game_ui *ui, const game_state *state)
{
    int i;
    char *ret, buf[80];
    const char *sep;
    int retlen, retsize;

    retsize = 256;
    ret = snewn(retsize, char);
    retlen = 0;
    ret[retlen++] = 'M';
    sep = "";

    for (i = 0; i < state->n; i++) {
	if (ui->tiles[i] & TILE_SELECTED) {
	    sprintf(buf, "%s%d", sep, i);
	    sep = ",";
	    if (retlen + (int)strlen(buf) >= retsize) {
		retsize = retlen + strlen(buf) + 256;
		ret = sresize(ret, retsize, char);
	    }
	    strcpy(ret + retlen, buf);
	    retlen += strlen(buf);

	    ui->tiles[i] &= ~TILE_SELECTED;
	}
    }
    ui->nselected = 0;

    assert(retlen < retsize);
    ret[retlen++] = '\0';
    return sresize(ret, retlen, char);
}

static void sel_expand(game_ui *ui, const game_state *state, int tx, int ty)
{
    int ns = 1, nadded, x, y, c;

    TILE(ui,tx,ty) |= TILE_SELECTED;
    do {
	nadded = 0;

	for (x = 0; x < state->params.w; x++) {
	    for (y = 0; y < state->params.h; y++) {
		if (x == tx && y == ty) continue;
		if (ISSEL(ui,x,y)) continue;

		c = COL(state,x,y);
		if ((x > 0) &&
		    ISSEL(ui,x-1,y) && COL(state,x-1,y) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((x+1 < state->params.w) &&
		    ISSEL(ui,x+1,y) && COL(state,x+1,y) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((y > 0) &&
		    ISSEL(ui,x,y-1) && COL(state,x,y-1) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}

		if ((y+1 < state->params.h) &&
		    ISSEL(ui,x,y+1) && COL(state,x,y+1) == c) {
		    TILE(ui,x,y) |= TILE_SELECTED;
		    nadded++;
		    continue;
		}
	    }
	}
	ns += nadded;
    } while (nadded > 0);

    if (ns > 1) {
	ui->nselected = ns;
    } else {
	sel_clear(ui, state);
    }
}

static bool sg_emptycol(game_state *ret, int x)
{
    int y;
    for (y = 0; y < ret->params.h; y++) {
	if (COL(ret,x,y)) return false;
    }
    return true;
}


static void sg_snuggle(game_state *ret)
{
    int x,y, ndone;

    /* make all unsupported tiles fall down. */
    do {
	ndone = 0;
	for (x = 0; x < ret->params.w; x++) {
	    for (y = ret->params.h-1; y > 0; y--) {
		if (COL(ret,x,y) != 0) continue;
		if (COL(ret,x,y-1) != 0) {
		    SWAPTILE(ret,x,y,x,y-1);
		    ndone++;
		}
	    }
	}
    } while (ndone);

    /* shuffle all columns as far left as they can go. */
    do {
	ndone = 0;
	for (x = 0; x < ret->params.w-1; x++) {
	    if (sg_emptycol(ret,x) && !sg_emptycol(ret,x+1)) {
		ndone++;
		for (y = 0; y < ret->params.h; y++) {
		    SWAPTILE(ret,x,y,x+1,y);
		}
	    }
	}
    } while (ndone);
}

static void sg_check(game_state *ret)
{
    int x,y;
    bool complete = true, impossible = true;

    for (x = 0; x < ret->params.w; x++) {
	for (y = 0; y < ret->params.h; y++) {
	    if (COL(ret,x,y) == 0)
		continue;
	    complete = false;
	    if (x+1 < ret->params.w) {
		if (COL(ret,x,y) == COL(ret,x+1,y))
		    impossible = false;
	    }
	    if (y+1 < ret->params.h) {
		if (COL(ret,x,y) == COL(ret,x,y+1))
		    impossible = false;
	    }
	}
    }
    ret->complete = complete;
    ret->impossible = impossible;
}

struct game_drawstate {
    bool started;
    int bgcolour;
    int tileinner, tilegap;
    int *tiles; /* contains colour and SELECTED. */
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int tx, ty;
    char *ret = UI_UPDATE;

    ui->displaysel = false;

    if (button == RIGHT_BUTTON || button == LEFT_BUTTON) {
	tx = FROMCOORD(x); ty= FROMCOORD(y);
    } else if (IS_CURSOR_MOVE(button)) {
	int dx = 0, dy = 0;
	ui->displaysel = true;
	dx = (button == CURSOR_LEFT) ? -1 : ((button == CURSOR_RIGHT) ? +1 : 0);
	dy = (button == CURSOR_DOWN) ? +1 : ((button == CURSOR_UP)    ? -1 : 0);
	ui->xsel = (ui->xsel + state->params.w + dx) % state->params.w;
	ui->ysel = (ui->ysel + state->params.h + dy) % state->params.h;
	return ret;
    } else if (IS_CURSOR_SELECT(button)) {
	ui->displaysel = true;
	tx = ui->xsel;
	ty = ui->ysel;
    } else
	return NULL;

    if (tx < 0 || tx >= state->params.w || ty < 0 || ty >= state->params.h)
	return NULL;
    if (COL(state, tx, ty) == 0) return NULL;

    if (ISSEL(ui,tx,ty)) {
	if (button == RIGHT_BUTTON || button == CURSOR_SELECT2)
	    sel_clear(ui, state);
	else
	    ret = sel_movedesc(ui, state);
    } else {
	sel_clear(ui, state); /* might be no-op */
	sel_expand(ui, state, tx, ty);
    }

    return ret;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int i, n;
    game_state *ret;

    if (move[0] == 'M') {
	ret = dup_game(from);

	n = 0;
	move++;

	while (*move) {
            if (!isdigit((unsigned char)*move)) {
                free_game(ret);
                return NULL;
            }
	    i = atoi(move);
	    if (i < 0 || i >= ret->n) {
		free_game(ret);
		return NULL;
	    }
	    n++;
	    ret->tiles[i] = 0;

	    while (*move && isdigit((unsigned char)*move)) move++;
	    if (*move == ',') move++;
	}

	ret->score += npoints(&ret->params, n);

	sg_snuggle(ret); /* shifts blanks down and to the left */
	sg_check(ret);   /* checks for completeness or impossibility */

	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilegap = 2;
    ds->tileinner = tilesize - ds->tilegap;
}

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up tile size variables for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(NULL, ds, params, tilesize);

    *x = TILE_SIZE * params->w + 2 * BORDER - TILE_GAP;
    *y = TILE_SIZE * params->h + 2 * BORDER - TILE_GAP;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_1 * 3 + 0] = 0.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 1.0F;

    ret[COL_2 * 3 + 0] = 0.0F;
    ret[COL_2 * 3 + 1] = 0.5F;
    ret[COL_2 * 3 + 2] = 0.0F;

    ret[COL_3 * 3 + 0] = 1.0F;
    ret[COL_3 * 3 + 1] = 0.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    ret[COL_4 * 3 + 0] = 1.0F;
    ret[COL_4 * 3 + 1] = 1.0F;
    ret[COL_4 * 3 + 2] = 0.0F;

    ret[COL_5 * 3 + 0] = 1.0F;
    ret[COL_5 * 3 + 1] = 0.0F;
    ret[COL_5 * 3 + 2] = 1.0F;

    ret[COL_6 * 3 + 0] = 0.0F;
    ret[COL_6 * 3 + 1] = 1.0F;
    ret[COL_6 * 3 + 2] = 1.0F;

    ret[COL_7 * 3 + 0] = 0.5F;
    ret[COL_7 * 3 + 1] = 0.5F;
    ret[COL_7 * 3 + 2] = 1.0F;

    ret[COL_8 * 3 + 0] = 0.5F;
    ret[COL_8 * 3 + 1] = 1.0F;
    ret[COL_8 * 3 + 2] = 0.5F;

    ret[COL_9 * 3 + 0] = 1.0F;
    ret[COL_9 * 3 + 1] = 0.5F;
    ret[COL_9 * 3 + 2] = 0.5F;

    ret[COL_IMPOSSIBLE * 3 + 0] = 0.0F;
    ret[COL_IMPOSSIBLE * 3 + 1] = 0.0F;
    ret[COL_IMPOSSIBLE * 3 + 2] = 0.0F;

    ret[COL_SEL * 3 + 0] = 1.0F;
    ret[COL_SEL * 3 + 1] = 1.0F;
    ret[COL_SEL * 3 + 2] = 1.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_LOWLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0F / 3.0F;
    ret[COL_LOWLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0F / 3.0F;
    ret[COL_LOWLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0F / 3.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = false;
    ds->tileinner = ds->tilegap = 0;   /* not decided yet */
    ds->tiles = snewn(state->n, int);
    ds->bgcolour = -1;
    for (i = 0; i < state->n; i++)
	ds->tiles[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

/* Drawing routing for the tile at (x,y) is responsible for drawing
 * itself and the gaps to its right and below. If we're the same colour
 * as the tile to our right, then we fill in the gap; ditto below, and if
 * both then we fill the teeny tiny square in the corner as well.
 */

static void tile_redraw(drawing *dr, game_drawstate *ds,
			int x, int y, bool dright, bool dbelow,
                        int tile, int bgcolour)
{
    int outer = bgcolour, inner = outer, col = tile & TILE_COLMASK;

    if (col) {
	if (tile & TILE_IMPOSSIBLE) {
	    outer = col;
	    inner = COL_IMPOSSIBLE;
	} else if (tile & TILE_SELECTED) {
	    outer = COL_SEL;
	    inner = col;
	} else {
	    outer = inner = col;
	}
    }
    draw_rect(dr, COORD(x), COORD(y), TILE_INNER, TILE_INNER, outer);
    draw_rect(dr, COORD(x)+TILE_INNER/4, COORD(y)+TILE_INNER/4,
	      TILE_INNER/2, TILE_INNER/2, inner);

    if (dright)
	draw_rect(dr, COORD(x)+TILE_INNER, COORD(y), TILE_GAP, TILE_INNER,
		  (tile & TILE_JOINRIGHT) ? outer : bgcolour);
    if (dbelow)
	draw_rect(dr, COORD(x), COORD(y)+TILE_INNER, TILE_INNER, TILE_GAP,
		  (tile & TILE_JOINDOWN) ? outer : bgcolour);
    if (dright && dbelow)
	draw_rect(dr, COORD(x)+TILE_INNER, COORD(y)+TILE_INNER, TILE_GAP, TILE_GAP,
		  (tile & TILE_JOINDIAG) ? outer : bgcolour);

    if (tile & TILE_HASSEL) {
	int sx = COORD(x)+2, sy = COORD(y)+2, ssz = TILE_INNER-5;
	int scol = (outer == COL_SEL) ? COL_LOWLIGHT : COL_HIGHLIGHT;
	draw_line(dr, sx,     sy,     sx+ssz, sy,     scol);
	draw_line(dr, sx+ssz, sy,     sx+ssz, sy+ssz, scol);
	draw_line(dr, sx+ssz, sy+ssz, sx,     sy+ssz, scol);
	draw_line(dr, sx,     sy+ssz, sx,     sy,     scol);
    }

    draw_update(dr, COORD(x), COORD(y), TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int bgcolour, x, y;

    /* This was entirely cloned from fifteen.c; it should probably be
     * moved into some generic 'draw-recessed-rectangle' utility fn. */
    if (!ds->started) {
	int coords[10];

	/*
	 * Recessed area containing the whole puzzle.
	 */
	coords[0] = COORD(state->params.w) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[1] = COORD(state->params.h) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[2] = COORD(state->params.w) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[4] = coords[2] - TILE_SIZE;
	coords[5] = coords[3] + TILE_SIZE;
	coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[9] = COORD(state->params.h) + HIGHLIGHT_WIDTH - 1 - TILE_GAP;
	coords[6] = coords[8] + TILE_SIZE;
	coords[7] = coords[9] - TILE_SIZE;
	draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

	coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
	draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);

	ds->started = true;
    }

    if (flashtime > 0.0F) {
	int frame = (int)(flashtime / FLASH_FRAME);
	bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
	bgcolour = COL_BACKGROUND;

    for (x = 0; x < state->params.w; x++) {
	for (y = 0; y < state->params.h; y++) {
	    int i = (state->params.w * y) + x;
	    int col = COL(state,x,y), tile = col;
	    bool dright = (x+1 < state->params.w);
	    bool dbelow = (y+1 < state->params.h);

	    tile |= ISSEL(ui,x,y);
	    if (state->impossible)
		tile |= TILE_IMPOSSIBLE;
	    if (dright && COL(state,x+1,y) == col)
		tile |= TILE_JOINRIGHT;
	    if (dbelow && COL(state,x,y+1) == col)
		tile |= TILE_JOINDOWN;
	    if ((tile & TILE_JOINRIGHT) && (tile & TILE_JOINDOWN) &&
		COL(state,x+1,y+1) == col)
		tile |= TILE_JOINDIAG;

	    if (ui->displaysel && ui->xsel == x && ui->ysel == y)
		tile |= TILE_HASSEL;

	    /* For now we're never expecting oldstate at all (because we have
	     * no animation); when we do we might well want to be looking
	     * at the tile colours from oldstate, not state. */
	    if ((oldstate && COL(oldstate,x,y) != col) ||
		(ds->bgcolour != bgcolour) ||
		(tile != ds->tiles[i])) {
		tile_redraw(dr, ds, x, y, dright, dbelow, tile, bgcolour);
		ds->tiles[i] = tile;
	    }
	}
    }
    ds->bgcolour = bgcolour;

    {
	char status[255], score[80];

	sprintf(score, "Score: %d", state->score);

	if (state->complete)
	    sprintf(status, "COMPLETE! %s", score);
	else if (state->impossible)
	    sprintf(status, "Cannot move! %s", score);
	else if (ui->nselected)
	    sprintf(status, "%s  Selected: %d (%d)",
		    score, ui->nselected, npoints(&state->params, ui->nselected));
	else
	    sprintf(status, "%s", score);
	status_bar(dr, status);
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
    if ((!oldstate->complete && newstate->complete) ||
        (!oldstate->impossible && newstate->impossible))
	return 2 * FLASH_FRAME;
    else
	return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->displaysel) {
        *x = COORD(ui->xsel);
        *y = COORD(ui->ysel);
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    /*
     * Dead-end situations are assumed to be rescuable by Undo, so we
     * don't bother to identify them and return -1.
     */
    return state->complete ? +1 : 0;
}

#ifdef COMBINED
#define thegame samegame
#endif

const struct game thegame = {
    "Same Game", "games.samegame", "samegame",
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
    false, NULL, /* solve */
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
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};
