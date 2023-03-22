/*
 * range.c: implementation of the Nikoli game 'Kurodoko' / 'Kuromasu'.
 */

/*
 * Puzzle rules: the player is given a WxH grid of white squares, some
 * of which contain numbers. The goal is to paint some of the squares
 * black, such that:
 *
 *  - no cell (err, cell = square) with a number is painted black
 *  - no black cells have an adjacent (horz/vert) black cell
 *  - the white cells are all connected (through other white cells)
 *  - if a cell contains a number n, let h and v be the lengths of the
 *    maximal horizontal and vertical white sequences containing that
 *    cell.  Then n must equal h + v - 1.
 */

/* example instance with its encoding and textual representation, both
 * solved and unsolved (made by thegame.solve and thegame.text_format)
 *
 * +--+--+--+--+--+--+--+
 * |  |  |  |  | 7|  |  |
 * +--+--+--+--+--+--+--+
 * | 3|  |  |  |  |  | 8|
 * +--+--+--+--+--+--+--+
 * |  |  |  |  |  | 5|  |
 * +--+--+--+--+--+--+--+
 * |  |  | 7|  | 7|  |  |
 * +--+--+--+--+--+--+--+
 * |  |13|  |  |  |  |  |
 * +--+--+--+--+--+--+--+
 * | 4|  |  |  |  |  | 8|
 * +--+--+--+--+--+--+--+
 * |  |  | 4|  |  |  |  |
 * +--+--+--+--+--+--+--+
 *
 * 7x7:d7b3e8e5c7a7c13e4d8b4d
 *
 * +--+--+--+--+--+--+--+
 * |..|..|..|..| 7|..|..|
 * +--+--+--+--+--+--+--+
 * | 3|..|##|..|##|..| 8|
 * +--+--+--+--+--+--+--+
 * |##|..|..|##|..| 5|..|
 * +--+--+--+--+--+--+--+
 * |..|..| 7|..| 7|##|..|
 * +--+--+--+--+--+--+--+
 * |..|13|..|..|..|..|..|
 * +--+--+--+--+--+--+--+
 * | 4|..|##|..|##|..| 8|
 * +--+--+--+--+--+--+--+
 * |##|..| 4|..|..|##|..|
 * +--+--+--+--+--+--+--+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#include <stdarg.h>

#define setmember(obj, field) ( (obj) . field = field )

static char *nfmtstr(int n, const char *fmt, ...) {
    va_list va;
    char *ret = snewn(n+1, char);
    va_start(va, fmt);
    vsprintf(ret, fmt, va);
    va_end(va);
    return ret;
}

#define SWAP(type, lvar1, lvar2) do { \
    type tmp = (lvar1); \
    (lvar1) = (lvar2); \
    (lvar2) = tmp; \
} while (0)

/* ----------------------------------------------------------------------
 * Game parameters, presets, states
 */

typedef signed char puzzle_size;

struct game_params {
    puzzle_size w;
    puzzle_size h;
};

struct game_state {
    struct game_params params;
    bool has_cheated, was_solved;
    puzzle_size *grid;
};

#define DEFAULT_PRESET 0
static struct game_params range_presets[] = {{9, 6}, {12, 8}, {13, 9}, {16, 11}};
/* rationale: I want all four combinations of {odd/even, odd/even}, as
 * they play out differently with respect to two-way symmetry.  I also
 * want them to be generated relatively fast yet still be large enough
 * to be entertaining for a decent amount of time, and I want them to
 * make good use of monitor real estate (the typical screen resolution
 * is why I do 13x9 and not 9x13).
 */

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = range_presets[DEFAULT_PRESET]; /* structure copy */
    return ret;
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params; /* structure copy */
    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;

    if (i < 0 || i >= lenof(range_presets)) return false;

    ret = default_params();
    *ret = range_presets[i]; /* struct copy */
    *params = ret;

    *name = nfmtstr(40, "%d x %d", range_presets[i].w, range_presets[i].h);

    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static void decode_params(game_params *params, char const *string)
{
    /* FIXME check for puzzle_size overflow and decoding issues */
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char) *string)) ++string;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char str[80];
    sprintf(str, "%dx%d", params->w, params->h);
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;

    ret = snewn(3, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    ret[0].u.string.sval = nfmtstr(10, "%d", params->w);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    ret[1].u.string.sval = nfmtstr(10, "%d", params->h);

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *configuration)
{
    game_params *ret = snew(game_params);
    ret->w = atoi(configuration[0].u.string.sval);
    ret->h = atoi(configuration[1].u.string.sval);
    return ret;
}

#define memdup(dst, src, n, type) do { \
    dst = snewn(n, type); \
    memcpy(dst, src, n * sizeof (type)); \
} while (0)

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int const n = state->params.w * state->params.h;

    *ret = *state; /* structure copy */

    /* copy the poin_tee_, set a new value of the poin_ter_ */
    memdup(ret->grid, state->grid, n, puzzle_size);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}


/* ----------------------------------------------------------------------
 * The solver subsystem.
 *
 * The solver is used for two purposes:
 *  - To solve puzzles when the user selects `Solve'.
 *  - To test solubility of a grid as clues are being removed from it
 *    during the puzzle generation.
 *
 * It supports the following ways of reasoning:
 *
 *  - A cell adjacent to a black cell must be white.
 *
 *  - If painting a square black would bisect the white regions, that
 *    square is white (by finding biconnected components' cut points)
 *
 *  - A cell with number n, covering at most k white squares in three
 *    directions must white-cover n-k squares in the last direction.
 *
 *  - A cell with number n known to cover k squares, if extending the
 *    cover by one square in a given direction causes the cell to
 *    cover _more_ than n squares, that extension cell must be black.
 *
 *    (either if the square already covers n, or if it extends into a
 *    chunk of size > n - k)
 *
 *  - Recursion.  Pick any cell and see if this leads to either a
 *    contradiction or a solution (and then act appropriately).
 *
 *
 * TODO:
 *
 * (propagation upper limit)
 *  - If one has two numbers on the same line, the smaller limits the
 *    larger.  Example: in |b|_|_|8|4|_|_|b|, only two _'s can be both
 *    white and connected to the "8" cell; so that cell will propagate
 *    at least four cells orthogonally to the displayed line (which is
 *    better than the current "at least 2").
 *
 * (propagation upper limit)
 *  - cells can't propagate into other cells if doing so exceeds that
 *    number.  Example: in |b|4|.|.|2|b|, at most one _ can be white;
 *    otherwise, the |2| would have too many reaching white cells.
 *
 * (propagation lower and upper limit)
 *  - `Full Combo': in each four directions d_1 ... d_4, find a set of
 *    possible propagation distances S_1 ... S_4.  For each i=1..4,
 *    for each x in S_i: if not exists (y, z, w) in the other sets
 *    such that (x+y+z+w+1 == clue value): then remove x from S_i.
 *    Repeat until this stabilizes.  If any cell would contradict
 */

#define idx(i, j, w) ((i)*(w) + (j))
#define out_of_bounds(r, c, w, h) \
   ((r) < 0 || (r) >= h || (c) < 0 || (c) >= w)

typedef struct square {
    puzzle_size r, c;
} square;

enum {BLACK = -2, WHITE, EMPTY};
/* white is for pencil marks, empty is undecided */

static int const dr[4] = {+1,  0, -1,  0};
static int const dc[4] = { 0, +1,  0, -1};
static int const cursors[4] = /* must match dr and dc */
{CURSOR_DOWN, CURSOR_RIGHT, CURSOR_UP, CURSOR_LEFT};

typedef struct move {
    square square;
    unsigned int colour: 1;
} move;
enum {M_BLACK = 0, M_WHITE = 1};

typedef move *(reasoning)(game_state *state,
                          int nclues,
                          const square *clues,
                          move *buf);

static reasoning solver_reasoning_not_too_big;
static reasoning solver_reasoning_adjacency;
static reasoning solver_reasoning_connectedness;
static reasoning solver_reasoning_recursion;

enum {
    DIFF_NOT_TOO_BIG,
    DIFF_ADJACENCY,
    DIFF_CONNECTEDNESS,
    DIFF_RECURSION
};

static move *solve_internal(const game_state *state, move *base, int diff);

static char *solve_game(const game_state *orig, const game_state *curpos,
                        const char *aux, const char **error)
{
    int const n = orig->params.w * orig->params.h;
    move *const base = snewn(n, move);
    move *moves = solve_internal(orig, base, DIFF_RECURSION);

    char *ret = NULL;

    if (moves != NULL) {
        int const k = moves - base;
        char *str = ret = snewn(15*k + 2, char);
        char colour[2] = "BW";
        move *it;
        *str++ = 'S';
        *str = '\0';
        for (it = base; it < moves; ++it)
            str += sprintf(str, "%c,%d,%d", colour[it->colour],
                           it->square.r, it->square.c);
    } else *error = "This puzzle instance contains a contradiction";

    sfree(base);
    return ret;
}

static square *find_clues(const game_state *state, int *ret_nclues);
static move *do_solve(game_state *state,
                      int nclues,
                      const square *clues,
                      move *move_buffer,
                      int difficulty);

/* new_game_desc entry point in the solver subsystem */
static move *solve_internal(const game_state *state, move *base, int diff)
{
    int nclues;
    square *const clues = find_clues(state, &nclues);
    game_state *dup = dup_game(state);
    move *const moves = do_solve(dup, nclues, clues, base, diff);
    free_game(dup);
    sfree(clues);
    return moves;
}

static reasoning *const reasonings[] = {
    solver_reasoning_not_too_big,
    solver_reasoning_adjacency,
    solver_reasoning_connectedness,
    solver_reasoning_recursion
};

static move *do_solve(game_state *state,
                      int nclues,
                      const square *clues,
                      move *move_buffer,
                      int difficulty)
{
    struct move *buf = move_buffer, *oldbuf;
    int i;

    do {
        oldbuf = buf;
        for (i = 0; i < lenof(reasonings) && i <= difficulty; ++i) {
            /* only recurse if all else fails */
            if (i == DIFF_RECURSION && buf > oldbuf) continue;
            buf = (*reasonings[i])(state, nclues, clues, buf);
            if (buf == NULL) return NULL;
        }
    } while (buf > oldbuf);

    return buf;
}

#define MASK(n) (1 << ((n) + 2))

static int runlength(puzzle_size r, puzzle_size c,
                     puzzle_size dr, puzzle_size dc,
                     const game_state *state, int colourmask)
{
    int const w = state->params.w, h = state->params.h;
    int sz = 0;
    while (true) {
        int cell = idx(r, c, w);
        if (out_of_bounds(r, c, w, h)) break;
        if (state->grid[cell] > 0) {
            if (!(colourmask & ~(MASK(BLACK) | MASK(WHITE) | MASK(EMPTY))))
                break;
        } else if (!(MASK(state->grid[cell]) & colourmask)) break;
        ++sz;
        r += dr;
        c += dc;
    }
    return sz;
}

static void solver_makemove(puzzle_size r, puzzle_size c, int colour,
                            game_state *state, move **buffer_ptr)
{
    int const cell = idx(r, c, state->params.w);
    if (out_of_bounds(r, c, state->params.w, state->params.h)) return;
    if (state->grid[cell] != EMPTY) return;
    setmember((*buffer_ptr)->square, r);
    setmember((*buffer_ptr)->square, c);
    setmember(**buffer_ptr, colour);
    ++*buffer_ptr;
    state->grid[cell] = (colour == M_BLACK ? BLACK : WHITE);
}

static move *solver_reasoning_adjacency(game_state *state,
                                        int nclues,
                                        const square *clues,
                                        move *buf)
{
    int r, c, i;
    for (r = 0; r < state->params.h; ++r)
        for (c = 0; c < state->params.w; ++c) {
            int const cell = idx(r, c, state->params.w);
            if (state->grid[cell] != BLACK) continue;
            for (i = 0; i < 4; ++i)
                solver_makemove(r + dr[i], c + dc[i], M_WHITE, state, &buf);
        }
    return buf;
}

enum {NOT_VISITED = -1};

static int dfs_biconnect_visit(puzzle_size r, puzzle_size c,
                               game_state *state,
                               square *dfs_parent, int *dfs_depth,
                               move **buf);

static move *solver_reasoning_connectedness(game_state *state,
                                            int nclues,
                                            const square *clues,
                                            move *buf)
{
    int const w = state->params.w, h = state->params.h, n = w * h;

    square *const dfs_parent = snewn(n, square);
    int *const dfs_depth = snewn(n, int);

    int i;
    for (i = 0; i < n; ++i) {
        dfs_parent[i].r = NOT_VISITED;
        dfs_depth[i] = -n;
    }

    for (i = 0; i < n && state->grid[i] == BLACK; ++i);

    dfs_parent[i].r = i / w;
    dfs_parent[i].c = i % w; /* `dfs root`.parent == `dfs root` */
    dfs_depth[i] = 0;

    dfs_biconnect_visit(i / w, i % w, state, dfs_parent, dfs_depth, &buf);

    sfree(dfs_parent);
    sfree(dfs_depth);

    return buf;
}

/* returns the `lowpoint` of (r, c) */
static int dfs_biconnect_visit(puzzle_size r, puzzle_size c,
                               game_state *state,
                               square *dfs_parent, int *dfs_depth,
                               move **buf)
{
    const puzzle_size w = state->params.w, h = state->params.h;
    int const i = idx(r, c, w), mydepth = dfs_depth[i];
    int lowpoint = mydepth, j, nchildren = 0;

    for (j = 0; j < 4; ++j) {
        const puzzle_size rr = r + dr[j], cc = c + dc[j];
        int const cell = idx(rr, cc, w);

        if (out_of_bounds(rr, cc, w, h)) continue;
        if (state->grid[cell] == BLACK) continue;

        if (dfs_parent[cell].r == NOT_VISITED) {
            int child_lowpoint;
            dfs_parent[cell].r = r;
            dfs_parent[cell].c = c;
            dfs_depth[cell] = mydepth + 1;
            child_lowpoint = dfs_biconnect_visit(rr, cc, state, dfs_parent,
                                                 dfs_depth, buf);

            if (child_lowpoint >= mydepth && mydepth > 0)
                solver_makemove(r, c, M_WHITE, state, buf);

            lowpoint = min(lowpoint, child_lowpoint);
            ++nchildren;
        } else if (rr != dfs_parent[i].r || cc != dfs_parent[i].c) {
            lowpoint = min(lowpoint, dfs_depth[cell]);
        }
    }

    if (mydepth == 0 && nchildren >= 2)
        solver_makemove(r, c, M_WHITE, state, buf);

    return lowpoint;
}

static move *solver_reasoning_not_too_big(game_state *state,
                                          int nclues,
                                          const square *clues,
                                          move *buf)
{
    int const w = state->params.w, runmasks[4] = {
        ~(MASK(BLACK) | MASK(EMPTY)),
        MASK(EMPTY),
        ~(MASK(BLACK) | MASK(EMPTY)),
        ~(MASK(BLACK))
    };
    enum {RUN_WHITE, RUN_EMPTY, RUN_BEYOND, RUN_SPACE};

    int i, runlengths[4][4];

    for (i = 0; i < nclues; ++i) {
        int j, k, whites, space;

        const puzzle_size row = clues[i].r, col = clues[i].c;
        int const clue = state->grid[idx(row, col, w)];

        for (j = 0; j < 4; ++j) {
            puzzle_size r = row + dr[j], c = col + dc[j];
            runlengths[RUN_SPACE][j] = 0;
            for (k = 0; k <= RUN_SPACE; ++k) {
                int l = runlength(r, c, dr[j], dc[j], state, runmasks[k]);
                if (k < RUN_SPACE) {
                    runlengths[k][j] = l;
                    r += dr[j] * l;
                    c += dc[j] * l;
                }
                runlengths[RUN_SPACE][j] += l;
            }
        }

        whites = 1;
        for (j = 0; j < 4; ++j) whites += runlengths[RUN_WHITE][j];

        for (j = 0; j < 4; ++j) {
            int const delta = 1 + runlengths[RUN_WHITE][j];
            const puzzle_size r = row + delta * dr[j];
            const puzzle_size c = col + delta * dc[j];

            if (whites == clue) {
                solver_makemove(r, c, M_BLACK, state, &buf);
                continue;
            }

            if (runlengths[RUN_EMPTY][j] == 1 &&
                whites
                + runlengths[RUN_EMPTY][j]
                + runlengths[RUN_BEYOND][j]
                > clue) {
                solver_makemove(r, c, M_BLACK, state, &buf);
                continue;
            }

            if (whites
                + runlengths[RUN_EMPTY][j]
                + runlengths[RUN_BEYOND][j]
                > clue) {
                runlengths[RUN_SPACE][j] =
                    runlengths[RUN_WHITE][j] +
                    runlengths[RUN_EMPTY][j] - 1;

                if (runlengths[RUN_EMPTY][j] == 1)
                    solver_makemove(r, c, M_BLACK, state, &buf);
            }
        }

        space = 1;
        for (j = 0; j < 4; ++j) space += runlengths[RUN_SPACE][j];
        for (j = 0; j < 4; ++j) {
            puzzle_size r = row + dr[j], c = col + dc[j];

            int k = space - runlengths[RUN_SPACE][j];
            if (k >= clue) continue;

            for (; k < clue; ++k, r += dr[j], c += dc[j])
                solver_makemove(r, c, M_WHITE, state, &buf);
        }
    }
    return buf;
}

static move *solver_reasoning_recursion(game_state *state,
                                        int nclues,
                                        const square *clues,
                                        move *buf)
{
    int const w = state->params.w, n = w * state->params.h;
    int cell, colour;

    for (cell = 0; cell < n; ++cell) {
        int const r = cell / w, c = cell % w;
        int i;
        game_state *newstate;
        move *recursive_result;

        if (state->grid[cell] != EMPTY) continue;

        /* FIXME: add enum alias for smallest and largest (or N) */
        for (colour = M_BLACK; colour <= M_WHITE; ++colour) {
            newstate = dup_game(state);
            newstate->grid[cell] = colour == M_BLACK ? BLACK : WHITE;
            recursive_result = do_solve(newstate, nclues, clues, buf,
                                        DIFF_RECURSION);
            if (recursive_result == NULL) {
                free_game(newstate);
                solver_makemove(r, c, M_BLACK + M_WHITE - colour, state, &buf);
                return buf;
            }
            for (i = 0; i < n && newstate->grid[i] != EMPTY; ++i);
            free_game(newstate);
            if (i == n) return buf;
        }
    }
    return buf;
}

static square *find_clues(const game_state *state, int *ret_nclues)
{
    int r, c, i, nclues = 0;
    square *ret = snewn(state->params.w * state->params.h, struct square);

    for (i = r = 0; r < state->params.h; ++r)
        for (c = 0; c < state->params.w; ++c, ++i)
            if (state->grid[i] > 0) {
                ret[nclues].r = r;
                ret[nclues].c = c;
                ++nclues;
            }

    *ret_nclues = nclues;
    return sresize(ret, nclues + (nclues == 0), square);
}

/* ----------------------------------------------------------------------
 * Puzzle generation
 *
 * Generating kurodoko instances is rather straightforward:
 *
 *  - Start with a white grid and add black squares at randomly chosen
 *    locations, unless colouring that square black would violate
 *    either the adjacency or connectedness constraints.
 *
 *  - For each white square, compute the number it would contain if it
 *    were given as a clue.
 *
 *  - From a starting point of "give _every_ white square as a clue",
 *    for each white square (in a random order), see if the board is
 *    solvable when that square is not given as a clue.  If not, don't
 *    give it as a clue, otherwise do.
 *
 * This never fails, but it's only _almost_ what I do.  The real final
 * step is this:
 *
 *  - From a starting point of "give _every_ white square as a clue",
 *    first remove all clues that are two-way rotationally symmetric
 *    to a black square.  If this leaves the puzzle unsolvable, throw
 *    it out and try again.  Otherwise, remove all _pairs_ of clues
 *    (that are rotationally symmetric) which can be removed without
 *    rendering the puzzle unsolvable.
 *
 * This can fail even if one only removes the black and symmetric
 * clues; indeed it happens often (avg. once or twice per puzzle) when
 * generating 1xN instances.  (If you add black cells they must be in
 * the end, and if you only add one, it's ambiguous where).
 */

/* forward declarations of internal calls */
static void newdesc_choose_black_squares(game_state *state,
                                         const int *shuffle_1toN);
static void newdesc_compute_clues(game_state *state);
static int newdesc_strip_clues(game_state *state, int *shuffle_1toN);
static char *newdesc_encode_game_description(int n, puzzle_size *grid);

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    int const w = params->w, h = params->h, n = w * h;

    puzzle_size *const grid = snewn(n, puzzle_size);
    int *const shuffle_1toN = snewn(n, int);

    int i, clues_removed;

    char *encoding;

    game_state state;
    state.params = *params;
    state.grid = grid;

    interactive = false; /* I don't need it, I shouldn't use it*/

    for (i = 0; i < n; ++i) shuffle_1toN[i] = i;

    while (true) {
        shuffle(shuffle_1toN, n, sizeof (int), rs);
        newdesc_choose_black_squares(&state, shuffle_1toN);

        newdesc_compute_clues(&state);

        shuffle(shuffle_1toN, n, sizeof (int), rs);
        clues_removed = newdesc_strip_clues(&state, shuffle_1toN);

        if (clues_removed < 0) continue; else break;
    }

    encoding = newdesc_encode_game_description(n, grid);

    sfree(grid);
    sfree(shuffle_1toN);

    return encoding;
}

static int dfs_count_white(game_state *state, int cell);

static void newdesc_choose_black_squares(game_state *state,
                                         const int *shuffle_1toN)
{
    int const w = state->params.w, h = state->params.h, n = w * h;

    int k, any_white_cell, n_black_cells;

    for (k = 0; k < n; ++k) state->grid[k] = WHITE;

    any_white_cell = shuffle_1toN[n - 1];
    n_black_cells = 0;

    /* I like the puzzles that result from n / 3, but maybe this
     * could be made a (generation, i.e. non-full) parameter? */
    for (k = 0; k < n / 3; ++k) {
        int const i = shuffle_1toN[k], c = i % w, r = i / w;

        int j;
        for (j = 0; j < 4; ++j) {
            int const rr = r + dr[j], cc = c + dc[j], cell = idx(rr, cc, w);
            /* if you're out of bounds, we skip you */
            if (out_of_bounds(rr, cc, w, h)) continue;
            if (state->grid[cell] == BLACK) break; /* I can't be black */
        } if (j < 4) continue; /* I have black neighbour: I'm white */

        state->grid[i] = BLACK;
        ++n_black_cells;

        j = dfs_count_white(state, any_white_cell);
        if (j + n_black_cells < n) {
            state->grid[i] = WHITE;
            --n_black_cells;
        }
    }
}

static void newdesc_compute_clues(game_state *state)
{
    int const w = state->params.w, h = state->params.h;
    int r, c;

    for (r = 0; r < h; ++r) {
        int run_size = 0, c, cc;
        for (c = 0; c <= w; ++c) {
            if (c == w || state->grid[idx(r, c, w)] == BLACK) {
                for (cc = c - run_size; cc < c; ++cc)
                    state->grid[idx(r, cc, w)] += run_size;
                run_size = 0;
            } else ++run_size;
        }
    }

    for (c = 0; c < w; ++c) {
        int run_size = 0, r, rr;
        for (r = 0; r <= h; ++r) {
            if (r == h || state->grid[idx(r, c, w)] == BLACK) {
                for (rr = r - run_size; rr < r; ++rr)
                    state->grid[idx(rr, c, w)] += run_size;
                run_size = 0;
            } else ++run_size;
        }
    }
}

#define rotate(x) (n - 1 - (x))

static int newdesc_strip_clues(game_state *state, int *shuffle_1toN)
{
    int const w = state->params.w, n = w * state->params.h;

    move *const move_buffer = snewn(n, move);
    move *buf;
    game_state *dupstate;

    /*
     * do a partition/pivot of shuffle_1toN into three groups:
     * (1) squares rotationally-symmetric to (3)
     * (2) squares not in (1) or (3)
     * (3) black squares
     *
     * They go from [0, left), [left, right) and [right, n) in
     * shuffle_1toN (and from there into state->grid[ ])
     *
     * Then, remove clues from the grid one by one in shuffle_1toN
     * order, until the solver becomes unhappy.  If we didn't remove
     * all of (1), return (-1).  Else, we're happy.
     */

    /* do the partition */
    int clues_removed, k = 0, left = 0, right = n;

    for (;; ++k) {
        while (k < right && state->grid[shuffle_1toN[k]] == BLACK) {
            --right;
            SWAP(int, shuffle_1toN[right], shuffle_1toN[k]);
            assert(state->grid[shuffle_1toN[right]] == BLACK);
        }
        if (k >= right) break;
        assert (k >= left);
        if (state->grid[rotate(shuffle_1toN[k])] == BLACK) {
            SWAP(int, shuffle_1toN[k], shuffle_1toN[left]);
            ++left;
        }
        assert (state->grid[rotate(shuffle_1toN[k])] != BLACK
                || k == left - 1);
    }

    for (k = 0; k < left; ++k) {
        assert (state->grid[rotate(shuffle_1toN[k])] == BLACK);
        state->grid[shuffle_1toN[k]] = EMPTY;
    }
    for (k = left; k < right; ++k) {
        assert (state->grid[rotate(shuffle_1toN[k])] != BLACK);
        assert (state->grid[shuffle_1toN[k]] != BLACK);
    }
    for (k = right; k < n; ++k) {
        assert (state->grid[shuffle_1toN[k]] == BLACK);
        state->grid[shuffle_1toN[k]] = EMPTY;
    }

    clues_removed = (left - 0) + (n - right);

    dupstate = dup_game(state);
    buf = solve_internal(dupstate, move_buffer, DIFF_RECURSION - 1);
    free_game(dupstate);
    if (buf - move_buffer < clues_removed) {
        /* branch prediction: I don't think I'll go here */
        clues_removed = -1;
        goto ret;
    }

    for (k = left; k < right; ++k) {
        const int i = shuffle_1toN[k], j = rotate(i);
        int const clue = state->grid[i], clue_rot = state->grid[j];
        if (clue == BLACK) continue;
        state->grid[i] = state->grid[j] = EMPTY;
        dupstate = dup_game(state);
        buf = solve_internal(dupstate, move_buffer, DIFF_RECURSION - 1);
        free_game(dupstate);
        clues_removed += 2 - (i == j);
        /* if i is the center square, then i == (j = rotate(i))
         * when i and j are one, removing i and j removes only one */
        if (buf - move_buffer == clues_removed) continue;
        /* if the solver is sound, refilling all removed clues means
         * we have filled all squares, i.e. solved the puzzle. */
        state->grid[i] = clue;
        state->grid[j] = clue_rot;
        clues_removed -= 2 - (i == j);
    }
    
ret:
    sfree(move_buffer);
    return clues_removed;
}

static int dfs_count_rec(puzzle_size *grid, int r, int c, int w, int h)
{
    int const cell = idx(r, c, w);
    if (out_of_bounds(r, c, w, h)) return 0;
    if (grid[cell] != WHITE) return 0;
    grid[cell] = EMPTY;
    return 1 +
        dfs_count_rec(grid, r + 0, c + 1, w, h) +
        dfs_count_rec(grid, r + 0, c - 1, w, h) +
        dfs_count_rec(grid, r + 1, c + 0, w, h) +
        dfs_count_rec(grid, r - 1, c + 0, w, h);
}

static int dfs_count_white(game_state *state, int cell)
{
    int const w = state->params.w, h = state->params.h, n = w * h;
    int const r = cell / w, c = cell % w;
    int i, k = dfs_count_rec(state->grid, r, c, w, h);
    for (i = 0; i < n; ++i)
        if (state->grid[i] == EMPTY)
            state->grid[i] = WHITE;
    return k;
}

static const char *validate_params(const game_params *params, bool full)
{
    int const w = params->w, h = params->h;
    if (w < 1) return "Error: width is less than 1";
    if (h < 1) return "Error: height is less than 1";
    if (w > SCHAR_MAX - (h - 1)) return "Error: w + h is too big";
    if (w * h < 1) return "Error: size is less than 1";
    /* I might be unable to store clues in my puzzle_size *grid; */
    if (full) {
        if (w == 2 && h == 2) return "Error: can't create 2x2 puzzles";
        if (w == 1 && h == 2) return "Error: can't create 1x2 puzzles";
        if (w == 2 && h == 1) return "Error: can't create 2x1 puzzles";
        if (w == 1 && h == 1) return "Error: can't create 1x1 puzzles";
    }
    return NULL;
}

/* Definition: a puzzle instance is _good_ if:
 *  - it has a unique solution
 *  - the solver can find this solution without using recursion
 *  - the solution contains at least one black square
 *  - the clues are 2-way rotationally symmetric
 *
 * (the idea being: the generator can not output any _bad_ puzzles)
 *
 * Theorem: validate_params, when full != 0, discards exactly the set
 * of parameters for which there are _no_ good puzzle instances.
 *
 * Proof: it's an immediate consequence of the five lemmas below.
 *
 * Observation: not only do puzzles on non-tiny grids exist, the
 * generator is pretty fast about coming up with them.  On my pre-2004
 * desktop box, it generates 100 puzzles on the highest preset (16x11)
 * in 8.383 seconds, or <= 0.1 second per puzzle.
 *
 * ----------------------------------------------------------------------
 *
 * Lemma: On a 1x1 grid, there are no good puzzles.
 *
 * Proof: the one square can't be a clue because at least one square
 * is black.  But both a white square and a black square satisfy the
 * solution criteria, so the puzzle is ambiguous (and hence bad).
 *
 * Lemma: On a 1x2 grid, there are no good puzzles.
 *
 * Proof: let's name the squares l and r.  Note that there can be at
 * most one black square, or adjacency is violated.  By assumption at
 * least one square is black, so let's call that one l.  By clue
 * symmetry, neither l nor r can be given as a clue, so the puzzle
 * instance is blank and thus ambiguous.
 *
 * Corollary: On a 2x1 grid, there are no good puzzles.
 * Proof: rotate the above proof 90 degrees ;-)
 *
 * ----------------------------------------------------------------------
 *
 * Lemma: On a 2x2 grid, there are no soluble puzzles with 2-way
 * rotational symmetric clues and at least one black square.
 *
 * Proof: Let's name the squares a, b, c, and d, with a and b on the
 * top row, a and c in the left column.  Let's consider the case where
 * a is black.  Then no other square can be black: b and c would both
 * violate the adjacency constraint; d would disconnect b from c.
 *
 * So exactly one square is black (and by 4-way rotation symmetry of
 * the 2x2 square, it doesn't matter which one, so let's stick to a).
 * By 2-way rotational symmetry of the clues and the rule about not
 * painting numbers black, neither a nor d can be clues.  A blank
 * puzzle would be ambiguous, so one of {b, c} is a clue; by symmetry,
 * so is the other one.
 *
 * It is readily seen that their clue value is 2.  But "a is black"
 * and "d is black" are both valid solutions in this case, so the
 * puzzle is ambiguous (and hence bad).
 *
 * ----------------------------------------------------------------------
 *
 * Lemma: On a wxh grid with w, h >= 1 and (w > 2 or h > 2), there is
 * at least one good puzzle.
 *
 * Proof: assume that w > h (otherwise rotate the proof again).  Paint
 * the top left and bottom right corners black, and fill a clue into
 * all the other squares.  Present this board to the solver code (or
 * player, hypothetically), except with the two black squares as blank
 * squares.
 *
 * For an Nx1 puzzle, observe that every clue is N - 2, and there are
 * N - 2 of them in one connected sequence, so the remaining two
 * squares can be deduced to be black, which solves the puzzle.
 *
 * For any other puzzle, let j be a cell in the same row as a black
 * cell, but not in the same column (such a cell doesn't exist in 2x3
 * puzzles, but we assume w > h and such cells exist in 3x2 puzzles).
 *
 * Note that the number of cells in axis parallel `rays' going out
 * from j exceeds j's clue value by one.  Only one such cell is a
 * non-clue, so it must be black.  Similarly for the other corner (let
 * j' be a cell in the same row as the _other_ black cell, but not in
 * the same column as _any_ black cell; repeat this argument at j').
 *
 * This fills the grid and satisfies all clues and the adjacency
 * constraint and doesn't paint on top of any clues.  All that is left
 * to see is connectedness.
 *
 * Observe that the white cells in each column form a single connected
 * `run', and each column contains a white cell adjacent to a white
 * cell in the column to the right, if that column exists.
 *
 * Thus, any cell in the left-most column can reach any other cell:
 * first go to the target column (by repeatedly going to the cell in
 * your current column that lets you go right, then going right), then
 * go up or down to the desired cell.
 *
 * As reachability is symmetric (in undirected graphs) and transitive,
 * any cell can reach any left-column cell, and from there any other
 * cell.
 */

/* ----------------------------------------------------------------------
 * Game encoding and decoding
 */

#define NDIGITS_BASE '!'

static char *newdesc_encode_game_description(int area, puzzle_size *grid)
{
    char *desc = NULL;
    int desclen = 0, descsize = 0;
    int run, i;

    run = 0;
    for (i = 0; i <= area; i++) {
	int n = (i < area ? grid[i] : -1);

	if (!n)
	    run++;
	else {
	    if (descsize < desclen + 40) {
		descsize = desclen * 3 / 2 + 40;
		desc = sresize(desc, descsize, char);
	    }
	    if (run) {
		while (run > 0) {
		    int c = 'a' - 1 + run;
		    if (run > 26)
			c = 'z';
		    desc[desclen++] = c;
		    run -= c - ('a' - 1);
		}
	    } else {
		/*
		 * If there's a number in the very top left or
		 * bottom right, there's no point putting an
		 * unnecessary _ before or after it.
		 */
		if (desclen > 0 && n > 0)
		    desc[desclen++] = '_';
	    }
	    if (n > 0)
		desclen += sprintf(desc+desclen, "%d", n);
	    run = 0;
	}
    }
    desc[desclen] = '\0';
    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int const n = params->w * params->h;
    int squares = 0;
    int range = params->w + params->h - 1;   /* maximum cell value */

    while (*desc && *desc != ',') {
        int c = *desc++;
        if (c >= 'a' && c <= 'z') {
            squares += c - 'a' + 1;
        } else if (c == '_') {
            /* do nothing */;
        } else if (c > '0' && c <= '9') {
            int val = atoi(desc-1);
            if (val < 1 || val > range)
                return "Out-of-range number in game description";
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < n)
        return "Not enough data to fill grid";

    if (squares > n)
        return "Too much data to fit in grid";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int i;
    const char *p;

    int const n = params->w * params->h;
    game_state *state = snew(game_state);

    me = NULL; /* I don't need it, I shouldn't use it */

    state->params = *params; /* structure copy */
    state->grid = snewn(n, puzzle_size);

    p = desc;
    i = 0;
    while (i < n && *p) {
        int c = *p++;
        if (c >= 'a' && c <= 'z') {
            int squares = c - 'a' + 1;
	    while (squares--)
		state->grid[i++] = 0;
        } else if (c == '_') {
            /* do nothing */;
        } else if (c > '0' && c <= '9') {
            int val = atoi(p-1);
            assert(val >= 1 && val <= params->w+params->h-1);
            state->grid[i++] = val;
            while (*p >= '0' && *p <= '9')
                p++;
        }
    }
    assert(i == n);
    state->has_cheated = false;
    state->was_solved = false;

    return state;
}

/* ----------------------------------------------------------------------
 * User interface: ascii
 */

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int r, c, i, w_string, h_string, n_string;
    char cellsize;
    char *ret, *buf, *gridline;

    int const w = state->params.w, h = state->params.h;

    cellsize = 0; /* or may be used uninitialized */

    for (c = 0; c < w; ++c) {
        for (r = 0; r < h; ++r) {
            puzzle_size k = state->grid[idx(r, c, w)];
            int d;
            for (d = 0; k; k /= 10, ++d);
            cellsize = max(cellsize, d);
        }
    }

    ++cellsize;

    w_string = w * cellsize + 2; /* "|%d|%d|...|\n" */
    h_string = 2 * h + 1; /* "+--+--+...+\n%s\n+--+--+...+\n" */
    n_string = w_string * h_string;

    gridline = snewn(w_string + 1, char); /* +1: NUL terminator */
    memset(gridline, '-', w_string);
    for (c = 0; c <= w; ++c) gridline[c * cellsize] = '+';
    gridline[w_string - 1] = '\n';
    gridline[w_string - 0] = '\0';

    buf = ret = snewn(n_string + 1, char); /* +1: NUL terminator */
    for (i = r = 0; r < h; ++r) {
        memcpy(buf, gridline, w_string);
        buf += w_string;
        for (c = 0; c < w; ++c, ++i) {
            char ch;
            switch (state->grid[i]) {
	      case BLACK: ch = '#'; break;
	      case WHITE: ch = '.'; break;
	      case EMPTY: ch = ' '; break;
	      default:
                buf += sprintf(buf, "|%*d", cellsize - 1, state->grid[i]);
                continue;
            }
            *buf++ = '|';
            memset(buf, ch, cellsize - 1);
            buf += cellsize - 1;
        }
        buf += sprintf(buf, "|\n");
    }
    memcpy(buf, gridline, w_string);
    buf += w_string;
    assert (buf - ret == n_string);
    *buf = '\0';

    sfree(gridline);

    return ret;
}

/* ----------------------------------------------------------------------
 * User interfaces: interactive
 */

struct game_ui {
    puzzle_size r, c; /* cursor position */
    bool cursor_show;
};

static game_ui *new_ui(const game_state *state)
{
    struct game_ui *ui = snew(game_ui);
    ui->r = ui->c = 0;
    ui->cursor_show = getenv_bool("PUZZLES_SHOW_CURSOR", false);
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

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int cell;

    if (IS_CURSOR_SELECT(button)) {
        cell = state->grid[idx(ui->r, ui->c, state->params.w)];
        if (!ui->cursor_show || cell > 0) return "";
        switch (cell) {
          case EMPTY:
            return button == CURSOR_SELECT ? "Fill" : "Dot";
          case WHITE:
            return button == CURSOR_SELECT ? "Empty" : "Fill";
          case BLACK:
            return button == CURSOR_SELECT ? "Dot" : "Empty";
        }
    }
    return "";

}

typedef struct drawcell {
    puzzle_size value;
    bool error, cursor, flash;
} drawcell;

struct game_drawstate {
    int tilesize;
    drawcell *grid;
};

#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE / 2)
#define COORD(x) ((x) * TILESIZE + BORDER)
#define FROMCOORD(x) (((x) - BORDER) / TILESIZE)

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    enum {none, forwards, backwards, hint};
    int const w = state->params.w, h = state->params.h;
    int r = ui->r, c = ui->c, action = none, cell;
    bool shift = button & MOD_SHFT;
    button &= ~MOD_SHFT;

    if (IS_CURSOR_SELECT(button) && !ui->cursor_show) return NULL;

    if (IS_MOUSE_DOWN(button)) {
        r = FROMCOORD(y + TILESIZE) - 1; /* or (x, y) < TILESIZE) */
        c = FROMCOORD(x + TILESIZE) - 1; /* are considered inside */
        if (out_of_bounds(r, c, w, h)) return NULL;
        ui->r = r;
        ui->c = c;
        ui->cursor_show = false;
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
	/*
	 * Utterly awful hack, exactly analogous to the one in Slant,
	 * to configure the left and right mouse buttons the opposite
	 * way round.
	 *
	 * The original puzzle submitter thought it would be more
	 * useful to have the left button turn an empty square into a
	 * dotted one, on the grounds that that was what you did most
	 * often; I (SGT) felt instinctively that the left button
	 * ought to place black squares and the right button place
	 * dots, on the grounds that that was consistent with many
	 * other puzzles in which the left button fills in the data
	 * used by the solution checker while the right button places
	 * pencil marks for the user's convenience.
	 *
	 * My first beta-player wasn't sure either, so I thought I'd
	 * pre-emptively put in a 'configuration' mechanism just in
	 * case.
	 */
	{
	    static int swap_buttons = -1;
	    if (swap_buttons < 0)
                swap_buttons = getenv_bool("RANGE_SWAP_BUTTONS", false);
	    if (swap_buttons) {
		if (button == LEFT_BUTTON)
		    button = RIGHT_BUTTON;
		else
		    button = LEFT_BUTTON;
	    }
	}
    }

    switch (button) {
      case CURSOR_SELECT : case   LEFT_BUTTON: action = backwards; break;
      case CURSOR_SELECT2: case  RIGHT_BUTTON: action =  forwards; break;
      case 'h': case 'H' :                     action =      hint; break;
      case CURSOR_UP: case CURSOR_DOWN:
      case CURSOR_LEFT: case CURSOR_RIGHT:
        if (ui->cursor_show) {
            int i;
            for (i = 0; i < 4 && cursors[i] != button; ++i);
            assert (i < 4);
            if (shift) {
                int pre_r = r, pre_c = c;
                bool do_pre, do_post;
                cell = state->grid[idx(r, c, state->params.w)];
                do_pre = (cell == EMPTY);

                if (out_of_bounds(ui->r + dr[i], ui->c + dc[i], w, h)) {
                    if (do_pre)
                        return nfmtstr(40, "W,%d,%d", pre_r, pre_c);
                    else
                        return NULL;
                }

                ui->r += dr[i];
                ui->c += dc[i];

                cell = state->grid[idx(ui->r, ui->c, state->params.w)];
                do_post = (cell == EMPTY);

                /* (do_pre ? "..." : "") concat (do_post ? "..." : "") */
                if (do_pre && do_post)
                    return nfmtstr(80, "W,%d,%dW,%d,%d",
                                   pre_r, pre_c, ui->r, ui->c);
                else if (do_pre)
                    return nfmtstr(40, "W,%d,%d", pre_r, pre_c);
                else if (do_post)
                    return nfmtstr(40, "W,%d,%d", ui->r, ui->c);
                else
                    return UI_UPDATE;

            } else if (!out_of_bounds(ui->r + dr[i], ui->c + dc[i], w, h)) {
                ui->r += dr[i];
                ui->c += dc[i];
            }
        } else ui->cursor_show = true;
        return UI_UPDATE;
    }

    if (action == hint) {
        move *end, *buf = snewn(state->params.w * state->params.h,
                                struct move);
        char *ret = NULL;
        end = solve_internal(state, buf, DIFF_RECURSION);
        if (end != NULL && end > buf) {
            ret = nfmtstr(40, "%c,%d,%d",
                          buf->colour == M_BLACK ? 'B' : 'W',
                          buf->square.r, buf->square.c);
            /* We used to set a flag here in the game_ui indicating
             * that the player had used the hint function. I (SGT)
             * retired it, on grounds of consistency with other games
             * (most of these games will still flash to indicate
             * completion if you solved and undid it, so why not if
             * you got a hint?) and because the flash is as much about
             * checking you got it all right than about congratulating
             * you on a job well done. */
        }
        sfree(buf);
        return ret;
    }

    cell = state->grid[idx(r, c, state->params.w)];
    if (cell > 0) return NULL;

    if (action == forwards) switch (cell) {
      case EMPTY: return nfmtstr(40, "W,%d,%d", r, c);
      case WHITE: return nfmtstr(40, "B,%d,%d", r, c);
      case BLACK: return nfmtstr(40, "E,%d,%d", r, c);
    }

    else if (action == backwards) switch (cell) {
      case BLACK: return nfmtstr(40, "W,%d,%d", r, c);
      case WHITE: return nfmtstr(40, "E,%d,%d", r, c);
      case EMPTY: return nfmtstr(40, "B,%d,%d", r, c);
    }

    return NULL;
}

static bool find_errors(const game_state *state, bool *report)
{
    int const w = state->params.w, h = state->params.h, n = w * h;
    int *dsf;

    int r, c, i;

    int nblack = 0, any_white_cell = -1;
    game_state *dup = dup_game(state);

    for (i = r = 0; r < h; ++r)
        for (c = 0; c < w; ++c, ++i) {
            switch (state->grid[i]) {

	      case BLACK:
		{
		    int j;
		    ++nblack;
		    for (j = 0; j < 4; ++j) {
			int const rr = r + dr[j], cc = c + dc[j];
			if (out_of_bounds(rr, cc, w, h)) continue;
			if (state->grid[idx(rr, cc, w)] != BLACK) continue;
			if (!report) goto found_error;
			report[i] = true;
			break;
		    }
		}
                break;
	      default:
		{
		    int j, runs;
		    for (runs = 1, j = 0; j < 4; ++j) {
			int const rr = r + dr[j], cc = c + dc[j];
			runs += runlength(rr, cc, dr[j], dc[j], state,
					  ~MASK(BLACK));
		    }
		    if (!report) {
			if (runs != state->grid[i]) goto found_error;
		    } else if (runs < state->grid[i]) report[i] = true;
		    else {
			for (runs = 1, j = 0; j < 4; ++j) {
			    int const rr = r + dr[j], cc = c + dc[j];
			    runs += runlength(rr, cc, dr[j], dc[j], state,
					      ~(MASK(BLACK) | MASK(EMPTY)));
			}
			if (runs > state->grid[i]) report[i] = true;
		    }
		}

                /* note: fallthrough _into_ these cases */
	      case EMPTY:
	      case WHITE: any_white_cell = i;
            }
        }

    /*
     * Check that all the white cells form a single connected component.
     */
    dsf = snew_dsf(n);
    for (r = 0; r < h-1; ++r)
        for (c = 0; c < w; ++c)
            if (state->grid[r*w+c] != BLACK &&
                state->grid[(r+1)*w+c] != BLACK)
                dsf_merge(dsf, r*w+c, (r+1)*w+c);
    for (r = 0; r < h; ++r)
        for (c = 0; c < w-1; ++c)
            if (state->grid[r*w+c] != BLACK &&
                state->grid[r*w+(c+1)] != BLACK)
                dsf_merge(dsf, r*w+c, r*w+(c+1));
    if (any_white_cell != -1 &&
        nblack + dsf_size(dsf, any_white_cell) < n) {
        int biggest, canonical;

        if (!report) {
            sfree(dsf);
            goto found_error;
        }

        /*
         * Report this error by choosing one component to be the
         * canonical one (we pick the largest, arbitrarily
         * tie-breaking towards lower array indices) and highlighting
         * as an error any square in a different component.
         */
        canonical = -1;
        biggest = 0;
        for (i = 0; i < n; ++i)
            if (state->grid[i] != BLACK) {
                int size = dsf_size(dsf, i);
                if (size > biggest) {
                    biggest = size;
                    canonical = dsf_canonify(dsf, i);
                }
            }

        for (i = 0; i < n; ++i)
            if (state->grid[i] != BLACK && dsf_canonify(dsf, i) != canonical)
                report[i] = true;
    }
    sfree(dsf);

    free_game(dup);
    return false; /* if report != NULL, this is ignored */

found_error:
    free_game(dup);
    return true;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    signed int r, c, value, nchars, ntok;
    signed char what_to_do;
    game_state *ret;

    assert (move);

    ret = dup_game(state);

    if (*move == 'S') {
        ++move;
        ret->has_cheated = ret->was_solved = true;
    }

    for (; *move; move += nchars) {
        ntok = sscanf(move, "%c,%d,%d%n", &what_to_do, &r, &c, &nchars);
        if (ntok < 3) goto failure;
        switch (what_to_do) {
	  case 'W': value = WHITE; break;
	  case 'E': value = EMPTY; break;
	  case 'B': value = BLACK; break;
	  default: goto failure;
        }
        if (out_of_bounds(r, c, ret->params.w, ret->params.h)) goto failure;
        ret->grid[idx(r, c, ret->params.w)] = value;
    }

    if (!ret->was_solved)
        ret->was_solved = !find_errors(ret, NULL);

    return ret;

failure:
    free_game(ret);
    return NULL;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

#define FLASH_TIME 0.7F

static float game_flash_length(const game_state *from,
                               const game_state *to, int dir, game_ui *ui)
{
    if (!from->was_solved && to->was_solved && !to->has_cheated)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cursor_show) {
        *x = BORDER + TILESIZE * ui->c;
        *y = BORDER + TILESIZE * ui->r;
        *w = *h = TILESIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->was_solved ? +1 : 0;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define PREFERRED_TILE_SIZE 32

enum {
    COL_BACKGROUND = 0,
    COL_GRID,
    COL_BLACK = COL_GRID,
    COL_TEXT = COL_GRID,
    COL_USER = COL_GRID,
    COL_ERROR,
    COL_LOWLIGHT,
    COL_CURSOR = COL_LOWLIGHT,
    NCOLOURS
};

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = (1 + params->w) * tilesize;
    *y = (1 + params->h) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

#define COLOUR(ret, i, r, g, b) \
   ((ret[3*(i)+0] = (r)), (ret[3*(i)+1] = (g)), (ret[3*(i)+2] = (b)))

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_BACKGROUND, -1, COL_LOWLIGHT);
    COLOUR(ret, COL_GRID,  0.0F, 0.0F, 0.0F);
    COLOUR(ret, COL_ERROR, 1.0F, 0.0F, 0.0F);

    *ncolours = NCOLOURS;
    return ret;
}

static drawcell makecell(puzzle_size value,
                         bool error, bool cursor, bool flash)
{
    drawcell ret;
    setmember(ret, value);
    setmember(ret, error);
    setmember(ret, cursor);
    setmember(ret, flash);
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int const w = state->params.w, h = state->params.h, n = w * h;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;

    ds->grid = snewn(n, drawcell);
    for (i = 0; i < n; ++i)
        ds->grid[i] = makecell(w + h, false, false, false);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

#define cmpmember(a, b, field) ((a) . field == (b) . field)

static bool cell_eq(drawcell a, drawcell b)
{
    return
        cmpmember(a, b, value) &&
        cmpmember(a, b, error) &&
        cmpmember(a, b, cursor) &&
        cmpmember(a, b, flash);
}

static void draw_cell(drawing *dr, game_drawstate *ds, int r, int c,
                      drawcell cell);

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int const w = state->params.w, h = state->params.h, n = w * h;
    int const flash = ((int) (flashtime * 5 / FLASH_TIME)) % 2;

    int r, c, i;

    bool *errors = snewn(n, bool);
    memset(errors, 0, n * sizeof (bool));
    find_errors(state, errors);

    assert (oldstate == NULL); /* only happens if animating moves */

    for (i = r = 0; r < h; ++r) {
        for (c = 0; c < w; ++c, ++i) {
            drawcell cell = makecell(state->grid[i], errors[i], false, flash);
            if (r == ui->r && c == ui->c && ui->cursor_show)
                cell.cursor = true;
            if (!cell_eq(cell, ds->grid[i])) {
                draw_cell(dr, ds, r, c, cell);
                ds->grid[i] = cell;
            }
        }
    }

    sfree(errors);
}

static void draw_cell(drawing *draw, game_drawstate *ds, int r, int c,
                      drawcell cell)
{
    int const ts = ds->tilesize;
    int const y = BORDER + ts * r, x = BORDER + ts * c;
    int const tx = x + (ts / 2), ty = y + (ts / 2);
    int const dotsz = (ds->tilesize + 9) / 10;

    int const colour = (cell.value == BLACK ?
                        cell.error ? COL_ERROR : COL_BLACK :
                        cell.flash || cell.cursor ?
                        COL_LOWLIGHT : COL_BACKGROUND);

    draw_rect_outline(draw, x,     y,     ts + 1, ts + 1, COL_GRID);
    draw_rect        (draw, x + 1, y + 1, ts - 1, ts - 1, colour);
    if (cell.error)
	draw_rect_outline(draw, x + 1, y + 1, ts - 1, ts - 1, COL_ERROR);

    switch (cell.value) {
      case WHITE: draw_rect(draw, tx - dotsz / 2, ty - dotsz / 2, dotsz, dotsz,
			    cell.error ? COL_ERROR : COL_USER);
      case BLACK: case EMPTY: break;
      default:
	{
	    int const colour = (cell.error ? COL_ERROR : COL_GRID);
	    char *msg = nfmtstr(10, "%d", cell.value);
	    draw_text(draw, tx, ty, FONT_VARIABLE, ts * 3 / 5,
		      ALIGN_VCENTRE | ALIGN_HCENTRE, colour, msg);
	    sfree(msg);
	}
    }

    draw_update(draw, x, y, ts + 1, ts + 1);
}

/* ----------------------------------------------------------------------
 * User interface: print
 */

static void game_print_size(const game_params *params, float *x, float *y)
{
    int print_width, print_height;
    game_compute_size(params, 800, &print_width, &print_height);
    *x = print_width  / 100.0F;
    *y = print_height / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int const w = state->params.w, h = state->params.h;
    game_drawstate ds_obj, *ds = &ds_obj;
    int r, c, i, colour;

    ds->tilesize = tilesize;

    colour = print_mono_colour(dr, 1); assert(colour == COL_BACKGROUND);
    colour = print_mono_colour(dr, 0); assert(colour == COL_GRID);
    colour = print_mono_colour(dr, 1); assert(colour == COL_ERROR);
    colour = print_mono_colour(dr, 0); assert(colour == COL_LOWLIGHT);
    colour = print_mono_colour(dr, 0); assert(colour == NCOLOURS);

    for (i = r = 0; r < h; ++r)
        for (c = 0; c < w; ++c, ++i)
            draw_cell(dr, ds, r, c,
                      makecell(state->grid[i], false, false, false));

    print_line_width(dr, 3 * tilesize / 40);
    draw_rect_outline(dr, BORDER, BORDER, w*TILESIZE, h*TILESIZE, COL_GRID);
}

/* And that's about it ;-) **************************************************/

#ifdef COMBINED
#define thegame range
#endif

struct game const thegame = {
    "Range", "games.range", "range",
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
    false, /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0, /* flags */
};
