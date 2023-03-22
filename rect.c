/*
 * rect.c: Puzzle from nikoli.co.jp. You have a square grid with
 * numbers in some squares; you must divide the square grid up into
 * variously sized rectangles, such that every rectangle contains
 * exactly one numbered square and the area of each rectangle is
 * equal to the number contained in it.
 */

/*
 * TODO:
 * 
 *  - Improve singleton removal.
 *     + It would be nice to limit the size of the generated
 *       rectangles in accordance with existing constraints such as
 *       the maximum rectangle size and the one about not
 *       generating a rectangle the full width or height of the
 *       grid.
 *     + This could be achieved by making a less random choice
 *       about which of the available options to use.
 *     + Alternatively, we could create our rectangle and then
 *       split it up.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    COL_CORRECT,
    COL_LINE,
    COL_TEXT,
    COL_GRID,
    COL_DRAG, COL_DRAGERASE,
    COL_CURSOR,
    NCOLOURS
};

struct game_params {
    int w, h;
    float expandfactor;
    bool unique;
};

#define INDEX(state, x, y)    (((y) * (state)->w) + (x))
#define index(state, a, x, y) ((a) [ INDEX(state,x,y) ])
#define grid(state,x,y)       index(state, (state)->grid, x, y)
#define vedge(state,x,y)      index(state, (state)->vedge, x, y)
#define hedge(state,x,y)      index(state, (state)->hedge, x, y)

#define CRANGE(state,x,y,dx,dy) ( (x) >= dx && (x) < (state)->w && \
			        (y) >= dy && (y) < (state)->h )
#define RANGE(state,x,y)  CRANGE(state,x,y,0,0)
#define HRANGE(state,x,y) CRANGE(state,x,y,0,1)
#define VRANGE(state,x,y) CRANGE(state,x,y,1,0)

#define PREFERRED_TILE_SIZE 24
#define TILE_SIZE (ds->tilesize)
#ifdef SMALL_SCREEN
#define BORDER (2)
#else
#define BORDER (TILE_SIZE * 3 / 4)
#endif

#define CORNER_TOLERANCE 0.15F
#define CENTRE_TOLERANCE 0.15F

#define FLASH_TIME 0.13F

#define COORD(x) ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x) ( ((x) - BORDER) / TILE_SIZE )

struct game_state {
    int w, h;
    int *grid;			       /* contains the numbers */
    unsigned char *vedge;	       /* (w+1) x h */
    unsigned char *hedge;	       /* w x (h+1) */
    bool completed, cheated;
    unsigned char *correct;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 7;
    ret->expandfactor = 0.0F;
    ret->unique = true;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int w, h;
    char buf[80];

    switch (i) {
      case 0: w = 7, h = 7; break;
      case 1: w = 9, h = 9; break;
      case 2: w = 11, h = 11; break;
      case 3: w = 13, h = 13; break;
      case 4: w = 15, h = 15; break;
#ifndef SMALL_SCREEN
      case 5: w = 17, h = 17; break;
      case 6: w = 19, h = 19; break;
#endif
      default: return false;
    }

    sprintf(buf, "%dx%d", w, h);
    *name = dupstr(buf);
    *params = ret = snew(game_params);
    ret->w = w;
    ret->h = h;
    ret->expandfactor = 0.0F;
    ret->unique = true;
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
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'e') {
	string++;
	ret->expandfactor = (float)atof(string);
	while (*string &&
	       (*string == '.' || isdigit((unsigned char)*string))) string++;
    }
    if (*string == 'a') {
	string++;
	ret->unique = false;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);
    if (full && params->expandfactor)
        sprintf(data + strlen(data), "e%g", params->expandfactor);
    if (full && !params->unique)
        strcat(data, "a");

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Expansion factor";
    ret[2].type = C_STRING;
    sprintf(buf, "%g", params->expandfactor);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Ensure unique solution";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->unique;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->expandfactor = (float)atof(cfg[2].u.string.sval);
    ret->unique = cfg[3].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w <= 0 || params->h <= 0)
	return "Width and height must both be greater than zero";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (params->w*params->h < 2)
	return "Grid area must be greater than one";
    if (params->expandfactor < 0.0F)
	return "Expansion factor may not be negative";
    return NULL;
}

struct point {
    int x, y;
};

struct rect {
    int x, y;
    int w, h;
};

struct rectlist {
    struct rect *rects;
    int n;
};

struct numberdata {
    int area;
    int npoints;
    struct point *points;
};

/* ----------------------------------------------------------------------
 * Solver for Rectangles games.
 * 
 * This solver is souped up beyond the needs of actually _solving_
 * a puzzle. It is also designed to cope with uncertainty about
 * where the numbers have been placed. This is because I run it on
 * my generated grids _before_ placing the numbers, and have it
 * tell me where I need to place the numbers to ensure a unique
 * solution.
 */

static void remove_rect_placement(int w, int h,
                                  struct rectlist *rectpositions,
                                  int *overlaps,
                                  int rectnum, int placement)
{
    int x, y, xx, yy;

#ifdef SOLVER_DIAGNOSTICS
    printf("ruling out rect %d placement at %d,%d w=%d h=%d\n", rectnum,
           rectpositions[rectnum].rects[placement].x,
           rectpositions[rectnum].rects[placement].y,
           rectpositions[rectnum].rects[placement].w,
           rectpositions[rectnum].rects[placement].h);
#endif

    /*
     * Decrement each entry in the overlaps array to reflect the
     * removal of this rectangle placement.
     */
    for (yy = 0; yy < rectpositions[rectnum].rects[placement].h; yy++) {
        y = yy + rectpositions[rectnum].rects[placement].y;
        for (xx = 0; xx < rectpositions[rectnum].rects[placement].w; xx++) {
            x = xx + rectpositions[rectnum].rects[placement].x;

            assert(overlaps[(rectnum * h + y) * w + x] != 0);

            if (overlaps[(rectnum * h + y) * w + x] > 0)
                overlaps[(rectnum * h + y) * w + x]--;
        }
    }

    /*
     * Remove the placement from the list of positions for that
     * rectangle, by interchanging it with the one on the end.
     */
    if (placement < rectpositions[rectnum].n - 1) {
        struct rect t;

        t = rectpositions[rectnum].rects[rectpositions[rectnum].n - 1];
        rectpositions[rectnum].rects[rectpositions[rectnum].n - 1] =
            rectpositions[rectnum].rects[placement];
        rectpositions[rectnum].rects[placement] = t;
    }
    rectpositions[rectnum].n--;
}

static void remove_number_placement(int w, int h, struct numberdata *number,
                                    int index, int *rectbyplace)
{
    /*
     * Remove the entry from the rectbyplace array.
     */
    rectbyplace[number->points[index].y * w + number->points[index].x] = -1;

    /*
     * Remove the placement from the list of candidates for that
     * number, by interchanging it with the one on the end.
     */
    if (index < number->npoints - 1) {
        struct point t;

        t = number->points[number->npoints - 1];
        number->points[number->npoints - 1] = number->points[index];
        number->points[index] = t;
    }
    number->npoints--;
}

/*
 * Returns 0 for failure to solve due to inconsistency; 1 for
 * success; 2 for failure to complete a solution due to either
 * ambiguity or it being too difficult.
 */
static int rect_solver(int w, int h, int nrects, struct numberdata *numbers,
                       unsigned char *hedge, unsigned char *vedge,
		       random_state *rs)
{
    struct rectlist *rectpositions;
    int *overlaps, *rectbyplace, *workspace;
    int i, ret;

    /*
     * Start by setting up a list of candidate positions for each
     * rectangle.
     */
    rectpositions = snewn(nrects, struct rectlist);
    for (i = 0; i < nrects; i++) {
        int rw, rh, area = numbers[i].area;
        int j, minx, miny, maxx, maxy;
        struct rect *rlist;
        int rlistn, rlistsize;

        /*
         * For each rectangle, begin by finding the bounding
         * rectangle of its candidate number placements.
         */
        maxx = maxy = -1;
        minx = w;
        miny = h;
        for (j = 0; j < numbers[i].npoints; j++) {
            if (minx > numbers[i].points[j].x) minx = numbers[i].points[j].x;
            if (miny > numbers[i].points[j].y) miny = numbers[i].points[j].y;
            if (maxx < numbers[i].points[j].x) maxx = numbers[i].points[j].x;
            if (maxy < numbers[i].points[j].y) maxy = numbers[i].points[j].y;
        }

        /*
         * Now loop over all possible rectangle placements
         * overlapping a point within that bounding rectangle;
         * ensure each one actually contains a candidate number
         * placement, and add it to the list.
         */
        rlist = NULL;
        rlistn = rlistsize = 0;

        for (rw = 1; rw <= area && rw <= w; rw++) {
            int x, y;

            if (area % rw)
                continue;
            rh = area / rw;
            if (rh > h)
                continue;

            for (y = miny - rh + 1; y <= maxy; y++) {
                if (y < 0 || y+rh > h)
                    continue;

                for (x = minx - rw + 1; x <= maxx; x++) {
                    if (x < 0 || x+rw > w)
                        continue;

                    /*
                     * See if we can find a candidate number
                     * placement within this rectangle.
                     */
                    for (j = 0; j < numbers[i].npoints; j++)
                        if (numbers[i].points[j].x >= x &&
                            numbers[i].points[j].x < x+rw &&
                            numbers[i].points[j].y >= y &&
                            numbers[i].points[j].y < y+rh)
                            break;

                    if (j < numbers[i].npoints) {
                        /*
                         * Add this to the list of candidate
                         * placements for this rectangle.
                         */
                        if (rlistn >= rlistsize) {
                            rlistsize = rlistn + 32;
                            rlist = sresize(rlist, rlistsize, struct rect);
                        }
                        rlist[rlistn].x = x;
                        rlist[rlistn].y = y;
                        rlist[rlistn].w = rw;
                        rlist[rlistn].h = rh;
#ifdef SOLVER_DIAGNOSTICS
                        printf("rect %d [area %d]: candidate position at"
                               " %d,%d w=%d h=%d\n",
                               i, area, x, y, rw, rh);
#endif
                        rlistn++;
                    }
                }
            }
        }

        rectpositions[i].rects = rlist;
        rectpositions[i].n = rlistn;
    }

    /*
     * Next, construct a multidimensional array tracking how many
     * candidate positions for each rectangle overlap each square.
     * 
     * Indexing of this array is by the formula
     * 
     *   overlaps[(rectindex * h + y) * w + x]
     * 
     * A positive or zero value indicates what it sounds as if it
     * should; -1 indicates that this square _cannot_ be part of
     * this rectangle; and -2 indicates that it _definitely_ is
     * (which is distinct from 1, because one might very well know
     * that _if_ square S is part of rectangle R then it must be
     * because R is placed in a certain position without knowing
     * that it definitely _is_).
     */
    overlaps = snewn(nrects * w * h, int);
    memset(overlaps, 0, nrects * w * h * sizeof(int));
    for (i = 0; i < nrects; i++) {
        int j;

        for (j = 0; j < rectpositions[i].n; j++) {
            int xx, yy;

            for (yy = 0; yy < rectpositions[i].rects[j].h; yy++)
                for (xx = 0; xx < rectpositions[i].rects[j].w; xx++)
                    overlaps[(i * h + yy+rectpositions[i].rects[j].y) * w +
                             xx+rectpositions[i].rects[j].x]++;
        }
    }

    /*
     * Also we want an array covering the grid once, to make it
     * easy to figure out which squares are candidate number
     * placements for which rectangles. (The existence of this
     * single array assumes that no square starts off as a
     * candidate number placement for more than one rectangle. This
     * assumption is justified, because this solver is _either_
     * used to solve real problems - in which case there is a
     * single placement for every number - _or_ used to decide on
     * number placements for a new puzzle, in which case each
     * number's placements are confined to the intended position of
     * the rectangle containing that number.)
     */
    rectbyplace = snewn(w * h, int);
    for (i = 0; i < w*h; i++)
        rectbyplace[i] = -1;

    for (i = 0; i < nrects; i++) {
        int j;

        for (j = 0; j < numbers[i].npoints; j++) {
            int x = numbers[i].points[j].x;
            int y = numbers[i].points[j].y;

            assert(rectbyplace[y * w + x] == -1);
            rectbyplace[y * w + x] = i;
        }
    }

    workspace = snewn(nrects, int);

    /*
     * Now run the actual deduction loop.
     */
    while (1) {
        bool done_something = false;

#ifdef SOLVER_DIAGNOSTICS
        printf("starting deduction loop\n");

        for (i = 0; i < nrects; i++) {
            printf("rect %d overlaps:\n", i);
            {
                int x, y;
                for (y = 0; y < h; y++) {
                    for (x = 0; x < w; x++) {
                        printf("%3d", overlaps[(i * h + y) * w + x]);
                    }
                    printf("\n");
                }
            }
        }
        printf("rectbyplace:\n");
        {
            int x, y;
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    printf("%3d", rectbyplace[y * w + x]);
                }
                printf("\n");
            }
        }
#endif

        /*
         * Housekeeping. Look for rectangles whose number has only
         * one candidate position left, and mark that square as
         * known if it isn't already.
         */
        for (i = 0; i < nrects; i++) {
            if (numbers[i].npoints == 1) {
                int x = numbers[i].points[0].x;
                int y = numbers[i].points[0].y;
                if (overlaps[(i * h + y) * w + x] >= -1) {
                    int j;

                    if (overlaps[(i * h + y) * w + x] <= 0) {
                        ret = 0;       /* inconsistency */
                        goto cleanup;
                    }
#ifdef SOLVER_DIAGNOSTICS
                    printf("marking %d,%d as known for rect %d"
                           " (sole remaining number position)\n", x, y, i);
#endif

                    for (j = 0; j < nrects; j++)
                        overlaps[(j * h + y) * w + x] = -1;
                    
                    overlaps[(i * h + y) * w + x] = -2;
                }
            }
        }

        /*
         * Now look at the intersection of all possible placements
         * for each rectangle, and mark all squares in that
         * intersection as known for that rectangle if they aren't
         * already.
         */
        for (i = 0; i < nrects; i++) {
            int minx, miny, maxx, maxy, xx, yy, j;

            minx = miny = 0;
            maxx = w;
            maxy = h;

            for (j = 0; j < rectpositions[i].n; j++) {
                int x = rectpositions[i].rects[j].x;
                int y = rectpositions[i].rects[j].y;
                int w = rectpositions[i].rects[j].w;
                int h = rectpositions[i].rects[j].h;

                if (minx < x) minx = x;
                if (miny < y) miny = y;
                if (maxx > x+w) maxx = x+w;
                if (maxy > y+h) maxy = y+h;
            }

            for (yy = miny; yy < maxy; yy++)
                for (xx = minx; xx < maxx; xx++)
                    if (overlaps[(i * h + yy) * w + xx] >= -1) {
                        if (overlaps[(i * h + yy) * w + xx] <= 0) {
                            ret = 0;   /* inconsistency */
                            goto cleanup;
                        }
#ifdef SOLVER_DIAGNOSTICS
                        printf("marking %d,%d as known for rect %d"
                               " (intersection of all placements)\n",
                               xx, yy, i);
#endif

                        for (j = 0; j < nrects; j++)
                            overlaps[(j * h + yy) * w + xx] = -1;
                    
                        overlaps[(i * h + yy) * w + xx] = -2;
                    }
        }

        /*
         * Rectangle-focused deduction. Look at each rectangle in
         * turn and try to rule out some of its candidate
         * placements.
         */
        for (i = 0; i < nrects; i++) {
            int j;

            for (j = 0; j < rectpositions[i].n; j++) {
                int xx, yy, k;
                bool del = false;

                for (k = 0; k < nrects; k++)
                    workspace[k] = 0;

                for (yy = 0; yy < rectpositions[i].rects[j].h; yy++) {
                    int y = yy + rectpositions[i].rects[j].y;
                    for (xx = 0; xx < rectpositions[i].rects[j].w; xx++) {
                        int x = xx + rectpositions[i].rects[j].x;
 
                        if (overlaps[(i * h + y) * w + x] == -1) {
                            /*
                             * This placement overlaps a square
                             * which is _known_ to be part of
                             * another rectangle. Therefore we must
                             * rule it out.
                             */
#ifdef SOLVER_DIAGNOSTICS
                            printf("rect %d placement at %d,%d w=%d h=%d "
                                   "contains %d,%d which is known-other\n", i,
                                   rectpositions[i].rects[j].x,
                                   rectpositions[i].rects[j].y,
                                   rectpositions[i].rects[j].w,
                                   rectpositions[i].rects[j].h,
                                   x, y);
#endif
                            del = true;
                        }

                        if (rectbyplace[y * w + x] != -1) {
                            /*
                             * This placement overlaps one of the
                             * candidate number placements for some
                             * rectangle. Count it.
                             */
                            workspace[rectbyplace[y * w + x]]++;
                        }
                    }
                }

                if (!del) {
                    /*
                     * If we haven't ruled this placement out
                     * already, see if it overlaps _all_ of the
                     * candidate number placements for any
                     * rectangle. If so, we can rule it out.
                     */
                    for (k = 0; k < nrects; k++)
                        if (k != i && workspace[k] == numbers[k].npoints) {
#ifdef SOLVER_DIAGNOSTICS
                            printf("rect %d placement at %d,%d w=%d h=%d "
                                   "contains all number points for rect %d\n",
                                   i,
                                   rectpositions[i].rects[j].x,
                                   rectpositions[i].rects[j].y,
                                   rectpositions[i].rects[j].w,
                                   rectpositions[i].rects[j].h,
                                   k);
#endif
                            del = true;
                            break;
                        }

                    /*
                     * Failing that, see if it overlaps at least
                     * one of the candidate number placements for
                     * itself! (This might not be the case if one
                     * of those number placements has been removed
                     * recently.).
                     */
                    if (!del && workspace[i] == 0) {
#ifdef SOLVER_DIAGNOSTICS
                        printf("rect %d placement at %d,%d w=%d h=%d "
                               "contains none of its own number points\n",
                               i,
                               rectpositions[i].rects[j].x,
                               rectpositions[i].rects[j].y,
                               rectpositions[i].rects[j].w,
                               rectpositions[i].rects[j].h);
#endif
                        del = true;
                    }
                }

                if (del) {
                    remove_rect_placement(w, h, rectpositions, overlaps, i, j);

                    j--;               /* don't skip over next placement */

                    done_something = true;
                }
            }
        }

        /*
         * Square-focused deduction. Look at each square not marked
         * as known, and see if there are any which can only be
         * part of a single rectangle.
         */
        {
            int x, y, n, index;
            for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
                /* Known squares are marked as <0 everywhere, so we only need
                 * to check the overlaps entry for rect 0. */
                if (overlaps[y * w + x] < 0)
                    continue;          /* known already */

                n = 0;
                index = -1;
                for (i = 0; i < nrects; i++)
                    if (overlaps[(i * h + y) * w + x] > 0)
                        n++, index = i;

                if (n == 1) {
                    int j;

                    /*
                     * Now we can rule out all placements for
                     * rectangle `index' which _don't_ contain
                     * square x,y.
                     */
#ifdef SOLVER_DIAGNOSTICS
                    printf("square %d,%d can only be in rectangle %d\n",
                           x, y, index);
#endif
                    for (j = 0; j < rectpositions[index].n; j++) {
                        struct rect *r = &rectpositions[index].rects[j];
                        if (x >= r->x && x < r->x + r->w &&
                            y >= r->y && y < r->y + r->h)
                            continue;  /* this one is OK */
                        remove_rect_placement(w, h, rectpositions, overlaps,
                                              index, j);
                        j--;           /* don't skip over next placement */
                        done_something = true;
                    }
                }
            }
        }

        /*
         * If we've managed to deduce anything by normal means,
         * loop round again and see if there's more to be done.
         * Only if normal deduction has completely failed us should
         * we now move on to narrowing down the possible number
         * placements.
         */
        if (done_something)
            continue;

        /*
         * Now we have done everything we can with the current set
         * of number placements. So we need to winnow the number
         * placements so as to narrow down the possibilities. We do
         * this by searching for a candidate placement (of _any_
         * rectangle) which overlaps a candidate placement of the
         * number for some other rectangle.
         */
        if (rs) {
            struct rpn {
                int rect;
                int placement;
                int number;
            } *rpns = NULL;
            size_t nrpns = 0, rpnsize = 0;
            int j;

            for (i = 0; i < nrects; i++) {
                for (j = 0; j < rectpositions[i].n; j++) {
                    int xx, yy;

                    for (yy = 0; yy < rectpositions[i].rects[j].h; yy++) {
                        int y = yy + rectpositions[i].rects[j].y;
                        for (xx = 0; xx < rectpositions[i].rects[j].w; xx++) {
                            int x = xx + rectpositions[i].rects[j].x;

                            if (rectbyplace[y * w + x] >= 0 &&
                                rectbyplace[y * w + x] != i) {
                                /*
                                 * Add this to the list of
                                 * winnowing possibilities.
                                 */
                                if (nrpns >= rpnsize) {
                                    rpnsize = rpnsize * 3 / 2 + 32;
                                    rpns = sresize(rpns, rpnsize, struct rpn);
                                }
                                rpns[nrpns].rect = i;
                                rpns[nrpns].placement = j;
                                rpns[nrpns].number = rectbyplace[y * w + x];
                                nrpns++;
                            }
                        }
                    }
 
                }
            }

#ifdef SOLVER_DIAGNOSTICS
            printf("%d candidate rect placements we could eliminate\n", nrpns);
#endif
            if (nrpns > 0) {
                /*
                 * Now choose one of these unwanted rectangle
                 * placements, and eliminate it.
                 */
                int index = random_upto(rs, nrpns);
                int k, m;
                struct rpn rpn = rpns[index];
                struct rect r;
                sfree(rpns);

                i = rpn.rect;
                j = rpn.placement;
                k = rpn.number;
                r = rectpositions[i].rects[j];

                /*
                 * We rule out placement j of rectangle i by means
                 * of removing all of rectangle k's candidate
                 * number placements which do _not_ overlap it.
                 * This will ensure that it is eliminated during
                 * the next pass of rectangle-focused deduction.
                 */
#ifdef SOLVER_DIAGNOSTICS
                printf("ensuring number for rect %d is within"
                       " rect %d's placement at %d,%d w=%d h=%d\n",
                       k, i, r.x, r.y, r.w, r.h);
#endif

                for (m = 0; m < numbers[k].npoints; m++) {
                    int x = numbers[k].points[m].x;
                    int y = numbers[k].points[m].y;

                    if (x < r.x || x >= r.x + r.w ||
                        y < r.y || y >= r.y + r.h) {
#ifdef SOLVER_DIAGNOSTICS
                        printf("eliminating number for rect %d at %d,%d\n",
                               k, x, y);
#endif
                        remove_number_placement(w, h, &numbers[k],
                                                m, rectbyplace);
                        m--;           /* don't skip the next one */
                        done_something = true;
                    }
                }
            }
        }

        if (!done_something) {
#ifdef SOLVER_DIAGNOSTICS
            printf("terminating deduction loop\n");
#endif
            break;
        }
    }

    cleanup:
    ret = 1;
    for (i = 0; i < nrects; i++) {
#ifdef SOLVER_DIAGNOSTICS
        printf("rect %d has %d possible placements\n",
               i, rectpositions[i].n);
#endif
        if (rectpositions[i].n <= 0) {
            ret = 0;                   /* inconsistency */
        } else if (rectpositions[i].n > 1) {
            ret = 2;                   /* remaining uncertainty */
        } else if (hedge && vedge) {
            /*
             * Place the rectangle in its only possible position.
             */
            int x, y;
            struct rect *r = &rectpositions[i].rects[0];

            for (y = 0; y < r->h; y++) {
                if (r->x > 0)
		    vedge[(r->y+y) * w + r->x] = 1;
                if (r->x+r->w < w)
		    vedge[(r->y+y) * w + r->x+r->w] = 1;
            }
            for (x = 0; x < r->w; x++) {
                if (r->y > 0)
                    hedge[r->y * w + r->x+x] = 1;
                if (r->y+r->h < h)
                    hedge[(r->y+r->h) * w + r->x+x] = 1;
            }
	}
    }

    /*
     * Free up all allocated storage.
     */
    sfree(workspace);
    sfree(rectbyplace);
    sfree(overlaps);
    for (i = 0; i < nrects; i++)
        sfree(rectpositions[i].rects);
    sfree(rectpositions);

    return ret;
}

/* ----------------------------------------------------------------------
 * Grid generation code.
 */

/*
 * This function does one of two things. If passed r==NULL, it
 * counts the number of possible rectangles which cover the given
 * square, and returns it in *n. If passed r!=NULL then it _reads_
 * *n to find an index, counts the possible rectangles until it
 * reaches the nth, and writes it into r.
 * 
 * `scratch' is expected to point to an array of 2 * params->w
 * ints, used internally as scratch space (and passed in like this
 * to avoid re-allocating and re-freeing it every time round a
 * tight loop).
 */
static void enum_rects(game_params *params, int *grid, struct rect *r, int *n,
                       int sx, int sy, int *scratch)
{
    int rw, rh, mw, mh;
    int x, y, dx, dy;
    int maxarea, realmaxarea;
    int index = 0;
    int *top, *bottom;

    /*
     * Maximum rectangle area is 1/6 of total grid size, unless
     * this means we can't place any rectangles at all in which
     * case we set it to 2 at minimum.
     */
    maxarea = params->w * params->h / 6;
    if (maxarea < 2)
        maxarea = 2;

    /*
     * Scan the grid to find the limits of the region within which
     * any rectangle containing this point must fall. This will
     * save us trawling the inside of every rectangle later on to
     * see if it contains any used squares.
     */
    top = scratch;
    bottom = scratch + params->w;
    for (dy = -1; dy <= +1; dy += 2) {
        int *array = (dy == -1 ? top : bottom);
        for (dx = -1; dx <= +1; dx += 2) {
            for (x = sx; x >= 0 && x < params->w; x += dx) {
                array[x] = -2 * params->h * dy;
                for (y = sy; y >= 0 && y < params->h; y += dy) {
                    if (index(params, grid, x, y) == -1 &&
                        (x == sx || dy*y <= dy*array[x-dx]))
                        array[x] = y;
                    else
                        break;
                }
            }
        }
    }

    /*
     * Now scan again to work out the largest rectangles we can fit
     * in the grid, so that we can terminate the following loops
     * early once we get down to not having much space left in the
     * grid.
     */
    realmaxarea = 0;
    for (x = 0; x < params->w; x++) {
        int x2;

        rh = bottom[x] - top[x] + 1;
        if (rh <= 0)
            continue;                  /* no rectangles can start here */

        dx = (x > sx ? -1 : +1);
        for (x2 = x; x2 >= 0 && x2 < params->w; x2 += dx)
            if (bottom[x2] < bottom[x] || top[x2] > top[x])
                break;

        rw = abs(x2 - x);
        if (realmaxarea < rw * rh)
            realmaxarea = rw * rh;
    }

    if (realmaxarea > maxarea)
        realmaxarea = maxarea;

    /*
     * Rectangles which go right the way across the grid are
     * boring, although they can't be helped in the case of
     * extremely small grids. (Also they might be generated later
     * on by the singleton-removal process; we can't help that.)
     */
    mw = params->w - 1;
    if (mw < 3) mw++;
    mh = params->h - 1;
    if (mh < 3) mh++;

    for (rw = 1; rw <= mw; rw++)
        for (rh = 1; rh <= mh; rh++) {
            if (rw * rh > realmaxarea)
                continue;
            if (rw * rh == 1)
                continue;
            for (x = max(sx - rw + 1, 0); x <= min(sx, params->w - rw); x++)
                for (y = max(sy - rh + 1, 0); y <= min(sy, params->h - rh);
                     y++) {
                    /*
                     * Check this rectangle against the region we
                     * defined above.
                     */
                    if (top[x] <= y && top[x+rw-1] <= y &&
                        bottom[x] >= y+rh-1 && bottom[x+rw-1] >= y+rh-1) {
                        if (r && index == *n) {
                            r->x = x;
                            r->y = y;
                            r->w = rw;
                            r->h = rh;
                            return;
                        }
                        index++;
                    }
                }
        }

    assert(!r);
    *n = index;
}

static void place_rect(game_params *params, int *grid, struct rect r)
{
    int idx = INDEX(params, r.x, r.y);
    int x, y;

    for (x = r.x; x < r.x+r.w; x++)
        for (y = r.y; y < r.y+r.h; y++) {
            index(params, grid, x, y) = idx;
        }
#ifdef GENERATION_DIAGNOSTICS
    printf("    placing rectangle at (%d,%d) size %d x %d\n",
           r.x, r.y, r.w, r.h);
#endif
}

static struct rect find_rect(game_params *params, int *grid, int x, int y)
{
    int idx, w, h;
    struct rect r;

    /*
     * Find the top left of the rectangle.
     */
    idx = index(params, grid, x, y);

    if (idx < 0) {
        r.x = x;
        r.y = y;
        r.w = r.h = 1;
        return r;                      /* 1x1 singleton here */
    }

    y = idx / params->w;
    x = idx % params->w;

    /*
     * Find the width and height of the rectangle.
     */
    for (w = 1;
         (x+w < params->w && index(params,grid,x+w,y)==idx);
         w++);
    for (h = 1;
         (y+h < params->h && index(params,grid,x,y+h)==idx);
         h++);

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;

    return r;
}

#ifdef GENERATION_DIAGNOSTICS
static void display_grid(game_params *params, int *grid, int *numbers, int all)
{
    unsigned char *egrid = snewn((params->w*2+3) * (params->h*2+3),
                                 unsigned char);
    int x, y;
    int r = (params->w*2+3);

    memset(egrid, 0, (params->w*2+3) * (params->h*2+3));

    for (x = 0; x < params->w; x++)
        for (y = 0; y < params->h; y++) {
            int i = index(params, grid, x, y);
            if (x == 0 || index(params, grid, x-1, y) != i)
                egrid[(2*y+2) * r + (2*x+1)] = 1;
            if (x == params->w-1 || index(params, grid, x+1, y) != i)
                egrid[(2*y+2) * r + (2*x+3)] = 1;
            if (y == 0 || index(params, grid, x, y-1) != i)
                egrid[(2*y+1) * r + (2*x+2)] = 1;
            if (y == params->h-1 || index(params, grid, x, y+1) != i)
                egrid[(2*y+3) * r + (2*x+2)] = 1;
        }

    for (y = 1; y < 2*params->h+2; y++) {
        for (x = 1; x < 2*params->w+2; x++) {
            if (!((y|x)&1)) {
                int k = numbers ? index(params, numbers, x/2-1, y/2-1) : 0;
                if (k || (all && numbers)) printf("%2d", k); else printf("  ");
            } else if (!((y&x)&1)) {
                int v = egrid[y*r+x];
                if ((y&1) && v) v = '-';
                if ((x&1) && v) v = '|';
                if (!v) v = ' ';
                putchar(v);
                if (!(x&1)) putchar(v);
            } else {
                int c, d = 0;
                if (egrid[y*r+(x+1)]) d |= 1;
                if (egrid[(y-1)*r+x]) d |= 2;
                if (egrid[y*r+(x-1)]) d |= 4;
                if (egrid[(y+1)*r+x]) d |= 8;
                c = " ??+?-++?+|+++++"[d];
                putchar(c);
                if (!(x&1)) putchar(c);
            }
        }
        putchar('\n');
    }

    sfree(egrid);
}
#endif

static char *new_game_desc(const game_params *params_in, random_state *rs,
			   char **aux, bool interactive)
{
    game_params params_copy = *params_in; /* structure copy */
    game_params *params = &params_copy;
    int *grid, *numbers = NULL;
    int x, y, y2, y2last, yx, run, i, nsquares;
    char *desc, *p;
    int *enum_rects_scratch;
    game_params params2real, *params2 = &params2real;

    while (1) {
        /*
         * Set up the smaller width and height which we will use to
         * generate the base grid.
         */
        params2->w = (int)((float)params->w / (1.0F + params->expandfactor));
        if (params2->w < 2 && params->w >= 2) params2->w = 2;
        params2->h = (int)((float)params->h / (1.0F + params->expandfactor));
        if (params2->h < 2 && params->h >= 2) params2->h = 2;

        grid = snewn(params2->w * params2->h, int);

        enum_rects_scratch = snewn(2 * params2->w, int);

        nsquares = 0;
        for (y = 0; y < params2->h; y++)
            for (x = 0; x < params2->w; x++) {
                index(params2, grid, x, y) = -1;
                nsquares++;
            }

        /*
         * Place rectangles until we can't any more. We do this by
         * finding a square we haven't yet covered, and randomly
         * choosing a rectangle to cover it.
         */
        
        while (nsquares > 0) {
            int square = random_upto(rs, nsquares);
            int n;
            struct rect r;

            x = params2->w;
            y = params2->h;
            for (y = 0; y < params2->h; y++) {
                for (x = 0; x < params2->w; x++) {
                    if (index(params2, grid, x, y) == -1 && square-- == 0)
                        break;
                }
                if (x < params2->w)
                    break;
            }
            assert(x < params2->w && y < params2->h);

            /*
             * Now see how many rectangles fit around this one.
             */
            enum_rects(params2, grid, NULL, &n, x, y, enum_rects_scratch);

            if (!n) {
                /*
                 * There are no possible rectangles covering this
                 * square, meaning it must be a singleton. Mark it
                 * -2 so we know not to keep trying.
                 */
                index(params2, grid, x, y) = -2;
                nsquares--;
            } else {
                /*
                 * Pick one at random.
                 */
                n = random_upto(rs, n);
                enum_rects(params2, grid, &r, &n, x, y, enum_rects_scratch);

                /*
                 * Place it.
                 */
                place_rect(params2, grid, r);
                nsquares -= r.w * r.h;
            }
        }

        sfree(enum_rects_scratch);

        /*
         * Deal with singleton spaces remaining in the grid, one by
         * one.
         *
         * We do this by making a local change to the layout. There are
         * several possibilities:
         *
         *     +-----+-----+    Here, we can remove the singleton by
         *     |     |     |    extending the 1x2 rectangle below it
         *     +--+--+-----+    into a 1x3.
         *     |  |  |     |
         *     |  +--+     |
         *     |  |  |     |
         *     |  |  |     |
         *     |  |  |     |
         *     +--+--+-----+
         *
         *     +--+--+--+       Here, that trick doesn't work: there's no
         *     |     |  |       1 x n rectangle with the singleton at one
         *     |     |  |       end. Instead, we extend a 1 x n rectangle
         *     |     |  |       _out_ from the singleton, shaving a layer
         *     +--+--+  |       off the end of another rectangle. So if we
         *     |  |  |  |       extended up, we'd make our singleton part
         *     |  +--+--+       of a 1x3 and generate a 1x2 where the 2x2
         *     |  |     |       used to be; or we could extend right into
         *     +--+-----+       a 2x1, turning the 1x3 into a 1x2.
         *
         *     +-----+--+       Here, we can't even do _that_, since any
         *     |     |  |       direction we choose to extend the singleton
         *     +--+--+  |       will produce a new singleton as a result of
         *     |  |  |  |       truncating one of the size-2 rectangles.
         *     |  +--+--+       Fortunately, this case can _only_ occur when
         *     |  |     |       a singleton is surrounded by four size-2s
         *     +--+-----+       in this fashion; so instead we can simply
         *                      replace the whole section with a single 3x3.
         */
        for (x = 0; x < params2->w; x++) {
            for (y = 0; y < params2->h; y++) {
                if (index(params2, grid, x, y) < 0) {
                    int dirs[4], ndirs;

#ifdef GENERATION_DIAGNOSTICS
                    display_grid(params2, grid, NULL, false);
                    printf("singleton at %d,%d\n", x, y);
#endif

                    /*
                     * Check in which directions we can feasibly extend
                     * the singleton. We can extend in a particular
                     * direction iff either:
                     *
                     *  - the rectangle on that side of the singleton
                     *    is not 2x1, and we are at one end of the edge
                     *    of it we are touching
                     *
                     *  - it is 2x1 but we are on its short side.
                     *
                     * FIXME: we could plausibly choose between these
                     * based on the sizes of the rectangles they would
                     * create?
                     */
                    ndirs = 0;
                    if (x < params2->w-1) {
                        struct rect r = find_rect(params2, grid, x+1, y);
                        if ((r.w * r.h > 2 && (r.y==y || r.y+r.h-1==y)) || r.h==1)
                            dirs[ndirs++] = 1;   /* right */
                    }
                    if (y > 0) {
                        struct rect r = find_rect(params2, grid, x, y-1);
                        if ((r.w * r.h > 2 && (r.x==x || r.x+r.w-1==x)) || r.w==1)
                            dirs[ndirs++] = 2;   /* up */
                    }
                    if (x > 0) {
                        struct rect r = find_rect(params2, grid, x-1, y);
                        if ((r.w * r.h > 2 && (r.y==y || r.y+r.h-1==y)) || r.h==1)
                            dirs[ndirs++] = 4;   /* left */
                    }
                    if (y < params2->h-1) {
                        struct rect r = find_rect(params2, grid, x, y+1);
                        if ((r.w * r.h > 2 && (r.x==x || r.x+r.w-1==x)) || r.w==1)
                            dirs[ndirs++] = 8;   /* down */
                    }

                    if (ndirs > 0) {
                        int which, dir;
                        struct rect r1, r2;
                        memset(&r1, 0, sizeof(struct rect));
                        memset(&r2, 0, sizeof(struct rect));
                        which = random_upto(rs, ndirs);
                        dir = dirs[which];

                        switch (dir) {
                          case 1:          /* right */
                            assert(x < params2->w+1);
#ifdef GENERATION_DIAGNOSTICS
                            printf("extending right\n");
#endif
                            r1 = find_rect(params2, grid, x+1, y);
                            r2.x = x;
                            r2.y = y;
                            r2.w = 1 + r1.w;
                            r2.h = 1;
                            if (r1.y == y)
                                r1.y++;
                            r1.h--;
                            break;
                          case 2:          /* up */
                            assert(y > 0);
#ifdef GENERATION_DIAGNOSTICS
                            printf("extending up\n");
#endif
                            r1 = find_rect(params2, grid, x, y-1);
                            r2.x = x;
                            r2.y = r1.y;
                            r2.w = 1;
                            r2.h = 1 + r1.h;
                            if (r1.x == x)
                                r1.x++;
                            r1.w--;
                            break;
                          case 4:          /* left */
                            assert(x > 0);
#ifdef GENERATION_DIAGNOSTICS
                            printf("extending left\n");
#endif
                            r1 = find_rect(params2, grid, x-1, y);
                            r2.x = r1.x;
                            r2.y = y;
                            r2.w = 1 + r1.w;
                            r2.h = 1;
                            if (r1.y == y)
                                r1.y++;
                            r1.h--;
                            break;
                          case 8:          /* down */
                            assert(y < params2->h+1);
#ifdef GENERATION_DIAGNOSTICS
                            printf("extending down\n");
#endif
                            r1 = find_rect(params2, grid, x, y+1);
                            r2.x = x;
                            r2.y = y;
                            r2.w = 1;
                            r2.h = 1 + r1.h;
                            if (r1.x == x)
                                r1.x++;
                            r1.w--;
                            break;
                          default:     /* should never happen */
                            assert(!"invalid direction");
                        }
                        if (r1.h > 0 && r1.w > 0)
                            place_rect(params2, grid, r1);
                        place_rect(params2, grid, r2);
                    } else {
#ifndef NDEBUG
                        /*
                         * Sanity-check that there really is a 3x3
                         * rectangle surrounding this singleton and it
                         * contains absolutely everything we could
                         * possibly need.
                         */
                        {
                            int xx, yy;
                            assert(x > 0 && x < params2->w-1);
                            assert(y > 0 && y < params2->h-1);

                            for (xx = x-1; xx <= x+1; xx++)
                                for (yy = y-1; yy <= y+1; yy++) {
                                    struct rect r = find_rect(params2,grid,xx,yy);
                                    assert(r.x >= x-1);
                                    assert(r.y >= y-1);
                                    assert(r.x+r.w-1 <= x+1);
                                    assert(r.y+r.h-1 <= y+1);
                                }
                        }
#endif

#ifdef GENERATION_DIAGNOSTICS
                        printf("need the 3x3 trick\n");
#endif

                        /*
                         * FIXME: If the maximum rectangle area for
                         * this grid is less than 9, we ought to
                         * subdivide the 3x3 in some fashion. There are
                         * five other possibilities:
                         *
                         *  - a 6 and a 3
                         *  - a 4, a 3 and a 2
                         *  - three 3s
                         *  - a 3 and three 2s (two different arrangements).
                         */

                        {
                            struct rect r;
                            r.x = x-1;
                            r.y = y-1;
                            r.w = r.h = 3;
                            place_rect(params2, grid, r);
                        }
                    }
                }
            }
        }

        /*
         * We have now constructed a grid of the size specified in
         * params2. Now we extend it into a grid of the size specified
         * in params. We do this in two passes: we extend it vertically
         * until it's the right height, then we transpose it, then
         * extend it vertically again (getting it effectively the right
         * width), then finally transpose again.
         */
        for (i = 0; i < 2; i++) {
            int *grid2, *expand, *where;
            game_params params3real, *params3 = &params3real;

#ifdef GENERATION_DIAGNOSTICS
            printf("before expansion:\n");
            display_grid(params2, grid, NULL, true);
#endif

            /*
             * Set up the new grid.
             */
            grid2 = snewn(params2->w * params->h, int);
            expand = snewn(params2->h-1, int);
            where = snewn(params2->w, int);
            params3->w = params2->w;
            params3->h = params->h;

            /*
             * Decide which horizontal edges are going to get expanded,
             * and by how much.
             */
            for (y = 0; y < params2->h-1; y++)
                expand[y] = 0;
            for (y = params2->h; y < params->h; y++) {
                x = random_upto(rs, params2->h-1);
                expand[x]++;
            }

#ifdef GENERATION_DIAGNOSTICS
            printf("expand[] = {");
            for (y = 0; y < params2->h-1; y++)
                printf(" %d", expand[y]);
            printf(" }\n");
#endif

            /*
             * Perform the expansion. The way this works is that we
             * alternately:
             *
             *  - copy a row from grid into grid2
             *
             *  - invent some number of additional rows in grid2 where
             *    there was previously only a horizontal line between
             *    rows in grid, and make random decisions about where
             *    among these to place each rectangle edge that ran
             *    along this line.
             */
            for (y = y2 = y2last = 0; y < params2->h; y++) {
                /*
                 * Copy a single line from row y of grid into row y2 of
                 * grid2.
                 */
                for (x = 0; x < params2->w; x++) {
                    int val = index(params2, grid, x, y);
                    if (val / params2->w == y &&   /* rect starts on this line */
                        (y2 == 0 ||	       /* we're at the very top, or... */
                         index(params3, grid2, x, y2-1) / params3->w < y2last
                         /* this rect isn't already started */))
                        index(params3, grid2, x, y2) =
                        INDEX(params3, val % params2->w, y2);
                    else
                        index(params3, grid2, x, y2) =
                        index(params3, grid2, x, y2-1);
                }

                /*
                 * If that was the last line, terminate the loop early.
                 */
                if (++y2 == params3->h)
                    break;

                y2last = y2;

                /*
                 * Invent some number of additional lines. First walk
                 * along this line working out where to put all the
                 * edges that coincide with it.
                 */
                yx = -1;
                for (x = 0; x < params2->w; x++) {
                    if (index(params2, grid, x, y) !=
                        index(params2, grid, x, y+1)) {
                        /*
                         * This is a horizontal edge, so it needs
                         * placing.
                         */
                        if (x == 0 ||
                            (index(params2, grid, x-1, y) !=
                             index(params2, grid, x, y) &&
                             index(params2, grid, x-1, y+1) !=
                             index(params2, grid, x, y+1))) {
                            /*
                             * Here we have the chance to make a new
                             * decision.
                             */
                            yx = random_upto(rs, expand[y]+1);
                        } else {
                            /*
                             * Here we just reuse the previous value of
                             * yx.
                             */
                        }
                    } else
                        yx = -1;
                    where[x] = yx;
                }

                for (yx = 0; yx < expand[y]; yx++) {
                    /*
                     * Invent a single row. For each square in the row,
                     * we copy the grid entry from the square above it,
                     * unless we're starting the new rectangle here.
                     */
                    for (x = 0; x < params2->w; x++) {
                        if (yx == where[x]) {
                            int val = index(params2, grid, x, y+1);
                            val %= params2->w;
                            val = INDEX(params3, val, y2);
                            index(params3, grid2, x, y2) = val;
                        } else
                            index(params3, grid2, x, y2) =
                            index(params3, grid2, x, y2-1);
                    }

                    y2++;
                }
            }

            sfree(expand);
            sfree(where);

#ifdef GENERATION_DIAGNOSTICS
            printf("after expansion:\n");
            display_grid(params3, grid2, NULL, true);
#endif
            /*
             * Transpose.
             */
            params2->w = params3->h;
            params2->h = params3->w;
            sfree(grid);
            grid = snewn(params2->w * params2->h, int);
            for (x = 0; x < params2->w; x++)
                for (y = 0; y < params2->h; y++) {
                    int idx1 = INDEX(params2, x, y);
                    int idx2 = INDEX(params3, y, x);
                    int tmp;

                    tmp = grid2[idx2];
                    tmp = (tmp % params3->w) * params2->w + (tmp / params3->w);
                    grid[idx1] = tmp;
                }

            sfree(grid2);

            {
                int tmp;
                tmp = params->w;
                params->w = params->h;
                params->h = tmp;
            }

#ifdef GENERATION_DIAGNOSTICS
            printf("after transposition:\n");
            display_grid(params2, grid, NULL, true);
#endif
        }

        /*
         * Run the solver to narrow down the possible number
         * placements.
         */
        {
            struct numberdata *nd;
            int nnumbers, i, ret;

            /* Count the rectangles. */
            nnumbers = 0;
            for (y = 0; y < params->h; y++) {
                for (x = 0; x < params->w; x++) {
                    int idx = INDEX(params, x, y);
                    if (index(params, grid, x, y) == idx)
                        nnumbers++;
                }
            }

            nd = snewn(nnumbers, struct numberdata);

            /* Now set up each number's candidate position list. */
            i = 0;
            for (y = 0; y < params->h; y++) {
                for (x = 0; x < params->w; x++) {
                    int idx = INDEX(params, x, y);
                    if (index(params, grid, x, y) == idx) {
                        struct rect r = find_rect(params, grid, x, y);
                        int j, k, m;

                        nd[i].area = r.w * r.h;
                        nd[i].npoints = nd[i].area;
                        nd[i].points = snewn(nd[i].npoints, struct point);
                        m = 0;
                        for (j = 0; j < r.h; j++)
                            for (k = 0; k < r.w; k++) {
                                nd[i].points[m].x = k + r.x;
                                nd[i].points[m].y = j + r.y;
                                m++;
                            }
                        assert(m == nd[i].npoints);

                        i++;
                    }
                }
            }

	    if (params->unique)
		ret = rect_solver(params->w, params->h, nnumbers, nd,
				  NULL, NULL, rs);
	    else
		ret = 1;	       /* allow any number placement at all */

            if (ret == 1) {
                /*
                 * Now place the numbers according to the solver's
                 * recommendations.
                 */
                numbers = snewn(params->w * params->h, int);

                for (y = 0; y < params->h; y++)
                    for (x = 0; x < params->w; x++) {
                        index(params, numbers, x, y) = 0;
                    }

                for (i = 0; i < nnumbers; i++) {
                    int idx = random_upto(rs, nd[i].npoints);
                    int x = nd[i].points[idx].x;
                    int y = nd[i].points[idx].y;
                    index(params,numbers,x,y) = nd[i].area;
                }
            }

            /*
             * Clean up.
             */
            for (i = 0; i < nnumbers; i++)
                sfree(nd[i].points);
            sfree(nd);

            /*
             * If we've succeeded, then terminate the loop.
             */
            if (ret == 1)
                break;
        }

        /*
         * Give up and go round again.
         */
        sfree(grid);
    }

    /*
     * Store the solution in aux.
     */
    {
        char *ai;
        int len;

        len = 2 + (params->w-1)*params->h + (params->h-1)*params->w;
        ai = snewn(len, char);

        ai[0] = 'S';

        p = ai+1;

        for (y = 0; y < params->h; y++)
            for (x = 1; x < params->w; x++)
                *p++ = (index(params, grid, x, y) !=
                        index(params, grid, x-1, y) ? '1' : '0');

        for (y = 1; y < params->h; y++)
            for (x = 0; x < params->w; x++)
                *p++ = (index(params, grid, x, y) !=
                        index(params, grid, x, y-1) ? '1' : '0');

        assert(p - ai == len-1);
        *p = '\0';

        *aux = ai;
    }

#ifdef GENERATION_DIAGNOSTICS
    display_grid(params, grid, numbers, false);
#endif

    desc = snewn(11 * params->w * params->h, char);
    p = desc;
    run = 0;
    for (i = 0; i <= params->w * params->h; i++) {
        int n = (i < params->w * params->h ? numbers[i] : -1);

        if (!n)
            run++;
        else {
            if (run) {
                while (run > 0) {
                    int c = 'a' - 1 + run;
                    if (run > 26)
                        c = 'z';
                    *p++ = c;
                    run -= c - ('a' - 1);
                }
            } else {
                /*
                 * If there's a number in the very top left or
                 * bottom right, there's no point putting an
                 * unnecessary _ before or after it.
                 */
                if (p > desc && n > 0)
                    *p++ = '_';
            }
            if (n > 0)
                p += sprintf(p, "%d", n);
            run = 0;
        }
    }
    *p = '\0';

    sfree(grid);
    sfree(numbers);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int area = params->w * params->h;
    int squares = 0;

    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";

    return NULL;
}

static unsigned char *get_correct(game_state *state)
{
    unsigned char *ret;
    int x, y;

    ret = snewn(state->w * state->h, unsigned char);
    memset(ret, 0xFF, state->w * state->h);

    for (x = 0; x < state->w; x++)
	for (y = 0; y < state->h; y++)
	    if (index(state,ret,x,y) == 0xFF) {
		int rw, rh;
		int xx, yy;
		int num, area;
                bool valid;

		/*
		 * Find a rectangle starting at this point.
		 */
		rw = 1;
		while (x+rw < state->w && !vedge(state,x+rw,y))
		    rw++;
		rh = 1;
		while (y+rh < state->h && !hedge(state,x,y+rh))
		    rh++;

		/*
		 * We know what the dimensions of the rectangle
		 * should be if it's there at all. Find out if we
		 * really have a valid rectangle.
		 */
		valid = true;
		/* Check the horizontal edges. */
		for (xx = x; xx < x+rw; xx++) {
		    for (yy = y; yy <= y+rh; yy++) {
			int e = !HRANGE(state,xx,yy) || hedge(state,xx,yy);
			int ec = (yy == y || yy == y+rh);
			if (e != ec)
			    valid = false;
		    }
		}
		/* Check the vertical edges. */
		for (yy = y; yy < y+rh; yy++) {
		    for (xx = x; xx <= x+rw; xx++) {
			int e = !VRANGE(state,xx,yy) || vedge(state,xx,yy);
			int ec = (xx == x || xx == x+rw);
			if (e != ec)
			    valid = false;
		    }
		}

		/*
		 * If this is not a valid rectangle with no other
		 * edges inside it, we just mark this square as not
		 * complete and proceed to the next square.
		 */
		if (!valid) {
		    index(state, ret, x, y) = 0;
		    continue;
		}

		/*
		 * We have a rectangle. Now see what its area is,
		 * and how many numbers are in it.
		 */
		num = 0;
		area = 0;
		for (xx = x; xx < x+rw; xx++) {
		    for (yy = y; yy < y+rh; yy++) {
			area++;
			if (grid(state,xx,yy)) {
			    if (num > 0)
				valid = false;   /* two numbers */
			    num = grid(state,xx,yy);
			}
		    }
		}
		if (num != area)
		    valid = false;

		/*
		 * Now fill in the whole rectangle based on the
		 * value of `valid'.
		 */
		for (xx = x; xx < x+rw; xx++) {
		    for (yy = y; yy < y+rh; yy++) {
			index(state, ret, xx, yy) = valid;
		    }
		}
	    }

    return ret;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int x, y, i, area;

    state->w = params->w;
    state->h = params->h;

    area = state->w * state->h;

    state->grid = snewn(area, int);
    state->vedge = snewn(area, unsigned char);
    state->hedge = snewn(area, unsigned char);
    state->completed = false;
    state->cheated = false;

    i = 0;
    while (*desc) {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                state->grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
            state->grid[i++] = atoi(desc-1);
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);

    for (y = 0; y < state->h; y++)
	for (x = 0; x < state->w; x++)
	    vedge(state,x,y) = hedge(state,x,y) = 0;

    state->correct = get_correct(state);

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;

    ret->vedge = snewn(state->w * state->h, unsigned char);
    ret->hedge = snewn(state->w * state->h, unsigned char);
    ret->grid = snewn(state->w * state->h, int);
    ret->correct = snewn(ret->w * ret->h, unsigned char);

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    memcpy(ret->grid, state->grid, state->w * state->h * sizeof(int));
    memcpy(ret->vedge, state->vedge, state->w*state->h*sizeof(unsigned char));
    memcpy(ret->hedge, state->hedge, state->w*state->h*sizeof(unsigned char));

    memcpy(ret->correct, state->correct, state->w*state->h*sizeof(unsigned char));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->vedge);
    sfree(state->hedge);
    sfree(state->correct);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *ai, const char **error)
{
    unsigned char *vedge, *hedge;
    int x, y, len;
    char *ret, *p;
    int i, j, n;
    struct numberdata *nd;

    if (ai)
        return dupstr(ai);

    /*
     * Attempt the in-built solver.
     */

    /* Set up each number's (very short) candidate position list. */
    for (i = n = 0; i < state->h * state->w; i++)
        if (state->grid[i])
            n++;

    nd = snewn(n, struct numberdata);

    for (i = j = 0; i < state->h * state->w; i++)
        if (state->grid[i]) {
            nd[j].area = state->grid[i];
            nd[j].npoints = 1;
            nd[j].points = snewn(1, struct point);
            nd[j].points[0].x = i % state->w;
            nd[j].points[0].y = i / state->w;
            j++;
        }

    assert(j == n);

    vedge = snewn(state->w * state->h, unsigned char);
    hedge = snewn(state->w * state->h, unsigned char);
    memset(vedge, 0, state->w * state->h);
    memset(hedge, 0, state->w * state->h);

    rect_solver(state->w, state->h, n, nd, hedge, vedge, NULL);

    /*
     * Clean up.
     */
    for (i = 0; i < n; i++)
        sfree(nd[i].points);
    sfree(nd);

    len = 2 + (state->w-1)*state->h + (state->h-1)*state->w;
    ret = snewn(len, char);

    p = ret;
    *p++ = 'S';
    for (y = 0; y < state->h; y++)
        for (x = 1; x < state->w; x++)
            *p++ = vedge[y*state->w+x] ? '1' : '0';
    for (y = 1; y < state->h; y++)
	for (x = 0; x < state->w; x++)
	    *p++ = hedge[y*state->w+x] ? '1' : '0';
    *p++ = '\0';
    assert(p - ret == len);

    sfree(vedge);
    sfree(hedge);

    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char *ret, *p, buf[80];
    int i, x, y, col, maxlen;

    /*
     * First determine the number of spaces required to display a
     * number. We'll use at least two, because one looks a bit
     * silly.
     */
    col = 2;
    for (i = 0; i < state->w * state->h; i++) {
	x = sprintf(buf, "%d", state->grid[i]);
	if (col < x) col = x;
    }

    /*
     * Now we know the exact total size of the grid we're going to
     * produce: it's got 2*h+1 rows, each containing w lots of col,
     * w+1 boundary characters and a trailing newline.
     */
    maxlen = (2*state->h+1) * (state->w * (col+1) + 2);

    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y <= 2*state->h; y++) {
	for (x = 0; x <= 2*state->w; x++) {
	    if (x & y & 1) {
		/*
		 * Display a number.
		 */
		int v = grid(state, x/2, y/2);
		if (v)
		    sprintf(buf, "%*d", col, v);
		else
		    sprintf(buf, "%*s", col, "");
		memcpy(p, buf, col);
		p += col;
	    } else if (x & 1) {
		/*
		 * Display a horizontal edge or nothing.
		 */
		int h = (y==0 || y==2*state->h ? 1 :
			 HRANGE(state, x/2, y/2) && hedge(state, x/2, y/2));
		int i;
		if (h)
		    h = '-';
		else
		    h = ' ';
		for (i = 0; i < col; i++)
		    *p++ = h;
	    } else if (y & 1) {
		/*
		 * Display a vertical edge or nothing.
		 */
		int v = (x==0 || x==2*state->w ? 1 :
			 VRANGE(state, x/2, y/2) && vedge(state, x/2, y/2));
		if (v)
		    *p++ = '|';
		else
		    *p++ = ' ';
	    } else {
		/*
		 * Display a corner, or a vertical edge, or a
		 * horizontal edge, or nothing.
		 */
		int hl = (y==0 || y==2*state->h ? 1 :
			  HRANGE(state, (x-1)/2, y/2) && hedge(state, (x-1)/2, y/2));
		int hr = (y==0 || y==2*state->h ? 1 :
			  HRANGE(state, (x+1)/2, y/2) && hedge(state, (x+1)/2, y/2));
		int vu = (x==0 || x==2*state->w ? 1 :
			  VRANGE(state, x/2, (y-1)/2) && vedge(state, x/2, (y-1)/2));
		int vd = (x==0 || x==2*state->w ? 1 :
			  VRANGE(state, x/2, (y+1)/2) && vedge(state, x/2, (y+1)/2));
		if (!hl && !hr && !vu && !vd)
		    *p++ = ' ';
		else if (hl && hr && !vu && !vd)
		    *p++ = '-';
		else if (!hl && !hr && vu && vd)
		    *p++ = '|';
		else
		    *p++ = '+';
	    }
	}
	*p++ = '\n';
    }

    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

struct game_ui {
    /*
     * These coordinates are 2 times the obvious grid coordinates.
     * Hence, the top left of the grid is (0,0), the grid point to
     * the right of that is (2,0), the one _below that_ is (2,2)
     * and so on. This is so that we can specify a drag start point
     * on an edge (one odd coordinate) or in the middle of a square
     * (two odd coordinates) rather than always at a corner.
     * 
     * -1,-1 means no drag is in progress.
     */
    int drag_start_x;
    int drag_start_y;
    int drag_end_x;
    int drag_end_y;
    /*
     * This flag is set as soon as a dragging action moves the
     * mouse pointer away from its starting point, so that even if
     * the pointer _returns_ to its starting point the action is
     * treated as a small drag rather than a click.
     */
    bool dragged;
    /* This flag is set if we're doing an erase operation (i.e.
     * removing edges in the centre of the rectangle without altering
     * the outlines).
     */
    bool erasing;
    /*
     * These are the co-ordinates of the top-left and bottom-right squares
     * in the drag box, respectively, or -1 otherwise.
     */
    int x1;
    int y1;
    int x2;
    int y2;
    /*
     * These are the coordinates of a cursor, whether it's visible, and
     * whether it was used to start a drag.
     */
    int cur_x, cur_y;
    bool cur_visible, cur_dragging;
};

static void reset_ui(game_ui *ui)
{
    ui->drag_start_x = -1;
    ui->drag_start_y = -1;
    ui->drag_end_x = -1;
    ui->drag_end_y = -1;
    ui->x1 = -1;
    ui->y1 = -1;
    ui->x2 = -1;
    ui->y2 = -1;
    ui->dragged = false;
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    reset_ui(ui);
    ui->erasing = false;
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->cur_dragging = false;
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

static void coord_round(float x, float y, int *xr, int *yr)
{
    float xs, ys, xv, yv, dx, dy, dist;

    /*
     * Find the nearest square-centre.
     */
    xs = (float)floor(x) + 0.5F;
    ys = (float)floor(y) + 0.5F;

    /*
     * And find the nearest grid vertex.
     */
    xv = (float)floor(x + 0.5F);
    yv = (float)floor(y + 0.5F);

    /*
     * We allocate clicks in parts of the grid square to either
     * corners, edges or square centres, as follows:
     * 
     *   +--+--------+--+
     *   |  |        |  |
     *   +--+        +--+
     *   |   `.    ,'   |
     *   |     +--+     |
     *   |     |  |     |
     *   |     +--+     |
     *   |   ,'    `.   |
     *   +--+        +--+
     *   |  |        |  |
     *   +--+--------+--+
     * 
     * (Not to scale!)
     * 
     * In other words: we measure the square distance (i.e.
     * max(dx,dy)) from the click to the nearest corner, and if
     * it's within CORNER_TOLERANCE then we return a corner click.
     * We measure the square distance from the click to the nearest
     * centre, and if that's within CENTRE_TOLERANCE we return a
     * centre click. Failing that, we find which of the two edge
     * centres is nearer to the click and return that edge.
     */

    /*
     * Check for corner click.
     */
    dx = (float)fabs(x - xv);
    dy = (float)fabs(y - yv);
    dist = (dx > dy ? dx : dy);
    if (dist < CORNER_TOLERANCE) {
        *xr = 2 * (int)xv;
        *yr = 2 * (int)yv;
    } else {
        /*
         * Check for centre click.
         */
        dx = (float)fabs(x - xs);
        dy = (float)fabs(y - ys);
        dist = (dx > dy ? dx : dy);
        if (dist < CENTRE_TOLERANCE) {
            *xr = 1 + 2 * (int)xs;
            *yr = 1 + 2 * (int)ys;
        } else {
            /*
             * Failing both of those, see which edge we're closer to.
             * Conveniently, this is simply done by testing the relative
             * magnitude of dx and dy (which are currently distances from
             * the square centre).
             */
            if (dx > dy) {
                /* Vertical edge: x-coord of corner,
                 * y-coord of square centre. */
                *xr = 2 * (int)xv;
                *yr = 1 + 2 * (int)floor(ys);
            } else {
                /* Horizontal edge: x-coord of square centre,
                 * y-coord of corner. */
                *xr = 1 + 2 * (int)floor(xs);
                *yr = 2 * (int)yv;
            }
        }
    }
}

/*
 * Returns true if it has made any change to the grid.
 */
static bool grid_draw_rect(const game_state *state,
			  unsigned char *hedge, unsigned char *vedge,
			  int c, bool really, bool outline,
			  int x1, int y1, int x2, int y2)
{
    int x, y;
    bool changed = false;

    /*
     * Draw horizontal edges of rectangles.
     */
    for (x = x1; x < x2; x++)
        for (y = y1; y <= y2; y++)
            if (HRANGE(state,x,y)) {
                int val = index(state,hedge,x,y);
                if (y == y1 || y == y2) {
                    if (!outline) continue;
                    val = c;
                } else if (c == 1)
                    val = 0;
		changed = changed || (index(state,hedge,x,y) != val);
		if (really)
		    index(state,hedge,x,y) = val;
            }

    /*
     * Draw vertical edges of rectangles.
     */
    for (y = y1; y < y2; y++)
        for (x = x1; x <= x2; x++)
            if (VRANGE(state,x,y)) {
                int val = index(state,vedge,x,y);
                if (x == x1 || x == x2) {
                    if (!outline) continue;
                    val = c;
                } else if (c == 1)
                    val = 0;
		changed = changed || (index(state,vedge,x,y) != val);
                if (really)
		    index(state,vedge,x,y) = val;
            }

    return changed;
}

static bool ui_draw_rect(const game_state *state, const game_ui *ui,
			unsigned char *hedge, unsigned char *vedge, int c,
			bool really, bool outline)
{
    return grid_draw_rect(state, hedge, vedge, c, really, outline,
			  ui->x1, ui->y1, ui->x2, ui->y2);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    bool started;
    int w, h, tilesize;
    unsigned long *visible;
};

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cur_visible &&
        !(ui->drag_start_x >= 0 && !ui->cur_dragging)) {
        if (ui->cur_dragging) {
            if (!ui->dragged) return "Cancel";
            if ((button == CURSOR_SELECT2) == ui->erasing) return "Done";
            return "Cancel";
        }
        return button == CURSOR_SELECT ? "Mark" : "Erase";
    }
    return "";
}

static char *interpret_move(const game_state *from, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int xc, yc;
    bool startdrag = false, enddrag = false, active = false, erasing = false;
    char buf[80], *ret;

    button &= ~MOD_MASK;

    coord_round(FROMCOORD((float)x), FROMCOORD((float)y), &xc, &yc);

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        if (ui->drag_start_x >= 0 && ui->cur_dragging)
            reset_ui(ui); /* cancel keyboard dragging */
        startdrag = true;
        ui->cur_visible = ui->cur_dragging = false;
        active = true;
        erasing = (button == RIGHT_BUTTON);
    } else if (button == LEFT_RELEASE || button == RIGHT_RELEASE) {
        /* We assert we should have had a LEFT_BUTTON first. */
        if (ui->cur_visible) {
            ui->cur_visible = false;
            active = true;
        }
        assert(!ui->cur_dragging);
        enddrag = true;
        erasing = (button == RIGHT_RELEASE);
    } else if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cur_x, &ui->cur_y, from->w, from->h, false);
        ui->cur_visible = true;
        active = true;
        if (!ui->cur_dragging) return UI_UPDATE;
        coord_round((float)ui->cur_x + 0.5F, (float)ui->cur_y + 0.5F, &xc, &yc);
    } else if (IS_CURSOR_SELECT(button)) {
        if (ui->drag_start_x >= 0 && !ui->cur_dragging) {
            /*
             * If a mouse drag is in progress, ignore attempts to
             * start a keyboard one.
             */
            return NULL;
        }
        if (!ui->cur_visible) {
            assert(!ui->cur_dragging);
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        coord_round((float)ui->cur_x + 0.5F, (float)ui->cur_y + 0.5F, &xc, &yc);
        erasing = (button == CURSOR_SELECT2);
        if (ui->cur_dragging) {
            ui->cur_dragging = false;
            enddrag = true;
            active = true;
        } else {
            ui->cur_dragging = true;
            startdrag = true;
            active = true;
        }
    } else if (button == '\b' || button == 27) {
        if (!ui->cur_dragging) {
            ui->cur_visible = false;
        } else {
            assert(ui->cur_visible);
            reset_ui(ui); /* cancel keyboard dragging */
            ui->cur_dragging = false;
        }
        return UI_UPDATE;
    } else if (button != LEFT_DRAG && button != RIGHT_DRAG) {
        return NULL;
    }

    if (startdrag &&
	xc >= 0 && xc <= 2*from->w &&
	yc >= 0 && yc <= 2*from->h) {

        ui->drag_start_x = xc;
        ui->drag_start_y = yc;
        ui->drag_end_x = -1;
        ui->drag_end_y = -1;
        ui->dragged = false;
        ui->erasing = erasing;
        active = true;
    }

    if (ui->drag_start_x >= 0 &&
	(xc != ui->drag_end_x || yc != ui->drag_end_y)) {
	int t;

	if (ui->drag_end_x != -1 && ui->drag_end_y != -1)
	    ui->dragged = true;
        ui->drag_end_x = xc;
        ui->drag_end_y = yc;
        active = true;

	if (xc >= 0 && xc <= 2*from->w &&
	    yc >= 0 && yc <= 2*from->h) {
            ui->x1 = ui->drag_start_x;
            ui->x2 = ui->drag_end_x;
            if (ui->x2 < ui->x1) { t = ui->x1; ui->x1 = ui->x2; ui->x2 = t; }

            ui->y1 = ui->drag_start_y;
            ui->y2 = ui->drag_end_y;
            if (ui->y2 < ui->y1) { t = ui->y1; ui->y1 = ui->y2; ui->y2 = t; }

            ui->x1 = ui->x1 / 2;               /* rounds down */
            ui->x2 = (ui->x2+1) / 2;           /* rounds up */
            ui->y1 = ui->y1 / 2;               /* rounds down */
            ui->y2 = (ui->y2+1) / 2;           /* rounds up */
        } else {
            ui->x1 = -1;
            ui->y1 = -1;
            ui->x2 = -1;
            ui->y2 = -1;
        }
    }

    ret = NULL;

    if (enddrag && (ui->drag_start_x >= 0)) {
	if (xc >= 0 && xc <= 2*from->w &&
	    yc >= 0 && yc <= 2*from->h &&
            erasing == ui->erasing) {

	    if (ui->dragged) {
		if (ui_draw_rect(from, ui, from->hedge,
				 from->vedge, 1, false, !ui->erasing)) {
		    sprintf(buf, "%c%d,%d,%d,%d",
			    (int)(ui->erasing ? 'E' : 'R'),
			    ui->x1, ui->y1, ui->x2 - ui->x1, ui->y2 - ui->y1);
		    ret = dupstr(buf);
		}
	    } else {
		if ((xc & 1) && !(yc & 1) && HRANGE(from,xc/2,yc/2)) {
		    sprintf(buf, "H%d,%d", xc/2, yc/2);
		    ret = dupstr(buf);
		}
		if ((yc & 1) && !(xc & 1) && VRANGE(from,xc/2,yc/2)) {
		    sprintf(buf, "V%d,%d", xc/2, yc/2);
		    ret = dupstr(buf);
		}
	    }
	}

        reset_ui(ui);
	active = true;
    }

    if (ret)
	return ret;		       /* a move has been made */
    else if (active)
        return UI_UPDATE;
    else
	return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    int x1, y1, x2, y2, mode;

    if (move[0] == 'S') {
	const char *p = move+1;
	int x, y;

	ret = dup_game(from);
	ret->cheated = true;

	for (y = 0; y < ret->h; y++)
	    for (x = 1; x < ret->w; x++) {
		vedge(ret, x, y) = (*p == '1');
		if (*p) p++;
	    }
	for (y = 1; y < ret->h; y++)
	    for (x = 0; x < ret->w; x++) {
		hedge(ret, x, y) = (*p == '1');
		if (*p) p++;
	    }

	sfree(ret->correct);
	ret->correct = get_correct(ret);

	return ret;

    } else if ((move[0] == 'R' || move[0] == 'E') &&
	sscanf(move+1, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4 &&
	x1 >= 0 && x2 >= 0 && x1+x2 <= from->w &&
	y1 >= 0 && y2 >= 0 && y1+y2 <= from->h) {
	x2 += x1;
	y2 += y1;
	mode = move[0];
    } else if ((move[0] == 'H' || move[0] == 'V') &&
	       sscanf(move+1, "%d,%d", &x1, &y1) == 2 &&
	       (move[0] == 'H' ? HRANGE(from, x1, y1) :
		VRANGE(from, x1, y1))) {
	mode = move[0];
    } else
	return NULL;		       /* can't parse move string */

    ret = dup_game(from);

    if (mode == 'R' || mode == 'E') {
	grid_draw_rect(ret, ret->hedge, ret->vedge, 1, true,
                       mode == 'R', x1, y1, x2, y2);
    } else if (mode == 'H') {
	hedge(ret,x1,y1) = !hedge(ret,x1,y1);
    } else if (mode == 'V') {
	vedge(ret,x1,y1) = !vedge(ret,x1,y1);
    }

    sfree(ret->correct);
    ret->correct = get_correct(ret);

    /*
     * We've made a real change to the grid. Check to see
     * if the game has been completed.
     */
    if (!ret->completed) {
	int x, y;
        bool ok;

	ok = true;
	for (x = 0; x < ret->w; x++)
	    for (y = 0; y < ret->h; y++)
		if (!index(ret, ret->correct, x, y))
		    ok = false;

	if (ok)
	    ret->completed = true;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define CORRECT (1L<<16)
#define CURSOR  (1L<<17)

#define COLOUR(k) ( (k)==1 ? COL_LINE : (k)==2 ? COL_DRAG : COL_DRAGERASE )
#define MAX4(x,y,z,w) ( max(max(x,y),max(z,w)) )

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = params->w * TILE_SIZE + 2*BORDER + 1;
    *y = params->h * TILE_SIZE + 2*BORDER + 1;
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

    ret[COL_GRID * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_GRID * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_GRID * 3 + 2] = 0.5F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_DRAG * 3 + 0] = 1.0F;
    ret[COL_DRAG * 3 + 1] = 0.0F;
    ret[COL_DRAG * 3 + 2] = 0.0F;

    ret[COL_DRAGERASE * 3 + 0] = 0.2F;
    ret[COL_DRAGERASE * 3 + 1] = 0.2F;
    ret[COL_DRAGERASE * 3 + 2] = 1.0F;

    ret[COL_CORRECT * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_CORRECT * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_CORRECT * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_LINE * 3 + 0] = 0.0F;
    ret[COL_LINE * 3 + 1] = 0.0F;
    ret[COL_LINE * 3 + 2] = 0.0F;

    ret[COL_TEXT * 3 + 0] = 0.0F;
    ret[COL_TEXT * 3 + 1] = 0.0F;
    ret[COL_TEXT * 3 + 2] = 0.0F;

    ret[COL_CURSOR * 3 + 0] = 1.0F;
    ret[COL_CURSOR * 3 + 1] = 0.5F;
    ret[COL_CURSOR * 3 + 2] = 0.5F;

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

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, unsigned char *hedge, unsigned char *vedge,
                      unsigned char *corners, unsigned long bgflags)
{
    int cx = COORD(x), cy = COORD(y);
    char str[80];

    draw_rect(dr, cx, cy, TILE_SIZE+1, TILE_SIZE+1, COL_GRID);
    draw_rect(dr, cx+1, cy+1, TILE_SIZE-1, TILE_SIZE-1,
	      (bgflags & CURSOR) ? COL_CURSOR :
              (bgflags & CORRECT) ? COL_CORRECT : COL_BACKGROUND);

    if (grid(state,x,y)) {
	sprintf(str, "%d", grid(state,x,y));
	draw_text(dr, cx+TILE_SIZE/2, cy+TILE_SIZE/2, FONT_VARIABLE,
		  TILE_SIZE/2, ALIGN_HCENTRE | ALIGN_VCENTRE, COL_TEXT, str);
    }

    /*
     * Draw edges.
     */
    if (!HRANGE(state,x,y) || index(state,hedge,x,y))
	draw_rect(dr, cx, cy, TILE_SIZE+1, 2,
                  HRANGE(state,x,y) ? COLOUR(index(state,hedge,x,y)) :
                  COL_LINE);
    if (!HRANGE(state,x,y+1) || index(state,hedge,x,y+1))
	draw_rect(dr, cx, cy+TILE_SIZE-1, TILE_SIZE+1, 2,
                  HRANGE(state,x,y+1) ? COLOUR(index(state,hedge,x,y+1)) :
                  COL_LINE);
    if (!VRANGE(state,x,y) || index(state,vedge,x,y))
	draw_rect(dr, cx, cy, 2, TILE_SIZE+1,
                  VRANGE(state,x,y) ? COLOUR(index(state,vedge,x,y)) :
                  COL_LINE);
    if (!VRANGE(state,x+1,y) || index(state,vedge,x+1,y))
	draw_rect(dr, cx+TILE_SIZE-1, cy, 2, TILE_SIZE+1,
                  VRANGE(state,x+1,y) ? COLOUR(index(state,vedge,x+1,y)) :
                  COL_LINE);

    /*
     * Draw corners.
     */
    if (index(state,corners,x,y))
	draw_rect(dr, cx, cy, 2, 2,
                  COLOUR(index(state,corners,x,y)));
    if (x+1 < state->w && index(state,corners,x+1,y))
	draw_rect(dr, cx+TILE_SIZE-1, cy, 2, 2,
                  COLOUR(index(state,corners,x+1,y)));
    if (y+1 < state->h && index(state,corners,x,y+1))
	draw_rect(dr, cx, cy+TILE_SIZE-1, 2, 2,
                  COLOUR(index(state,corners,x,y+1)));
    if (x+1 < state->w && y+1 < state->h && index(state,corners,x+1,y+1))
	draw_rect(dr, cx+TILE_SIZE-1, cy+TILE_SIZE-1, 2, 2,
                  COLOUR(index(state,corners,x+1,y+1)));

    draw_update(dr, cx, cy, TILE_SIZE+1, TILE_SIZE+1);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y;
    unsigned char *hedge, *vedge, *corners;

    if (ui->dragged) {
        hedge = snewn(state->w*state->h, unsigned char);
        vedge = snewn(state->w*state->h, unsigned char);
        memcpy(hedge, state->hedge, state->w*state->h);
        memcpy(vedge, state->vedge, state->w*state->h);
        ui_draw_rect(state, ui, hedge, vedge, ui->erasing ? 3 : 2, true, true);
    } else {
        hedge = state->hedge;
        vedge = state->vedge;
    }

    corners = snewn(state->w * state->h, unsigned char);
    memset(corners, 0, state->w * state->h);
    for (x = 0; x < state->w; x++)
	for (y = 0; y < state->h; y++) {
	    if (x > 0) {
		int e = index(state, vedge, x, y);
		if (index(state,corners,x,y) < e)
		    index(state,corners,x,y) = e;
		if (y+1 < state->h &&
		    index(state,corners,x,y+1) < e)
		    index(state,corners,x,y+1) = e;
	    }
	    if (y > 0) {
		int e = index(state, hedge, x, y);
		if (index(state,corners,x,y) < e)
		    index(state,corners,x,y) = e;
		if (x+1 < state->w &&
		    index(state,corners,x+1,y) < e)
		    index(state,corners,x+1,y) = e;
	    }
	}

    if (!ds->started) {
	draw_rect(dr, COORD(0)-1, COORD(0)-1,
		  ds->w*TILE_SIZE+3, ds->h*TILE_SIZE+3, COL_LINE);
	ds->started = true;
	draw_update(dr, 0, 0,
		    state->w * TILE_SIZE + 2*BORDER + 1,
		    state->h * TILE_SIZE + 2*BORDER + 1);
    }

    for (x = 0; x < state->w; x++)
	for (y = 0; y < state->h; y++) {
	    unsigned long c = 0;

	    if (HRANGE(state,x,y))
                c |= index(state,hedge,x,y);
	    if (HRANGE(state,x,y+1))
		c |= index(state,hedge,x,y+1) << 2;
	    if (VRANGE(state,x,y))
		c |= index(state,vedge,x,y) << 4;
	    if (VRANGE(state,x+1,y))
		c |= index(state,vedge,x+1,y) << 6;
	    c |= index(state,corners,x,y) << 8;
	    if (x+1 < state->w)
		c |= index(state,corners,x+1,y) << 10;
	    if (y+1 < state->h)
		c |= index(state,corners,x,y+1) << 12;
	    if (x+1 < state->w && y+1 < state->h)
		/* cast to prevent 2<<14 sign-extending on promotion to long */
		c |= (unsigned long)index(state,corners,x+1,y+1) << 14;
	    if (index(state, state->correct, x, y) && !flashtime)
		c |= CORRECT;
            if (ui->cur_visible && ui->cur_x == x && ui->cur_y == y)
                c |= CURSOR;

	    if (index(ds,ds->visible,x,y) != c) {
		draw_tile(dr, ds, state, x, y, hedge, vedge, corners,
                          (c & (CORRECT|CURSOR)) );
		index(ds,ds->visible,x,y) = c;
	    }
	}

    {
	char buf[256];

	if (ui->dragged &&
	    ui->x1 >= 0 && ui->y1 >= 0 &&
	    ui->x2 >= 0 && ui->y2 >= 0) {
	    sprintf(buf, "%dx%d ",
		    ui->x2-ui->x1,
		    ui->y2-ui->y1);
	} else {
	    buf[0] = '\0';
	}

        if (state->cheated)
            strcat(buf, "Auto-solved.");
        else if (state->completed)
            strcat(buf, "COMPLETED!");

        status_bar(dr, buf);
    }

    if (hedge != state->hedge) {
        sfree(hedge);
        sfree(vedge);
    }

    sfree(corners);
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
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        *x = COORD(ui->cur_x);
        *y = COORD(ui->cur_y);
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

    /*
     * I'll use 5mm squares by default.
     */
    game_compute_size(params, 500, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->w, h = state->h;
    int ink = print_mono_colour(dr, 0);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, TILE_SIZE / 10);
    draw_rect_outline(dr, COORD(0), COORD(0), w*TILE_SIZE, h*TILE_SIZE, ink);

    /*
     * Grid. We have to make the grid lines particularly thin,
     * because users will be drawing lines _along_ them and we want
     * those lines to be visible.
     */
    print_line_width(dr, TILE_SIZE / 256);
    for (x = 1; x < w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), ink);
    for (y = 1; y < h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), ink);

    /*
     * Solution.
     */
    print_line_width(dr, TILE_SIZE / 10);
    for (y = 0; y <= h; y++)
	for (x = 0; x <= w; x++) {
	    if (HRANGE(state,x,y) && hedge(state,x,y))
		draw_line(dr, COORD(x), COORD(y), COORD(x+1), COORD(y), ink);
	    if (VRANGE(state,x,y) && vedge(state,x,y))
		draw_line(dr, COORD(x), COORD(y), COORD(x), COORD(y+1), ink);
	}

    /*
     * Clues.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (grid(state,x,y)) {
		char str[80];
		sprintf(str, "%d", grid(state,x,y));
		draw_text(dr, COORD(x)+TILE_SIZE/2, COORD(y)+TILE_SIZE/2,
			  FONT_VARIABLE, TILE_SIZE/2,
			  ALIGN_HCENTRE | ALIGN_VCENTRE, ink, str);
	    }
}

#ifdef COMBINED
#define thegame rect
#endif

const struct game thegame = {
    "Rectangles", "games.rectangles", "rect",
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
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
