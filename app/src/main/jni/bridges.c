/*
 * bridges.c: Implementation of the Nikoli game 'Bridges'.
 *
 * Things still to do:
 *
 *  - The solver's algorithmic design is not really ideal. It makes
 *    use of the same data representation as gameplay uses, which
 *    often looks like a tempting reuse of code but isn't always a
 *    good idea. In this case, it's unpleasant that each edge of the
 *    graph ends up represented as multiple squares on a grid, with
 *    flags indicating when edges and non-edges cross; that's useful
 *    when the result can be directly translated into positions of
 *    graphics on the display, but in purely internal work it makes
 *    even simple manipulations during solving more painful than they
 *    should be, and complex ones have no choice but to modify the
 *    data structures temporarily, test things, and put them back. I
 *    envisage a complete solver rewrite along the following lines:
 *     + We have a collection of vertices (islands) and edges
 *       (potential bridge locations, i.e. pairs of horizontal or
 *       vertical islands with no other island in between).
 *     + Each edge has an associated list of edges that cross it, and
 *       hence with which it is mutually exclusive.
 *     + For each edge, we track the min and max number of bridges we
 *       currently think possible.
 *     + For each vertex, we track the number of _liberties_ it has,
 *       i.e. its clue number minus the min bridge count for each edge
 *       out of it.
 *     + We also maintain a dsf that identifies sets of vertices which
 *       are connected components of the puzzle so far, and for each
 *       equivalence class we track the total number of liberties for
 *       that component. (The dsf mechanism will also already track
 *       the size of each component, i.e. number of islands.)
 *     + So incrementing the min for an edge requires processing along
 *       the lines of:
 *        - set the max for all edges crossing that one to zero
 *        - decrement the liberty count for the vertex at each end,
 *          and also for each vertex's equivalence class (NB they may
 *          be the same class)
 *        - unify the two equivalence classes if they're not already,
 *          and if so, set the liberty count for the new class to be
 *          the sum of the previous two.
 *     + Decrementing the max is much easier, however.
 *     + With this data structure the really fiddly stuff in stage3()
 *       becomes more or less trivial, because it's now a quick job to
 *       find out whether an island would form an isolated subgraph if
 *       connected to a given subset of its neighbours:
 *        - identify the connected components containing the test
 *          vertex and its putative new neighbours (but be careful not
 *          to count a component more than once if two or more of the
 *          vertices involved are already in the same one)
 *        - find the sum of those components' liberty counts, and also
 *          the total number of islands involved
 *        - if the total liberty count of the connected components is
 *          exactly equal to twice the number of edges we'd be adding
 *          (of course each edge destroys two liberties, one at each
 *          end) then these components would become a subgraph with
 *          zero liberties if connected together.
 *        - therefore, if that subgraph also contains fewer than the
 *          total number of islands, it's disallowed.
 *        - As mentioned in stage3(), once we've identified such a
 *          disallowed pattern, we have two choices for what to do
 *          with it: if the candidate set of neighbours has size 1 we
 *          can reduce the max for the edge to that one neighbour,
 *          whereas if its complement has size 1 we can increase the
 *          min for the edge to the _omitted_ neighbour.
 *
 *  - write a recursive solver?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/* Turn this on for hints about which lines are considered possibilities. */
#undef DRAW_GRID

/* --- structures for params, state, etc. --- */

#define MAX_BRIDGES     4

#define PREFERRED_TILE_SIZE 24
#define TILE_SIZE       (ds->tilesize)
#define BORDER          (TILE_SIZE / 2)

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define FLASH_TIME 0.50F

enum {
    COL_BACKGROUND,
    COL_FOREGROUND,
    COL_HIGHLIGHT, COL_LOWLIGHT,
    COL_SELECTED, COL_MARK,
    COL_HINT, COL_GRID,
    COL_WARNING,
    COL_CURSOR,
    NCOLOURS
};

struct game_params {
    int w, h, maxb;
    int islands, expansion;     /* %age of island squares, %age chance of expansion */
    int allowloops, difficulty;
};

/* general flags used by all structs */
#define G_ISLAND        0x0001
#define G_LINEV         0x0002     /* contains a vert. line */
#define G_LINEH         0x0004     /* contains a horiz. line (mutex with LINEV) */
#define G_LINE          (G_LINEV|G_LINEH)
#define G_MARKV         0x0008
#define G_MARKH         0x0010
#define G_MARK          (G_MARKV|G_MARKH)
#define G_NOLINEV       0x0020
#define G_NOLINEH       0x0040
#define G_NOLINE        (G_NOLINEV|G_NOLINEH)

/* flags used by the error checker */
#define G_WARN          0x0080

/* flags used by the solver etc. */
#define G_SWEEP         0x1000

#define G_FLAGSH        (G_LINEH|G_MARKH|G_NOLINEH)
#define G_FLAGSV        (G_LINEV|G_MARKV|G_NOLINEV)

typedef unsigned int grid_type; /* change me later if we invent > 16 bits of flags. */

struct solver_state {
    int *dsf, *comptspaces;
    int *tmpdsf, *tmpcompspaces;
    int refcount;
};

/* state->gridi is an optimisation; it stores the pointer to the island
 * structs indexed by (x,y). It's not strictly necessary (we could use
 * find234 instead), but Purify showed that board generation (mostly the solver)
 * was spending 60% of its time in find234. */

struct surrounds { /* cloned from lightup.c */
    struct { int x, y, dx, dy, off; } points[4];
    int npoints, nislands;
};

struct island {
  game_state *state;
  int x, y, count;
  struct surrounds adj;
};

struct game_state {
    int w, h, completed, solved, allowloops, maxb;
    grid_type *grid, *scratch;
    struct island *islands;
    int n_islands, n_islands_alloc;
    game_params params; /* used by the aux solver. */
#define N_WH_ARRAYS 5
    char *wha, *possv, *possh, *lines, *maxv, *maxh;
    struct island **gridi;
    struct solver_state *solver; /* refcounted */
};

#define GRIDSZ(s) ((s)->w * (s)->h * sizeof(grid_type))

#define INGRID(s,x,y) ((x) >= 0 && (x) < (s)->w && (y) >= 0 && (y) < (s)->h)

#define DINDEX(x,y) ((y)*state->w + (x))

#define INDEX(s,g,x,y) ((s)->g[(y)*((s)->w) + (x)])
#define IDX(s,g,i) ((s)->g[(i)])
#define GRID(s,x,y) INDEX(s,grid,x,y)
#define SCRATCH(s,x,y) INDEX(s,scratch,x,y)
#define POSSIBLES(s,dx,x,y) ((dx) ? (INDEX(s,possh,x,y)) : (INDEX(s,possv,x,y)))
#define MAXIMUM(s,dx,x,y) ((dx) ? (INDEX(s,maxh,x,y)) : (INDEX(s,maxv,x,y)))

#define GRIDCOUNT(s,x,y,f) ((GRID(s,x,y) & (f)) ? (INDEX(s,lines,x,y)) : 0)

#define WITHIN2(x,min,max) (((x) < (min)) ? 0 : (((x) > (max)) ? 0 : 1))
#define WITHIN(x,min,max) ((min) > (max) ? \
                           WITHIN2(x,max,min) : WITHIN2(x,min,max))

/* --- island struct and tree support functions --- */

#define ISLAND_ORTH(is,j,f,df) \
    (is->f + (is->adj.points[(j)].off*is->adj.points[(j)].df))

#define ISLAND_ORTHX(is,j) ISLAND_ORTH(is,j,x,dx)
#define ISLAND_ORTHY(is,j) ISLAND_ORTH(is,j,y,dy)

static void fixup_islands_for_realloc(game_state *state)
{
    int i;

    for (i = 0; i < state->w*state->h; i++) state->gridi[i] = NULL;
    for (i = 0; i < state->n_islands; i++) {
        struct island *is = &state->islands[i];
        is->state = state;
        INDEX(state, gridi, is->x, is->y) = is;
    }
}

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    int x, y, len, nl;
    char *ret, *p;
    struct island *is;
    grid_type grid;

    len = (state->h) * (state->w+1) + 1;
    ret = snewn(len, char);
    p = ret;

    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            grid = GRID(state,x,y);
            nl = INDEX(state,lines,x,y);
            is = INDEX(state, gridi, x, y);
            if (is) {
                *p++ = '0' + is->count;
            } else if (grid & G_LINEV) {
                *p++ = (nl > 1) ? '"' : (nl == 1) ? '|' : '!'; /* gaah, want a double-bar. */
            } else if (grid & G_LINEH) {
                *p++ = (nl > 1) ? '=' : (nl == 1) ? '-' : '~';
            } else {
                *p++ = '.';
            }
        }
        *p++ = '\n';
    }
    *p++ = '\0';

    assert(p - ret == len);
    return ret;
}

static void debug_state(game_state *state)
{
    char *textversion = game_text_format(state);
    debug(("%s", textversion));
    sfree(textversion);
}

/*static void debug_possibles(game_state *state)
{
    int x, y;
    debug(("possh followed by possv\n"));
    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            debug(("%d", POSSIBLES(state, 1, x, y)));
        }
        debug((" "));
        for (x = 0; x < state->w; x++) {
            debug(("%d", POSSIBLES(state, 0, x, y)));
        }
        debug(("\n"));
    }
    debug(("\n"));
        for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            debug(("%d", MAXIMUM(state, 1, x, y)));
        }
        debug((" "));
        for (x = 0; x < state->w; x++) {
            debug(("%d", MAXIMUM(state, 0, x, y)));
        }
        debug(("\n"));
    }
    debug(("\n"));
}*/

static void island_set_surrounds(struct island *is)
{
    assert(INGRID(is->state,is->x,is->y));
    is->adj.npoints = is->adj.nislands = 0;
#define ADDPOINT(cond,ddx,ddy) do {\
    if (cond) { \
        is->adj.points[is->adj.npoints].x = is->x+(ddx); \
        is->adj.points[is->adj.npoints].y = is->y+(ddy); \
        is->adj.points[is->adj.npoints].dx = (ddx); \
        is->adj.points[is->adj.npoints].dy = (ddy); \
        is->adj.points[is->adj.npoints].off = 0; \
        is->adj.npoints++; \
    } } while(0)
    ADDPOINT(is->x > 0,                -1,  0);
    ADDPOINT(is->x < (is->state->w-1), +1,  0);
    ADDPOINT(is->y > 0,                 0, -1);
    ADDPOINT(is->y < (is->state->h-1),  0, +1);
}

static void island_find_orthogonal(struct island *is)
{
    /* fills in the rest of the 'surrounds' structure, assuming
     * all other islands are now in place. */
    int i, x, y, dx, dy, off;

    is->adj.nislands = 0;
    for (i = 0; i < is->adj.npoints; i++) {
        dx = is->adj.points[i].dx;
        dy = is->adj.points[i].dy;
        x = is->x + dx;
        y = is->y + dy;
        off = 1;
        is->adj.points[i].off = 0;
        while (INGRID(is->state, x, y)) {
            if (GRID(is->state, x, y) & G_ISLAND) {
                is->adj.points[i].off = off;
                is->adj.nislands++;
                /*debug(("island (%d,%d) has orth is. %d*(%d,%d) away at (%d,%d).\n",
                       is->x, is->y, off, dx, dy,
                       ISLAND_ORTHX(is,i), ISLAND_ORTHY(is,i)));*/
                goto foundisland;
            }
            off++; x += dx; y += dy;
        }
foundisland:
        ;
    }
}

static int island_hasbridge(struct island *is, int direction)
{
    int x = is->adj.points[direction].x;
    int y = is->adj.points[direction].y;
    grid_type gline = is->adj.points[direction].dx ? G_LINEH : G_LINEV;

    if (GRID(is->state, x, y) & gline) return 1;
    return 0;
}

static struct island *island_find_connection(struct island *is, int adjpt)
{
    struct island *is_r;

    assert(adjpt < is->adj.npoints);
    if (!is->adj.points[adjpt].off) return NULL;
    if (!island_hasbridge(is, adjpt)) return NULL;

    is_r = INDEX(is->state, gridi,
                 ISLAND_ORTHX(is, adjpt), ISLAND_ORTHY(is, adjpt));
    assert(is_r);

    return is_r;
}

static struct island *island_add(game_state *state, int x, int y, int count)
{
    struct island *is;
    int realloced = 0;

    assert(!(GRID(state,x,y) & G_ISLAND));
    GRID(state,x,y) |= G_ISLAND;

    state->n_islands++;
    if (state->n_islands > state->n_islands_alloc) {
        state->n_islands_alloc = state->n_islands * 2;
        state->islands =
            sresize(state->islands, state->n_islands_alloc, struct island);
        realloced = 1;
    }
    is = &state->islands[state->n_islands-1];

    memset(is, 0, sizeof(struct island));
    is->state = state;
    is->x = x;
    is->y = y;
    is->count = count;
    island_set_surrounds(is);

    if (realloced)
        fixup_islands_for_realloc(state);
    else
        INDEX(state, gridi, x, y) = is;

    return is;
}


/* n = -1 means 'flip NOLINE flags [and set line to 0].' */
static void island_join(struct island *i1, struct island *i2, int n, int is_max)
{
    game_state *state = i1->state;
    int s, e, x, y;

    assert(i1->state == i2->state);
    assert(n >= -1 && n <= i1->state->maxb);

    if (i1->x == i2->x) {
        x = i1->x;
        if (i1->y < i2->y) {
            s = i1->y+1; e = i2->y-1;
        } else {
            s = i2->y+1; e = i1->y-1;
        }
        for (y = s; y <= e; y++) {
            if (is_max) {
                INDEX(state,maxv,x,y) = n;
            } else {
                if (n < 0) {
                    GRID(state,x,y) ^= G_NOLINEV;
                } else if (n == 0) {
                    GRID(state,x,y) &= ~G_LINEV;
                } else {
                    GRID(state,x,y) |= G_LINEV;
                    INDEX(state,lines,x,y) = n;
                }
            }
        }
    } else if (i1->y == i2->y) {
        y = i1->y;
        if (i1->x < i2->x) {
            s = i1->x+1; e = i2->x-1;
        } else {
            s = i2->x+1; e = i1->x-1;
        }
        for (x = s; x <= e; x++) {
            if (is_max) {
                INDEX(state,maxh,x,y) = n;
            } else {
                if (n < 0) {
                    GRID(state,x,y) ^= G_NOLINEH;
                } else if (n == 0) {
                    GRID(state,x,y) &= ~G_LINEH;
                } else {
                    GRID(state,x,y) |= G_LINEH;
                    INDEX(state,lines,x,y) = n;
                }
            }
        }
    } else {
        assert(!"island_join: islands not orthogonal.");
    }
}

/* Counts the number of bridges currently attached to the island. */
static int island_countbridges(struct island *is)
{
    int i, c = 0;

    for (i = 0; i < is->adj.npoints; i++) {
        c += GRIDCOUNT(is->state,
                       is->adj.points[i].x, is->adj.points[i].y,
                       is->adj.points[i].dx ? G_LINEH : G_LINEV);
    }
    /*debug(("island count for (%d,%d) is %d.\n", is->x, is->y, c));*/
    return c;
}

static int island_adjspace(struct island *is, int marks, int missing,
                           int direction)
{
    int x, y, poss, curr, dx;
    grid_type gline, mline;

    x = is->adj.points[direction].x;
    y = is->adj.points[direction].y;
    dx = is->adj.points[direction].dx;
    gline = dx ? G_LINEH : G_LINEV;

    if (marks) {
        mline = dx ? G_MARKH : G_MARKV;
        if (GRID(is->state,x,y) & mline) return 0;
    }
    poss = POSSIBLES(is->state, dx, x, y);
    poss = min(poss, missing);

    curr = GRIDCOUNT(is->state, x, y, gline);
    poss = min(poss, MAXIMUM(is->state, dx, x, y) - curr);

    return poss;
}

/* Counts the number of bridge spaces left around the island;
 * expects the possibles to be up-to-date. */
static int island_countspaces(struct island *is, int marks)
{
    int i, c = 0, missing;

    missing = is->count - island_countbridges(is);
    if (missing < 0) return 0;

    for (i = 0; i < is->adj.npoints; i++) {
        c += island_adjspace(is, marks, missing, i);
    }
    return c;
}

static int island_isadj(struct island *is, int direction)
{
    int x, y;
    grid_type gline, mline;

    x = is->adj.points[direction].x;
    y = is->adj.points[direction].y;

    mline = is->adj.points[direction].dx ? G_MARKH : G_MARKV;
    gline = is->adj.points[direction].dx ? G_LINEH : G_LINEV;
    if (GRID(is->state, x, y) & mline) {
        /* If we're marked (i.e. the thing to attach to is complete)
         * only count an adjacency if we're already attached. */
        return GRIDCOUNT(is->state, x, y, gline);
    } else {
        /* If we're unmarked, count possible adjacency iff it's
         * flagged as POSSIBLE. */
        return POSSIBLES(is->state, is->adj.points[direction].dx, x, y);
    }
    return 0;
}

/* Counts the no. of possible adjacent islands (including islands
 * we're already connected to). */
static int island_countadj(struct island *is)
{
    int i, nadj = 0;

    for (i = 0; i < is->adj.npoints; i++) {
        if (island_isadj(is, i)) nadj++;
    }
    return nadj;
}

static void island_togglemark(struct island *is)
{
    int i, j, x, y, o;
    struct island *is_loop;

    /* mark the island... */
    GRID(is->state, is->x, is->y) ^= G_MARK;

    /* ...remove all marks on non-island squares... */
    for (x = 0; x < is->state->w; x++) {
        for (y = 0; y < is->state->h; y++) {
            if (!(GRID(is->state, x, y) & G_ISLAND))
                GRID(is->state, x, y) &= ~G_MARK;
        }
    }

    /* ...and add marks to squares around marked islands. */
    for (i = 0; i < is->state->n_islands; i++) {
        is_loop = &is->state->islands[i];
        if (!(GRID(is_loop->state, is_loop->x, is_loop->y) & G_MARK))
            continue;

        for (j = 0; j < is_loop->adj.npoints; j++) {
            /* if this direction takes us to another island, mark all
             * squares between the two islands. */
            if (!is_loop->adj.points[j].off) continue;
            assert(is_loop->adj.points[j].off > 1);
            for (o = 1; o < is_loop->adj.points[j].off; o++) {
                GRID(is_loop->state,
                     is_loop->x + is_loop->adj.points[j].dx*o,
                     is_loop->y + is_loop->adj.points[j].dy*o) |=
                    is_loop->adj.points[j].dy ? G_MARKV : G_MARKH;
            }
        }
    }
}

static int island_impossible(struct island *is, int strict)
{
    int curr = island_countbridges(is), nspc = is->count - curr, nsurrspc;
    int i, poss;
    struct island *is_orth;

    if (nspc < 0) {
        debug(("island at (%d,%d) impossible because full.\n", is->x, is->y));
        return 1;        /* too many bridges */
    } else if ((curr + island_countspaces(is, 0)) < is->count) {
        debug(("island at (%d,%d) impossible because not enough spaces.\n", is->x, is->y));
        return 1;        /* impossible to create enough bridges */
    } else if (strict && curr < is->count) {
        debug(("island at (%d,%d) impossible because locked.\n", is->x, is->y));
        return 1;        /* not enough bridges and island is locked */
    }

    /* Count spaces in surrounding islands. */
    nsurrspc = 0;
    for (i = 0; i < is->adj.npoints; i++) {
        int ifree, dx = is->adj.points[i].dx;

        if (!is->adj.points[i].off) continue;
        poss = POSSIBLES(is->state, dx,
                         is->adj.points[i].x, is->adj.points[i].y);
        if (poss == 0) continue;
        is_orth = INDEX(is->state, gridi,
                        ISLAND_ORTHX(is,i), ISLAND_ORTHY(is,i));
        assert(is_orth);

        ifree = is_orth->count - island_countbridges(is_orth);
        if (ifree > 0) {
	    /*
	     * ifree is the number of bridges unfilled in the other
	     * island, which is clearly an upper bound on the number
	     * of extra bridges this island may run to it.
	     *
	     * Another upper bound is the number of bridges unfilled
	     * on the specific line between here and there. We must
	     * take the minimum of both.
	     */
	    int bmax = MAXIMUM(is->state, dx,
			       is->adj.points[i].x, is->adj.points[i].y);
	    int bcurr = GRIDCOUNT(is->state,
				  is->adj.points[i].x, is->adj.points[i].y,
				  dx ? G_LINEH : G_LINEV);
	    assert(bcurr <= bmax);
            nsurrspc += min(ifree, bmax - bcurr);
	}
    }
    if (nsurrspc < nspc) {
        debug(("island at (%d,%d) impossible: surr. islands %d spc, need %d.\n",
               is->x, is->y, nsurrspc, nspc));
        return 1;       /* not enough spaces around surrounding islands to fill this one. */
    }

    return 0;
}

/* --- Game parameter functions --- */

#define DEFAULT_PRESET 0

const struct game_params bridges_presets[] = {
  { 7, 7, 2, 30, 10, 1, 0 },
  { 7, 7, 2, 30, 10, 1, 1 },
  { 7, 7, 2, 30, 10, 1, 2 },
  { 10, 10, 2, 30, 10, 1, 0 },
  { 10, 10, 2, 30, 10, 1, 1 },
  { 10, 10, 2, 30, 10, 1, 2 },
  { 15, 15, 2, 30, 10, 1, 0 },
  { 15, 15, 2, 30, 10, 1, 1 },
  { 15, 15, 2, 30, 10, 1, 2 },
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = bridges_presets[DEFAULT_PRESET];

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(bridges_presets))
        return FALSE;

    ret = default_params();
    *ret = bridges_presets[i];
    *params = ret;

    sprintf(buf, "%dx%d %s", ret->w, ret->h,
            ret->difficulty == 0 ? _("Easy") :
            ret->difficulty == 1 ? _("Medium") : _("Hard"));
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
    *ret = *params;		       /* structure copy */
    return ret;
}

#define EATNUM(x) do { \
    (x) = atoi(string); \
    while (*string && isdigit((unsigned char)*string)) string++; \
} while(0)

static void decode_params(game_params *params, char const *string)
{
    EATNUM(params->w);
    params->h = params->w;
    if (*string == 'x') {
        string++;
        EATNUM(params->h);
    }
    if (*string == 'i') {
        string++;
        EATNUM(params->islands);
    }
    if (*string == 'e') {
        string++;
        EATNUM(params->expansion);
    }
    if (*string == 'm') {
        string++;
        EATNUM(params->maxb);
    }
    params->allowloops = 1;
    if (*string == 'L') {
        string++;
        params->allowloops = 0;
    }
    if (*string == 'd') {
        string++;
        EATNUM(params->difficulty);
    }
}

static char *encode_params(const game_params *params, int full)
{
    char buf[80];

    if (full) {
        sprintf(buf, "%dx%di%de%dm%d%sd%d",
                params->w, params->h, params->islands, params->expansion,
                params->maxb, params->allowloops ? "" : "L",
                params->difficulty);
    } else {
        sprintf(buf, "%dx%dm%d%s", params->w, params->h,
                params->maxb, params->allowloops ? "" : "L");
    }
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(8, config_item);

    ret[0].name = _("Width");
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = _("Height");
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = _("Difficulty");
    ret[2].type = C_CHOICES;
    ret[2].sval = _(":Easy:Medium:Hard");
    ret[2].ival = params->difficulty;

    ret[3].name = _("Allow loops");
    ret[3].type = C_BOOLEAN;
    ret[3].sval = NULL;
    ret[3].ival = params->allowloops;

    ret[4].name = _("Max. bridges per direction");
    ret[4].type = C_CHOICES;
    ret[4].sval = ":1:2:3:4"; /* keep up-to-date with MAX_BRIDGES */
    ret[4].ival = params->maxb - 1;

    ret[5].name = _("%age of island squares");
    ret[5].type = C_CHOICES;
    ret[5].sval = ":5%:10%:15%:20%:25%:30%";
    ret[5].ival = (params->islands / 5)-1;

    ret[6].name = _("Expansion factor (%age)");
    ret[6].type = C_CHOICES;
    ret[6].sval = ":0%:10%:20%:30%:40%:50%:60%:70%:80%:90%:100%";
    ret[6].ival = params->expansion / 10;

    ret[7].name = NULL;
    ret[7].type = C_END;
    ret[7].sval = NULL;
    ret[7].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w          = atoi(cfg[0].sval);
    ret->h          = atoi(cfg[1].sval);
    ret->difficulty = cfg[2].ival;
    ret->allowloops = cfg[3].ival;
    ret->maxb       = cfg[4].ival + 1;
    ret->islands    = (cfg[5].ival + 1) * 5;
    ret->expansion  = cfg[6].ival * 10;

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->w < 3 || params->h < 3)
        return _("Width and height must be at least 3");
    if (params->maxb < 1 || params->maxb > MAX_BRIDGES)
        return _("Too many bridges.");
    if (full) {
        if (params->islands <= 0 || params->islands > 30)
            return _("%age of island squares must be between 1% and 30%");
        if (params->expansion < 0 || params->expansion > 100)
            return _("Expansion factor must be between 0 and 100");
    }
    return NULL;
}

/* --- Game encoding and differences --- */

static char *encode_game(game_state *state)
{
    char *ret, *p;
    int wh = state->w*state->h, run, x, y;
    struct island *is;

    ret = snewn(wh + 1, char);
    p = ret;
    run = 0;
    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            is = INDEX(state, gridi, x, y);
            if (is) {
                if (run) {
                    *p++ = ('a'-1) + run;
                    run = 0;
                }
                if (is->count < 10)
                    *p++ = '0' + is->count;
                else
                    *p++ = 'A' + (is->count - 10);
            } else {
                if (run == 26) {
                    *p++ = ('a'-1) + run;
                    run = 0;
                }
                run++;
            }
        }
    }
    if (run) {
        *p++ = ('a'-1) + run;
        run = 0;
    }
    *p = '\0';
    assert(p - ret <= wh);

    return ret;
}

static char *game_state_diff(const game_state *src, const game_state *dest)
{
    int movesize = 256, movelen = 0;
    char *move = snewn(movesize, char), buf[80];
    int i, d, x, y, len;
    grid_type gline, nline;
    struct island *is_s, *is_d, *is_orth;

#define APPEND do {                                     \
    if (movelen + len >= movesize) {                    \
        movesize = movelen + len + 256;                 \
        move = sresize(move, movesize, char);           \
    }                                                   \
    strcpy(move + movelen, buf);                        \
    movelen += len;                                     \
} while(0)

    move[movelen++] = 'S';
    move[movelen] = '\0';

    assert(src->n_islands == dest->n_islands);

    for (i = 0; i < src->n_islands; i++) {
        is_s = &src->islands[i];
        is_d = &dest->islands[i];
        assert(is_s->x == is_d->x);
        assert(is_s->y == is_d->y);
        assert(is_s->adj.npoints == is_d->adj.npoints); /* more paranoia */

        for (d = 0; d < is_s->adj.npoints; d++) {
            if (is_s->adj.points[d].dx == -1 ||
                is_s->adj.points[d].dy == -1) continue;

            x = is_s->adj.points[d].x;
            y = is_s->adj.points[d].y;
            gline = is_s->adj.points[d].dx ? G_LINEH : G_LINEV;
            nline = is_s->adj.points[d].dx ? G_NOLINEH : G_NOLINEV;
            is_orth = INDEX(dest, gridi,
                            ISLAND_ORTHX(is_d, d), ISLAND_ORTHY(is_d, d));

            if (GRIDCOUNT(src, x, y, gline) != GRIDCOUNT(dest, x, y, gline)) {
                assert(is_orth);
                len = sprintf(buf, ";L%d,%d,%d,%d,%d",
                              is_s->x, is_s->y, is_orth->x, is_orth->y,
                              GRIDCOUNT(dest, x, y, gline));
                APPEND;
            }
            if ((GRID(src,x,y) & nline) != (GRID(dest, x, y) & nline)) {
                assert(is_orth);
                len = sprintf(buf, ";N%d,%d,%d,%d",
                              is_s->x, is_s->y, is_orth->x, is_orth->y);
                APPEND;
            }
        }
        if ((GRID(src, is_s->x, is_s->y) & G_MARK) !=
            (GRID(dest, is_d->x, is_d->y) & G_MARK)) {
            len = sprintf(buf, ";M%d,%d", is_s->x, is_s->y);
            APPEND;
        }
    }
    return move;
}

/* --- Game setup and solving utilities --- */

/* This function is optimised; a Quantify showed that lots of grid-generation time
 * (>50%) was spent in here. Hence the IDX() stuff. */

static void map_update_possibles(game_state *state)
{
    int x, y, s, e, bl, i, np, maxb, w = state->w, idx;
    struct island *is_s = NULL, *is_f = NULL;

    /* Run down vertical stripes [un]setting possv... */
    for (x = 0; x < state->w; x++) {
        idx = x;
        s = e = -1;
        bl = 0;
        maxb = state->params.maxb;     /* placate optimiser */
        /* Unset possible flags until we find an island. */
        for (y = 0; y < state->h; y++) {
            is_s = IDX(state, gridi, idx);
            if (is_s) {
                maxb = is_s->count;
                break;
            }

            IDX(state, possv, idx) = 0;
            idx += w;
        }
        for (; y < state->h; y++) {
            maxb = min(maxb, IDX(state, maxv, idx));
            is_f = IDX(state, gridi, idx);
            if (is_f) {
                assert(is_s);
                np = min(maxb, is_f->count);

                if (s != -1) {
                    for (i = s; i <= e; i++) {
                        INDEX(state, possv, x, i) = bl ? 0 : np;
                    }
                }
                s = y+1;
                bl = 0;
                is_s = is_f;
                maxb = is_s->count;
            } else {
                e = y;
                if (IDX(state,grid,idx) & (G_LINEH|G_NOLINEV)) bl = 1;
            }
            idx += w;
        }
        if (s != -1) {
            for (i = s; i <= e; i++)
                INDEX(state, possv, x, i) = 0;
        }
    }

    /* ...and now do horizontal stripes [un]setting possh. */
    /* can we lose this clone'n'hack? */
    for (y = 0; y < state->h; y++) {
        idx = y*w;
        s = e = -1;
        bl = 0;
        maxb = state->params.maxb;     /* placate optimiser */
        for (x = 0; x < state->w; x++) {
            is_s = IDX(state, gridi, idx);
            if (is_s) {
                maxb = is_s->count;
                break;
            }

            IDX(state, possh, idx) = 0;
            idx += 1;
        }
        for (; x < state->w; x++) {
            maxb = min(maxb, IDX(state, maxh, idx));
            is_f = IDX(state, gridi, idx);
            if (is_f) {
                assert(is_s);
                np = min(maxb, is_f->count);

                if (s != -1) {
                    for (i = s; i <= e; i++) {
                        INDEX(state, possh, i, y) = bl ? 0 : np;
                    }
                }
                s = x+1;
                bl = 0;
                is_s = is_f;
                maxb = is_s->count;
            } else {
                e = x;
                if (IDX(state,grid,idx) & (G_LINEV|G_NOLINEH)) bl = 1;
            }
            idx += 1;
        }
        if (s != -1) {
            for (i = s; i <= e; i++)
                INDEX(state, possh, i, y) = 0;
        }
    }
}

static void map_count(game_state *state)
{
    int i, n, ax, ay;
    grid_type flag, grid;
    struct island *is;

    for (i = 0; i < state->n_islands; i++) {
        is = &state->islands[i];
        is->count = 0;
        for (n = 0; n < is->adj.npoints; n++) {
            ax = is->adj.points[n].x;
            ay = is->adj.points[n].y;
            flag = (ax == is->x) ? G_LINEV : G_LINEH;
            grid = GRID(state,ax,ay);
            if (grid & flag) {
                is->count += INDEX(state,lines,ax,ay);
            }
        }
    }
}

static void map_find_orthogonal(game_state *state)
{
    int i;

    for (i = 0; i < state->n_islands; i++) {
        island_find_orthogonal(&state->islands[i]);
    }
}

static int grid_degree(game_state *state, int x, int y, int *nx_r, int *ny_r)
{
    grid_type grid = SCRATCH(state, x, y), gline = grid & G_LINE;
    struct island *is;
    int x1, y1, x2, y2, c = 0, i, nx, ny;

    nx = ny = -1; /* placate optimiser */
    is = INDEX(state, gridi, x, y);
    if (is) {
        for (i = 0; i < is->adj.npoints; i++) {
            gline = is->adj.points[i].dx ? G_LINEH : G_LINEV;
            if (SCRATCH(state,
                        is->adj.points[i].x,
                        is->adj.points[i].y) & gline) {
                nx = is->adj.points[i].x;
                ny = is->adj.points[i].y;
                c++;
            }
        }
    } else if (gline) {
        if (gline & G_LINEV) {
            x1 = x2 = x;
            y1 = y-1; y2 = y+1;
        } else {
            x1 = x-1; x2 = x+1;
            y1 = y2 = y;
        }
        /* Non-island squares with edges in should never be pointing off the
         * edge of the grid. */
        assert(INGRID(state, x1, y1));
        assert(INGRID(state, x2, y2));
        if (SCRATCH(state, x1, y1) & (gline | G_ISLAND)) {
            nx = x1; ny = y1; c++;
        }
        if (SCRATCH(state, x2, y2) & (gline | G_ISLAND)) {
            nx = x2; ny = y2; c++;
        }
    }
    if (c == 1) {
        assert(nx != -1 && ny != -1); /* paranoia */
        *nx_r = nx; *ny_r = ny;
    }
    return c;
}

static int map_hasloops(game_state *state, int mark)
{
    int x, y, ox, oy, nx = 0, ny = 0, loop = 0;

    memcpy(state->scratch, state->grid, GRIDSZ(state));

    /* This algorithm is actually broken; if there are two loops connected
     * by bridges this will also highlight bridges. The correct algorithm
     * uses a dsf and a two-pass edge-detection algorithm (see check_correct
     * in slant.c); this is BALGE for now, especially since disallow-loops
     * is not the default for this puzzle. If we want to fix this later then
     * copy the alg in slant.c to the empty statement in map_group. */

    /* Remove all 1-degree edges. */
    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            ox = x; oy = y;
            while (grid_degree(state, ox, oy, &nx, &ny) == 1) {
                /*debug(("hasloops: removing 1-degree at (%d,%d).\n", ox, oy));*/
                SCRATCH(state, ox, oy) &= ~(G_LINE|G_ISLAND);
                ox = nx; oy = ny;
            }
        }
    }
    /* Mark any remaining edges as G_WARN, if required. */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (GRID(state,x,y) & G_ISLAND) continue;

            if (SCRATCH(state, x, y) & G_LINE) {
                if (mark) {
                    /*debug(("hasloops: marking loop square at (%d,%d).\n",
                           x, y));*/
                    GRID(state,x,y) |= G_WARN;
                    loop = 1;
                } else
                    return 1; /* short-cut as soon as we find one */
            } else {
                if (mark)
                    GRID(state,x,y) &= ~G_WARN;
            }
        }
    }
    return loop;
}

static void map_group(game_state *state)
{
    int i, wh = state->w*state->h, d1, d2;
    int x, y, x2, y2;
    int *dsf = state->solver->dsf;
    struct island *is, *is_join;

    /* Initialise dsf. */
    dsf_init(dsf, wh);

    /* For each island, find connected islands right or down
     * and merge the dsf for the island squares as well as the
     * bridge squares. */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            GRID(state,x,y) &= ~(G_SWEEP|G_WARN); /* for group_full. */

            is = INDEX(state, gridi, x, y);
            if (!is) continue;
            d1 = DINDEX(x,y);
            for (i = 0; i < is->adj.npoints; i++) {
                /* only want right/down */
                if (is->adj.points[i].dx == -1 ||
                    is->adj.points[i].dy == -1) continue;

                is_join = island_find_connection(is, i);
                if (!is_join) continue;

                d2 = DINDEX(is_join->x, is_join->y);
                if (dsf_canonify(dsf,d1) == dsf_canonify(dsf,d2)) {
                    ; /* we have a loop. See comment in map_hasloops. */
                    /* However, we still want to merge all squares joining
                     * this side-that-makes-a-loop. */
                }
                /* merge all squares between island 1 and island 2. */
                for (x2 = x; x2 <= is_join->x; x2++) {
                    for (y2 = y; y2 <= is_join->y; y2++) {
                        d2 = DINDEX(x2,y2);
                        if (d1 != d2) dsf_merge(dsf,d1,d2);
                    }
                }
            }
        }
    }
}

static int map_group_check(game_state *state, int canon, int warn,
                           int *nislands_r)
{
    int *dsf = state->solver->dsf, nislands = 0;
    int x, y, i, allfull = 1;
    struct island *is;

    for (i = 0; i < state->n_islands; i++) {
        is = &state->islands[i];
        if (dsf_canonify(dsf, DINDEX(is->x,is->y)) != canon) continue;

        GRID(state, is->x, is->y) |= G_SWEEP;
        nislands++;
        if (island_countbridges(is) != is->count)
            allfull = 0;
    }
    if (warn && allfull && nislands != state->n_islands) {
        /* we're full and this island group isn't the whole set.
         * Mark all squares with this dsf canon as ERR. */
        for (x = 0; x < state->w; x++) {
            for (y = 0; y < state->h; y++) {
                if (dsf_canonify(dsf, DINDEX(x,y)) == canon) {
                    GRID(state,x,y) |= G_WARN;
                }
            }
        }

    }
    if (nislands_r) *nislands_r = nislands;
    return allfull;
}

static int map_group_full(game_state *state, int *ngroups_r)
{
    int *dsf = state->solver->dsf, ngroups = 0;
    int i, anyfull = 0;
    struct island *is;

    /* NB this assumes map_group (or sth else) has cleared G_SWEEP. */

    for (i = 0; i < state->n_islands; i++) {
        is = &state->islands[i];
        if (GRID(state,is->x,is->y) & G_SWEEP) continue;

        ngroups++;
        if (map_group_check(state, dsf_canonify(dsf, DINDEX(is->x,is->y)),
                            1, NULL))
            anyfull = 1;
    }

    *ngroups_r = ngroups;
    return anyfull;
}

static int map_check(game_state *state)
{
    int ngroups;

    /* Check for loops, if necessary. */
    if (!state->allowloops) {
        if (map_hasloops(state, 1))
            return 0;
    }

    /* Place islands into island groups and check for early
     * satisfied-groups. */
    map_group(state); /* clears WARN and SWEEP */
    if (map_group_full(state, &ngroups)) {
        if (ngroups == 1) return 1;
    }
    return 0;
}

static void map_clear(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            /* clear most flags; might want to be slightly more careful here. */
            GRID(state,x,y) &= G_ISLAND;
        }
    }
}

static void solve_join(struct island *is, int direction, int n, int is_max)
{
    struct island *is_orth;
    int d1, d2, *dsf = is->state->solver->dsf;
    game_state *state = is->state; /* for DINDEX */

    is_orth = INDEX(is->state, gridi,
                    ISLAND_ORTHX(is, direction),
                    ISLAND_ORTHY(is, direction));
    assert(is_orth);
    /*debug(("...joining (%d,%d) to (%d,%d) with %d bridge(s).\n",
           is->x, is->y, is_orth->x, is_orth->y, n));*/
    island_join(is, is_orth, n, is_max);

    if (n > 0 && !is_max) {
        d1 = DINDEX(is->x, is->y);
        d2 = DINDEX(is_orth->x, is_orth->y);
        if (dsf_canonify(dsf, d1) != dsf_canonify(dsf, d2))
            dsf_merge(dsf, d1, d2);
    }
}

static int solve_fillone(struct island *is)
{
    int i, nadded = 0;

    debug(("solve_fillone for island (%d,%d).\n", is->x, is->y));

    for (i = 0; i < is->adj.npoints; i++) {
        if (island_isadj(is, i)) {
            if (island_hasbridge(is, i)) {
                /* already attached; do nothing. */;
            } else {
                solve_join(is, i, 1, 0);
                nadded++;
            }
        }
    }
    return nadded;
}

static int solve_fill(struct island *is)
{
    /* for each unmarked adjacent, make sure we convert every possible bridge
     * to a real one, and then work out the possibles afresh. */
    int i, nnew, ncurr, nadded = 0, missing;

    debug(("solve_fill for island (%d,%d).\n", is->x, is->y));

    missing = is->count - island_countbridges(is);
    if (missing < 0) return 0;

    /* very like island_countspaces. */
    for (i = 0; i < is->adj.npoints; i++) {
        nnew = island_adjspace(is, 1, missing, i);
        if (nnew) {
            ncurr = GRIDCOUNT(is->state,
                              is->adj.points[i].x, is->adj.points[i].y,
                              is->adj.points[i].dx ? G_LINEH : G_LINEV);

            solve_join(is, i, nnew + ncurr, 0);
            nadded += nnew;
        }
    }
    return nadded;
}

static int solve_island_stage1(struct island *is, int *didsth_r)
{
    int bridges = island_countbridges(is);
    int nspaces = island_countspaces(is, 1);
    int nadj = island_countadj(is);
    int didsth = 0;

    assert(didsth_r);

    /*debug(("island at (%d,%d) filled %d/%d (%d spc) nadj %d\n",
           is->x, is->y, bridges, is->count, nspaces, nadj));*/
    if (bridges > is->count) {
        /* We only ever add bridges when we're sure they fit, or that's
         * the only place they can go. If we've added bridges such that
         * another island has become wrong, the puzzle must not have had
         * a solution. */
        debug(("...island at (%d,%d) is overpopulated!\n", is->x, is->y));
        return 0;
    } else if (bridges == is->count) {
        /* This island is full. Make sure it's marked (and update
         * possibles if we did). */
        if (!(GRID(is->state, is->x, is->y) & G_MARK)) {
            debug(("...marking island (%d,%d) as full.\n", is->x, is->y));
            island_togglemark(is);
            didsth = 1;
        }
    } else if (GRID(is->state, is->x, is->y) & G_MARK) {
        debug(("...island (%d,%d) is marked but unfinished!\n",
               is->x, is->y));
        return 0; /* island has been marked unfinished; no solution from here. */
    } else {
        /* This is the interesting bit; we try and fill in more information
         * about this island. */
        if (is->count == bridges + nspaces) {
            if (solve_fill(is) > 0) didsth = 1;
        } else if (is->count > ((nadj-1) * is->state->maxb)) {
            /* must have at least one bridge in each possible direction. */
            if (solve_fillone(is) > 0) didsth = 1;
        }
    }
    if (didsth) {
        map_update_possibles(is->state);
        *didsth_r = 1;
    }
    return 1;
}

/* returns non-zero if a new line here would cause a loop. */
static int solve_island_checkloop(struct island *is, int direction)
{
    struct island *is_orth;
    int *dsf = is->state->solver->dsf, d1, d2;
    game_state *state = is->state;

    if (is->state->allowloops) return 0; /* don't care anyway */
    if (island_hasbridge(is, direction)) return 0; /* already has a bridge */
    if (island_isadj(is, direction) == 0) return 0; /* no adj island */

    is_orth = INDEX(is->state, gridi,
                    ISLAND_ORTHX(is,direction),
                    ISLAND_ORTHY(is,direction));
    if (!is_orth) return 0;

    d1 = DINDEX(is->x, is->y);
    d2 = DINDEX(is_orth->x, is_orth->y);
    if (dsf_canonify(dsf, d1) == dsf_canonify(dsf, d2)) {
        /* two islands are connected already; don't join them. */
        return 1;
    }
    return 0;
}

static int solve_island_stage2(struct island *is, int *didsth_r)
{
    int added = 0, removed = 0, navail = 0, nadj, i;

    assert(didsth_r);

    for (i = 0; i < is->adj.npoints; i++) {
        if (solve_island_checkloop(is, i)) {
            debug(("removing possible loop at (%d,%d) direction %d.\n",
                   is->x, is->y, i));
            solve_join(is, i, -1, 0);
            map_update_possibles(is->state);
            removed = 1;
        } else {
            navail += island_isadj(is, i);
            /*debug(("stage2: navail for (%d,%d) direction (%d,%d) is %d.\n",
                   is->x, is->y,
                   is->adj.points[i].dx, is->adj.points[i].dy,
                   island_isadj(is, i)));*/
        }
    }

    /*debug(("island at (%d,%d) navail %d: checking...\n", is->x, is->y, navail));*/

    for (i = 0; i < is->adj.npoints; i++) {
        if (!island_hasbridge(is, i)) {
            nadj = island_isadj(is, i);
            if (nadj > 0 && (navail - nadj) < is->count) {
                /* we couldn't now complete the island without at
                 * least one bridge here; put it in. */
                /*debug(("nadj %d, navail %d, is->count %d.\n",
                       nadj, navail, is->count));*/
                debug(("island at (%d,%d) direction (%d,%d) must have 1 bridge\n",
                       is->x, is->y,
                       is->adj.points[i].dx, is->adj.points[i].dy));
                solve_join(is, i, 1, 0);
                added = 1;
                /*debug_state(is->state);
                debug_possibles(is->state);*/
            }
        }
    }
    if (added) map_update_possibles(is->state);
    if (added || removed) *didsth_r = 1;
    return 1;
}

static int solve_island_subgroup(struct island *is, int direction)
{
    struct island *is_join;
    int nislands, *dsf = is->state->solver->dsf;
    game_state *state = is->state;

    debug(("..checking subgroups.\n"));

    /* if is isn't full, return 0. */
    if (island_countbridges(is) < is->count) {
        debug(("...orig island (%d,%d) not full.\n", is->x, is->y));
        return 0;
    }

    if (direction >= 0) {
        is_join = INDEX(state, gridi,
                        ISLAND_ORTHX(is, direction),
                        ISLAND_ORTHY(is, direction));
        assert(is_join);

        /* if is_join isn't full, return 0. */
        if (island_countbridges(is_join) < is_join->count) {
            debug(("...dest island (%d,%d) not full.\n",
                   is_join->x, is_join->y));
            return 0;
        }
    }

    /* Check group membership for is->dsf; if it's full return 1. */
    if (map_group_check(state, dsf_canonify(dsf, DINDEX(is->x,is->y)),
                        0, &nislands)) {
        if (nislands < state->n_islands) {
            /* we have a full subgroup that isn't the whole set.
             * This isn't allowed. */
            debug(("island at (%d,%d) makes full subgroup, disallowing.\n",
                   is->x, is->y));
            return 1;
        } else {
            debug(("...has finished puzzle.\n"));
        }
    }
    return 0;
}

static int solve_island_impossible(game_state *state)
{
    struct island *is;
    int i;

    /* If any islands are impossible, return 1. */
    for (i = 0; i < state->n_islands; i++) {
        is = &state->islands[i];
        if (island_impossible(is, 0)) {
            debug(("island at (%d,%d) has become impossible, disallowing.\n",
                   is->x, is->y));
            return 1;
        }
    }
    return 0;
}

/* Bear in mind that this function is really rather inefficient. */
static int solve_island_stage3(struct island *is, int *didsth_r)
{
    int i, n, x, y, missing, spc, curr, maxb, didsth = 0;
    int wh = is->state->w * is->state->h;
    struct solver_state *ss = is->state->solver;

    assert(didsth_r);

    missing = is->count - island_countbridges(is);
    if (missing <= 0) return 1;

    for (i = 0; i < is->adj.npoints; i++) {
        x = is->adj.points[i].x;
        y = is->adj.points[i].y;
        spc = island_adjspace(is, 1, missing, i);
        if (spc == 0) continue;

        curr = GRIDCOUNT(is->state, x, y,
                         is->adj.points[i].dx ? G_LINEH : G_LINEV);
        debug(("island at (%d,%d) s3, trying %d - %d bridges.\n",
               is->x, is->y, curr+1, curr+spc));

        /* Now we know that this island could have more bridges,
         * to bring the total from curr+1 to curr+spc. */
        maxb = -1;
        /* We have to squirrel the dsf away and restore it afterwards;
         * it is additive only, and can't be removed from. */
        memcpy(ss->tmpdsf, ss->dsf, wh*sizeof(int));
        for (n = curr+1; n <= curr+spc; n++) {
            solve_join(is, i, n, 0);
            map_update_possibles(is->state);

            if (solve_island_subgroup(is, i) ||
                solve_island_impossible(is->state)) {
                maxb = n-1;
                debug(("island at (%d,%d) d(%d,%d) new max of %d bridges:\n",
                       is->x, is->y,
                       is->adj.points[i].dx, is->adj.points[i].dy,
                       maxb));
                break;
            }
        }
        solve_join(is, i, curr, 0); /* put back to before. */
        memcpy(ss->dsf, ss->tmpdsf, wh*sizeof(int));

        if (maxb != -1) {
            /*debug_state(is->state);*/
            if (maxb == 0) {
                debug(("...adding NOLINE.\n"));
                solve_join(is, i, -1, 0); /* we can't have any bridges here. */
            } else {
                debug(("...setting maximum\n"));
                solve_join(is, i, maxb, 1);
            }
            didsth = 1;
        }
        map_update_possibles(is->state);
    }

    for (i = 0; i < is->adj.npoints; i++) {
        /*
         * Now check to see if any currently empty direction must have
         * at least one bridge in order to avoid forming an isolated
         * subgraph. This differs from the check above in that it
         * considers multiple target islands. For example:
         *
         *   2   2    4
         *                                  1     3     2
         *       3
         *                                        4
         *
         * The example on the left can be handled by the above loop:
         * it will observe that connecting the central 2 twice to the
         * left would form an isolated subgraph, and hence it will
         * restrict that 2 to at most one bridge in that direction.
         * But the example on the right won't be handled by that loop,
         * because the deduction requires us to imagine connecting the
         * 3 to _both_ the 1 and 2 at once to form an isolated
         * subgraph.
         *
         * This pass is necessary _as well_ as the above one, because
         * neither can do the other's job. In the left one,
         * restricting the direction which _would_ cause trouble can
         * be done even if it's not yet clear which of the remaining
         * directions has to have a compensatory bridge; whereas the
         * pass below that can handle the right-hand example does need
         * to know what direction to point the necessary bridge in.
         *
         * Neither pass can handle the most general case, in which we
         * observe that an arbitrary subset of an island's neighbours
         * would form an isolated subgraph with it if it connected
         * maximally to them, and hence that at least one bridge must
         * point to some neighbour outside that subset but we don't
         * know which neighbour. To handle that, we'd have to have a
         * richer data format for the solver, which could cope with
         * recording the idea that at least one of two edges must have
         * a bridge.
         */
        int got = 0;
        int before[4];
        int j;

        spc = island_adjspace(is, 1, missing, i);
        if (spc == 0) continue;

        for (j = 0; j < is->adj.npoints; j++)
            before[j] = GRIDCOUNT(is->state,
                                  is->adj.points[j].x,
                                  is->adj.points[j].y,
                                  is->adj.points[j].dx ? G_LINEH : G_LINEV);
        if (before[i] != 0) continue;  /* this idea is pointless otherwise */

        memcpy(ss->tmpdsf, ss->dsf, wh*sizeof(int));

        for (j = 0; j < is->adj.npoints; j++) {
            spc = island_adjspace(is, 1, missing, j);
            if (spc == 0) continue;
            if (j == i) continue;
            solve_join(is, j, before[j] + spc, 0);
        }
        map_update_possibles(is->state);

        if (solve_island_subgroup(is, -1))
            got = 1;

        for (j = 0; j < is->adj.npoints; j++)
            solve_join(is, j, before[j], 0);
        memcpy(ss->dsf, ss->tmpdsf, wh*sizeof(int));

        if (got) {
            debug(("island at (%d,%d) must connect in direction (%d,%d) to"
                   " avoid full subgroup.\n",
                   is->x, is->y, is->adj.points[i].dx, is->adj.points[i].dy));
            solve_join(is, i, 1, 0);
            didsth = 1;
        }

        map_update_possibles(is->state);
    }

    if (didsth) *didsth_r = didsth;
    return 1;
}

#define CONTINUE_IF_FULL do {                           \
if (GRID(state, is->x, is->y) & G_MARK) {            \
    /* island full, don't try fixing it */           \
    continue;                                        \
} } while(0)

static int solve_sub(game_state *state, int difficulty, int depth)
{
    struct island *is;
    int i, didsth;

    while (1) {
        didsth = 0;

        /* First island iteration: things we can work out by looking at
         * properties of the island as a whole. */
        for (i = 0; i < state->n_islands; i++) {
            is = &state->islands[i];
            if (!solve_island_stage1(is, &didsth)) return 0;
        }
        if (didsth) continue;
        else if (difficulty < 1) break;

        /* Second island iteration: thing we can work out by looking at
         * properties of individual island connections. */
        for (i = 0; i < state->n_islands; i++) {
            is = &state->islands[i];
            CONTINUE_IF_FULL;
            if (!solve_island_stage2(is, &didsth)) return 0;
        }
        if (didsth) continue;
        else if (difficulty < 2) break;

        /* Third island iteration: things we can only work out by looking
         * at groups of islands. */
        for (i = 0; i < state->n_islands; i++) {
            is = &state->islands[i];
            if (!solve_island_stage3(is, &didsth)) return 0;
        }
        if (didsth) continue;
        else if (difficulty < 3) break;

        /* If we can be bothered, write a recursive solver to finish here. */
        break;
    }
    if (map_check(state)) return 1; /* solved it */
    return 0;
}

static void solve_for_hint(game_state *state)
{
    map_group(state);
    solve_sub(state, 10, 0);
}

static int solve_from_scratch(game_state *state, int difficulty)
{
    map_clear(state);
    map_group(state);
    map_update_possibles(state);
    return solve_sub(state, difficulty, 0);
}

/* --- New game functions --- */

static game_state *new_state(const game_params *params)
{
    game_state *ret = snew(game_state);
    int wh = params->w * params->h, i;

    ret->w = params->w;
    ret->h = params->h;
    ret->allowloops = params->allowloops;
    ret->maxb = params->maxb;
    ret->params = *params;

    ret->grid = snewn(wh, grid_type);
    memset(ret->grid, 0, GRIDSZ(ret));
    ret->scratch = snewn(wh, grid_type);
    memset(ret->scratch, 0, GRIDSZ(ret));

    ret->wha = snewn(wh*N_WH_ARRAYS, char);
    memset(ret->wha, 0, wh*N_WH_ARRAYS*sizeof(char));

    ret->possv = ret->wha;
    ret->possh = ret->wha + wh;
    ret->lines = ret->wha + wh*2;
    ret->maxv = ret->wha + wh*3;
    ret->maxh = ret->wha + wh*4;

    memset(ret->maxv, ret->maxb, wh*sizeof(char));
    memset(ret->maxh, ret->maxb, wh*sizeof(char));

    ret->islands = NULL;
    ret->n_islands = 0;
    ret->n_islands_alloc = 0;

    ret->gridi = snewn(wh, struct island *);
    for (i = 0; i < wh; i++) ret->gridi[i] = NULL;

    ret->solved = ret->completed = 0;

    ret->solver = snew(struct solver_state);
    ret->solver->dsf = snew_dsf(wh);
    ret->solver->tmpdsf = snewn(wh, int);

    ret->solver->refcount = 1;

    return ret;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int wh = state->w*state->h;

    ret->w = state->w;
    ret->h = state->h;
    ret->allowloops = state->allowloops;
    ret->maxb = state->maxb;
    ret->params = state->params;

    ret->grid = snewn(wh, grid_type);
    memcpy(ret->grid, state->grid, GRIDSZ(ret));
    ret->scratch = snewn(wh, grid_type);
    memcpy(ret->scratch, state->scratch, GRIDSZ(ret));

    ret->wha = snewn(wh*N_WH_ARRAYS, char);
    memcpy(ret->wha, state->wha, wh*N_WH_ARRAYS*sizeof(char));

    ret->possv = ret->wha;
    ret->possh = ret->wha + wh;
    ret->lines = ret->wha + wh*2;
    ret->maxv = ret->wha + wh*3;
    ret->maxh = ret->wha + wh*4;

    ret->islands = snewn(state->n_islands, struct island);
    memcpy(ret->islands, state->islands, state->n_islands * sizeof(struct island));
    ret->n_islands = ret->n_islands_alloc = state->n_islands;

    ret->gridi = snewn(wh, struct island *);
    fixup_islands_for_realloc(ret);

    ret->solved = state->solved;
    ret->completed = state->completed;

    ret->solver = state->solver;
    ret->solver->refcount++;

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->solver->refcount <= 0) {
        sfree(state->solver->dsf);
        sfree(state->solver->tmpdsf);
        sfree(state->solver);
    }

    sfree(state->islands);
    sfree(state->gridi);

    sfree(state->wha);

    sfree(state->scratch);
    sfree(state->grid);
    sfree(state);
}

#define MAX_NEWISLAND_TRIES     50
#define MIN_SENSIBLE_ISLANDS    3

#define ORDER(a,b) do { if (a < b) { int tmp=a; int a=b; int b=tmp; } } while(0)

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    game_state *tobuild  = NULL;
    int i, j, wh = params->w * params->h, x, y, dx, dy;
    int minx, miny, maxx, maxy, joinx, joiny, newx, newy, diffx, diffy;
    int ni_req = max((params->islands * wh) / 100, MIN_SENSIBLE_ISLANDS), ni_curr, ni_bad;
    struct island *is, *is2;
    char *ret;
    unsigned int echeck;

    /* pick a first island position randomly. */
generate:
    if (tobuild) free_game(tobuild);
    tobuild = new_state(params);

    x = random_upto(rs, params->w);
    y = random_upto(rs, params->h);
    island_add(tobuild, x, y, 0);
    ni_curr = 1;
    ni_bad = 0;
    debug(("Created initial island at (%d,%d).\n", x, y));

    while (ni_curr < ni_req) {
        /* Pick a random island to try and extend from. */
        i = random_upto(rs, tobuild->n_islands);
        is = &tobuild->islands[i];

        /* Pick a random direction to extend in. */
        j = random_upto(rs, is->adj.npoints);
        dx = is->adj.points[j].x - is->x;
        dy = is->adj.points[j].y - is->y;

        /* Find out limits of where we could put a new island. */
        joinx = joiny = -1;
        minx = is->x + 2*dx; miny = is->y + 2*dy; /* closest is 2 units away. */
        x = is->x+dx; y = is->y+dy;
        if (GRID(tobuild,x,y) & (G_LINEV|G_LINEH)) {
            /* already a line next to the island, continue. */
            goto bad;
        }
        while (1) {
            if (x < 0 || x >= params->w || y < 0 || y >= params->h) {
                /* got past the edge; put a possible at the island
                 * and exit. */
                maxx = x-dx; maxy = y-dy;
                goto foundmax;
            }
            if (GRID(tobuild,x,y) & G_ISLAND) {
                /* could join up to an existing island... */
                joinx = x; joiny = y;
                /* ... or make a new one 2 spaces away. */
                maxx = x - 2*dx; maxy = y - 2*dy;
                goto foundmax;
            } else if (GRID(tobuild,x,y) & (G_LINEV|G_LINEH)) {
                /* could make a new one 1 space away from the line. */
                maxx = x - dx; maxy = y - dy;
                goto foundmax;
            }
            x += dx; y += dy;
        }

foundmax:
        debug(("Island at (%d,%d) with d(%d,%d) has new positions "
               "(%d,%d) -> (%d,%d), join (%d,%d).\n",
               is->x, is->y, dx, dy, minx, miny, maxx, maxy, joinx, joiny));
        /* Now we know where we could either put a new island
         * (between min and max), or (if loops are allowed) could join on
         * to an existing island (at join). */
        if (params->allowloops && joinx != -1 && joiny != -1) {
            if (random_upto(rs, 100) < (unsigned long)params->expansion) {
                is2 = INDEX(tobuild, gridi, joinx, joiny);
                debug(("Joining island at (%d,%d) to (%d,%d).\n",
                       is->x, is->y, is2->x, is2->y));
                goto join;
            }
        }
        diffx = (maxx - minx) * dx;
        diffy = (maxy - miny) * dy;
        if (diffx < 0 || diffy < 0)  goto bad;
        if (random_upto(rs,100) < (unsigned long)params->expansion) {
            newx = maxx; newy = maxy;
            debug(("Creating new island at (%d,%d) (expanded).\n", newx, newy));
        } else {
            newx = minx + random_upto(rs,diffx+1)*dx;
            newy = miny + random_upto(rs,diffy+1)*dy;
            debug(("Creating new island at (%d,%d).\n", newx, newy));
        }
        /* check we're not next to island in the other orthogonal direction. */
        if ((INGRID(tobuild,newx+dy,newy+dx) && (GRID(tobuild,newx+dy,newy+dx) & G_ISLAND)) ||
            (INGRID(tobuild,newx-dy,newy-dx) && (GRID(tobuild,newx-dy,newy-dx) & G_ISLAND))) {
            debug(("New location is adjacent to island, skipping.\n"));
            goto bad;
        }
        is2 = island_add(tobuild, newx, newy, 0);
        /* Must get is again at this point; the array might have
         * been realloced by island_add... */
        is = &tobuild->islands[i]; /* ...but order will not change. */

        ni_curr++; ni_bad = 0;
join:
        island_join(is, is2, random_upto(rs, tobuild->maxb)+1, 0);
        debug_state(tobuild);
        continue;

bad:
        ni_bad++;
        if (ni_bad > MAX_NEWISLAND_TRIES) {
            debug(("Unable to create any new islands after %d tries; "
                   "created %d [%d%%] (instead of %d [%d%%] requested).\n",
                   MAX_NEWISLAND_TRIES,
                   ni_curr, ni_curr * 100 / wh,
                   ni_req, ni_req * 100 / wh));
            goto generated;
        }
    }

generated:
    if (ni_curr == 1) {
        debug(("Only generated one island (!), retrying.\n"));
        goto generate;
    }
    /* Check we have at least one island on each extremity of the grid. */
    echeck = 0;
    for (x = 0; x < params->w; x++) {
        if (INDEX(tobuild, gridi, x, 0))           echeck |= 1;
        if (INDEX(tobuild, gridi, x, params->h-1)) echeck |= 2;
    }
    for (y = 0; y < params->h; y++) {
        if (INDEX(tobuild, gridi, 0,           y)) echeck |= 4;
        if (INDEX(tobuild, gridi, params->w-1, y)) echeck |= 8;
    }
    if (echeck != 15) {
        debug(("Generated grid doesn't fill to sides, retrying.\n"));
        goto generate;
    }

    map_count(tobuild);
    map_find_orthogonal(tobuild);

    if (params->difficulty > 0) {
        if ((ni_curr > MIN_SENSIBLE_ISLANDS) &&
            (solve_from_scratch(tobuild, params->difficulty-1) > 0)) {
            debug(("Grid is solvable at difficulty %d (too easy); retrying.\n",
                   params->difficulty-1));
            goto generate;
        }
    }

    if (solve_from_scratch(tobuild, params->difficulty) == 0) {
        debug(("Grid not solvable at difficulty %d, (too hard); retrying.\n",
               params->difficulty));
        goto generate;
    }

    /* ... tobuild is now solved. We rely on this making the diff for aux. */
    debug_state(tobuild);
    ret = encode_game(tobuild);
    {
        game_state *clean = dup_game(tobuild);
        map_clear(clean);
        map_update_possibles(clean);
        *aux = game_state_diff(clean, tobuild);
        free_game(clean);
    }
    free_game(tobuild);

    return ret;
}

static char *validate_desc(const game_params *params, const char *desc)
{
    int i, wh = params->w * params->h;

    for (i = 0; i < wh; i++) {
        if (*desc >= '1' && *desc <= '9')
            /* OK */;
        else if (*desc >= 'a' && *desc <= 'z')
            i += *desc - 'a'; /* plus the i++ */
        else if (*desc >= 'A' && *desc <= 'G')
            /* OK */;
        else if (*desc == 'V' || *desc == 'W' ||
                 *desc == 'X' || *desc == 'Y' ||
                 *desc == 'H' || *desc == 'I' ||
                 *desc == 'J' || *desc == 'K')
            /* OK */;
        else if (!*desc)
            return _("Game description shorter than expected");
        else
            return _("Game description contains unexpected character");
        desc++;
    }
    if (*desc || i > wh)
        return _("Game description longer than expected");

    return NULL;
}

static game_state *new_game_sub(const game_params *params, const char *desc)
{
    game_state *state = new_state(params);
    int x, y, run = 0;

    debug(("new_game[_sub]: desc = '%s'.\n", desc));

    for (y = 0; y < params->h; y++) {
        for (x = 0; x < params->w; x++) {
            char c = '\0';

            if (run == 0) {
                c = *desc++;
                assert(c != 'S');
                if (c >= 'a' && c <= 'z')
                    run = c - 'a' + 1;
            }

            if (run > 0) {
                c = 'S';
                run--;
            }

            switch (c) {
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                island_add(state, x, y, (c - '0'));
                break;

            case 'A': case 'B': case 'C': case 'D':
            case 'E': case 'F': case 'G':
                island_add(state, x, y, (c - 'A') + 10);
                break;

            case 'S':
                /* empty square */
                break;

            default:
                assert(!"Malformed desc.");
                break;
            }
        }
    }
    if (*desc) assert(!"Over-long desc.");

    map_find_orthogonal(state);
    map_update_possibles(state);

    return state;
}

#ifdef ANDROID
static void android_request_keys(const game_params *params)
{
    android_keys2("GH", "L", ANDROID_ARROWS_LEFT_RIGHT);
}
#endif

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    return new_game_sub(params, desc);
}

struct game_ui {
    int dragx_src, dragy_src;   /* source; -1 means no drag */
    int dragx_dst, dragy_dst;   /* src's closest orth island. */
    grid_type todraw;
    int dragging, drag_is_noline, nlines;

    int cur_x, cur_y, cur_visible;      /* cursor position */
    int show_hints;
};

static char *ui_cancel_drag(game_ui *ui)
{
    ui->dragx_src = ui->dragy_src = -1;
    ui->dragx_dst = ui->dragy_dst = -1;
    ui->dragging = 0;
    return "";
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui_cancel_drag(ui);
    ui->cur_x = state->islands[0].x;
    ui->cur_y = state->islands[0].y;
    ui->cur_visible = 0;
    ui->show_hints = 0;
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
    ui->cur_visible = visible;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
#ifdef ANDROID
    if (newstate->completed && ! newstate->solved && oldstate && ! oldstate->completed) android_completed();
#endif
}

struct game_drawstate {
    int tilesize;
    int w, h;
    unsigned long *grid, *newgrid;
    int *lv, *lh;
    int started, dragging;
};

/*
 * The contents of ds->grid are complicated, because of the circular
 * islands which overlap their own grid square into neighbouring
 * squares. An island square can contain pieces of the bridges in all
 * directions, and conversely a bridge square can be intruded on by
 * islands from any direction.
 *
 * So we define one group of flags describing what's important about
 * an island, and another describing a bridge. Island squares' entries
 * in ds->grid contain one of the former and four of the latter; bridge
 * squares, four of the former and _two_ of the latter - because a
 * horizontal and vertical 'bridge' can cross, when one of them is a
 * 'no bridge here' pencil mark.
 *
 * Bridge flags need to indicate 0-4 actual bridges (3 bits), a 'no
 * bridge' row of crosses, or a grey hint line; that's 7
 * possibilities, so 3 bits suffice. But then we also need to vary the
 * colours: the bridges can turn COL_WARNING if they're part of a loop
 * in no-loops mode, COL_HIGHLIGHT during a victory flash, or
 * COL_SELECTED if they're the bridge the user is currently dragging,
 * so that's 2 more bits for foreground colour. Also bridges can be
 * backed by COL_MARK if they're locked by the user, so that's one
 * more bit, making 6 bits per bridge direction.
 *
 * Island flags omit the actual island clue (it never changes during
 * the game, so doesn't have to be stored in ds->grid to check against
 * the previous version), so they just need to include 2 bits for
 * foreground colour (an island can be normal, COL_HIGHLIGHT during
 * victory, COL_WARNING if its clue is unsatisfiable, or COL_SELECTED
 * if it's part of the user's drag) and 2 bits for background (normal,
 * COL_MARK for a locked island, COL_CURSOR for the keyboard cursor).
 * That's 4 bits per island direction. We must also indicate whether
 * no island is present at all (in the case where the island is
 * potentially intruding into the side of a line square), which we do
 * using the unused 4th value of the background field.
 *
 * So an island square needs 4 + 4*6 = 28 bits, while a bridge square
 * needs 4*4 + 2*6 = 28 bits too. Both only just fit in 32 bits, which
 * is handy, because otherwise we'd have to faff around forever with
 * little structs!
 */
/* Flags for line data */
#define DL_COUNTMASK    0x07
#define DL_COUNT_CROSS  0x06
#define DL_COUNT_HINT   0x07
#define DL_COLMASK      0x18
#define DL_COL_NORMAL   0x00
#define DL_COL_WARNING  0x08
#define DL_COL_FLASH    0x10
#define DL_COL_SELECTED 0x18
#define DL_LOCK         0x20
#define DL_MASK         0x3F
/* Flags for island data */
#define DI_COLMASK      0x03
#define DI_COL_NORMAL   0x00
#define DI_COL_FLASH    0x01
#define DI_COL_WARNING  0x02
#define DI_COL_SELECTED 0x03
#define DI_BGMASK       0x0C
#define DI_BG_NO_ISLAND 0x00
#define DI_BG_NORMAL    0x04
#define DI_BG_MARK      0x08
#define DI_BG_CURSOR    0x0C
#define DI_MASK         0x0F
/* Shift counts for the format of a 32-bit word in an island square */
#define D_I_ISLAND_SHIFT 0
#define D_I_LINE_SHIFT_L 4
#define D_I_LINE_SHIFT_R 10
#define D_I_LINE_SHIFT_U 16
#define D_I_LINE_SHIFT_D 24
/* Shift counts for the format of a 32-bit word in a line square */
#define D_L_ISLAND_SHIFT_L 0
#define D_L_ISLAND_SHIFT_R 4
#define D_L_ISLAND_SHIFT_U 8
#define D_L_ISLAND_SHIFT_D 12
#define D_L_LINE_SHIFT_H 16
#define D_L_LINE_SHIFT_V 22

static char *update_drag_dst(const game_state *state, game_ui *ui,
                             const game_drawstate *ds, int nx, int ny)
{
    int ox, oy, dx, dy, i, currl, maxb;
    struct island *is;
    grid_type gtype, ntype, mtype, curr;

    if (ui->dragx_src == -1 || ui->dragy_src == -1) return NULL;

    ui->dragx_dst = -1;
    ui->dragy_dst = -1;

    /* work out which of the four directions we're closest to... */
    ox = COORD(ui->dragx_src) + TILE_SIZE/2;
    oy = COORD(ui->dragy_src) + TILE_SIZE/2;

    if (abs(nx-ox) < abs(ny-oy)) {
        dx = 0;
        dy = (ny-oy) < 0 ? -1 : 1;
        gtype = G_LINEV; ntype = G_NOLINEV; mtype = G_MARKV;
        maxb = INDEX(state, maxv, ui->dragx_src+dx, ui->dragy_src+dy);
    } else {
        dy = 0;
        dx = (nx-ox) < 0 ? -1 : 1;
        gtype = G_LINEH; ntype = G_NOLINEH; mtype = G_MARKH;
        maxb = INDEX(state, maxh, ui->dragx_src+dx, ui->dragy_src+dy);
    }
    if (ui->drag_is_noline) {
        ui->todraw = ntype;
    } else {
        curr = GRID(state, ui->dragx_src+dx, ui->dragy_src+dy);
        currl = INDEX(state, lines, ui->dragx_src+dx, ui->dragy_src+dy);

        if (curr & gtype) {
            if (currl == maxb) {
                ui->todraw = 0;
                ui->nlines = 0;
            } else {
                ui->todraw = gtype;
                ui->nlines = currl + 1;
            }
        } else {
            ui->todraw = gtype;
            ui->nlines = 1;
        }
    }

    /* ... and see if there's an island off in that direction. */
    is = INDEX(state, gridi, ui->dragx_src, ui->dragy_src);
    for (i = 0; i < is->adj.npoints; i++) {
        if (is->adj.points[i].off == 0) continue;
        curr = GRID(state, is->x+dx, is->y+dy);
        if (curr & mtype) continue; /* don't allow changes to marked lines. */
        if (ui->drag_is_noline) {
            if (curr & gtype) continue; /* no no-line where already a line */
        } else {
            if (POSSIBLES(state, dx, is->x+dx, is->y+dy) == 0) continue; /* no line if !possible. */
            if (curr & ntype) continue; /* can't have a bridge where there's a no-line. */
        }

        if (is->adj.points[i].dx == dx &&
            is->adj.points[i].dy == dy) {
            ui->dragx_dst = ISLAND_ORTHX(is,i);
            ui->dragy_dst = ISLAND_ORTHY(is,i);
        }
    }
    /*debug(("update_drag src (%d,%d) d(%d,%d) dst (%d,%d)\n",
           ui->dragx_src, ui->dragy_src, dx, dy,
           ui->dragx_dst, ui->dragy_dst));*/
    return "";
}

static char *finish_drag(const game_state *state, game_ui *ui)
{
    char buf[80];

    if (ui->dragx_src == -1 || ui->dragy_src == -1)
        return NULL;
    if (ui->dragx_dst == -1 || ui->dragy_dst == -1)
        return ui_cancel_drag(ui);

    if (ui->drag_is_noline) {
        sprintf(buf, "N%d,%d,%d,%d",
                ui->dragx_src, ui->dragy_src,
                ui->dragx_dst, ui->dragy_dst);
    } else {
        sprintf(buf, "L%d,%d,%d,%d,%d",
                ui->dragx_src, ui->dragy_src,
                ui->dragx_dst, ui->dragy_dst, ui->nlines);
    }

    ui_cancel_drag(ui);

    return dupstr(buf);
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gx = FROMCOORD(x), gy = FROMCOORD(y);
    char buf[80], *ret;
    grid_type ggrid = INGRID(state,gx,gy) ? GRID(state,gx,gy) : 0;
    int shift = button & MOD_SHFT, control = button & MOD_CTRL;
    button &= ~MOD_MASK;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        if (!INGRID(state, gx, gy)) return NULL;
        ui->cur_visible = 0;
        if (ggrid & G_ISLAND) {
            ui->dragx_src = gx;
            ui->dragy_src = gy;
            return "";
        } else
            return ui_cancel_drag(ui);
    } else if (button == LEFT_DRAG || button == RIGHT_DRAG) {
        if (INGRID(state, ui->dragx_src, ui->dragy_src)
                && (gx != ui->dragx_src || gy != ui->dragy_src)
                && !(GRID(state,ui->dragx_src,ui->dragy_src) & G_MARK)) {
            ui->dragging = 1;
            ui->drag_is_noline = (button == RIGHT_DRAG) ? 1 : 0;
            return update_drag_dst(state, ui, ds, x, y);
        } else {
            /* cancel a drag when we go back to the starting point */
            ui->dragx_dst = -1;
            ui->dragy_dst = -1;
            return "";
        }
    } else if (button == LEFT_RELEASE || button == RIGHT_RELEASE) {
        if (ui->dragging) {
            return finish_drag(state, ui);
        } else {
            if (!INGRID(state, ui->dragx_src, ui->dragy_src)
                    || gx != ui->dragx_src || gy != ui->dragy_src) {
                return ui_cancel_drag(ui);
            }
            ui_cancel_drag(ui);
            if (!INGRID(state, gx, gy)) return NULL;
            if (!(GRID(state, gx, gy) & G_ISLAND)) return NULL;
            sprintf(buf, "M%d,%d", gx, gy);
            return dupstr(buf);
        }
    } else if (button == 'h' || button == 'H') {
        game_state *solved = dup_game(state);
        solve_for_hint(solved);
        ret = game_state_diff(state, solved);
        free_game(solved);
        return ret;
    } else if (IS_CURSOR_MOVE(button)) {
        ui->cur_visible = 1;
        if (control || shift) {
            ui->dragx_src = ui->cur_x;
            ui->dragy_src = ui->cur_y;
            ui->dragging = TRUE;
            ui->drag_is_noline = !control;
        }
        if (ui->dragging) {
            int nx = ui->cur_x, ny = ui->cur_y;

            move_cursor(button, &nx, &ny, state->w, state->h, 0);
            if (nx == ui->cur_x && ny == ui->cur_y)
                return NULL;
            update_drag_dst(state, ui, ds,
                             COORD(nx)+TILE_SIZE/2,
                             COORD(ny)+TILE_SIZE/2);
            return finish_drag(state, ui);
        } else {
            int dx = (button == CURSOR_RIGHT) ? +1 : (button == CURSOR_LEFT) ? -1 : 0;
            int dy = (button == CURSOR_DOWN)  ? +1 : (button == CURSOR_UP)   ? -1 : 0;
            int dorthx = 1 - abs(dx), dorthy = 1 - abs(dy);
            int dir, orth, nx = x, ny = y;

            /* 'orthorder' is a tweak to ensure that if you press RIGHT and
             * happen to move upwards, when you press LEFT you then tend
             * downwards (rather than upwards again). */
            int orthorder = (button == CURSOR_LEFT || button == CURSOR_UP) ? 1 : -1;

            /* This attempts to find an island in the direction you're
             * asking for, broadly speaking. If you ask to go right, for
             * example, it'll look for islands to the right and slightly
             * above or below your current horiz. position, allowing
             * further above/below the further away it searches. */

            assert(GRID(state, ui->cur_x, ui->cur_y) & G_ISLAND);
            /* currently this is depth-first (so orthogonally-adjacent
             * islands across the other side of the grid will be moved to
             * before closer islands slightly offset). Swap the order of
             * these two loops to change to breadth-first search. */
            for (orth = 0; ; orth++) {
                int oingrid = 0;
                for (dir = 1; ; dir++) {
                    int dingrid = 0;

                    if (orth > dir) continue; /* only search in cone outwards. */

                    nx = ui->cur_x + dir*dx + orth*dorthx*orthorder;
                    ny = ui->cur_y + dir*dy + orth*dorthy*orthorder;
                    if (INGRID(state, nx, ny)) {
                        dingrid = oingrid = 1;
                        if (GRID(state, nx, ny) & G_ISLAND) goto found;
                    }

                    nx = ui->cur_x + dir*dx - orth*dorthx*orthorder;
                    ny = ui->cur_y + dir*dy - orth*dorthy*orthorder;
                    if (INGRID(state, nx, ny)) {
                        dingrid = oingrid = 1;
                        if (GRID(state, nx, ny) & G_ISLAND) goto found;
                    }

                    if (!dingrid) break;
                }
                if (!oingrid) return "";
            }
            /* not reached */

found:
            ui->cur_x = nx;
            ui->cur_y = ny;
            return "";
        }
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = 1;
            return "";
        }
        if (ui->dragging || button == CURSOR_SELECT2) {
            ui_cancel_drag(ui);
            if (ui->dragx_dst == -1 && ui->dragy_dst == -1) {
                sprintf(buf, "M%d,%d", ui->cur_x, ui->cur_y);
                return dupstr(buf);
            } else
                return "";
        } else {
            grid_type v = GRID(state, ui->cur_x, ui->cur_y);
            if (v & G_ISLAND) {
                ui->dragging = 1;
                ui->dragx_src = ui->cur_x;
                ui->dragy_src = ui->cur_y;
                ui->dragx_dst = ui->dragy_dst = -1;
                ui->drag_is_noline = (button == CURSOR_SELECT2) ? 1 : 0;
                return "";
            }
        }
    } else if (button == 'l' || button == 'L') {
        if (!ui->cur_visible) {
            ui->cur_visible = 1;
        }
        if (ui->dragging) {
            ui_cancel_drag(ui);
        }
        sprintf(buf, "M%d,%d", ui->cur_x, ui->cur_y);
        return dupstr(buf);
    } else if ((button >= '0' && button <= '9') ||
               (button >= 'a' && button <= 'f') ||
               (button >= 'A' && button <= 'F')) {
        /* jump to island with .count == number closest to cur_{x,y} */
        int best_x = -1, best_y = -1, best_sqdist = -1, number = -1, i;

        if (button >= '0' && button <= '9')
            number = (button == '0' ? 16 : button - '0');
        else if (button >= 'a' && button <= 'f')
            number = 10 + button - 'a';
        else if (button >= 'A' && button <= 'F')
            number = 10 + button - 'A';

        if (!ui->cur_visible) {
            ui->cur_visible = 1;
            return "";
        }

        for (i = 0; i < state->n_islands; ++i) {
            int x = state->islands[i].x, y = state->islands[i].y;
            int dx = x - ui->cur_x, dy = y - ui->cur_y;
            int sqdist = dx*dx + dy*dy;

            if (state->islands[i].count != number)
                continue;
            if (x == ui->cur_x && y == ui->cur_y)
                continue;

            /* new_game() reads the islands in row-major order, so by
             * breaking ties in favor of `first in state->islands' we
             * also break ties by `lexicographically smallest (y, x)'.
             * Thus, there's a stable pattern to how ties are broken
             * which the user can learn and use to navigate faster. */
            if (best_sqdist == -1 || sqdist < best_sqdist) {
                best_x = x;
                best_y = y;
                best_sqdist = sqdist;
            }
        }
        if (best_x != -1 && best_y != -1) {
            ui->cur_x = best_x;
            ui->cur_y = best_y;
            return "";
        } else
            return NULL;
    } else if (button == 'g' || button == 'G') {
        ui->show_hints = 1 - ui->show_hints;
        return "";
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    int x1, y1, x2, y2, nl, n;
    struct island *is1, *is2;
    char c;

    debug(("execute_move: %s\n", move));

    if (!*move) goto badmove;
    while (*move) {
        c = *move++;
        if (c == 'S') {
            ret->solved = TRUE;
            n = 0;
        } else if (c == 'L') {
            if (sscanf(move, "%d,%d,%d,%d,%d%n",
                       &x1, &y1, &x2, &y2, &nl, &n) != 5)
                goto badmove;
            if (!INGRID(ret, x1, y1) || !INGRID(ret, x2, y2))
                goto badmove;
            is1 = INDEX(ret, gridi, x1, y1);
            is2 = INDEX(ret, gridi, x2, y2);
            if (!is1 || !is2) goto badmove;
            if (nl < 0 || nl > state->maxb) goto badmove;
            island_join(is1, is2, nl, 0);
        } else if (c == 'N') {
            if (sscanf(move, "%d,%d,%d,%d%n",
                       &x1, &y1, &x2, &y2, &n) != 4)
                goto badmove;
            if (!INGRID(ret, x1, y1) || !INGRID(ret, x2, y2))
                goto badmove;
            is1 = INDEX(ret, gridi, x1, y1);
            is2 = INDEX(ret, gridi, x2, y2);
            if (!is1 || !is2) goto badmove;
            island_join(is1, is2, -1, 0);
        } else if (c == 'M') {
            if (sscanf(move, "%d,%d%n",
                       &x1, &y1, &n) != 2)
                goto badmove;
            if (!INGRID(ret, x1, y1))
                goto badmove;
            is1 = INDEX(ret, gridi, x1, y1);
            if (!is1) goto badmove;
            island_togglemark(is1);
        } else
            goto badmove;

        move += n;
        if (*move == ';')
            move++;
        else if (*move) goto badmove;
    }

    map_update_possibles(ret);
    if (map_check(ret)) {
        debug(("Game completed.\n"));
        ret->completed = 1;
    }
    return ret;

badmove:
    debug(("%s: unrecognised move.\n", move));
    free_game(ret);
    return NULL;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
    char *ret;
    game_state *solved;

    if (aux) {
        debug(("solve_game: aux = %s\n", aux));
        solved = execute_move(state, aux);
        if (!solved) {
            *error = _("Generated aux string is not a valid move (!).");
            return NULL;
        }
    } else {
        solved = dup_game(state);
        /* solve with max strength... */
        if (solve_from_scratch(solved, 10) == 0) {
            free_game(solved);
            *error = _("Game does not have a (non-recursive) solution.");
            return NULL;
        }
    }
    ret = game_state_diff(currstate, solved);
    free_game(solved);
    debug(("solve_game: ret = %s\n", ret));
    return ret;
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

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_FOREGROUND * 3 + i] = 0.0F;
        ret[COL_HINT * 3 + i] = ret[COL_LOWLIGHT * 3 + i];
        ret[COL_GRID * 3 + i] =
            (ret[COL_HINT * 3 + i] + ret[COL_BACKGROUND * 3 + i]) * 0.5F;
        ret[COL_MARK * 3 + i] = ret[COL_HIGHLIGHT * 3 + i];
    }
    ret[COL_WARNING * 3 + 0] = 1.0F;
    ret[COL_WARNING * 3 + 1] = 0.25F;
    ret[COL_WARNING * 3 + 2] = 0.25F;

    ret[COL_SELECTED * 3 + 0] = 0.25F;
    ret[COL_SELECTED * 3 + 1] = 1.00F;
    ret[COL_SELECTED * 3 + 2] = 0.25F;

    ret[COL_CURSOR * 3 + 0] = min(ret[COL_BACKGROUND * 3 + 0] * 1.4F, 1.0F);
    ret[COL_CURSOR * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.8F;
    ret[COL_CURSOR * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 0.8F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int wh = state->w*state->h;
    int i;

    ds->tilesize = 0;
    ds->w = state->w;
    ds->h = state->h;
    ds->started = 0;
    ds->dragging = 0;
    ds->grid = snewn(wh, unsigned long);
    for (i = 0; i < wh; i++)
        ds->grid[i] = ~0UL;
    ds->newgrid = snewn(wh, unsigned long);
    ds->lv = snewn(wh, int);
    ds->lh = snewn(wh, int);
    memset(ds->lv, 0, wh*sizeof(int));
    memset(ds->lh, 0, wh*sizeof(int));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->lv);
    sfree(ds->lh);
    sfree(ds->newgrid);
    sfree(ds->grid);
    sfree(ds);
}

#define LINE_WIDTH (TILE_SIZE/8)
#define TS8(x) (((x)*TILE_SIZE)/8)

#define OFFSET(thing) ((TILE_SIZE/2) - ((thing)/2))

static int between_island(const game_state *state, int sx, int sy,
                          int dx, int dy)
{
    int x = sx - dx, y = sy - dy;

    while (INGRID(state, x, y)) {
        if (GRID(state, x, y) & G_ISLAND) goto found;
        x -= dx; y -= dy;
    }
    return 0;
found:
    x = sx + dx, y = sy + dy;
    while (INGRID(state, x, y)) {
        if (GRID(state, x, y) & G_ISLAND) return 1;
        x += dx; y += dy;
    }
    return 0;
}

static void lines_lvlh(const game_state *state, const game_ui *ui,
                       int x, int y, grid_type v, int *lv_r, int *lh_r)
{
    int lh = 0, lv = 0;

    if (v & G_LINEV) lv = INDEX(state,lines,x,y);
    if (v & G_LINEH) lh = INDEX(state,lines,x,y);

    if (ui->show_hints) {
        if (between_island(state, x, y, 0, 1) && !lv) lv = 1;
        if (between_island(state, x, y, 1, 0) && !lh) lh = 1;
    }
    /*debug(("lvlh: (%d,%d) v 0x%x lv %d lh %d.\n", x, y, v, lv, lh));*/
    *lv_r = lv; *lh_r = lh;
}

static void draw_cross(drawing *dr, game_drawstate *ds,
                       int ox, int oy, int col)
{
    int off = TS8(2);
    draw_line(dr, ox,     oy, ox+off, oy+off, col);
    draw_line(dr, ox+off, oy, ox,     oy+off, col);
}

static void draw_general_line(drawing *dr, game_drawstate *ds,
                              int ox, int oy, int fx, int fy, int ax, int ay,
                              int len, unsigned long ldata, int which)
{
    /*
     * Draw one direction of lines in a square. To permit the same
     * code to handle horizontal and vertical lines, fx,fy are the
     * 'forward' direction (along the lines) and ax,ay are the
     * 'across' direction.
     *
     * We draw the white background for a locked bridge if (which &
     * 1), and draw the bridges themselves if (which & 2). This
     * permits us to get two overlapping locked bridges right without
     * one of them erasing part of the other.
     */
    int fg;

    fg = ((ldata & DL_COUNTMASK) == DL_COUNT_HINT ? COL_HINT :
          (ldata & DL_COLMASK) == DL_COL_SELECTED ? COL_SELECTED :
          (ldata & DL_COLMASK) == DL_COL_FLASH ? COL_HIGHLIGHT :
          (ldata & DL_COLMASK) == DL_COL_WARNING ? COL_WARNING :
          COL_FOREGROUND);

    if ((ldata & DL_COUNTMASK) == DL_COUNT_CROSS) {
        draw_cross(dr, ds,
                   ox + TS8(1)*fx + TS8(3)*ax,
                   oy + TS8(1)*fy + TS8(3)*ay, fg);
        draw_cross(dr, ds,
                   ox + TS8(5)*fx + TS8(3)*ax,
                   oy + TS8(5)*fy + TS8(3)*ay, fg);
    } else if ((ldata & DL_COUNTMASK) != 0) {
        int lh, lw, gw, bw, i, loff;

        lh = (ldata & DL_COUNTMASK);
        if (lh == DL_COUNT_HINT)
            lh = 1;

        lw = gw = LINE_WIDTH;
        while ((bw = lw * lh + gw * (lh+1)) > TILE_SIZE)
            gw--;

        loff = OFFSET(bw);

        if (which & 1) {
            if ((ldata & DL_LOCK) && fg != COL_HINT)
                draw_rect(dr, ox + loff*ax, oy + loff*ay,
                          len*fx+bw*ax, len*fy+bw*ay, COL_MARK);
        }
        if (which & 2) {
            for (i = 0; i < lh; i++, loff += lw + gw)
                draw_rect(dr, ox + (loff+gw)*ax, oy + (loff+gw)*ay,
                          len*fx+lw*ax, len*fy+lw*ay, fg);
        }
    }
}

static void draw_hline(drawing *dr, game_drawstate *ds,
                       int ox, int oy, int w, unsigned long vdata, int which)
{
    draw_general_line(dr, ds, ox, oy, 1, 0, 0, 1, w, vdata, which);
}

static void draw_vline(drawing *dr, game_drawstate *ds,
                       int ox, int oy, int h, unsigned long vdata, int which)
{
    draw_general_line(dr, ds, ox, oy, 0, 1, 1, 0, h, vdata, which);
}

#define ISLAND_RADIUS ((TILE_SIZE*12)/20)
#define ISLAND_NUMSIZE(clue) \
    (((clue) < 10) ? (TILE_SIZE*7)/10 : (TILE_SIZE*5)/10)

static void draw_island(drawing *dr, game_drawstate *ds,
                        int ox, int oy, int clue, unsigned long idata)
{
    int half, orad, irad, fg, bg;

    if ((idata & DI_BGMASK) == DI_BG_NO_ISLAND)
        return;

    half = TILE_SIZE/2;
    orad = ISLAND_RADIUS;
    irad = orad - LINE_WIDTH;
    fg = ((idata & DI_COLMASK) == DI_COL_SELECTED ? COL_SELECTED :
          (idata & DI_COLMASK) == DI_COL_WARNING ? COL_WARNING :
          (idata & DI_COLMASK) == DI_COL_FLASH ? COL_HIGHLIGHT :
          COL_FOREGROUND);
    bg = ((idata & DI_BGMASK) == DI_BG_CURSOR ? COL_CURSOR :
          (idata & DI_BGMASK) == DI_BG_MARK ? COL_MARK :
          COL_BACKGROUND);

    /* draw a thick circle */
    draw_circle(dr, ox+half, oy+half, orad, fg, fg);
    draw_circle(dr, ox+half, oy+half, irad, bg, bg);

    if (clue > 0) {
        char str[32];
        int textcolour = (fg == COL_SELECTED ? COL_FOREGROUND : fg);
        sprintf(str, "%d", clue);
        draw_text(dr, ox+half, oy+half, FONT_VARIABLE, ISLAND_NUMSIZE(clue),
                  ALIGN_VCENTRE | ALIGN_HCENTRE, textcolour, str);
    }
}

static void draw_island_tile(drawing *dr, game_drawstate *ds,
                             int x, int y, int clue, unsigned long data)
{
    int ox = COORD(x), oy = COORD(y);
    int which;

    clip(dr, ox, oy, TILE_SIZE, TILE_SIZE);
    draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);

    /*
     * Because of the possibility of incoming bridges just about
     * meeting at one corner, we must split the line-drawing into
     * background and foreground segments.
     */
    for (which = 1; which <= 2; which <<= 1) {
        draw_hline(dr, ds, ox, oy, TILE_SIZE/2,
                   (data >> D_I_LINE_SHIFT_L) & DL_MASK, which);
        draw_hline(dr, ds, ox + TILE_SIZE - TILE_SIZE/2, oy, TILE_SIZE/2,
                   (data >> D_I_LINE_SHIFT_R) & DL_MASK, which);
        draw_vline(dr, ds, ox, oy, TILE_SIZE/2,
                   (data >> D_I_LINE_SHIFT_U) & DL_MASK, which);
        draw_vline(dr, ds, ox, oy + TILE_SIZE - TILE_SIZE/2, TILE_SIZE/2,
                   (data >> D_I_LINE_SHIFT_D) & DL_MASK, which);
    }
    draw_island(dr, ds, ox, oy, clue, (data >> D_I_ISLAND_SHIFT) & DI_MASK);

    unclip(dr);
    draw_update(dr, ox, oy, TILE_SIZE, TILE_SIZE);
}

static void draw_line_tile(drawing *dr, game_drawstate *ds,
                           int x, int y, unsigned long data)
{
    int ox = COORD(x), oy = COORD(y);
    unsigned long hdata, vdata;

    clip(dr, ox, oy, TILE_SIZE, TILE_SIZE);
    draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);

    /*
     * We have to think about which of the horizontal and vertical
     * line to draw first, if both exist.
     *
     * The rule is that hint lines are drawn at the bottom, then
     * NOLINE crosses, then actual bridges. The enumeration in the
     * DL_COUNTMASK field is set up so that this drops out of a
     * straight comparison between the two.
     *
     * Since lines crossing in this type of square cannot both be
     * actual bridges, there's no need to pass a nontrivial 'which'
     * parameter to draw_[hv]line.
     */
    hdata = (data >> D_L_LINE_SHIFT_H) & DL_MASK;
    vdata = (data >> D_L_LINE_SHIFT_V) & DL_MASK;
    if ((hdata & DL_COUNTMASK) > (vdata & DL_COUNTMASK)) {
        draw_hline(dr, ds, ox, oy, TILE_SIZE, hdata, 3);
        draw_vline(dr, ds, ox, oy, TILE_SIZE, vdata, 3);
    } else {
        draw_vline(dr, ds, ox, oy, TILE_SIZE, vdata, 3);
        draw_hline(dr, ds, ox, oy, TILE_SIZE, hdata, 3);
    }

    /*
     * The islands drawn at the edges of a line tile don't need clue
     * numbers.
     */
    draw_island(dr, ds, ox - TILE_SIZE, oy, -1,
                (data >> D_L_ISLAND_SHIFT_L) & DI_MASK);
    draw_island(dr, ds, ox + TILE_SIZE, oy, -1,
                (data >> D_L_ISLAND_SHIFT_R) & DI_MASK);
    draw_island(dr, ds, ox, oy - TILE_SIZE, -1,
                (data >> D_L_ISLAND_SHIFT_U) & DI_MASK);
    draw_island(dr, ds, ox, oy + TILE_SIZE, -1,
                (data >> D_L_ISLAND_SHIFT_D) & DI_MASK);

    unclip(dr);
    draw_update(dr, ox, oy, TILE_SIZE, TILE_SIZE);
}

static void draw_edge_tile(drawing *dr, game_drawstate *ds,
                           int x, int y, int dx, int dy, unsigned long data)
{
    int ox = COORD(x), oy = COORD(y);
    int cx = ox, cy = oy, cw = TILE_SIZE, ch = TILE_SIZE;

    if (dy) {
        if (dy > 0)
            cy += TILE_SIZE/2;
        ch -= TILE_SIZE/2;
    } else {
        if (dx > 0)
            cx += TILE_SIZE/2;
        cw -= TILE_SIZE/2;
    }
    clip(dr, cx, cy, cw, ch);
    draw_rect(dr, cx, cy, cw, ch, COL_BACKGROUND);

    draw_island(dr, ds, ox + TILE_SIZE*dx, oy + TILE_SIZE*dy, -1,
                (data >> D_I_ISLAND_SHIFT) & DI_MASK);

    unclip(dr);
    draw_update(dr, cx, cy, cw, ch);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y, lv, lh;
    grid_type v, flash = 0;
    struct island *is, *is_drag_src = NULL, *is_drag_dst = NULL;

    if (flashtime) {
        int f = (int)(flashtime * 5 / FLASH_TIME);
        if (f == 1 || f == 3) flash = TRUE;
    }

    /* Clear screen, if required. */
    if (!ds->started) {
        draw_rect(dr, 0, 0,
                  TILE_SIZE * ds->w + 2 * BORDER,
                  TILE_SIZE * ds->h + 2 * BORDER, COL_BACKGROUND);
#ifdef DRAW_GRID
        draw_rect_outline(dr,
                          COORD(0)-1, COORD(0)-1,
                          TILE_SIZE * ds->w + 2, TILE_SIZE * ds->h + 2,
                          COL_GRID);
#endif
        draw_update(dr, 0, 0,
                    TILE_SIZE * ds->w + 2 * BORDER,
                    TILE_SIZE * ds->h + 2 * BORDER);
        ds->started = 1;
    }

    if (ui->dragx_src != -1 && ui->dragy_src != -1) {
        ds->dragging = 1;
        is_drag_src = INDEX(state, gridi, ui->dragx_src, ui->dragy_src);
        assert(is_drag_src);
        if (ui->dragx_dst != -1 && ui->dragy_dst != -1) {
            is_drag_dst = INDEX(state, gridi, ui->dragx_dst, ui->dragy_dst);
            assert(is_drag_dst);
        }
    } else
        ds->dragging = 0;

    /*
     * Set up ds->newgrid with the current grid contents.
     */
    for (x = 0; x < ds->w; x++)
        for (y = 0; y < ds->h; y++)
            INDEX(ds,newgrid,x,y) = 0;

    for (x = 0; x < ds->w; x++) {
        for (y = 0; y < ds->h; y++) {
            v = GRID(state, x, y);

            if (v & G_ISLAND) {
                /*
                 * An island square. Compute the drawing data for the
                 * island, and put it in this square and surrounding
                 * squares.
                 */
                unsigned long idata = 0;

                is = INDEX(state, gridi, x, y);

                if (flash)
                    idata |= DI_COL_FLASH;
                if (is_drag_src && (is == is_drag_src ||
                                    (is_drag_dst && is == is_drag_dst)))
                    idata |= DI_COL_SELECTED;
                else if (island_impossible(is, v & G_MARK) || (v & G_WARN))
                    idata |= DI_COL_WARNING;
                else
                    idata |= DI_COL_NORMAL;

                if (ui->cur_visible &&
                    ui->cur_x == is->x && ui->cur_y == is->y)
                    idata |= DI_BG_CURSOR;
                else if (v & G_MARK)
                    idata |= DI_BG_MARK;
                else
                    idata |= DI_BG_NORMAL;

                INDEX(ds,newgrid,x,y) |= idata << D_I_ISLAND_SHIFT;
                if (x > 0 && !(GRID(state,x-1,y) & G_ISLAND))
                    INDEX(ds,newgrid,x-1,y) |= idata << D_L_ISLAND_SHIFT_R;
                if (x+1 < state->w && !(GRID(state,x+1,y) & G_ISLAND))
                    INDEX(ds,newgrid,x+1,y) |= idata << D_L_ISLAND_SHIFT_L;
                if (y > 0 && !(GRID(state,x,y-1) & G_ISLAND))
                    INDEX(ds,newgrid,x,y-1) |= idata << D_L_ISLAND_SHIFT_D;
                if (y+1 < state->h && !(GRID(state,x,y+1) & G_ISLAND))
                    INDEX(ds,newgrid,x,y+1) |= idata << D_L_ISLAND_SHIFT_U;
            } else {
                unsigned long hdata, vdata;
                int selh = FALSE, selv = FALSE;

                /*
                 * A line (non-island) square. Compute the drawing
                 * data for any horizontal and vertical lines in the
                 * square, and put them in this square's entry and
                 * optionally those for neighbouring islands too.
                 */

                if (is_drag_dst &&
                    WITHIN(x,is_drag_src->x, is_drag_dst->x) &&
                    WITHIN(y,is_drag_src->y, is_drag_dst->y)) {
                    if (is_drag_src->x != is_drag_dst->x)
                        selh = TRUE;
                    else
                        selv = TRUE;
                }
                lines_lvlh(state, ui, x, y, v, &lv, &lh);

                hdata = (v & G_NOLINEH ? DL_COUNT_CROSS :
                         v & G_LINEH ? lh :
                         (ui->show_hints &&
                          between_island(state,x,y,1,0)) ? DL_COUNT_HINT : 0);
                vdata = (v & G_NOLINEV ? DL_COUNT_CROSS :
                         v & G_LINEV ? lv :
                         (ui->show_hints &&
                          between_island(state,x,y,0,1)) ? DL_COUNT_HINT : 0);

                hdata |= (flash ? DL_COL_FLASH :
                          v & G_WARN ? DL_COL_WARNING :
                          selh ? DL_COL_SELECTED :
                          DL_COL_NORMAL);
                vdata |= (flash ? DL_COL_FLASH :
                          v & G_WARN ? DL_COL_WARNING :
                          selv ? DL_COL_SELECTED :
                          DL_COL_NORMAL);

                if (v & G_MARKH)
                    hdata |= DL_LOCK;
                if (v & G_MARKV)
                    vdata |= DL_LOCK;

                INDEX(ds,newgrid,x,y) |= hdata << D_L_LINE_SHIFT_H;
                INDEX(ds,newgrid,x,y) |= vdata << D_L_LINE_SHIFT_V;
                if (x > 0 && (GRID(state,x-1,y) & G_ISLAND))
                    INDEX(ds,newgrid,x-1,y) |= hdata << D_I_LINE_SHIFT_R;
                if (x+1 < state->w && (GRID(state,x+1,y) & G_ISLAND))
                    INDEX(ds,newgrid,x+1,y) |= hdata << D_I_LINE_SHIFT_L;
                if (y > 0 && (GRID(state,x,y-1) & G_ISLAND))
                    INDEX(ds,newgrid,x,y-1) |= vdata << D_I_LINE_SHIFT_D;
                if (y+1 < state->h && (GRID(state,x,y+1) & G_ISLAND))
                    INDEX(ds,newgrid,x,y+1) |= vdata << D_I_LINE_SHIFT_U;
            }
        }
    }

    /*
     * Now go through and draw any changed grid square.
     */
    for (x = 0; x < ds->w; x++) {
        for (y = 0; y < ds->h; y++) {
            unsigned long newval = INDEX(ds,newgrid,x,y);
            if (INDEX(ds,grid,x,y) != newval) {
                v = GRID(state, x, y);
                if (v & G_ISLAND) {
                    is = INDEX(state, gridi, x, y);
                    draw_island_tile(dr, ds, x, y, is->count, newval);

                    /*
                     * If this tile is right at the edge of the grid,
                     * we must also draw the part of the island that
                     * goes completely out of bounds. We don't bother
                     * keeping separate entries in ds->newgrid for
                     * these tiles; it's easier just to redraw them
                     * iff we redraw their parent island tile.
                     */
                    if (x == 0)
                        draw_edge_tile(dr, ds, x-1, y, +1, 0, newval);
                    if (y == 0)
                        draw_edge_tile(dr, ds, x, y-1, 0, +1, newval);
                    if (x == state->w-1)
                        draw_edge_tile(dr, ds, x+1, y, -1, 0, newval);
                    if (y == state->h-1)
                        draw_edge_tile(dr, ds, x, y+1, 0, -1, newval);
                } else {
                    draw_line_tile(dr, ds, x, y, newval);
                }
                INDEX(ds,grid,x,y) = newval;
            }
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
        !oldstate->solved && !newstate->solved)
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

#ifndef NO_PRINTING
static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /* 10mm squares by default. */
    game_compute_size(params, 1000, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int ts)
{
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y, cx, cy, i, nl;
    int loff;
    grid_type grid;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    ads.tilesize = ts;

    /* I don't think this wants a border. */

    /* Bridges */
    loff = ts / (8 * sqrt((state->params.maxb - 1)));
    print_line_width(dr, ts / 12);
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            cx = COORD(x); cy = COORD(y);
            grid = GRID(state,x,y);
            nl = INDEX(state,lines,x,y);

            if (grid & G_ISLAND) continue;
            if (grid & G_LINEV) {
                for (i = 0; i < nl; i++)
                    draw_line(dr, cx+ts/2+(2*i-nl+1)*loff, cy,
                              cx+ts/2+(2*i-nl+1)*loff, cy+ts, ink);
            }
            if (grid & G_LINEH) {
                for (i = 0; i < nl; i++)
                    draw_line(dr, cx, cy+ts/2+(2*i-nl+1)*loff,
                              cx+ts, cy+ts/2+(2*i-nl+1)*loff, ink);
            }
        }
    }

    /* Islands */
    for (i = 0; i < state->n_islands; i++) {
        char str[32];
        struct island *is = &state->islands[i];
        grid = GRID(state, is->x, is->y);
        cx = COORD(is->x) + ts/2;
        cy = COORD(is->y) + ts/2;

        draw_circle(dr, cx, cy, ISLAND_RADIUS, paper, ink);

        sprintf(str, "%d", is->count);
        draw_text(dr, cx, cy, FONT_VARIABLE, ISLAND_NUMSIZE(is->count),
                  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
    }
}
#endif

#ifdef COMBINED
#define thegame bridges
#endif

const struct game thegame = {
    "Bridges", "games.bridges", "bridges",
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
    REQUIRE_RBUTTON,		       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
