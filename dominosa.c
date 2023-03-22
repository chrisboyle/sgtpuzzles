/*
 * dominosa.c: Domino jigsaw puzzle. Aim to place one of every
 * possible domino within a rectangle in such a way that the number
 * on each square matches the provided clue.
 */

/*
 * Further possible deduction types in the solver:
 *
 *  * possibly an advanced form of deduce_parity via 2-connectedness.
 *    We currently deal with areas of the graph with exactly one way
 *    in and out; but if you have an area with exactly _two_ routes in
 *    and out of it, then you can at least decide on the _relative_
 *    parity of the two (either 'these two edges both bisect dominoes
 *    or neither do', or 'exactly one of these edges bisects a
 *    domino'). And occasionally that can be enough to let you rule
 *    out one of the two remaining choices.
 *     + For example, if both those edges bisect a domino, then those
 *       two dominoes would also be both the same.
 *     + Or perhaps between them they rule out all possibilities for
 *       some other square.
 *     + Or perhaps they themselves would be duplicates!
 *     + Or perhaps, on purely geometric grounds, they would box in a
 *       square to the point where it ended up having to be an
 *       isolated singleton.
 *     + The tricky part of this is how you do the graph theory.
 *       Perhaps a modified form of Tarjan's bridge-finding algorithm
 *       would work, but I haven't thought through the details.
 *
 *  * possibly an advanced version of set analysis which doesn't have
 *    to start from squares all having the same number? For example,
 *    if you have three mutually non-adjacent squares labelled 1,2,3
 *    such that the numbers adjacent to each are precisely the other
 *    two, then set analysis can work just fine in principle, and
 *    tells you that those three squares must overlap the three
 *    dominoes 1-2, 2-3 and 1-3 in some order, so you can rule out any
 *    placements of those elsewhere.
 *     + the difficulty with this is how you avoid it going painfully
 *       exponential-time. You can't iterate over all the subsets, so
 *       you'd need some kind of more sophisticated directed search.
 *     + and the adjacency allowance has to be similarly accounted
 *       for, which could get tricky to keep track of.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

/* nth triangular number */
#define TRI(n) ( (n) * ((n) + 1) / 2 )
/* number of dominoes for value n */
#define DCOUNT(n) TRI((n)+1)
/* map a pair of numbers to a unique domino index from 0 upwards. */
#define DINDEX(n1,n2) ( TRI(max(n1,n2)) + min(n1,n2) )

#define FLASH_TIME 0.13F

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(X)                             \
    X(TRIVIAL,Trivial,t)                        \
    X(BASIC,Basic,b)                            \
    X(HARD,Hard,h)                              \
    X(EXTREME,Extreme,e)                        \
    X(AMBIGUOUS,Ambiguous,a)                    \
    /* end of list */
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const dominosa_diffnames[] = { DIFFLIST(TITLE) };
static char const dominosa_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_DOMINO,
    COL_DOMINOCLASH,
    COL_DOMINOTEXT,
    COL_EDGE,
    COL_HIGHLIGHT_1,
    COL_HIGHLIGHT_2,
    NCOLOURS
};

struct game_params {
    int n;
    int diff;
};

struct game_numbers {
    int refcount;
    int *numbers;                      /* h x w */
};

#define EDGE_L 0x100
#define EDGE_R 0x200
#define EDGE_T 0x400
#define EDGE_B 0x800

struct game_state {
    game_params params;
    int w, h;
    struct game_numbers *numbers;
    int *grid;
    unsigned short *edges;             /* h x w */
    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->n = 6;
    ret->diff = DIFF_BASIC;

    return ret;
}

static const struct game_params dominosa_presets[] = {
    {  3, DIFF_TRIVIAL },
    {  4, DIFF_TRIVIAL },
    {  5, DIFF_TRIVIAL },
    {  6, DIFF_TRIVIAL },
    {  4, DIFF_BASIC   },
    {  5, DIFF_BASIC   },
    {  6, DIFF_BASIC   },
    {  7, DIFF_BASIC   },
    {  8, DIFF_BASIC   },
    {  9, DIFF_BASIC   },
    {  6, DIFF_HARD    },
    {  6, DIFF_EXTREME },
};

static bool game_fetch_preset(int i, char **name, game_params **params_out)
{
    game_params *params;
    char buf[80];

    if (i < 0 || i >= lenof(dominosa_presets))
        return false;

    params = snew(game_params);
    *params = dominosa_presets[i]; /* structure copy */

    sprintf(buf, "Order %d, %s", params->n, dominosa_diffnames[params->diff]);

    *name = dupstr(buf);
    *params_out = params;
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
    const char *p = string;

    params->n = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;

    while (*p) {
        char c = *p++;
        if (c == 'a') {
            /* Legacy encoding from before the difficulty system */
            params->diff = DIFF_AMBIGUOUS;
        } else if (c == 'd') {
            int i;
            params->diff = DIFFCOUNT+1; /* ...which is invalid */
            if (*p) {
                for (i = 0; i < DIFFCOUNT; i++) {
                    if (*p == dominosa_diffchars[i])
                        params->diff = i;
                }
                p++;
            }
        }
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[80];
    int len = sprintf(buf, "%d", params->n);
    if (full)
        len += sprintf(buf + len, "d%c", dominosa_diffchars[params->diff]);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Maximum number on dominoes";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].u.choices.choicenames = DIFFCONFIG;
    ret[1].u.choices.selected = params->diff;

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->n = atoi(cfg[0].u.string.sval);
    ret->diff = cfg[1].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->n < 1)
        return "Maximum face number must be at least one";
    if (params->n > INT_MAX - 2 ||
        params->n + 2 > INT_MAX / (params->n + 1))
        return "Maximum face number must not be unreasonably large";

    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

#ifdef STANDALONE_SOLVER
#define SOLVER_DIAGNOSTICS
static bool solver_diagnostics = false;
#elif defined SOLVER_DIAGNOSTICS
static const bool solver_diagnostics = true;
#endif

struct solver_domino;
struct solver_placement;

/*
 * Information about a particular domino.
 */
struct solver_domino {
    /* The numbers on the domino, and its index in the dominoes array. */
    int lo, hi, index;

    /* List of placements not yet ruled out for this domino. */
    int nplacements;
    struct solver_placement **placements;

#ifdef SOLVER_DIAGNOSTICS
    /* A textual name we can easily reuse in solver diagnostics. */
    char *name;
#endif
};

/*
 * Information about a particular 'placement' (i.e. specific location
 * that a domino might go in).
 */
struct solver_placement {
    /* The index of this placement in sc->placements. */
    int index;

    /* The two squares that make up this placement. */
    struct solver_square *squares[2];

    /* The domino that has to go in this position, if any. */
    struct solver_domino *domino;

    /* The index of this placement in each square's placements array,
     * and in that of the domino. */
    int spi[2], dpi;

    /* Whether this is still considered a possible placement. */
    bool active;

    /* Other domino placements that overlap with this one. (Maximum 6:
     * three overlapping each square of the placement.) */
    int noverlaps;
    struct solver_placement *overlaps[6];

#ifdef SOLVER_DIAGNOSTICS
    /* A textual name we can easily reuse in solver diagnostics. */
    char *name;
#endif
};

/*
 * Information about a particular solver square.
 */
struct solver_square {
    /* The coordinates of the square, and its index in a normal grid array. */
    int x, y, index;

    /* List of domino placements not yet ruled out for this square. */
    int nplacements;
    struct solver_placement *placements[4];

    /* The number in the square. */
    int number;

#ifdef SOLVER_DIAGNOSTICS
    /* A textual name we can easily reuse in solver diagnostics. */
    char *name;
#endif
};

struct solver_scratch {
    int n, dc, pc, w, h, wh;
    int max_diff_used;
    struct solver_domino *dominoes;
    struct solver_placement *placements;
    struct solver_square *squares;
    struct solver_placement **domino_placement_lists;
    struct solver_square **squares_by_number;
    struct findloopstate *fls;
    bool squares_by_number_initialised;
    int *wh_scratch, *pc_scratch, *pc_scratch2, *dc_scratch;
};

static struct solver_scratch *solver_make_scratch(int n)
{
    int dc = DCOUNT(n), w = n+2, h = n+1, wh = w*h;
    int pc = (w-1)*h + w*(h-1);
    struct solver_scratch *sc = snew(struct solver_scratch);
    int hi, lo, di, x, y, pi, si;

    sc->n = n;
    sc->dc = dc;
    sc->pc = pc;
    sc->w = w;
    sc->h = h;
    sc->wh = wh;

    sc->dominoes = snewn(dc, struct solver_domino);
    sc->placements = snewn(pc, struct solver_placement);
    sc->squares = snewn(wh, struct solver_square);
    sc->domino_placement_lists = snewn(pc, struct solver_placement *);
    sc->fls = findloop_new_state(wh);

    for (di = hi = 0; hi <= n; hi++) {
        for (lo = 0; lo <= hi; lo++) {
            assert(di == DINDEX(hi, lo));
            sc->dominoes[di].hi = hi;
            sc->dominoes[di].lo = lo;
            sc->dominoes[di].index = di;

#ifdef SOLVER_DIAGNOSTICS
            {
                char buf[128];
                sprintf(buf, "%d-%d", hi, lo);
                sc->dominoes[di].name = dupstr(buf);
            }
#endif

            di++;
        }
    }

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            struct solver_square *sq = &sc->squares[y*w+x];
            sq->x = x;
            sq->y = y;
            sq->index = y * w + x;
            sq->nplacements = 0;

#ifdef SOLVER_DIAGNOSTICS
            {
                char buf[128];
                sprintf(buf, "(%d,%d)", x, y);
                sq->name = dupstr(buf);
            }
#endif
        }
    }

    pi = 0;
    for (y = 0; y < h-1; y++) {
        for (x = 0; x < w; x++) {
            assert(pi < pc);
            sc->placements[pi].squares[0] = &sc->squares[y*w+x];
            sc->placements[pi].squares[1] = &sc->squares[(y+1)*w+x];
#ifdef SOLVER_DIAGNOSTICS
            {
                char buf[128];
                sprintf(buf, "(%d,%d-%d)", x, y, y+1);
                sc->placements[pi].name = dupstr(buf);
            }
#endif
            pi++;
        }
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w-1; x++) {
            assert(pi < pc);
            sc->placements[pi].squares[0] = &sc->squares[y*w+x];
            sc->placements[pi].squares[1] = &sc->squares[y*w+(x+1)];
#ifdef SOLVER_DIAGNOSTICS
            {
                char buf[128];
                sprintf(buf, "(%d-%d,%d)", x, x+1, y);
                sc->placements[pi].name = dupstr(buf);
            }
#endif
            pi++;
        }
    }
    assert(pi == pc);

    /* Set up the full placement lists for all squares, temporarily,
     * so as to use them to calculate the overlap lists */
    for (si = 0; si < wh; si++)
        sc->squares[si].nplacements = 0;
    for (pi = 0; pi < pc; pi++) {
        struct solver_placement *p = &sc->placements[pi];
        for (si = 0; si < 2; si++) {
            struct solver_square *sq = p->squares[si];
            p->spi[si] = sq->nplacements;
            sq->placements[sq->nplacements++] = p;
        }
    }

    /* Actually calculate the overlap lists */
    for (pi = 0; pi < pc; pi++) {
        struct solver_placement *p = &sc->placements[pi];
        p->noverlaps = 0;
        for (si = 0; si < 2; si++) {
            struct solver_square *sq = p->squares[si];
            int j;
            for (j = 0; j < sq->nplacements; j++) {
                struct solver_placement *q = sq->placements[j];
                if (q != p)
                    p->overlaps[p->noverlaps++] = q;
            }
        }
    }

    /* Fill in the index field of the placements */
    for (pi = 0; pi < pc; pi++)
        sc->placements[pi].index = pi;

    /* Lazily initialised by particular solver techniques that might
     * never be needed */
    sc->squares_by_number = NULL;
    sc->squares_by_number_initialised = false;
    sc->wh_scratch = NULL;
    sc->pc_scratch = sc->pc_scratch2 = NULL;
    sc->dc_scratch = NULL;

    return sc;
}

static void solver_free_scratch(struct solver_scratch *sc)
{
#ifdef SOLVER_DIAGNOSTICS
    {
        int i;
        for (i = 0; i < sc->dc; i++)
            sfree(sc->dominoes[i].name);
        for (i = 0; i < sc->pc; i++)
            sfree(sc->placements[i].name);
        for (i = 0; i < sc->wh; i++)
            sfree(sc->squares[i].name);
    }
#endif
    sfree(sc->dominoes);
    sfree(sc->placements);
    sfree(sc->squares);
    sfree(sc->domino_placement_lists);
    sfree(sc->squares_by_number);
    findloop_free_state(sc->fls);
    sfree(sc->wh_scratch);
    sfree(sc->pc_scratch);
    sfree(sc->pc_scratch2);
    sfree(sc->dc_scratch);
    sfree(sc);
}

static void solver_setup_grid(struct solver_scratch *sc, const int *numbers)
{
    int i, j;

    for (i = 0; i < sc->wh; i++) {
        sc->squares[i].nplacements = 0;
        sc->squares[i].number = numbers[sc->squares[i].index];
    }

    for (i = 0; i < sc->pc; i++) {
        struct solver_placement *p = &sc->placements[i];
        int di = DINDEX(p->squares[0]->number, p->squares[1]->number);
        p->domino = &sc->dominoes[di];
    }

    for (i = 0; i < sc->dc; i++)
        sc->dominoes[i].nplacements = 0;
    for (i = 0; i < sc->pc; i++)
        sc->placements[i].domino->nplacements++;
    for (i = j = 0; i < sc->dc; i++) {
        sc->dominoes[i].placements = sc->domino_placement_lists + j;
        j += sc->dominoes[i].nplacements;
        sc->dominoes[i].nplacements = 0;
    }
    for (i = 0; i < sc->pc; i++) {
        struct solver_placement *p = &sc->placements[i];
        p->dpi = p->domino->nplacements;
        p->domino->placements[p->domino->nplacements++] = p;
        p->active = true;
    }

    for (i = 0; i < sc->wh; i++)
        sc->squares[i].nplacements = 0;
    for (i = 0; i < sc->pc; i++) {
        struct solver_placement *p = &sc->placements[i];
        for (j = 0; j < 2; j++) {
            struct solver_square *sq = p->squares[j];
            p->spi[j] = sq->nplacements;
            sq->placements[sq->nplacements++] = p;
        }
    }

    sc->max_diff_used = DIFF_TRIVIAL;
    sc->squares_by_number_initialised = false;
}

/* Given two placements p,q that overlap, returns si such that
 * p->squares[si] is the square also in q */
static int common_square_index(struct solver_placement *p,
                               struct solver_placement *q)
{
    return (p->squares[0] == q->squares[0] ||
            p->squares[0] == q->squares[1]) ? 0 : 1;
}

/* Sort function used to set up squares_by_number */
static int squares_by_number_cmpfn(const void *av, const void *bv)
{
    struct solver_square *a = *(struct solver_square *const *)av;
    struct solver_square *b = *(struct solver_square *const *)bv;
    return (a->number < b->number ? -1 : a->number > b->number ? +1 :
            a->index  < b->index  ? -1 : a->index  > b->index  ? +1 : 0);
}

static void rule_out_placement(
    struct solver_scratch *sc, struct solver_placement *p)
{
    struct solver_domino *d = p->domino;
    int i, j, si;

#ifdef SOLVER_DIAGNOSTICS
    if (solver_diagnostics)
        printf("  ruling out placement %s for domino %s\n", p->name, d->name);
#endif

    p->active = false;

    i = p->dpi;
    assert(d->placements[i] == p);
    if (--d->nplacements != i) {
        d->placements[i] = d->placements[d->nplacements];
        d->placements[i]->dpi = i;
    }

    for (si = 0; si < 2; si++) {
        struct solver_square *sq = p->squares[si];
        i = p->spi[si];
        assert(sq->placements[i] == p);
        if (--sq->nplacements != i) {
            sq->placements[i] = sq->placements[sq->nplacements];
            j = (sq->placements[i]->squares[0] == sq ? 0 : 1);
            sq->placements[i]->spi[j] = i;
        }
    }
}

/*
 * If a domino has only one placement remaining, rule out all other
 * placements that overlap it.
 */
static bool deduce_domino_single_placement(struct solver_scratch *sc, int di)
{
    struct solver_domino *d = &sc->dominoes[di];
    struct solver_placement *p, *q;
    int oi;
    bool done_something = false;

    if (d->nplacements != 1)
        return false;
    p = d->placements[0];

    for (oi = 0; oi < p->noverlaps; oi++) {
        q = p->overlaps[oi];
        if (q->active) {
            if (!done_something) {
                done_something = true;
#ifdef SOLVER_DIAGNOSTICS
                if (solver_diagnostics)
                    printf("domino %s has unique placement %s\n",
                           d->name, p->name);
#endif
            }
            rule_out_placement(sc, q);
        }
    }

    return done_something;
}

/*
 * If a square has only one placement remaining, rule out all other
 * placements of its domino.
 */
static bool deduce_square_single_placement(struct solver_scratch *sc, int si)
{
    struct solver_square *sq = &sc->squares[si];
    struct solver_placement *p;
    struct solver_domino *d;

    if (sq->nplacements != 1)
        return false;
    p = sq->placements[0];
    d = p->domino;

    if (d->nplacements <= 1)
        return false;   /* we already knew everything this would tell us */

#ifdef SOLVER_DIAGNOSTICS
    if (solver_diagnostics)
        printf("square %s has unique placement %s (domino %s)\n",
               sq->name, p->name, p->domino->name);
#endif

    while (d->nplacements > 1)
        rule_out_placement(
            sc, d->placements[0] == p ? d->placements[1] : d->placements[0]);

    return true;
}

/*
 * If all placements for a square involve the same domino, rule out
 * all other placements of that domino.
 */
static bool deduce_square_single_domino(struct solver_scratch *sc, int si)
{
    struct solver_square *sq = &sc->squares[si];
    struct solver_domino *d;
    int i;

    /*
     * We only bother with this if the square has at least _two_
     * placements. If it only has one, then a simpler deduction will
     * have handled it already, or will do so the next time round the
     * main solver loop - and we should let the simpler deduction do
     * it, because that will give a less overblown diagnostic.
     */
    if (sq->nplacements < 2)
        return false;

    d = sq->placements[0]->domino;
    for (i = 1; i < sq->nplacements; i++)
        if (sq->placements[i]->domino != d)
            return false;              /* not all the same domino */

    if (d->nplacements <= sq->nplacements)
        return false;       /* no other placements of d to rule out */

#ifdef SOLVER_DIAGNOSTICS
    if (solver_diagnostics)
        printf("square %s can only contain domino %s\n", sq->name, d->name);
#endif

    for (i = d->nplacements; i-- > 0 ;) {
        struct solver_placement *p = d->placements[i];
        if (p->squares[0] != sq && p->squares[1] != sq)
            rule_out_placement(sc, p);
    }

    return true;
}

/*
 * If any placement is overlapped by _all_ possible placements of a
 * given domino, rule that placement out.
 */
static bool deduce_domino_must_overlap(struct solver_scratch *sc, int di)
{
    struct solver_domino *d = &sc->dominoes[di];
    struct solver_placement *intersection[6], *p;
    int nintersection = 0;
    int i, j, k;

    /*
     * As in deduce_square_single_domino, we only bother with this
     * deduction if the domino has at least two placements.
     */
    if (d->nplacements < 2)
        return false;

    /* Initialise our set of overlapped placements with all the active
     * ones overlapped by placements[0]. */
    p = d->placements[0];
    for (i = 0; i < p->noverlaps; i++)
        if (p->overlaps[i]->active)
            intersection[nintersection++] = p->overlaps[i];

    /* Now loop over the other placements of d, winnowing that set. */
    for (j = 1; j < d->nplacements; j++) {
        int old_n;

        p = d->placements[j];

        old_n = nintersection;
        nintersection = 0;

        for (k = 0; k < old_n; k++) {
            for (i = 0; i < p->noverlaps; i++)
                if (p->overlaps[i] == intersection[k])
                    goto found;
            /* If intersection[k] isn't in p->overlaps, exclude it
             * from our set of placements overlapped by everything */
            continue;
          found:
            intersection[nintersection++] = intersection[k];
        }
    }

    if (nintersection == 0)
        return false;                  /* no new exclusions */

    for (i = 0; i < nintersection; i++) {
        p = intersection[i];

#ifdef SOLVER_DIAGNOSTICS
        if (solver_diagnostics) {
            printf("placement %s of domino %s overlaps all placements "
                   "of domino %s:", p->name, p->domino->name, d->name);
            for (j = 0; j < d->nplacements; j++)
                printf(" %s", d->placements[j]->name);
            printf("\n");
        }
#endif
        rule_out_placement(sc, p);
    }

    return true;
}

/*
 * If a placement of domino D overlaps the only remaining placement
 * for some square S which is not also for domino D, then placing D
 * here would require another copy of it in S, so we can rule it out.
 */
static bool deduce_local_duplicate(struct solver_scratch *sc, int pi)
{
    struct solver_placement *p = &sc->placements[pi];
    struct solver_domino *d = p->domino;
    int i, j;

    if (!p->active)
        return false;

    for (i = 0; i < p->noverlaps; i++) {
        struct solver_placement *q = p->overlaps[i];
        struct solver_square *sq;

        if (!q->active)
            continue;

        /* Find the square of q that _isn't_ part of p */
        sq = q->squares[1 - common_square_index(q, p)];

        for (j = 0; j < sq->nplacements; j++)
            if (sq->placements[j] != q && sq->placements[j]->domino != d)
                goto no;

        /* If we get here, every possible placement for sq is either q
         * itself, or another copy of d. Success! We can rule out p. */
#ifdef SOLVER_DIAGNOSTICS
        if (solver_diagnostics) {
            printf("placement %s of domino %s would force another copy of %s "
                   "in square %s\n", p->name, d->name, d->name, sq->name);
        }
#endif

        rule_out_placement(sc, p);
        return true;

      no:;
    }

    return false;
}

/*
 * If placement P overlaps one placement for each of two squares S,T
 * such that all the remaining placements for both S and T are the
 * same domino D (and none of those placements joins S and T to each
 * other), then P can't be placed, because it would leave S,T each
 * having to be a copy of D, i.e. duplicates.
 */
static bool deduce_local_duplicate_2(struct solver_scratch *sc, int pi)
{
    struct solver_placement *p = &sc->placements[pi];
    int i, j, k;

    if (!p->active)
        return false;

    /*
     * Iterate over pairs of placements qi,qj overlapping p.
     */
    for (i = 0; i < p->noverlaps; i++) {
        struct solver_placement *qi = p->overlaps[i];
        struct solver_square *sqi;
        struct solver_domino *di = NULL;

        if (!qi->active)
            continue;

        /* Find the square of qi that _isn't_ part of p */
        sqi = qi->squares[1 - common_square_index(qi, p)];

        /*
         * Identify the unique domino involved in all possible
         * placements of sqi other than qi. If there isn't a unique
         * one (either too many or too few), move on and try the next
         * qi.
         */
        for (k = 0; k < sqi->nplacements; k++) {
            struct solver_placement *pk = sqi->placements[k];
            if (sqi->placements[k] == qi)
                continue;              /* not counting qi itself */
            if (!di)
                di = pk->domino;
            else if (di != pk->domino)
                goto done_qi;
        }
        if (!di)
            goto done_qi;

        /*
         * Now find an appropriate qj != qi.
         */
        for (j = 0; j < p->noverlaps; j++) {
            struct solver_placement *qj = p->overlaps[j];
            struct solver_square *sqj;
            bool found_di = false;

            if (j == i || !qj->active)
                continue;

            sqj = qj->squares[1 - common_square_index(qj, p)];

            /*
             * As above, we want the same domino di to be the only one
             * sqj can be if placement qj is ruled out. But also we
             * need no placement of sqj to overlap sqi.
             */
            for (k = 0; k < sqj->nplacements; k++) {
                struct solver_placement *pk = sqj->placements[k];
                if (pk == qj)
                    continue;          /* not counting qj itself */
                if (pk->domino != di)
                    goto done_qj;      /* found a different domino */
                if (pk->squares[0] == sqi || pk->squares[1] == sqi)
                    goto done_qj; /* sqi,sqj can be joined to each other */
                found_di = true;
            }
            if (!found_di)
                goto done_qj;

            /* If we get here, then every placement for either of sqi
             * and sqj is a copy of di, except for the ones that
             * overlap p. Success! We can rule out p. */
#ifdef SOLVER_DIAGNOSTICS
            if (solver_diagnostics) {
                printf("placement %s of domino %s would force squares "
                       "%s and %s to both be domino %s\n",
                       p->name, p->domino->name,
                       sqi->name, sqj->name, di->name);
            }
#endif
            rule_out_placement(sc, p);
            return true;

          done_qj:;
        }

      done_qi:;
    }

    return false;
}

struct parity_findloop_ctx {
    struct solver_scratch *sc;
    struct solver_square *sq;
    int i;
};

static int parity_neighbour(int vertex, void *vctx)
{
    struct parity_findloop_ctx *ctx = (struct parity_findloop_ctx *)vctx;
    struct solver_placement *p;

    if (vertex >= 0) {
        ctx->sq = &ctx->sc->squares[vertex];
        ctx->i = 0;
    } else {
        assert(ctx->sq);
    }

    if (ctx->i >= ctx->sq->nplacements) {
        ctx->sq = NULL;
        return -1;
    }

    p = ctx->sq->placements[ctx->i++];
    return p->squares[0]->index + p->squares[1]->index - ctx->sq->index;
}

/*
 * Look for dominoes whose placement would disconnect the unfilled
 * area of the grid into pieces with odd area. Such a domino can't be
 * placed, because then the area on each side of it would be
 * untileable.
 */
static bool deduce_parity(struct solver_scratch *sc)
{
    struct parity_findloop_ctx pflctx;
    bool done_something = false;
    int pi;

    /*
     * Run findloop, aka Tarjan's bridge-finding algorithm, on the
     * graph whose vertices are squares, with two vertices separated
     * by an edge iff some not-yet-ruled-out domino placement covers
     * them both. (So each edge itself corresponds to a domino
     * placement.)
     *
     * The effect is that any bridge in this graph is a domino whose
     * placement would separate two previously connected areas of the
     * unfilled squares of the grid.
     *
     * Placing that domino would not just disconnect those areas from
     * each other, but also use up one square of each. So if we want
     * to avoid leaving two odd areas after placing the domino, it
     * follows that we want to avoid the bridge having an _even_
     * number of vertices on each side.
     */
    pflctx.sc = sc;
    findloop_run(sc->fls, sc->wh, parity_neighbour, &pflctx);

    for (pi = 0; pi < sc->pc; pi++) {
        struct solver_placement *p = &sc->placements[pi];
        int size0, size1;

        if (!p->active)
            continue;
        if (!findloop_is_bridge(
                sc->fls, p->squares[0]->index, p->squares[1]->index,
                &size0, &size1))
            continue;
        /* To make a deduction, size0 and size1 must both be even,
         * i.e. after placing this domino decrements each by 1 they
         * would both become odd and untileable areas. */
        if ((size0 | size1) & 1)
            continue;

#ifdef SOLVER_DIAGNOSTICS
        if (solver_diagnostics) {
            printf("placement %s of domino %s would create two odd-sized "
                   "areas\n", p->name, p->domino->name);
        }
#endif
        rule_out_placement(sc, p);
        done_something = true;
    }

    return done_something;
}

/*
 * Try to find a set of squares all containing the same number, such
 * that the set of possible dominoes for all the squares in that set
 * is small enough to let us rule out placements of those dominoes
 * elsewhere.
 */
static bool deduce_set(struct solver_scratch *sc, bool doubles)
{
    struct solver_square **sqs, **sqp, **sqe;
    int num, nsq, i, j;
    unsigned long domino_sets[16], adjacent[16];
    struct solver_domino *ds[16];
    bool done_something = false;

    if (!sc->squares_by_number)
        sc->squares_by_number = snewn(sc->wh, struct solver_square *);
    if (!sc->wh_scratch)
        sc->wh_scratch = snewn(sc->wh, int);

    if (!sc->squares_by_number_initialised) {
        /*
         * If this is the first call to this function for a given
         * grid, start by sorting the squares by their containing
         * number.
         */
        for (i = 0; i < sc->wh; i++)
            sc->squares_by_number[i] = &sc->squares[i];
        qsort(sc->squares_by_number, sc->wh, sizeof(*sc->squares_by_number),
              squares_by_number_cmpfn);
    }

    sqp = sc->squares_by_number;
    sqe = sc->squares_by_number + sc->wh;
    for (num = 0; num <= sc->n; num++) {
        unsigned long squares;
        unsigned long squares_done;

        /* Find the bounds of the subinterval of squares_by_number
         * containing squares with this particular number. */
        sqs = sqp;
        while (sqp < sqe && (*sqp)->number == num)
            sqp++;
        nsq = sqp - sqs;

        /*
         * Now sqs[0], ..., sqs[nsq-1] are the squares containing 'num'.
         */

        if (nsq > lenof(domino_sets)) {
            /*
             * Abort this analysis if we're trying to enumerate all
             * the subsets of a too-large base set.
             *
             * This _shouldn't_ happen, at the time of writing this
             * code, because the largest puzzle we support is only
             * supposed to have 10 instances of each number, and part
             * of our input grid validation checks that each number
             * does appear the right number of times. But just in case
             * weird test input makes its way to this function, or the
             * puzzle sizes are expanded later, it's easy enough to
             * just rule out doing this analysis for overlarge sets of
             * numbers.
             */
            continue;
        }

        /*
         * Index the squares in wh_scratch, which we're using as a
         * lookup table to map the official index of a square back to
         * its value in our local indexing scheme.
         */
        for (i = 0; i < nsq; i++)
            sc->wh_scratch[sqs[i]->index] = i;

        /*
         * For each square, make a bit mask of the dominoes that can
         * overlap it, by finding the number at the other end of each
         * one.
         *
         * Also, for each square, make a bit mask of other squares in
         * the current list that might occupy the _same_ domino
         * (because a possible placement of a double overlaps both).
         * We'll need that for evaluating whether sets are properly
         * exhaustive.
         */
        for (i = 0; i < nsq; i++)
            adjacent[i] = 0;

        for (i = 0; i < nsq; i++) {
            struct solver_square *sq = sqs[i];
            unsigned long mask = 0;

            for (j = 0; j < sq->nplacements; j++) {
                struct solver_placement *p = sq->placements[j];
                int othernum = p->domino->lo + p->domino->hi - num;
                mask |= 1UL << othernum;
                ds[othernum] = p->domino; /* so we can find them later */

                if (othernum == num) {
                    /*
                     * Special case: this is a double, so it gives
                     * rise to entries in adjacent[].
                     */
                    int i2 = sc->wh_scratch[p->squares[0]->index +
                                            p->squares[1]->index - sq->index];
                    adjacent[i] |= 1UL << i2;
                    adjacent[i2] |= 1UL << i;
                }
            }

            domino_sets[i] = mask;

        }

        squares_done = 0;

        for (squares = 0; squares < (1UL << nsq); squares++) {
            unsigned long dominoes = 0;
            int bitpos, nsquares, ndominoes;
            bool got_adj_squares = false;
            bool reported = false;
            bool rule_out_nondoubles;
            int min_nused_for_double;
#ifdef SOLVER_DIAGNOSTICS
            const char *rule_out_text;
#endif

            /*
             * We don't do set analysis on the same square of the grid
             * more than once in this loop. Otherwise you generate
             * pointlessly overcomplicated diagnostics for simpler
             * follow-up deductions. For example, suppose squares
             * {A,B} must go with dominoes {X,Y}. So you rule out X,Y
             * elsewhere, and then it turns out square C (from which
             * one of those was eliminated) has only one remaining
             * possibility Z. What you _don't_ want to do is
             * triumphantly report a second case of set elimination
             * where you say 'And also, squares {A,B,C} have to be
             * {X,Y,Z}!' You'd prefer to give 'now C has to be Z' as a
             * separate deduction later, more simply phrased.
             */
            if (squares & squares_done)
                continue;

            nsquares = 0;

            /* Make the set of dominoes that these squares can inhabit. */
            for (bitpos = 0; bitpos < nsq; bitpos++) {
                if (!(1 & (squares >> bitpos)))
                    continue;          /* this bit isn't set in the mask */

                if (adjacent[bitpos] & squares)
                    got_adj_squares = true;

                dominoes |= domino_sets[bitpos];
                nsquares++;
            }

            /* Count them. */
            ndominoes = 0;
            for (bitpos = 0; bitpos < nsq; bitpos++)
                ndominoes += 1 & (dominoes >> bitpos);

            /*
             * Do the two sets have the right relative size?
             */
            if (!got_adj_squares) {
                /*
                 * The normal case, in which every possible domino
                 * placement involves at most _one_ of these squares.
                 *
                 * This is exactly analogous to the set analysis
                 * deductions in many other puzzles: if our N squares
                 * between them have to account for N distinct
                 * dominoes, with exactly one of those dominoes to
                 * each square, then all those dominoes correspond to
                 * all those squares and we can rule out any
                 * placements of the same dominoes appearing
                 * elsewhere.
                 */
                if (ndominoes != nsquares)
                    continue;
                rule_out_nondoubles = true;
                min_nused_for_double = 1;
#ifdef SOLVER_DIAGNOSTICS
                rule_out_text = "all of them elsewhere";
#endif
            } else {
                if (!doubles)
                    continue;          /* not at this difficulty level */

                /*
                 * But in Dominosa, there's a special case if _two_
                 * squares in this set can possibly both be covered by
                 * the same double domino. (I.e. if they are adjacent,
                 * and moreover, the double-domino placement
                 * containing both is not yet ruled out.)
                 *
                 * In that situation, the simple argument doesn't hold
                 * up, because the N squares might be covered by N-1
                 * dominoes - or, put another way, if you list the
                 * containing domino for each of the squares, they
                 * might not be all distinct.
                 *
                 * In that situation, we can still do something, but
                 * the details vary, and there are two further cases.
                 */
                if (ndominoes == nsquares-1) {
                    /*
                     * Suppose there is one _more_ square in our set
                     * than there are dominoes it can involve. For
                     * example, suppose we had four '0' squares which
                     * between them could contain only the 0-0, 0-1
                     * and 0-2 dominoes.
                     *
                     * Then that can only work at all if the 0-0
                     * covers two of those squares - and in that
                     * situation that _must_ be what's happened.
                     *
                     * So we can rule out the 0-1 and 0-2 dominoes (in
                     * this example) in any placement that doesn't use
                     * one of the squares in this set. And we can rule
                     * out a placement of the 0-0 even if it uses
                     * _one_ square from this set: in this situation,
                     * we have to insist on it using _two_.
                     */
                    rule_out_nondoubles = true;
                    min_nused_for_double = 2;
#ifdef SOLVER_DIAGNOSTICS
                    rule_out_text = "all of them elsewhere "
                        "(including the double if it fails to use both)";
#endif
                } else if (ndominoes == nsquares) {
                    /*
                     * A restricted form of the deduction is still
                     * possible if we have the same number of dominoes
                     * as squares.
                     *
                     * If we have _three_ '0' squares none of which
                     * can be any domino other than 0-0, 0-1 and 0-2,
                     * and there's still a possibility of an 0-0
                     * domino using up two of them, then we can't rule
                     * out 0-1 or 0-2 anywhere else, because it's
                     * possible that these three squares only use two
                     * of the dominoes between them.
                     *
                     * But we _can_ rule out the double 0-0, in any
                     * placement that uses _none_ of our three
                     * squares. Because we do know that _at least one_
                     * of our squares must be involved in the 0-0, or
                     * else the three of them would only have the
                     * other two dominoes left.
                     */
                    rule_out_nondoubles = false;
                    min_nused_for_double = 1;
#ifdef SOLVER_DIAGNOSTICS
                    rule_out_text = "the double elsewhere";
#endif
                } else {
                    /*
                     * If none of those cases has happened, then our
                     * set admits no deductions at all.
                     */
                    continue;
                }
            }

            /* Skip sets of size 1, or whose complement has size 1.
             * Those can be handled by a simpler analysis, and should
             * be, for more sensible solver diagnostics. */
            if (ndominoes <= 1 || ndominoes >= nsq-1)
                continue;

            /*
             * We've found a set! That means we can rule out any
             * placement of any domino in that set which would leave
             * the squares in the set with too few dominoes between
             * them.
             *
             * We may or may not actually end up ruling anything out
             * here. But even if we don't, we should record that these
             * squares form a self-contained set, so that we don't
             * pointlessly report a superset of them later which could
             * instead be reported as just the other ones.
             *
             * Or rather, we do that for the main cases that let us
             * rule out lots of dominoes. We only do this with the
             * borderline case where we can only rule out a double if
             * we _actually_ rule something out. Otherwise we'll never
             * even _find_ a larger set with the same number of
             * dominoes!
             */
            if (rule_out_nondoubles)
                squares_done |= squares;

            for (bitpos = 0; bitpos < nsq; bitpos++) {
                struct solver_domino *d;

                if (!(1 & (dominoes >> bitpos)))
                    continue;
                d = ds[bitpos];

                for (i = d->nplacements; i-- > 0 ;) {
                    struct solver_placement *p = d->placements[i];
                    int si, nused;

                    /* Count how many of our squares this placement uses. */
                    for (si = nused = 0; si < 2; si++) {
                        struct solver_square *sq2 = p->squares[si];
                        if (sq2->number == num &&
                            (1 & (squares >> sc->wh_scratch[sq2->index])))
                            nused++;
                    }

                    /* See if that's too many to rule it out. */
                    if (d->lo == d->hi) {
                        if (nused >= min_nused_for_double)
                            continue;
                    } else {
                        if (nused > 0 || !rule_out_nondoubles)
                            continue;
                    }

                    if (!reported) {
                        reported = true;
                        done_something = true;

                        /* In case we didn't do this above */
                        squares_done |= squares;

#ifdef SOLVER_DIAGNOSTICS
                        if (solver_diagnostics) {
                            int b;
                            const char *sep;
                            printf("squares {");
                            for (sep = "", b = 0; b < nsq; b++)
                                if (1 & (squares >> b)) {
                                    printf("%s%s", sep, sqs[b]->name);
                                    sep = ",";
                                }
                            printf("} can contain only dominoes {");
                            for (sep = "", b = 0; b < nsq; b++)
                                if (1 & (dominoes >> b)) {
                                    printf("%s%s", sep, ds[b]->name);
                                    sep = ",";
                                }
                            printf("}, so rule out %s", rule_out_text);
                            printf("\n");
                        }
#endif
                    }

                    rule_out_placement(sc, p);
                }
            }
        }

    }

    return done_something;
}

static int forcing_chain_dup_cmp(const void *av, const void *bv, void *ctx)
{
    struct solver_scratch *sc = (struct solver_scratch *)ctx;
    int a = *(const int *)av, b = *(const int *)bv;
    int ac, bc;

    ac = sc->pc_scratch[a];
    bc = sc->pc_scratch[b];
    if (ac != bc) return ac > bc ? +1 : -1;

    ac = sc->placements[a].domino->index;
    bc = sc->placements[b].domino->index;
    if (ac != bc) return ac > bc ? +1 : -1;

    return 0;
}

static int forcing_chain_sq_cmp(const void *av, const void *bv, void *ctx)
{
    struct solver_scratch *sc = (struct solver_scratch *)ctx;
    int a = *(const int *)av, b = *(const int *)bv;
    int ac, bc;

    ac = sc->placements[a].domino->index;
    bc = sc->placements[b].domino->index;
    if (ac != bc) return ac > bc ? +1 : -1;

    ac = sc->pc_scratch[a];
    bc = sc->pc_scratch[b];
    if (ac != bc) return ac > bc ? +1 : -1;

    return 0;
}

static bool deduce_forcing_chain(struct solver_scratch *sc)
{
    int si, pi, di, j, k, m;
    bool done_something = false;

    if (!sc->wh_scratch)
        sc->wh_scratch = snewn(sc->wh, int);
    if (!sc->pc_scratch)
        sc->pc_scratch = snewn(sc->pc, int);
    if (!sc->pc_scratch2)
        sc->pc_scratch2 = snewn(sc->pc, int);
    if (!sc->dc_scratch)
        sc->dc_scratch = snewn(sc->dc, int);

    /*
     * Start by identifying chains of placements which must all occur
     * together if any of them occurs. We do this by making
     * pc_scratch2 an edsf binding the placements into an equivalence
     * class for each entire forcing chain, with the two possible sets
     * of dominoes for the chain listed as inverses.
     */
    dsf_init(sc->pc_scratch2, sc->pc);
    for (si = 0; si < sc->wh; si++) {
        struct solver_square *sq = &sc->squares[si];
        if (sq->nplacements == 2)
            edsf_merge(sc->pc_scratch2,
                       sq->placements[0]->index,
                       sq->placements[1]->index, true);
    }
    /*
     * Now read out the whole dsf into pc_scratch, flattening its
     * structured data into a simple integer id per chain of dominoes
     * that must occur together.
     *
     * The integer ids have the property that any two that differ only
     * in the lowest bit (i.e. of the form {2n,2n+1}) represent
     * complementary chains, each of which rules out the other.
     */
    for (pi = 0; pi < sc->pc; pi++) {
        bool inv;
        int c = edsf_canonify(sc->pc_scratch2, pi, &inv);
        sc->pc_scratch[pi] = c * 2 + (inv ? 1 : 0);
    }

    /*
     * Identify chains that contain a duplicate domino, and rule them
     * out. We do this by making a list of the placement indices in
     * pc_scratch2, sorted by (chain id, domino id), so that dupes
     * become adjacent.
     */
    for (pi = 0; pi < sc->pc; pi++)
        sc->pc_scratch2[pi] = pi;
    arraysort(sc->pc_scratch2, sc->pc, forcing_chain_dup_cmp, sc);

    for (j = 0; j < sc->pc ;) {
        struct solver_domino *duplicated_domino = NULL;

        /*
         * This loop iterates once per contiguous segment of the same
         * value in pc_scratch2, i.e. once per chain.
         */
        int ci = sc->pc_scratch[sc->pc_scratch2[j]];
        int climit, cstart = j;
        while (j < sc->pc && sc->pc_scratch[sc->pc_scratch2[j]] == ci)
            j++;
        climit = j;

        /*
         * Now look for a duplicate domino within that chain.
         */
        for (k = cstart; k + 1 < climit; k++) {
            struct solver_placement *p = &sc->placements[sc->pc_scratch2[k]];
            struct solver_placement *q = &sc->placements[sc->pc_scratch2[k+1]];
            if (p->domino == q->domino) {
                duplicated_domino = p->domino;
                break;
            }
        }

        if (!duplicated_domino)
            continue;

#ifdef SOLVER_DIAGNOSTICS
        if (solver_diagnostics) {
            printf("domino %s occurs more than once in forced chain:",
                   duplicated_domino->name);
            for (k = cstart; k < climit; k++)
                printf(" %s", sc->placements[sc->pc_scratch2[k]].name);
            printf("\n");
        }
#endif

        for (k = cstart; k < climit; k++)
            rule_out_placement(sc, &sc->placements[sc->pc_scratch2[k]]);

        done_something = true;
    }

    if (done_something)
        return true;

    /*
     * A second way in which a whole forcing chain can be ruled out is
     * if it contains all the dominoes that can occupy some other
     * square, so that if the domnioes in the chain were all laid, the
     * other square would be left without any choices.
     *
     * To detect this, we sort the placements again, this time by
     * (domino index, chain index), so that we can easily find a
     * sorted list of chains per domino. That allows us to iterate
     * over the squares and check for a chain id common to all the
     * placements of that square.
     */
    for (pi = 0; pi < sc->pc; pi++)
        sc->pc_scratch2[pi] = pi;
    arraysort(sc->pc_scratch2, sc->pc, forcing_chain_sq_cmp, sc);

    /* Store a lookup table of the first entry in pc_scratch2
     * corresponding to each domino. */
    for (di = j = 0; j < sc->pc; j++) {
        while (di <= sc->placements[sc->pc_scratch2[j]].domino->index) {
            assert(di < sc->dc);
            sc->dc_scratch[di++] = j;
        }
    }
    assert(di == sc->dc);

    for (si = 0; si < sc->wh; si++) {
        struct solver_square *sq = &sc->squares[si];
        int listpos = 0, listsize = 0, listout = 0;
        int exclude[4];
        int n_exclude;

        if (sq->nplacements < 2)
            continue;              /* too simple to be worth trying */

        /*
         * Start by checking for chains this square can actually form
         * part of. We won't consider those. (The aim is to find a
         * completely _different_ square whose placements are all
         * ruled out by a chain.)
         */
        assert(sq->nplacements <= lenof(exclude));
        for (j = n_exclude = 0; j < sq->nplacements; j++)
            exclude[n_exclude++] = sc->pc_scratch[sq->placements[j]->index];

        for (j = 0; j < sq->nplacements; j++) {
            struct solver_domino *d = sq->placements[j]->domino;

            listout = listpos = 0;

            for (k = sc->dc_scratch[d->index];
                 k < sc->pc && sc->placements[sc->pc_scratch2[k]].domino == d;
                 k++) {
                int chain = sc->pc_scratch[sc->pc_scratch2[k]];
                bool keep;

                if (!sc->placements[sc->pc_scratch2[k]].active)
                    continue;

                if (j == 0) {
                    keep = true;
                } else {
                    while (listpos < listsize &&
                           sc->wh_scratch[listpos] < chain)
                        listpos++;
                    keep = (listpos < listsize &&
                            sc->wh_scratch[listpos] == chain);
                }

                for (m = 0; m < n_exclude; m++)
                    if (chain == exclude[m])
                        keep = false;

                if (keep)
                    sc->wh_scratch[listout++] = chain;
            }

            listsize = listout;
            if (listsize == 0)
                break; /* ruled out all chains; terminate loop early */
        }

        for (listpos = 0; listpos < listsize; listpos++) {
            int chain = sc->wh_scratch[listpos];

            /*
             * We've found a chain we can rule out.
             */
#ifdef SOLVER_DIAGNOSTICS
            if (solver_diagnostics) {
                printf("all choices for square %s would be ruled out "
                       "by forced chain:", sq->name);
                for (pi = 0; pi < sc->pc; pi++)
                    if (sc->pc_scratch[pi] == chain)
                        printf(" %s", sc->placements[pi].name);
                printf("\n");
            }
#endif

            for (pi = 0; pi < sc->pc; pi++)
                if (sc->pc_scratch[pi] == chain)
                    rule_out_placement(sc, &sc->placements[pi]);

            done_something = true;
        }
    }

    /*
     * Another thing you can do with forcing chains, besides ruling
     * out a whole one at a time, is to look at each pair of chains
     * that overlap each other. Each such pair gives you two sets of
     * domino placements, such that if either set is not placed, then
     * the other one must be.
     *
     * This means that any domino which has a placement in _both_
     * chains of a pair must occupy one of those two placements, i.e.
     * we can rule that domino out anywhere else it might appear.
     */
    for (di = 0; di < sc->dc; di++) {
        struct solver_domino *d = &sc->dominoes[di];

        if (d->nplacements <= 2)
            continue;      /* not enough placements to rule one out */

        for (j = 0; j+1 < d->nplacements; j++) {
            int ij = d->placements[j]->index;
            int cj = sc->pc_scratch[ij];
            for (k = j+1; k < d->nplacements; k++) {
                int ik = d->placements[k]->index;
                int ck = sc->pc_scratch[ik];
                if ((cj ^ ck) == 1) {
                    /*
                     * Placements j,k of domino d are in complementary
                     * chains, so we can rule out all the others.
                     */
                    int i;

#ifdef SOLVER_DIAGNOSTICS
                    if (solver_diagnostics) {
                        printf("domino %s occurs in both complementary "
                               "forced chains:", d->name);
                        for (i = 0; i < sc->pc; i++)
                            if (sc->pc_scratch[i] == cj)
                                printf(" %s", sc->placements[i].name);
                        printf(" and");
                        for (i = 0; i < sc->pc; i++)
                            if (sc->pc_scratch[i] == ck)
                                printf(" %s", sc->placements[i].name);
                        printf("\n");
                    }
#endif

                    for (i = d->nplacements; i-- > 0 ;)
                        if (i != j && i != k)
                            rule_out_placement(sc, d->placements[i]);

                    done_something = true;
                    goto done_this_domino;
                }
            }
        }

      done_this_domino:;
    }

    return done_something;
}

/*
 * Run the solver until it can't make any more progress.
 *
 * Return value is:
 *   0 = no solution exists (puzzle clues are unsatisfiable)
 *   1 = unique solution found (success!)
 *   2 = multiple possibilities remain (puzzle is ambiguous or solver is not
 *                                      smart enough)
 */
static int run_solver(struct solver_scratch *sc, int max_diff_allowed)
{
    int di, si, pi;
    bool done_something;

#ifdef SOLVER_DIAGNOSTICS
    if (solver_diagnostics) {
        int di, j;
        printf("Initial possible placements:\n");
        for (di = 0; di < sc->dc; di++) {
            struct solver_domino *d = &sc->dominoes[di];
            printf("  %s:", d->name);
            for (j = 0; j < d->nplacements; j++)
                printf(" %s", d->placements[j]->name);
            printf("\n");
        }
    }
#endif

    do {
        done_something = false;

        for (di = 0; di < sc->dc; di++)
            if (deduce_domino_single_placement(sc, di))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_TRIVIAL);
            continue;
        }

        for (si = 0; si < sc->wh; si++)
            if (deduce_square_single_placement(sc, si))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_TRIVIAL);
            continue;
        }

        if (max_diff_allowed <= DIFF_TRIVIAL)
            continue;

        for (si = 0; si < sc->wh; si++)
            if (deduce_square_single_domino(sc, si))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_BASIC);
            continue;
        }

        for (di = 0; di < sc->dc; di++)
            if (deduce_domino_must_overlap(sc, di))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_BASIC);
            continue;
        }

        for (pi = 0; pi < sc->pc; pi++)
            if (deduce_local_duplicate(sc, pi))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_BASIC);
            continue;
        }

        for (pi = 0; pi < sc->pc; pi++)
            if (deduce_local_duplicate_2(sc, pi))
                done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_BASIC);
            continue;
        }

        if (deduce_parity(sc))
            done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_BASIC);
            continue;
        }

        if (max_diff_allowed <= DIFF_BASIC)
            continue;

        if (deduce_set(sc, false))
            done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_HARD);
            continue;
        }

        if (max_diff_allowed <= DIFF_HARD)
            continue;

        if (deduce_set(sc, true))
            done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_EXTREME);
            continue;
        }

        if (deduce_forcing_chain(sc))
            done_something = true;
        if (done_something) {
            sc->max_diff_used = max(sc->max_diff_used, DIFF_EXTREME);
            continue;
        }

    } while (done_something);

#ifdef SOLVER_DIAGNOSTICS
    if (solver_diagnostics) {
        int di, j;
        printf("Final possible placements:\n");
        for (di = 0; di < sc->dc; di++) {
            struct solver_domino *d = &sc->dominoes[di];
            printf("  %s:", d->name);
            for (j = 0; j < d->nplacements; j++)
                printf(" %s", d->placements[j]->name);
            printf("\n");
        }
    }
#endif

    for (di = 0; di < sc->dc; di++)
        if (sc->dominoes[di].nplacements == 0)
            return 0;
    for (di = 0; di < sc->dc; di++)
        if (sc->dominoes[di].nplacements > 1)
            return 2;
    return 1;
}

/* ----------------------------------------------------------------------
 * Functions for generating a candidate puzzle (before we run the
 * solver to check it's soluble at the right difficulty level).
 */

struct alloc_val;
struct alloc_loc;

struct alloc_scratch {
    /* Game parameters. */
    int n, w, h, wh, dc;

    /* The domino layout. Indexed by squares in the usual y*w+x raster
     * order: layout[i] gives the index of the other square in the
     * same domino as square i. */
    int *layout;

    /* The output array, containing a number in every square. */
    int *numbers;

    /* List of domino values (i.e. number pairs), indexed by DINDEX. */
    struct alloc_val *vals;

    /* List of domino locations, indexed arbitrarily. */
    struct alloc_loc *locs;

    /* Preallocated scratch spaces. */
    int *wh_scratch;                   /* size wh */
    int *wh2_scratch;                  /* size 2*wh */
};

struct alloc_val {
    int lo, hi;
    bool confounder;
};

struct alloc_loc {
    int sq[2];
};

static struct alloc_scratch *alloc_make_scratch(int n)
{
    struct alloc_scratch *as = snew(struct alloc_scratch);
    int lo, hi;

    as->n = n;
    as->w = n+2;
    as->h = n+1;
    as->wh = as->w * as->h;
    as->dc = DCOUNT(n);

    as->layout = snewn(as->wh, int);
    as->numbers = snewn(as->wh, int);
    as->vals = snewn(as->dc, struct alloc_val);
    as->locs = snewn(as->dc, struct alloc_loc);
    as->wh_scratch = snewn(as->wh, int);
    as->wh2_scratch = snewn(as->wh * 2, int);

    for (hi = 0; hi <= n; hi++)
        for (lo = 0; lo <= hi; lo++) {
            struct alloc_val *v = &as->vals[DINDEX(hi, lo)];
            v->lo = lo;
            v->hi = hi;
        }

    return as;
}

static void alloc_free_scratch(struct alloc_scratch *as)
{
    sfree(as->layout);
    sfree(as->numbers);
    sfree(as->vals);
    sfree(as->locs);
    sfree(as->wh_scratch);
    sfree(as->wh2_scratch);
    sfree(as);
}

static void alloc_make_layout(struct alloc_scratch *as, random_state *rs)
{
    int i, pos;

    domino_layout_prealloc(as->w, as->h, rs,
                           as->layout, as->wh_scratch, as->wh2_scratch);

    for (i = pos = 0; i < as->wh; i++) {
        if (as->layout[i] > i) {
            struct alloc_loc *loc;
            assert(pos < as->dc);

            loc = &as->locs[pos++];
            loc->sq[0] = i;
            loc->sq[1] = as->layout[i];
        }
    }
    assert(pos == as->dc);
}

static void alloc_trivial(struct alloc_scratch *as, random_state *rs)
{
    int i;
    for (i = 0; i < as->dc; i++)
        as->wh_scratch[i] = i;
    shuffle(as->wh_scratch, as->dc, sizeof(*as->wh_scratch), rs);

    for (i = 0; i < as->dc; i++) {
        struct alloc_val *val = &as->vals[as->wh_scratch[i]];
        struct alloc_loc *loc = &as->locs[i];
        int which_lo = random_upto(rs, 2), which_hi = 1 - which_lo;
        as->numbers[loc->sq[which_lo]] = val->lo;
        as->numbers[loc->sq[which_hi]] = val->hi;
    }
}

/*
 * Given a domino location in the form of two square indices, compute
 * the square indices of the domino location that would lie on one
 * side of it. Returns false if the location would be outside the
 * grid, or if it isn't actually a domino in the layout.
 */
static bool alloc_find_neighbour(
    struct alloc_scratch *as, int p0, int p1, int *n0, int *n1)
{
    int x0 = p0 % as->w, y0 = p0 / as->w, x1 = p1 % as->w, y1 = p1 / as->w;
    int dy = y1-y0, dx = x1-x0;
    int nx0 = x0 + dy, ny0 = y0 - dx, nx1 = x1 + dy, ny1 = y1 - dx;
    int np0, np1;

    if (!(nx0 >= 0 && nx0 < as->w && ny0 >= 0 && ny0 < as->h &&
          nx1 >= 1 && nx1 < as->w && ny1 >= 1 && ny1 < as->h))
        return false;                  /* out of bounds */

    np0 = ny0 * as->w + nx0;
    np1 = ny1 * as->w + nx1;
    if (as->layout[np0] != np1)
        return false;                  /* not a domino */

    *n0 = np0;
    *n1 = np1;
    return true;
}

static bool alloc_try_unique(struct alloc_scratch *as, random_state *rs)
{
    int i;
    for (i = 0; i < as->dc; i++)
        as->wh_scratch[i] = i;
    shuffle(as->wh_scratch, as->dc, sizeof(*as->wh_scratch), rs);
    for (i = 0; i < as->dc; i++)
        as->wh2_scratch[i] = i;
    shuffle(as->wh2_scratch, as->dc, sizeof(*as->wh2_scratch), rs);

    for (i = 0; i < as->wh; i++)
        as->numbers[i] = -1;

    for (i = 0; i < as->dc; i++) {
        struct alloc_val *val = &as->vals[as->wh_scratch[i]];
        struct alloc_loc *loc = &as->locs[as->wh2_scratch[i]];
        int which_lo, which_hi;
        bool can_lo_0 = true, can_lo_1 = true;
        int n0, n1;

        /*
         * This is basically the same strategy as alloc_trivial:
         * simply iterate through the locations and values in random
         * relative order and pair them up. But we make sure to avoid
         * the most common, and also simplest, cause of a non-unique
         * solution:two dominoes side by side, sharing a number at
         * opposite ends. Any section of that form automatically leads
         * to an alternative solution:
         *
         *  +-------+         +---+---+
         *  | 1   2 |         | 1 | 2 |
         *  +-------+   <->   |   |   |
         *  | 2   3 |         | 2 | 3 |
         *  +-------+         +---+---+
         *
         * So as we place each domino, we check for a neighbouring
         * domino on each side, and if there is one, rule out any
         * placement of _this_ domino that places a number diagonally
         * opposite the same number in the neighbour.
         *
         * Sometimes this can fail completely, if a domino on each
         * side is already placed and between them they rule out all
         * placements of this one. But it happens rarely enough that
         * it's fine to just abort and try the layout again.
         */

        if (alloc_find_neighbour(as, loc->sq[0], loc->sq[1], &n0, &n1) &&
            (as->numbers[n0] == val->hi || as->numbers[n1] == val->lo))
            can_lo_0 = false;
        if (alloc_find_neighbour(as, loc->sq[1], loc->sq[0], &n0, &n1) &&
            (as->numbers[n0] == val->hi || as->numbers[n1] == val->lo))
            can_lo_1 = false;

        if (!can_lo_0 && !can_lo_1)
            return false;              /* layout failed */
        else if (can_lo_0 && can_lo_1)
            which_lo = random_upto(rs, 2);
        else
            which_lo = can_lo_0 ? 0 : 1;

        which_hi = 1 - which_lo;
        as->numbers[loc->sq[which_lo]] = val->lo;
        as->numbers[loc->sq[which_hi]] = val->hi;
    }

    return true;
}

static bool alloc_try_hard(struct alloc_scratch *as, random_state *rs)
{
    int i, x, y, hi, lo, vals, locs, confounders_needed;
    bool ok;

    for (i = 0; i < as->wh; i++)
        as->numbers[i] = -1;

    /*
     * Shuffle the location indices.
     */
    for (i = 0; i < as->dc; i++)
        as->wh2_scratch[i] = i;
    shuffle(as->wh2_scratch, as->dc, sizeof(*as->wh2_scratch), rs);

    /*
     * Start by randomly placing the double dominoes, to give a
     * starting instance of every number to try to put other things
     * next to.
     */
    for (i = 0; i <= as->n; i++)
        as->wh_scratch[i] = DINDEX(i, i);
    shuffle(as->wh_scratch, i, sizeof(*as->wh_scratch), rs);
    for (i = 0; i <= as->n; i++) {
        struct alloc_loc *loc = &as->locs[as->wh2_scratch[i]];
        as->numbers[loc->sq[0]] = as->numbers[loc->sq[1]] = i;
    }

    /*
     * Find all the dominoes that don't yet have a _wrong_ placement
     * somewhere in the grid.
     */
    for (i = 0; i < as->dc; i++)
        as->vals[i].confounder = false;
    for (y = 0; y < as->h; y++) {
        for (x = 0; x < as->w; x++) {
            int p = y * as->w + x;
            if (as->numbers[p] == -1)
                continue;

            if (x+1 < as->w) {
                int p1 = y * as->w + (x+1);
                if (as->layout[p] != p1 && as->numbers[p1] != -1)
                    as->vals[DINDEX(as->numbers[p], as->numbers[p1])]
                        .confounder = true;
            }
            if (y+1 < as->h) {
                int p1 = (y+1) * as->w + x;
                if (as->layout[p] != p1 && as->numbers[p1] != -1)
                    as->vals[DINDEX(as->numbers[p], as->numbers[p1])]
                        .confounder = true;
            }
        }
    }

    for (i = confounders_needed = 0; i < as->dc; i++)
        if (!as->vals[i].confounder)
            confounders_needed++;

    /*
     * Make a shuffled list of all the unplaced dominoes, and go
     * through it trying to find a placement for each one that also
     * fills in at least one of the needed confounders.
     */
    vals = 0;
    for (hi = 0; hi <= as->n; hi++)
        for (lo = 0; lo < hi; lo++)
            as->wh_scratch[vals++] = DINDEX(hi, lo);
    shuffle(as->wh_scratch, vals, sizeof(*as->wh_scratch), rs);

    locs = as->dc;

    while (vals > 0) {
        int valpos, valout, oldvals = vals;

        for (valpos = valout = 0; valpos < vals; valpos++) {
            int validx = as->wh_scratch[valpos];
            struct alloc_val *val = &as->vals[validx];
            struct alloc_loc *loc;
            int locpos, si, which_lo;

            for (locpos = 0; locpos < locs; locpos++) {
                int locidx = as->wh2_scratch[locpos];
                int wi, flip;

                loc = &as->locs[locidx];
                if (as->numbers[loc->sq[0]] != -1)
                    continue;              /* this location is already filled */

                flip = random_upto(rs, 2);

                /* Try this location both ways round. */
                for (wi = 0; wi < 2; wi++) {
                    int n0, n1;

                    which_lo = wi ^ flip;

                    /* First, do the same check as in alloc_try_unique, to
                     * avoid making an obviously insoluble puzzle. */
                    if (alloc_find_neighbour(as, loc->sq[which_lo],
                                             loc->sq[1-which_lo], &n0, &n1) &&
                        (as->numbers[n0] == val->hi ||
                         as->numbers[n1] == val->lo))
                        break;             /* can't place it this way round */

                    if (confounders_needed == 0)
                        goto place_ok;

                    /* Look to see if we're adding at least one
                     * previously absent confounder. */
                    for (si = 0; si < 2; si++) {
                        int x = loc->sq[si] % as->w, y = loc->sq[si] / as->w;
                        int n = (si == which_lo ? val->lo : val->hi);
                        int d;
                        for (d = 0; d < 4; d++) {
                            int dx = d==0 ? +1 : d==2 ? -1 : 0;
                            int dy = d==1 ? +1 : d==3 ? -1 : 0;
                            int x1 = x+dx, y1 = y+dy, p1 = y1 * as->w + x1;
                            if (x1 >= 0 && x1 < as->w &&
                                y1 >= 0 && y1 < as->h &&
                                as->numbers[p1] != -1 &&
                                !(as->vals[DINDEX(n, as->numbers[p1])]
                                  .confounder)) {
                                /*
                                 * Place this domino.
                                 */
                                goto place_ok;
                            }
                        }
                    }
                }
            }

            /* If we get here without executing 'goto place_ok', we
             * didn't find anywhere useful to put this domino. Put it
             * back on the list for the next pass. */
            as->wh_scratch[valout++] = validx;
            continue;

          place_ok:;

            /* We've found a domino to place. Place it, and fill in
             * all the confounders it adds. */
            as->numbers[loc->sq[which_lo]] = val->lo;
            as->numbers[loc->sq[1 - which_lo]] = val->hi;

            for (si = 0; si < 2; si++) {
                int p = loc->sq[si];
                int n = as->numbers[p];
                int x = p % as->w, y = p / as->w;
                int d;
                for (d = 0; d < 4; d++) {
                    int dx = d==0 ? +1 : d==2 ? -1 : 0;
                    int dy = d==1 ? +1 : d==3 ? -1 : 0;
                    int x1 = x+dx, y1 = y+dy, p1 = y1 * as->w + x1;

                    if (x1 >= 0 && x1 < as->w && y1 >= 0 && y1 < as->h &&
                        p1 != loc->sq[1-si] && as->numbers[p1] != -1) {
                        int di = DINDEX(n, as->numbers[p1]);
                        if (!as->vals[di].confounder)
                            confounders_needed--;
                        as->vals[di].confounder = true;
                    }
                }
            }
        }

        vals = valout;

        if (oldvals == vals)
            break;
    }

    ok = true;

    for (i = 0; i < as->dc; i++)
        if (!as->vals[i].confounder)
            ok = false;
    for (i = 0; i < as->wh; i++)
        if (as->numbers[i] == -1)
            ok = false;

    return ok;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h, diff = params->diff;
    struct solver_scratch *sc;
    struct alloc_scratch *as;
    int i, j, k, len;
    char *ret;

#ifndef OMIT_DIFFICULTY_CAP
    /*
     * Cap the difficulty level for small puzzles which would
     * otherwise become impossible to generate.
     *
     * Under an #ifndef, to make it easy to remove this cap for the
     * purpose of re-testing what it ought to be.
     */
    if (diff != DIFF_AMBIGUOUS) {
        if (n == 1 && diff > DIFF_TRIVIAL)
            diff = DIFF_TRIVIAL;
        if (n == 2 && diff > DIFF_BASIC)
            diff = DIFF_BASIC;
    }
#endif /* OMIT_DIFFICULTY_CAP */

    /*
     * Allocate space in which to lay the grid out.
     */
    sc = solver_make_scratch(n);
    as = alloc_make_scratch(n);

    /*
     * I haven't been able to think of any particularly clever
     * techniques for generating instances of Dominosa with a
     * unique solution. Many of the deductions used in this puzzle
     * are based on information involving half the grid at a time
     * (`of all the 6s, exactly one is next to a 3'), so a strategy
     * of partially solving the grid and then perturbing the place
     * where the solver got stuck seems particularly likely to
     * accidentally destroy the information which the solver had
     * used in getting that far. (Contrast with, say, Mines, in
     * which most deductions are local so this is an excellent
     * strategy.)
     *
     * Therefore I resort to the basest of brute force methods:
     * generate a random grid, see if it's solvable, throw it away
     * and try again if not. My only concession to sophistication
     * and cleverness is to at least _try_ not to generate obvious
     * 2x2 ambiguous sections (see comment below in the domino-
     * flipping section).
     *
     * During tests performed on 2005-07-15, I found that the brute
     * force approach without that tweak had to throw away about 87
     * grids on average (at the default n=6) before finding a
     * unique one, or a staggering 379 at n=9; good job the
     * generator and solver are fast! When I added the
     * ambiguous-section avoidance, those numbers came down to 19
     * and 26 respectively, which is a lot more sensible.
     */

    while (1) {
        alloc_make_layout(as, rs);

        if (diff == DIFF_AMBIGUOUS) {
            /* Just assign numbers to each domino completely at random. */
            alloc_trivial(as, rs);
        } else if (diff < DIFF_HARD) {
            /* Try to rule out the most common case of a non-unique solution */
            if (!alloc_try_unique(as, rs))
                continue;
        } else {
            /*
             * For Hard puzzles and above, we'd like there not to be
             * any easy toehold to start with.
             *
             * Mostly, that's arranged by alloc_try_hard, which will
             * ensure that no domino starts off with only one
             * potential placement. But a few other deductions
             * possible at Basic level can still sneak through the
             * cracks - for example, if the only two placements of one
             * domino overlap in a square, and you therefore rule out
             * some other domino that can use that square, you might
             * then find that _that_ domino now has only one
             * placement, and you've made a start.
             *
             * Of course, the main difficulty-level check will still
             * guarantee that you have to do a harder deduction
             * _somewhere_ in the grid. But it's more elegant if
             * there's nowhere obvious to get started at all.
             */
            int di;
            bool ok;

            if (!alloc_try_hard(as, rs))
                continue;

            solver_setup_grid(sc, as->numbers);
            if (run_solver(sc, DIFF_BASIC) < 2)
                continue;

            ok = true;
            for (di = 0; di < sc->dc; di++)
                if (sc->dominoes[di].nplacements <= 1) {
                    ok = false;
                    break;
                }

            if (!ok) {
                continue;
            }
        }

        if (diff != DIFF_AMBIGUOUS) {
            int solver_result;
            solver_setup_grid(sc, as->numbers);
            solver_result = run_solver(sc, diff);
            if (solver_result > 1)
                continue; /* puzzle couldn't be solved at this difficulty */
            if (sc->max_diff_used < diff)
                continue; /* puzzle _could_ be solved at easier difficulty */
        }

        break;
    }

#ifdef GENERATION_DIAGNOSTICS
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            putchar('0' + as->numbers[j*w+i]);
        }
        putchar('\n');
    }
    putchar('\n');
#endif

    /*
     * Encode the resulting game state.
     * 
     * Our encoding is a string of digits. Any number greater than
     * 9 is represented by a decimal integer within square
     * brackets. We know there are n+2 of every number (it's paired
     * with each number from 0 to n inclusive, and one of those is
     * itself so that adds another occurrence), so we can work out
     * the string length in advance.
     */

    /*
     * To work out the total length of the decimal encodings of all
     * the numbers from 0 to n inclusive:
     *  - every number has a units digit; total is n+1.
     *  - all numbers above 9 have a tens digit; total is max(n+1-10,0).
     *  - all numbers above 99 have a hundreds digit; total is max(n+1-100,0).
     *  - and so on.
     */
    len = n+1;
    for (i = 10; i <= n; i *= 10)
	len += max(n + 1 - i, 0);
    /* Now add two square brackets for each number above 9. */
    len += 2 * max(n + 1 - 10, 0);
    /* And multiply by n+2 for the repeated occurrences of each number. */
    len *= n+2;

    /*
     * Now actually encode the string.
     */
    ret = snewn(len+1, char);
    j = 0;
    for (i = 0; i < wh; i++) {
        k = as->numbers[i];
        if (k < 10)
            ret[j++] = '0' + k;
        else
            j += sprintf(ret+j, "[%d]", k);
        assert(j <= len);
    }
    assert(j == len);
    ret[j] = '\0';

    /*
     * Encode the solved state as an aux_info.
     */
    {
	char *auxinfo = snewn(wh+1, char);

	for (i = 0; i < wh; i++) {
	    int v = as->layout[i];
	    auxinfo[i] = (v == i+1 ? 'L' : v == i-1 ? 'R' :
			  v == i+w ? 'T' : v == i-w ? 'B' : '.');
	}
	auxinfo[wh] = '\0';

	*aux = auxinfo;
    }

    solver_free_scratch(sc);
    alloc_free_scratch(as);

    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    int *occurrences;
    int i, j;
    const char *ret;

    ret = NULL;
    occurrences = snewn(n+1, int);
    for (i = 0; i <= n; i++)
        occurrences[i] = 0;

    for (i = 0; i < wh; i++) {
        if (!*desc) {
            ret = ret ? ret : "Game description is too short";
        } else {
            if (*desc >= '0' && *desc <= '9')
                j = *desc++ - '0';
            else if (*desc == '[') {
                desc++;
                j = atoi(desc);
                while (*desc && isdigit((unsigned char)*desc)) desc++;
                if (*desc != ']')
                    ret = ret ? ret : "Missing ']' in game description";
                else
                    desc++;
            } else {
                j = -1;
                ret = ret ? ret : "Invalid syntax in game description";
            }
            if (j < 0 || j > n)
                ret = ret ? ret : "Number out of range in game description";
            else
                occurrences[j]++;
        }
    }

    if (*desc)
        ret = ret ? ret : "Game description is too long";

    if (!ret) {
        for (i = 0; i <= n; i++)
            if (occurrences[i] != n+2)
                ret = "Incorrect number balance in game description";
    }

    sfree(occurrences);

    return ret;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    game_state *state = snew(game_state);
    int i, j;

    state->params = *params;
    state->w = w;
    state->h = h;

    state->grid = snewn(wh, int);
    for (i = 0; i < wh; i++)
        state->grid[i] = i;

    state->edges = snewn(wh, unsigned short);
    for (i = 0; i < wh; i++)
        state->edges[i] = 0;

    state->numbers = snew(struct game_numbers);
    state->numbers->refcount = 1;
    state->numbers->numbers = snewn(wh, int);

    for (i = 0; i < wh; i++) {
        assert(*desc);
        if (*desc >= '0' && *desc <= '9')
            j = *desc++ - '0';
        else {
            assert(*desc == '[');
            desc++;
            j = atoi(desc);
            while (*desc && isdigit((unsigned char)*desc)) desc++;
            assert(*desc == ']');
            desc++;
        }
        assert(j >= 0 && j <= n);
        state->numbers->numbers[i] = j;
    }

    state->completed = false;
    state->cheated = false;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int n = state->params.n, w = n+2, h = n+1, wh = w*h;
    game_state *ret = snew(game_state);

    ret->params = state->params;
    ret->w = state->w;
    ret->h = state->h;
    ret->grid = snewn(wh, int);
    memcpy(ret->grid, state->grid, wh * sizeof(int));
    ret->edges = snewn(wh, unsigned short);
    memcpy(ret->edges, state->edges, wh * sizeof(unsigned short));
    ret->numbers = state->numbers;
    ret->numbers->refcount++;
    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->edges);
    if (--state->numbers->refcount <= 0) {
        sfree(state->numbers->numbers);
        sfree(state->numbers);
    }
    sfree(state);
}

static char *solution_move_string(struct solver_scratch *sc)
{
    char *ret;
    int retlen, retsize;
    int i, pass;

    /*
     * First make a pass putting in edges for -1, then make a pass
     * putting in dominoes for +1.
     */
    retsize = 256;
    ret = snewn(retsize, char);
    retlen = sprintf(ret, "S");

    for (pass = 0; pass < 2; pass++) {
        char type = "ED"[pass];

        for (i = 0; i < sc->pc; i++) {
            struct solver_placement *p = &sc->placements[i];
            char buf[80];
            int extra;

            if (pass == 0) {
                /* Emit a barrier if this placement is ruled out for
                 * the domino. */
                if (p->active)
                    continue;
            } else {
                /* Emit a domino if this placement is the only one not
                 * ruled out. */
                if (!p->active || p->domino->nplacements > 1)
                    continue;
            }

            extra = sprintf(buf, ";%c%d,%d", type,
                            p->squares[0]->index, p->squares[1]->index);

            if (retlen + extra + 1 >= retsize) {
                retsize = retlen + extra + 256;
                ret = sresize(ret, retsize, char);
            }
            strcpy(ret + retlen, buf);
            retlen += extra;
        }
    }

    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int n = state->params.n, w = n+2, h = n+1, wh = w*h;
    char *ret;
    int retlen, retsize;
    int i;
    char buf[80];
    int extra;

    if (aux) {
	retsize = 256;
	ret = snewn(retsize, char);
	retlen = sprintf(ret, "S");

	for (i = 0; i < wh; i++) {
	    if (aux[i] == 'L')
		extra = sprintf(buf, ";D%d,%d", i, i+1);
	    else if (aux[i] == 'T')
		extra = sprintf(buf, ";D%d,%d", i, i+w);
	    else
		continue;

	    if (retlen + extra + 1 >= retsize) {
		retsize = retlen + extra + 256;
		ret = sresize(ret, retsize, char);
	    }
	    strcpy(ret + retlen, buf);
	    retlen += extra;
	}

    } else {
        struct solver_scratch *sc = solver_make_scratch(n);
        solver_setup_grid(sc, state->numbers->numbers);
        run_solver(sc, DIFFCOUNT);
        ret = solution_move_string(sc);
	solver_free_scratch(sc);
    }

    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return params->n < 1000;
}

static void draw_domino(char *board, int start, char corner,
			int dshort, int nshort, char cshort,
			int dlong, int nlong, char clong)
{
    int go_short = nshort*dshort, go_long = nlong*dlong, i;

    board[start] = corner;
    board[start + go_short] = corner;
    board[start + go_long] = corner;
    board[start + go_short + go_long] = corner;

    for (i = 1; i < nshort; ++i) {
	int j = start + i*dshort, k = start + i*dshort + go_long;
	if (board[j] != corner) board[j] = cshort;
	if (board[k] != corner) board[k] = cshort;
    }

    for (i = 1; i < nlong; ++i) {
	int j = start + i*dlong, k = start + i*dlong + go_short;
	if (board[j] != corner) board[j] = clong;
	if (board[k] != corner) board[k] = clong;
    }
}

static char *game_text_format(const game_state *state)
{
    int w = state->w, h = state->h, r, c;
    int cw = 4, ch = 2, gw = cw*w + 2, gh = ch * h + 1, len = gw * gh;
    char *board = snewn(len + 1, char);

    memset(board, ' ', len);

    for (r = 0; r < h; ++r) {
	for (c = 0; c < w; ++c) {
	    int cell = r*ch*gw + cw*c, center = cell + gw*ch/2 + cw/2;
	    int i = r*w + c, num = state->numbers->numbers[i];

	    if (num < 100) {
		board[center] = '0' + num % 10;
		if (num >= 10) board[center - 1] = '0' + num / 10;
	    } else {
		board[center+1] = '0' + num % 10;
		board[center] = '0' + num / 10 % 10;
		board[center-1] = '0' + num / 100;
	    }

	    if (state->edges[i] & EDGE_L) board[center - cw/2] = '|';
	    if (state->edges[i] & EDGE_R) board[center + cw/2] = '|';
	    if (state->edges[i] & EDGE_T) board[center - gw] = '-';
	    if (state->edges[i] & EDGE_B) board[center + gw] = '-';

	    if (state->grid[i] == i) continue; /* no domino pairing */
	    if (state->grid[i] < i) continue; /* already done */
	    assert (state->grid[i] == i + 1 || state->grid[i] == i + w);
	    if (state->grid[i] == i + 1)
		draw_domino(board, cell, '+', gw, ch, '|', +1, 2*cw, '-');
	    else if (state->grid[i] == i + w)
		draw_domino(board, cell, '+', +1, cw, '-', gw, 2*ch, '|');
	}
	board[r*ch*gw + gw - 1] = '\n';
	board[r*ch*gw + gw + gw - 1] = '\n';
    }
    board[len - 1] = '\n';
    board[len] = '\0';
    return board;
}

struct game_ui {
    int cur_x, cur_y, highlight_1, highlight_2;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->highlight_1 = ui->highlight_2 = -1;
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
        ui->cur_visible = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button)) {
        int d1, d2, w = state->w;

	if (!((ui->cur_x ^ ui->cur_y) & 1))
	    return "";	       /* must have exactly one dimension odd */
	d1 = (ui->cur_y / 2) * w + (ui->cur_x / 2);
	d2 = ((ui->cur_y+1) / 2) * w + ((ui->cur_x+1) / 2);

        /* We can't mark an edge next to any domino. */
        if (button == CURSOR_SELECT2 &&
            (state->grid[d1] != d1 || state->grid[d2] != d2))
            return "";
        if (button == CURSOR_SELECT) {
            if (state->grid[d1] == d2) return "Remove";
            return "Place";
        } else {
            int edge = d2 == d1 + 1 ? EDGE_R : EDGE_B;
            if (state->edges[d1] & edge) return "Remove";
            return "Line";
        }
    }
    return "";
}

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE * 3 / 4)
#define DOMINO_GUTTER (TILESIZE / 16)
#define DOMINO_RADIUS (TILESIZE / 8)
#define DOMINO_COFFSET (DOMINO_GUTTER + DOMINO_RADIUS)
#define CURSOR_RADIUS (TILESIZE / 4)

#define COORD(x) ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x) ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

struct game_drawstate {
    int w, h, tilesize;
    unsigned long *visible;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->w, h = state->h;
    char buf[80];

    /*
     * A left-click between two numbers toggles a domino covering
     * them. A right-click toggles an edge.
     */
    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        int tx = FROMCOORD(x), ty = FROMCOORD(y), t = ty*w+tx;
        int dx, dy;
        int d1, d2;

        if (tx < 0 || tx >= w || ty < 0 || ty >= h)
            return NULL;

        /*
         * Now we know which square the click was in, decide which
         * edge of the square it was closest to.
         */
        dx = 2 * (x - COORD(tx)) - TILESIZE;
        dy = 2 * (y - COORD(ty)) - TILESIZE;

        if (abs(dx) > abs(dy) && dx < 0 && tx > 0)
            d1 = t - 1, d2 = t;        /* clicked in right side of domino */
        else if (abs(dx) > abs(dy) && dx > 0 && tx+1 < w)
            d1 = t, d2 = t + 1;        /* clicked in left side of domino */
        else if (abs(dy) > abs(dx) && dy < 0 && ty > 0)
            d1 = t - w, d2 = t;        /* clicked in bottom half of domino */
        else if (abs(dy) > abs(dx) && dy > 0 && ty+1 < h)
            d1 = t, d2 = t + w;        /* clicked in top half of domino */
        else
            return NULL;

        /*
         * We can't mark an edge next to any domino.
         */
        if (button == RIGHT_BUTTON &&
            (state->grid[d1] != d1 || state->grid[d2] != d2))
            return NULL;

        ui->cur_visible = false;
        sprintf(buf, "%c%d,%d", (int)(button == RIGHT_BUTTON ? 'E' : 'D'), d1, d2);
        return dupstr(buf);
    } else if (IS_CURSOR_MOVE(button)) {
	ui->cur_visible = true;

        move_cursor(button, &ui->cur_x, &ui->cur_y, 2*w-1, 2*h-1, false);

	return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        int d1, d2;

	if (!((ui->cur_x ^ ui->cur_y) & 1))
	    return NULL;	       /* must have exactly one dimension odd */
	d1 = (ui->cur_y / 2) * w + (ui->cur_x / 2);
	d2 = ((ui->cur_y+1) / 2) * w + ((ui->cur_x+1) / 2);

        /*
         * We can't mark an edge next to any domino.
         */
        if (button == CURSOR_SELECT2 &&
            (state->grid[d1] != d1 || state->grid[d2] != d2))
            return NULL;

        sprintf(buf, "%c%d,%d", (int)(button == CURSOR_SELECT2 ? 'E' : 'D'), d1, d2);
        return dupstr(buf);
    } else if (isdigit(button)) {
        int n = state->params.n, num = button - '0';
        if (num > n) {
            return NULL;
        } else if (ui->highlight_1 == num) {
            ui->highlight_1 = -1;
        } else if (ui->highlight_2 == num) {
            ui->highlight_2 = -1;
        } else if (ui->highlight_1 == -1) {
            ui->highlight_1 = num;
        } else if (ui->highlight_2 == -1) {
            ui->highlight_2 = num;
        } else {
            return NULL;
        }
        return UI_UPDATE;
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int n = state->params.n, w = n+2, h = n+1, wh = w*h;
    int d1, d2, d3, p;
    game_state *ret = dup_game(state);

    while (*move) {
        if (move[0] == 'S') {
            int i;

            ret->cheated = true;

            /*
             * Clear the existing edges and domino placements. We
             * expect the S to be followed by other commands.
             */
            for (i = 0; i < wh; i++) {
                ret->grid[i] = i;
                ret->edges[i] = 0;
            }
            move++;
        } else if (move[0] == 'D' &&
                   sscanf(move+1, "%d,%d%n", &d1, &d2, &p) == 2 &&
                   d1 >= 0 && d1 < wh && d2 >= 0 && d2 < wh && d1 < d2 &&
                   (d2 - d1 == 1 || d2 - d1 == w)) {

            /*
             * Toggle domino presence between d1 and d2.
             */
            if (ret->grid[d1] == d2) {
                assert(ret->grid[d2] == d1);
                ret->grid[d1] = d1;
                ret->grid[d2] = d2;
            } else {
                /*
                 * Erase any dominoes that might overlap the new one.
                 */
                d3 = ret->grid[d1];
                if (d3 != d1)
                    ret->grid[d3] = d3;
                d3 = ret->grid[d2];
                if (d3 != d2)
                    ret->grid[d3] = d3;
                /*
                 * Place the new one.
                 */
                ret->grid[d1] = d2;
                ret->grid[d2] = d1;

                /*
                 * Destroy any edges lurking around it.
                 */
                if (ret->edges[d1] & EDGE_L) {
                    assert(d1 - 1 >= 0);
                    ret->edges[d1 - 1] &= ~EDGE_R;
                }
                if (ret->edges[d1] & EDGE_R) {
                    assert(d1 + 1 < wh);
                    ret->edges[d1 + 1] &= ~EDGE_L;
                }
                if (ret->edges[d1] & EDGE_T) {
                    assert(d1 - w >= 0);
                    ret->edges[d1 - w] &= ~EDGE_B;
                }
                if (ret->edges[d1] & EDGE_B) {
                    assert(d1 + 1 < wh);
                    ret->edges[d1 + w] &= ~EDGE_T;
                }
                ret->edges[d1] = 0;
                if (ret->edges[d2] & EDGE_L) {
                    assert(d2 - 1 >= 0);
                    ret->edges[d2 - 1] &= ~EDGE_R;
                }
                if (ret->edges[d2] & EDGE_R) {
                    assert(d2 + 1 < wh);
                    ret->edges[d2 + 1] &= ~EDGE_L;
                }
                if (ret->edges[d2] & EDGE_T) {
                    assert(d2 - w >= 0);
                    ret->edges[d2 - w] &= ~EDGE_B;
                }
                if (ret->edges[d2] & EDGE_B) {
                    assert(d2 + 1 < wh);
                    ret->edges[d2 + w] &= ~EDGE_T;
                }
                ret->edges[d2] = 0;
            }

            move += p+1;
        } else if (move[0] == 'E' &&
                   sscanf(move+1, "%d,%d%n", &d1, &d2, &p) == 2 &&
                   d1 >= 0 && d1 < wh && d2 >= 0 && d2 < wh && d1 < d2 &&
                   ret->grid[d1] == d1 && ret->grid[d2] == d2 &&
                   (d2 - d1 == 1 || d2 - d1 == w)) {

            /*
             * Toggle edge presence between d1 and d2.
             */
            if (d2 == d1 + 1) {
                ret->edges[d1] ^= EDGE_R;
                ret->edges[d2] ^= EDGE_L;
            } else {
                ret->edges[d1] ^= EDGE_B;
                ret->edges[d2] ^= EDGE_T;
            }

            move += p+1;
        } else {
            free_game(ret);
            return NULL;
        }

        if (*move) {
            if (*move != ';') {
                free_game(ret);
                return NULL;
            }
            move++;
        }
    }

    /*
     * After modifying the grid, check completion.
     */
    if (!ret->completed) {
        int i, ok = 0;
        bool *used = snewn(TRI(n+1), bool);

        memset(used, 0, TRI(n+1));
        for (i = 0; i < wh; i++)
            if (ret->grid[i] > i) {
                int n1, n2, di;

                n1 = ret->numbers->numbers[i];
                n2 = ret->numbers->numbers[ret->grid[i]];

                di = DINDEX(n1, n2);
                assert(di >= 0 && di < TRI(n+1));

                if (!used[di]) {
                    used[di] = true;
                    ok++;
                }
            }

        sfree(used);
        if (ok == DCOUNT(n))
            ret->completed = true;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    int n = params->n, w = n+2, h = n+1;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = w * TILESIZE + 2*BORDER;
    *y = h * TILESIZE + 2*BORDER;
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

    ret[COL_TEXT * 3 + 0] = 0.0F;
    ret[COL_TEXT * 3 + 1] = 0.0F;
    ret[COL_TEXT * 3 + 2] = 0.0F;

    ret[COL_DOMINO * 3 + 0] = 0.0F;
    ret[COL_DOMINO * 3 + 1] = 0.0F;
    ret[COL_DOMINO * 3 + 2] = 0.0F;

    ret[COL_DOMINOCLASH * 3 + 0] = 0.5F;
    ret[COL_DOMINOCLASH * 3 + 1] = 0.0F;
    ret[COL_DOMINOCLASH * 3 + 2] = 0.0F;

    ret[COL_DOMINOTEXT * 3 + 0] = 1.0F;
    ret[COL_DOMINOTEXT * 3 + 1] = 1.0F;
    ret[COL_DOMINOTEXT * 3 + 2] = 1.0F;

    ret[COL_EDGE * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2 / 3;
    ret[COL_EDGE * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2 / 3;
    ret[COL_EDGE * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2 / 3;

    ret[COL_HIGHLIGHT_1 * 3 + 0] = 0.85;
    ret[COL_HIGHLIGHT_1 * 3 + 1] = 0.20;
    ret[COL_HIGHLIGHT_1 * 3 + 2] = 0.20;

    ret[COL_HIGHLIGHT_2 * 3 + 0] = 0.30;
    ret[COL_HIGHLIGHT_2 * 3 + 1] = 0.85;
    ret[COL_HIGHLIGHT_2 * 3 + 2] = 0.20;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->w = state->w;
    ds->h = state->h;
    ds->visible = snewn(ds->w * ds->h, unsigned long);
    ds->tilesize = 0;                  /* not decided yet */
    for (i = 0; i < ds->w * ds->h; i++)
        ds->visible[i] = 0xFFFF;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

enum {
    TYPE_L,
    TYPE_R,
    TYPE_T,
    TYPE_B,
    TYPE_BLANK,
    TYPE_MASK = 0x0F
};

/* These flags must be disjoint with:
   * the above enum (TYPE_*)    [0x000 -- 0x00F]
   * EDGE_*                     [0x100 -- 0xF00]
 * and must fit into an unsigned long (32 bits).
 */
#define DF_HIGHLIGHT_1  0x10
#define DF_HIGHLIGHT_2  0x20
#define DF_FLASH        0x40
#define DF_CLASH        0x80

#define DF_CURSOR        0x01000
#define DF_CURSOR_USEFUL 0x02000
#define DF_CURSOR_XBASE  0x10000
#define DF_CURSOR_XMASK  0x30000
#define DF_CURSOR_YBASE  0x40000
#define DF_CURSOR_YMASK  0xC0000

#define CEDGE_OFF       (TILESIZE / 8)
#define IS_EMPTY(s,x,y) ((s)->grid[(y)*(s)->w+(x)] == ((y)*(s)->w+(x)))

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, int type, int highlight_1, int highlight_2)
{
    int w = state->w /*, h = state->h */;
    int cx = COORD(x), cy = COORD(y);
    int nc;
    char str[80];
    int flags;

    clip(dr, cx, cy, TILESIZE, TILESIZE);
    draw_rect(dr, cx, cy, TILESIZE, TILESIZE, COL_BACKGROUND);

    flags = type &~ TYPE_MASK;
    type &= TYPE_MASK;

    if (type != TYPE_BLANK) {
        int i, bg;

        /*
         * Draw one end of a domino. This is composed of:
         * 
         *  - two filled circles (rounded corners)
         *  - two rectangles
         *  - a slight shift in the number
         */

        if (flags & DF_CLASH)
            bg = COL_DOMINOCLASH;
        else
            bg = COL_DOMINO;
        nc = COL_DOMINOTEXT;

        if (flags & DF_FLASH) {
            int tmp = nc;
            nc = bg;
            bg = tmp;
        }

        if (type == TYPE_L || type == TYPE_T)
            draw_circle(dr, cx+DOMINO_COFFSET, cy+DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_R || type == TYPE_T)
            draw_circle(dr, cx+TILESIZE-1-DOMINO_COFFSET, cy+DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_L || type == TYPE_B)
            draw_circle(dr, cx+DOMINO_COFFSET, cy+TILESIZE-1-DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_R || type == TYPE_B)
            draw_circle(dr, cx+TILESIZE-1-DOMINO_COFFSET,
                        cy+TILESIZE-1-DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);

        for (i = 0; i < 2; i++) {
            int x1, y1, x2, y2;

            x1 = cx + (i ? DOMINO_GUTTER : DOMINO_COFFSET);
            y1 = cy + (i ? DOMINO_COFFSET : DOMINO_GUTTER);
            x2 = cx + TILESIZE-1 - (i ? DOMINO_GUTTER : DOMINO_COFFSET);
            y2 = cy + TILESIZE-1 - (i ? DOMINO_COFFSET : DOMINO_GUTTER);
            if (type == TYPE_L)
                x2 = cx + TILESIZE + TILESIZE/16;
            else if (type == TYPE_R)
                x1 = cx - TILESIZE/16;
            else if (type == TYPE_T)
                y2 = cy + TILESIZE + TILESIZE/16;
            else if (type == TYPE_B)
                y1 = cy - TILESIZE/16;

            draw_rect(dr, x1, y1, x2-x1+1, y2-y1+1, bg);
        }
    } else {
        if (flags & EDGE_T)
            draw_rect(dr, cx+DOMINO_GUTTER, cy,
                      TILESIZE-2*DOMINO_GUTTER, 1, COL_EDGE);
        if (flags & EDGE_B)
            draw_rect(dr, cx+DOMINO_GUTTER, cy+TILESIZE-1,
                      TILESIZE-2*DOMINO_GUTTER, 1, COL_EDGE);
        if (flags & EDGE_L)
            draw_rect(dr, cx, cy+DOMINO_GUTTER,
                      1, TILESIZE-2*DOMINO_GUTTER, COL_EDGE);
        if (flags & EDGE_R)
            draw_rect(dr, cx+TILESIZE-1, cy+DOMINO_GUTTER,
                      1, TILESIZE-2*DOMINO_GUTTER, COL_EDGE);
        nc = COL_TEXT;
    }

    if (flags & DF_CURSOR) {
	int curx = ((flags & DF_CURSOR_XMASK) / DF_CURSOR_XBASE) & 3;
	int cury = ((flags & DF_CURSOR_YMASK) / DF_CURSOR_YBASE) & 3;
	int ox = cx + curx*TILESIZE/2;
	int oy = cy + cury*TILESIZE/2;

	draw_rect_corners(dr, ox, oy, CURSOR_RADIUS, nc);
        if (flags & DF_CURSOR_USEFUL)
	    draw_rect_corners(dr, ox, oy, CURSOR_RADIUS+1, nc);
    }

    if (flags & DF_HIGHLIGHT_1) {
        nc = COL_HIGHLIGHT_1;
    } else if (flags & DF_HIGHLIGHT_2) {
        nc = COL_HIGHLIGHT_2;
    }

    sprintf(str, "%d", state->numbers->numbers[y*w+x]);
    draw_text(dr, cx+TILESIZE/2, cy+TILESIZE/2, FONT_VARIABLE, TILESIZE/2,
              ALIGN_HCENTRE | ALIGN_VCENTRE, nc, str);

    draw_update(dr, cx, cy, TILESIZE, TILESIZE);
    unclip(dr);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int n = state->params.n, w = state->w, h = state->h, wh = w*h;
    int x, y, i;
    unsigned char *used;

    /*
     * See how many dominoes of each type there are, so we can
     * highlight clashes in red.
     */
    used = snewn(TRI(n+1), unsigned char);
    memset(used, 0, TRI(n+1));
    for (i = 0; i < wh; i++)
        if (state->grid[i] > i) {
            int n1, n2, di;

            n1 = state->numbers->numbers[i];
            n2 = state->numbers->numbers[state->grid[i]];

            di = DINDEX(n1, n2);
            assert(di >= 0 && di < TRI(n+1));

            if (used[di] < 2)
                used[di]++;
        }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int n = y*w+x;
            int n1, n2, di;
	    unsigned long c;

            if (state->grid[n] == n-1)
                c = TYPE_R;
            else if (state->grid[n] == n+1)
                c = TYPE_L;
            else if (state->grid[n] == n-w)
                c = TYPE_B;
            else if (state->grid[n] == n+w)
                c = TYPE_T;
            else
                c = TYPE_BLANK;

            n1 = state->numbers->numbers[n];
            if (c != TYPE_BLANK) {
                n2 = state->numbers->numbers[state->grid[n]];
                di = DINDEX(n1, n2);
                if (used[di] > 1)
                    c |= DF_CLASH;         /* highlight a clash */
            } else {
                c |= state->edges[n];
            }

            if (n1 == ui->highlight_1)
                c |= DF_HIGHLIGHT_1;
            if (n1 == ui->highlight_2)
                c |= DF_HIGHLIGHT_2;

            if (flashtime != 0)
                c |= DF_FLASH;             /* we're flashing */

            if (ui->cur_visible) {
		unsigned curx = (unsigned)(ui->cur_x - (2*x-1));
		unsigned cury = (unsigned)(ui->cur_y - (2*y-1));
		if (curx < 3 && cury < 3) {
		    c |= (DF_CURSOR |
			  (curx * DF_CURSOR_XBASE) |
			  (cury * DF_CURSOR_YBASE));
                    if ((ui->cur_x ^ ui->cur_y) & 1)
                        c |= DF_CURSOR_USEFUL;
                }
            }

	    if (ds->visible[n] != c) {
		draw_tile(dr, ds, state, x, y, c,
                          ui->highlight_1, ui->highlight_2);
                ds->visible[n] = c;
	    }
	}

    sfree(used);
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
    {
        ui->highlight_1 = ui->highlight_2 = -1;
        return FLASH_TIME;
    }
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
        *x = BORDER + ((2 * ui->cur_x + 1) * TILESIZE) / 4;
        *y = BORDER + ((2 * ui->cur_y + 1) * TILESIZE) / 4;
        *w = *h = TILESIZE / 2 + 2;
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
    int w = state->w, h = state->h;
    int c, x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    c = print_mono_colour(dr, 1); assert(c == COL_BACKGROUND);
    c = print_mono_colour(dr, 0); assert(c == COL_TEXT);
    c = print_mono_colour(dr, 0); assert(c == COL_DOMINO);
    c = print_mono_colour(dr, 0); assert(c == COL_DOMINOCLASH);
    c = print_mono_colour(dr, 1); assert(c == COL_DOMINOTEXT);
    c = print_mono_colour(dr, 0); assert(c == COL_EDGE);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int n = y*w+x;
	    unsigned long c;

            if (state->grid[n] == n-1)
                c = TYPE_R;
            else if (state->grid[n] == n+1)
                c = TYPE_L;
            else if (state->grid[n] == n-w)
                c = TYPE_B;
            else if (state->grid[n] == n+w)
                c = TYPE_T;
            else
                c = TYPE_BLANK;

	    draw_tile(dr, ds, state, x, y, c, -1, -1);
	}
}

#ifdef COMBINED
#define thegame dominosa
#endif

const struct game thegame = {
    "Dominosa", "games.dominosa", "dominosa",
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
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
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
    0,				       /* flags */
};

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s, *s2;
    char *id = NULL, *desc;
    int maxdiff = DIFFCOUNT;
    const char *err;
    bool grade = false, diagnostics = false;
    struct solver_scratch *sc;
    int retd;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            diagnostics = true;
        } else if (!strcmp(p, "-g")) {
            grade = true;
        } else if (!strncmp(p, "-d", 2) && p[2] && !p[3]) {
            int i;
            bool bad = true;
            for (i = 0; i < lenof(dominosa_diffchars); i++)
                if (dominosa_diffchars[i] != DIFF_AMBIGUOUS &&
                    dominosa_diffchars[i] == p[2]) {
                    bad = false;
                    maxdiff = i;
                    break;
                }
            if (bad) {
                fprintf(stderr, "%s: unrecognised difficulty `%c'\n",
                        argv[0], p[2]);
                return 1;
            }
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-v | -g] <game_id>\n", argv[0]);
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

    solver_diagnostics = diagnostics;
    sc = solver_make_scratch(p->n);
    solver_setup_grid(sc, s->numbers->numbers);
    retd = run_solver(sc, maxdiff);
    if (retd == 0) {
        printf("Puzzle is inconsistent\n");
    } else if (grade) {
        printf("Difficulty rating: %s\n",
               dominosa_diffnames[sc->max_diff_used]);
    } else {
        char *move, *text;
        move = solution_move_string(sc);
        s2 = execute_move(s, move);
        text = game_text_format(s2);
        sfree(move);
        fputs(text, stdout);
        sfree(text);
        free_game(s2);
        if (retd > 1)
            printf("Could not deduce a unique solution\n");
    }
    solver_free_scratch(sc);
    free_game(s);
    free_params(p);

    return 0;
}

#endif

/* vim: set shiftwidth=4 :set textwidth=80: */

