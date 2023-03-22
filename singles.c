/*
 * singles.c: implementation of Hitori ('let me alone') from Nikoli.
 *
 * Make single-get able to fetch a specific puzzle ID from menneske.no?
 *
 * www.menneske.no solving methods:
 *
 * Done:
 * SC: if you circle a cell, any cells in same row/col with same no --> black
 *  -- solver_op_circle
 * SB: if you make a cell black, any cells around it --> white
 *  -- solver_op_blacken
 * ST: 3 identical cells in row, centre is white and outer two black.
 * SP: 2 identical cells with single-cell gap, middle cell is white.
 *  -- solver_singlesep (both ST and SP)
 * PI: if you have a pair of same number in row/col, any other
 *      cells of same number must be black.
 *  -- solve_doubles
 * CC: if you have a black on edge one cell away from corner, cell
 *       on edge diag. adjacent must be white.
 * CE: if you have 2 black cells of triangle on edge, third cell must
 *      be white.
 * QM: if you have 3 black cells of diagonal square in middle, fourth
 *      cell must be white.
 *  -- solve_allblackbutone (CC, CE, and QM).
 * QC: a corner with 4 identical numbers (or 2 and 2) must have the
 *      corner cell (and cell diagonal to that) black.
 * TC: a corner with 3 identical numbers (with the L either way)
 *      must have the apex of L black, and other two white.
 * DC: a corner with 2 identical numbers in domino can set a white
 *      cell along wall.
 *  -- solve_corners (QC, TC, DC)
 * IP: pair with one-offset-pair force whites by offset pair
 *  -- solve_offsetpair
 * MC: any cells diag. adjacent to black cells that would split board
 *      into separate white regions must be white.
 *  -- solve_removesplits
 *
 * Still to do:
 *
 * TEP: 3 pairs of dominos parallel to side, can mark 4 white cells
 *       alongside.
 * DEP: 2 pairs of dominos parallel to side, can mark 2 white cells.
 * FI: if you have two sets of double-cells packed together, singles
 *      in that row/col must be white (qv. PI)
 * QuM: four identical cells (or 2 and 2) in middle of grid only have
 *       two possible solutions each.
 * FDE: doubles one row/column away from edge can force a white cell.
 * FDM: doubles in centre (next to bits of diag. square) can force a white cell.
 * MP: two pairs with same number between force number to black.
 * CnC: if circling a cell leads to impossible board, cell is black.
 * MC: if we have two possiblilities, can we force a white circle?
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

#ifdef STANDALONE_SOLVER
static bool verbose = false;
#endif

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define BORDER    (TILE_SIZE / 2)

#define CRAD      ((TILE_SIZE / 2) - 1)
#define TEXTSZ    ((14*CRAD/10) - 1) /* 2 * sqrt(2) of CRAD */

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define INGRID(s,x,y) ((x) >= 0 && (x) < (s)->w && (y) >= 0 && (y) < (s)->h)

#define FLASH_TIME 0.7F

enum {
    COL_BACKGROUND, COL_UNUSED1, COL_LOWLIGHT,
    COL_BLACK, COL_WHITE, COL_BLACKNUM, COL_GRID,
    COL_CURSOR, COL_ERROR,
    NCOLOURS
};

struct game_params {
    int w, h, diff;
};

#define F_BLACK         0x1
#define F_CIRCLE        0x2
#define F_ERROR         0x4
#define F_SCRATCH       0x8

struct game_state {
    int w, h, n, o;             /* n = w*h; o = max(w, h) */
    bool completed, used_solve, impossible;
    int *nums;                  /* size w*h */
    unsigned int *flags;        /* size w*h */
};

/* top, right, bottom, left */
static const int dxs[4] = { 0, 1, 0, -1 };
static const int dys[4] = { -1, 0, 1, 0 };

/* --- Game parameters and preset functions --- */

#define DIFFLIST(A)             \
    A(EASY,Easy,e)              \
    A(TRICKY,Tricky,k)

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title

enum { DIFFLIST(ENUM) DIFF_MAX, DIFF_ANY };
static char const *const singles_diffnames[] = { DIFFLIST(TITLE) };
static char const singles_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCOUNT lenof(singles_diffchars)
#define DIFFCONFIG DIFFLIST(CONFIG)

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    ret->w = ret->h = 5;
    ret->diff = DIFF_EASY;

    return ret;
}

static const struct game_params singles_presets[] = {
  {  5,  5, DIFF_EASY },
  {  5,  5, DIFF_TRICKY },
  {  6,  6, DIFF_EASY },
  {  6,  6, DIFF_TRICKY },
  {  8,  8, DIFF_EASY },
  {  8,  8, DIFF_TRICKY },
  { 10, 10, DIFF_EASY },
  { 10, 10, DIFF_TRICKY },
  { 12, 12, DIFF_EASY },
  { 12, 12, DIFF_TRICKY }
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(singles_presets))
        return false;

    ret = default_params();
    *ret = singles_presets[i];
    *params = ret;

    sprintf(buf, "%dx%d %s", ret->w, ret->h, singles_diffnames[ret->diff]);
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
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    char const *p = string;
    int i;

    ret->w = ret->h = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        ret->h = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
    }
    if (*p == 'd') {
        ret->diff = DIFF_MAX; /* which is invalid */
        p++;
        for (i = 0; i < DIFFCOUNT; i++) {
            if (*p == singles_diffchars[i])
                ret->diff = i;
        }
        p++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    if (full)
        sprintf(data, "%dx%dd%c", params->w, params->h, singles_diffchars[params->diff]);
    else
        sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = DIFFCONFIG;
    ret[2].u.choices.selected = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->diff = cfg[2].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 || params->h < 2)
	return "Width and neight must be at least two";
    if (params->w > 10+26+26 || params->h > 10+26+26)
        return "Puzzle is too large";
    if (full) {
        if (params->diff < 0 || params->diff >= DIFF_MAX)
            return "Unknown difficulty rating";
    }

    return NULL;
}

/* --- Game description string generation and unpicking --- */

static game_state *blank_game(int w, int h)
{
    game_state *state = snew(game_state);

    memset(state, 0, sizeof(game_state));
    state->w = w;
    state->h = h;
    state->n = w*h;
    state->o = max(w,h);

    state->completed = false;
    state->used_solve = false;
    state->impossible = false;

    state->nums  = snewn(state->n, int);
    state->flags = snewn(state->n, unsigned int);

    memset(state->nums, 0, state->n*sizeof(int));
    memset(state->flags, 0, state->n*sizeof(unsigned int));

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = blank_game(state->w, state->h);

    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ret->impossible = state->impossible;

    memcpy(ret->nums, state->nums, state->n*sizeof(int));
    memcpy(ret->flags, state->flags, state->n*sizeof(unsigned int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->nums);
    sfree(state->flags);
    sfree(state);
}

static char n2c(int num) {
    if (num < 10)
        return '0' + num;
    else if (num < 10+26)
        return 'a' + num - 10;
    else
        return 'A' + num - 10 - 26;
    return '?';
}

static int c2n(char c) {
    if (isdigit((unsigned char)c))
        return (int)(c - '0');
    else if (c >= 'a' && c <= 'z')
        return (int)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'Z')
        return (int)(c - 'A' + 10 + 26);
    return -1;
}

static void unpick_desc(const game_params *params, const char *desc,
                        game_state **sout, const char **mout)
{
    game_state *state = blank_game(params->w, params->h);
    const char *msg = NULL;
    int num = 0, i = 0;

    if (strlen(desc) != state->n) {
        msg = "Game description is wrong length";
        goto done;
    }
    for (i = 0; i < state->n; i++) {
        num = c2n(desc[i]);
        if (num <= 0 || num > state->o) {
            msg = "Game description contains unexpected characters";
            goto done;
        }
        state->nums[i] = num;
    }
done:
    if (msg) { /* sth went wrong. */
        if (mout) *mout = msg;
        free_game(state);
    } else {
        if (mout) *mout = NULL;
        if (sout) *sout = state;
        else free_game(state);
    }
}

static char *generate_desc(game_state *state, bool issolve)
{
    char *ret = snewn(state->n+1+(issolve?1:0), char);
    int i, p=0;

    if (issolve)
        ret[p++] = 'S';
    for (i = 0; i < state->n; i++)
        ret[p++] = n2c(state->nums[i]);
    ret[p] = '\0';
    return ret;
}

/* --- Useful game functions (completion, etc.) --- */

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int len, x, y, i;
    char *ret, *p;

    len = (state->w)*2;       /* one row ... */
    len = len * (state->h*2); /* ... h rows, including gaps ... */
    len += 1;              /* ... final NL */
    p = ret = snewn(len, char);

    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            i = y*state->w + x;
            if (x > 0) *p++ = ' ';
            *p++ = (state->flags[i] & F_BLACK) ? '*' : n2c(state->nums[i]);
        }
        *p++ = '\n';
        for (x = 0; x < state->w; x++) {
            i = y*state->w + x;
            if (x > 0) *p++ = ' ';
            *p++ = (state->flags[i] & F_CIRCLE) ? '~' : ' ';
        }
        *p++ = '\n';
    }
    *p++ = '\0';
    assert(p - ret == len);

    return ret;
}

static void debug_state(const char *desc, game_state *state) {
    char *dbg = game_text_format(state);
    debug(("%s:\n%s", desc, dbg));
    sfree(dbg);
}

static void connect_if_same(game_state *state, int *dsf, int i1, int i2)
{
    int c1, c2;

    if ((state->flags[i1] & F_BLACK) != (state->flags[i2] & F_BLACK))
        return;

    c1 = dsf_canonify(dsf, i1);
    c2 = dsf_canonify(dsf, i2);
    dsf_merge(dsf, c1, c2);
}

static void connect_dsf(game_state *state, int *dsf)
{
    int x, y, i;

    /* Construct a dsf array for connected blocks; connections
     * tracked to right and down. */
    dsf_init(dsf, state->n);
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*state->w + x;

            if (x < state->w-1)
                connect_if_same(state, dsf, i, i+1); /* right */
            if (y < state->h-1)
                connect_if_same(state, dsf, i, i+state->w); /* down */
        }
    }
}

#define CC_MARK_ERRORS  1
#define CC_MUST_FILL    2

static int check_rowcol(game_state *state, int starti, int di, int sz, unsigned flags)
{
    int nerr = 0, n, m, i, j;

    /* if any circled numbers have identical non-circled numbers on
     *     same row/column, error (non-circled)
     * if any circled numbers in same column are same number, highlight them.
     * if any rows/columns have >1 of same number, not complete. */

    for (n = 0, i = starti; n < sz; n++, i += di) {
        if (state->flags[i] & F_BLACK) continue;
        for (m = n+1, j = i+di; m < sz; m++, j += di) {
            if (state->flags[j] & F_BLACK) continue;
            if (state->nums[i] != state->nums[j]) continue;

            nerr++; /* ok, we have two numbers the same in a row. */
            if (!(flags & CC_MARK_ERRORS)) continue;

            /* If we have two circles in the same row around
             * two identical numbers, they are _both_ wrong. */
            if ((state->flags[i] & F_CIRCLE) &&
                (state->flags[j] & F_CIRCLE)) {
                state->flags[i] |= F_ERROR;
                state->flags[j] |= F_ERROR;
            }
            /* Otherwise, if we have a circle, any other identical
             * numbers in that row are obviously wrong. We don't
             * highlight this, however, since it makes the process
             * of solving the puzzle too easy (you circle a number
             * and it promptly tells you which numbers to blacken! */
#if 0
            else if (state->flags[i] & F_CIRCLE)
                state->flags[j] |= F_ERROR;
            else if (state->flags[j] & F_CIRCLE)
                state->flags[i] |= F_ERROR;
#endif
        }
    }
    return nerr;
}

static bool check_complete(game_state *state, unsigned flags)
{
    int *dsf = snewn(state->n, int);
    int x, y, i, error = 0, nwhite, w = state->w, h = state->h;

    if (flags & CC_MARK_ERRORS) {
        for (i = 0; i < state->n; i++)
            state->flags[i] &= ~F_ERROR;
    }
    connect_dsf(state, dsf);

    /* If we're the solver we need the grid all to be definitively
     * black or definitively white (i.e. circled) otherwise the solver
     * has found an ambiguous grid. */
    if (flags & CC_MUST_FILL) {
        for (i = 0; i < state->n; i++) {
            if (!(state->flags[i] & F_BLACK) && !(state->flags[i] & F_CIRCLE))
                error += 1;
        }
    }

    /* Mark any black squares in groups of >1 as errors.
     * Count number of white squares. */
    nwhite = 0;
    for (i = 0; i < state->n; i++) {
        if (state->flags[i] & F_BLACK) {
            if (dsf_size(dsf, i) > 1) {
                error += 1;
                if (flags & CC_MARK_ERRORS)
                    state->flags[i] |= F_ERROR;
            }
        } else
            nwhite += 1;
    }

    /* Check attributes of white squares, row- and column-wise. */
    for (x = 0; x < w; x++) /* check cols from (x,0) */
        error += check_rowcol(state, x,   w, h, flags);
    for (y = 0; y < h; y++) /* check rows from (0,y) */
        error += check_rowcol(state, y*w, 1, w, flags);

    /* If there's more than one white region, pick the largest one to
     * be the canonical one (arbitrarily tie-breaking towards lower
     * array indices), and mark all the others as erroneous. */
    {
        int largest = 0, canonical = -1;
        for (i = 0; i < state->n; i++)
            if (!(state->flags[i] & F_BLACK)) {
                int size = dsf_size(dsf, i);
                if (largest < size) {
                    largest = size;
                    canonical = i;
                }
            }

        if (largest < nwhite) {
            for (i = 0; i < state->n; i++)
                if (!(state->flags[i] & F_BLACK) &&
                    dsf_canonify(dsf, i) != canonical) {
                    error += 1;
                    if (flags & CC_MARK_ERRORS)
                        state->flags[i] |= F_ERROR;
                }
        }
    }

    sfree(dsf);
    return !(error > 0);
}

static char *game_state_diff(const game_state *src, const game_state *dst,
                             bool issolve)
{
    char *ret = NULL, buf[80], c;
    int retlen = 0, x, y, i, k;
    unsigned int fmask = F_BLACK | F_CIRCLE;

    assert(src->n == dst->n);

    if (issolve) {
        ret = sresize(ret, 3, char);
        ret[0] = 'S'; ret[1] = ';'; ret[2] = '\0';
        retlen += 2;
    }

    for (x = 0; x < dst->w; x++) {
        for (y = 0; y < dst->h; y++) {
            i = y*dst->w + x;
            if ((src->flags[i] & fmask) != (dst->flags[i] & fmask)) {
                assert((dst->flags[i] & fmask) != fmask);
                if (dst->flags[i] & F_BLACK)
                    c = 'B';
                else if (dst->flags[i] & F_CIRCLE)
                    c = 'C';
                else
                    c = 'E';
                k = sprintf(buf, "%c%d,%d;", (int)c, x, y);
                ret = sresize(ret, retlen + k + 1, char);
                strcpy(ret + retlen, buf);
                retlen += k;
            }
        }
    }
    return ret;
}

/* --- Solver --- */

enum { BLACK, CIRCLE };

struct solver_op {
    int x, y, op; /* op one of BLACK or CIRCLE. */
    const char *desc; /* must be non-malloced. */
};

struct solver_state {
    struct solver_op *ops;
    int n_ops, n_alloc;
    int *scratch;
};

static struct solver_state *solver_state_new(game_state *state)
{
    struct solver_state *ss = snew(struct solver_state);

    ss->ops = NULL;
    ss->n_ops = ss->n_alloc = 0;
    ss->scratch = snewn(state->n, int);

    return ss;
}

static void solver_state_free(struct solver_state *ss)
{
    sfree(ss->scratch);
    if (ss->ops) sfree(ss->ops);
    sfree(ss);
}

static void solver_op_add(struct solver_state *ss, int x, int y, int op, const char *desc)
{
    struct solver_op *sop;

    if (ss->n_alloc < ss->n_ops + 1) {
        ss->n_alloc = (ss->n_alloc + 1) * 2;
        ss->ops = sresize(ss->ops, ss->n_alloc, struct solver_op);
    }
    sop = &(ss->ops[ss->n_ops++]);
    sop->x = x; sop->y = y; sop->op = op; sop->desc = desc;
    debug(("added solver op %s ('%s') at (%d,%d)\n",
           op == BLACK ? "BLACK" : "CIRCLE", desc, x, y));
}

static void solver_op_circle(game_state *state, struct solver_state *ss,
                             int x, int y)
{
    int i = y*state->w + x;

    if (!INGRID(state, x, y)) return;
    if (state->flags[i] & F_BLACK) {
        debug(("... solver wants to add auto-circle on black (%d,%d)\n", x, y));
        state->impossible = true;
        return;
    }
    /* Only add circle op if it's not already circled. */
    if (!(state->flags[i] & F_CIRCLE)) {
        solver_op_add(ss, x, y, CIRCLE, "SB - adjacent to black square");
    }
}

static void solver_op_blacken(game_state *state, struct solver_state *ss,
                              int x, int y, int num)
{
    int i = y*state->w + x;

    if (!INGRID(state, x, y)) return;
    if (state->nums[i] != num) return;
    if (state->flags[i] & F_CIRCLE) {
        debug(("... solver wants to add auto-black on circled(%d,%d)\n", x, y));
        state->impossible = true;
        return;
    }
    /* Only add black op if it's not already black. */
    if (!(state->flags[i] & F_BLACK)) {
        solver_op_add(ss, x, y, BLACK, "SC - number on same row/col as circled");
    }
}

static int solver_ops_do(game_state *state, struct solver_state *ss)
{
    int next_op = 0, i, x, y, n_ops = 0;
    struct solver_op op;

    /* Care here: solver_op_* may call solver_op_add which may extend the
     * ss->n_ops. */

    while (next_op < ss->n_ops) {
        op = ss->ops[next_op++]; /* copy this away, it may get reallocated. */
        i = op.y*state->w + op.x;

        if (op.op == BLACK) {
            if (state->flags[i] & F_CIRCLE) {
                debug(("Solver wants to blacken circled square (%d,%d)!\n", op.x, op.y));
                state->impossible = true;
                return n_ops;
            }
            if (!(state->flags[i] & F_BLACK)) {
                debug(("... solver adding black at (%d,%d): %s\n", op.x, op.y, op.desc));
#ifdef STANDALONE_SOLVER
                if (verbose)
                    printf("Adding black at (%d,%d): %s\n", op.x, op.y, op.desc);
#endif
                state->flags[i] |= F_BLACK;
                /*debug_state("State after adding black", state);*/
                n_ops++;
                solver_op_circle(state, ss, op.x-1, op.y);
                solver_op_circle(state, ss, op.x+1, op.y);
                solver_op_circle(state, ss, op.x,   op.y-1);
                solver_op_circle(state, ss, op.x,   op.y+1);
                }
        } else {
            if (state->flags[i] & F_BLACK) {
                debug(("Solver wants to circle blackened square (%d,%d)!\n", op.x, op.y));
                state->impossible = true;
                return n_ops;
            }
            if (!(state->flags[i] & F_CIRCLE)) {
                debug(("... solver adding circle at (%d,%d): %s\n", op.x, op.y, op.desc));
#ifdef STANDALONE_SOLVER
                if (verbose)
                    printf("Adding circle at (%d,%d): %s\n", op.x, op.y, op.desc);
#endif
                state->flags[i] |= F_CIRCLE;
                /*debug_state("State after adding circle", state);*/
                n_ops++;
                for (x = 0; x < state->w; x++) {
                    if (x != op.x)
                        solver_op_blacken(state, ss, x, op.y, state->nums[i]);
                }
                for (y = 0; y < state->h; y++) {
                    if (y != op.y)
                        solver_op_blacken(state, ss, op.x, y, state->nums[i]);
                }
            }
        }
    }
    ss->n_ops = 0;
    return n_ops;
}

/* If the grid has two identical numbers with one cell between them, the inner
 * cell _must_ be white (and thus circled); (at least) one of the two must be
 * black (since they're in the same column or row) and thus the middle cell is
 * next to a black cell. */
static int solve_singlesep(game_state *state, struct solver_state *ss)
{
    int x, y, i, ir, irr, id, idd, n_ops = ss->n_ops;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*state->w + x;

            /* Cell two to our right? */
            ir = i + 1; irr = ir + 1;
            if (x < (state->w-2) &&
                state->nums[i] == state->nums[irr] &&
                !(state->flags[ir] & F_CIRCLE)) {
                solver_op_add(ss, x+1, y, CIRCLE, "SP/ST - between identical nums");
            }
            /* Cell two below us? */
            id = i + state->w; idd = id + state->w;
            if (y < (state->h-2) &&
                state->nums[i] == state->nums[idd] &&
                !(state->flags[id] & F_CIRCLE)) {
                solver_op_add(ss, x, y+1, CIRCLE, "SP/ST - between identical nums");
            }
        }
    }
    return ss->n_ops - n_ops;
}

/* If we have two identical numbers next to each other (in a row or column),
 * any other identical numbers in that column must be black. */
static int solve_doubles(game_state *state, struct solver_state *ss)
{
    int x, y, i, ii, n_ops = ss->n_ops, xy;

    for (y = 0, i = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++, i++) {
            assert(i == y*state->w+x);
            if (state->flags[i] & F_BLACK) continue;

            ii = i+1; /* check cell to our right. */
            if (x < (state->w-1) &&
                !(state->flags[ii] & F_BLACK) &&
                state->nums[i] == state->nums[ii]) {
                for (xy = 0; xy < state->w; xy++) {
                    if (xy == x || xy == (x+1)) continue;
                    if (state->nums[y*state->w + xy] == state->nums[i] &&
                        !(state->flags[y*state->w + xy] & F_BLACK))
                        solver_op_add(ss, xy, y, BLACK, "PI - same row as pair");
                }
            }

            ii = i+state->w; /* check cell below us */
            if (y < (state->h-1) &&
                !(state->flags[ii] & F_BLACK) &&
                state->nums[i] == state->nums[ii]) {
                for (xy = 0; xy < state->h; xy++) {
                    if (xy == y || xy == (y+1)) continue;
                    if (state->nums[xy*state->w + x] == state->nums[i] &&
                        !(state->flags[xy*state->w + x] & F_BLACK))
                        solver_op_add(ss, x, xy, BLACK, "PI - same col as pair");
                }
            }
        }
    }
    return ss->n_ops - n_ops;
}

/* If a white square has all-but-one possible adjacent squares black, the
 * one square left over must be white. */
static int solve_allblackbutone(game_state *state, struct solver_state *ss)
{
    int x, y, i, n_ops = ss->n_ops, xd, yd, id, ifree;
    int dis[4], d;

    dis[0] = -state->w;
    dis[1] = 1;
    dis[2] = state->w;
    dis[3] = -1;

    for (y = 0, i = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++, i++) {
            assert(i == y*state->w+x);
            if (state->flags[i] & F_BLACK) continue;

            ifree = -1;
            for (d = 0; d < 4; d++) {
                xd = x + dxs[d]; yd = y + dys[d]; id = i + dis[d];
                if (!INGRID(state, xd, yd)) continue;

                if (state->flags[id] & F_CIRCLE)
                    goto skip; /* this cell already has a way out */
                if (!(state->flags[id] & F_BLACK)) {
                    if (ifree != -1)
                        goto skip; /* this cell has >1 white cell around it. */
                    ifree = id;
                }
            }
            if (ifree != -1)
                solver_op_add(ss, ifree%state->w, ifree/state->w, CIRCLE,
                              "CC/CE/QM: white cell with single non-black around it");
            else {
                debug(("White cell with no escape at (%d,%d)\n", x, y));
                state->impossible = true;
                return 0;
            }
skip: ;
        }
    }
    return ss->n_ops - n_ops;
}

/* If we have 4 numbers the same in a 2x2 corner, the far corner and the
 * diagonally-adjacent square must both be black.
 * If we have 3 numbers the same in a 2x2 corner, the apex of the L
 * thus formed must be black.
 * If we have 2 numbers the same in a 2x2 corner, the non-same cell
 * one away from the corner must be white. */
static void solve_corner(game_state *state, struct solver_state *ss,
                        int x, int y, int dx, int dy)
{
    int is[4], ns[4], xx, yy, w = state->w;

    for (yy = 0; yy < 2; yy++) {
        for (xx = 0; xx < 2; xx++) {
            is[yy*2+xx] = (y + dy*yy) * w + (x + dx*xx);
            ns[yy*2+xx] = state->nums[is[yy*2+xx]];
        }
    } /* order is now (corner, side 1, side 2, inner) */

    if (ns[0] == ns[1] && ns[0] == ns[2] && ns[0] == ns[3]) {
        solver_op_add(ss, is[0]%w, is[0]/w, BLACK, "QC: corner with 4 matching");
        solver_op_add(ss, is[3]%w, is[3]/w, BLACK, "QC: corner with 4 matching");
    } else if (ns[0] == ns[1] && ns[0] == ns[2]) {
        /* corner and 2 sides: apex is corner. */
        solver_op_add(ss, is[0]%w, is[0]/w, BLACK, "TC: corner apex from 3 matching");
    } else if (ns[1] == ns[2] && ns[1] == ns[3]) {
        /* side, side, fourth: apex is fourth. */
        solver_op_add(ss, is[3]%w, is[3]/w, BLACK, "TC: inside apex from 3 matching");
    } else if (ns[0] == ns[1] || ns[1] == ns[3]) {
        /* either way here we match the non-identical side. */
        solver_op_add(ss, is[2]%w, is[2]/w, CIRCLE, "DC: corner with 2 matching");
    } else if (ns[0] == ns[2] || ns[2] == ns[3]) {
        /* ditto */
        solver_op_add(ss, is[1]%w, is[1]/w, CIRCLE, "DC: corner with 2 matching");
    }
}

static int solve_corners(game_state *state, struct solver_state *ss)
{
    int n_ops = ss->n_ops;

    solve_corner(state, ss, 0,          0,           1,  1);
    solve_corner(state, ss, state->w-1, 0,          -1,  1);
    solve_corner(state, ss, state->w-1, state->h-1, -1, -1);
    solve_corner(state, ss, 0,          state->h-1,  1, -1);

    return ss->n_ops - n_ops;
}

/* If you have the following situation:
 * ...
 * ...x A x x y A x...
 * ...x B x x B y x...
 * ...
 * then both squares marked 'y' must be white. One of the left-most A or B must
 * be white (since two side-by-side black cells are disallowed), which means
 * that the corresponding right-most A or B must be black (since you can't
 * have two of the same number on one line); thus, the adjacent squares
 * to that right-most A or B must be white, which include the two marked 'y'
 * in either case.
 * Obviously this works in any row or column. It also works if A == B.
 * It doesn't work for the degenerate case:
 * ...x A A x x
 * ...x B y x x
 * where the square marked 'y' isn't necessarily white (consider the left-most A
 * is black).
 *
 * */
static void solve_offsetpair_pair(game_state *state, struct solver_state *ss,
                                  int x1, int y1, int x2, int y2)
{
    int ox, oy, w = state->w, ax, ay, an, d, dx[2], dy[2], dn, xd, yd;

    if (x1 == x2) { /* same column */
        ox = 1; oy = 0;
    } else {
        assert(y1 == y2);
        ox = 0; oy = 1;
    }

    /* We try adjacent to (x1,y1) and the two diag. adjacent to (x2, y2).
     * We expect to be called twice, once each way around. */
    ax = x1+ox; ay = y1+oy;
    assert(INGRID(state, ax, ay));
    an = state->nums[ay*w + ax];

    dx[0] = x2 + ox + oy; dx[1] = x2 + ox - oy;
    dy[0] = y2 + oy + ox; dy[1] = y2 + oy - ox;

    for (d = 0; d < 2; d++) {
        if (INGRID(state, dx[d], dy[d]) && (dx[d] != ax || dy[d] != ay)) {
            /* The 'dx != ax || dy != ay' removes the degenerate case,
             * mentioned above. */
            dn = state->nums[dy[d]*w + dx[d]];
            if (an == dn) {
                /* We have a match; so (WLOG) the 'A' marked above are at
                 * (x1,y1) and (x2,y2), and the 'B' are at (ax,ay) and (dx,dy). */
                debug(("Found offset-pair: %d at (%d,%d) and (%d,%d)\n",
                       state->nums[y1*w + x1], x1, y1, x2, y2));
                debug(("              and: %d at (%d,%d) and (%d,%d)\n",
                       an, ax, ay, dx[d], dy[d]));

                xd = dx[d] - x2; yd = dy[d] - y2;
                solver_op_add(ss, x2 + xd, y2, CIRCLE, "IP: next to offset-pair");
                solver_op_add(ss, x2, y2 + yd, CIRCLE, "IP: next to offset-pair");
            }
        }
    }
}

static int solve_offsetpair(game_state *state, struct solver_state *ss)
{
    int n_ops = ss->n_ops, x, xx, y, yy, n1, n2;

    for (x = 0; x < state->w-1; x++) {
        for (y = 0; y < state->h; y++) {
            n1 = state->nums[y*state->w + x];
            for (yy = y+1; yy < state->h; yy++) {
                n2 = state->nums[yy*state->w + x];
                if (n1 == n2) {
                    solve_offsetpair_pair(state, ss, x,  y, x, yy);
                    solve_offsetpair_pair(state, ss, x, yy, x,  y);
                }
            }
        }
    }
    for (y = 0; y < state->h-1; y++) {
        for (x = 0; x < state->w; x++) {
            n1 = state->nums[y*state->w + x];
            for (xx = x+1; xx < state->w; xx++) {
                n2 = state->nums[y*state->w + xx];
                if (n1 == n2) {
                    solve_offsetpair_pair(state, ss, x,  y, xx, y);
                    solve_offsetpair_pair(state, ss, xx, y,  x, y);
                }
            }
        }
    }
    return ss->n_ops - n_ops;
}

static bool solve_hassinglewhiteregion(
    game_state *state, struct solver_state *ss)
{
    int i, j, nwhite = 0, lwhite = -1, szwhite, start, end, next, a, d, x, y;

    for (i = 0; i < state->n; i++) {
        if (!(state->flags[i] & F_BLACK)) {
            nwhite++;
            lwhite = i;
        }
        state->flags[i] &= ~F_SCRATCH;
    }
    if (lwhite == -1) {
        debug(("solve_hassinglewhite: no white squares found!\n"));
        state->impossible = true;
        return false;
    }
    /* We don't use connect_dsf here; it's too slow, and there's a quicker
     * algorithm if all we want is the size of one region. */
    /* Having written this, this algorithm is only about 5% faster than
     * using a dsf. */
    memset(ss->scratch, -1, state->n * sizeof(int));
    ss->scratch[0] = lwhite;
    state->flags[lwhite] |= F_SCRATCH;
    start = 0; end = next = 1;
    while (start < end) {
        for (a = start; a < end; a++) {
            i = ss->scratch[a]; assert(i != -1);
            for (d = 0; d < 4; d++) {
                x = (i % state->w) + dxs[d];
                y = (i / state->w) + dys[d];
                j = y*state->w + x;
                if (!INGRID(state, x, y)) continue;
                if (state->flags[j] & (F_BLACK | F_SCRATCH)) continue;
                ss->scratch[next++] = j;
                state->flags[j] |= F_SCRATCH;
            }
        }
        start = end; end = next;
    }
    szwhite = next;
    return (szwhite == nwhite);
}

static void solve_removesplits_check(game_state *state, struct solver_state *ss,
                                     int x, int y)
{
    int i = y*state->w + x;
    bool issingle;

    if (!INGRID(state, x, y)) return;
    if ((state->flags[i] & F_CIRCLE) || (state->flags[i] & F_BLACK))
        return;

    /* If putting a black square at (x,y) would make the white region
     * non-contiguous, it must be circled. */
    state->flags[i] |= F_BLACK;
    issingle = solve_hassinglewhiteregion(state, ss);
    state->flags[i] &= ~F_BLACK;

    if (!issingle)
        solver_op_add(ss, x, y, CIRCLE, "MC: black square here would split white region");
}

/* For all black squares, search in squares diagonally adjacent to see if
 * we can rule out putting a black square there (because it would make the
 * white region non-contiguous). */
/* This function is likely to be somewhat slow. */
static int solve_removesplits(game_state *state, struct solver_state *ss)
{
    int i, x, y, n_ops = ss->n_ops;

    if (!solve_hassinglewhiteregion(state, ss)) {
        debug(("solve_removesplits: white region is not contiguous at start!\n"));
        state->impossible = true;
        return 0;
    }

    for (i = 0; i < state->n; i++) {
        if (!(state->flags[i] & F_BLACK)) continue;

        x = i%state->w; y = i/state->w;
        solve_removesplits_check(state, ss, x-1, y-1);
        solve_removesplits_check(state, ss, x+1, y-1);
        solve_removesplits_check(state, ss, x+1, y+1);
        solve_removesplits_check(state, ss, x-1, y+1);
    }
    return ss->n_ops - n_ops;
}

/*
 * This function performs a solver step that isn't implicit in the rules
 * of the game and is thus treated somewhat differently.
 *
 * It marks cells whose number does not exist elsewhere in its row/column
 * with circles. As it happens the game generator here does mean that this
 * is always correct, but it's a solving method that people should not have
 * to rely upon (except in the hidden 'sneaky' difficulty setting) and so
 * all grids at 'tricky' and above are checked to make sure that the grid
 * is no easier if this solving step is performed beforehand.
 *
 * Calling with ss=NULL just returns the number of sneaky deductions that
 * would have been made.
 */
static int solve_sneaky(game_state *state, struct solver_state *ss)
{
    int i, ii, x, xx, y, yy, nunique = 0;

    /* Clear SCRATCH flags. */
    for (i = 0; i < state->n; i++) state->flags[i] &= ~F_SCRATCH;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*state->w + x;

            /* Check for duplicate numbers on our row, mark (both) if so */
            for (xx = x; xx < state->w; xx++) {
                ii = y*state->w + xx;
                if (i == ii) continue;

                if (state->nums[i] == state->nums[ii]) {
                    state->flags[i] |= F_SCRATCH;
                    state->flags[ii] |= F_SCRATCH;
                }
            }

            /* Check for duplicate numbers on our col, mark (both) if so */
            for (yy = y; yy < state->h; yy++) {
                ii = yy*state->w + x;
                if (i == ii) continue;

                if (state->nums[i] == state->nums[ii]) {
                    state->flags[i] |= F_SCRATCH;
                    state->flags[ii] |= F_SCRATCH;
                }
            }
        }
    }

    /* Any cell with no marking has no duplicates on its row or column:
     * set its CIRCLE. */
    for (i = 0; i < state->n; i++) {
        if (!(state->flags[i] & F_SCRATCH)) {
            if (ss) solver_op_add(ss, i%state->w, i/state->w, CIRCLE,
                                  "SNEAKY: only one of its number in row and col");
            nunique += 1;
        } else
            state->flags[i] &= ~F_SCRATCH;
    }
    return nunique;
}

static int solve_specific(game_state *state, int diff, bool sneaky)
{
    struct solver_state *ss = solver_state_new(state);

    if (sneaky) solve_sneaky(state, ss);

    /* Some solver operations we only have to perform once --
     * they're only based on the numbers available, and not black
     * squares or circles which may be added later. */

    solve_singlesep(state, ss);        /* never sets impossible */
    solve_doubles(state, ss);          /* ditto */
    solve_corners(state, ss);          /* ditto */

    if (diff >= DIFF_TRICKY)
        solve_offsetpair(state, ss);       /* ditto */

    while (1) {
        if (ss->n_ops > 0) solver_ops_do(state, ss);
        if (state->impossible) break;

        if (solve_allblackbutone(state, ss) > 0) continue;
        if (state->impossible) break;

        if (diff >= DIFF_TRICKY) {
            if (solve_removesplits(state, ss) > 0) continue;
            if (state->impossible) break;
        }

        break;
    }

    solver_state_free(ss);
    return state->impossible ? -1 : check_complete(state, CC_MUST_FILL);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *solved = dup_game(currstate);
    char *move = NULL;

    if (solve_specific(solved, DIFF_ANY, false) > 0) goto solved;
    free_game(solved);

    solved = dup_game(state);
    if (solve_specific(solved, DIFF_ANY, false) > 0) goto solved;
    free_game(solved);

    *error = "Unable to solve puzzle.";
    return NULL;

solved:
    move = game_state_diff(currstate, solved, true);
    free_game(solved);
    return move;
}

/* --- Game generation --- */

/* A correctly completed Hitori board is essentially a latin square
 * (no duplicated numbers in any row or column) with black squares
 * added such that no black square touches another, and the white
 * squares make a contiguous region.
 *
 * So we can generate it by:
   * constructing a latin square
   * adding black squares at random (minding the constraints)
   * altering the numbers under the new black squares such that
      the solver gets a headstart working out where they are.
 */

static bool new_game_is_good(const game_params *params,
                             game_state *state, game_state *tosolve)
{
    int sret, sret_easy = 0;

    memcpy(tosolve->nums, state->nums, state->n * sizeof(int));
    memset(tosolve->flags, 0, state->n * sizeof(unsigned int));
    tosolve->completed = false;
    tosolve->impossible = false;

    /*
     * We try and solve it twice, once at our requested difficulty level
     * (ensuring it's soluble at all) and once at the level below (if
     * it exists), which we hope to fail: if you can also solve it at
     * the level below then it's too easy and we have to try again.
     *
     * With this puzzle in particular there's an extra finesse, which is
     * that we check that the generated puzzle isn't too easy _with
     * an extra solver step first_, which is the 'sneaky' mode of deductions
     * (asserting that any number which fulfils the latin-square rules
     * on its row/column must be white). This is an artefact of the
     * generation process and not implicit in the rules, so we don't want
     * people to be able to use it to make the puzzle easier.
     */

    assert(params->diff < DIFF_MAX);
    sret = solve_specific(tosolve, params->diff, false);
    if (params->diff > DIFF_EASY) {
        memset(tosolve->flags, 0, state->n * sizeof(unsigned int));
        tosolve->completed = false;
        tosolve->impossible = false;

        /* this is the only time the 'sneaky' flag is set. */
        sret_easy = solve_specific(tosolve, params->diff-1, true);
    }

    if (sret <= 0 || sret_easy > 0) {
        debug(("Generated puzzle %s at chosen difficulty %s\n",
               sret <= 0 ? "insoluble" : "too easy",
               singles_diffnames[params->diff]));
        return false;
    }
    return true;
}

#define MAXTRIES 20

static int best_black_col(game_state *state, random_state *rs, int *scratch,
                          int i, int *rownums, int *colnums)
{
    int w = state->w, x = i%w, y = i/w, j, o = state->o;

    /* Randomise the list of numbers to try. */
    for (i = 0; i < o; i++) scratch[i] = i;
    shuffle(scratch, o, sizeof(int), rs);

    /* Try each number in turn, first giving preference to removing
     * latin-square characteristics (i.e. those numbers which only
     * occur once in a row/column). The '&&' here, although intuitively
     * wrong, results in a smaller number of 'sneaky' deductions on
     * solvable boards. */
    for (i = 0; i < o; i++) {
        j = scratch[i] + 1;
        if (rownums[y*o + j-1] == 1 && colnums[x*o + j-1] == 1)
            goto found;
    }

    /* Then try each number in turn returning the first one that's
     * not actually unique in its row/column (see comment below) */
    for (i = 0; i < o; i++) {
        j = scratch[i] + 1;
        if (rownums[y*o + j-1] != 0 || colnums[x*o + j-1] != 0)
            goto found;
    }
    assert(!"unable to place number under black cell.");
    return 0;

found:
    /* Update column and row counts assuming this number will be placed. */
    rownums[y*o + j-1] += 1;
    colnums[x*o + j-1] += 1;
    return j;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    game_state *state = blank_game(params->w, params->h);
    game_state *tosolve = blank_game(params->w, params->h);
    int i, j, *scratch, *rownums, *colnums, x, y, ntries;
    int w = state->w, h = state->h, o = state->o;
    char *ret;
    digit *latin;
    struct solver_state *ss = solver_state_new(state);

    scratch = snewn(state->n, int);
    rownums = snewn(h*o, int);
    colnums = snewn(w*o, int);

generate:
    ss->n_ops = 0;
    debug(("Starting game generation, size %dx%d\n", w, h));

    memset(state->flags, 0, state->n*sizeof(unsigned int));

    /* First, generate the latin rectangle.
     * The order of this, o, is max(w,h). */
    latin = latin_generate_rect(w, h, rs);
    for (i = 0; i < state->n; i++)
        state->nums[i] = (int)latin[i];
    sfree(latin);
    debug_state("State after latin square", state);

    /* Add black squares at random, using bits of solver as we go (to lay
     * white squares), until we can lay no more blacks. */
    for (i = 0; i < state->n; i++)
        scratch[i] = i;
    shuffle(scratch, state->n, sizeof(int), rs);
    for (j = 0; j < state->n; j++) {
        i = scratch[j];
        if ((state->flags[i] & F_CIRCLE) || (state->flags[i] & F_BLACK)) {
            debug(("generator skipping (%d,%d): %s\n", i%w, i/w,
                   (state->flags[i] & F_CIRCLE) ? "CIRCLE" : "BLACK"));
            continue; /* solver knows this must be one or the other already. */
        }

        /* Add a random black cell... */
        solver_op_add(ss, i%w, i/w, BLACK, "Generator: adding random black cell");
        solver_ops_do(state, ss);

        /* ... and do as well as we know how to lay down whites that are now forced. */
        solve_allblackbutone(state, ss);
        solver_ops_do(state, ss);

        solve_removesplits(state, ss);
        solver_ops_do(state, ss);

        if (state->impossible) {
            debug(("generator made impossible, restarting...\n"));
            goto generate;
        }
    }
    debug_state("State after adding blacks", state);

    /* Now we know which squares are white and which are black, we lay numbers
     * under black squares at random, except that the number must appear in
     * white cells at least once more in the same column or row as that [black]
     * square. That's necessary to avoid multiple solutions, where blackening
     * squares in the finished puzzle becomes optional. We use two arrays:
     *
     * rownums[ROW * o + NUM-1] is the no. of white cells containing NUM in y=ROW
     * colnums[COL * o + NUM-1] is the no. of white cells containing NUM in x=COL
     */

    memset(rownums, 0, h*o * sizeof(int));
    memset(colnums, 0, w*o * sizeof(int));
    for (i = 0; i < state->n; i++) {
        if (state->flags[i] & F_BLACK) continue;
        j = state->nums[i];
        x = i%w; y = i/w;
        rownums[y * o + j-1] += 1;
        colnums[x * o + j-1] += 1;
    }

    ntries = 0;
randomise:
    for (i = 0; i < state->n; i++) {
        if (!(state->flags[i] & F_BLACK)) continue;
        state->nums[i] = best_black_col(state, rs, scratch, i, rownums, colnums);
    }
    debug_state("State after adding numbers", state);

    /* DIFF_ANY just returns whatever we first generated, for testing purposes. */
    if (params->diff != DIFF_ANY &&
        !new_game_is_good(params, state, tosolve)) {
        ntries++;
        if (ntries > MAXTRIES) {
            debug(("Ran out of randomisation attempts, re-generating.\n"));
            goto generate;
        }
        debug(("Re-randomising numbers under black squares.\n"));
        goto randomise;
    }

    ret = generate_desc(state, false);

    free_game(tosolve);
    free_game(state);
    solver_state_free(ss);
    sfree(scratch);
    sfree(rownums);
    sfree(colnums);

    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *ret = NULL;

    unpick_desc(params, desc, NULL, &ret);
    return ret;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = NULL;

    unpick_desc(params, desc, &state, NULL);
    if (!state) assert(!"new_game failed to unpick");
    return state;
}

/* --- Game UI and move routines --- */

struct game_ui {
    int cx, cy;
    bool cshow, show_black_nums;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->cx = ui->cy = 0;
    ui->cshow = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->show_black_nums = false;

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
    if (!oldstate->completed && newstate->completed)
        ui->cshow = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cshow) {
        unsigned int f = state->flags[ui->cy * state->w + ui->cx];
        if (f & F_BLACK) return "Restore";
        if (f & F_CIRCLE) return "Remove";
        return button == CURSOR_SELECT ? "Black" : "Circle";
    }
    return "";
}

#define DS_BLACK        0x1
#define DS_CIRCLE       0x2
#define DS_CURSOR       0x4
#define DS_BLACK_NUM    0x8
#define DS_ERROR        0x10
#define DS_FLASH        0x20
#define DS_IMPOSSIBLE   0x40

struct game_drawstate {
    int tilesize;
    bool started, solved;
    int w, h, n;

    unsigned int *flags;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int mx, int my, int button)
{
    char buf[80], c;
    int i, x = FROMCOORD(mx), y = FROMCOORD(my);
    enum { NONE, TOGGLE_BLACK, TOGGLE_CIRCLE, UI } action = NONE;

    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cx, &ui->cy, state->w, state->h, true);
        ui->cshow = true;
        action = UI;
    } else if (IS_CURSOR_SELECT(button)) {
        x = ui->cx; y = ui->cy;
        if (!ui->cshow) {
            action = UI;
            ui->cshow = true;
        }
        if (button == CURSOR_SELECT) {
            action = TOGGLE_BLACK;
        } else if (button == CURSOR_SELECT2) {
            action = TOGGLE_CIRCLE;
        }
    } else if (IS_MOUSE_DOWN(button)) {
        if (ui->cshow) {
            ui->cshow = false;
            action = UI;
        }
        if (!INGRID(state, x, y)) {
            ui->show_black_nums = !ui->show_black_nums;
            action = UI; /* this wants to be a per-game option. */
        } else if (button == LEFT_BUTTON) {
            action = TOGGLE_BLACK;
        } else if (button == RIGHT_BUTTON) {
            action = TOGGLE_CIRCLE;
        }
    }
    if (action == UI) return UI_UPDATE;

    if (action == TOGGLE_BLACK || action == TOGGLE_CIRCLE) {
        i = y * state->w + x;
        if (state->flags[i] & (F_BLACK | F_CIRCLE))
            c = 'E';
        else
            c = (action == TOGGLE_BLACK) ? 'B' : 'C';
        sprintf(buf, "%c%d,%d", (int)c, x, y);
        return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    int x, y, i, n;

    debug(("move: %s\n", move));

    while (*move) {
        char c = *move;
        if (c == 'B' || c == 'C' || c == 'E') {
            move++;
            if (sscanf(move, "%d,%d%n", &x, &y, &n) != 2 ||
                !INGRID(state, x, y))
                goto badmove;

            i = y*ret->w + x;
            ret->flags[i] &= ~(F_CIRCLE | F_BLACK); /* empty first, always. */
            if (c == 'B')
                ret->flags[i] |= F_BLACK;
            else if (c == 'C')
                ret->flags[i] |= F_CIRCLE;
            move += n;
        } else if (c == 'S') {
            move++;
            ret->used_solve = true;
        } else
            goto badmove;

        if (*move == ';')
            move++;
        else if (*move)
            goto badmove;
    }
    if (check_complete(ret, CC_MARK_ERRORS)) ret->completed = true;
    return ret;

badmove:
    free_game(ret);
    return NULL;
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

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
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

    game_mkhighlight(fe, ret, COL_BACKGROUND, -1, COL_LOWLIGHT);
    for (i = 0; i < 3; i++) {
        ret[COL_BLACK * 3 + i] = 0.0F;
        ret[COL_BLACKNUM * 3 + i] = 0.4F;
        ret[COL_WHITE * 3 + i] = 1.0F;
        ret[COL_GRID * 3 + i] = ret[COL_LOWLIGHT * 3 + i];
        ret[COL_UNUSED1 * 3 + i] = 0.0F; /* To placate an assertion. */
    }
    ret[COL_CURSOR * 3 + 0] = 0.2F;
    ret[COL_CURSOR * 3 + 1] = 0.8F;
    ret[COL_CURSOR * 3 + 2] = 0.0F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->started = false;
    ds->solved = false;
    ds->w = state->w;
    ds->h = state->h;
    ds->n = state->n;

    ds->flags = snewn(state->n, unsigned int);

    memset(ds->flags, 0, state->n*sizeof(unsigned int));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->flags);
    sfree(ds);
}

static void tile_redraw(drawing *dr, game_drawstate *ds, int x, int y,
                        int num, unsigned int f)
{
    int tcol, bg, cx, cy, tsz;
    bool dnum;
    char buf[32];

    if (f & DS_BLACK) {
        bg = (f & DS_ERROR) ? COL_ERROR : COL_BLACK;
        tcol = COL_BLACKNUM;
        dnum = (f & DS_BLACK_NUM);
    } else {
        bg = (f & DS_FLASH) ? COL_LOWLIGHT : COL_BACKGROUND;
        tcol = (f & DS_ERROR) ? COL_ERROR : COL_BLACK;
        dnum = true;
    }

    cx = x + TILE_SIZE/2; cy = y + TILE_SIZE/2;

    draw_rect(dr, x,    y, TILE_SIZE, TILE_SIZE, bg);
    draw_rect_outline(dr, x, y, TILE_SIZE, TILE_SIZE,
                      (f & DS_IMPOSSIBLE) ? COL_ERROR : COL_GRID);

    if (f & DS_CIRCLE) {
        draw_circle(dr, cx, cy, CRAD, tcol, tcol);
        draw_circle(dr, cx, cy, CRAD-1, bg, tcol);
    }

    if (dnum) {
        sprintf(buf, "%d", num);
        if (strlen(buf) == 1)
            tsz = TEXTSZ;
        else
            tsz = (CRAD*2 - 1) / strlen(buf);
        draw_text(dr, cx, cy, FONT_VARIABLE, tsz,
                  ALIGN_VCENTRE | ALIGN_HCENTRE, tcol, buf);
    }

    if (f & DS_CURSOR)
        draw_rect_corners(dr, cx, cy, TEXTSZ/2, COL_CURSOR);

    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y, i, flash;
    unsigned int f;

    flash = (int)(flashtime * 5 / FLASH_TIME) % 2;

    if (!ds->started) {
        int wsz = TILE_SIZE * state->w + 2 * BORDER;
        int hsz = TILE_SIZE * state->h + 2 * BORDER;
        draw_rect_outline(dr, COORD(0)-1, COORD(0)-1,
			  TILE_SIZE * state->w + 2, TILE_SIZE * state->h + 2,
                          COL_GRID);
        draw_update(dr, 0, 0, wsz, hsz);
    }
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*state->w + x;
            f = 0;

            if (flash) f |= DS_FLASH;
            if (state->impossible) f |= DS_IMPOSSIBLE;

            if (ui->cshow && x == ui->cx && y == ui->cy)
                f |= DS_CURSOR;
            if (state->flags[i] & F_BLACK) {
                f |= DS_BLACK;
                if (ui->show_black_nums) f |= DS_BLACK_NUM;
            }
            if (state->flags[i] & F_CIRCLE)
                f |= DS_CIRCLE;
            if (state->flags[i] & F_ERROR)
                f |= DS_ERROR;

            if (!ds->started || ds->flags[i] != f) {
                tile_redraw(dr, ds, COORD(x), COORD(y),
                            state->nums[i], f);
                ds->flags[i] = f;
            }
        }
    }
    ds->started = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed &&
        newstate->completed && !newstate->used_solve)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cshow) {
        *x = COORD(ui->cx);
        *y = COORD(ui->cy);
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

    /* 8mm squares by default. */
    game_compute_size(params, 800, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y, ox, oy, i;
    char buf[32];

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    print_line_width(dr, 2 * TILE_SIZE / 40);

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            ox = COORD(x); oy = COORD(y);
            i = y*state->w+x;

            if (state->flags[i] & F_BLACK) {
                draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE, ink);
            } else {
                draw_rect_outline(dr, ox, oy, TILE_SIZE, TILE_SIZE, ink);

                if (state->flags[i] & DS_CIRCLE)
                    draw_circle(dr, ox+TILE_SIZE/2, oy+TILE_SIZE/2, CRAD,
                                paper, ink);

                sprintf(buf, "%d", state->nums[i]);
                draw_text(dr, ox+TILE_SIZE/2, oy+TILE_SIZE/2, FONT_VARIABLE,
                          TEXTSZ/strlen(buf), ALIGN_VCENTRE | ALIGN_HCENTRE,
                          ink, buf);
            }
        }
    }
}

#ifdef COMBINED
#define thegame singles
#endif

const struct game thegame = {
    "Singles", "games.singles", "singles",
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

#include <time.h>
#include <stdarg.h>

static void start_soak(game_params *p, random_state *rs)
{
    time_t tt_start, tt_now, tt_last;
    char *desc, *aux;
    game_state *s;
    int i, n = 0, ndiff[DIFF_MAX], diff, sret, nblack = 0, nsneaky = 0;

    tt_start = tt_now = time(NULL);

    printf("Soak-testing a %dx%d grid.\n", p->w, p->h);
    p->diff = DIFF_ANY;

    memset(ndiff, 0, DIFF_MAX * sizeof(int));

    while (1) {
        n++;
        desc = new_game_desc(p, rs, &aux, false);
        s = new_game(NULL, p, desc);
        nsneaky += solve_sneaky(s, NULL);

        for (diff = 0; diff < DIFF_MAX; diff++) {
            memset(s->flags, 0, s->n * sizeof(unsigned int));
            s->completed = false;
            s->impossible = false;
            sret = solve_specific(s, diff, false);
            if (sret > 0) {
                ndiff[diff]++;
                break;
            } else if (sret < 0)
                fprintf(stderr, "Impossible! %s\n", desc);
        }
        for (i = 0; i < s->n; i++) {
            if (s->flags[i] & F_BLACK) nblack++;
        }
        free_game(s);
        sfree(desc);

        tt_last = time(NULL);
        if (tt_last > tt_now) {
            tt_now = tt_last;
            printf("%d total, %3.1f/s, bl/sn %3.1f%%/%3.1f%%: ",
                   n, (double)n / ((double)tt_now - tt_start),
                   ((double)nblack * 100.0) / (double)(n * p->w * p->h),
                   ((double)nsneaky * 100.0) / (double)(n * p->w * p->h));
            for (diff = 0; diff < DIFF_MAX; diff++) {
                if (diff > 0) printf(", ");
                printf("%d (%3.1f%%) %s",
                       ndiff[diff], (double)ndiff[diff] * 100.0 / (double)n,
                       singles_diffnames[diff]);
            }
            printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    char *id = NULL, *desc, *desc_gen = NULL, *tgame, *aux;
    const char *err;
    game_state *s = NULL;
    game_params *p = NULL;
    int soln, ret = 1;
    bool soak = false;
    time_t seed = time(NULL);
    random_state *rs = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            verbose = true;
        } else if (!strcmp(p, "--soak")) {
            soak = true;
        } else if (!strcmp(p, "--seed")) {
            if (argc == 0) {
                fprintf(stderr, "%s: --seed needs an argument", argv[0]);
                goto done;
            }
            seed = (time_t)atoi(*++argv);
            argc--;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    rs = random_new((void*)&seed, sizeof(time_t));

    if (!id) {
        fprintf(stderr, "usage: %s [-v] [--soak] <params> | <game_id>\n", argv[0]);
        goto done;
    }
    desc = strchr(id, ':');
    if (desc) *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_params(p, true);
    if (err) {
        fprintf(stderr, "%s: %s", argv[0], err);
        goto done;
    }

    if (soak) {
        if (desc) {
            fprintf(stderr, "%s: --soak only needs params, not game desc.\n", argv[0]);
            goto done;
        }
        start_soak(p, rs);
    } else {
        if (!desc) desc = desc_gen = new_game_desc(p, rs, &aux, false);

        err = validate_desc(p, desc);
        if (err) {
            fprintf(stderr, "%s: %s\n", argv[0], err);
            free_params(p);
            goto done;
        }
        s = new_game(NULL, p, desc);

        if (verbose) {
            tgame = game_text_format(s);
            fputs(tgame, stdout);
            sfree(tgame);
        }

        soln = solve_specific(s, DIFF_ANY, false);
        tgame = game_text_format(s);
        fputs(tgame, stdout);
        sfree(tgame);
        printf("Game was %s.\n\n",
               soln < 0 ? "impossible" : soln > 0 ? "solved" : "not solved");
    }
    ret = 0;

done:
    if (desc_gen) sfree(desc_gen);
    if (p) free_params(p);
    if (s) free_game(s);
    if (rs) random_free(rs);

    return ret;
}

#endif


/* vim: set shiftwidth=4 tabstop=8: */
