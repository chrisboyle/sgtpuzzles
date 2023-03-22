/*
 * netslide.c: cross between Net and Sixteen, courtesy of Richard
 * Boulton.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

#define MATMUL(xr,yr,m,x,y) do { \
    float rx, ry, xx = (x), yy = (y), *mat = (m); \
    rx = mat[0] * xx + mat[2] * yy; \
    ry = mat[1] * xx + mat[3] * yy; \
    (xr) = rx; (yr) = ry; \
} while (0)

/* Direction and other bitfields */
#define R 0x01
#define U 0x02
#define L 0x04
#define D 0x08
#define FLASHING 0x10
#define ACTIVE 0x20
/* Corner flags go in the barriers array */
#define RU 0x10
#define UL 0x20
#define LD 0x40
#define DR 0x80

/* Get tile at given coordinate */
#define T(state, x, y) ( (y) * (state)->width + (x) )

/* Rotations: Anticlockwise, Clockwise, Flip, general rotate */
#define A(x) ( (((x) & 0x07) << 1) | (((x) & 0x08) >> 3) )
#define C(x) ( (((x) & 0x0E) >> 1) | (((x) & 0x01) << 3) )
#define F(x) ( (((x) & 0x0C) >> 2) | (((x) & 0x03) << 2) )
#define ROT(x, n) ( ((n)&3) == 0 ? (x) : \
		    ((n)&3) == 1 ? A(x) : \
		    ((n)&3) == 2 ? F(x) : C(x) )

/* X and Y displacements */
#define X(x) ( (x) == R ? +1 : (x) == L ? -1 : 0 )
#define Y(x) ( (x) == D ? +1 : (x) == U ? -1 : 0 )

/* Bit count */
#define COUNT(x) ( (((x) & 0x08) >> 3) + (((x) & 0x04) >> 2) + \
		   (((x) & 0x02) >> 1) + ((x) & 0x01) )

#define PREFERRED_TILE_SIZE 48
#define TILE_SIZE (ds->tilesize)
#define BORDER TILE_SIZE
#define TILE_BORDER 1
#define WINDOW_OFFSET 0

#define ANIM_TIME 0.13F
#define FLASH_FRAME 0.07F

enum {
    COL_BACKGROUND,
    COL_FLASHING,
    COL_BORDER,
    COL_WIRE,
    COL_ENDPOINT,
    COL_POWERED,
    COL_BARRIER,
    COL_LOWLIGHT,
    COL_TEXT,
    NCOLOURS
};

struct game_params {
    int width;
    int height;
    bool wrapping;
    float barrier_probability;
    int movetarget;
};

struct game_state {
    int width, height, cx, cy, completed;
    bool wrapping, used_solve;
    int move_count, movetarget;

    /* position (row or col number, starting at 0) of last move. */
    int last_move_row, last_move_col;

    /* direction of last move: +1 or -1 */
    int last_move_dir;

    unsigned char *tiles;
    unsigned char *barriers;
};

#define OFFSET(x2,y2,x1,y1,dir,state) \
    ( (x2) = ((x1) + (state)->width + X((dir))) % (state)->width, \
      (y2) = ((y1) + (state)->height + Y((dir))) % (state)->height)

#define index(state, a, x, y) ( a[(y) * (state)->width + (x)] )
#define tile(state, x, y)     index(state, (state)->tiles, x, y)
#define barrier(state, x, y)  index(state, (state)->barriers, x, y)

struct xyd {
    int x, y, direction;
};

static int xyd_cmp(void *av, void *bv) {
    struct xyd *a = (struct xyd *)av;
    struct xyd *b = (struct xyd *)bv;
    if (a->x < b->x)
	return -1;
    if (a->x > b->x)
	return +1;
    if (a->y < b->y)
	return -1;
    if (a->y > b->y)
	return +1;
    if (a->direction < b->direction)
	return -1;
    if (a->direction > b->direction)
	return +1;
    return 0;
}

static struct xyd *new_xyd(int x, int y, int direction)
{
    struct xyd *xyd = snew(struct xyd);
    xyd->x = x;
    xyd->y = y;
    xyd->direction = direction;
    return xyd;
}

static void slide_col(game_state *state, int dir, int col);
static void slide_col_int(int w, int h, unsigned char *tiles, int dir, int col);
static void slide_row(game_state *state, int dir, int row);
static void slide_row_int(int w, int h, unsigned char *tiles, int dir, int row);

/* ----------------------------------------------------------------------
 * Manage game parameters.
 */
static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->width = 3;
    ret->height = 3;
    ret->wrapping = false;
    ret->barrier_probability = 1.0;
    ret->movetarget = 0;

    return ret;
}

static const struct { int x, y, wrap, bprob; const char* desc; }
netslide_presets[] = {
    {3, 3, false, 1, " easy"},
    {3, 3, false, 0, " medium"},
    {3, 3, true,  0, " hard"},
    {4, 4, false, 1, " easy"},
    {4, 4, false, 0, " medium"},
    {4, 4, true,  0, " hard"},
    {5, 5, false, 1, " easy"},
    {5, 5, false, 0, " medium"},
    {5, 5, true,  0, " hard"},
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(netslide_presets))
        return false;

    ret = snew(game_params);
    ret->width = netslide_presets[i].x;
    ret->height = netslide_presets[i].y;
    ret->wrapping = netslide_presets[i].wrap;
    ret->barrier_probability = (float)netslide_presets[i].bprob;
    ret->movetarget = 0;

    sprintf(str, "%dx%d%s", ret->width, ret->height, netslide_presets[i].desc);

    *name = dupstr(str);
    *params = ret;
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

    ret->wrapping = false;
    ret->barrier_probability = 0.0;
    ret->movetarget = 0;

    ret->width = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        ret->height = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
        ret->wrapping = (*p == 'w');
        if (ret->wrapping)
            p++;
        if (*p == 'b') {
            ret->barrier_probability = (float)atof(++p);
            while (*p && (isdigit((unsigned char)*p) || *p == '.')) p++;
        }
        if (*p == 'm') {
            ret->movetarget = atoi(++p);
        }
    } else {
        ret->height = ret->width;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[400];
    int len;

    len = sprintf(ret, "%dx%d", params->width, params->height);
    if (params->wrapping)
        ret[len++] = 'w';
    if (full && params->barrier_probability)
        len += sprintf(ret+len, "b%g", params->barrier_probability);
    /* Shuffle limit is part of the limited parameters, because we have to
     * provide the target move count. */
    if (params->movetarget)
        len += sprintf(ret+len, "m%d", params->movetarget);
    assert(len < lenof(ret));
    ret[len] = '\0';

    return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->width);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->height);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Walls wrap around";
    ret[2].type = C_BOOLEAN;
    ret[2].u.boolean.bval = params->wrapping;

    ret[3].name = "Barrier probability";
    ret[3].type = C_STRING;
    sprintf(buf, "%g", params->barrier_probability);
    ret[3].u.string.sval = dupstr(buf);

    ret[4].name = "Number of shuffling moves";
    ret[4].type = C_STRING;
    sprintf(buf, "%d", params->movetarget);
    ret[4].u.string.sval = dupstr(buf);

    ret[5].name = NULL;
    ret[5].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->width = atoi(cfg[0].u.string.sval);
    ret->height = atoi(cfg[1].u.string.sval);
    ret->wrapping = cfg[2].u.boolean.bval;
    ret->barrier_probability = (float)atof(cfg[3].u.string.sval);
    ret->movetarget = atoi(cfg[4].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->width <= 1 || params->height <= 1)
	return "Width and height must both be greater than one";
    if (params->width > INT_MAX / params->height)
        return "Width times height must not be unreasonably large";
    if (params->barrier_probability < 0)
	return "Barrier probability may not be negative";
    if (params->barrier_probability > 1)
	return "Barrier probability may not be greater than 1";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Randomly select a new game description.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    tree234 *possibilities, *barriertree;
    int w, h, x, y, cx, cy, nbarriers;
    unsigned char *tiles, *barriers;
    char *desc, *p;

    w = params->width;
    h = params->height;

    tiles = snewn(w * h, unsigned char);
    memset(tiles, 0, w * h);
    barriers = snewn(w * h, unsigned char);
    memset(barriers, 0, w * h);

    cx = w / 2;
    cy = h / 2;

    /*
     * Construct the unshuffled grid.
     * 
     * To do this, we simply start at the centre point, repeatedly
     * choose a random possibility out of the available ways to
     * extend a used square into an unused one, and do it. After
     * extending the third line out of a square, we remove the
     * fourth from the possibilities list to avoid any full-cross
     * squares (which would make the game too easy because they
     * only have one orientation).
     * 
     * The slightly worrying thing is the avoidance of full-cross
     * squares. Can this cause our unsophisticated construction
     * algorithm to paint itself into a corner, by getting into a
     * situation where there are some unreached squares and the
     * only way to reach any of them is to extend a T-piece into a
     * full cross?
     * 
     * Answer: no it can't, and here's a proof.
     * 
     * Any contiguous group of such unreachable squares must be
     * surrounded on _all_ sides by T-pieces pointing away from the
     * group. (If not, then there is a square which can be extended
     * into one of the `unreachable' ones, and so it wasn't
     * unreachable after all.) In particular, this implies that
     * each contiguous group of unreachable squares must be
     * rectangular in shape (any deviation from that yields a
     * non-T-piece next to an `unreachable' square).
     * 
     * So we have a rectangle of unreachable squares, with T-pieces
     * forming a solid border around the rectangle. The corners of
     * that border must be connected (since every tile connects all
     * the lines arriving in it), and therefore the border must
     * form a closed loop around the rectangle.
     * 
     * But this can't have happened in the first place, since we
     * _know_ we've avoided creating closed loops! Hence, no such
     * situation can ever arise, and the naive grid construction
     * algorithm will guaranteeably result in a complete grid
     * containing no unreached squares, no full crosses _and_ no
     * closed loops. []
     */
    possibilities = newtree234(xyd_cmp);

    if (cx+1 < w)
	add234(possibilities, new_xyd(cx, cy, R));
    if (cy-1 >= 0)
	add234(possibilities, new_xyd(cx, cy, U));
    if (cx-1 >= 0)
	add234(possibilities, new_xyd(cx, cy, L));
    if (cy+1 < h)
	add234(possibilities, new_xyd(cx, cy, D));

    while (count234(possibilities) > 0) {
	int i;
	struct xyd *xyd;
	int x1, y1, d1, x2, y2, d2, d;

	/*
	 * Extract a randomly chosen possibility from the list.
	 */
	i = random_upto(rs, count234(possibilities));
	xyd = delpos234(possibilities, i);
	x1 = xyd->x;
	y1 = xyd->y;
	d1 = xyd->direction;
	sfree(xyd);

	OFFSET(x2, y2, x1, y1, d1, params);
	d2 = F(d1);
#ifdef GENERATION_DIAGNOSTICS
	printf("picked (%d,%d,%c) <-> (%d,%d,%c)\n",
	       x1, y1, "0RU3L567D9abcdef"[d1], x2, y2, "0RU3L567D9abcdef"[d2]);
#endif

	/*
	 * Make the connection. (We should be moving to an as yet
	 * unused tile.)
	 */
	index(params, tiles, x1, y1) |= d1;
	assert(index(params, tiles, x2, y2) == 0);
	index(params, tiles, x2, y2) |= d2;

	/*
	 * If we have created a T-piece, remove its last
	 * possibility.
	 */
	if (COUNT(index(params, tiles, x1, y1)) == 3) {
	    struct xyd xyd1, *xydp;

	    xyd1.x = x1;
	    xyd1.y = y1;
	    xyd1.direction = 0x0F ^ index(params, tiles, x1, y1);

	    xydp = find234(possibilities, &xyd1, NULL);

	    if (xydp) {
#ifdef GENERATION_DIAGNOSTICS
		printf("T-piece; removing (%d,%d,%c)\n",
		       xydp->x, xydp->y, "0RU3L567D9abcdef"[xydp->direction]);
#endif
		del234(possibilities, xydp);
		sfree(xydp);
	    }
	}

	/*
	 * Remove all other possibilities that were pointing at the
	 * tile we've just moved into.
	 */
	for (d = 1; d < 0x10; d <<= 1) {
	    int x3, y3, d3;
	    struct xyd xyd1, *xydp;

	    OFFSET(x3, y3, x2, y2, d, params);
	    d3 = F(d);

	    xyd1.x = x3;
	    xyd1.y = y3;
	    xyd1.direction = d3;

	    xydp = find234(possibilities, &xyd1, NULL);

	    if (xydp) {
#ifdef GENERATION_DIAGNOSTICS
		printf("Loop avoidance; removing (%d,%d,%c)\n",
		       xydp->x, xydp->y, "0RU3L567D9abcdef"[xydp->direction]);
#endif
		del234(possibilities, xydp);
		sfree(xydp);
	    }
	}

	/*
	 * Add new possibilities to the list for moving _out_ of
	 * the tile we have just moved into.
	 */
	for (d = 1; d < 0x10; d <<= 1) {
	    int x3, y3;

	    if (d == d2)
		continue;	       /* we've got this one already */

	    if (!params->wrapping) {
		if (d == U && y2 == 0)
		    continue;
		if (d == D && y2 == h-1)
		    continue;
		if (d == L && x2 == 0)
		    continue;
		if (d == R && x2 == w-1)
		    continue;
	    }

	    OFFSET(x3, y3, x2, y2, d, params);

	    if (index(params, tiles, x3, y3))
		continue;	       /* this would create a loop */

#ifdef GENERATION_DIAGNOSTICS
	    printf("New frontier; adding (%d,%d,%c)\n",
		   x2, y2, "0RU3L567D9abcdef"[d]);
#endif
	    add234(possibilities, new_xyd(x2, y2, d));
	}
    }
    /* Having done that, we should have no possibilities remaining. */
    assert(count234(possibilities) == 0);
    freetree234(possibilities);

    /*
     * Now compute a list of the possible barrier locations.
     */
    barriertree = newtree234(xyd_cmp);
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {

	    if (!(index(params, tiles, x, y) & R) &&
                (params->wrapping || x < w-1))
		add234(barriertree, new_xyd(x, y, R));
	    if (!(index(params, tiles, x, y) & D) &&
                (params->wrapping || y < h-1))
		add234(barriertree, new_xyd(x, y, D));
	}
    }

    /*
     * Save the unshuffled grid in aux.
     */
    {
	char *solution;
        int i;

        /*
         * String format is exactly the same as a solve move, so we
         * can just dupstr this in solve_game().
         */

	solution = snewn(w * h + 2, char);
        solution[0] = 'S';
        for (i = 0; i < w * h; i++)
            solution[i+1] = "0123456789abcdef"[tiles[i] & 0xF];
        solution[w*h+1] = '\0';

	*aux = solution;
    }

    /*
     * Now shuffle the grid.
     * FIXME - this simply does a set of random moves to shuffle the pieces,
     * although we make a token effort to avoid boring cases by avoiding moves
     * that directly undo the previous one, or that repeat so often as to
     * turn into fewer moves.
     *
     * A better way would be to number all the pieces, generate a placement
     * for all the numbers as for "sixteen", observing parity constraints if
     * neccessary, and then place the pieces according to their numbering.
     * BUT - I'm not sure if this will work, since we disallow movement of
     * the middle row and column.
     */
    {
        int i;
        int cols = w - 1;
        int rows = h - 1;
        int moves = params->movetarget;
        int prevdir = -1, prevrowcol = -1, nrepeats = 0;
        if (!moves) moves = cols * rows * 2;
        for (i = 0; i < moves; /* incremented conditionally */) {
            /* Choose a direction: 0,1,2,3 = up, right, down, left. */
            int dir = random_upto(rs, 4);
            int rowcol;
            if (dir % 2 == 0) {
                int col = random_upto(rs, cols);
                if (col >= cx) col += 1;    /* avoid centre */
                if (col == prevrowcol) {
                    if (dir == 2-prevdir)
                        continue;   /* undoes last move */
                    else if (dir == prevdir && (nrepeats+1)*2 > h)
                        continue;   /* makes fewer moves */
                }
                slide_col_int(w, h, tiles, 1 - dir, col);
                rowcol = col;
            } else {
                int row = random_upto(rs, rows);
                if (row >= cy) row += 1;    /* avoid centre */
                if (row == prevrowcol) {
                    if (dir == 4-prevdir)
                        continue;   /* undoes last move */
                    else if (dir == prevdir && (nrepeats+1)*2 > w)
                        continue;   /* makes fewer moves */
                }
                slide_row_int(w, h, tiles, 2 - dir, row);
                rowcol = row;
            }
            if (dir == prevdir && rowcol == prevrowcol)
                nrepeats++;
            else
                nrepeats = 1;
            prevdir = dir;
            prevrowcol = rowcol;
            i++;    /* if we got here, the move was accepted */
        }
    }

    /*
     * And now choose barrier locations. (We carefully do this
     * _after_ shuffling, so that changing the barrier rate in the
     * params while keeping the random seed the same will give the
     * same shuffled grid and _only_ change the barrier locations.
     * Also the way we choose barrier locations, by repeatedly
     * choosing one possibility from the list until we have enough,
     * is designed to ensure that raising the barrier rate while
     * keeping the seed the same will provide a superset of the
     * previous barrier set - i.e. if you ask for 10 barriers, and
     * then decide that's still too hard and ask for 20, you'll get
     * the original 10 plus 10 more, rather than getting 20 new
     * ones and the chance of remembering your first 10.)
     */
    nbarriers = (int)(params->barrier_probability * count234(barriertree));
    assert(nbarriers >= 0 && nbarriers <= count234(barriertree));

    while (nbarriers > 0) {
	int i;
	struct xyd *xyd;
	int x1, y1, d1, x2, y2, d2;

	/*
	 * Extract a randomly chosen barrier from the list.
	 */
	i = random_upto(rs, count234(barriertree));
	xyd = delpos234(barriertree, i);

	assert(xyd != NULL);

	x1 = xyd->x;
	y1 = xyd->y;
	d1 = xyd->direction;
	sfree(xyd);

	OFFSET(x2, y2, x1, y1, d1, params);
	d2 = F(d1);

	index(params, barriers, x1, y1) |= d1;
	index(params, barriers, x2, y2) |= d2;

	nbarriers--;
    }

    /*
     * Clean up the rest of the barrier list.
     */
    {
	struct xyd *xyd;

	while ( (xyd = delpos234(barriertree, 0)) != NULL)
	    sfree(xyd);

	freetree234(barriertree);
    }

    /*
     * Finally, encode the grid into a string game description.
     * 
     * My syntax is extremely simple: each square is encoded as a
     * hex digit in which bit 0 means a connection on the right,
     * bit 1 means up, bit 2 left and bit 3 down. (i.e. the same
     * encoding as used internally). Each digit is followed by
     * optional barrier indicators: `v' means a vertical barrier to
     * the right of it, and `h' means a horizontal barrier below
     * it.
     */
    desc = snewn(w * h * 3 + 1, char);
    p = desc;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            *p++ = "0123456789abcdef"[index(params, tiles, x, y)];
            if ((params->wrapping || x < w-1) &&
                (index(params, barriers, x, y) & R))
                *p++ = 'v';
            if ((params->wrapping || y < h-1) &&
                (index(params, barriers, x, y) & D))
                *p++ = 'h';
        }
    }
    assert(p - desc <= w*h*3);
    *p = '\0';

    sfree(tiles);
    sfree(barriers);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->width, h = params->height;
    int i;

    for (i = 0; i < w*h; i++) {
        if (*desc >= '0' && *desc <= '9')
            /* OK */;
        else if (*desc >= 'a' && *desc <= 'f')
            /* OK */;
        else if (*desc >= 'A' && *desc <= 'F')
            /* OK */;
        else if (!*desc)
            return "Game description shorter than expected";
        else
            return "Game description contained unexpected character";
        desc++;
        while (*desc == 'h' || *desc == 'v')
            desc++;
    }
    if (*desc)
        return "Game description longer than expected";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Construct an initial game state, given a description and parameters.
 */

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state;
    int w, h, x, y;

    assert(params->width > 0 && params->height > 0);
    assert(params->width > 1 || params->height > 1);

    /*
     * Create a blank game state.
     */
    state = snew(game_state);
    w = state->width = params->width;
    h = state->height = params->height;
    state->cx = state->width / 2;
    state->cy = state->height / 2;
    state->wrapping = params->wrapping;
    state->movetarget = params->movetarget;
    state->completed = 0;
    state->used_solve = false;
    state->move_count = 0;
    state->last_move_row = -1;
    state->last_move_col = -1;
    state->last_move_dir = 0;
    state->tiles = snewn(state->width * state->height, unsigned char);
    memset(state->tiles, 0, state->width * state->height);
    state->barriers = snewn(state->width * state->height, unsigned char);
    memset(state->barriers, 0, state->width * state->height);


    /*
     * Parse the game description into the grid.
     */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (*desc >= '0' && *desc <= '9')
                tile(state, x, y) = *desc - '0';
            else if (*desc >= 'a' && *desc <= 'f')
                tile(state, x, y) = *desc - 'a' + 10;
            else if (*desc >= 'A' && *desc <= 'F')
                tile(state, x, y) = *desc - 'A' + 10;
            if (*desc)
                desc++;
            while (*desc == 'h' || *desc == 'v') {
                int x2, y2, d1, d2;
                if (*desc == 'v')
                    d1 = R;
                else
                    d1 = D;

                OFFSET(x2, y2, x, y, d1, state);
                d2 = F(d1);

                barrier(state, x, y) |= d1;
                barrier(state, x2, y2) |= d2;

                desc++;
            }
        }
    }

    /*
     * Set up border barriers if this is a non-wrapping game.
     */
    if (!state->wrapping) {
	for (x = 0; x < state->width; x++) {
	    barrier(state, x, 0) |= U;
	    barrier(state, x, state->height-1) |= D;
	}
	for (y = 0; y < state->height; y++) {
	    barrier(state, 0, y) |= L;
	    barrier(state, state->width-1, y) |= R;
	}
    }

    /*
     * Set up the barrier corner flags, for drawing barriers
     * prettily when they meet.
     */
    for (y = 0; y < state->height; y++) {
	for (x = 0; x < state->width; x++) {
            int dir;

            for (dir = 1; dir < 0x10; dir <<= 1) {
                int dir2 = A(dir);
                int x1, y1, x2, y2, x3, y3;
                bool corner = false;

                if (!(barrier(state, x, y) & dir))
                    continue;

                if (barrier(state, x, y) & dir2)
                    corner = true;

                x1 = x + X(dir), y1 = y + Y(dir);
                if (x1 >= 0 && x1 < state->width &&
                    y1 >= 0 && y1 < state->height &&
                    (barrier(state, x1, y1) & dir2))
                    corner = true;

                x2 = x + X(dir2), y2 = y + Y(dir2);
                if (x2 >= 0 && x2 < state->width &&
                    y2 >= 0 && y2 < state->height &&
                    (barrier(state, x2, y2) & dir))
                    corner = true;

                if (corner) {
                    barrier(state, x, y) |= (dir << 4);
                    if (x1 >= 0 && x1 < state->width &&
                        y1 >= 0 && y1 < state->height)
                        barrier(state, x1, y1) |= (A(dir) << 4);
                    if (x2 >= 0 && x2 < state->width &&
                        y2 >= 0 && y2 < state->height)
                        barrier(state, x2, y2) |= (C(dir) << 4);
                    x3 = x + X(dir) + X(dir2), y3 = y + Y(dir) + Y(dir2);
                    if (x3 >= 0 && x3 < state->width &&
                        y3 >= 0 && y3 < state->height)
                        barrier(state, x3, y3) |= (F(dir) << 4);
                }
            }
	}
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret;

    ret = snew(game_state);
    ret->width = state->width;
    ret->height = state->height;
    ret->cx = state->cx;
    ret->cy = state->cy;
    ret->wrapping = state->wrapping;
    ret->movetarget = state->movetarget;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ret->move_count = state->move_count;
    ret->last_move_row = state->last_move_row;
    ret->last_move_col = state->last_move_col;
    ret->last_move_dir = state->last_move_dir;
    ret->tiles = snewn(state->width * state->height, unsigned char);
    memcpy(ret->tiles, state->tiles, state->width * state->height);
    ret->barriers = snewn(state->width * state->height, unsigned char);
    memcpy(ret->barriers, state->barriers, state->width * state->height);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state->barriers);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    if (!aux) {
	*error = "Solution not known for this puzzle";
	return NULL;
    }

    return dupstr(aux);
}

/* ----------------------------------------------------------------------
 * Utility routine.
 */

/*
 * Compute which squares are reachable from the centre square, as a
 * quick visual aid to determining how close the game is to
 * completion. This is also a simple way to tell if the game _is_
 * completed - just call this function and see whether every square
 * is marked active.
 *
 * squares in the moving_row and moving_col are always inactive - this
 * is so that "current" doesn't appear to jump across moving lines.
 */
static unsigned char *compute_active(const game_state *state,
                                     int moving_row, int moving_col)
{
    unsigned char *active;
    tree234 *todo;
    struct xyd *xyd;

    active = snewn(state->width * state->height, unsigned char);
    memset(active, 0, state->width * state->height);

    /*
     * We only store (x,y) pairs in todo, but it's easier to reuse
     * xyd_cmp and just store direction 0 every time.
     */
    todo = newtree234(xyd_cmp);
    index(state, active, state->cx, state->cy) = ACTIVE;
    add234(todo, new_xyd(state->cx, state->cy, 0));

    while ( (xyd = delpos234(todo, 0)) != NULL) {
	int x1, y1, d1, x2, y2, d2;

	x1 = xyd->x;
	y1 = xyd->y;
	sfree(xyd);

	for (d1 = 1; d1 < 0x10; d1 <<= 1) {
	    OFFSET(x2, y2, x1, y1, d1, state);
	    d2 = F(d1);

	    /*
	     * If the next tile in this direction is connected to
	     * us, and there isn't a barrier in the way, and it
	     * isn't already marked active, then mark it active and
	     * add it to the to-examine list.
	     */
	    if ((x2 != moving_col && y2 != moving_row) &&
                (tile(state, x1, y1) & d1) &&
		(tile(state, x2, y2) & d2) &&
		!(barrier(state, x1, y1) & d1) &&
		!index(state, active, x2, y2)) {
		index(state, active, x2, y2) = ACTIVE;
		add234(todo, new_xyd(x2, y2, 0));
	    }
	}
    }
    /* Now we expect the todo list to have shrunk to zero size. */
    assert(count234(todo) == 0);
    freetree234(todo);

    return active;
}

struct game_ui {
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = 0;
    ui->cur_y = -1;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);

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

/* ----------------------------------------------------------------------
 * Process a move.
 */

static void slide_row_int(int w, int h, unsigned char *tiles, int dir, int row)
{
    int x = dir > 0 ? -1 : w;
    int tx = x + dir;
    int n = w - 1;
    unsigned char endtile;
    assert(0 <= tx && tx < w);
    endtile = tiles[row * w + tx];
    do {
        x = tx;
        tx = (x + dir + w) % w;
        tiles[row * w + x] = tiles[row * w + tx];
    } while (--n > 0);
    tiles[row * w + tx] = endtile;
}

static void slide_col_int(int w, int h, unsigned char *tiles, int dir, int col)
{
    int y = dir > 0 ? -1 : h;
    int ty = y + dir;
    int n = h - 1;
    unsigned char endtile;
    assert(0 <= ty && ty < h);
    endtile = tiles[ty * w + col];
    do {
        y = ty;
        ty = (y + dir + h) % h;
        tiles[y * w + col] = tiles[ty * w + col];
    } while (--n > 0);
    tiles[ty * w + col] = endtile;
}

static void slide_row(game_state *state, int dir, int row)
{
    slide_row_int(state->width, state->height, state->tiles, dir, row);
}

static void slide_col(game_state *state, int dir, int col)
{
    slide_col_int(state->width, state->height, state->tiles, dir, col);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    bool started;
    int width, height;
    int tilesize;
    unsigned char *visible;
    int cur_x, cur_y;
};

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cur_visible)
        return "Slide";
    return "";
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int cx, cy;
    int dx, dy;
    char buf[80];

    button &= ~MOD_MASK;

    if (IS_CURSOR_MOVE(button)) {
        int cpos, diff = 0;
        cpos = c2pos(state->width, state->height, ui->cur_x, ui->cur_y);
        diff = c2diff(state->width, state->height, ui->cur_x, ui->cur_y, button);

        if (diff != 0) {
            do { /* we might have to do this more than once to skip missing arrows */
                cpos += diff;
                pos2c(state->width, state->height, cpos, &ui->cur_x, &ui->cur_y);
            } while (ui->cur_x == state->cx || ui->cur_y == state->cy);
        }

        ui->cur_visible = true;
        return UI_UPDATE;
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        cx = (x - (BORDER + WINDOW_OFFSET + TILE_BORDER) + 2*TILE_SIZE) / TILE_SIZE - 2;
        cy = (y - (BORDER + WINDOW_OFFSET + TILE_BORDER) + 2*TILE_SIZE) / TILE_SIZE - 2;
        ui->cur_visible = false;
    } else if (IS_CURSOR_SELECT(button)) {
        if (ui->cur_visible) {
            cx = ui->cur_x;
            cy = ui->cur_y;
        } else {
            /* 'click' when cursor is invisible just makes cursor visible. */
            ui->cur_visible = true;
            return UI_UPDATE;
        }
    } else
        return NULL;

    if (cy >= 0 && cy < state->height && cy != state->cy)
    {
        if (cx == -1) dx = +1;
        else if (cx == state->width) dx = -1;
        else return NULL;
        dy = 0;
    }
    else if (cx >= 0 && cx < state->width && cx != state->cx)
    {
        if (cy == -1) dy = +1;
        else if (cy == state->height) dy = -1;
        else return NULL;
        dx = 0;
    }
    else
        return NULL;

    /* reverse direction if right hand button is pressed */
    if (button == RIGHT_BUTTON)
    {
        dx = -dx;
        dy = -dy;
    }

    if (dx == 0)
	sprintf(buf, "C%d,%d", cx, dy);
    else
	sprintf(buf, "R%d,%d", cy, dx);
    return dupstr(buf);
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    int c, d;
    bool col;

    if ((move[0] == 'C' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d", &c, &d) == 2 &&
	c >= 0 && c < (move[0] == 'C' ? from->width : from->height) &&
        d <= (move[0] == 'C' ? from->height : from->width) &&
        d >= -(move[0] == 'C' ? from->height : from->width) && d != 0) {
	col = (move[0] == 'C');
    } else if (move[0] == 'S' &&
	       strlen(move) == from->width * from->height + 1) {
	int i;
	ret = dup_game(from);
	ret->used_solve = true;
	ret->completed = ret->move_count = 1;

	for (i = 0; i < from->width * from->height; i++) {
	    c = move[i+1];
	    if (c >= '0' && c <= '9')
		c -= '0';
	    else if (c >= 'A' && c <= 'F')
		c -= 'A' - 10;
	    else if (c >= 'a' && c <= 'f')
		c -= 'a' - 10;
	    else {
		free_game(ret);
		return NULL;
	    }
	    ret->tiles[i] = c;
	}
	return ret;
    } else
	return NULL;		       /* can't parse move string */

    ret = dup_game(from);

    if (col)
	slide_col(ret, d, c);
    else
	slide_row(ret, d, c);

    ret->move_count++;
    ret->last_move_row = col ? -1 : c;
    ret->last_move_col = col ? c : -1;
    ret->last_move_dir = d;

    /*
     * See if the game has been completed.
     */
    if (!ret->completed) {
	unsigned char *active = compute_active(ret, -1, -1);
	int x1, y1;
        bool complete = true;

	for (x1 = 0; x1 < ret->width; x1++)
	    for (y1 = 0; y1 < ret->height; y1++)
		if (!index(ret, active, x1, y1)) {
		    complete = false;
		    goto break_label;  /* break out of two loops at once */
		}
	break_label:

	sfree(active);

	if (complete)
	    ret->completed = ret->move_count;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Routines for drawing the game position on the screen.
 */

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);

    ds->started = false;
    ds->width = state->width;
    ds->height = state->height;
    ds->visible = snewn(state->width * state->height, unsigned char);
    ds->tilesize = 0;                  /* not decided yet */
    memset(ds->visible, 0xFF, state->width * state->height);
    ds->cur_x = ds->cur_y = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = BORDER * 2 + WINDOW_OFFSET * 2 + TILE_SIZE * params->width + TILE_BORDER;
    *y = BORDER * 2 + WINDOW_OFFSET * 2 + TILE_SIZE * params->height + TILE_BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret;

    ret = snewn(NCOLOURS * 3, float);
    *ncolours = NCOLOURS;

    /*
     * Basic background colour is whatever the front end thinks is
     * a sensible default.
     */
    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /*
     * Wires are black.
     */
    ret[COL_WIRE * 3 + 0] = 0.0F;
    ret[COL_WIRE * 3 + 1] = 0.0F;
    ret[COL_WIRE * 3 + 2] = 0.0F;

    /*
     * Powered wires and powered endpoints are cyan.
     */
    ret[COL_POWERED * 3 + 0] = 0.0F;
    ret[COL_POWERED * 3 + 1] = 1.0F;
    ret[COL_POWERED * 3 + 2] = 1.0F;

    /*
     * Barriers are red.
     */
    ret[COL_BARRIER * 3 + 0] = 1.0F;
    ret[COL_BARRIER * 3 + 1] = 0.0F;
    ret[COL_BARRIER * 3 + 2] = 0.0F;

    /*
     * Unpowered endpoints are blue.
     */
    ret[COL_ENDPOINT * 3 + 0] = 0.0F;
    ret[COL_ENDPOINT * 3 + 1] = 0.0F;
    ret[COL_ENDPOINT * 3 + 2] = 1.0F;

    /*
     * Tile borders are a darker grey than the background.
     */
    ret[COL_BORDER * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_BORDER * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_BORDER * 3 + 2] = 0.5F * ret[COL_BACKGROUND * 3 + 2];

    /*
     * Flashing tiles are a grey in between those two.
     */
    ret[COL_FLASHING * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_FLASHING * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_FLASHING * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_LOWLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.8F;
    ret[COL_LOWLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.8F;
    ret[COL_LOWLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 0.8F;
    ret[COL_TEXT * 3 + 0] = 0.0;
    ret[COL_TEXT * 3 + 1] = 0.0;
    ret[COL_TEXT * 3 + 2] = 0.0;

    return ret;
}

static void draw_filled_line(drawing *dr, int x1, int y1, int x2, int y2,
			     int colour)
{
    draw_line(dr, x1-1, y1, x2-1, y2, COL_WIRE);
    draw_line(dr, x1+1, y1, x2+1, y2, COL_WIRE);
    draw_line(dr, x1, y1-1, x2, y2-1, COL_WIRE);
    draw_line(dr, x1, y1+1, x2, y2+1, COL_WIRE);
    draw_line(dr, x1, y1, x2, y2, colour);
}

static void draw_rect_coords(drawing *dr, int x1, int y1, int x2, int y2,
                             int colour)
{
    int mx = (x1 < x2 ? x1 : x2);
    int my = (y1 < y2 ? y1 : y2);
    int dx = (x2 + x1 - 2*mx + 1);
    int dy = (y2 + y1 - 2*my + 1);

    draw_rect(dr, mx, my, dx, dy, colour);
}

static void draw_barrier_corner(drawing *dr, game_drawstate *ds,
                                int x, int y, int dir, int phase)
{
    int bx = BORDER + WINDOW_OFFSET + TILE_SIZE * x;
    int by = BORDER + WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, dx, dy, dir2;

    dir >>= 4;

    dir2 = A(dir);
    dx = X(dir) + X(dir2);
    dy = Y(dir) + Y(dir2);
    x1 = (dx > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);
    y1 = (dy > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);

    if (phase == 0) {
        draw_rect_coords(dr, bx+x1, by+y1,
                         bx+x1-TILE_BORDER*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_WIRE);
        draw_rect_coords(dr, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-TILE_BORDER*dy,
                         COL_WIRE);
    } else {
        draw_rect_coords(dr, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_BARRIER);
    }
}

static void draw_barrier(drawing *dr, game_drawstate *ds,
                         int x, int y, int dir, int phase)
{
    int bx = BORDER + WINDOW_OFFSET + TILE_SIZE * x;
    int by = BORDER + WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, w, h;

    x1 = (X(dir) > 0 ? TILE_SIZE : X(dir) == 0 ? TILE_BORDER : 0);
    y1 = (Y(dir) > 0 ? TILE_SIZE : Y(dir) == 0 ? TILE_BORDER : 0);
    w = (X(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);
    h = (Y(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);

    if (phase == 0) {
        draw_rect(dr, bx+x1-X(dir), by+y1-Y(dir), w, h, COL_WIRE);
    } else {
        draw_rect(dr, bx+x1, by+y1, w, h, COL_BARRIER);
    }
}

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, int tile, float xshift, float yshift)
{
    int bx = BORDER + WINDOW_OFFSET + TILE_SIZE * x + (int)(xshift * TILE_SIZE);
    int by = BORDER + WINDOW_OFFSET + TILE_SIZE * y + (int)(yshift * TILE_SIZE);
    float cx, cy, ex, ey;
    int dir, col;

    /*
     * When we draw a single tile, we must draw everything up to
     * and including the borders around the tile. This means that
     * if the neighbouring tiles have connections to those borders,
     * we must draw those connections on the borders themselves.
     *
     * This would be terribly fiddly if we ever had to draw a tile
     * while its neighbour was in mid-rotate, because we'd have to
     * arrange to _know_ that the neighbour was being rotated and
     * hence had an anomalous effect on the redraw of this tile.
     * Fortunately, the drawing algorithm avoids ever calling us in
     * this circumstance: we're either drawing lots of straight
     * tiles at game start or after a move is complete, or we're
     * repeatedly drawing only the rotating tile. So no problem.
     */

    /*
     * So. First blank the tile out completely: draw a big
     * rectangle in border colour, and a smaller rectangle in
     * background colour to fill it in.
     */
    draw_rect(dr, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER,
              COL_BORDER);
    draw_rect(dr, bx+TILE_BORDER, by+TILE_BORDER,
              TILE_SIZE-TILE_BORDER, TILE_SIZE-TILE_BORDER,
              tile & FLASHING ? COL_FLASHING : COL_BACKGROUND);

    /*
     * Draw the wires.
     */
    cx = cy = TILE_BORDER + (TILE_SIZE-TILE_BORDER) / 2.0F - 0.5F;
    col = (tile & ACTIVE ? COL_POWERED : COL_WIRE);
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            draw_filled_line(dr, bx+(int)cx, by+(int)cy,
			     bx+(int)(cx+ex), by+(int)(cy+ey),
			     COL_WIRE);
        }
    }
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            draw_line(dr, bx+(int)cx, by+(int)cy,
		      bx+(int)(cx+ex), by+(int)(cy+ey), col);
        }
    }

    /*
     * Draw the box in the middle. We do this in blue if the tile
     * is an unpowered endpoint, in cyan if the tile is a powered
     * endpoint, in black if the tile is the centrepiece, and
     * otherwise not at all.
     */
    col = -1;
    if (x == state->cx && y == state->cy)
        col = COL_WIRE;
    else if (COUNT(tile) == 1) {
        col = (tile & ACTIVE ? COL_POWERED : COL_ENDPOINT);
    }
    if (col >= 0) {
        int i, points[8];

        points[0] = +1; points[1] = +1;
        points[2] = +1; points[3] = -1;
        points[4] = -1; points[5] = -1;
        points[6] = -1; points[7] = +1;

        for (i = 0; i < 8; i += 2) {
            ex = (TILE_SIZE * 0.24F) * points[i];
            ey = (TILE_SIZE * 0.24F) * points[i+1];
            points[i] = bx+(int)(cx+ex);
            points[i+1] = by+(int)(cy+ey);
        }

        draw_polygon(dr, points, 4, col, COL_WIRE);
    }

    /*
     * Draw the points on the border if other tiles are connected
     * to us.
     */
    for (dir = 1; dir < 0x10; dir <<= 1) {
        int dx, dy, px, py, lx, ly, vx, vy, ox, oy;

        dx = X(dir);
        dy = Y(dir);

        ox = x + dx;
        oy = y + dy;

        if (ox < 0 || ox >= state->width || oy < 0 || oy >= state->height)
            continue;

        if (!(tile(state, ox, oy) & F(dir)))
            continue;

        px = bx + (int)(dx>0 ? TILE_SIZE + TILE_BORDER - 1 : dx<0 ? 0 : cx);
        py = by + (int)(dy>0 ? TILE_SIZE + TILE_BORDER - 1 : dy<0 ? 0 : cy);
        lx = dx * (TILE_BORDER-1);
        ly = dy * (TILE_BORDER-1);
        vx = (dy ? 1 : 0);
        vy = (dx ? 1 : 0);

        if (xshift == 0.0F && yshift == 0.0F && (tile & dir)) {
            /*
             * If we are fully connected to the other tile, we must
             * draw right across the tile border. (We can use our
             * own ACTIVE state to determine what colour to do this
             * in: if we are fully connected to the other tile then
             * the two ACTIVE states will be the same.)
             */
            draw_rect_coords(dr, px-vx, py-vy, px+lx+vx, py+ly+vy, COL_WIRE);
            draw_rect_coords(dr, px, py, px+lx, py+ly,
                             (tile & ACTIVE) ? COL_POWERED : COL_WIRE);
        } else {
            /*
             * The other tile extends into our border, but isn't
             * actually connected to us. Just draw a single black
             * dot.
             */
            draw_rect_coords(dr, px, py, px, py, COL_WIRE);
        }
    }

    draw_update(dr, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER);
}

static void draw_tile_barriers(drawing *dr, game_drawstate *ds,
                               const game_state *state, int x, int y)
{
    int phase;
    int dir;
    int bx = BORDER + WINDOW_OFFSET + TILE_SIZE * x;
    int by = BORDER + WINDOW_OFFSET + TILE_SIZE * y;
    /*
     * Draw barrier corners, and then barriers.
     */
    for (phase = 0; phase < 2; phase++) {
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & (dir << 4))
                draw_barrier_corner(dr, ds, x, y, dir << 4, phase);
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & dir)
                draw_barrier(dr, ds, x, y, dir, phase);
    }

    draw_update(dr, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER);
}

static void draw_arrow(drawing *dr, game_drawstate *ds,
                       int x, int y, int xdx, int xdy, bool cur)
{
    int coords[14];
    int ydy = -xdx, ydx = xdy;

    x = x * TILE_SIZE + BORDER + WINDOW_OFFSET;
    y = y * TILE_SIZE + BORDER + WINDOW_OFFSET;

#define POINT(n, xx, yy) ( \
    coords[2*(n)+0] = x + (xx)*xdx + (yy)*ydx, \
    coords[2*(n)+1] = y + (xx)*xdy + (yy)*ydy)

    POINT(0, TILE_SIZE / 2, 3 * TILE_SIZE / 4);   /* top of arrow */
    POINT(1, 3 * TILE_SIZE / 4, TILE_SIZE / 2);   /* right corner */
    POINT(2, 5 * TILE_SIZE / 8, TILE_SIZE / 2);   /* right concave */
    POINT(3, 5 * TILE_SIZE / 8, TILE_SIZE / 4);   /* bottom right */
    POINT(4, 3 * TILE_SIZE / 8, TILE_SIZE / 4);   /* bottom left */
    POINT(5, 3 * TILE_SIZE / 8, TILE_SIZE / 2);   /* left concave */
    POINT(6,     TILE_SIZE / 4, TILE_SIZE / 2);   /* left corner */

    draw_polygon(dr, coords, 7, cur ? COL_POWERED : COL_LOWLIGHT, COL_TEXT);
}

static void draw_arrow_for_cursor(drawing *dr, game_drawstate *ds,
                                  int cur_x, int cur_y, bool cur)
{
    if (cur_x == -1 && cur_y == -1)
        return; /* 'no cursur here */
    else if (cur_x == -1) /* LH column. */
        draw_arrow(dr, ds, 0, cur_y+1, 0, -1, cur);
    else if (cur_x == ds->width) /* RH column */
        draw_arrow(dr, ds, ds->width, cur_y, 0, +1, cur);
    else if (cur_y == -1) /* Top row */
        draw_arrow(dr, ds, cur_x, 0, +1, 0, cur);
    else if (cur_y == ds->height) /* Bottom row */
        draw_arrow(dr, ds, cur_x+1, ds->height, -1, 0, cur);
    else
        assert(!"Invalid cursor position");

    draw_update(dr,
                cur_x * TILE_SIZE + BORDER + WINDOW_OFFSET,
                cur_y * TILE_SIZE + BORDER + WINDOW_OFFSET,
                TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float t, float ft)
{
    int x, y, frame;
    unsigned char *active;
    float xshift = 0.0;
    float yshift = 0.0;
    int cur_x = -1, cur_y = -1;

    /*
     * Draw the exterior barrier lines if this is our first call.
     */
    if (!ds->started) {
        int phase;

        ds->started = true;

        for (phase = 0; phase < 2; phase++) {

            for (x = 0; x < ds->width; x++) {
                if (barrier(state, x, 0) & UL)
                    draw_barrier_corner(dr, ds, x, -1, LD, phase);
                if (barrier(state, x, 0) & RU)
                    draw_barrier_corner(dr, ds, x, -1, DR, phase);
                if (barrier(state, x, 0) & U)
                    draw_barrier(dr, ds, x, -1, D, phase);
                if (barrier(state, x, ds->height-1) & DR)
                    draw_barrier_corner(dr, ds, x, ds->height, RU, phase);
                if (barrier(state, x, ds->height-1) & LD)
                    draw_barrier_corner(dr, ds, x, ds->height, UL, phase);
                if (barrier(state, x, ds->height-1) & D)
                    draw_barrier(dr, ds, x, ds->height, U, phase);
            }

            for (y = 0; y < ds->height; y++) {
                if (barrier(state, 0, y) & UL)
                    draw_barrier_corner(dr, ds, -1, y, RU, phase);
                if (barrier(state, 0, y) & LD)
                    draw_barrier_corner(dr, ds, -1, y, DR, phase);
                if (barrier(state, 0, y) & L)
                    draw_barrier(dr, ds, -1, y, R, phase);
                if (barrier(state, ds->width-1, y) & RU)
                    draw_barrier_corner(dr, ds, ds->width, y, UL, phase);
                if (barrier(state, ds->width-1, y) & DR)
                    draw_barrier_corner(dr, ds, ds->width, y, LD, phase);
                if (barrier(state, ds->width-1, y) & R)
                    draw_barrier(dr, ds, ds->width, y, L, phase);
            }
        }

        /*
         * Arrows for making moves.
         */
        for (x = 0; x < ds->width; x++) {
            if (x == state->cx) continue;
            draw_arrow(dr, ds, x, 0, +1, 0, false);
            draw_arrow(dr, ds, x+1, ds->height, -1, 0, false);
        }
        for (y = 0; y < ds->height; y++) {
            if (y == state->cy) continue;
            draw_arrow(dr, ds, ds->width, y, 0, +1, false);
            draw_arrow(dr, ds, 0, y+1, 0, -1, false);
        }
    }
    if (ui->cur_visible) {
        cur_x = ui->cur_x; cur_y = ui->cur_y;
    }
    if (cur_x != ds->cur_x || cur_y != ds->cur_y) {
        /* Cursor has changed; redraw two (prev and curr) arrows. */
        assert(cur_x != state->cx && cur_y != state->cy);

        draw_arrow_for_cursor(dr, ds, cur_x, cur_y, true);
        draw_arrow_for_cursor(dr, ds, ds->cur_x, ds->cur_y, false);
        ds->cur_x = cur_x; ds->cur_y = cur_y;
    }

    /* Check if this is an undo.  If so, we will need to run any animation
     * backwards.
     */
    if (oldstate && oldstate->move_count > state->move_count) {
        const game_state * tmpstate = state;
        state = oldstate;
        oldstate = tmpstate;
        t = ANIM_TIME - t;
    }

    if (oldstate && (t < ANIM_TIME)) {
        /*
         * We're animating a slide, of row/column number
         * state->last_move_pos, in direction
         * state->last_move_dir
         */
        xshift = state->last_move_row == -1 ? 0.0F :
                (1 - t / ANIM_TIME) * state->last_move_dir;
        yshift = state->last_move_col == -1 ? 0.0F :
                (1 - t / ANIM_TIME) * state->last_move_dir;
    }
    
    frame = -1;
    if (ft > 0) {
        /*
         * We're animating a completion flash. Find which frame
         * we're at.
         */
        frame = (int)(ft / FLASH_FRAME);
    }

    /*
     * Draw any tile which differs from the way it was last drawn.
     */
    if (xshift != 0.0F || yshift != 0.0F) {
        active = compute_active(state,
                                state->last_move_row, state->last_move_col);
    } else {
        active = compute_active(state, -1, -1);
    }

    clip(dr,
         BORDER + WINDOW_OFFSET, BORDER + WINDOW_OFFSET,
         TILE_SIZE * state->width + TILE_BORDER,
         TILE_SIZE * state->height + TILE_BORDER);
    
    for (x = 0; x < ds->width; x++)
        for (y = 0; y < ds->height; y++) {
            unsigned char c = tile(state, x, y) | index(state, active, x, y);

            /*
             * In a completion flash, we adjust the FLASHING bit
             * depending on our distance from the centre point and
             * the frame number.
             */
            if (frame >= 0) {
                int xdist, ydist, dist;
                xdist = (x < state->cx ? state->cx - x : x - state->cx);
                ydist = (y < state->cy ? state->cy - y : y - state->cy);
                dist = (xdist > ydist ? xdist : ydist);

                if (frame >= dist && frame < dist+4) {
                    int flash = (frame - dist) & 1;
                    flash = flash ? FLASHING : 0;
                    c = (c &~ FLASHING) | flash;
                }
            }

            if (index(state, ds->visible, x, y) != c ||
                index(state, ds->visible, x, y) == 0xFF ||
                (x == state->last_move_col || y == state->last_move_row))
            {
                float xs = (y == state->last_move_row ? xshift : (float)0.0);
                float ys = (x == state->last_move_col ? yshift : (float)0.0);

                draw_tile(dr, ds, state, x, y, c, xs, ys);
                if (xs < 0 && x == 0)
                    draw_tile(dr, ds, state, state->width, y, c, xs, ys);
                else if (xs > 0 && x == state->width - 1)
                    draw_tile(dr, ds, state, -1, y, c, xs, ys);
                else if (ys < 0 && y == 0)
                    draw_tile(dr, ds, state, x, state->height, c, xs, ys);
                else if (ys > 0 && y == state->height - 1)
                    draw_tile(dr, ds, state, x, -1, c, xs, ys);

                if (x == state->last_move_col || y == state->last_move_row)
                    index(state, ds->visible, x, y) = 0xFF;
                else
                    index(state, ds->visible, x, y) = c;
            }
        }

    for (x = 0; x < ds->width; x++)
        for (y = 0; y < ds->height; y++)
            draw_tile_barriers(dr, ds, state, x, y);

    unclip(dr);

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];
	int i, n, a;

	n = state->width * state->height;
	for (i = a = 0; i < n; i++)
	    if (active[i])
		a++;

	if (state->used_solve)
	    sprintf(statusbuf, "Moves since auto-solve: %d",
		    state->move_count - state->completed);
	else
	    sprintf(statusbuf, "%sMoves: %d",
		    (state->completed ? "COMPLETED! " : ""),
		    (state->completed ? state->completed : state->move_count));

        if (state->movetarget)
            sprintf(statusbuf + strlen(statusbuf), " (target %d)",
                    state->movetarget);

	sprintf(statusbuf + strlen(statusbuf), " Active: %d/%d", a, n);

	status_bar(dr, statusbuf);
    }

    sfree(active);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return ANIM_TIME;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    /*
     * If the game has just been completed, we display a completion
     * flash.
     */
    if (!oldstate->completed && newstate->completed &&
	!oldstate->used_solve && !newstate->used_solve) {
        int size;
        size = 0;
        if (size < newstate->cx+1)
            size = newstate->cx+1;
        if (size < newstate->cy+1)
            size = newstate->cy+1;
        if (size < newstate->width - newstate->cx)
            size = newstate->width - newstate->cx;
        if (size < newstate->height - newstate->cy)
            size = newstate->height - newstate->cy;
        return FLASH_FRAME * (size+4);
    }

    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        *x = BORDER + WINDOW_OFFSET + TILE_SIZE * ui->cur_x;
        *y = BORDER + WINDOW_OFFSET + TILE_SIZE * ui->cur_y;

        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

#ifdef COMBINED
#define thegame netslide
#endif

const struct game thegame = {
    "Netslide", "games.netslide", "netslide",
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
    false, NULL, NULL, /* can_format_as_text_now, text_format */
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
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
