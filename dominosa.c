/*
 * dominosa.c: Domino jigsaw puzzle. Aim to place one of every
 * possible domino within a rectangle in such a way that the number
 * on each square matches the provided clue.
 */

/*
 * TODO:
 * 
 *  - improve solver so as to use more interesting forms of
 *    deduction
 *
 *     * rule out a domino placement if it would divide an unfilled
 *       region such that at least one resulting region had an odd
 *       area
 *        + Tarjan's bridge-finding algorithm would be a way to find
 *          domino placements that split a connected region in two:
 *          form the graph whose vertices are unpaired squares and
 *          whose edges are potential (not placed but also not ruled
 *          out) dominoes covering two of them, and any bridge in that
 *          graph is a candidate.
 *        + Then, finding any old spanning forest of the unfilled
 *          squares should be sufficient to determine the area parity
 *          of the region that any such placement would cut off.
 *
 *     * set analysis
 *        + look at all unclaimed squares containing a given number
 *        + for each one, find the set of possible numbers that it
 *          can connect to (i.e. each neighbouring tile such that
 *          the placement between it and that neighbour has not yet
 *          been ruled out)
 *        + now proceed similarly to Solo set analysis: try to find
 *          a subset of the squares such that the union of their
 *          possible numbers is the same size as the subset. If so,
 *          rule out those possible numbers for all other squares.
 *           * important wrinkle: the double dominoes complicate
 *             matters. Connecting a number to itself uses up _two_
 *             of the unclaimed squares containing a number. Thus,
 *             when finding the initial subset we must never
 *             include two adjacent squares; and also, when ruling
 *             things out after finding the subset, we must be
 *             careful that we don't rule out precisely the domino
 *             placement that was _included_ in our set!
 *
 *     * playing off the two ends of one potential domino, by
 *       considering the alternatives to that domino that each end
 *       might otherwise be part of.
 *        + if not playing this domino would require each end to be
 *          part of an identical domino, play it. (e.g. the middle of
 *          5-4-4-5)
 *        + if not playing this domino would guarantee that the two
 *          ends between them used up all of some other square's
 *          choices, play it. (e.g. the middle of 2-3-3-1 if another 3
 *          cell can only link to a 2 or a 1)
 *
 *     * identify 'forcing chains', in the sense of any path of cells
 *       each of which has only two possible dominoes to be part of,
 *       and each of those rules out one of the choices for the next
 *       cell. Such a chain has the property that either all the odd
 *       dominoes are placed, or all the even ones are placed; so if
 *       either set of those introduces a conflict (e.g. a dupe within
 *       the chain, or using up all of some other square's choices),
 *       then the whole set can be ruled out, and the other set played
 *       immediately.
 *        + this is of course a generalisation of the previous idea,
 *          which is simply a forcing chain of length 3.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
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
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

#ifdef STANDALONE_SOLVER
#define SOLVER_DIAGNOSTICS
bool solver_diagnostics = false;
#endif

struct solver_domino;
struct solver_placement;

/*
 * Information about a particular domino.
 */
struct solver_domino {
    /* The numbers on the domino. */
    int lo, hi;

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

    for (di = hi = 0; hi <= n; hi++) {
        for (lo = 0; lo <= hi; lo++) {
            assert(di == DINDEX(hi, lo));
            sc->dominoes[di].hi = hi;
            sc->dominoes[di].lo = lo;

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
    int di, si;
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
 * End of solver code.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    int *grid, *grid2, *list;
    struct solver_scratch *sc;
    int i, j, k, len;
    char *ret;

    /*
     * Allocate space in which to lay the grid out.
     */
    grid = snewn(wh, int);
    grid2 = snewn(wh, int);
    list = snewn(2*wh, int);
    sc = solver_make_scratch(n);

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

    do {
        domino_layout_prealloc(w, h, rs, grid, grid2, list);

        /*
         * Now we have a complete layout covering the whole
         * rectangle with dominoes. So shuffle the actual domino
         * values and fill the rectangle with numbers.
         */
        k = 0;
        for (i = 0; i <= params->n; i++)
            for (j = 0; j <= i; j++) {
                list[k++] = i;
                list[k++] = j;
            }
        shuffle(list, k/2, 2*sizeof(*list), rs);
        j = 0;
        for (i = 0; i < wh; i++)
            if (grid[i] > i) {
                /* Optionally flip the domino round. */
                int flip = -1;

                if (params->diff != DIFF_AMBIGUOUS) {
                    int t1, t2;
                    /*
                     * If we're after a unique solution, we can do
                     * something here to improve the chances. If
                     * we're placing a domino so that it forms a
                     * 2x2 rectangle with one we've already placed,
                     * and if that domino and this one share a
                     * number, we can try not to put them so that
                     * the identical numbers are diagonally
                     * separated, because that automatically causes
                     * non-uniqueness:
                     * 
                     * +---+      +-+-+
                     * |2 3|      |2|3|
                     * +---+  ->  | | |
                     * |4 2|      |4|2|
                     * +---+      +-+-+
                     */
                    t1 = i;
                    t2 = grid[i];
                    if (t2 == t1 + w) {  /* this domino is vertical */
                        if (t1 % w > 0 &&/* and not on the left hand edge */
                            grid[t1-1] == t2-1 &&/* alongside one to left */
                            (grid2[t1-1] == list[j] ||   /* and has a number */
                             grid2[t1-1] == list[j+1] ||   /* in common */
                             grid2[t2-1] == list[j] ||
                             grid2[t2-1] == list[j+1])) {
                            if (grid2[t1-1] == list[j] ||
                                grid2[t2-1] == list[j+1])
                                flip = 0;
                            else
                                flip = 1;
                        }
                    } else {           /* this domino is horizontal */
                        if (t1 / w > 0 &&/* and not on the top edge */
                            grid[t1-w] == t2-w &&/* alongside one above */
                            (grid2[t1-w] == list[j] ||   /* and has a number */
                             grid2[t1-w] == list[j+1] ||   /* in common */
                             grid2[t2-w] == list[j] ||
                             grid2[t2-w] == list[j+1])) {
                            if (grid2[t1-w] == list[j] ||
                                grid2[t2-w] == list[j+1])
                                flip = 0;
                            else
                                flip = 1;
                        }
                    }
                }

                if (flip < 0)
                    flip = random_upto(rs, 2);

                grid2[i] = list[j + flip];
                grid2[grid[i]] = list[j + 1 - flip];
                j += 2;
            }
        assert(j == k);
        solver_setup_grid(sc, grid2);
    } while (params->diff != DIFF_AMBIGUOUS &&
             (run_solver(sc, params->diff) > 1 ||
              sc->max_diff_used < params->diff));

    solver_free_scratch(sc);

#ifdef GENERATION_DIAGNOSTICS
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            putchar('0' + grid2[j*w+i]);
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
        k = grid2[i];
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
	    int v = grid[i];
	    auxinfo[i] = (v == i+1 ? 'L' : v == i-1 ? 'R' :
			  v == i+w ? 'T' : v == i-w ? 'B' : '.');
	}
	auxinfo[wh] = '\0';

	*aux = auxinfo;
    }

    sfree(list);
    sfree(grid2);
    sfree(grid);

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
    ui->cur_visible = false;
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
    bool started;
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
                   d1 >= 0 && d1 < wh && d2 >= 0 && d2 < wh && d1 < d2) {

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
                   ret->grid[d1] == d1 && ret->grid[d2] == d2) {

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

    ds->started = false;
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

    if (!ds->started) {
        int pw, ph;
        game_compute_size(&state->params, TILESIZE, &pw, &ph);
	draw_rect(dr, 0, 0, pw, ph, COL_BACKGROUND);
	draw_update(dr, 0, 0, pw, ph);
	ds->started = true;
    }

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

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return true;
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
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
};

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s, *s2;
    char *id = NULL, *desc;
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
    retd = run_solver(sc, DIFFCOUNT);
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

