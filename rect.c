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
 *  - Improve on singleton removal by making an aesthetic choice
 *    about which of the options to take.
 * 
 *  - When doing the 3x3 trick in singleton removal, limit the size
 *    of the generated rectangles in accordance with the max
 *    rectangle size.
 * 
 *  - It might be interesting to deliberately try to place
 *    numbers so as to reduce alternative solution patterns. I
 *    doubt we can do a perfect job of this, but we can make a
 *    start by, for example, noticing pairs of 2-rects
 *    alongside one another and _not_ putting their numbers at
 *    opposite ends.
 *
 *  - If we start by sorting the rectlist in descending order
 *    of area, we might be able to bias our random number
 *    selection to produce a few large rectangles more often
 *    than oodles of small ones? Unsure, but might be worth a
 *    try.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

const char *const game_name = "Rectangles";
const int game_can_configure = TRUE;

enum {
    COL_BACKGROUND,
    COL_CORRECT,
    COL_LINE,
    COL_TEXT,
    COL_GRID,
    COL_DRAG,
    NCOLOURS
};

struct game_params {
    int w, h;
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

#define TILE_SIZE 24
#define BORDER 18

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
    int completed;
};

game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 7;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int w, h;
    char buf[80];

    switch (i) {
      case 0: w = 7, h = 7; break;
      case 1: w = 11, h = 11; break;
      case 2: w = 15, h = 15; break;
      case 3: w = 19, h = 19; break;
      default: return FALSE;
    }

    sprintf(buf, "%dx%d", w, h);
    *name = dupstr(buf);
    *params = ret = snew(game_params);
    ret->w = w;
    ret->h = h;
    return TRUE;
}

void free_params(game_params *params)
{
    sfree(params);
}

game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

game_params *decode_params(char const *string)
{
    game_params *ret = default_params();

    ret->w = ret->h = atoi(string);
    while (*string && isdigit(*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
    }

    return ret;
}

char *encode_params(game_params *params)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

config_item *game_configure(game_params *params)
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

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

char *validate_params(game_params *params)
{
    if (params->w <= 0 && params->h <= 0)
	return "Width and height must both be greater than zero";
    if (params->w < 2 && params->h < 2)
	return "Grid area must be greater than one";
    return NULL;
}

struct rect {
    int x, y;
    int w, h;
};

struct rectlist {
    struct rect *rects;
    int n;
};

static struct rectlist *get_rectlist(game_params *params, int *grid)
{
    int rw, rh;
    int x, y;
    int maxarea;
    struct rect *rects = NULL;
    int nrects = 0, rectsize = 0;

    /*
     * Maximum rectangle area is 1/6 of total grid size, unless
     * this means we can't place any rectangles at all in which
     * case we set it to 2 at minimum.
     */
    maxarea = params->w * params->h / 6;
    if (maxarea < 2)
        maxarea = 2;

    for (rw = 1; rw <= params->w; rw++)
        for (rh = 1; rh <= params->h; rh++) {
            if (rw * rh > maxarea)
                continue;
            if (rw * rh == 1)
                continue;
            for (x = 0; x <= params->w - rw; x++)
                for (y = 0; y <= params->h - rh; y++) {
                    if (nrects >= rectsize) {
                        rectsize = nrects + 256;
                        rects = sresize(rects, rectsize, struct rect);
                    }

                    rects[nrects].x = x;
                    rects[nrects].y = y;
                    rects[nrects].w = rw;
                    rects[nrects].h = rh;
                    nrects++;
                }
        }

    if (nrects > 0) {
        struct rectlist *ret;
        ret = snew(struct rectlist);
        ret->rects = rects;
        ret->n = nrects;
        return ret;
    } else {
        assert(rects == NULL);         /* hence no need to free */
        return NULL;
    }
}

static void free_rectlist(struct rectlist *list)
{
    sfree(list->rects);
    sfree(list);
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
static void display_grid(game_params *params, int *grid, int *numbers)
{
    unsigned char *egrid = snewn((params->w*2+3) * (params->h*2+3),
                                 unsigned char);
    memset(egrid, 0, (params->w*2+3) * (params->h*2+3));
    int x, y;
    int r = (params->w*2+3);

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
                int k = index(params, numbers, x/2-1, y/2-1);
                if (k) printf("%2d", k); else printf("  ");
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

char *new_game_seed(game_params *params, random_state *rs)
{
    int *grid, *numbers;
    struct rectlist *list;
    int x, y, run, i;
    char *seed, *p;

    grid = snewn(params->w * params->h, int);
    numbers = snewn(params->w * params->h, int);

    for (y = 0; y < params->h; y++)
        for (x = 0; x < params->w; x++) {
            index(params, grid, x, y) = -1;
            index(params, numbers, x, y) = 0;
        }

    list = get_rectlist(params, grid);
    assert(list != NULL);

    /*
     * Place rectangles until we can't any more.
     */
    while (list->n > 0) {
        int i, m;
        struct rect r;

        /*
         * Pick a random rectangle.
         */
        i = random_upto(rs, list->n);
        r = list->rects[i];

        /*
         * Place it.
         */
        place_rect(params, grid, r);

        /*
         * Winnow the list by removing any rectangles which
         * overlap this one.
         */
        m = 0;
        for (i = 0; i < list->n; i++) {
            struct rect s = list->rects[i];
            if (s.x+s.w <= r.x || r.x+r.w <= s.x ||
                s.y+s.h <= r.y || r.y+r.h <= s.y)
                list->rects[m++] = s;
        }
        list->n = m;
    }

    free_rectlist(list);

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
    for (x = 0; x < params->w; x++) {
        for (y = 0; y < params->h; y++) {
            if (index(params, grid, x, y) < 0) {
                int dirs[4], ndirs;

#ifdef GENERATION_DIAGNOSTICS
                display_grid(params, grid, numbers);
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
                if (x < params->w-1) {
                    struct rect r = find_rect(params, grid, x+1, y);
                    if ((r.w * r.h > 2 && (r.y==y || r.y+r.h-1==y)) || r.h==1)
                        dirs[ndirs++] = 1;   /* right */
                }
                if (y > 0) {
                    struct rect r = find_rect(params, grid, x, y-1);
                    if ((r.w * r.h > 2 && (r.x==x || r.x+r.w-1==x)) || r.w==1)
                        dirs[ndirs++] = 2;   /* up */
                }
                if (x > 0) {
                    struct rect r = find_rect(params, grid, x-1, y);
                    if ((r.w * r.h > 2 && (r.y==y || r.y+r.h-1==y)) || r.h==1)
                        dirs[ndirs++] = 4;   /* left */
                }
                if (y < params->h-1) {
                    struct rect r = find_rect(params, grid, x, y+1);
                    if ((r.w * r.h > 2 && (r.x==x || r.x+r.w-1==x)) || r.w==1)
                        dirs[ndirs++] = 8;   /* down */
                }

                if (ndirs > 0) {
                    int which, dir;
                    struct rect r1, r2;

                    which = random_upto(rs, ndirs);
                    dir = dirs[which];

                    switch (dir) {
                      case 1:          /* right */
                        assert(x < params->w+1);
#ifdef GENERATION_DIAGNOSTICS
                        printf("extending right\n");
#endif
                        r1 = find_rect(params, grid, x+1, y);
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
                        r1 = find_rect(params, grid, x, y-1);
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
                        r1 = find_rect(params, grid, x-1, y);
                        r2.x = r1.x;
                        r2.y = y;
                        r2.w = 1 + r1.w;
                        r2.h = 1;
                        if (r1.y == y)
                            r1.y++;
                        r1.h--;
                        break;
                      case 8:          /* down */
                        assert(y < params->h+1);
#ifdef GENERATION_DIAGNOSTICS
                        printf("extending down\n");
#endif
                        r1 = find_rect(params, grid, x, y+1);
                        r2.x = x;
                        r2.y = y;
                        r2.w = 1;
                        r2.h = 1 + r1.h;
                        if (r1.x == x)
                            r1.x++;
                        r1.w--;
                        break;
                    }
                    if (r1.h > 0 && r1.w > 0)
                        place_rect(params, grid, r1);
                    place_rect(params, grid, r2);
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
                        assert(x > 0 && x < params->w-1);
                        assert(y > 0 && y < params->h-1);

                        for (xx = x-1; xx <= x+1; xx++)
                            for (yy = y-1; yy <= y+1; yy++) {
                                struct rect r = find_rect(params,grid,xx,yy);
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
                        place_rect(params, grid, r);
                    }
                }
            }
        }
    }

    /*
     * Place numbers.
     */
    for (x = 0; x < params->w; x++) {
        for (y = 0; y < params->h; y++) {
            int idx = INDEX(params, x, y);
            if (index(params, grid, x, y) == idx) {
                struct rect r = find_rect(params, grid, x, y);
                int n, xx, yy;

                /*
                 * Decide where to put the number.
                 */
                n = random_upto(rs, r.w*r.h);
                yy = n / r.w;
                xx = n % r.w;
                index(params,numbers,x+xx,y+yy) = r.w*r.h;
            }
        }
    }

#ifdef GENERATION_DIAGNOSTICS
    display_grid(params, grid, numbers);
#endif

    seed = snewn(11 * params->w * params->h, char);
    p = seed;
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

    return seed;
}

char *validate_seed(game_params *params, char *seed)
{
    int area = params->w * params->h;
    int squares = 0;

    while (*seed) {
        int n = *seed++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            squares++;
            while (*seed >= '0' && *seed <= '9')
                seed++;
        } else
            return "Invalid character in game specification";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";

    return NULL;
}

game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);
    int x, y, i, area;

    state->w = params->w;
    state->h = params->h;

    area = state->w * state->h;

    state->grid = snewn(area, int);
    state->vedge = snewn(area, unsigned char);
    state->hedge = snewn(area, unsigned char);
    state->completed = FALSE;

    i = 0;
    while (*seed) {
        int n = *seed++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                state->grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
            state->grid[i++] = atoi(seed-1);
            while (*seed >= '0' && *seed <= '9')
                seed++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);

    for (y = 0; y < state->h; y++)
	for (x = 0; x < state->w; x++)
	    vedge(state,x,y) = hedge(state,x,y) = 0;

    return state;
}

game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;

    ret->vedge = snewn(state->w * state->h, unsigned char);
    ret->hedge = snewn(state->w * state->h, unsigned char);
    ret->grid = snewn(state->w * state->h, int);

    ret->completed = state->completed;

    memcpy(ret->grid, state->grid, state->w * state->h * sizeof(int));
    memcpy(ret->vedge, state->vedge, state->w*state->h*sizeof(unsigned char));
    memcpy(ret->hedge, state->hedge, state->w*state->h*sizeof(unsigned char));

    return ret;
}

void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->vedge);
    sfree(state->hedge);
    sfree(state);
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
		int num, area, valid;

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
		valid = TRUE;
		/* Check the horizontal edges. */
		for (xx = x; xx < x+rw; xx++) {
		    for (yy = y; yy <= y+rh; yy++) {
			int e = !HRANGE(state,xx,yy) || hedge(state,xx,yy);
			int ec = (yy == y || yy == y+rh);
			if (e != ec)
			    valid = FALSE;
		    }
		}
		/* Check the vertical edges. */
		for (yy = y; yy < y+rh; yy++) {
		    for (xx = x; xx <= x+rw; xx++) {
			int e = !VRANGE(state,xx,yy) || vedge(state,xx,yy);
			int ec = (xx == x || xx == x+rw);
			if (e != ec)
			    valid = FALSE;
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
				valid = FALSE;   /* two numbers */
			    num = grid(state,xx,yy);
			}
		    }
		}
		if (num != area)
		    valid = FALSE;

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
    int dragged;
};

game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->drag_start_x = -1;
    ui->drag_start_y = -1;
    ui->drag_end_x = -1;
    ui->drag_end_y = -1;
    ui->dragged = FALSE;
    return ui;
}

void free_ui(game_ui *ui)
{
    sfree(ui);
}

void coord_round(float x, float y, int *xr, int *yr)
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
                *yr = 1 + 2 * (int)ys;
            } else {
                /* Horizontal edge: x-coord of square centre,
                 * y-coord of corner. */
                *xr = 1 + 2 * (int)xs;
                *yr = 2 * (int)yv;
            }
        }
    }
}

static void ui_draw_rect(game_state *state, game_ui *ui,
                         unsigned char *hedge, unsigned char *vedge, int c)
{
    int x1, x2, y1, y2, x, y, t;

    x1 = ui->drag_start_x;
    x2 = ui->drag_end_x;
    if (x2 < x1) { t = x1; x1 = x2; x2 = t; }

    y1 = ui->drag_start_y;
    y2 = ui->drag_end_y;
    if (y2 < y1) { t = y1; y1 = y2; y2 = t; }

    x1 = x1 / 2;               /* rounds down */
    x2 = (x2+1) / 2;           /* rounds up */
    y1 = y1 / 2;               /* rounds down */
    y2 = (y2+1) / 2;           /* rounds up */

    /*
     * Draw horizontal edges of rectangles.
     */
    for (x = x1; x < x2; x++)
        for (y = y1; y <= y2; y++)
            if (HRANGE(state,x,y)) {
                int val = index(state,hedge,x,y);
                if (y == y1 || y == y2)
                    val = c;
                else if (c == 1)
                    val = 0;
                index(state,hedge,x,y) = val;
            }

    /*
     * Draw vertical edges of rectangles.
     */
    for (y = y1; y < y2; y++)
        for (x = x1; x <= x2; x++)
            if (VRANGE(state,x,y)) {
                int val = index(state,vedge,x,y);
                if (x == x1 || x == x2)
                    val = c;
                else if (c == 1)
                    val = 0;
                index(state,vedge,x,y) = val;
            }
}

game_state *make_move(game_state *from, game_ui *ui, int x, int y, int button)
{
    int xc, yc;
    int startdrag = FALSE, enddrag = FALSE, active = FALSE;
    game_state *ret;

    if (button == LEFT_BUTTON) {
        startdrag = TRUE;
    } else if (button == LEFT_RELEASE) {
        enddrag = TRUE;
    } else if (button != LEFT_DRAG) {
        return NULL;
    }

    coord_round(FROMCOORD((float)x), FROMCOORD((float)y), &xc, &yc);

    if (startdrag) {
        ui->drag_start_x = xc;
        ui->drag_start_y = yc;
        ui->drag_end_x = xc;
        ui->drag_end_y = yc;
        ui->dragged = FALSE;
        active = TRUE;
    }

    if (xc != ui->drag_end_x || yc != ui->drag_end_y) {
        ui->drag_end_x = xc;
        ui->drag_end_y = yc;
        ui->dragged = TRUE;
        active = TRUE;
    }

    ret = NULL;

    if (enddrag) {
	if (xc >= 0 && xc <= 2*from->w &&
	    yc >= 0 && yc <= 2*from->h) {
	    ret = dup_game(from);

	    if (ui->dragged) {
		ui_draw_rect(ret, ui, ret->hedge, ret->vedge, 1);
	    } else {
		if ((xc & 1) && !(yc & 1) && HRANGE(from,xc/2,yc/2)) {
		    hedge(ret,xc/2,yc/2) = !hedge(ret,xc/2,yc/2);
		}
		if ((yc & 1) && !(xc & 1) && VRANGE(from,xc/2,yc/2)) {
		    vedge(ret,xc/2,yc/2) = !vedge(ret,xc/2,yc/2);
		}
	    }

	    if (!memcmp(ret->hedge, from->hedge, from->w*from->h) &&
		!memcmp(ret->vedge, from->vedge, from->w*from->h)) {
		free_game(ret);
		ret = NULL;
	    }

            /*
             * We've made a real change to the grid. Check to see
             * if the game has been completed.
             */
            if (ret && !ret->completed) {
                int x, y, ok;
                unsigned char *correct = get_correct(ret);

                ok = TRUE;
                for (x = 0; x < ret->w; x++)
                    for (y = 0; y < ret->h; y++)
                        if (!index(ret, correct, x, y))
                            ok = FALSE;

                sfree(correct);

                if (ok)
                    ret->completed = TRUE;
            }
	}

	ui->drag_start_x = -1;
	ui->drag_start_y = -1;
	ui->drag_end_x = -1;
	ui->drag_end_y = -1;
	ui->dragged = FALSE;
	active = TRUE;
    }

    if (ret)
	return ret;		       /* a move has been made */
    else if (active)
        return from;                   /* UI activity has occurred */
    else
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define CORRECT 65536

#define COLOUR(k) ( (k)==1 ? COL_LINE : COL_DRAG )
#define MAX(x,y) ( (x)>(y) ? (x) : (y) )
#define MAX4(x,y,z,w) ( MAX(MAX(x,y),MAX(z,w)) )

struct game_drawstate {
    int started;
    int w, h;
    unsigned int *visible;
};

void game_size(game_params *params, int *x, int *y)
{
    *x = params->w * TILE_SIZE + 2*BORDER + 1;
    *y = params->h * TILE_SIZE + 2*BORDER + 1;
}

float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_GRID * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_GRID * 3 + 2] = 0.5F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_DRAG * 3 + 0] = 1.0F;
    ret[COL_DRAG * 3 + 1] = 0.0F;
    ret[COL_DRAG * 3 + 2] = 0.0F;

    ret[COL_CORRECT * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_CORRECT * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_CORRECT * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_LINE * 3 + 0] = 0.0F;
    ret[COL_LINE * 3 + 1] = 0.0F;
    ret[COL_LINE * 3 + 2] = 0.0F;

    ret[COL_TEXT * 3 + 0] = 0.0F;
    ret[COL_TEXT * 3 + 1] = 0.0F;
    ret[COL_TEXT * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->visible = snewn(ds->w * ds->h, unsigned int);
    for (i = 0; i < ds->w * ds->h; i++)
        ds->visible[i] = 0xFFFF;

    return ds;
}

void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

void draw_tile(frontend *fe, game_state *state, int x, int y,
               unsigned char *hedge, unsigned char *vedge,
	       unsigned char *corners, int correct)
{
    int cx = COORD(x), cy = COORD(y);
    char str[80];

    draw_rect(fe, cx, cy, TILE_SIZE+1, TILE_SIZE+1, COL_GRID);
    draw_rect(fe, cx+1, cy+1, TILE_SIZE-1, TILE_SIZE-1,
	      correct ? COL_CORRECT : COL_BACKGROUND);

    if (grid(state,x,y)) {
	sprintf(str, "%d", grid(state,x,y));
	draw_text(fe, cx+TILE_SIZE/2, cy+TILE_SIZE/2, FONT_VARIABLE,
		  TILE_SIZE/2, ALIGN_HCENTRE | ALIGN_VCENTRE, COL_TEXT, str);
    }

    /*
     * Draw edges.
     */
    if (!HRANGE(state,x,y) || index(state,hedge,x,y))
	draw_rect(fe, cx, cy, TILE_SIZE+1, 2,
                  HRANGE(state,x,y) ? COLOUR(index(state,hedge,x,y)) :
                  COL_LINE);
    if (!HRANGE(state,x,y+1) || index(state,hedge,x,y+1))
	draw_rect(fe, cx, cy+TILE_SIZE-1, TILE_SIZE+1, 2,
                  HRANGE(state,x,y+1) ? COLOUR(index(state,hedge,x,y+1)) :
                  COL_LINE);
    if (!VRANGE(state,x,y) || index(state,vedge,x,y))
	draw_rect(fe, cx, cy, 2, TILE_SIZE+1,
                  VRANGE(state,x,y) ? COLOUR(index(state,vedge,x,y)) :
                  COL_LINE);
    if (!VRANGE(state,x+1,y) || index(state,vedge,x+1,y))
	draw_rect(fe, cx+TILE_SIZE-1, cy, 2, TILE_SIZE+1,
                  VRANGE(state,x+1,y) ? COLOUR(index(state,vedge,x+1,y)) :
                  COL_LINE);

    /*
     * Draw corners.
     */
    if (index(state,corners,x,y))
	draw_rect(fe, cx, cy, 2, 2,
                  COLOUR(index(state,corners,x,y)));
    if (x+1 < state->w && index(state,corners,x+1,y))
	draw_rect(fe, cx+TILE_SIZE-1, cy, 2, 2,
                  COLOUR(index(state,corners,x+1,y)));
    if (y+1 < state->h && index(state,corners,x,y+1))
	draw_rect(fe, cx, cy+TILE_SIZE-1, 2, 2,
                  COLOUR(index(state,corners,x,y+1)));
    if (x+1 < state->w && y+1 < state->h && index(state,corners,x+1,y+1))
	draw_rect(fe, cx+TILE_SIZE-1, cy+TILE_SIZE-1, 2, 2,
                  COLOUR(index(state,corners,x+1,y+1)));

    draw_update(fe, cx, cy, TILE_SIZE+1, TILE_SIZE+1);
}

void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, game_ui *ui,
                 float animtime, float flashtime)
{
    int x, y;
    unsigned char *correct;
    unsigned char *hedge, *vedge, *corners;

    correct = get_correct(state);

    if (ui->dragged) {
        hedge = snewn(state->w*state->h, unsigned char);
        vedge = snewn(state->w*state->h, unsigned char);
        memcpy(hedge, state->hedge, state->w*state->h);
        memcpy(vedge, state->vedge, state->w*state->h);
        ui_draw_rect(state, ui, hedge, vedge, 2);
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
	draw_rect(fe, 0, 0,
		  state->w * TILE_SIZE + 2*BORDER + 1,
		  state->h * TILE_SIZE + 2*BORDER + 1, COL_BACKGROUND);
	draw_rect(fe, COORD(0)-1, COORD(0)-1,
		  ds->w*TILE_SIZE+3, ds->h*TILE_SIZE+3, COL_LINE);
	ds->started = TRUE;
	draw_update(fe, 0, 0,
		    state->w * TILE_SIZE + 2*BORDER + 1,
		    state->h * TILE_SIZE + 2*BORDER + 1);
    }

    for (x = 0; x < state->w; x++)
	for (y = 0; y < state->h; y++) {
	    unsigned int c = 0;

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
		c |= index(state,corners,x+1,y+1) << 14;
	    if (index(state, correct, x, y) && !flashtime)
		c |= CORRECT;

	    if (index(ds,ds->visible,x,y) != c) {
		draw_tile(fe, state, x, y, hedge, vedge, corners, c & CORRECT);
		index(ds,ds->visible,x,y) = c;
	    }
	}

    if (hedge != state->hedge) {
        sfree(hedge);
        sfree(vedge);
   }

    sfree(correct);
}

float game_anim_length(game_state *oldstate, game_state *newstate)
{
    return 0.0F;
}

float game_flash_length(game_state *oldstate, game_state *newstate)
{
    if (!oldstate->completed && newstate->completed)
        return FLASH_TIME;
    return 0.0F;
}

int game_wants_statusbar(void)
{
    return FALSE;
}
