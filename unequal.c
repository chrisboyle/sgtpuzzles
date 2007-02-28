/*
 * unequal.c
 *
 * Implementation of 'Futoshiki', a puzzle featured in the Guardian.
 *
 * TTD:
   * add multiple-links-on-same-col/row solver nous
   * Optimise set solver to use bit operations instead
 *
 * Guardian puzzles of note:
   * #1: 5:0,0L,0L,0,0,0R,0,0L,0D,0L,0R,0,2,0D,0,0,0,0,0,0,0U,0,0,0,0U,
   * #2: 5:0,0,0,4L,0L,0,2LU,0L,0U,0,0,0U,0,0,0,0,0D,0,3LRUD,0,0R,3,0L,0,0,
   * #3: (reprint of #2)
   * #4: 
   * #5: 5:0,0,0,0,0,0,2,0U,3U,0U,0,0,3,0,0,0,3,0D,4,0,0,0L,0R,0,0,
   * #6: 5:0D,0L,0,0R,0,0,0D,0,3,0D,0,0R,0,0R,0D,0U,0L,0,1,2,0,0,0U,0,0L,
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h" /* contains typedef for digit */

/* ----------------------------------------------------------
 * Constant and structure definitions
 */

#define FLASH_TIME 0.4F

#define PREFERRED_TILE_SIZE 32

#define TILE_SIZE (ds->tilesize)
#define GAP_SIZE  (TILE_SIZE/2)
#define SQUARE_SIZE (TILE_SIZE + GAP_SIZE)

#define BORDER    (TILE_SIZE / 2)

#define COORD(x)  ( (x) * SQUARE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + SQUARE_SIZE) / SQUARE_SIZE - 1 )

#define GRID(p,w,x,y) ((p)->w[((y)*(p)->order)+(x)])
#define GRID3(p,w,x,y,z) ((p)->w[ (((x)*(p)->order+(y))*(p)->order+(z)) ])
#define HINT(p,x,y,n) GRID3(p, hints, x, y, n)

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_TEXT, COL_GUESS, COL_ERROR, COL_PENCIL,
    COL_HIGHLIGHT, COL_LOWLIGHT,
    NCOLOURS
};

struct game_params {
    int order, diff;
};

#define F_IMMUTABLE     1       /* passed in as game description */
#define F_GT_UP         2
#define F_GT_RIGHT      4
#define F_GT_DOWN       8
#define F_GT_LEFT       16
#define F_ERROR         32
#define F_ERROR_UP      64
#define F_ERROR_RIGHT   128
#define F_ERROR_DOWN    256
#define F_ERROR_LEFT    512

#define F_ERROR_MASK (F_ERROR|F_ERROR_UP|F_ERROR_RIGHT|F_ERROR_DOWN|F_ERROR_LEFT)

struct game_state {
    int order, completed, cheated;
    digit *nums;                 /* actual numbers (size order^2) */
    unsigned char *hints;        /* remaining possiblities (size order^3) */
    unsigned int *flags;         /* flags (size order^2) */
};

/* ----------------------------------------------------------
 * Game parameters and presets
 */

/* Steal the method from map.c for difficulty levels. */
#define DIFFLIST(A)             \
    A(LATIN,Trivial,t)          \
    A(EASY,Easy,e)              \
    A(SET,Tricky,k)             \
    A(EXTREME,Extreme,x)        \
    A(RECURSIVE,Recursive,r)

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFF_IMPOSSIBLE = diff_impossible, DIFF_AMBIGUOUS = diff_ambiguous, DIFF_UNFINISHED = diff_unfinished };
static char const *const unequal_diffnames[] = { DIFFLIST(TITLE) };
static char const unequal_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCOUNT lenof(unequal_diffchars)
#define DIFFCONFIG DIFFLIST(CONFIG)

#define DEFAULT_PRESET 0

const static struct game_params unequal_presets[] = {
    {  4, DIFF_EASY     },
    {  5, DIFF_EASY     },
    {  5, DIFF_SET      },
    {  5, DIFF_EXTREME  },
    {  6, DIFF_EASY     },
    {  6, DIFF_SET      },
    {  6, DIFF_EXTREME  },
    {  7, DIFF_SET      },
    {  7, DIFF_EXTREME  },
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(unequal_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = unequal_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s", ret->order, ret->order,
            unequal_diffnames[ret->diff]);

    *name = dupstr(buf);
    *params = ret;
    return TRUE;
}

static game_params *default_params(void)
{
    game_params *ret;
    char *name;

    if (!game_fetch_preset(DEFAULT_PRESET, &name, &ret)) return NULL;
    sfree(name);
    return ret;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    char const *p = string;

    ret->order = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;

    if (*p == 'd') {
        int i;
        p++;
        ret->diff = DIFFCOUNT+1; /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == unequal_diffchars[i])
                    ret->diff = i;
            }
            p++;
        }
    }
}

static char *encode_params(game_params *params, int full)
{
    char ret[80];

    sprintf(ret, "%d", params->order);
    if (full)
        sprintf(ret + strlen(ret), "d%c", unequal_diffchars[params->diff]);

    return dupstr(ret);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Size (s*s)";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->order);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].sval = DIFFCONFIG;
    ret[1].ival = params->diff;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->order = atoi(cfg[0].sval);
    ret->diff = cfg[1].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->order < 3 || params->order > 32)
        return "Order must be between 3 and 32";
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating.";
    return NULL;
}

/* ----------------------------------------------------------
 * Various utility functions
 */

static const struct { unsigned int f, fo, fe; int dx, dy; char c; } gtthan[] = {
    { F_GT_UP,    F_GT_DOWN,  F_ERROR_UP,     0, -1, '^' },
    { F_GT_RIGHT, F_GT_LEFT,  F_ERROR_RIGHT,  1,  0, '>' },
    { F_GT_DOWN,  F_GT_UP,    F_ERROR_DOWN,   0,  1, 'v' },
    { F_GT_LEFT,  F_GT_RIGHT, F_ERROR_LEFT,  -1,  0, '<' }
};

static game_state *blank_game(int order)
{
    game_state *state = snew(game_state);
    int o2 = order*order, o3 = o2*order;

    state->order = order;
    state->completed = state->cheated = 0;

    state->nums = snewn(o2, digit);
    state->hints = snewn(o3, unsigned char);
    state->flags = snewn(o2, unsigned int);

    memset(state->nums, 0, o2 * sizeof(digit));
    memset(state->hints, 0, o3);
    memset(state->flags, 0, o2 * sizeof(unsigned int));

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = blank_game(state->order);
    int o2 = state->order*state->order, o3 = o2*state->order;

    memcpy(ret->nums, state->nums, o2 * sizeof(digit));
    memcpy(ret->hints, state->hints, o3);
    memcpy(ret->flags, state->flags, o2 * sizeof(unsigned int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->nums);
    sfree(state->hints);
    sfree(state->flags);
    sfree(state);
}

#define CHECKG(x,y) grid[(y)*o+(x)]

/* Returns 0 if it finds an error, 1 otherwise. */
static int check_gt(digit *grid, game_state *state,
                    int x, int y, int dx, int dy)
{
    int o = state->order;
    int n = CHECKG(x,y), dn = CHECKG(x+dx, y+dy);

    assert(n != 0);
    if (dn == 0) return 1;

    if (n <= dn) {
        debug(("check_gt error (%d,%d) (%d,%d)", x, y, x+dx, y+dy));
        return 0;
    }
    return 1;
}

/* Returns 0 if it finds an error, 1 otherwise. */
static int check_num_gt(digit *grid, game_state *state,
                        int x, int y, int me)
{
    unsigned int f = GRID(state, flags, x, y);
    int ret = 1, i;

    for (i = 0; i < 4; i++) {
        if ((f & gtthan[i].f) &&
            !check_gt(grid, state, x, y, gtthan[i].dx, gtthan[i].dy)) {
            if (me) GRID(state, flags, x, y) |= gtthan[i].fe;
            ret = 0;
        }
    }
    return ret;
}

/* Returns 0 if it finds an error, 1 otherwise. */
static int check_num_error(digit *grid, game_state *state,
                           int x, int y, int mark_errors)
{
    int o = state->order;
    int xx, yy, val = CHECKG(x,y), ret = 1;

    assert(val != 0);

    /* check for dups in same column. */
    for (yy = 0; yy < state->order; yy++) {
        if (yy == y) continue;
        if (CHECKG(x,yy) == val) ret = 0;
    }

    /* check for dups in same row. */
    for (xx = 0; xx < state->order; xx++) {
        if (xx == x) continue;
        if (CHECKG(xx,y) == val) ret = 0;
    }

    if (!ret) {
        debug(("check_num_error (%d,%d) duplicate %d", x, y, val));
        if (mark_errors) GRID(state, flags, x, y) |= F_ERROR;
    }
    return ret;
}

/* Returns:     -1 for 'wrong'
 *               0 for 'incomplete'
 *               1 for 'complete and correct'
 */
static int check_complete(digit *grid, game_state *state, int mark_errors)
{
    int x, y, ret = 1, o = state->order;

    if (mark_errors)
        assert(grid == state->nums);

    for (x = 0; x < state->order; x++) {
        for (y = 0; y < state->order; y++) {
            if (mark_errors)
                GRID(state, flags, x, y) &= ~F_ERROR_MASK;
            if (grid[y*o+x] == 0) {
                ret = 0;
            } else {
                if (!check_num_error(grid, state, x, y, mark_errors)) ret = -1;
                if (!check_num_gt(grid, state, x, y, mark_errors)) ret = -1;
            }
        }
    }
    if (ret == 1 && latin_check(grid, o))
        ret = -1;
    return ret;
}

static char n2c(digit n, int order) {
    if (n == 0)         return ' ';
    if (order < 10) {
        if (n < 10)     return '0' + n;
    } else {
        if (n < 11)     return '0' + n-1;
        n -= 11;
        if (n <= 26)    return 'A' + n;
    }
    return '?';
}

/* should be 'digit', but includes -1 for 'not a digit'.
 * Includes keypresses (0 especially) for interpret_move. */
static int c2n(int c, int order) {
    if (c < 0 || c > 0xff)
        return -1;
    if (c == ' ' || c == '\010' || c == '\177')
        return 0;
    if (order < 10) {
        if (c >= '1' && c <= '9')
            return (int)(c - '0');
    } else {
        if (c >= '0' && c <= '9')
            return (int)(c - '0' + 1);
        if (c >= 'A' && c <= 'Z')
            return (int)(c - 'A' + 11);
        if (c >= 'a' && c <= 'z')
            return (int)(c - 'a' + 11);
    }
    return -1;
}

static char *game_text_format(game_state *state)
{
    int x, y, len, n;
    char *ret, *p;

    len = (state->order*2) * (state->order*2-1) + 1;
    ret = snewn(len, char);
    p = ret;

    for (y = 0; y < state->order; y++) {
        for (x = 0; x < state->order; x++) {
            n = GRID(state, nums, x, y);
            *p++ = n > 0 ? n2c(n, state->order) : '.';

            if (x < (state->order-1)) {
                if (GRID(state, flags, x, y) & F_GT_RIGHT)
                    *p++ = '>';
                else if (GRID(state, flags, x+1, y) & F_GT_LEFT)
                    *p++ = '<';
                else
                    *p++ = ' ';
            }
        }
        *p++ = '\n';

        if (y < (state->order-1)) {
            for (x = 0; x < state->order; x++) {
                if (GRID(state, flags, x, y) & F_GT_DOWN)
                    *p++ = 'v';
                else if (GRID(state, flags, x, y+1) & F_GT_UP)
                    *p++ = '^';
                else
                    *p++ = ' ';

                if (x < state->order-1)
                  *p++ = ' ';
            }
            *p++ = '\n';
        }
    }
    *p++ = '\0';

    assert(p - ret == len);
    return ret;
}

#ifdef STANDALONE_SOLVER
static void game_debug(game_state *state)
{
    char *dbg = game_text_format(state);
    printf("%s", dbg);
    sfree(dbg);
}
#endif

/* ----------------------------------------------------------
 * Solver.
 */

struct solver_link {
    int len, gx, gy, lx, ly;
};

typedef struct game_solver {
    struct latin_solver latin; /* keep first in struct! */

    game_state *state;

    int nlinks, alinks;
    struct solver_link *links;
} game_solver;

#if 0
static void solver_debug(game_solver *solver, int wide)
{
#ifdef STANDALONE_SOLVER
    if (solver_show_working) {
        if (!wide)
            game_debug(solver->state);
        else
            latin_solver_debug(solver->latin.cube, solver->latin.o);
    }
#endif
}
#endif

static void solver_add_link(game_solver *solver,
                            int gx, int gy, int lx, int ly, int len)
{
    if (solver->alinks < solver->nlinks+1) {
        solver->alinks = solver->alinks*2 + 1;
        /*debug(("resizing solver->links, new size %d", solver->alinks));*/
        solver->links = sresize(solver->links, solver->alinks, struct solver_link);
    }
    solver->links[solver->nlinks].gx = gx;
    solver->links[solver->nlinks].gy = gy;
    solver->links[solver->nlinks].lx = lx;
    solver->links[solver->nlinks].ly = ly;
    solver->links[solver->nlinks].len = len;
    solver->nlinks++;
    /*debug(("Adding new link: len %d (%d,%d) < (%d,%d), nlinks now %d",
           len, lx, ly, gx, gy, solver->nlinks));*/
}

static game_solver *new_solver(digit *grid, game_state *state)
{
    game_solver *solver = snew(game_solver);
    int o = state->order;
    int i, x, y;
    unsigned int f;

    latin_solver_alloc(&solver->latin, grid, o);

    solver->nlinks = solver->alinks = 0;
    solver->links = NULL;

    for (x = 0; x < o; x++) {
        for (y = 0; y < o; y++) {
            f = GRID(state, flags, x, y);
            for (i = 0; i < 4; i++) {
                if (f & gtthan[i].f)
                    solver_add_link(solver, x, y, x+gtthan[i].dx, y+gtthan[i].dy, 1);
            }
        }
    }

    return solver;
}

static void free_solver(game_solver *solver)
{
    if (solver->links) sfree(solver->links);
    latin_solver_free(&solver->latin);
    sfree(solver);
}

static void solver_nminmax(game_solver *usolver,
                           int x, int y, int *min_r, int *max_r,
                           unsigned char **ns_r)
{
    struct latin_solver *solver = &usolver->latin;
    int o = usolver->latin.o, min = o, max = 0, n;
    unsigned char *ns;

    assert(x >= 0 && y >= 0 && x < o && y < o);

    ns = solver->cube + cubepos(x,y,1);

    if (grid(x,y) > 0) {
        min = max = grid(x,y)-1;
    } else {
        for (n = 0; n < o; n++) {
            if (ns[n]) {
                if (n > max) max = n;
                if (n < min) min = n;
            }
        }
    }
    if (min_r) *min_r = min;
    if (max_r) *max_r = max;
    if (ns_r) *ns_r = ns;
}

static int solver_links(game_solver *usolver)
{
    int i, j, lmin, gmax, nchanged = 0;
    unsigned char *gns, *lns;
    struct solver_link *link;
    struct latin_solver *solver = &usolver->latin;

    for (i = 0; i < usolver->nlinks; i++) {
        link = &usolver->links[i];
        solver_nminmax(usolver, link->gx, link->gy, NULL, &gmax, &gns);
        solver_nminmax(usolver, link->lx, link->ly, &lmin, NULL, &lns);

        for (j = 0; j < solver->o; j++) {
            /* For the 'greater' end of the link, discount all numbers
             * too small to satisfy the inequality. */
            if (gns[j]) {
                if (j < (lmin+link->len)) {
#ifdef STANDALONE_SOLVER
                    if (solver_show_working) {
                        printf("%*slink elimination, (%d,%d) > (%d,%d):\n",
                               solver_recurse_depth*4, "",
                               link->gx, link->gy, link->lx, link->ly);
                        printf("%*s  ruling out %d at (%d,%d)\n",
                               solver_recurse_depth*4, "",
                               j+1, link->gx, link->gy);
                    }
#endif
                    cube(link->gx, link->gy, j+1) = FALSE;
                    nchanged++;
                }
            }
            /* For the 'lesser' end of the link, discount all numbers
             * too large to satisfy inequality. */
            if (lns[j]) {
                if (j > (gmax-link->len)) {
#ifdef STANDALONE_SOLVER
                    if (solver_show_working) {
                        printf("%*slink elimination, (%d,%d) > (%d,%d):\n",
                               solver_recurse_depth*4, "",
                               link->gx, link->gy, link->lx, link->ly);
                        printf("%*s  ruling out %d at (%d,%d)\n",
                               solver_recurse_depth*4, "",
                               j+1, link->lx, link->ly);
                    }
#endif
                    cube(link->lx, link->ly, j+1) = FALSE;
                    nchanged++;
                }
            }
        }
    }
    return nchanged;
}

static int solver_grid(digit *grid, int o, int maxdiff, void *ctx)
{
    game_state *state = (game_state *)ctx;
    game_solver *solver;
    struct latin_solver *lsolver;
    struct latin_solver_scratch *scratch;
    int ret, diff = DIFF_LATIN;

    assert(maxdiff <= DIFF_RECURSIVE);

    assert(state->order == o);
    solver = new_solver(grid, state);

    lsolver = &solver->latin;
    scratch = latin_solver_new_scratch(lsolver);

    while (1) {
cont:
        ret = latin_solver_diff_simple(lsolver);
        if (ret < 0) {
            diff = DIFF_IMPOSSIBLE;
            goto got_result;
        } else if (ret > 0) {
            diff = max(diff, DIFF_LATIN);
            goto cont;
        }

        if (maxdiff <= DIFF_LATIN)
            break;

        /* This bit is unequal-specific */
        ret = solver_links(solver);
        if (ret < 0) {
            diff = DIFF_IMPOSSIBLE;
            goto got_result;
        } else if (ret > 0) {
            diff = max(diff, DIFF_EASY);
            goto cont;
        }

        if (maxdiff <= DIFF_EASY)
            break;

        /* Row- and column-wise set elimination */
        ret = latin_solver_diff_set(lsolver, scratch, 0);
        if (ret < 0) {
            diff = DIFF_IMPOSSIBLE;
            goto got_result;
        } else if (ret > 0) {
            diff = max(diff, DIFF_SET);
            goto cont;
        }

        if (maxdiff <= DIFF_SET)
            break;

        ret = latin_solver_diff_set(lsolver, scratch, 1);
        if (ret < 0) {
            diff = DIFF_IMPOSSIBLE;
            goto got_result;
        } else if (ret > 0) {
            diff = max(diff, DIFF_EXTREME);
            goto cont;
        }

        /*
         * Forcing chains.
         */
        if (latin_solver_forcing(lsolver, scratch)) {
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
    if (maxdiff == DIFF_RECURSIVE) {
        int nsol = latin_solver_recurse(lsolver, DIFF_RECURSIVE, solver_grid, ctx);
        if (nsol < 0) diff = DIFF_IMPOSSIBLE;
        else if (nsol == 1) diff = DIFF_RECURSIVE;
        else if (nsol > 1) diff = DIFF_AMBIGUOUS;
        /* if nsol == 0 then we were complete anyway
         * (and thus don't need to change diff) */
    } else {
        int cc = check_complete(grid, state, 0);
        if (cc == -1) diff = DIFF_IMPOSSIBLE;
        if (cc == 0) diff = DIFF_UNFINISHED;
    }

got_result:

#ifdef STANDALONE_SOLVER
    if (solver_show_working)
        printf("%*s%s found\n",
               solver_recurse_depth*4, "",
               diff == DIFF_IMPOSSIBLE ? "no solution (impossible)" :
               diff == DIFF_UNFINISHED ? "no solution (unfinished)" :
               diff == DIFF_AMBIGUOUS ? "multiple solutions" :
               "one solution");
#endif

    latin_solver_free_scratch(scratch);
    memcpy(state->hints, solver->latin.cube, o*o*o);
    free_solver(solver);

    return diff;
}

static int solver_state(game_state *state, int maxdiff)
{
    int diff = solver_grid(state->nums, state->order, maxdiff, (void*)state);

    if (diff == DIFF_IMPOSSIBLE)
        return -1;
    if (diff == DIFF_UNFINISHED)
        return 0;
    if (diff == DIFF_AMBIGUOUS)
        return 2;
    return 1;
}

static game_state *solver_hint(game_state *state, int *diff_r, int mindiff, int maxdiff)
{
    game_state *ret = dup_game(state);
    int diff, r = 0;

    for (diff = mindiff; diff <= maxdiff; diff++) {
        r = solver_state(ret, diff);
        debug(("solver_state after %s %d", unequal_diffnames[diff], r));
        if (r != 0) goto done;
    }

done:
    if (diff_r) *diff_r = (r > 0) ? diff : -1;
    return ret;
}

/* ----------------------------------------------------------
 * Game generation.
 */

static char *latin_desc(digit *sq, size_t order)
{
    int o2 = order*order, i;
    char *soln = snewn(o2+2, char);

    soln[0] = 'S';
    for (i = 0; i < o2; i++)
        soln[i+1] = n2c(sq[i], order);
    soln[o2+1] = '\0';

    return soln;
}

/* returns non-zero if it placed (or could have placed) clue. */
static int gg_place_clue(game_state *state, int ccode, digit *latin, int checkonly)
{
    int loc = ccode / 5, which = ccode % 5;
    int x = loc % state->order, y = loc / state->order;

    assert(loc < state->order*state->order);

    if (which == 4) {           /* add number */
        if (state->nums[loc] != 0) {
#ifdef STANDALONE_SOLVER
            if (state->nums[loc] != latin[loc]) {
                printf("inconsistency for (%d,%d): state %d latin %d\n",
                       x, y, state->nums[loc], latin[loc]);
            }
#endif
            assert(state->nums[loc] == latin[loc]);
            return 0;
        }
        if (!checkonly) {
            state->nums[loc] = latin[loc];
        }
    } else {                    /* add flag */
        int lx, ly, lloc;

        if (state->flags[loc] & gtthan[which].f)
            return 0; /* already has flag. */

        lx = x + gtthan[which].dx;
        ly = y + gtthan[which].dy;
        if (lx < 0 || ly < 0 || lx >= state->order || ly >= state->order)
            return 0; /* flag compares to off grid */

        lloc = loc + gtthan[which].dx + gtthan[which].dy*state->order;
        if (latin[loc] <= latin[lloc])
            return 0; /* flag would be incorrect */

        if (!checkonly) {
            state->flags[loc] |= gtthan[which].f;
        }
    }
    return 1;
}

/* returns non-zero if it removed (or could have removed) the clue. */
static int gg_remove_clue(game_state *state, int ccode, int checkonly)
{
    int loc = ccode / 5, which = ccode % 5;
#ifdef STANDALONE_SOLVER
    int x = loc % state->order, y = loc / state->order;
#endif

    assert(loc < state->order*state->order);

    if (which == 4) {           /* remove number. */
        if (state->nums[loc] == 0) return 0;
        if (!checkonly) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
                printf("gg_remove_clue: removing %d at (%d,%d)",
                       state->nums[loc], x, y);
#endif
            state->nums[loc] = 0;
        }
    } else {                    /* remove flag */
        if (!(state->flags[loc] & gtthan[which].f)) return 0;
        if (!checkonly) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
               printf("gg_remove_clue: removing %c at (%d,%d)",
                       gtthan[which].c, x, y);
#endif
            state->flags[loc] &= ~gtthan[which].f;
        }
    }
    return 1;
}

static int gg_best_clue(game_state *state, int *scratch, digit *latin)
{
    int ls = state->order * state->order * 5;
    int maxposs = 0, minclues = 5, best = -1, i, j;
    int nposs, nclues, loc, x, y;

#ifdef STANDALONE_SOLVER
    if (solver_show_working) {
        game_debug(state);
        latin_solver_debug(state->hints, state->order);
    }
#endif

    for (i = ls; i-- > 0 ;) {
        if (!gg_place_clue(state, scratch[i], latin, 1)) continue;

        loc = scratch[i] / 5;
        x = loc % state->order; y = loc / state->order;
        for (j = nposs = 0; j < state->order; j++) {
            if (state->hints[loc*state->order + j]) nposs++;
        }
        for (j = nclues = 0; j < 4; j++) {
            if (state->flags[loc] & gtthan[j].f) nclues++;
        }
        if ((nposs > maxposs) ||
            (nposs == maxposs && nclues < minclues)) {
            best = i; maxposs = nposs; minclues = nclues;
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
                printf("gg_best_clue: b%d (%d,%d) new best [%d poss, %d clues].",
                       best, x, y, nposs, nclues);
#endif
        }
    }
    /* if we didn't solve, we must have 1 clue to place! */
    assert(best != -1);
    return best;
}

#ifdef STANDALONE_SOLVER
int maxtries;
#define MAXTRIES maxtries
#else
#define MAXTRIES 50
#endif
int gg_solved;

static int game_assemble(game_state *new, int *scratch, digit *latin,
                         int difficulty)
{
    game_state *copy = dup_game(new);
    int best;

    if (difficulty >= DIFF_RECURSIVE) {
        /* We mustn't use any solver that might guess answers;
         * if it guesses wrongly but solves, gg_place_clue will
         * get mighty confused. We will always trim clues down
         * (making it more difficult) in game_strip, which doesn't
         * have this problem. */
        difficulty = DIFF_RECURSIVE-1;
    }

#ifdef STANDALONE_SOLVER
    if (solver_show_working) {
        game_debug(new);
        latin_solver_debug(new->hints, new->order);
    }
#endif

    while(1) {
        gg_solved++;
        if (solver_state(copy, difficulty) == 1) break;

        best = gg_best_clue(copy, scratch, latin);
        gg_place_clue(new, scratch[best], latin, 0);
        gg_place_clue(copy, scratch[best], latin, 0);
    }
    free_game(copy);
#ifdef STANDALONE_SOLVER
    if (solver_show_working) {
        char *dbg = game_text_format(new);
        printf("game_assemble: done, %d solver iterations:\n%s\n", gg_solved, dbg);
        sfree(dbg);
    }
#endif
    return 0;
}

static void game_strip(game_state *new, int *scratch, digit *latin,
                       int difficulty)
{
    int o = new->order, o2 = o*o, lscratch = o2*5, i;
    game_state *copy = blank_game(new->order);

    /* For each symbol (if it exists in new), try and remove it and
     * solve again; if we couldn't solve without it put it back. */
    for (i = 0; i < lscratch; i++) {
        if (!gg_remove_clue(new, scratch[i], 0)) continue;

        memcpy(copy->nums,  new->nums,  o2 * sizeof(digit));
        memcpy(copy->flags, new->flags, o2 * sizeof(unsigned int));
        gg_solved++;
        if (solver_state(copy, difficulty) != 1) {
            /* put clue back, we can't solve without it. */
            int ret = gg_place_clue(new, scratch[i], latin, 0);
            assert(ret == 1);
        } else {
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
                printf("game_strip: clue was redundant.");
#endif
        }
    }
    free_game(copy);
#ifdef STANDALONE_SOLVER
    if (solver_show_working) {
        char *dbg = game_text_format(new);
        debug(("game_strip: done, %d solver iterations.", gg_solved));
        debug(("%s", dbg));
        sfree(dbg);
    }
#endif
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    digit *sq = NULL;
    int i, x, y, retlen, k, nsol;
    int o2 = params->order * params->order, ntries = 0;
    int *scratch, lscratch = o2*5;
    char *ret, buf[80];
    game_state *state = blank_game(params->order);

    /* Generate a list of 'things to strip' (randomised later) */
    scratch = snewn(lscratch, int);
    /* Put the numbers (4 mod 5) before the inequalities (0-3 mod 5) */
    for (i = 0; i < lscratch; i++) scratch[i] = (i%o2)*5 + 4 - (i/o2);

generate:
#ifdef STANDALONE_SOLVER
    if (solver_show_working)
        printf("new_game_desc: generating %s puzzle, ntries so far %d",
               unequal_diffnames[params->diff], ntries);
#endif
    if (sq) sfree(sq);
    sq = latin_generate(params->order, rs);
    latin_debug(sq, params->order);
    /* Separately shuffle the numeric and inequality clues */
    shuffle(scratch, lscratch/5, sizeof(int), rs);
    shuffle(scratch+lscratch/5, 4*lscratch/5, sizeof(int), rs);

    memset(state->nums, 0, o2 * sizeof(digit));
    memset(state->flags, 0, o2 * sizeof(unsigned int));

    gg_solved = 0;
    if (game_assemble(state, scratch, sq, params->diff) < 0)
        goto generate;
    game_strip(state, scratch, sq, params->diff);

    if (params->diff > 0) {
        game_state *copy = dup_game(state);
        nsol = solver_state(copy, params->diff-1);
        free_game(copy);
        if (nsol > 0) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
                printf("game_assemble: puzzle as generated is too easy.");
#endif
            if (ntries < MAXTRIES) {
                ntries++;
                goto generate;
            }
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
                printf("Unable to generate %s %dx%d after %d attempts.",
                       unequal_diffnames[params->diff],
                       params->order, params->order, MAXTRIES);
#endif
            params->diff--;
        }
    }
#ifdef STANDALONE_SOLVER
    if (solver_show_working)
        printf("new_game_desc: generated %s puzzle; %d attempts (%d solver).",
               unequal_diffnames[params->diff], ntries, gg_solved);
#endif

    ret = NULL; retlen = 0;
    for (y = 0; y < params->order; y++) {
        for (x = 0; x < params->order; x++) {
            unsigned int f = GRID(state, flags, x, y);
            k = sprintf(buf, "%d%s%s%s%s,",
                        GRID(state, nums, x, y),
                        (f & F_GT_UP)    ? "U" : "",
                        (f & F_GT_RIGHT) ? "R" : "",
                        (f & F_GT_DOWN)  ? "D" : "",
                        (f & F_GT_LEFT)  ? "L" : "");

            ret = sresize(ret, retlen + k + 1, char);
            strcpy(ret + retlen, buf);
            retlen += k;
        }
    }
    *aux = latin_desc(sq, params->order);

    free_game(state);
    sfree(sq);
    sfree(scratch);

    return ret;
}

static game_state *load_game(game_params *params, char *desc,
                             char **why_r)
{
    game_state *state = blank_game(params->order);
    char *p = desc;
    int i = 0, n, o = params->order, x, y;
    char *why = NULL;

    while (*p) {
        while (*p >= 'a' && *p <= 'z') {
            i += *p - 'a' + 1;
            p++;
        }
        if (i >= o*o) {
            why = "Too much data to fill grid"; goto fail;
        }

        if (*p < '0' && *p > '9') {
            why = "Expecting number in game description"; goto fail;
        }
        n = atoi(p);
        if (n < 0 || n > o) {
            why = "Out-of-range number in game description"; goto fail;
        }
        state->nums[i] = (digit)n;
        while (*p >= '0' && *p <= '9') p++; /* skip number */

        if (state->nums[i] != 0)
            state->flags[i] |= F_IMMUTABLE; /* === number set by game description */

        while (*p == 'U' || *p == 'R' || *p == 'D' || *p == 'L') {
            switch (*p) {
            case 'U': state->flags[i] |= F_GT_UP;    break;
            case 'R': state->flags[i] |= F_GT_RIGHT; break;
            case 'D': state->flags[i] |= F_GT_DOWN;  break;
            case 'L': state->flags[i] |= F_GT_LEFT;  break;
            default: why = "Expecting flag URDL in game description"; goto fail;
            }
            p++;
        }
        i++;
        if (i < o*o && *p != ',') {
            why = "Missing separator"; goto fail;
        }
        if (*p == ',') p++;
    }
    if (i < o*o) {
        why = "Not enough data to fill grid"; goto fail;
    }
    i = 0;
    for (y = 0; y < o; y++) {
        for (x = 0; x < o; x++) {
            for (n = 0; n < 4; n++) {
                if (GRID(state, flags, x, y) & gtthan[n].f) {
                    int nx = x + gtthan[n].dx;
                    int ny = y + gtthan[n].dy;
                    /* a flag must not point us off the grid. */
                    if (nx < 0 || ny < 0 || nx >= o || ny >= o) {
                        why = "Flags go off grid"; goto fail;
                    }
                    /* if one cell is GT another, the other must not also
                     * be GT the first. */
                    if (GRID(state, flags, nx, ny) & gtthan[n].fo) {
                        why = "Flags contradicting each other"; goto fail;
                    }
                }
            }
        }
    }

    return state;

fail:
    free_game(state);
    if (why_r) *why_r = why;
    return NULL;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    game_state *state = load_game(params, desc, NULL);
    if (!state) {
        assert("Unable to load ?validated game.");
        return NULL;
    }
    return state;
}

static char *validate_desc(game_params *params, char *desc)
{
    char *why = NULL;
    game_state *dummy = load_game(params, desc, &why);
    if (dummy) {
        free_game(dummy);
        assert(!why);
    } else
        assert(why);
    return why;
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    game_state *solved;
    int r;
    char *ret = NULL;

    if (aux) return dupstr(aux);

    solved = dup_game(state);
    for (r = 0; r < state->order*state->order; r++) {
        if (!(solved->flags[r] & F_IMMUTABLE))
            solved->nums[r] = 0;
    }
    r = solver_state(solved, DIFFCOUNT);
    if (r > 0) ret = latin_desc(solved->nums, solved->order);
    free_game(solved);
    return ret;
}

/* ----------------------------------------------------------
 * Game UI input processing.
 */

struct game_ui {
    int hx, hy, hpencil;        /* as for solo.c, highlight pos and type */
    int cx, cy;                 /* cursor position (-1,-1 for none) */
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = -1;
    ui->hpencil = 0;

    ui->cx = ui->cy = -1;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    /* See solo.c; if we were pencil-mode highlighting and
     * somehow a square has just been properly filled, cancel
     * pencil mode. */
    if (ui->hx >= 0 && ui->hy >= 0 && ui->hpencil &&
        GRID(newstate, nums, ui->hx, ui->hy) != 0) {
        ui->hx = ui->hy = -1;
    }
}

struct game_drawstate {
    int tilesize, order, started;
    digit *nums;                  /* copy of nums, o^2 */
    unsigned char *hints;                 /* copy of hints, o^3 */
    unsigned int *flags;        /* o^2 */

    int hx, hy, hpencil;
    int cx, cy;
    int hflash;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int ox, int oy, int button)
{
    int x = FROMCOORD(ox), y = FROMCOORD(oy), n;
    char buf[80];

    button &= ~MOD_MASK;

    if (x >= 0 && x < ds->order && y >= 0 && y < ds->order) {
        if (button == LEFT_BUTTON) {
            /* normal highlighting for non-immutable squares */
            if (GRID(state, flags, x, y) & F_IMMUTABLE)
                ui->hx = ui->hy = -1;
            else if (x == ui->hx && y == ui->hy && ui->hpencil == 0)
                ui->hx = ui->hy = -1;
            else {
                ui->hx = x; ui->hy = y; ui->hpencil = 0;
            }
            return "";
        }
        if (button == RIGHT_BUTTON) {
            /* pencil highlighting for non-filled squares */
            if (GRID(state, nums, x, y) != 0)
                ui->hx = ui->hy = -1;
            else if (x == ui->hx && y == ui->hy && ui->hpencil)
                ui->hx = ui->hy = -1;
            else {
                ui->hx = x; ui->hy = y; ui->hpencil = 1;
            }
            return "";
        }
    }
    if (button == 'H' || button == 'h')
        return dupstr("H");

    if (ui->hx != -1 && ui->hy != -1) {
        debug(("button %d, cbutton %d", button, (int)((char)button)));
        n = c2n(button, state->order);

        debug(("n %d, h (%d,%d) p %d flags 0x%x nums %d",
               n, ui->hx, ui->hy, ui->hpencil,
               GRID(state, flags, ui->hx, ui->hy),
               GRID(state, nums, ui->hx, ui->hy)));

        if (n < 0 || n > ds->order)
            return NULL;        /* out of range */
        if (GRID(state, flags, ui->hx, ui->hy) & F_IMMUTABLE)
            return NULL;        /* can't edit immutable square (!) */
        if (ui->hpencil && GRID(state, nums, ui->hx, ui->hy) > 0)
            return NULL;        /* can't change hints on filled square (!) */


        sprintf(buf, "%c%d,%d,%d",
                (char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        ui->hx = ui->hy = -1;

        return dupstr(buf);
    }
    return NULL;
}

static game_state *execute_move(game_state *state, char *move)
{
    game_state *ret = NULL;
    int x, y, n, i, rc;

    debug(("execute_move: %s", move));

    if ((move[0] == 'P' || move[0] == 'R') &&
        sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
        x >= 0 && x < state->order && y >= 0 && y < state->order &&
        n >= 0 && n <= state->order) {
        ret = dup_game(state);
        if (move[0] == 'P' && n > 0)
            HINT(ret, x, y, n-1) = !HINT(ret, x, y, n-1);
        else {
            GRID(ret, nums, x, y) = n;
            for (i = 0; i < state->order; i++)
                HINT(ret, x, y, i) = 0;

            /* real change to grid; check for completion */
            if (!ret->completed && check_complete(ret->nums, ret, 1) > 0)
                ret->completed = TRUE;
        }
        return ret;
    } else if (move[0] == 'S') {
        char *p;

        ret = dup_game(state);
        ret->completed = ret->cheated = TRUE;

        p = move+1;
        for (i = 0; i < state->order*state->order; i++) {
            n = c2n((int)*p, state->order);
            if (!*p || n <= 0 || n > state->order)
                goto badmove;
            ret->nums[i] = n;
            p++;
        }
        if (*p) goto badmove;
        rc = check_complete(ret->nums, ret, 1);
	assert(rc > 0);
        return ret;
    } else if (move[0] == 'H') {
        return solver_hint(state, NULL, DIFF_EASY, DIFF_EASY);
    }

badmove:
    if (ret) free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing/printing routines.
 */

#define DRAW_SIZE (TILE_SIZE*ds->order + GAP_SIZE*(ds->order-1) + BORDER*2)

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize, order; } ads, *ds = &ads;
    ads.tilesize = tilesize;
    ads.order = params->order;

    *x = *y = DRAW_SIZE;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_TEXT * 3 + i] = 0.0F;
        ret[COL_GRID * 3 + i] = 0.5F;
    }

    /* Lots of these were taken from solo.c. */
    ret[COL_GUESS * 3 + 0] = 0.0F;
    ret[COL_GUESS * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_GUESS * 3 + 2] = 0.0F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int o2 = state->order*state->order, o3 = o2*state->order;

    ds->tilesize = 0;
    ds->order = state->order;

    ds->nums = snewn(o2, digit);
    ds->hints = snewn(o3, unsigned char);
    ds->flags = snewn(o2, unsigned int);
    memset(ds->nums, 0, o2*sizeof(digit));
    memset(ds->hints, 0, o3);
    memset(ds->flags, 0, o2*sizeof(unsigned int));

    ds->hx = ds->hy = ds->cx = ds->cy = -1;
    ds->started = ds->hpencil = ds->hflash = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->nums);
    sfree(ds->hints);
    sfree(ds->flags);
    sfree(ds);
}

static void draw_gt(drawing *dr, int ox, int oy,
                    int dx1, int dy1, int dx2, int dy2, int col)
{
    draw_line(dr, ox, oy, ox+dx1, oy+dy1, col);
    draw_line(dr, ox+dx1, oy+dy1, ox+dx1+dx2, oy+dy1+dy2, col);
}

static void draw_gts(drawing *dr, game_drawstate *ds, int ox, int oy,
                     unsigned int f, int col, int needsupdate)
{
    int g = GAP_SIZE, g2 = (g+1)/2, g4 = (g+1)/4;

    if (f & F_GT_UP) {
        draw_gt(dr, ox+g2, oy-g4, g2, -g2, g2, g2,
               (f & F_ERROR_UP) ? COL_ERROR : col);
        if (needsupdate) draw_update(dr, ox, oy-g, TILE_SIZE, g);
    }
    if (f & F_GT_RIGHT) {
        draw_gt(dr, ox+TILE_SIZE+g4, oy+g2, g2, g2, -g2, g2,
                (f & F_ERROR_RIGHT) ? COL_ERROR : col);
        if (needsupdate) draw_update(dr, ox+TILE_SIZE, oy, g, TILE_SIZE);
    }
    if (f & F_GT_DOWN) {
        draw_gt(dr, ox+g2, oy+TILE_SIZE+g4, g2, g2, g2, -g2,
               (f & F_ERROR_DOWN) ? COL_ERROR : col);
        if (needsupdate) draw_update(dr, ox, oy+TILE_SIZE, TILE_SIZE, g);
    }
    if (f & F_GT_LEFT) {
        draw_gt(dr, ox-g4, oy+g2, -g2, g2, g2, g2,
                (f & F_ERROR_LEFT) ? COL_ERROR : col);
        if (needsupdate) draw_update(dr, ox-g, oy, g, TILE_SIZE);
    }
}

static void draw_furniture(drawing *dr, game_drawstate *ds, game_state *state,
                           game_ui *ui, int x, int y, int hflash)
{
    int ox = COORD(x), oy = COORD(y), bg, hon, con;
    unsigned int f = GRID(state, flags, x, y);

    bg = hflash ? COL_HIGHLIGHT : COL_BACKGROUND;

    hon = (x == ui->hx && y == ui->hy);
    con = (x == ui->cx && y == ui->cy);

    /* Clear square. */
    draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE,
              (hon && !ui->hpencil) ? COL_HIGHLIGHT : bg);

    /* Draw the highlight (pencil or full), if we're the highlight */
    if (hon && ui->hpencil) {
        int coords[6];
        coords[0] = ox;
        coords[1] = oy;
        coords[2] = ox + TILE_SIZE/2;
        coords[3] = oy;
        coords[4] = ox;
        coords[5] = oy + TILE_SIZE/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /* Draw the square outline (which is the cursor, if we're the cursor). */
    draw_rect_outline(dr, ox, oy, TILE_SIZE, TILE_SIZE,
                      con ? COL_GUESS : COL_GRID);

    draw_update(dr, ox, oy, TILE_SIZE, TILE_SIZE);

    /* Draw the GT signs. */
    draw_gts(dr, ds, ox, oy, f, COL_TEXT, 1);
}

static void draw_num(drawing *dr, game_drawstate *ds, int x, int y)
{
    int ox = COORD(x), oy = COORD(y);
    unsigned int f = GRID(ds,flags,x,y);
    char str[2];

    /* (can assume square has just been cleared) */

    /* Draw number, choosing appropriate colour */
    str[0] = n2c(GRID(ds, nums, x, y), ds->order);
    str[1] = '\0';
    draw_text(dr, ox + TILE_SIZE/2, oy + TILE_SIZE/2,
              FONT_VARIABLE, 3*TILE_SIZE/4, ALIGN_VCENTRE | ALIGN_HCENTRE,
              (f & F_IMMUTABLE) ? COL_TEXT : (f & F_ERROR) ? COL_ERROR : COL_GUESS, str);
}

static void draw_hints(drawing *dr, game_drawstate *ds, int x, int y)
{
    int ox = COORD(x), oy = COORD(y);
    int nhints, i, j, hw, hh, hmax, fontsz;
    char str[2];

    /* (can assume square has just been cleared) */

    /* Draw hints; steal ingenious algorithm (basically)
     * from solo.c:draw_number() */
    for (i = nhints = 0; i < ds->order; i++) {
        if (HINT(ds, x, y, i)) nhints++;
    }

    for (hw = 1; hw * hw < nhints; hw++);
    if (hw < 3) hw = 3;
    hh = (nhints + hw - 1) / hw;
    if (hh < 2) hh = 2;
    hmax = max(hw, hh);
    fontsz = TILE_SIZE/(hmax*(11-hmax)/8);

    for (i = j = 0; i < ds->order; i++) {
        if (HINT(ds,x,y,i)) {
            int hx = j % hw, hy = j / hw;

            str[0] = n2c(i+1, ds->order);
            str[1] = '\0';
            draw_text(dr,
                      ox + (4*hx+3) * TILE_SIZE / (4*hw+2),
                      oy + (4*hy+3) * TILE_SIZE / (4*hh+2),
                      FONT_VARIABLE, fontsz,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
            j++;
        }
    }
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int x, y, i, hchanged = 0, cchanged = 0, stale, hflash = 0;

    debug(("highlight old (%d,%d), new (%d,%d)", ds->hx, ds->hy, ui->hx, ui->hy));

    if (flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 || flashtime >= FLASH_TIME*2/3))
         hflash = 1;

    if (!ds->started) {
        draw_rect(dr, 0, 0, DRAW_SIZE, DRAW_SIZE, COL_BACKGROUND);
        draw_update(dr, 0, 0, DRAW_SIZE, DRAW_SIZE);
    }
    if (ds->hx != ui->hx || ds->hy != ui->hy || ds->hpencil != ui->hpencil)
        hchanged = 1;
    if (ds->cx != ui->cx || ds->cy != ui->cy)
        cchanged = 1;

    for (x = 0; x < ds->order; x++) {
        for (y = 0; y < ds->order; y++) {
            if (!ds->started)
                stale = 1;
            else if (hflash != ds->hflash)
                stale = 1;
            else
                stale = 0;

            if (hchanged) {
                if ((x == ui->hx && y == ui->hy) ||
                    (x == ds->hx && y == ds->hy))
                    stale = 1;
            }
            if (cchanged) {
                if ((x == ui->cx && y == ui->cy) ||
                    (x == ds->cx && y == ds->cy))
                    stale = 1;
            }

            if (GRID(state, nums, x, y) != GRID(ds, nums, x, y)) {
                GRID(ds, nums, x, y) = GRID(state, nums, x, y);
                stale = 1;
            }
            if (GRID(state, flags, x, y) != GRID(ds, flags, x, y)) {
                GRID(ds, flags, x, y) = GRID(state, flags, x, y);
                stale = 1;
            }
            if (GRID(ds, nums, x, y) == 0) {
                /* We're not a number square (therefore we might
                 * display hints); do we need to update? */
                for (i = 0; i < ds->order; i++) {
                    if (HINT(state, x, y, i) != HINT(ds, x, y, i)) {
                        HINT(ds, x, y, i) = HINT(state, x, y, i);
                        stale = 1;
                    }
                }
            }
            if (stale) {
                draw_furniture(dr, ds, state, ui, x, y, hflash);
                if (GRID(ds, nums, x, y) > 0)
                    draw_num(dr, ds, x, y);
                else
                    draw_hints(dr, ds, x, y);
            }
        }
    }
    ds->hx = ui->hx; ds->hy = ui->hy; ds->hpencil = ui->hpencil;
    ds->cx = ui->cx; ds->cy = ui->cy;
    ds->started = 1;
    ds->hflash = hflash;
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /* 10mm squares by default, roughly the same as Grauniad. */
    game_compute_size(params, 1000, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int x, y, o = state->order, ox, oy, n;
    char str[2];

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    print_line_width(dr, 2 * TILE_SIZE / 40);

    /* Squares, numbers, gt signs */
    for (y = 0; y < o; y++) {
        for (x = 0; x < o; x++) {
            ox = COORD(x); oy = COORD(y);
            n = GRID(state, nums, x, y);

            draw_rect_outline(dr, ox, oy, TILE_SIZE, TILE_SIZE, ink);

            str[0] = n ? n2c(n, state->order) : ' ';
            str[1] = '\0';
            draw_text(dr, ox + TILE_SIZE/2, oy + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
                      ink, str);

            draw_gts(dr, ds, ox, oy, GRID(state, flags, x, y), ink, 1);
        }
    }
}

/* ----------------------------------------------------------------------
 * Housekeeping.
 */

#ifdef COMBINED
#define thegame unequal
#endif

const struct game thegame = {
    "Unequal", "games.unequal", "unequal",
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
    TRUE, game_text_format,
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
    TRUE, FALSE, game_print_size, game_print,
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    REQUIRE_RBUTTON | REQUIRE_NUMPAD,  /* flags */
};

/* ----------------------------------------------------------------------
 * Standalone solver.
 */

#ifdef STANDALONE_SOLVER

#include <time.h>
#include <stdarg.h>

const char *quis = NULL;

#if 0 /* currently unused */

static void debug_printf(char *fmt, ...)
{
    char buf[4096];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    puts(buf);
    va_end(ap);
}

static void game_printf(game_state *state)
{
    char *dbg = game_text_format(state);
    printf("%s", dbg);
    sfree(dbg);
}

static void game_printf_wide(game_state *state)
{
    int x, y, i, n;

    for (y = 0; y < state->order; y++) {
        for (x = 0; x < state->order; x++) {
            n = GRID(state, nums, x, y);
            for (i = 0; i < state->order; i++) {
                if (n > 0)
                    printf("%c", n2c(n, state->order));
                else if (HINT(state, x, y, i))
                    printf("%c", n2c(i+1, state->order));
                else
                    printf(".");
            }
            printf(" ");
        }
        printf("\n");
    }
    printf("\n");
}

#endif

static void pdiff(int diff)
{
    if (diff == DIFF_IMPOSSIBLE)
        printf("Game is impossible.\n");
    else if (diff == DIFF_UNFINISHED)
        printf("Game has incomplete.\n");
    else if (diff == DIFF_AMBIGUOUS)
        printf("Game has multiple solutions.\n");
    else
        printf("Game has difficulty %s.\n", unequal_diffnames[diff]);
}

static int solve(game_params *p, char *desc, int debug)
{
    game_state *st = new_game(NULL, p, desc);
    int diff;

    solver_show_working = debug;
    game_debug(st);

    diff = solver_grid(st->nums, st->order, DIFF_RECURSIVE, (void*)st);
    if (debug) pdiff(diff);

    game_debug(st);
    free_game(st);
    return diff;
}

static void check(game_params *p)
{
    char *msg = validate_params(p, 1);
    if (msg) {
        fprintf(stderr, "%s: %s", quis, msg);
        exit(1);
    }
}

static int gen(game_params *p, random_state *rs, int debug)
{
    char *desc, *aux;
    int diff;

    check(p);

    solver_show_working = debug;
    desc = new_game_desc(p, rs, &aux, 0);
    diff = solve(p, desc, debug);
    sfree(aux);
    sfree(desc);

    return diff;
}

static void soak(game_params *p, random_state *rs)
{
    time_t tt_start, tt_now, tt_last;
    char *aux, *desc;
    game_state *st;
    int n = 0, neasy = 0, realdiff = p->diff;

    check(p);

    solver_show_working = 0;
    maxtries = 1;

    tt_start = tt_now = time(NULL);

    printf("Soak-generating a %dx%d grid, difficulty %s.\n",
           p->order, p->order, unequal_diffnames[p->diff]);

    while (1) {
        p->diff = realdiff;
        desc = new_game_desc(p, rs, &aux, 0);
        st = new_game(NULL, p, desc);
        solver_state(st, DIFF_RECURSIVE);
        free_game(st);
        sfree(aux);
        sfree(desc);

        n++;
        if (realdiff != p->diff) neasy++;

        tt_last = time(NULL);
        if (tt_last > tt_now) {
            tt_now = tt_last;
            printf("%d total, %3.1f/s; %d/%2.1f%% easy, %3.1f/s good.\n",
                   n, (double)n / ((double)tt_now - tt_start),
                   neasy, (double)neasy*100.0/(double)n,
                   (double)(n - neasy) / ((double)tt_now - tt_start));
        }
    }
}

static void usage_exit(const char *msg)
{
    if (msg)
        fprintf(stderr, "%s: %s\n", quis, msg);
    fprintf(stderr, "Usage: %s [--seed SEED] --soak <params> | [game_id [game_id ...]]\n", quis);
    exit(1);
}

int main(int argc, const char *argv[])
{
    random_state *rs;
    time_t seed = time(NULL);
    int do_soak = 0, diff;

    game_params *p;

    maxtries = 50;

    quis = argv[0];
    while (--argc > 0) {
        const char *p = *++argv;
        if (!strcmp(p, "--soak"))
            do_soak = 1;
        else if (!strcmp(p, "--seed")) {
            if (argc == 0)
                usage_exit("--seed needs an argument");
            seed = (time_t)atoi(*++argv);
            argc--;
        } else if (*p == '-')
            usage_exit("unrecognised option");
        else
            break;
    }
    rs = random_new((void*)&seed, sizeof(time_t));

    if (do_soak == 1) {
        if (argc != 1) usage_exit("only one argument for --soak");
        p = default_params();
        decode_params(p, *argv);
        soak(p, rs);
    } else if (argc > 0) {
        int i;
        for (i = 0; i < argc; i++) {
            const char *id = *argv++;
            char *desc = strchr(id, ':'), *err;
            p = default_params();
            if (desc) {
                *desc++ = '\0';
                decode_params(p, id);
                err = validate_desc(p, desc);
                if (err) {
                    fprintf(stderr, "%s: %s\n", quis, err);
                    exit(1);
                }
                solve(p, desc, 1);
            } else {
                decode_params(p, id);
                diff = gen(p, rs, 1);
            }
        }
    } else {
        while(1) {
            p = default_params();
            p->order = random_upto(rs, 7) + 3;
            p->diff = random_upto(rs, 4);
            diff = gen(p, rs, 0);
            pdiff(diff);
        }
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
