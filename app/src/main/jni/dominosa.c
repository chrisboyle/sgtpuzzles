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
    COL_CURSOR, COL_DOMINOCURSOR,
    COL_HIGHLIGHT_1,
    COL_HIGHLIGHT_2,
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
      case 1: n = 4; break;
      case 2: n = 5; break;
      case 3: n = 6; break;
      case 4: n = 7; break;
      case 5: n = 8; break;
      case 6: n = 9; break;
      default: return FALSE;
    }

    sprintf(buf, _("Up to double-%d"), n);
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

static game_params *dup_params(const game_params *params)
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

static char *encode_params(const game_params *params, int full)
{
    char buf[80];
    sprintf(buf, "%d", params->n);
    if (full && !params->unique)
        strcat(buf, "a");
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = _("Maximum number on dominoes");
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = _("Ensure unique solution");
    ret[1].type = C_BOOLEAN;
    ret[1].sval = NULL;
    ret[1].ival = params->unique;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->n = atoi(cfg[0].sval);
    ret->unique = cfg[1].ival;

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->n < 1)
        return _("Maximum face number must be at least one");
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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int n = params->n, w = n+2, h = n+1, wh = w*h;
    int *grid, *grid2, *list;
    int i, j, k, len;
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

static char *validate_desc(const game_params *params, const char *desc)
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
            ret = ret ? ret : _("Game description shorter than expected");
        } else {
            if (*desc >= '0' && *desc <= '9')
                j = *desc++ - '0';
            else if (*desc == '[') {
                desc++;
                j = atoi(desc);
                while (*desc && isdigit((unsigned char)*desc)) desc++;
                if (*desc != ']')
                    ret = ret ? ret : _("Missing ']' in game description");
                else
                    desc++;
            } else {
                j = -1;
                ret = ret ? ret : _("Invalid syntax in game description");
            }
            if (j < 0 || j > n)
                ret = ret ? ret : _("Number out of range in game description");
            else
                occurrences[j]++;
        }
    }

    if (*desc)
        ret = ret ? ret : _("Game description longer than expected");

    if (!ret) {
        for (i = 0; i <= n; i++)
            if (occurrences[i] != n+2)
                ret = _("Incorrect number balance in game description");
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

    state->completed = state->cheated = FALSE;

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

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
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
				    (int)(v==-1 ? 'E' : 'D'), p1, p2);

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

static int game_can_format_as_text_now(const game_params *params)
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
    int cur_x, cur_y, cur_visible, highlight_1, highlight_2;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = 0;
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

static void android_cursor_visibility(game_ui *ui, int visible)
{
    ui->cur_visible = visible;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    if (!oldstate->completed && newstate->completed)
        ui->cur_visible = 0;
#ifdef ANDROID
    if (newstate->completed && ! newstate->cheated && oldstate && ! oldstate->completed) android_completed();
#endif
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
    int started;
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

        ui->cur_visible = 0;
        sprintf(buf, "%c%d,%d", (int)(button == RIGHT_BUTTON ? 'E' : 'D'), d1, d2);
        return dupstr(buf);
    } else if (IS_CURSOR_MOVE(button)) {
	ui->cur_visible = 1;

        move_cursor(button, &ui->cur_x, &ui->cur_y, 2*w-1, 2*h-1, 0);

	return "";
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
        return "";
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

    /* Cursors are (currently) usually reddish; however, since reddish
     * is used here for DOMINOCLASH, I'll go greenish. */
    ret[COL_CURSOR * 3 + 0] = 0.0F;
    ret[COL_CURSOR * 3 + 1] = 0.5F;
    ret[COL_CURSOR * 3 + 2] = 0.0F;

    ret[COL_DOMINOCURSOR * 3 + 0] = 0.25F;
    ret[COL_DOMINOCURSOR * 3 + 1] = 1.0F;
    ret[COL_DOMINOCURSOR * 3 + 2] = 0.25F;

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

    ds->started = FALSE;
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
    int nc, noc = -1;
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
    draw_text_outline(dr, cx+TILESIZE/2, cy+TILESIZE/2, FONT_VARIABLE, TILESIZE/2,
              ALIGN_HCENTRE | ALIGN_VCENTRE, nc, noc, str);

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

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
}

#ifndef NO_PRINTING
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
#endif

#ifdef COMBINED
#define thegame dominosa
#endif

const struct game thegame = {
    "Dominosa", "games.dominosa", "dominosa",
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
    NULL,  /* android_request_keys */
    android_cursor_visibility,
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
#ifndef NO_PRINTING
    TRUE, FALSE, game_print_size, game_print,
#endif
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};

/* vim: set shiftwidth=4 :set textwidth=80: */

