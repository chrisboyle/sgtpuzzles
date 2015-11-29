/*
 * Implementation of 'Train Tracks', a puzzle from the Times on Saturday.
 *
 * "Lay tracks to enable the train to travel from village A to village B.
 * The numbers indicate how many sections of rail go in each row and
 * column. There are only straight rails and curved rails. The track
 * cannot cross itself."
 *
 * Puzzles:
 * #9     8x8:d9s5c6zgAa,1,4,1,4,4,3,S3,5,2,2,4,S5,3,3,5,1
 * #112   8x8:w6x5mAa,1,3,1,4,6,4,S4,3,3,4,5,2,4,2,S5,1
 * #113   8x8:gCx5xAf,1,S4,2,5,4,6,2,3,4,2,5,2,S4,4,5,1
 * #114   8x8:p5fAzkAb,1,6,3,3,3,S6,2,3,5,4,S3,3,5,1,5,1
 * #115   8x8:zi9d5tAb,1,3,4,5,3,S4,2,4,2,6,2,3,6,S3,3,1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/* --- Game parameters --- */

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(TRICKY,Tricky,t)

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const tracks_diffnames[] = { DIFFLIST(TITLE) };
static char const tracks_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    int w, h, diff, single_ones;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 8;
    ret->diff = DIFF_TRICKY;
    ret->single_ones = TRUE;

    return ret;
}

static const struct game_params tracks_presets[] = {
    {8, 8, DIFF_EASY, 1},
    {8, 8, DIFF_TRICKY, 1},
    {10, 8, DIFF_EASY, 1},
    {10, 8, DIFF_TRICKY, 1 },
    {10, 10, DIFF_EASY, 1},
    {10, 10, DIFF_TRICKY, 1},
    {15, 10, DIFF_EASY, 1},
    {15, 10, DIFF_TRICKY, 1},
    {15, 15, DIFF_EASY, 1},
    {15, 15, DIFF_TRICKY, 1},
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(tracks_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = tracks_presets[i];

    sprintf(str, "%dx%d %s", ret->w, ret->h, tracks_diffnames[ret->diff]);

    *name = dupstr(str);
    *params = ret;
    return TRUE;
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
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
        int i;
        string++;
        params->diff = DIFF_TRICKY;
        for (i = 0; i < DIFFCOUNT; i++)
            if (*string == tracks_diffchars[i])
                params->diff = i;
        if (*string) string++;
    }
    params->single_ones = TRUE;
    if (*string == 'o') {
        params->single_ones = FALSE;
        string++;
    }

}

static char *encode_params(const game_params *params, int full)
{
    char buf[120];

    sprintf(buf, "%dx%d", params->w, params->h);
    if (full)
        sprintf(buf + strlen(buf), "d%c%s",
                tracks_diffchars[params->diff],
                params->single_ones ? "" : "o");
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

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

    ret[3].name = "Disallow consecutive 1 clues";
    ret[3].type = C_BOOLEAN;
    ret[3].ival = params->single_ones;

    ret[4].name = NULL;
    ret[4].type = C_END;
    ret[4].sval = NULL;
    ret[4].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->diff = cfg[2].ival;
    ret->single_ones = cfg[3].ival;

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    /*
     * Generating anything under 4x4 runs into trouble of one kind
     * or another.
     */
    if (params->w < 4 || params->h < 4)
        return "Width and height must both be at least four";
    return NULL;
}

/* --- Game state --- */

/* flag usage copied from pearl */

#define R 1
#define U 2
#define L 4
#define D 8

#define MOVECHAR(m) ((m==R)?'R':(m==U)?'U':(m==L)?'L':(m==D)?'D':'?')

#define DX(d) ( ((d)==R) - ((d)==L) )
#define DY(d) ( ((d)==D) - ((d)==U) )

#define F(d) (((d << 2) | (d >> 2)) & 0xF)
#define C(d) (((d << 3) | (d >> 1)) & 0xF)
#define A(d) (((d << 1) | (d >> 3)) & 0xF)

#define LR (L | R)
#define RL (R | L)
#define UD (U | D)
#define DU (D | U)
#define LU (L | U)
#define UL (U | L)
#define LD (L | D)
#define DL (D | L)
#define RU (R | U)
#define UR (U | R)
#define RD (R | D)
#define DR (D | R)
#define ALLDIR 15
#define BLANK 0
#define UNKNOWN 15

int nbits[] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

/* square grid flags */
#define S_TRACK 1     /* a track passes through this square (--> 2 edges) */
#define S_NOTRACK 2   /* no track passes through this square */
#define S_ERROR 4
#define S_CLUE 8
#define S_MARK 16

#define S_TRACK_SHIFT   16 /* U/D/L/R flags for edge track indicators */
#define S_NOTRACK_SHIFT 20 /* U/D/L/R flags for edge no-track indicators */

/* edge grid flags */
#define E_TRACK 1     /* a track passes through this edge */
#define E_NOTRACK 2   /* no track passes through this edge */

struct numbers {
    int refcount;
    int *numbers;     /* sz w+h */
    int row_s, col_s; /* stations: TODO think about multiple lines
                         (for bigger grids)? */
};

#define INGRID(state, gx, gy) ((gx) >= 0 && (gx) < (state)->p.w && \
                               (gy) >= 0 && (gy) < (state)->p.h)

struct game_state {
    game_params p;
    unsigned int *sflags;       /* size w*h */
    struct numbers *numbers;
    int *num_errors;            /* size w+h */
    int completed, used_solve, impossible;
};

/* Return the four directions in which a particular edge flag is set, around a square. */
int S_E_DIRS(const game_state *state, int sx, int sy, unsigned int eflag) {
    return (state->sflags[sy*state->p.w+sx] >>
            ((eflag == E_TRACK) ? S_TRACK_SHIFT : S_NOTRACK_SHIFT)) & ALLDIR;
}

/* Count the number of a particular edge flag around a grid square. */
int S_E_COUNT(const game_state *state, int sx, int sy, unsigned int eflag) {
    return nbits[S_E_DIRS(state, sx, sy, eflag)];
}

/* Return the two flags (E_TRACK and/or E_NOTRACK) set on a specific
 * edge of a square. */
unsigned S_E_FLAGS(const game_state *state, int sx, int sy, int d) {
    unsigned f = state->sflags[sy*state->p.w+sx];
    int t = (f & (d << S_TRACK_SHIFT)), nt = (f & (d << S_NOTRACK_SHIFT));
    return (t ? E_TRACK : 0) | (nt ? E_NOTRACK : 0);
}

int S_E_ADJ(const game_state *state, int sx, int sy, int d, int *ax, int *ay, unsigned int *ad) {
    if (d == L && sx > 0)            { *ax = sx-1; *ay = sy;   *ad = R; return 1; }
    if (d == R && sx < state->p.w-1) { *ax = sx+1; *ay = sy;   *ad = L; return 1; }
    if (d == U && sy > 0)            { *ax = sx;   *ay = sy-1; *ad = D; return 1; }
    if (d == D && sy < state->p.h-1) { *ax = sx;   *ay = sy+1; *ad = U; return 1; }

    return 0;
}

/* Sets flag (E_TRACK or E_NOTRACK) on a given edge of a square. */
void S_E_SET(game_state *state, int sx, int sy, int d, unsigned int eflag) {
    unsigned shift = (eflag == E_TRACK) ? S_TRACK_SHIFT : S_NOTRACK_SHIFT, ad;
    int ax, ay;

    state->sflags[sy*state->p.w+sx] |= (d << shift);

    if (S_E_ADJ(state, sx, sy, d, &ax, &ay, &ad)) {
        state->sflags[ay*state->p.w+ax] |= (ad << shift);
    }
}

/* Clears flag (E_TRACK or E_NOTRACK) on a given edge of a square. */
void S_E_CLEAR(game_state *state, int sx, int sy, int d, unsigned int eflag) {
    unsigned shift = (eflag == E_TRACK) ? S_TRACK_SHIFT : S_NOTRACK_SHIFT, ad;
    int ax, ay;

    state->sflags[sy*state->p.w+sx] &= ~(d << shift);

    if (S_E_ADJ(state, sx, sy, d, &ax, &ay, &ad)) {
        state->sflags[ay*state->p.w+ax] &= ~(ad << shift);
    }
}

static void clear_game(game_state *state)
{
    int w = state->p.w, h = state->p.h;

    memset(state->sflags, 0, w*h * sizeof(unsigned int));

    memset(state->numbers->numbers, 0, (w+h) * sizeof(int));
    state->numbers->col_s = state->numbers->row_s = -1;

    memset(state->num_errors, 0, (w+h) * sizeof(int));

    state->completed = state->used_solve = state->impossible = FALSE;
}

static game_state *blank_game(const game_params *params)
{
    game_state *state = snew(game_state);
    int w = params->w, h = params->h;

    state->p = *params;

    state->sflags = snewn(w*h, unsigned int);

    state->numbers = snew(struct numbers);
    state->numbers->refcount = 1;
    state->numbers->numbers = snewn(w+h, int);

    state->num_errors = snewn(w+h, int);

    clear_game(state);

    return state;
}

static void copy_game_flags(const game_state *src, game_state *dest)
{
    int w = src->p.w, h = src->p.h;

    memcpy(dest->sflags, src->sflags, w*h*sizeof(unsigned int));
}

static game_state *dup_game(const game_state *state)
{
    int w = state->p.w, h = state->p.h;
    game_state *ret = snew(game_state);

    ret->p = state->p;		       /* structure copy */

    ret->sflags = snewn(w*h, unsigned int);
    copy_game_flags(state, ret);

    ret->numbers = state->numbers;
    state->numbers->refcount++;
    ret->num_errors = snewn(w+h, int);
    memcpy(ret->num_errors, state->num_errors, (w+h)*sizeof(int));

    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ret->impossible = state->impossible;

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->numbers->refcount <= 0) {
        sfree(state->numbers->numbers);
        sfree(state->numbers);
    }
    sfree(state->num_errors);
    sfree(state->sflags);
    sfree(state);
}

#define NDIRS 4
const unsigned int dirs_const[] = { U, D, L, R };

static unsigned int find_direction(game_state *state, random_state *rs,
                                   int x, int y)
{
    int i, nx, ny, w=state->p.w, h=state->p.h;
    unsigned int dirs[NDIRS];

    memcpy(dirs, dirs_const, sizeof(dirs));
    shuffle(dirs, NDIRS, sizeof(*dirs), rs);
    for (i = 0; i < NDIRS; i++) {
        nx = x + DX(dirs[i]);
        ny = y + DY(dirs[i]);
        if (nx >= 0 && nx < w && ny == h) {
            /* off the bottom of the board: we've finished the path. */
            return dirs[i];
        } else if (!INGRID(state, nx, ny)) {
            /* off the board: can't move here */
            continue;
        } else if (S_E_COUNT(state, nx, ny, E_TRACK) > 0) {
            /* already tracks here: can't move */
            continue;
        }
        return dirs[i];
    }
    return 0; /* no possible directions left. */
}

static int check_completion(game_state *state, int mark);

static void lay_path(game_state *state, random_state *rs)
{
    int px, py, w=state->p.w, h=state->p.h;
    unsigned int d;

start:
    clear_game(state);

    /* pick a random entry point, lay its left edge */
    state->numbers->row_s = py = random_upto(rs, h);
    px = 0;
    S_E_SET(state, px, py, L, E_TRACK);

    while (INGRID(state, px, py)) {
        d = find_direction(state, rs, px, py);
        if (d == 0)
            goto start; /* nowhere else to go, restart */

        S_E_SET(state, px, py, d, E_TRACK);
        px += DX(d);
        py += DY(d);
    }
    /* double-check we got to the right place */
    assert(px >= 0 && px < w && py == h);

    state->numbers->col_s = px;
}

static int tracks_solve(game_state *state, int diff);
static void debug_state(game_state *state, const char *what);

/* Clue-setting algorithm:

 - first lay clues randomly until it's soluble
 - then remove clues randomly if removing them doesn't affect solubility

 - We start with two clues, one at each path entrance.

 More details:
 - start with an array of all square i positions
 - if the grid is already soluble by a level easier than we've requested,
    go back and make a new grid
 - if the grid is already soluble by our requested difficulty level, skip
    the clue-laying step
 - count the number of flags the solver managed to place, remember this.

 - to lay clues:
   - shuffle the i positions
   - for each possible clue position:
     - copy the solved board, strip it
     - take the next position, add a clue there on the copy
     - try and solve the copy
     - if it's soluble by a level easier than we've requested, continue (on
        to next clue position: putting a clue here makes it too easy)
     - if it's soluble by our difficulty level, we're done:
       - put the clue flag into the solved board
       - go to strip-clues.
     - if the solver didn't manage to place any more flags, continue (on to next
        clue position: putting a clue here didn't help he solver)
     - otherwise put the clue flag in the original board, and go on to the next
        clue position
   - if we get here and we've not solved it yet, we never will (did we really
      fill _all_ the clues in?!). Go back and make a new grid.

 - to strip clues:
   - shuffle the i positions
   - for each possible clue position:
     - if the solved grid doesn't have a clue here, skip
     - copy the solved board, remove this clue, strip it
     - try and solve the copy
     - assert that it is not soluble by a level easier than we've requested
       - (because this should never happen)
     - if this is (still) soluble by our difficulty level:
       - remove this clue from the solved board, it's redundant (with the other
          clues)

  - that should be it.
*/

static game_state *copy_and_strip(const game_state *state, game_state *ret, int flipcluei)
{
    int i, j, w = state->p.w, h = state->p.h;

    copy_game_flags(state, ret);

    /* Add/remove a clue before stripping, if required */

    if (flipcluei != -1)
        ret->sflags[flipcluei] ^= S_CLUE;

    /* All squares that are not clue squares have square track info erased, and some edge flags.. */

    for (i = 0; i < w*h; i++) {
        if (!(ret->sflags[i] & S_CLUE)) {
            ret->sflags[i] &= ~(S_TRACK|S_NOTRACK|S_ERROR|S_MARK);
            for (j = 0; j < 4; j++) {
                unsigned f = 1<<j;
                int xx = i%w + DX(f), yy = i/w + DY(f);
                if (!INGRID(state, xx, yy) || !(ret->sflags[yy*w+xx] & S_CLUE)) {
                    /* only erase an edge flag if neither side of the edge is S_CLUE. */
                    S_E_CLEAR(ret, i%w, i/w, f, E_TRACK);
                    S_E_CLEAR(ret, i%w, i/w, f, E_NOTRACK);
                }
            }
        }
    }
    return ret;
}

static int solve_progress(const game_state *state) {
    int i, w = state->p.w, h = state->p.h, progress = 0;

    /* Work out how many flags the solver managed to set (either TRACK
       or NOTRACK) and return this as a progress measure, to check whether
       a partially-solved board gets any further than a previous partially-
       solved board. */

    for (i = 0; i < w*h; i++) {
        if (state->sflags[i] & S_TRACK) progress++;
        if (state->sflags[i] & S_NOTRACK) progress++;
        progress += S_E_COUNT(state, i%w, i/w, E_TRACK);
        progress += S_E_COUNT(state, i%w, i/w, E_NOTRACK);
    }
    return progress;
}

static int check_phantom_moves(const game_state *state) {
    int x, y, i;

    /* Check that this state won't show 'phantom moves' at the start of the
     * game: squares which have multiple edge flags set but no clue flag
     * cause a piece of track to appear that isn't on a clue square. */

    for (x = 0; x < state->p.w; x++) {
        for (y = 0; y < state->p.h; y++) {
            i = y*state->p.w+x;
            if (state->sflags[i] & S_CLUE)
                continue;
            if (S_E_COUNT(state, x, y, E_TRACK) > 1)
                return 1; /* found one! */
        }
    }
    return 0;
}

static int add_clues(game_state *state, random_state *rs, int diff)
{
    int i, j, pi, w = state->p.w, h = state->p.h, progress, ret = 0, sr;
    int *positions = snewn(w*h, int), npositions = 0;
    int *nedges_previous_solve = snewn(w*h, int);
    game_state *scratch = dup_game(state);

    debug_state(state, "gen: Initial board");

    debug(("gen: Adding clues..."));

    /* set up the shuffly-position grid for later, used for adding clues:
     * we only bother adding clues where any edges are set. */
    for (i = 0; i < w*h; i++) {
        if (S_E_DIRS(state, i%w, i/w, E_TRACK) != 0) {
            positions[npositions++] = i;
        }
        nedges_previous_solve[i] = 0;
    }

    /* First, check whether the puzzle is already either too easy, or just right */
    scratch = copy_and_strip(state, scratch, -1);
    if (diff > 0) {
        sr = tracks_solve(scratch, diff-1);
        if (sr < 0)
            assert(!"Generator should not have created impossible puzzle");
        if (sr > 0) {
            ret = -1; /* already too easy, even without adding clues. */
            debug(("gen:  ...already too easy, need new board."));
            goto done;
        }
    }
    sr = tracks_solve(scratch, diff);
    if (sr < 0)
        assert(!"Generator should not have created impossible puzzle");
    if (sr > 0) {
        ret = 1; /* already soluble without any extra clues. */
        debug(("gen:  ...soluble without clues, nothing to do."));
        goto done;
    }
    debug_state(scratch, "gen: Initial part-solved state: ");
    progress = solve_progress(scratch);
    debug(("gen: Initial solve progress is %d", progress));

    /* First, lay clues until we're soluble. */
    shuffle(positions, npositions, sizeof(int), rs);
    for (pi = 0; pi < npositions; pi++) {
        i = positions[pi]; /* pick a random position */
        if (state->sflags[i] & S_CLUE)
            continue; /* already a clue here (entrance location?) */
        if (nedges_previous_solve[i] == 2)
            continue; /* no point putting a clue here, we could solve both edges
                         with the previous set of clues */

        /* set a clue in that position (on a copy of the board) and test solubility */
        scratch = copy_and_strip(state, scratch, i);

        if (check_phantom_moves(scratch))
            continue; /* adding a clue here would add phantom track */

        if (diff > 0) {
            if (tracks_solve(scratch, diff-1) > 0) {
                continue; /* adding a clue here makes it too easy */
            }
        }
        if (tracks_solve(scratch, diff) > 0) {
            /* we're now soluble (and we weren't before): add this clue, and then
               start stripping clues */
            debug(("gen:  ...adding clue at (%d,%d), now soluble", i%w, i/w));
            state->sflags[i] |= S_CLUE;
            goto strip_clues;
        }
        if (solve_progress(scratch) > progress) {
            /* We've made more progress solving: add this clue, then. */
            progress = solve_progress(scratch);
            debug(("gen:  ... adding clue at (%d,%d), new progress %d", i%w, i/w, progress));
            state->sflags[i] |= S_CLUE;

            for (j = 0; j < w*h; j++)
                nedges_previous_solve[j] = S_E_COUNT(scratch, j%w, j/w, E_TRACK);
        }
    }
    /* If we got here we didn't ever manage to make the puzzle soluble
       (without making it too easily soluble, that is): give up. */

    debug(("gen: Unable to make soluble with clues, need new board."));
    ret = -1;
    goto done;

strip_clues:
    debug(("gen: Stripping clues."));

    /* Now, strip redundant clues (i.e. those without which the puzzle is still
       soluble) */
    shuffle(positions, npositions, sizeof(int), rs);
    for (pi = 0; pi < npositions; pi++) {
        i = positions[pi]; /* pick a random position */
        if (!(state->sflags[i] & S_CLUE))
            continue; /* no clue here to strip */
        if ((i%w == 0 && i/w == state->numbers->row_s) ||
                (i/w == (h-1) && i%w == state->numbers->col_s))
            continue; /* don't strip clues at entrance/exit */

        scratch = copy_and_strip(state, scratch, i);
        if (check_phantom_moves(scratch))
            continue; /* removing a clue here would add phantom track */

        if (tracks_solve(scratch, diff) > 0) {
            debug(("gen:  ... removing clue at (%d,%d), still soluble without it", i%w, i/w));
            state->sflags[i] &= ~S_CLUE; /* still soluble without this clue. */
        }
    }
    debug(("gen: Finished stripping clues."));
    ret = 1;

done:
    sfree(positions);
    free_game(scratch);
    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    int i, j, w = params->w, h = params->h, x, y, ret;
    game_state *state;
    char *desc, *p;
    game_params adjusted_params;

    /*
     * 4x4 Tricky cannot be generated, so fall back to Easy.
     */
    if (w == 4 && h == 4 && params->diff > DIFF_EASY) {
        adjusted_params = *params;     /* structure copy */
        adjusted_params.diff = DIFF_EASY;
        params = &adjusted_params;
    }

    state = blank_game(params);

    /* --- lay the random path */

newpath:
    lay_path(state, rs);
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            if (S_E_COUNT(state, x, y, E_TRACK) > 0) {
                state->sflags[y*w + x] |= S_TRACK;
            }
            if ((x == 0 && y == state->numbers->row_s) ||
                    (y == (h-1) && x == state->numbers->col_s)) {
                state->sflags[y*w + x] |= S_CLUE;
            }
        }
    }

    /* --- Update the clue numbers based on the tracks we have generated. */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            if (state->sflags[y*w + x] & S_TRACK) {
                state->numbers->numbers[x]++;
                state->numbers->numbers[y+w]++;
            }
        }
    }
    for (i = 0; i < w+h; i++) {
        if (state->numbers->numbers[i] == 0)
            goto newpath; /* too boring */
    }

    if (params->single_ones) {
        int last_was_one = 1, is_one; /* (disallow 1 clue at entry point) */
        for (i = 0; i < w+h; i++) {
            is_one = (state->numbers->numbers[i] == 1);
            if (is_one && last_was_one)
                goto newpath; /* disallow consecutive 1 clues. */
            last_was_one = is_one;
        }
        if (state->numbers->numbers[w+h-1] == 1)
            goto newpath; /* (disallow 1 clue at exit point) */
    }

    /* --- Add clues to make a soluble puzzle */
    ret = add_clues(state, rs, params->diff);
    if (ret != 1) goto newpath; /* couldn't make it soluble, or too easy */

    /* --- Generate the game desc based on the generated grid. */
    desc = snewn(w*h*3 + (w+h)*5, char);
    for (i = j = 0; i < w*h; i++) {
        if (!(state->sflags[i] & S_CLUE) && j > 0 &&
                desc[j-1] >= 'a' && desc[j-1] < 'z')
            desc[j-1]++;
        else if (!(state->sflags[i] & S_CLUE))
            desc[j++] = 'a';
        else {
            unsigned int f = S_E_DIRS(state, i%w, i/w, E_TRACK);
            desc[j++] = (f < 10) ? ('0' + f) : ('A' + (f-10));
        }
    }

    p = desc + j;
    for (x = 0; x < w; x++) {
        p += sprintf(p, ",%s%d", x == state->numbers->col_s ? "S" : "",
                     state->numbers->numbers[x]);
    }
    for (y = 0; y < h; y++) {
        p += sprintf(p, ",%s%d", y == state->numbers->row_s ? "S" : "",
                     state->numbers->numbers[y+w]);
    }
    *p++ = '\0';

    ret = tracks_solve(state, DIFFCOUNT);
    assert(ret >= 0);
    free_game(state);

    debug(("new_game_desc: %s", desc));
    return desc;
}

static char *validate_desc(const game_params *params, const char *desc)
{
    int i = 0, w = params->w, h = params->h, in = 0, out = 0;

    while (*desc) {
        unsigned int f = 0;
        if (*desc >= '0' && *desc <= '9')
            f = (*desc - '0');
        else if (*desc >= 'A' && *desc <= 'F')
            f = (*desc - 'A' + 10);
        else if (*desc >= 'a' && *desc <= 'z')
            i += *desc - 'a';
        else
            return "Game description contained unexpected characters";

        if (f != 0) {
            if (nbits[f] != 2)
                return "Clue did not provide 2 direction flags";
        }
        i++;
        desc++;
        if (i == w*h) break;
    }
    for (i = 0; i < w+h; i++) {
        if (!*desc)
            return "Not enough numbers given after grid specification";
        else if (*desc != ',')
            return "Invalid character in number list";
        desc++;
        if (*desc == 'S') {
            if (i < w)
                out++;
            else
                in++;
            desc++;
        }
        while (*desc && isdigit((unsigned char)*desc)) desc++;
    }
    if (in != 1 || out != 1)
        return "Puzzle must have one entrance and one exit";
    if (*desc)
        return "Unexpected additional character at end of game description";
    return NULL;
}

#ifdef ANDROID
static void android_request_keys(const game_params *params)
{
    android_keys("", ANDROID_ARROWS_LEFT_RIGHT);
}
#endif

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
    game_state *state = blank_game(params);
    int w = params->w, h = params->h, i = 0;

    while (*desc) {
        unsigned int f = 0;
        if (*desc >= '0' && *desc <= '9')
            f = (*desc - '0');
        else if (*desc >= 'A' && *desc <= 'F')
            f = (*desc - 'A' + 10);
        else if (*desc >= 'a' && *desc <= 'z')
            i += *desc - 'a';

        if (f != 0) {
            int x = i % w, y = i / w;
            assert(f < 16);
            assert(nbits[f] == 2);

            state->sflags[i] |= (S_TRACK | S_CLUE);
            if (f & U) S_E_SET(state, x, y, U, E_TRACK);
            if (f & D) S_E_SET(state, x, y, D, E_TRACK);
            if (f & L) S_E_SET(state, x, y, L, E_TRACK);
            if (f & R) S_E_SET(state, x, y, R, E_TRACK);
        }
        i++;
        desc++;
        if (i == w*h) break;
    }
    for (i = 0; i < w+h; i++) {
        assert(*desc == ',');
        desc++;

        if (*desc == 'S') {
            if (i < w)
                state->numbers->col_s = i;
            else
                state->numbers->row_s = i-w;
            desc++;
        }
        state->numbers->numbers[i] = atoi(desc);
        while (*desc && isdigit((unsigned char)*desc)) desc++;
    }

    assert(!*desc);

    return state;
}

static int solve_set_sflag(game_state *state, int x, int y,
                           unsigned int f, const char *why)
{
    int w = state->p.w, i = y*w + x;

    if (state->sflags[i] & f)
        return 0;
    debug(("solve: square (%d,%d) -> %s: %s",
           x, y, (f == S_TRACK ? "TRACK" : "NOTRACK"), why));
    if (state->sflags[i] & (f == S_TRACK ? S_NOTRACK : S_TRACK)) {
        debug(("solve: opposite flag already set there, marking IMPOSSIBLE"));
        state->impossible = TRUE;
    }
    state->sflags[i] |= f;
    return 1;
}

static int solve_set_eflag(game_state *state, int x, int y, int d,
                           unsigned int f, const char *why)
{
    int sf = S_E_FLAGS(state, x, y, d);

    if (sf & f)
        return 0;
    debug(("solve: edge (%d,%d)/%c -> %s: %s", x, y,
           (d == U) ? 'U' : (d == D) ? 'D' : (d == L) ? 'L' : 'R',
           (f == S_TRACK ? "TRACK" : "NOTRACK"), why));
    if (sf & (f == E_TRACK ? E_NOTRACK : E_TRACK)) {
        debug(("solve: opposite flag already set there, marking IMPOSSIBLE"));
        state->impossible = TRUE;
    }
    S_E_SET(state, x, y, d, f);
    return 1;
}

static int solve_update_flags(game_state *state)
{
    int x, y, i, w = state->p.w, h = state->p.h, did = 0;

    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            /* If a square is NOTRACK, all four edges must be. */
            if (state->sflags[y*w + x] & S_NOTRACK) {
                for (i = 0; i < 4; i++) {
                    unsigned int d = 1<<i;
                    did += solve_set_eflag(state, x, y, d, E_NOTRACK, "edges around NOTRACK");
                }
            }

            /* If 3 or more edges around a square are NOTRACK, the square is. */
            if (S_E_COUNT(state, x, y, E_NOTRACK) >= 3) {
                did += solve_set_sflag(state, x, y, S_NOTRACK, "square has >2 NOTRACK edges");
            }

            /* If any edge around a square is TRACK, the square is. */
            if (S_E_COUNT(state, x, y, E_TRACK) > 0) {
                did += solve_set_sflag(state, x, y, S_TRACK, "square has TRACK edge");
            }

            /* If a square is TRACK and 2 edges are NOTRACK,
               the other two edges must be TRACK. */
            if ((state->sflags[y*w + x] & S_TRACK) &&
                    (S_E_COUNT(state, x, y, E_NOTRACK) == 2) &&
                    (S_E_COUNT(state, x, y, E_TRACK) < 2)) {
                for (i = 0; i < 4; i++) {
                    unsigned int d = 1<<i;
                    if (!(S_E_FLAGS(state, x, y, d) & (E_TRACK|E_NOTRACK))) {
                        did += solve_set_eflag(state, x, y, d, E_TRACK,
                                               "TRACK square/2 NOTRACK edges");
                    }
                }
            }

            /* If a square is TRACK and 2 edges are TRACK, the other two
               must be NOTRACK. */
            if ((state->sflags[y*w + x] & S_TRACK) &&
                    (S_E_COUNT(state, x, y, E_TRACK) == 2) &&
                    (S_E_COUNT(state, x, y, E_NOTRACK) < 2)) {
                for (i = 0; i < 4; i++) {
                    unsigned int d = 1<<i;
                    if (!(S_E_FLAGS(state, x, y, d) & (E_TRACK|E_NOTRACK))) {
                        did += solve_set_eflag(state, x, y, d, E_NOTRACK,
                                               "TRACK square/2 TRACK edges");
                    }
                }
            }
        }
    }
    return did;
}

static int solve_count_col(game_state *state, int col, unsigned int f)
{
    int i, n, c = 0, h = state->p.h, w = state->p.w;
    for (n = 0, i = col; n < h; n++, i += w) {
        if (state->sflags[i] & f) c++;
    }
    return c;
}

static int solve_count_row(game_state *state, int row, unsigned int f)
{
    int i, n, c = 0, w = state->p.w;
    for (n = 0, i = w*row; n < state->p.w; n++, i++) {
        if (state->sflags[i] & f) c++;
    }
    return c;
}

static int solve_count_clues_sub(game_state *state, int si, int id, int n,
                                 int target, const char *what)
{
    int ctrack = 0, cnotrack = 0, did = 0, j, i, w = state->p.w;

    for (j = 0, i = si; j < n; j++, i += id) {
        if (state->sflags[i] & S_TRACK)
            ctrack++;
        if (state->sflags[i] & S_NOTRACK)
            cnotrack++;
    }
    if (ctrack == target) {
        /* everything that's not S_TRACK must be S_NOTRACK. */
        for (j = 0, i = si; j < n; j++, i += id) {
            if (!(state->sflags[i] & S_TRACK))
                did += solve_set_sflag(state, i%w, i/w, S_NOTRACK, what);
        }
    }
    if (cnotrack == (n-target)) {
        /* everything that's not S_NOTRACK must be S_TRACK. */
        for (j = 0, i = si; j < n; j++, i += id) {
            if (!(state->sflags[i] & S_NOTRACK))
                did += solve_set_sflag(state, i%w, i/w, S_TRACK, what);
        }
    }
    return did;
}

static int solve_count_clues(game_state *state)
{
    int w = state->p.w, h = state->p.h, x, y, target, did = 0;

    for (x = 0; x < w; x++) {
        target = state->numbers->numbers[x];
        did += solve_count_clues_sub(state, x, w, h, target, "col count");
    }
    for (y = 0; y < h; y++) {
        target = state->numbers->numbers[w+y];
        did += solve_count_clues_sub(state, y*w, 1, w, target, "row count");
    }
    return did;
}

static int solve_check_single_sub(game_state *state, int si, int id, int n,
                                  int target, unsigned int perpf,
                                  const char *what)
{
    int ctrack = 0, nperp = 0, did = 0, j, i, w = state->p.w;
    int n1edge = 0, i1edge = 0, ox, oy, x, y;
    unsigned int impossible = 0;

    /* For rows or columns which only have one more square to put a track in, we
       know the only way a new track section could be there would be to run
       perpendicular to the track (otherwise we'd need at least two free squares).
       So, if there is nowhere we can run perpendicular to the track (e.g. because
       we're on an edge) we know the extra track section much be on one end of an
       existing section. */

    for (j = 0, i = si; j < n; j++, i += id) {
        if (state->sflags[i] & S_TRACK)
            ctrack++;
        impossible = S_E_DIRS(state, i%w, i/w, E_NOTRACK);
        if ((perpf & impossible) == 0)
            nperp++;
        if (S_E_COUNT(state, i%w, i/w, E_TRACK) <= 1) {
            n1edge++;
            i1edge = i;
        }
    }
    if (ctrack != (target-1)) return 0;
    if (nperp > 0 || n1edge != 1) return 0;

    debug(("check_single from (%d,%d): 1 match from (%d,%d)",
           si%w, si/w, i1edge%w, i1edge/w));

    /* We have a match: anything that's more than 1 away from this square
       cannot now contain a track. */
    ox = i1edge%w;
    oy = i1edge/w;
    for (j = 0, i = si; j < n; j++, i += id) {
        x = i%w;
        y = i/w;
        if (abs(ox-x) > 1 || abs(oy-y) > 1) {
            if (!state->sflags[i] & S_TRACK)
                did += solve_set_sflag(state, x, y, S_NOTRACK, what);
        }
    }

    return did;
}

static int solve_check_single(game_state *state)
{
    int w = state->p.w, h = state->p.h, x, y, target, did = 0;

    for (x = 0; x < w; x++) {
        target = state->numbers->numbers[x];
        did += solve_check_single_sub(state, x, w, h, target, R|L, "single on col");
    }
    for (y = 0; y < h; y++) {
        target = state->numbers->numbers[w+y];
        did += solve_check_single_sub(state, y*w, 1, w, target, U|D, "single on row");
    }
    return did;
}

static int solve_check_loose_sub(game_state *state, int si, int id, int n,
                                 int target, unsigned int perpf,
                                 const char *what)
{
    int nperp = 0, nloose = 0, e2count = 0, did = 0, i, j, k;
    int w = state->p.w;
    unsigned int parf = ALLDIR & (~perpf);

    for (j = 0, i = si; j < n; j++, i += id) {
        int fcount = S_E_COUNT(state, i%w, i/w, E_TRACK);
        if (fcount == 2)
            e2count++; /* this cell has 2 definite edges */
        state->sflags[i] &= ~S_MARK;
        if (fcount == 1 && (parf & S_E_DIRS(state, i%w, i/w, E_TRACK))) {
            nloose++; /* this cell has a loose end (single flag set parallel
                    to the direction of this row/column) */
            state->sflags[i] |= S_MARK; /* mark loose ends */
        }
        if (fcount != 2 && !(perpf & S_E_DIRS(state, i%w, i/w, E_NOTRACK)))
            nperp++; /* we could lay perpendicular across this cell */
    }

    if (nloose > (target - e2count)) {
        debug(("check %s from (%d,%d): more loose (%d) than empty (%d), IMPOSSIBLE",
               what, si%w, si/w, nloose, target-e2count));
        state->impossible = TRUE;
    }
    if (nloose > 0 && nloose == (target - e2count)) {
        debug(("check %s from (%d,%d): nloose = empty (%d), forcing loners out.",
               what, si%w, si/w, nloose));
        for (j = 0, i = si; j < n; j++, i += id) {
            if (!(state->sflags[i] & S_MARK))
                continue; /* skip non-loose ends */
            if (j > 0 && state->sflags[i-id] & S_MARK)
                continue; /* next to other loose end, could join up */
            if (j < (n-1) && state->sflags[i+id] & S_MARK)
                continue; /* ditto */

            for (k = 0; k < 4; k++) {
                if ((parf & (1<<k)) &&
                        !(S_E_DIRS(state, i%w, i/w, E_TRACK) & (1<<k))) {
                    /* set as NOTRACK the edge parallel to the row/column that's
                       not already set. */
                    did += solve_set_eflag(state, i%w, i/w, 1<<k, E_NOTRACK, what);
                }
            }
        }
    }
    if (nloose == 1 && (target - e2count) == 2 && nperp == 0) {
        debug(("check %s from (%d,%d): 1 loose end, 2 empty squares, forcing parallel",
               what, si%w, si/w));
        for (j = 0, i = si; j < n; j++, i += id) {
            if (!(state->sflags[i] & S_MARK))
                continue; /* skip non-loose ends */
            for (k = 0; k < 4; k++) {
                if (parf & (1<<k))
                    did += solve_set_eflag(state, i%w, i/w, 1<<k, E_TRACK, what);
            }
        }
    }

    return did;
}

static int solve_check_loose_ends(game_state *state)
{
    int w = state->p.w, h = state->p.h, x, y, target, did = 0;

    for (x = 0; x < w; x++) {
        target = state->numbers->numbers[x];
        did += solve_check_loose_sub(state, x, w, h, target, R|L, "loose on col");
    }
    for (y = 0; y < h; y++) {
        target = state->numbers->numbers[w+y];
        did += solve_check_loose_sub(state, y*w, 1, w, target, U|D, "loose on row");
    }
    return did;
}

static int solve_check_loop_sub(game_state *state, int x, int y, int dir,
                                int *dsf, int startc, int endc)
{
    int w = state->p.w, h = state->p.h, i = y*w+x, j, k, satisfied = 1;

    j = (y+DY(dir))*w + (x+DX(dir));

    assert(i < w*h && j < w*h);

    if ((state->sflags[i] & S_TRACK) &&
        (state->sflags[j] & S_TRACK) &&
        !(S_E_DIRS(state, x, y, E_TRACK) & dir) &&
        !(S_E_DIRS(state, x, y, E_NOTRACK) & dir)) {
        int ic = dsf_canonify(dsf, i), jc = dsf_canonify(dsf, j);
        if (ic == jc) {
            return solve_set_eflag(state, x, y, dir, E_NOTRACK, "would close loop");
        }
        if ((ic == startc && jc == endc) || (ic == endc && jc == startc)) {
            debug(("Adding link at (%d,%d) would join start to end", x, y));
            /* We mustn't join the start to the end if:
               - there are other bits of track that aren't attached to either end
               - the clues are not fully satisfied yet
             */
            for (k = 0; k < w*h; k++) {
                if (state->sflags[k] & S_TRACK &&
                        dsf_canonify(dsf, k) != startc && dsf_canonify(dsf, k) != endc) {
                    return solve_set_eflag(state, x, y, dir, E_NOTRACK,
                                           "joins start to end but misses tracks");
                }
            }
            for (k = 0; k < w; k++) {
                int target = state->numbers->numbers[k];
                int ntracks = solve_count_col(state, k, S_TRACK);
                if (ntracks < target) satisfied = 0;
            }
            for (k = 0; k < h; k++) {
                int target = state->numbers->numbers[w+k];
                int ntracks = solve_count_row(state, k, S_TRACK);
                if (ntracks < target) satisfied = 0;
            }
            if (!satisfied) {
                return solve_set_eflag(state, x, y, dir, E_NOTRACK,
                                       "joins start to end with incomplete clues");
            }
        }
    }
    return 0;
}

static int solve_check_loop(game_state *state)
{
    int w = state->p.w, h = state->p.h, x, y, i, j, did = 0;
    int *dsf, startc, endc;

    /* TODO eventually we should pull this out into a solver struct and keep it
       updated as we connect squares. For now we recreate it every time we try
       this particular solver step. */
    dsf = snewn(w*h, int);
    dsf_init(dsf, w*h);

    /* Work out the connectedness of the current loop set. */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            i = y*w + x;
            if (x < (w-1) && S_E_DIRS(state, x, y, E_TRACK) & R) {
                /* connection to the right... */
                j = y*w + (x+1);
                assert(i < w*h && j < w*h);
                dsf_merge(dsf, i, j);
            }
            if (y < (h-1) && S_E_DIRS(state, x, y, E_TRACK) & D) {
                /* connection down... */
                j = (y+1)*w + x;
                assert(i < w*h && j < w*h);
                dsf_merge(dsf, i, j);
            }
            /* NB no need to check up and left because they'll have been checked
               by the other side. */
        }
    }

    startc = dsf_canonify(dsf, state->numbers->row_s*w);
    endc = dsf_canonify(dsf, (h-1)*w+state->numbers->col_s);

    /* Now look at all adjacent squares that are both S_TRACK: if connecting
       any of them would complete a loop (i.e. they're both the same dsf class
       already) then that edge must be NOTRACK. */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            if (x < (w-1))
              did += solve_check_loop_sub(state, x, y, R, dsf, startc, endc);
            if (y < (h-1))
              did += solve_check_loop_sub(state, x, y, D, dsf, startc, endc);
        }
    }

    sfree(dsf);

    return did;
}

static void solve_discount_edge(game_state *state, int x, int y, int d)
{
    if (S_E_DIRS(state, x, y, E_TRACK) & d) {
        assert(state->sflags[y*state->p.w + x] & S_CLUE);
        return; /* (only) clue squares can have outer edges set. */
    }
    solve_set_eflag(state, x, y, d, E_NOTRACK, "outer edge");
}

static int tracks_solve(game_state *state, int diff)
{
    int didsth, x, y, w = state->p.w, h = state->p.h;

    debug(("solve..."));
    state->impossible = FALSE;

    /* Set all the outer border edges as no-track. */
    for (x = 0; x < w; x++) {
        solve_discount_edge(state, x, 0, U);
        solve_discount_edge(state, x, h-1, D);
    }
    for (y = 0; y < h; y++) {
        solve_discount_edge(state, 0, y, L);
        solve_discount_edge(state, w-1, y, R);
    }

    while (1) {
        didsth = 0;

        didsth += solve_update_flags(state);
        didsth += solve_count_clues(state);
        didsth += solve_check_loop(state);

        if (diff >= DIFF_TRICKY) {
            didsth += solve_check_single(state);
            didsth += solve_check_loose_ends(state);
        }

        if (!didsth || state->impossible) break;
    }

    return state->impossible ? -1 : check_completion(state, FALSE) ? 1 : 0;
}

static char *move_string_diff(const game_state *before, const game_state *after, int issolve)
{
    int w = after->p.w, h = after->p.h, i, j;
    char *move = snewn(w*h*40, char), *p = move;
    const char *sep = "";
    unsigned int otf, ntf, onf, nnf;

    if (issolve) {
        *p++ = 'S';
        sep = ";";
    }
    for (i = 0; i < w*h; i++) {
        otf = S_E_DIRS(before, i%w, i/w, E_TRACK);
        ntf = S_E_DIRS(after, i%w, i/w, E_TRACK);
        onf = S_E_DIRS(before, i%w, i/w, E_NOTRACK);
        nnf = S_E_DIRS(after, i%w, i/w, E_NOTRACK);

        for (j = 0; j < 4; j++) {
            unsigned df = 1<<j;
            if ((otf & df) != (ntf & df)) {
                p += sprintf(p, "%s%c%c%d,%d", sep,
                             (ntf & df) ? 'T' : 't', MOVECHAR(df), i%w, i/w);
                sep = ";";
            }
            if ((onf & df) != (nnf & df)) {
                p += sprintf(p, "%s%c%c%d,%d", sep,
                             (nnf & df) ? 'N' : 'n', MOVECHAR(df), i%w, i/w);
                sep = ";";
            }
        }

        if ((before->sflags[i] & S_NOTRACK) != (after->sflags[i] & S_NOTRACK)) {
            p += sprintf(p, "%s%cS%d,%d", sep,
                         (after->sflags[i] & S_NOTRACK) ? 'N' : 'n', i%w, i/w);
            sep = ";";
        }
        if ((before->sflags[i] & S_TRACK) != (after->sflags[i] & S_TRACK)) {
            p += sprintf(p, "%s%cS%d,%d", sep,
                         (after->sflags[i] & S_TRACK) ? 'T' : 't', i%w, i/w);
            sep = ";";
        }
    }
    *p++ = '\0';
    move = sresize(move, p - move, char);

    return move;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
    game_state *solved;
    int ret;
    char *move;

    solved = dup_game(currstate);
    ret = tracks_solve(solved, DIFFCOUNT);
    if (ret < 1) {
        free_game(solved);
        solved = dup_game(state);
        ret = tracks_solve(solved, DIFFCOUNT);
    }

    if (ret < 1) {
        *error = "Unable to find solution";
        move = NULL;
    } else {
        move = move_string_diff(currstate, solved, TRUE);
    }

    free_game(solved);
    return move;
}

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    char *ret, *p;
    int x, y, len, w = state->p.w, h = state->p.h;

    len = ((w*2) + 4) * ((h*2)+4) + 2;
    ret = snewn(len+1, char);
    p = ret;

    /* top line: column clues */
    *p++ = ' ';
    *p++ = ' ';
    for (x = 0; x < w; x++) {
        *p++ = (state->numbers->numbers[x] < 10 ?
                '0' + state->numbers->numbers[x] :
                'A' + state->numbers->numbers[x] - 10);
        *p++ = ' ';
    }
    *p++ = '\n';

    /* second line: top edge */
    *p++ = ' ';
    *p++ = '+';
    for (x = 0; x < w*2-1; x++)
        *p++ = '-';
    *p++ = '+';
    *p++ = '\n';

    /* grid rows: one line of squares, one line of edges. */
    for (y = 0; y < h; y++) {
        /* grid square line */
        *p++ = (y == state->numbers->row_s) ? 'A' : ' ';
        *p++ = (y == state->numbers->row_s) ? '-' : '|';

        for (x = 0; x < w; x++) {
            unsigned int f = S_E_DIRS(state, x, y, E_TRACK);
            if (state->sflags[y*w+x] & S_CLUE) *p++ = 'C';
            else if (f == LU || f == RD) *p++ = '/';
            else if (f == LD || f == RU) *p++ = '\\';
            else if (f == UD) *p++ = '|';
            else if (f == RL) *p++ = '-';
            else if (state->sflags[y*w+x] & S_NOTRACK) *p++ = 'x';
            else *p++ = ' ';

            if (x < w-1) {
                *p++ = (f & R) ? '-' : ' ';
            } else
                *p++ = '|';
        }
        *p++ = (state->numbers->numbers[w+y] < 10 ?
                '0' + state->numbers->numbers[w+y] :
                'A' + state->numbers->numbers[w+y] - 10);
        *p++ = '\n';

        if (y == h-1) continue;

        /* edges line */
        *p++ = ' ';
        *p++ = '|';
        for (x = 0; x < w; x++) {
            unsigned int f = S_E_DIRS(state, x, y, E_TRACK);
            *p++ = (f & D) ? '|' : ' ';
            *p++ = (x < w-1) ? ' ' : '|';
        }
        *p++ = '\n';
    }

    /* next line: bottom edge */
    *p++ = ' ';
    *p++ = '+';
    for (x = 0; x < w*2-1; x++)
        *p++ = (x == state->numbers->col_s*2) ? '|' : '-';
    *p++ = '+';
    *p++ = '\n';

    /* final line: bottom clue */
    *p++ = ' ';
    *p++ = ' ';
    for (x = 0; x < w*2-1; x++)
        *p++ = (x == state->numbers->col_s*2) ? 'B' : ' ';
    *p++ = '\n';

    *p = '\0';
    return ret;
}

static void debug_state(game_state *state, const char *what) {
    char *sstring = game_text_format(state);
    debug(("%s: %s", what, sstring));
    sfree(sstring);
}

static void dsf_update_completion(game_state *state, int *loopclass,
                                  int ax, int ay, char dir,
                                  int *dsf)
{
    int w = state->p.w, ai = ay*w+ax, bx, by, bi, ac, bc;

    if (!(S_E_DIRS(state, ax, ay, E_TRACK) & dir)) return;
    bx = ax + DX(dir);
    by = ay + DY(dir);

    if (!INGRID(state, bx, by)) return;
    bi = by*w+bx;

    ac = dsf_canonify(dsf, ai);
    bc = dsf_canonify(dsf, bi);

    if (ac == bc) {
        /* loop detected */
        *loopclass = ac;
    } else {
        dsf_merge(dsf, ai, bi);
    }
}

static int check_completion(game_state *state, int mark)
{
    int w = state->p.w, h = state->p.h, x, y, i, target, ret = TRUE;
    int ntrack, nnotrack, ntrackcomplete;
    int *dsf, loopclass, pathclass;

    if (mark) {
        for (i = 0; i < w+h; i++) {
            state->num_errors[i] = 0;
        }
        for (i = 0; i < w*h; i++) {
            state->sflags[i] &= ~S_ERROR;
            if (S_E_COUNT(state, i%w, i/w, E_TRACK) > 0) {
                if (S_E_COUNT(state, i%w, i/w, E_TRACK) > 2)
                    state->sflags[i] |= S_ERROR;
            }
        }
    }

    /* A cell is 'complete', for the purposes of marking the game as
     * finished, if it has two edges marked as TRACK. But it only has
     * to have one edge marked as TRACK, or be filled in as trackful
     * without any specific edges known, to count towards checking
     * row/column clue errors. */
    for (x = 0; x < w; x++) {
        target = state->numbers->numbers[x];
        ntrack = nnotrack = ntrackcomplete = 0;
        for (y = 0; y < h; y++) {
            if (S_E_COUNT(state, x, y, E_TRACK) > 0 ||
                state->sflags[y*w+x] & S_TRACK)
                ntrack++;
            if (S_E_COUNT(state, x, y, E_TRACK) == 2)
                ntrackcomplete++;
            if (state->sflags[y*w+x] & S_NOTRACK)
                nnotrack++;
        }
        if (mark) {
            if (ntrack > target || nnotrack > (h-target)) {
                debug(("col %d error: target %d, track %d, notrack %d",
                       x, target, ntrack, nnotrack));
                state->num_errors[x] = 1;
            }
        }
        if (ntrackcomplete != target)
            ret = FALSE;
    }
    for (y = 0; y < h; y++) {
        target = state->numbers->numbers[w+y];
        ntrack = nnotrack = ntrackcomplete = 0;
        for (x = 0; x < w; x++) {
            if (S_E_COUNT(state, x, y, E_TRACK) > 0 ||
                state->sflags[y*w+x] & S_TRACK)
                ntrack++;
            if (S_E_COUNT(state, x, y, E_TRACK) == 2)
                ntrackcomplete++;
            if (state->sflags[y*w+x] & S_NOTRACK)
                nnotrack++;
        }
        if (mark) {
            if (ntrack > target || nnotrack > (w-target)) {
                debug(("row %d error: target %d, track %d, notrack %d",
                       y, target, ntrack, nnotrack));
                state->num_errors[w+y] = 1;
            }
        }
        if (ntrackcomplete != target)
            ret = FALSE;
    }

    dsf = snewn(w*h, int);
    dsf_init(dsf, w*h);
    loopclass = -1;

    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            dsf_update_completion(state, &loopclass, x, y, R, dsf);
            dsf_update_completion(state, &loopclass, x, y, D, dsf);
        }
    }
    if (loopclass != -1) {
        debug(("loop detected, not complete"));
        ret = FALSE; /* no loop allowed */
        if (mark) {
            for (x = 0; x < w; x++) {
                for (y = 0; y < h; y++) {
                    /* TODO this will only highlight the first loop found */
                    if (dsf_canonify(dsf, y*w + x) == loopclass) {
                        state->sflags[y*w+x] |= S_ERROR;
                    }
                }
            }
        }
    }
    if (mark) {
        pathclass = dsf_canonify(dsf, state->numbers->row_s*w);
        if (pathclass == dsf_canonify(dsf, (h-1)*w + state->numbers->col_s)) {
            /* We have a continuous path between the entrance and the exit: any
               other path must be in error. */
            for (i = 0; i < w*h; i++) {
                if ((dsf_canonify(dsf, i) != pathclass) &&
                    ((state->sflags[i] & S_TRACK) ||
                     (S_E_COUNT(state, i%w, i/w, E_TRACK) > 0)))
                    state->sflags[i] |= S_ERROR;
            }
        }
    }

    if (mark)
        state->completed = ret;
    sfree(dsf);
    return ret;
}

/* Code borrowed from Pearl. */

struct game_ui {
    int dragging, clearing, notrack;
    int drag_sx, drag_sy, drag_ex, drag_ey; /* drag start and end grid coords */
    int clickx, clicky;    /* pixel position of initial click */

    int curx, cury;        /* grid position of keyboard cursor; uses half-size grid */
    int cursor_active;     /* TRUE iff cursor is shown */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->clearing = ui->notrack = ui->dragging = 0;
    ui->drag_sx = ui->drag_sy = ui->drag_ex = ui->drag_ey = -1;
    ui->cursor_active = FALSE;
    ui->curx = ui->cury = 1;

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

static void android_cursor_visibility(game_ui *ui, int visible)
{
    ui->cursor_active = visible;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
#ifdef ANDROID
    if (newstate->completed && ! newstate->used_solve && oldstate && ! oldstate->completed) android_completed();
#endif
}

#define PREFERRED_TILE_SIZE 30
#define HALFSZ (ds->sz6*3)
#define THIRDSZ (ds->sz6*2)
#define TILE_SIZE (ds->sz6*6)

#define BORDER (TILE_SIZE/8)
#define BORDER_WIDTH (max(TILE_SIZE / 32, 1))

#define COORD(x) ( (x+1) * TILE_SIZE + BORDER )
#define CENTERED_COORD(x) ( COORD(x) + TILE_SIZE/2 )
#define FROMCOORD(x) ( ((x) < BORDER) ? -1 : ( ((x) - BORDER) / TILE_SIZE) - 1 )

#define DS_DSHIFT 4     /* R/U/L/D shift, for drag-in-progress flags */

#define DS_ERROR (1 << 8)
#define DS_CLUE (1 << 9)
#define DS_NOTRACK (1 << 10)
#define DS_FLASH (1 << 11)
#define DS_CURSOR (1 << 12) /* cursor in square (centre, or on edge) */
#define DS_TRACK (1 << 13)
#define DS_CLEARING (1 << 14)

#define DS_NSHIFT 16    /* R/U/L/D shift, for no-track edge flags */
#define DS_CSHIFT 20    /* R/U/L/D shift, for cursor-on-edge */

struct game_drawstate {
    int sz6;
    int started;

    int w, h, sz;
    unsigned int *flags, *flags_drag;
    int *num_errors;
};

static void update_ui_drag(const game_state *state, game_ui *ui, int gx, int gy)
{
    int w = state->p.w, h = state->p.h;
    int dx = abs(ui->drag_sx - gx), dy = abs(ui->drag_sy - gy);

    if (dy == 0) {
        ui->drag_ex = gx < 0 ? 0 : gx >= w ? w-1 : gx;
        ui->drag_ey = ui->drag_sy;
        ui->dragging = TRUE;
    } else if (dx == 0) {
        ui->drag_ex = ui->drag_sx;
        ui->drag_ey = gy < 0 ? 0 : gy >= h ? h-1 : gy;
        ui->dragging = TRUE;
    } else {
        ui->drag_ex = ui->drag_sx;
        ui->drag_ey = ui->drag_sy;
        ui->dragging = FALSE;
    }
}

static int ui_can_flip_edge(const game_state *state, int x, int y, int dir,
                            int notrack)
{
    int w = state->p.w /*, h = state->shared->h, sz = state->shared->sz */;
    int x2 = x + DX(dir);
    int y2 = y + DY(dir);
    unsigned int sf1, sf2, ef;

    if (!INGRID(state, x, y) || !INGRID(state, x2, y2))
        return FALSE;

    sf1 = state->sflags[y*w + x];
    sf2 = state->sflags[y2*w + x2];
    if ( !notrack && ((sf1 & S_CLUE) || (sf2 & S_CLUE)) )
        return FALSE;

    ef = S_E_FLAGS(state, x, y, dir);
    if (notrack) {
      /* if we're going to _set_ NOTRACK (i.e. the flag is currently unset),
         make sure the edge is not already set to TRACK. The adjacent squares
         could be set to TRACK, because we don't know which edges the general
         square setting refers to. */
      if (!(ef & E_NOTRACK) && (ef & E_TRACK))
          return FALSE;
    } else {
      if (!(ef & E_TRACK)) {
          /* if we're going to _set_ TRACK, make sure neither adjacent square nor
             the edge itself is already set to NOTRACK. */
          if ((sf1 & S_NOTRACK) || (sf2 & S_NOTRACK) || (ef & E_NOTRACK))
              return FALSE;
          /* if we're going to _set_ TRACK, make sure neither adjacent square has
             2 track flags already.  */
          if ((S_E_COUNT(state, x, y, E_TRACK) >= 2) ||
              (S_E_COUNT(state, x2, y2, E_TRACK) >= 2))
              return FALSE;
          }
    }
    return TRUE;
}

static int ui_can_flip_square(const game_state *state, int x, int y, int notrack)
{
    int w = state->p.w, trackc;
    unsigned sf;

    if (!INGRID(state, x, y)) return FALSE;
    sf = state->sflags[y*w+x];
    trackc = S_E_COUNT(state, x, y, E_TRACK);

    if (sf & S_CLUE) return FALSE;

    if (notrack) {
        /* If we're setting S_NOTRACK, we cannot have either S_TRACK or any E_TRACK. */
        if (!(sf & S_NOTRACK) && ((sf & S_TRACK) || (trackc > 0)))
            return FALSE;
    } else {
        /* If we're setting S_TRACK, we cannot have any S_NOTRACK (we could have
          E_NOTRACK, though, because one or two wouldn't rule out a track) */
        if (!(sf & S_TRACK) && (sf & S_NOTRACK))
            return FALSE;
    }
    return TRUE;
}

static char *edge_flip_str(const game_state *state, int x, int y, int dir, int notrack, char *buf) {
    unsigned ef = S_E_FLAGS(state, x, y, dir);
    char c;

    if (notrack)
        c = (ef & E_NOTRACK) ? 'n' : 'N';
    else
        c = (ef & E_TRACK) ? 't' : 'T';

    sprintf(buf, "%c%c%d,%d", c, MOVECHAR(dir), x, y);
    return dupstr(buf);
}

static char *square_flip_str(const game_state *state, int x, int y, int notrack, char *buf) {
    unsigned f = state->sflags[y*state->p.w+x];
    char c;

    if (notrack)
        c = (f & E_NOTRACK) ? 'n' : 'N';
    else
        c = (f & E_TRACK) ? 't' : 'T';

    sprintf(buf, "%cS%d,%d", c, x, y);
    return dupstr(buf);
}

#define SIGN(x) ((x<0) ? -1 : (x>0))

static game_state *copy_and_apply_drag(const game_state *state, const game_ui *ui)
{
    game_state *after = dup_game(state);
    int x1, y1, x2, y2, x, y, w = state->p.w;
    unsigned f = ui->notrack ? S_NOTRACK : S_TRACK, ff;

    x1 = min(ui->drag_sx, ui->drag_ex); x2 = max(ui->drag_sx, ui->drag_ex);
    y1 = min(ui->drag_sy, ui->drag_ey); y2 = max(ui->drag_sy, ui->drag_ey);

    /* actually either x1 == x2, or y1 == y2, but it's easier just to code
       the nested loop. */
    for (x = x1; x <= x2; x++) {
        for (y = y1; y <= y2; y++) {
            ff = state->sflags[y*w+x];
            if (ui->clearing && !(ff & f))
                continue; /* nothing to do, clearing and already clear */
            else if (!ui->clearing && (ff & f))
                continue; /* nothing to do, setting and already set */
            else if (ui_can_flip_square(state, x, y, ui->notrack))
                after->sflags[y*w+x] ^= f;
        }
    }
    return after;
}

#define KEY_DIRECTION(btn) (\
    (btn) == CURSOR_DOWN ? D : (btn) == CURSOR_UP ? U :\
    (btn) == CURSOR_LEFT ? L : R)

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->p.w, h = state->p.h, direction;
    int gx = FROMCOORD(x), gy = FROMCOORD(y);
    char tmpbuf[80];

    /* --- mouse operations --- */

    if (IS_MOUSE_DOWN(button)) {
        ui->cursor_active = FALSE;
        ui->dragging = FALSE;

        if (!INGRID(state, gx, gy)) {
            /* can't drag from off grid */
            return NULL;
        }

        if (button == RIGHT_BUTTON) {
            ui->notrack = TRUE;
            ui->clearing = state->sflags[gy*w+gx] & S_NOTRACK;
        } else {
            ui->notrack = FALSE;
            ui->clearing = state->sflags[gy*w+gx] & S_TRACK;
        }

        ui->clickx = x;
        ui->clicky = y;
        ui->drag_sx = ui->drag_ex = gx;
        ui->drag_sy = ui->drag_ey = gy;

        return "";
    }

    if (IS_MOUSE_DRAG(button)) {
        ui->cursor_active = FALSE;
        update_ui_drag(state, ui, gx, gy);
        return "";
    }

    if (IS_MOUSE_RELEASE(button)) {
        ui->cursor_active = FALSE;
        if (ui->dragging &&
            (ui->drag_sx != ui->drag_ex || ui->drag_sy != ui->drag_ey)) {
            game_state *dragged = copy_and_apply_drag(state, ui);
            char *ret = move_string_diff(state, dragged, FALSE);

            ui->dragging = 0;
            free_game(dragged);

            return ret;
        } else {
            int cx, cy;

            /* We might still have been dragging (and just done a one-
             * square drag): cancel drag, so undo doesn't make it like
             * a drag-in-progress. */
            ui->dragging = 0;

            /* Click (or tiny drag). Work out which edge we were
             * closest to. */

            /*
             * We process clicks based on the mouse-down location,
             * because that's more natural for a user to carefully
             * control than the mouse-up.
             */
            x = ui->clickx;
            y = ui->clicky;

            cx = CENTERED_COORD(gx);
            cy = CENTERED_COORD(gy);

            if (!INGRID(state, gx, gy) || FROMCOORD(x) != gx || FROMCOORD(y) != gy)
                return "";

            if (max(abs(x-cx),abs(y-cy)) < TILE_SIZE/4) {
                if (ui_can_flip_square(state, gx, gy, button == RIGHT_RELEASE))
                    return square_flip_str(state, gx, gy, button == RIGHT_RELEASE, tmpbuf);
                return "";
            } else {
                if (abs(x-cx) < abs(y-cy)) {
                    /* Closest to top/bottom edge. */
                    direction = (y < cy) ? U : D;
                } else {
                    /* Closest to left/right edge. */
                    direction = (x < cx) ? L : R;
                }
                if (ui_can_flip_edge(state, gx, gy, direction,
                        button == RIGHT_RELEASE))
                    return edge_flip_str(state, gx, gy, direction,
                            button == RIGHT_RELEASE, tmpbuf);
                else
                    return "";
            }
        }
    }

    /* --- cursor/keyboard operations --- */

    if (IS_CURSOR_MOVE(button)) {
        int dx = (button == CURSOR_LEFT) ? -1 : ((button == CURSOR_RIGHT) ? +1 : 0);
        int dy = (button == CURSOR_DOWN) ? +1 : ((button == CURSOR_UP)    ? -1 : 0);

        if (!ui->cursor_active) {
            ui->cursor_active = TRUE;
            return "";
        }

        ui->curx = ui->curx + dx;
        ui->cury = ui->cury + dy;
        if ((ui->curx % 2 == 0) && (ui->cury % 2 == 0)) {
            /* disallow cursor on square corners: centres and edges only */
            ui->curx += dx; ui->cury += dy;
        }
        ui->curx = min(max(ui->curx, 1), 2*w-1);
        ui->cury = min(max(ui->cury, 1), 2*h-1);
        return "";
    }

    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cursor_active) {
            ui->cursor_active = TRUE;
            return "";
        }
        /* click on square corner does nothing (shouldn't get here) */
        if ((ui->curx % 2) == 0 && (ui->cury % 2 == 0))
            return "";

        gx = ui->curx / 2;
        gy = ui->cury / 2;
        direction = ((ui->curx % 2) == 0) ? L : ((ui->cury % 2) == 0) ? U : 0;

        if (direction &&
            ui_can_flip_edge(state, gx, gy, direction, button == CURSOR_SELECT2))
            return edge_flip_str(state, gx, gy, direction, button == CURSOR_SELECT2, tmpbuf);
        else if (!direction &&
                 ui_can_flip_square(state, gx, gy, button == CURSOR_SELECT2))
            return square_flip_str(state, gx, gy, button == CURSOR_SELECT2, tmpbuf);
        return "";
    }

#if 0
    /* helps to debug the solver */
    if (button == 'H' || button == 'h')
        return dupstr("H");
#endif

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->p.w, x, y, n, i;
    char c, d;
    unsigned f;
    game_state *ret = dup_game(state);

    /* this is breaking the bank on GTK, which vsprintf's into a fixed-size buffer
     * which is 4096 bytes long. vsnprintf needs a feature-test macro to use, faff. */
    /*debug(("move: %s\n", move));*/

    while (*move) {
        c = *move;
        if (c == 'S') {
            ret->used_solve = TRUE;
            move++;
        } else if (c == 'T' || c == 't' || c == 'N' || c == 'n') {
            /* set track, clear track; set notrack, clear notrack */
            move++;
            if (sscanf(move, "%c%d,%d%n", &d, &x, &y, &n) != 3)
                goto badmove;
            if (!INGRID(state, x, y)) goto badmove;

            f = (c == 'T' || c == 't') ? S_TRACK : S_NOTRACK;

            if (d == 'S') {
                if (c == 'T' || c == 'N')
                    ret->sflags[y*w+x] |= f;
                else
                    ret->sflags[y*w+x] &= ~f;
            } else if (d == 'U' || d == 'D' || d == 'L' || d == 'R') {
                for (i = 0; i < 4; i++) {
                    unsigned df = 1<<i;

                    if (MOVECHAR(df) == d) {
                        if (c == 'T' || c == 'N')
                            S_E_SET(ret, x, y, df, f);
                        else
                            S_E_CLEAR(ret, x, y, df, f);
                    }
                }
            } else
                goto badmove;
            move += n;
        } else if (c == 'H') {
            tracks_solve(ret, DIFFCOUNT);
            move++;
        } else {
            goto badmove;
        }
        if (*move == ';')
            move++;
        else if (*move)
            goto badmove;
    }

    check_completion(ret, TRUE);

    return ret;

    badmove:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define FLASH_TIME 0.5F

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct {
        int sz6;
    } ads, *ds = &ads;
    ads.sz6 = tilesize/6;

    *x = (params->w+2) * TILE_SIZE + 2 * BORDER;
    *y = (params->h+2) * TILE_SIZE + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->sz6 = tilesize/6;
}

enum {
    COL_BACKGROUND, COL_LOWLIGHT,
    COL_TRACK_BACKGROUND = COL_LOWLIGHT,
    COL_HIGHLIGHT,
    COL_GRID, COL_CLUE, COL_CURSOR,
    COL_TRACK, COL_TRACK_CLUE, COL_SLEEPER,
    COL_DRAGON, COL_DRAGOFF,
    COL_ERROR, COL_FLASH,
    NCOLOURS
};

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_TRACK_CLUE * 3 + i] = 0.0F;
        ret[COL_TRACK * 3 + i] = 0.5F;
        ret[COL_CLUE * 3 + i] = 0.0F;
        ret[COL_GRID * 3 + i] = 0.0F;
        ret[COL_CURSOR * 3 + i] = 0.6F;
    }

    ret[COL_SLEEPER * 3 + 0] = 0.5F;
    ret[COL_SLEEPER * 3 + 1] = 0.4F;
    ret[COL_SLEEPER * 3 + 2] = 0.1F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_DRAGON * 3 + 0] = 0.0F;
    ret[COL_DRAGON * 3 + 1] = 0.0F;
    ret[COL_DRAGON * 3 + 2] = 1.0F;

    ret[COL_DRAGOFF * 3 + 0] = 0.8F;
    ret[COL_DRAGOFF * 3 + 1] = 0.8F;
    ret[COL_DRAGOFF * 3 + 2] = 1.0F;

    ret[COL_FLASH * 3 + 0] = 1.0F;
    ret[COL_FLASH * 3 + 1] = 1.0F;
    ret[COL_FLASH * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->sz6 = 0;
    ds->started = FALSE;

    ds->w = state->p.w;
    ds->h = state->p.h;
    ds->sz = ds->w*ds->h;
    ds->flags = snewn(ds->sz, unsigned int);
    ds->flags_drag = snewn(ds->sz, unsigned int);
    for (i = 0; i < ds->sz; i++)
        ds->flags[i] = ds->flags_drag[i] = 0;

    ds->num_errors = snewn(ds->w+ds->h, int);
    for (i = 0; i < ds->w+ds->h; i++)
        ds->num_errors[i] = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->flags);
    sfree(ds->flags_drag);
    sfree(ds->num_errors);
    sfree(ds);
}

static void draw_circle_sleepers(drawing *dr, game_drawstate *ds,
                                 float cx, float cy, float r2, float thickness, int c)
{
    float qr6 = (float)PI/12, qr3 = (float)PI/6, th, x1, y1, x2, y2;
    float t6 = THIRDSZ/2.0F, r1 = t6;
    int i;

    for (i = 0; i < 12; i++) {
        th = qr6 + (i*qr3);
        x1 = r1*(float)cos(th);
        x2 = r2*(float)cos(th);
        y1 = r1*(float)sin(th);
        y2 = r2*(float)sin(th);
        draw_thick_line(dr, thickness, cx+x1, cy+y1, cx+x2, cy+y2, c);
    }
}

static void draw_tracks_specific(drawing *dr, game_drawstate *ds,
                                 int x, int y, unsigned int flags,
                                 int ctrack, int csleeper)
{
    float ox = (float)COORD(x), oy = (float)COORD(y), cx, cy;
    float t1 = (float)TILE_SIZE, t3 = TILE_SIZE/3.0F, t6 = TILE_SIZE/6.0F;
    int d, i;
    float thick_track = TILE_SIZE/8.0F, thick_sleeper = TILE_SIZE/12.0F;

    if (flags == LR) {
        for (i = 1; i <= 7; i+=2) {
            cx = ox + TILE_SIZE/8.0F*i;
            draw_thick_line(dr, thick_sleeper,
                            cx, oy+t6, cx, oy+t6+2*t3, csleeper);
        }
        draw_thick_line(dr, thick_track, ox, oy + t3, ox + TILE_SIZE, oy + t3, ctrack);
        draw_thick_line(dr, thick_track, ox, oy + 2*t3, ox + TILE_SIZE, oy + 2*t3, ctrack);
        return;
    }
    if (flags == UD) {
        for (i = 1; i <= 7; i+=2) {
            cy = oy + TILE_SIZE/8.0F*i;
            draw_thick_line(dr, thick_sleeper,
                            ox+t6, cy, ox+t6+2*t3, cy, csleeper);
        }
        debug(("vert line: x=%.2f, thick=%.2f", ox + t3, thick_track));
        draw_thick_line(dr, thick_track, ox + t3, oy, ox + t3, oy + TILE_SIZE, ctrack);
        draw_thick_line(dr, thick_track, ox + 2*t3, oy, ox + 2*t3, oy + TILE_SIZE, ctrack);
        return;
    }
    if (flags == UL || flags == DL || flags == UR || flags == DR) {
        cx = (flags & L) ? ox : ox + TILE_SIZE;
        cy = (flags & U) ? oy : oy + TILE_SIZE;

        draw_circle_sleepers(dr, ds, cx, cy, (float)(5*t6), thick_sleeper, csleeper);

        draw_thick_circle(dr, thick_track, (float)cx, (float)cy,
                                  2*t3, -1, ctrack);
        draw_thick_circle(dr, thick_track, (float)cx, (float)cy,
                                  t3, -1, ctrack);

        return;
    }

    for (d = 1; d < 16; d *= 2) {
        float ox1 = 0, ox2 = 0, oy1 = 0, oy2 = 0;

        if (!(flags & d)) continue;

        for (i = 1; i <= 2; i++) {
            if (d == L) {
                ox1 = 0;
                ox2 = thick_track;
                oy1 = oy2 = i*t3;
            } else if (d == R) {
                ox1 = t1;
                ox2 = t1 - thick_track;
                oy1 = oy2 = i*t3;
            } else if (d == U) {
                ox1 = ox2 = i*t3;
                oy1 = 0;
                oy2 = thick_track;
            } else if (d == D) {
                ox1 = ox2 = i*t3;
                oy1 = t1;
                oy2 = t1 - thick_track;
            }
            draw_thick_line(dr, thick_track, ox+ox1, oy+oy1, ox+ox2, oy+oy2, ctrack);
        }
    }
}

static unsigned int best_bits(unsigned int flags, unsigned int flags_drag, int *col)
{
    int nb_orig = nbits[flags & ALLDIR], nb_drag = nbits[flags_drag & ALLDIR];

    if (nb_orig > nb_drag) {
        *col = COL_DRAGOFF;
        return flags & ALLDIR;
    } else if (nb_orig < nb_drag) {
        *col = COL_DRAGON;
        return flags_drag & ALLDIR;
    }
    return flags & ALLDIR; /* same number of bits: no special colour. */
}

static void draw_square(drawing *dr, game_drawstate *ds,
                        int x, int y, unsigned int flags, unsigned int flags_drag)
{
    int t2 = HALFSZ, t16 = HALFSZ/4, off;
    int ox = COORD(x), oy = COORD(y), cx = ox + t2, cy = oy + t2, d, c;
    int bg = (flags & DS_TRACK) ? COL_TRACK_BACKGROUND : COL_BACKGROUND;
    unsigned int flags_best;

    assert(dr);

    /* Clip to the grid square. */
    clip(dr, ox, oy, TILE_SIZE, TILE_SIZE);

    /* Clear the square. */
    best_bits((flags & DS_TRACK) == DS_TRACK,
              (flags_drag & DS_TRACK) == DS_TRACK, &bg);
    draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE, bg);

    /* Draw outline of grid square */
    draw_line(dr, ox, oy, COORD(x+1), oy, COL_GRID);
    draw_line(dr, ox, oy, ox, COORD(y+1), COL_GRID);

    /* More outlines for clue squares. */
    if (flags & DS_CURSOR) {
        int curx, cury, curw, curh;

        off = t16;
        curx = ox + off; cury = oy + off;
        curw = curh = TILE_SIZE - (2*off) + 1;

        if (flags & (U << DS_CSHIFT)) {
            cury = oy - off; curh = 2*off + 1;
        } else if (flags & (D << DS_CSHIFT)) {
            cury = oy + TILE_SIZE - off; curh = 2*off + 1;
        } else if (flags & (L << DS_CSHIFT)) {
            curx = ox - off; curw = 2*off + 1;
        } else if (flags & (R << DS_CSHIFT)) {
            curx = ox + TILE_SIZE - off; curw = 2*off + 1;
        }

        draw_rect_outline(dr, curx, cury, curw, curh, COL_GRID);
    }

    /* Draw tracks themselves */
    c = (flags & DS_ERROR) ? COL_ERROR :
      (flags & DS_FLASH) ? COL_FLASH :
      (flags & DS_CLUE) ? COL_TRACK_CLUE : COL_TRACK;
    flags_best = best_bits(flags, flags_drag, &c);
    draw_tracks_specific(dr, ds, x, y, flags_best, c, COL_SLEEPER);

    /* Draw no-track marks, if present, in square and on edges. */
    c = COL_TRACK;
    flags_best = best_bits((flags & DS_NOTRACK) == DS_NOTRACK,
                           (flags_drag & DS_NOTRACK) == DS_NOTRACK, &c);
    if (flags_best) {
        off = HALFSZ/2;
        draw_line(dr, cx - off, cy - off, cx + off, cy + off, c);
        draw_line(dr, cx - off, cy + off, cx + off, cy - off, c);
    }

    c = COL_TRACK;
    flags_best = best_bits(flags >> DS_NSHIFT, flags_drag >> DS_NSHIFT, &c);
    for (d = 1; d < 16; d *= 2) {
        off = t16;
        cx = ox + t2;
        cy = oy + t2;

        if (flags_best & d) {
            cx += (d == R) ? t2 : (d == L) ? -t2 : 0;
            cy += (d == D) ? t2 : (d == U) ? -t2 : 0;

            draw_line(dr, cx - off, cy - off, cx + off, cy + off, c);
            draw_line(dr, cx - off, cy + off, cx + off, cy - off, c);
        }
    }

    unclip(dr);
    draw_update(dr, ox, oy, TILE_SIZE, TILE_SIZE);
}

static void draw_clue(drawing *dr, game_drawstate *ds, int w, int clue, int i, int col)
{
    int cx, cy, tsz = TILE_SIZE/2;
    char buf[20];

    if (i < w) {
        cx = CENTERED_COORD(i);
        cy = CENTERED_COORD(-1);
    } else {
        cx = CENTERED_COORD(w);
        cy = CENTERED_COORD(i-w);
    }

    draw_rect(dr, cx - tsz + BORDER, cy - tsz + BORDER,
              TILE_SIZE - BORDER, TILE_SIZE - BORDER, COL_BACKGROUND);
    sprintf(buf, "%d", clue);
    draw_text(dr, cx, cy, FONT_VARIABLE, tsz, ALIGN_VCENTRE|ALIGN_HCENTRE,
              col, buf);
    draw_update(dr, cx - tsz, cy - tsz, TILE_SIZE, TILE_SIZE);
}

static void draw_loop_ends(drawing *dr, game_drawstate *ds,
                           const game_state *state, int c)
{
    int tsz = TILE_SIZE/2;

    draw_text(dr, CENTERED_COORD(-1), CENTERED_COORD(state->numbers->row_s),
              FONT_VARIABLE, tsz, ALIGN_VCENTRE|ALIGN_HCENTRE,
              c, "A");

    draw_text(dr, CENTERED_COORD(state->numbers->col_s), CENTERED_COORD(state->p.h),
              FONT_VARIABLE, tsz, ALIGN_VCENTRE|ALIGN_HCENTRE,
              c, "B");
}

static unsigned int s2d_flags(const game_state *state, int x, int y, const game_ui *ui)
{
    unsigned int f;
    int w = state->p.w;

    f = S_E_DIRS(state, x, y, E_TRACK);
    f |= (S_E_DIRS(state, x, y, E_NOTRACK) << DS_NSHIFT);

    if (state->sflags[y*w+x] & S_ERROR)
        f |= DS_ERROR;
    if (state->sflags[y*w+x] & S_CLUE)
        f |= DS_CLUE;
    if (state->sflags[y*w+x] & S_NOTRACK)
        f |= DS_NOTRACK;
    if ((state->sflags[y*w+x] & S_TRACK) || (S_E_COUNT(state, x, y, E_TRACK) > 0))
        f |= DS_TRACK;

    if (ui->cursor_active) {
        if (ui->curx >= x*2 && ui->curx <= (x+1)*2 &&
            ui->cury >= y*2 && ui->cury <= (y+1)*2) {
            f |= DS_CURSOR;
            if (ui->curx == x*2)        f |= (L << DS_CSHIFT);
            if (ui->curx == (x+1)*2)    f |= (R << DS_CSHIFT);
            if (ui->cury == y*2)        f |= (U << DS_CSHIFT);
            if (ui->cury == (y+1)*2)    f |= (D << DS_CSHIFT);
        }
    }

    return f;
}

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
                        const game_state *state, int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, x, y, force = 0, flashing = 0, w = ds->w, h = ds->h;
    game_state *drag_state = NULL;

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed and
         * can vary with front ends. To be on the safe side, all games
         * should start by drawing a big background-colour rectangle
         * covering the whole window.
         */
        draw_rect(dr, 0, 0, (w+2)*TILE_SIZE + 2*BORDER, (h+2)*TILE_SIZE + 2*BORDER,
                  COL_BACKGROUND);

        draw_loop_ends(dr, ds, state, COL_CLUE);

        draw_line(dr, COORD(ds->w), COORD(0), COORD(ds->w), COORD(ds->h), COL_GRID);
        draw_line(dr, COORD(0), COORD(ds->h), COORD(ds->w), COORD(ds->h), COL_GRID);

        draw_update(dr, 0, 0, (w+2)*TILE_SIZE + 2*BORDER, (h+2)*TILE_SIZE + 2*BORDER);

        ds->started = TRUE;
        force = 1;
    }

    for (i = 0; i < w+h; i++) {
        if (force || (state->num_errors[i] != ds->num_errors[i])) {
            ds->num_errors[i] = state->num_errors[i];
            draw_clue(dr, ds, w, state->numbers->numbers[i], i,
                      ds->num_errors[i] ? COL_ERROR : COL_CLUE);
        }
    }

    if (flashtime > 0 &&
            (flashtime <= FLASH_TIME/3 ||
             flashtime >= FLASH_TIME*2/3))
        flashing = DS_FLASH;

    if (ui->dragging)
        drag_state = copy_and_apply_drag(state, ui);

    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            unsigned int f, f_d;

            f = s2d_flags(state, x, y, ui) | flashing;
            f_d = drag_state ? s2d_flags(drag_state, x, y, ui) : f;

            if (f != ds->flags[y*w+x] || f_d != ds->flags_drag[y*w+x] || force) {
                ds->flags[y*w+x] = f;
                ds->flags_drag[y*w+x] = f_d;
                draw_square(dr, ds, x, y, f, f_d);
            }
        }
    }

    if (drag_state) free_game(drag_state);
}

static float game_anim_length(const game_state *oldstate, const game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate, const game_state *newstate,
                               int dir, game_ui *ui)
{
    if (!oldstate->completed &&
            newstate->completed && !newstate->used_solve)
        return FLASH_TIME;
    else
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

    /* The Times uses 7mm squares */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->p.w, h = state->p.h;
    int black = print_mono_colour(dr, 0), grey = print_grey_colour(dr, 0.5F);
    int x, y, i;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /* Grid, then border (second so it is on top) */
    print_line_width(dr, TILE_SIZE / 24);
    for (x = 1; x < w; x++)
        draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), grey);
    for (y = 1; y < h; y++)
        draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), grey);

    print_line_width(dr, TILE_SIZE / 16);
    draw_rect_outline(dr, COORD(0), COORD(0), w*TILE_SIZE, h*TILE_SIZE, black);

    print_line_width(dr, TILE_SIZE / 24);

    /* clue numbers, and loop ends */
    for (i = 0; i < w+h; i++)
        draw_clue(dr, ds, w, state->numbers->numbers[i], i, black);
    draw_loop_ends(dr, ds, state, black);

    /* clue tracks / solution */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            clip(dr, COORD(x), COORD(y), TILE_SIZE, TILE_SIZE);
            draw_tracks_specific(dr, ds, x, y, S_E_DIRS(state, x, y, E_TRACK),
                                 black, grey);
            unclip(dr);
        }
    }
}

#ifdef COMBINED
#define thegame tracks
#endif

const struct game thegame = {
    "Train Tracks", "games.tracks", "tracks",
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
    android_request_keys,
    android_cursor_visibility,
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
#ifndef NO_PRINTING
    TRUE, FALSE, game_print_size, game_print,
#endif
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
