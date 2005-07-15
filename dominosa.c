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
 *        + use b.f.s. to determine the area of an unfilled region
 *        + a square is unfilled iff it has at least two possible
 *          placements, and two adjacent unfilled squares are part
 *          of the same region iff the domino placement joining
 *          them is possible
 *
 *     * perhaps set analysis
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

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_DOMINO,
    COL_DOMINOCLASH,
    COL_DOMINOTEXT,
    COL_EDGE,
    NCOLOURS
};

struct game_params {
    int n;
    int unique;
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
    int completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->n = 6;
    ret->unique = TRUE;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int n;
    char buf[80];

    switch (i) {
      case 0: n = 3; break;
      case 1: n = 6; break;
      case 2: n = 9; break;
      default: return FALSE;
    }

    sprintf(buf, "Up to double-%d", n);
    *name = dupstr(buf);

    *params = ret = snew(game_params);
    ret->n = n;
    ret->unique = TRUE;

    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    params->n = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'a')
        params->unique = FALSE;
}

static char *encode_params(game_params *params, int full)
{
    char buf[80];
    sprintf(buf, "%d", params->n);
    if (full && !params->unique)
        strcat(buf, "a");
    return dupstr(buf);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Maximum number on dominoes";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Ensure unique solution";
    ret[1].type = C_BOOLEAN;
    ret[1].sval = NULL;
    ret[1].ival = params->unique;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->n = atoi(cfg[0].sval);
    ret->unique = cfg[1].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->n < 1)
        return "Maximum face number must be at least one";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

static int find_overlaps(int w, int h, int placement, int *set)
{
    int x, y, n;

    n = 0;                             /* number of returned placements */

    x = placement / 2;
    y = x / w;
    x %= w;

    if (placement & 1) {
        /*
         * Horizontal domino, indexed by its left end.
         */
        if (x > 0)
            set[n++] = placement-2;    /* horizontal domino to the left */
        if (y > 0)
            set[n++] = placement-2*w-1;/* vertical domino above left side */
        if (y+1 < h)
            set[n++] = placement-1;    /* vertical domino below left side */
        if (x+2 < w)
            set[n++] = placement+2;    /* horizontal domino to the right */
        if (y > 0)
            set[n++] = placement-2*w+2-1;/* vertical domino above right side */
        if (y+1 < h)
            set[n++] = placement+2-1;  /* vertical domino below right side */
    } else {
        /*
         * Vertical domino, indexed by its top end.
         */
        if (y > 0)
            set[n++] = placement-2*w;  /* vertical domino above */
        if (x > 0)
            set[n++] = placement-2+1;  /* horizontal domino left of top */
        if (x+1 < w)
            set[n++] = placement+1;    /* horizontal domino right of top */
        if (y+2 < h)
            set[n++] = placement+2*w;  /* vertical domino below */
        if (x > 0)
            set[n++] = placement-2+2*w+1;/* horizontal domino left of bottom */
        if (x+1 < w)
            set[n++] = placement+2*w+1;/* horizontal domino right of bottom */
    }

    return n;
}

/*
 * Returns 0, 1 or 2 for number of solutions. 2 means `any number
 * more than one', or more accurately `we were unable to prove
 * there was only one'.
 * 
 * Outputs in a `placements' array, indexed the same way as the one
 * within this function (see below); entries in there are <0 for a
 * placement ruled out, 0 for an uncertain placement, and 1 for a
 * definite one.
 */
static int solver(int w, int h, int n, int *grid, int *output)
{
    int wh = w*h, dc = DCOUNT(n);
    int *placements, *heads;
    int i, j, x, y, ret;

    /*
     * This array has one entry for every possible domino
     * placement. Vertical placements are indexed by their top
     * half, at (y*w+x)*2; horizontal placements are indexed by
     * their left half at (y*w+x)*2+1.
     * 
     * This array is used to link domino placements together into
     * linked lists, so that we can track all the possible
     * placements of each different domino. It's also used as a
     * quick means of looking up an individual placement to see
     * whether we still think it's possible. Actual values stored
     * in this array are -2 (placement not possible at all), -1
     * (end of list), or the array index of the next item.
     * 
     * Oh, and -3 for `not even valid', used for array indices
     * which don't even represent a plausible placement.
     */
    placements = snewn(2*wh, int);
    for (i = 0; i < 2*wh; i++)
        placements[i] = -3;            /* not even valid */

    /*
     * This array has one entry for every domino, and it is an
     * index into `placements' denoting the head of the placement
     * list for that domino.
     */
    heads = snewn(dc, int);
    for (i = 0; i < dc; i++)
        heads[i] = -1;

    /*
     * Set up the initial possibility lists by scanning the grid.
     */
    for (y = 0; y < h-1; y++)
        for (x = 0; x < w; x++) {
            int di = DINDEX(grid[y*w+x], grid[(y+1)*w+x]);
            placements[(y*w+x)*2] = heads[di];
            heads[di] = (y*w+x)*2;
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w-1; x++) {
            int di = DINDEX(grid[y*w+x], grid[y*w+(x+1)]);
            placements[(y*w+x)*2+1] = heads[di];
            heads[di] = (y*w+x)*2+1;
        }

#ifdef SOLVER_DIAGNOSTICS
    printf("before solver:\n");
    for (i = 0; i <= n; i++)
        for (j = 0; j <= i; j++) {
            int k, m;
            m = 0;
            printf("%2d [%d %d]:", DINDEX(i, j), i, j);
            for (k = heads[DINDEX(i,j)]; k >= 0; k = placements[k])
                printf(" %3d [%d,%d,%c]", k, k/2%w, k/2/w, k%2?'h':'v');
            printf("\n");
        }
#endif

    while (1) {
        int done_something = FALSE;

        /*
         * For each domino, look at its possible placements, and
         * for each placement consider the placements (of any
         * domino) it overlaps. Any placement overlapped by all
         * placements of this domino can be ruled out.
         * 
         * Each domino placement overlaps only six others, so we
         * need not do serious set theory to work this out.
         */
        for (i = 0; i < dc; i++) {
            int permset[6], permlen = 0, p;
            

            if (heads[i] == -1) {      /* no placement for this domino */
                ret = 0;               /* therefore puzzle is impossible */
                goto done;
            }
            for (j = heads[i]; j >= 0; j = placements[j]) {
                assert(placements[j] != -2);

                if (j == heads[i]) {
                    permlen = find_overlaps(w, h, j, permset);
                } else {
                    int tempset[6], templen, m, n, k;

                    templen = find_overlaps(w, h, j, tempset);

                    /*
                     * Pathetically primitive set intersection
                     * algorithm, which I'm only getting away with
                     * because I know my sets are bounded by a very
                     * small size.
                     */
                    for (m = n = 0; m < permlen; m++) {
                        for (k = 0; k < templen; k++)
                            if (tempset[k] == permset[m])
                                break;
                        if (k < templen)
                            permset[n++] = permset[m];
                    }
                    permlen = n;
                }
            }
            for (p = 0; p < permlen; p++) {
                j = permset[p];
                if (placements[j] != -2) {
                    int p1, p2, di;

                    done_something = TRUE;

                    /*
                     * Rule out this placement. First find what
                     * domino it is...
                     */
                    p1 = j / 2;
                    p2 = (j & 1) ? p1 + 1 : p1 + w;
                    di = DINDEX(grid[p1], grid[p2]);
#ifdef SOLVER_DIAGNOSTICS
                    printf("considering domino %d: ruling out placement %d"
                           " for %d\n", i, j, di);
#endif

                    /*
                     * ... then walk that domino's placement list,
                     * removing this placement when we find it.
                     */
                    if (heads[di] == j)
                        heads[di] = placements[j];
                    else {
                        int k = heads[di];
                        while (placements[k] != -1 && placements[k] != j)
                            k = placements[k];
                        assert(placements[k] == j);
                        placements[k] = placements[j];
                    }
                    placements[j] = -2;
                }
            }
        }

        /*
         * For each square, look at the available placements
         * involving that square. If all of them are for the same
         * domino, then rule out any placements for that domino
         * _not_ involving this square.
         */
        for (i = 0; i < wh; i++) {
            int list[4], k, n, adi;

            x = i % w;
            y = i / w;

            j = 0;
            if (x > 0)
                list[j++] = 2*(i-1)+1;
            if (x+1 < w)
                list[j++] = 2*i+1;
            if (y > 0)
                list[j++] = 2*(i-w);
            if (y+1 < h)
                list[j++] = 2*i;

            for (n = k = 0; k < j; k++)
                if (placements[list[k]] >= -1)
                    list[n++] = list[k];

            adi = -1;

            for (j = 0; j < n; j++) {
                int p1, p2, di;
                k = list[j];

                p1 = k / 2;
                p2 = (k & 1) ? p1 + 1 : p1 + w;
                di = DINDEX(grid[p1], grid[p2]);

                if (adi == -1)
                    adi = di;
                if (adi != di)
                    break;
            }

            if (j == n) {
                int nn;

                assert(adi >= 0);
                /*
                 * We've found something. All viable placements
                 * involving this square are for domino `adi'. If
                 * the current placement list for that domino is
                 * longer than n, reduce it to precisely this
                 * placement list and we've done something.
                 */
                nn = 0;
                for (k = heads[adi]; k >= 0; k = placements[k])
                    nn++;
                if (nn > n) {
                    done_something = TRUE;
#ifdef SOLVER_DIAGNOSTICS
                    printf("considering square %d,%d: reducing placements "
                           "of domino %d\n", x, y, adi);
#endif
                    /*
                     * Set all other placements on the list to
                     * impossible.
                     */
                    k = heads[adi];
                    while (k >= 0) {
                        int tmp = placements[k];
                        placements[k] = -2;
                        k = tmp;
                    }
                    /*
                     * Set up the new list.
                     */
                    heads[adi] = list[0];
                    for (k = 0; k < n; k++)
                        placements[list[k]] = (k+1 == n ? -1 : list[k+1]);
                }
            }
        }

        if (!done_something)
            break;
    }

#ifdef SOLVER_DIAGNOSTICS
    printf("after solver:\n");
    for (i = 0; i <= n; i++)
        for (j = 0; j <= i; j++) {
            int k, m;
            m = 0;
            printf("%2d [%d %d]:", DINDEX(i, j), i, j);
            for (k = heads[DINDEX(i,j)]; k >= 0; k = placements[k])
                printf(" %3d [%d,%d,%c]", k, k/2%w, k/2/w, k%2?'h':'v');
            printf("\n");
        }
#endif

    ret = 1;
    for (i = 0; i < wh*2; i++) {
        if (placements[i] == -2) {
            if (output)
                output[i] = -1;        /* ruled out */
        } else if (placements[i] != -3) {
            int p1, p2, di;

            p1 = i / 2;
            p2 = (i & 1) ? p1 + 1 : p1 + w;
            di = DINDEX(grid[p1], grid[p2]);

            if (i == heads[di] && placements[i] == -1) {
                if (output)
                    output[i] = 1;     /* certain */
            } else {
                if (output)
                    output[i] = 0;     /* uncertain */
                ret = 2;
            }
        }
    }

    done:
    /*
     * Free working data.
     */
    sfree(placements);
    sfree(heads);

    return ret;
}

/* ----------------------------------------------------------------------
 * End of solver code.
 */

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    int *grid, *grid2, *list;
    int i, j, k, m, todo, done, len;
    char *ret;

    /*
     * Allocate space in which to lay the grid out.
     */
    grid = snewn(wh, int);
    grid2 = snewn(wh, int);
    list = snewn(2*wh, int);

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
        /*
         * To begin with, set grid[i] = i for all i to indicate
         * that all squares are currently singletons. Later we'll
         * set grid[i] to be the index of the other end of the
         * domino on i.
         */
        for (i = 0; i < wh; i++)
            grid[i] = i;

        /*
         * Now prepare a list of the possible domino locations. There
         * are w*(h-1) possible vertical locations, and (w-1)*h
         * horizontal ones, for a total of 2*wh - h - w.
         *
         * I'm going to denote the vertical domino placement with
         * its top in square i as 2*i, and the horizontal one with
         * its left half in square i as 2*i+1.
         */
        k = 0;
        for (j = 0; j < h-1; j++)
            for (i = 0; i < w; i++)
                list[k++] = 2 * (j*w+i);   /* vertical positions */
        for (j = 0; j < h; j++)
            for (i = 0; i < w-1; i++)
                list[k++] = 2 * (j*w+i) + 1;   /* horizontal positions */
        assert(k == 2*wh - h - w);

        /*
         * Shuffle the list.
         */
        shuffle(list, k, sizeof(*list), rs);

        /*
         * Work down the shuffled list, placing a domino everywhere
         * we can.
         */
        for (i = 0; i < k; i++) {
            int horiz, xy, xy2;

            horiz = list[i] % 2;
            xy = list[i] / 2;
            xy2 = xy + (horiz ? 1 : w);

            if (grid[xy] == xy && grid[xy2] == xy2) {
                /*
                 * We can place this domino. Do so.
                 */
                grid[xy] = xy2;
                grid[xy2] = xy;
            }
        }

#ifdef GENERATION_DIAGNOSTICS
        printf("generated initial layout\n");
#endif

        /*
         * Now we've placed as many dominoes as we can immediately
         * manage. There will be squares remaining, but they'll be
         * singletons. So loop round and deal with the singletons
         * two by two.
         */
        while (1) {
#ifdef GENERATION_DIAGNOSTICS
            for (j = 0; j < h; j++) {
                for (i = 0; i < w; i++) {
                    int xy = j*w+i;
                    int v = grid[xy];
                    int c = (v == xy+1 ? '[' : v == xy-1 ? ']' :
                             v == xy+w ? 'n' : v == xy-w ? 'U' : '.');
                    putchar(c);
                }
                putchar('\n');
            }
            putchar('\n');
#endif

            /*
             * Our strategy is:
             *
             * First find a singleton square.
             *
             * Then breadth-first search out from the starting
             * square. From that square (and any others we reach on
             * the way), examine all four neighbours of the square.
             * If one is an end of a domino, we move to the _other_
             * end of that domino before looking at neighbours
             * again. When we encounter another singleton on this
             * search, stop.
             *
             * This will give us a path of adjacent squares such
             * that all but the two ends are covered in dominoes.
             * So we can now shuffle every domino on the path up by
             * one.
             *
             * (Chessboard colours are mathematically important
             * here: we always end up pairing each singleton with a
             * singleton of the other colour. However, we never
             * have to track this manually, since it's
             * automatically taken care of by the fact that we
             * always make an even number of orthogonal moves.)
             */
            for (i = 0; i < wh; i++)
                if (grid[i] == i)
                    break;
            if (i == wh)
                break;                 /* no more singletons; we're done. */

#ifdef GENERATION_DIAGNOSTICS
            printf("starting b.f.s. at singleton %d\n", i);
#endif
            /*
             * Set grid2 to -1 everywhere. It will hold our
             * distance-from-start values, and also our
             * backtracking data, during the b.f.s.
             */
            for (j = 0; j < wh; j++)
                grid2[j] = -1;
            grid2[i] = 0;              /* starting square has distance zero */

            /*
             * Start our to-do list of squares. It'll live in
             * `list'; since the b.f.s can cover every square at
             * most once there is no need for it to be circular.
             * We'll just have two counters tracking the end of the
             * list and the squares we've already dealt with.
             */
            done = 0;
            todo = 1;
            list[0] = i;

            /*
             * Now begin the b.f.s. loop.
             */
            while (done < todo) {
                int d[4], nd, x, y;

                i = list[done++];

#ifdef GENERATION_DIAGNOSTICS
                printf("b.f.s. iteration from %d\n", i);
#endif
                x = i % w;
                y = i / w;
                nd = 0;
                if (x > 0)
                    d[nd++] = i - 1;
                if (x+1 < w)
                    d[nd++] = i + 1;
                if (y > 0)
                    d[nd++] = i - w;
                if (y+1 < h)
                    d[nd++] = i + w;
                /*
                 * To avoid directional bias, process the
                 * neighbours of this square in a random order.
                 */
                shuffle(d, nd, sizeof(*d), rs);

                for (j = 0; j < nd; j++) {
                    k = d[j];
                    if (grid[k] == k) {
#ifdef GENERATION_DIAGNOSTICS
                        printf("found neighbouring singleton %d\n", k);
#endif
                        grid2[k] = i;
                        break;         /* found a target singleton! */
                    }

                    /*
                     * We're moving through a domino here, so we
                     * have two entries in grid2 to fill with
                     * useful data. In grid[k] - the square
                     * adjacent to where we came from - I'm going
                     * to put the address _of_ the square we came
                     * from. In the other end of the domino - the
                     * square from which we will continue the
                     * search - I'm going to put the distance.
                     */
                    m = grid[k];

                    if (grid2[m] < 0 || grid2[m] > grid2[i]+1) {
#ifdef GENERATION_DIAGNOSTICS
                        printf("found neighbouring domino %d/%d\n", k, m);
#endif
                        grid2[m] = grid2[i]+1;
                        grid2[k] = i;
                        /*
                         * And since we've now visited a new
                         * domino, add m to the to-do list.
                         */
                        assert(todo < wh);
                        list[todo++] = m;
                    }
                }

                if (j < nd) {
                    i = k;
#ifdef GENERATION_DIAGNOSTICS
                    printf("terminating b.f.s. loop, i = %d\n", i);
#endif
                    break;
                }

                i = -1;                /* just in case the loop terminates */
            }

            /*
             * We expect this b.f.s. to have found us a target
             * square.
             */
            assert(i >= 0);

            /*
             * Now we can follow the trail back to our starting
             * singleton, re-laying dominoes as we go.
             */
            while (1) {
                j = grid2[i];
                assert(j >= 0 && j < wh);
                k = grid[j];

                grid[i] = j;
                grid[j] = i;
#ifdef GENERATION_DIAGNOSTICS
                printf("filling in domino %d/%d (next %d)\n", i, j, k);
#endif
                if (j == k)
                    break;             /* we've reached the other singleton */
                i = k;
            }
#ifdef GENERATION_DIAGNOSTICS
            printf("fixup path completed\n");
#endif
        }

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

                if (params->unique) {
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
    } while (params->unique && solver(w, h, n, grid2, NULL) > 1);

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

static char *validate_desc(game_params *params, char *desc)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    int *occurrences;
    int i, j;
    char *ret;

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

static game_state *new_game(midend_data *me, game_params *params, char *desc)
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

    state->completed = state->cheated = FALSE;

    return state;
}

static game_state *dup_game(game_state *state)
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
    if (--state->numbers->refcount <= 0) {
        sfree(state->numbers->numbers);
        sfree(state->numbers);
    }
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    int n = state->params.n, w = n+2, h = n+1, wh = w*h;
    int *placements;
    char *ret;
    int retlen, retsize;
    int i, v;
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

	placements = snewn(wh*2, int);
	for (i = 0; i < wh*2; i++)
	    placements[i] = -3;
	solver(w, h, n, state->numbers->numbers, placements);

	/*
	 * First make a pass putting in edges for -1, then make a pass
	 * putting in dominoes for +1.
	 */
	retsize = 256;
	ret = snewn(retsize, char);
	retlen = sprintf(ret, "S");

	for (v = -1; v <= +1; v += 2)
	    for (i = 0; i < wh*2; i++)
		if (placements[i] == v) {
		    int p1 = i / 2;
		    int p2 = (i & 1) ? p1+1 : p1+w;

		    extra = sprintf(buf, ";%c%d,%d",
				    v==-1 ? 'E' : 'D', p1, p2);

		    if (retlen + extra + 1 >= retsize) {
			retsize = retlen + extra + 256;
			ret = sresize(ret, retsize, char);
		    }
		    strcpy(ret + retlen, buf);
		    retlen += extra;
		}

	sfree(placements);
    }

    return ret;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
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
}

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE * 3 / 4)
#define DOMINO_GUTTER (TILESIZE / 16)
#define DOMINO_RADIUS (TILESIZE / 8)
#define DOMINO_COFFSET (DOMINO_GUTTER + DOMINO_RADIUS)

#define COORD(x) ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x) ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

struct game_drawstate {
    int started;
    int w, h, tilesize;
    unsigned long *visible;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
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

        sprintf(buf, "%c%d,%d", button == RIGHT_BUTTON ? 'E' : 'D', d1, d2);
        return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(game_state *state, char *move)
{
    int n = state->params.n, w = n+2, h = n+1, wh = w*h;
    int d1, d2, d3, p;
    game_state *ret = dup_game(state);

    while (*move) {
        if (move[0] == 'S') {
            int i;

            ret->cheated = TRUE;

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
        unsigned char *used = snewn(TRI(n+1), unsigned char);

        memset(used, 0, TRI(n+1));
        for (i = 0; i < wh; i++)
            if (ret->grid[i] > i) {
                int n1, n2, di;

                n1 = ret->numbers->numbers[i];
                n2 = ret->numbers->numbers[ret->grid[i]];

                di = DINDEX(n1, n2);
                assert(di >= 0 && di < TRI(n+1));

                if (!used[di]) {
                    used[di] = 1;
                    ok++;
                }
            }

        sfree(used);
        if (ok == DCOUNT(n))
            ret->completed = TRUE;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    int n = params->n, w = n+2, h = n+1;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = w * TILESIZE + 2*BORDER;
    *y = h * TILESIZE + 2*BORDER;
}

static void game_set_size(game_drawstate *ds, game_params *params,
			  int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
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

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->visible = snewn(ds->w * ds->h, unsigned long);
    ds->tilesize = 0;                  /* not decided yet */
    for (i = 0; i < ds->w * ds->h; i++)
        ds->visible[i] = 0xFFFF;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
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

static void draw_tile(frontend *fe, game_drawstate *ds, game_state *state,
                      int x, int y, int type)
{
    int w = state->w /*, h = state->h */;
    int cx = COORD(x), cy = COORD(y);
    int nc;
    char str[80];
    int flags;

    draw_rect(fe, cx, cy, TILESIZE, TILESIZE, COL_BACKGROUND);

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

        if (flags & 0x80)
            bg = COL_DOMINOCLASH;
        else
            bg = COL_DOMINO;
        nc = COL_DOMINOTEXT;

        if (flags & 0x40) {
            int tmp = nc;
            nc = bg;
            bg = tmp;
        }

        if (type == TYPE_L || type == TYPE_T)
            draw_circle(fe, cx+DOMINO_COFFSET, cy+DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_R || type == TYPE_T)
            draw_circle(fe, cx+TILESIZE-1-DOMINO_COFFSET, cy+DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_L || type == TYPE_B)
            draw_circle(fe, cx+DOMINO_COFFSET, cy+TILESIZE-1-DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);
        if (type == TYPE_R || type == TYPE_B)
            draw_circle(fe, cx+TILESIZE-1-DOMINO_COFFSET,
                        cy+TILESIZE-1-DOMINO_COFFSET,
                        DOMINO_RADIUS, bg, bg);

        for (i = 0; i < 2; i++) {
            int x1, y1, x2, y2;

            x1 = cx + (i ? DOMINO_GUTTER : DOMINO_COFFSET);
            y1 = cy + (i ? DOMINO_COFFSET : DOMINO_GUTTER);
            x2 = cx + TILESIZE-1 - (i ? DOMINO_GUTTER : DOMINO_COFFSET);
            y2 = cy + TILESIZE-1 - (i ? DOMINO_COFFSET : DOMINO_GUTTER);
            if (type == TYPE_L)
                x2 = cx + TILESIZE-1;
            else if (type == TYPE_R)
                x1 = cx;
            else if (type == TYPE_T)
                y2 = cy + TILESIZE-1;
            else if (type == TYPE_B)
                y1 = cy;

            draw_rect(fe, x1, y1, x2-x1+1, y2-y1+1, bg);
        }
    } else {
        if (flags & EDGE_T)
            draw_rect(fe, cx+DOMINO_GUTTER, cy,
                      TILESIZE-2*DOMINO_GUTTER, 1, COL_EDGE);
        if (flags & EDGE_B)
            draw_rect(fe, cx+DOMINO_GUTTER, cy+TILESIZE-1,
                      TILESIZE-2*DOMINO_GUTTER, 1, COL_EDGE);
        if (flags & EDGE_L)
            draw_rect(fe, cx, cy+DOMINO_GUTTER,
                      1, TILESIZE-2*DOMINO_GUTTER, COL_EDGE);
        if (flags & EDGE_R)
            draw_rect(fe, cx+TILESIZE-1, cy+DOMINO_GUTTER,
                      1, TILESIZE-2*DOMINO_GUTTER, COL_EDGE);
        nc = COL_TEXT;
    }

    sprintf(str, "%d", state->numbers->numbers[y*w+x]);
    draw_text(fe, cx+TILESIZE/2, cy+TILESIZE/2, FONT_VARIABLE, TILESIZE/2,
              ALIGN_HCENTRE | ALIGN_VCENTRE, nc, str);

    draw_update(fe, cx, cy, TILESIZE, TILESIZE);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, int dir, game_ui *ui,
                 float animtime, float flashtime)
{
    int n = state->params.n, w = state->w, h = state->h, wh = w*h;
    int x, y, i;
    unsigned char *used;

    if (!ds->started) {
        int pw, ph;
        game_compute_size(&state->params, TILESIZE, &pw, &ph);
	draw_rect(fe, 0, 0, pw, ph, COL_BACKGROUND);
	draw_update(fe, 0, 0, pw, ph);
	ds->started = TRUE;
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

            if (c != TYPE_BLANK) {
                n1 = state->numbers->numbers[n];
                n2 = state->numbers->numbers[state->grid[n]];
                di = DINDEX(n1, n2);
                if (used[di] > 1)
                    c |= 0x80;         /* highlight a clash */
            } else {
                c |= state->edges[n];
            }

            if (flashtime != 0)
                c |= 0x40;             /* we're flashing */

	    if (ds->visible[n] != c) {
		draw_tile(fe, ds, state, x, y, c);
                ds->visible[n] = c;
	    }
	}

    sfree(used);
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

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame dominosa
#endif

const struct game thegame = {
    "Dominosa", "games.dominosa",
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
    FALSE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
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
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};
