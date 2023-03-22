/*
 * net.c: Net game.
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

/*
 * The standard user interface for Net simply has left- and
 * right-button mouse clicks in a square rotate it one way or the
 * other. We also provide, by #ifdef, a separate interface based on
 * rotational dragging motions. I initially developed this for the
 * Mac on the basis that it might work better than the click
 * interface with only one mouse button available, but in fact
 * found it to be quite strange and unintuitive. Apparently it
 * works better on stylus-driven platforms such as Palm and
 * PocketPC, though, so we enable it by default there.
 */
#ifdef STYLUS_BASED
#define USE_DRAGGING
#endif

/* Direction and other bitfields */
#define R 0x01
#define U 0x02
#define L 0x04
#define D 0x08
#define LOCKED 0x10
#define ACTIVE 0x20
#define RERR (R << 6)
#define UERR (U << 6)
#define LERR (L << 6)
#define DERR (D << 6)
#define ERR(dir) ((dir) << 6)

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

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define LINE_THICK ((TILE_SIZE+47)/48)
#ifdef SMALL_SCREEN
#define WINDOW_OFFSET 4
#else
#define WINDOW_OFFSET 16
#endif

#define ROTATE_TIME 0.13F
#define FLASH_FRAME 0.07F

enum {
    COL_BACKGROUND,
    COL_LOCKED,
    COL_BORDER,
    COL_WIRE,
    COL_ENDPOINT,
    COL_POWERED,
    COL_BARRIER,
    COL_ERR,
    NCOLOURS
};

struct game_params {
    int width;
    int height;
    bool wrapping;
    bool unique;
    float barrier_probability;
};

typedef struct game_immutable_state {
    int refcount;
    unsigned char *barriers;
} game_immutable_state;

struct game_state {
    int width, height;
    bool wrapping, completed;
    int last_rotate_x, last_rotate_y, last_rotate_dir;
    bool used_solve;
    unsigned char *tiles;
    struct game_immutable_state *imm;
};

#define OFFSETWH(x2,y2,x1,y1,dir,width,height) \
    ( (x2) = ((x1) + width + X((dir))) % width, \
      (y2) = ((y1) + height + Y((dir))) % height)

#define OFFSET(x2,y2,x1,y1,dir,state) \
	OFFSETWH(x2,y2,x1,y1,dir,(state)->width,(state)->height)

#define index(state, a, x, y) ( a[(y) * (state)->width + (x)] )
#define tile(state, x, y)     index(state, (state)->tiles, x, y)
#define barrier(state, x, y)  index(state, (state)->imm->barriers, x, y)

struct xyd {
    int x, y, direction;
};

static int xyd_cmp(const void *av, const void *bv) {
    const struct xyd *a = (const struct xyd *)av;
    const struct xyd *b = (const struct xyd *)bv;
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

static int xyd_cmp_nc(void *av, void *bv) { return xyd_cmp(av, bv); }

static struct xyd *new_xyd(int x, int y, int direction)
{
    struct xyd *xyd = snew(struct xyd);
    xyd->x = x;
    xyd->y = y;
    xyd->direction = direction;
    return xyd;
}

/* ----------------------------------------------------------------------
 * Manage game parameters.
 */
static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->width = 5;
    ret->height = 5;
    ret->wrapping = false;
    ret->unique = true;
    ret->barrier_probability = 0.0;

    return ret;
}

static const struct game_params net_presets[] = {
    {5, 5, false, true, 0.0},
    {7, 7, false, true, 0.0},
    {9, 9, false, true, 0.0},
    {11, 11, false, true, 0.0},
#ifndef SMALL_SCREEN
    {13, 11, false, true, 0.0},
#endif
    {5, 5, true, true, 0.0},
    {7, 7, true, true, 0.0},
    {9, 9, true, true, 0.0},
    {11, 11, true, true, 0.0},
#ifndef SMALL_SCREEN
    {13, 11, true, true, 0.0},
#endif
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(net_presets))
        return false;

    ret = snew(game_params);
    *ret = net_presets[i];

    sprintf(str, "%dx%d%s", ret->width, ret->height,
            ret->wrapping ? " wrapping" : "");

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

    ret->width = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        ret->height = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    } else {
        ret->height = ret->width;
    }

    while (*p) {
        if (*p == 'w') {
            p++;
	    ret->wrapping = true;
	} else if (*p == 'b') {
	    p++;
            ret->barrier_probability = (float)atof(p);
	    while (*p && (*p == '.' || isdigit((unsigned char)*p))) p++;
	} else if (*p == 'a') {
            p++;
	    ret->unique = false;
	} else
	    p++;		       /* skip any other gunk */
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
    if (full && !params->unique)
        ret[len++] = 'a';
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

    ret[4].name = "Ensure unique solution";
    ret[4].type = C_BOOLEAN;
    ret[4].u.boolean.bval = params->unique;

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
    ret->unique = cfg[4].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->width <= 0 || params->height <= 0)
	return "Width and height must both be greater than zero";
    if (params->width <= 1 && params->height <= 1)
	return "At least one of width and height must be greater than one";
    if (params->width > INT_MAX / params->height)
        return "Width times height must not be unreasonably large";
    if (params->barrier_probability < 0)
	return "Barrier probability may not be negative";
    if (params->barrier_probability > 1)
	return "Barrier probability may not be greater than 1";

    /*
     * Specifying either grid dimension as 2 in a wrapping puzzle
     * makes it actually impossible to ensure a unique puzzle
     * solution.
     * 
     * Proof:
     * 
     * Without loss of generality, let us assume the puzzle _width_
     * is 2, so we can conveniently discuss rows without having to
     * say `rows/columns' all the time. (The height may be 2 as
     * well, but that doesn't matter.)
     * 
     * In each row, there are two edges between tiles: the inner
     * edge (running down the centre of the grid) and the outer
     * edge (the identified left and right edges of the grid).
     * 
     * Lemma: In any valid 2xn puzzle there must be at least one
     * row in which _exactly one_ of the inner edge and outer edge
     * is connected.
     * 
     *   Proof: No row can have _both_ inner and outer edges
     *   connected, because this would yield a loop. So the only
     *   other way to falsify the lemma is for every row to have
     *   _neither_ the inner nor outer edge connected. But this
     *   means there is no connection at all between the left and
     *   right columns of the puzzle, so there are two disjoint
     *   subgraphs, which is also disallowed. []
     * 
     * Given such a row, it is always possible to make the
     * disconnected edge connected and the connected edge
     * disconnected without changing the state of any other edge.
     * (This is easily seen by case analysis on the various tiles:
     * left-pointing and right-pointing endpoints can be exchanged,
     * likewise T-pieces, and a corner piece can select its
     * horizontal connectivity independently of its vertical.) This
     * yields a distinct valid solution.
     * 
     * Thus, for _every_ row in which exactly one of the inner and
     * outer edge is connected, there are two valid states for that
     * row, and hence the total number of solutions of the puzzle
     * is at least 2^(number of such rows), and in particular is at
     * least 2 since there must be at least one such row. []
     */
    if (full && params->unique && params->wrapping &&
        (params->width == 2 || params->height == 2))
        return "No wrapping puzzle with a width or height of 2 can have"
        " a unique solution";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver used to assure solution uniqueness during generation. 
 */

/*
 * Test cases I used while debugging all this were
 * 
 *   ./net --generate 1 13x11w#12300
 * which expands under the non-unique grid generation rules to
 *   13x11w:5eaade1bd222664436d5e2965c12656b1129dd825219e3274d558d5eb2dab5da18898e571d5a2987be79746bd95726c597447d6da96188c513add829da7681da954db113d3cd244
 * and has two ambiguous areas.
 * 
 * An even better one is
 *   13x11w#507896411361192
 * which expands to
 *   13x11w:b7125b1aec598eb31bd58d82572bc11494e5dee4e8db2bdd29b88d41a16bdd996d2996ddec8c83741a1e8674e78328ba71737b8894a9271b1cd1399453d1952e43951d9b712822e
 * and has an ambiguous area _and_ a situation where loop avoidance
 * is a necessary deductive technique.
 * 
 * Then there's
 *   48x25w#820543338195187
 * becoming
 *   48x25w:255989d14cdd185deaa753a93821a12edc1ab97943ac127e2685d7b8b3c48861b2192416139212b316eddd35de43714ebc7628d753db32e596284d9ec52c5a7dc1b4c811a655117d16dc28921b2b4161352cab1d89d18bc836b8b891d55ea4622a1251861b5bc9a8aa3e5bcd745c95229ca6c3b5e21d5832d397e917325793d7eb442dc351b2db2a52ba8e1651642275842d8871d5534aabc6d5b741aaa2d48ed2a7dbbb3151ddb49d5b9a7ed1ab98ee75d613d656dbba347bc514c84556b43a9bc65a3256ead792488b862a9d2a8a39b4255a4949ed7dbd79443292521265896b4399c95ede89d7c8c797a6a57791a849adea489359a158aa12e5dacce862b8333b7ebea7d344d1a3c53198864b73a9dedde7b663abb1b539e1e8853b1b7edb14a2a17ebaae4dbe63598a2e7e9a2dbdad415bc1d8cb88cbab5a8c82925732cd282e641ea3bd7d2c6e776de9117a26be86deb7c82c89524b122cb9397cd1acd2284e744ea62b9279bae85479ababe315c3ac29c431333395b24e6a1e3c43a2da42d4dce84aadd5b154aea555eaddcbd6e527d228c19388d9b424d94214555a7edbdeebe569d4a56dc51a86bd9963e377bb74752bd5eaa5761ba545e297b62a1bda46ab4aee423ad6c661311783cc18786d4289236563cb4a75ec67d481c14814994464cd1b87396dee63e5ab6e952cc584baa1d4c47cb557ec84dbb63d487c8728118673a166846dd3a4ebc23d6cb9c5827d96b4556e91899db32b517eda815ae271a8911bd745447121dc8d321557bc2a435ebec1bbac35b1a291669451174e6aa2218a4a9c5a6ca31ebc45d84e3a82c121e9ced7d55e9a
 * which has a spot (far right) where slightly more complex loop
 * avoidance is required.
 */

struct todo {
    bool *marked;
    int *buffer;
    int buflen;
    int head, tail;
};

static struct todo *todo_new(int maxsize)
{
    struct todo *todo = snew(struct todo);
    todo->marked = snewn(maxsize, bool);
    memset(todo->marked, 0, maxsize);
    todo->buflen = maxsize + 1;
    todo->buffer = snewn(todo->buflen, int);
    todo->head = todo->tail = 0;
    return todo;
}

static void todo_free(struct todo *todo)
{
    sfree(todo->marked);
    sfree(todo->buffer);
    sfree(todo);
}

static void todo_add(struct todo *todo, int index)
{
    if (todo->marked[index])
	return;			       /* already on the list */
    todo->marked[index] = true;
    todo->buffer[todo->tail++] = index;
    if (todo->tail == todo->buflen)
	todo->tail = 0;
}

static int todo_get(struct todo *todo) {
    int ret;

    if (todo->head == todo->tail)
	return -1;		       /* list is empty */
    ret = todo->buffer[todo->head++];
    if (todo->head == todo->buflen)
	todo->head = 0;
    todo->marked[ret] = false;

    return ret;
}

/*
 * Return values: -1 means puzzle was proved inconsistent, 0 means we
 * failed to narrow down to a unique solution, +1 means we solved it
 * fully.
 */
static int net_solver(int w, int h, unsigned char *tiles,
		      unsigned char *barriers, bool wrapping)
{
    unsigned char *tilestate;
    unsigned char *edgestate;
    int *deadends;
    int *equivalence;
    struct todo *todo;
    int i, j, x, y;
    int area;
    bool done_something;

    /*
     * Set up the solver's data structures.
     */
    
    /*
     * tilestate stores the possible orientations of each tile.
     * There are up to four of these, so we'll index the array in
     * fours. tilestate[(y * w + x) * 4] and its three successive
     * members give the possible orientations, clearing to 255 from
     * the end as things are ruled out.
     * 
     * In this loop we also count up the area of the grid (which is
     * not _necessarily_ equal to w*h, because there might be one
     * or more blank squares present. This will never happen in a
     * grid generated _by_ this program, but it's worth keeping the
     * solver as general as possible.)
     */
    tilestate = snewn(w * h * 4, unsigned char);
    area = 0;
    for (i = 0; i < w*h; i++) {
	tilestate[i * 4] = tiles[i] & 0xF;
	for (j = 1; j < 4; j++) {
	    if (tilestate[i * 4 + j - 1] == 255 ||
		A(tilestate[i * 4 + j - 1]) == tilestate[i * 4])
		tilestate[i * 4 + j] = 255;
	    else
		tilestate[i * 4 + j] = A(tilestate[i * 4 + j - 1]);
	}
	if (tiles[i] != 0)
	    area++;
    }

    /*
     * edgestate stores the known state of each edge. It is 0 for
     * unknown, 1 for open (connected) and 2 for closed (not
     * connected).
     * 
     * In principle we need only worry about each edge once each,
     * but in fact it's easier to track each edge twice so that we
     * can reference it from either side conveniently. Also I'm
     * going to allocate _five_ bytes per tile, rather than the
     * obvious four, so that I can index edgestate[(y*w+x) * 5 + d]
     * where d is 1,2,4,8 and they never overlap.
     */
    edgestate = snewn((w * h - 1) * 5 + 9, unsigned char);
    memset(edgestate, 0, (w * h - 1) * 5 + 9);

    /*
     * deadends tracks which edges have dead ends on them. It is
     * indexed by tile and direction: deadends[(y*w+x) * 5 + d]
     * tells you whether heading out of tile (x,y) in direction d
     * can reach a limited amount of the grid. Values are area+1
     * (no dead end known) or less than that (can reach _at most_
     * this many other tiles by heading this way out of this tile).
     */
    deadends = snewn((w * h - 1) * 5 + 9, int);
    for (i = 0; i < (w * h - 1) * 5 + 9; i++)
	deadends[i] = area+1;

    /*
     * equivalence tracks which sets of tiles are known to be
     * connected to one another, so we can avoid creating loops by
     * linking together tiles which are already linked through
     * another route.
     * 
     * This is a disjoint set forest structure: equivalence[i]
     * contains the index of another member of the equivalence
     * class containing i, or contains i itself for precisely one
     * member in each such class. To find a representative member
     * of the equivalence class containing i, you keep replacing i
     * with equivalence[i] until it stops changing; then you go
     * _back_ along the same path and point everything on it
     * directly at the representative member so as to speed up
     * future searches. Then you test equivalence between tiles by
     * finding the representative of each tile and seeing if
     * they're the same; and you create new equivalence (merge
     * classes) by finding the representative of each tile and
     * setting equivalence[one]=the_other.
     */
    equivalence = snew_dsf(w * h);

    /*
     * On a non-wrapping grid, we instantly know that all the edges
     * round the edge are closed.
     */
    if (!wrapping) {
	for (i = 0; i < w; i++) {
	    edgestate[i * 5 + 2] = edgestate[((h-1) * w + i) * 5 + 8] = 2;
	}
	for (i = 0; i < h; i++) {
	    edgestate[(i * w + w-1) * 5 + 1] = edgestate[(i * w) * 5 + 4] = 2;
	}
    }

    /*
     * If we have barriers available, we can mark those edges as
     * closed too.
     */
    if (barriers) {
	for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
	    int d;
	    for (d = 1; d <= 8; d += d) {
		if (barriers[y*w+x] & d) {
		    int x2, y2;
		    /*
		     * In principle the barrier list should already
		     * contain each barrier from each side, but
		     * let's not take chances with our internal
		     * consistency.
		     */
		    OFFSETWH(x2, y2, x, y, d, w, h);
		    edgestate[(y*w+x) * 5 + d] = 2;
		    edgestate[(y2*w+x2) * 5 + F(d)] = 2;
		}
	    }
	}
    }

    /*
     * Since most deductions made by this solver are local (the
     * exception is loop avoidance, where joining two tiles
     * together on one side of the grid can theoretically permit a
     * fresh deduction on the other), we can address the scaling
     * problem inherent in iterating repeatedly over the entire
     * grid by instead working with a to-do list.
     */
    todo = todo_new(w * h);

    /*
     * Main deductive loop.
     */
    done_something = true;	       /* prevent instant termination! */
    while (1) {
	int index;

	/*
	 * Take a tile index off the todo list and process it.
	 */
	index = todo_get(todo);
	if (index == -1) {
	    /*
	     * If we have run out of immediate things to do, we
	     * have no choice but to scan the whole grid for
	     * longer-range things we've missed. Hence, I now add
	     * every square on the grid back on to the to-do list.
	     * I also set `done_something' to false at this point;
	     * if we later come back here and find it still false,
	     * we will know we've scanned the entire grid without
	     * finding anything new to do, and we can terminate.
	     */
	    if (!done_something)
		break;
	    for (i = 0; i < w*h; i++)
		todo_add(todo, i);
	    done_something = false;

	    index = todo_get(todo);
	}

	y = index / w;
	x = index % w;
	{
	    int d, ourclass = dsf_canonify(equivalence, y*w+x);
	    int deadendmax[9];

	    deadendmax[1] = deadendmax[2] = deadendmax[4] = deadendmax[8] = 0;

	    for (i = j = 0; i < 4 && tilestate[(y*w+x) * 4 + i] != 255; i++) {
		bool valid;
		int nnondeadends, nondeadends[4], deadendtotal;
		int nequiv, equiv[5];
		int val = tilestate[(y*w+x) * 4 + i];

		valid = true;
		nnondeadends = deadendtotal = 0;
		equiv[0] = ourclass;
		nequiv = 1;
		for (d = 1; d <= 8; d += d) {
		    /*
		     * Immediately rule out this orientation if it
		     * conflicts with any known edge.
		     */
		    if ((edgestate[(y*w+x) * 5 + d] == 1 && !(val & d)) ||
			(edgestate[(y*w+x) * 5 + d] == 2 && (val & d)))
			valid = false;

		    if (val & d) {
			/*
			 * Count up the dead-end statistics.
			 */
			if (deadends[(y*w+x) * 5 + d] <= area) {
			    deadendtotal += deadends[(y*w+x) * 5 + d];
			} else {
			    nondeadends[nnondeadends++] = d;
			}

			/*
			 * Ensure we aren't linking to any tiles,
			 * through edges not already known to be
			 * open, which create a loop.
			 */
			if (edgestate[(y*w+x) * 5 + d] == 0) {
			    int c, k, x2, y2;
			    
			    OFFSETWH(x2, y2, x, y, d, w, h);
			    c = dsf_canonify(equivalence, y2*w+x2);
			    for (k = 0; k < nequiv; k++)
				if (c == equiv[k])
				    break;
			    if (k == nequiv)
				equiv[nequiv++] = c;
			    else
				valid = false;
			}
		    }
		}

		if (nnondeadends == 0) {
		    /*
		     * If this orientation links together dead-ends
		     * with a total area of less than the entire
		     * grid, it is invalid.
		     *
		     * (We add 1 to deadendtotal because of the
		     * tile itself, of course; one tile linking
		     * dead ends of size 2 and 3 forms a subnetwork
		     * with a total area of 6, not 5.)
		     */
		    if (deadendtotal > 0 && deadendtotal+1 < area)
			valid = false;
		} else if (nnondeadends == 1) {
		    /*
		     * If this orientation links together one or
		     * more dead-ends with precisely one
		     * non-dead-end, then we may have to mark that
		     * non-dead-end as a dead end going the other
		     * way. However, it depends on whether all
		     * other orientations share the same property.
		     */
		    deadendtotal++;
		    if (deadendmax[nondeadends[0]] < deadendtotal)
			deadendmax[nondeadends[0]] = deadendtotal;
		} else {
		    /*
		     * If this orientation links together two or
		     * more non-dead-ends, then we can rule out the
		     * possibility of putting in new dead-end
		     * markings in those directions.
		     */
		    int k;
		    for (k = 0; k < nnondeadends; k++)
			deadendmax[nondeadends[k]] = area+1;
		}

		if (valid)
		    tilestate[(y*w+x) * 4 + j++] = val;
#ifdef SOLVER_DIAGNOSTICS
		else
		    printf("ruling out orientation %x at %d,%d\n", val, x, y);
#endif
	    }

	    if (j == 0) {
                /* If we've ruled out all possible orientations for a
                 * tile, then our puzzle has no solution at all. */
                return -1;
            }

	    if (j < i) {
		done_something = true;

		/*
		 * We have ruled out at least one tile orientation.
		 * Make sure the rest are blanked.
		 */
		while (j < 4)
		    tilestate[(y*w+x) * 4 + j++] = 255;
	    }

	    /*
	     * Now go through the tile orientations again and see
	     * if we've deduced anything new about any edges.
	     */
	    {
		int a, o;
		a = 0xF; o = 0;

		for (i = 0; i < 4 && tilestate[(y*w+x) * 4 + i] != 255; i++) {
		    a &= tilestate[(y*w+x) * 4 + i];
		    o |= tilestate[(y*w+x) * 4 + i];
		}
		for (d = 1; d <= 8; d += d)
		    if (edgestate[(y*w+x) * 5 + d] == 0) {
			int x2, y2, d2;
			OFFSETWH(x2, y2, x, y, d, w, h);
			d2 = F(d);
			if (a & d) {
			    /* This edge is open in all orientations. */
#ifdef SOLVER_DIAGNOSTICS
			    printf("marking edge %d,%d:%d open\n", x, y, d);
#endif
			    edgestate[(y*w+x) * 5 + d] = 1;
			    edgestate[(y2*w+x2) * 5 + d2] = 1;
			    dsf_merge(equivalence, y*w+x, y2*w+x2);
			    done_something = true;
			    todo_add(todo, y2*w+x2);
			} else if (!(o & d)) {
			    /* This edge is closed in all orientations. */
#ifdef SOLVER_DIAGNOSTICS
			    printf("marking edge %d,%d:%d closed\n", x, y, d);
#endif
			    edgestate[(y*w+x) * 5 + d] = 2;
			    edgestate[(y2*w+x2) * 5 + d2] = 2;
			    done_something = true;
			    todo_add(todo, y2*w+x2);
			}
		    }

	    }

	    /*
	     * Now check the dead-end markers and see if any of
	     * them has lowered from the real ones.
	     */
	    for (d = 1; d <= 8; d += d) {
		int x2, y2, d2;
		OFFSETWH(x2, y2, x, y, d, w, h);
		d2 = F(d);
		if (deadendmax[d] > 0 &&
		    deadends[(y2*w+x2) * 5 + d2] > deadendmax[d]) {
#ifdef SOLVER_DIAGNOSTICS
		    printf("setting dead end value %d,%d:%d to %d\n",
			   x2, y2, d2, deadendmax[d]);
#endif
		    deadends[(y2*w+x2) * 5 + d2] = deadendmax[d];
		    done_something = true;
		    todo_add(todo, y2*w+x2);
		}
	    }

	}
    }

    /*
     * Mark all completely determined tiles as locked.
     */
    j = +1;
    for (i = 0; i < w*h; i++) {
	if (tilestate[i * 4 + 1] == 255) {
	    assert(tilestate[i * 4 + 0] != 255);
	    tiles[i] = tilestate[i * 4] | LOCKED;
	} else {
	    tiles[i] &= ~LOCKED;
	    j = 0;
	}
    }

    /*
     * Free up working space.
     */
    todo_free(todo);
    sfree(tilestate);
    sfree(edgestate);
    sfree(deadends);
    sfree(equivalence);

    return j;
}

/* ----------------------------------------------------------------------
 * Randomly select a new game description.
 */

/*
 * Function to randomly perturb an ambiguous section in a grid, to
 * attempt to ensure unique solvability.
 */
static void perturb(int w, int h, unsigned char *tiles, bool wrapping,
		    random_state *rs, int startx, int starty, int startd)
{
    struct xyd *perimeter, *perim2, *loop[2], looppos[2];
    int nperim, perimsize, nloop[2], loopsize[2];
    int x, y, d, i;

    /*
     * We know that the tile at (startx,starty) is part of an
     * ambiguous section, and we also know that its neighbour in
     * direction startd is fully specified. We begin by tracing all
     * the way round the ambiguous area.
     */
    nperim = perimsize = 0;
    perimeter = NULL;
    x = startx;
    y = starty;
    d = startd;
#ifdef PERTURB_DIAGNOSTICS
    printf("perturb %d,%d:%d\n", x, y, d);
#endif
    do {
	int x2, y2, d2;

	if (nperim >= perimsize) {
	    perimsize = perimsize * 3 / 2 + 32;
	    perimeter = sresize(perimeter, perimsize, struct xyd);
	}
	perimeter[nperim].x = x;
	perimeter[nperim].y = y;
	perimeter[nperim].direction = d;
	nperim++;
#ifdef PERTURB_DIAGNOSTICS
	printf("perimeter: %d,%d:%d\n", x, y, d);
#endif

	/*
	 * First, see if we can simply turn left from where we are
	 * and find another locked square.
	 */
	d2 = A(d);
	OFFSETWH(x2, y2, x, y, d2, w, h);
	if ((!wrapping && (abs(x2-x) > 1 || abs(y2-y) > 1)) ||
	    (tiles[y2*w+x2] & LOCKED)) {
	    d = d2;
	} else {
	    /*
	     * Failing that, step left into the new square and look
	     * in front of us.
	     */
	    x = x2;
	    y = y2;
	    OFFSETWH(x2, y2, x, y, d, w, h);
	    if ((wrapping || (abs(x2-x) <= 1 && abs(y2-y) <= 1)) &&
		!(tiles[y2*w+x2] & LOCKED)) {
		/*
		 * And failing _that_, we're going to have to step
		 * forward into _that_ square and look right at the
		 * same locked square as we started with.
		 */
		x = x2;
		y = y2;
		d = C(d);
	    }
	}

    } while (x != startx || y != starty || d != startd);

    /*
     * Our technique for perturbing this ambiguous area is to
     * search round its edge for a join we can make: that is, an
     * edge on the perimeter which is (a) not currently connected,
     * and (b) connecting it would not yield a full cross on either
     * side. Then we make that join, search round the network to
     * find the loop thus constructed, and sever the loop at a
     * randomly selected other point.
     */
    perim2 = snewn(nperim, struct xyd);
    memcpy(perim2, perimeter, nperim * sizeof(struct xyd));
    /* Shuffle the perimeter, so as to search it without directional bias. */
    shuffle(perim2, nperim, sizeof(*perim2), rs);
    for (i = 0; i < nperim; i++) {
	int x2, y2;

	x = perim2[i].x;
	y = perim2[i].y;
	d = perim2[i].direction;

	OFFSETWH(x2, y2, x, y, d, w, h);
	if (!wrapping && (abs(x2-x) > 1 || abs(y2-y) > 1))
	    continue;            /* can't link across non-wrapping border */
	if (tiles[y*w+x] & d)
	    continue;		       /* already linked in this direction! */
	if (((tiles[y*w+x] | d) & 15) == 15)
	    continue;		       /* can't turn this tile into a cross */
	if (((tiles[y2*w+x2] | F(d)) & 15) == 15)
	    continue;		       /* can't turn other tile into a cross */

	/*
	 * We've found the point at which we're going to make a new
	 * link.
	 */
#ifdef PERTURB_DIAGNOSTICS	
	printf("linking %d,%d:%d\n", x, y, d);
#endif
	tiles[y*w+x] |= d;
	tiles[y2*w+x2] |= F(d);

	break;
    }
    sfree(perim2);

    if (i == nperim) {
        sfree(perimeter);
	return;			       /* nothing we can do! */
    }

    /*
     * Now we've constructed a new link, we need to find the entire
     * loop of which it is a part.
     * 
     * In principle, this involves doing a complete search round
     * the network. However, I anticipate that in the vast majority
     * of cases the loop will be quite small, so what I'm going to
     * do is make _two_ searches round the network in parallel, one
     * keeping its metaphorical hand on the left-hand wall while
     * the other keeps its hand on the right. As soon as one of
     * them gets back to its starting point, I abandon the other.
     */
    for (i = 0; i < 2; i++) {
	loopsize[i] = nloop[i] = 0;
	loop[i] = NULL;
	looppos[i].x = x;
	looppos[i].y = y;
	looppos[i].direction = d;
    }
    while (1) {
	for (i = 0; i < 2; i++) {
	    int x2, y2, j;

	    x = looppos[i].x;
	    y = looppos[i].y;
	    d = looppos[i].direction;

	    OFFSETWH(x2, y2, x, y, d, w, h);

	    /*
	     * Add this path segment to the loop, unless it exactly
	     * reverses the previous one on the loop in which case
	     * we take it away again.
	     */
#ifdef PERTURB_DIAGNOSTICS
	    printf("looppos[%d] = %d,%d:%d\n", i, x, y, d);
#endif
	    if (nloop[i] > 0 &&
		loop[i][nloop[i]-1].x == x2 &&
		loop[i][nloop[i]-1].y == y2 &&
		loop[i][nloop[i]-1].direction == F(d)) {
#ifdef PERTURB_DIAGNOSTICS
		printf("removing path segment %d,%d:%d from loop[%d]\n",
		       x2, y2, F(d), i);
#endif
		nloop[i]--;
	    } else {
		if (nloop[i] >= loopsize[i]) {
		    loopsize[i] = loopsize[i] * 3 / 2 + 32;
		    loop[i] = sresize(loop[i], loopsize[i], struct xyd);
		}
#ifdef PERTURB_DIAGNOSTICS
		printf("adding path segment %d,%d:%d to loop[%d]\n",
		       x, y, d, i);
#endif
		loop[i][nloop[i]++] = looppos[i];
	    }

#ifdef PERTURB_DIAGNOSTICS
	    printf("tile at new location is %x\n", tiles[y2*w+x2] & 0xF);
#endif
	    d = F(d);
	    for (j = 0; j < 4; j++) {
		if (i == 0)
		    d = A(d);
		else
		    d = C(d);
#ifdef PERTURB_DIAGNOSTICS
		printf("trying dir %d\n", d);
#endif
		if (tiles[y2*w+x2] & d) {
		    looppos[i].x = x2;
		    looppos[i].y = y2;
		    looppos[i].direction = d;
		    break;
		}
	    }

	    assert(j < 4);
	    assert(nloop[i] > 0);

	    if (looppos[i].x == loop[i][0].x &&
		looppos[i].y == loop[i][0].y &&
		looppos[i].direction == loop[i][0].direction) {
#ifdef PERTURB_DIAGNOSTICS
		printf("loop %d finished tracking\n", i);
#endif

		/*
		 * Having found our loop, we now sever it at a
		 * randomly chosen point - absolutely any will do -
		 * which is not the one we joined it at to begin
		 * with. Conveniently, the one we joined it at is
		 * loop[i][0], so we just avoid that one.
		 */
		j = random_upto(rs, nloop[i]-1) + 1;
		x = loop[i][j].x;
		y = loop[i][j].y;
		d = loop[i][j].direction;
		OFFSETWH(x2, y2, x, y, d, w, h);
		tiles[y*w+x] &= ~d;
		tiles[y2*w+x2] &= ~F(d);

		break;
	    }
	}
	if (i < 2)
	    break;
    }
    sfree(loop[0]);
    sfree(loop[1]);

    /*
     * Finally, we must mark the entire disputed section as locked,
     * to prevent the perturb function being called on it multiple
     * times.
     * 
     * To do this, we _sort_ the perimeter of the area. The
     * existing xyd_cmp function will arrange things into columns
     * for us, in such a way that each column has the edges in
     * vertical order. Then we can work down each column and fill
     * in all the squares between an up edge and a down edge.
     */
    qsort(perimeter, nperim, sizeof(struct xyd), xyd_cmp);
    x = y = -1;
    for (i = 0; i <= nperim; i++) {
	if (i == nperim || perimeter[i].x > x) {
	    /*
	     * Fill in everything from the last Up edge to the
	     * bottom of the grid, if necessary.
	     */
	    if (x != -1) {
		while (y < h) {
#ifdef PERTURB_DIAGNOSTICS
		    printf("resolved: locking tile %d,%d\n", x, y);
#endif
		    tiles[y * w + x] |= LOCKED;
		    y++;
		}
		x = y = -1;
	    }

	    if (i == nperim)
		break;

	    x = perimeter[i].x;
	    y = 0;
	}

	if (perimeter[i].direction == U) {
	    x = perimeter[i].x;
	    y = perimeter[i].y;
	} else if (perimeter[i].direction == D) {
	    /*
	     * Fill in everything from the last Up edge to here.
	     */
	    assert(x == perimeter[i].x && y <= perimeter[i].y);
	    while (y <= perimeter[i].y) {
#ifdef PERTURB_DIAGNOSTICS
		printf("resolved: locking tile %d,%d\n", x, y);
#endif
		tiles[y * w + x] |= LOCKED;
		y++;
	    }
	    x = y = -1;
	}
    }

    sfree(perimeter);
}

static int *compute_loops_inner(int w, int h, bool wrapping,
                                const unsigned char *tiles,
                                const unsigned char *barriers);

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    tree234 *possibilities, *barriertree;
    int w, h, x, y, cx, cy, nbarriers;
    unsigned char *tiles, *barriers;
    char *desc, *p;

    w = params->width;
    h = params->height;

    cx = w / 2;
    cy = h / 2;

    tiles = snewn(w * h, unsigned char);
    barriers = snewn(w * h, unsigned char);

    begin_generation:

    memset(tiles, 0, w * h);
    memset(barriers, 0, w * h);

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
    possibilities = newtree234(xyd_cmp_nc);

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

    if (params->unique) {
	int prevn = -1;

	/*
	 * Run the solver to check unique solubility.
	 */
	while (net_solver(w, h, tiles, NULL, params->wrapping) != 1) {
	    int n = 0;

	    /*
	     * We expect (in most cases) that most of the grid will
	     * be uniquely specified already, and the remaining
	     * ambiguous sections will be small and separate. So
	     * our strategy is to find each individual such
	     * section, and perform a perturbation on the network
	     * in that area.
	     */
	    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
		if (x+1 < w && ((tiles[y*w+x] ^ tiles[y*w+x+1]) & LOCKED)) {
		    n++;
		    if (tiles[y*w+x] & LOCKED)
			perturb(w, h, tiles, params->wrapping, rs, x+1, y, L);
		    else
			perturb(w, h, tiles, params->wrapping, rs, x, y, R);
		}
		if (y+1 < h && ((tiles[y*w+x] ^ tiles[(y+1)*w+x]) & LOCKED)) {
		    n++;
		    if (tiles[y*w+x] & LOCKED)
			perturb(w, h, tiles, params->wrapping, rs, x, y+1, U);
		    else
			perturb(w, h, tiles, params->wrapping, rs, x, y, D);
		}
	    }

	    /*
	     * Now n counts the number of ambiguous sections we
	     * have fiddled with. If we haven't managed to decrease
	     * it from the last time we ran the solver, give up and
	     * regenerate the entire grid.
	     */
	    if (prevn != -1 && prevn <= n)
		goto begin_generation; /* (sorry) */

	    prevn = n;
	}

	/*
	 * The solver will have left a lot of LOCKED bits lying
	 * around in the tiles array. Remove them.
	 */
	for (x = 0; x < w*h; x++)
	    tiles[x] &= ~LOCKED;
    }

    /*
     * Now compute a list of the possible barrier locations.
     */
    barriertree = newtree234(xyd_cmp_nc);
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

	solution = snewn(w * h + 1, char);
        for (i = 0; i < w * h; i++)
            solution[i] = "0123456789abcdef"[tiles[i] & 0xF];
        solution[w*h] = '\0';

	*aux = solution;
    }

    /*
     * Now shuffle the grid.
     * 
     * In order to avoid accidentally generating an already-solved
     * grid, we will reshuffle as necessary to ensure that at least
     * one edge has a mismatched connection.
     *
     * This can always be done, since validate_params() enforces a
     * grid area of at least 2 and our generator never creates
     * either type of rotationally invariant tile (cross and
     * blank). Hence there must be at least one edge separating
     * distinct tiles, and it must be possible to find orientations
     * of those tiles such that one tile is trying to connect
     * through that edge and the other is not.
     * 
     * (We could be more subtle, and allow the shuffle to generate
     * a grid in which all tiles match up locally and the only
     * criterion preventing the grid from being already solved is
     * connectedness. However, that would take more effort, and
     * it's easier to simply make sure every grid is _obviously_
     * not solved.)
     *
     * We also require that our shuffle produces no loops in the
     * initial grid state, because it's a bit rude to light up a 'HEY,
     * YOU DID SOMETHING WRONG!' indicator when the user hasn't even
     * had a chance to do _anything_ yet. This also is possible just
     * by retrying the whole shuffle on failure, because it's clear
     * that at least one non-solved shuffle with no loops must exist.
     * (Proof: take the _solved_ state of the puzzle, and rotate one
     * endpoint.)
     */
    while (1) {
        int mismatches, prev_loopsquares, this_loopsquares, i;
        int *loops;

      shuffle:
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int orig = index(params, tiles, x, y);
                int rot = random_upto(rs, 4);
                index(params, tiles, x, y) = ROT(orig, rot);
            }
        }

        /*
         * Check for loops, and try to fix them by reshuffling just
         * the squares involved.
         */
        prev_loopsquares = w*h+1;
        while (1) {
            loops = compute_loops_inner(w, h, params->wrapping, tiles, NULL);
            this_loopsquares = 0;
            for (i = 0; i < w*h; i++) {
                if (loops[i]) {
                    int orig = tiles[i];
                    int rot = random_upto(rs, 4);
                    tiles[i] = ROT(orig, rot);
                    this_loopsquares++;
                }
            }
            sfree(loops);
            if (this_loopsquares > prev_loopsquares) {
                /*
                 * We're increasing rather than reducing the number of
                 * loops. Give up and go back to the full shuffle.
                 */
                goto shuffle;
            }
            if (this_loopsquares == 0)
                break;
            prev_loopsquares = this_loopsquares;
        }

        mismatches = 0;
        /*
         * I can't even be bothered to check for mismatches across
         * a wrapping edge, so I'm just going to enforce that there
         * must be a mismatch across a non-wrapping edge, which is
         * still always possible.
         */
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            if (x+1 < w && ((ROT(index(params, tiles, x, y), 2) ^ 
                             index(params, tiles, x+1, y)) & L))
                mismatches++;
            if (y+1 < h && ((ROT(index(params, tiles, x, y), 2) ^ 
                             index(params, tiles, x, y+1)) & U))
                mismatches++;
        }

        if (mismatches == 0)
            continue;

        /* OK. */
        break;
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
    state->wrapping = params->wrapping;
    state->imm = snew(game_immutable_state);
    state->imm->refcount = 1;
    state->last_rotate_dir = state->last_rotate_x = state->last_rotate_y = 0;
    state->completed = state->used_solve = false;
    state->tiles = snewn(state->width * state->height, unsigned char);
    memset(state->tiles, 0, state->width * state->height);
    state->imm->barriers = snewn(state->width * state->height, unsigned char);
    memset(state->imm->barriers, 0, state->width * state->height);

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
    } else {
        /*
         * We check whether this is de-facto a non-wrapping game
         * despite the parameters, in case we were passed the
         * description of a non-wrapping game. This is so that we
         * can change some aspects of the UI behaviour.
         */
        state->wrapping = false;
        for (x = 0; x < state->width; x++)
            if (!(barrier(state, x, 0) & U) ||
                !(barrier(state, x, state->height-1) & D))
                state->wrapping = true;
        for (y = 0; y < state->height; y++)
            if (!(barrier(state, 0, y) & L) ||
                !(barrier(state, state->width-1, y) & R))
                state->wrapping = true;
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret;

    ret = snew(game_state);
    ret->imm = state->imm;
    ret->imm->refcount++;
    ret->width = state->width;
    ret->height = state->height;
    ret->wrapping = state->wrapping;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ret->last_rotate_dir = state->last_rotate_dir;
    ret->last_rotate_x = state->last_rotate_x;
    ret->last_rotate_y = state->last_rotate_y;
    ret->tiles = snewn(state->width * state->height, unsigned char);
    memcpy(ret->tiles, state->tiles, state->width * state->height);

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->imm->refcount == 0) {
        sfree(state->imm->barriers);
        sfree(state->imm);
    }
    sfree(state->tiles);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    unsigned char *tiles;
    char *ret;
    int retlen, retsize;
    int i;

    tiles = snewn(state->width * state->height, unsigned char);

    if (!aux) {
	/*
	 * Run the internal solver on the provided grid. This might
	 * not yield a complete solution.
	 */
        int solver_result;

	memcpy(tiles, state->tiles, state->width * state->height);
	solver_result = net_solver(state->width, state->height, tiles,
                                   state->imm->barriers, state->wrapping);

        if (solver_result < 0) {
            *error = "No solution exists for this puzzle";
            sfree(tiles);
            return NULL;
        }
    } else {
        for (i = 0; i < state->width * state->height; i++) {
            int c = aux[i];

            if (c >= '0' && c <= '9')
                tiles[i] = c - '0';
            else if (c >= 'a' && c <= 'f')
                tiles[i] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                tiles[i] = c - 'A' + 10;

	    tiles[i] |= LOCKED;
        }
    }

    /*
     * Now construct a string which can be passed to execute_move()
     * to transform the current grid into the solved one.
     */
    retsize = 256;
    ret = snewn(retsize, char);
    retlen = 0;
    ret[retlen++] = 'S';

    for (i = 0; i < state->width * state->height; i++) {
	int from = currstate->tiles[i], to = tiles[i];
	int ft = from & (R|L|U|D), tt = to & (R|L|U|D);
	int x = i % state->width, y = i / state->width;
	int chr = '\0';
	char buf[80], *p = buf;

	if (from == to)
	    continue;		       /* nothing needs doing at all */

	/*
	 * To transform this tile into the desired tile: first
	 * unlock the tile if it's locked, then rotate it if
	 * necessary, then lock it if necessary.
	 */
	if (from & LOCKED)
	    p += sprintf(p, ";L%d,%d", x, y);

	if (tt == A(ft))
	    chr = 'A';
	else if (tt == C(ft))
	    chr = 'C';
	else if (tt == F(ft))
	    chr = 'F';
	else {
	    assert(tt == ft);
	    chr = '\0';
	}
	if (chr)
	    p += sprintf(p, ";%c%d,%d", chr, x, y);

	if (to & LOCKED)
	    p += sprintf(p, ";L%d,%d", x, y);

	if (p > buf) {
	    if (retlen + (p - buf) >= retsize) {
		retsize = retlen + (p - buf) + 512;
		ret = sresize(ret, retsize, char);
	    }
	    memcpy(ret+retlen, buf, p - buf);
	    retlen += p - buf;
	}
    }

    assert(retlen < retsize);
    ret[retlen] = '\0';
    ret = sresize(ret, retlen+1, char);

    sfree(tiles);

    return ret;
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
 */
static unsigned char *compute_active(const game_state *state, int cx, int cy)
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
    todo = newtree234(xyd_cmp_nc);
    index(state, active, cx, cy) = ACTIVE;
    add234(todo, new_xyd(cx, cy, 0));

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
	    if ((tile(state, x1, y1) & d1) &&
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

struct net_neighbour_ctx {
    int w, h;
    const unsigned char *tiles, *barriers;
    int i, n, neighbours[4];
};
static int net_neighbour(int vertex, void *vctx)
{
    struct net_neighbour_ctx *ctx = (struct net_neighbour_ctx *)vctx;

    if (vertex >= 0) {
        int x = vertex % ctx->w, y = vertex / ctx->w;
        int tile, dir, x1, y1, v1;

        ctx->i = ctx->n = 0;

        tile = ctx->tiles[vertex];
        if (ctx->barriers)
            tile &= ~ctx->barriers[vertex];

        for (dir = 1; dir < 0x10; dir <<= 1) {
            if (!(tile & dir))
                continue;
            OFFSETWH(x1, y1, x, y, dir, ctx->w, ctx->h);
            v1 = y1 * ctx->w + x1;
            if (ctx->tiles[v1] & F(dir))
                ctx->neighbours[ctx->n++] = v1;
        }
    }

    if (ctx->i < ctx->n)
        return ctx->neighbours[ctx->i++];
    else
        return -1;
}

static int *compute_loops_inner(int w, int h, bool wrapping,
                                const unsigned char *tiles,
                                const unsigned char *barriers)
{
    struct net_neighbour_ctx ctx;
    struct findloopstate *fls;
    int *loops;
    int x, y;

    fls = findloop_new_state(w*h);
    ctx.w = w;
    ctx.h = h;
    ctx.tiles = tiles;
    ctx.barriers = barriers;
    findloop_run(fls, w*h, net_neighbour, &ctx);

    loops = snewn(w*h, int);

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int x1, y1, dir;
            int flags = 0;

            for (dir = 1; dir < 0x10; dir <<= 1) {
                if ((tiles[y*w+x] & dir) &&
                    !(barriers && (barriers[y*w+x] & dir))) {
                    OFFSETWH(x1, y1, x, y, dir, w, h);
                    if ((tiles[y1*w+x1] & F(dir)) &&
                        findloop_is_loop_edge(fls, y*w+x, y1*w+x1))
                        flags |= ERR(dir);
                }
            }
            loops[y*w+x] = flags;
        }
    }

    findloop_free_state(fls);
    return loops;
}

static int *compute_loops(const game_state *state)
{
    return compute_loops_inner(state->width, state->height, state->wrapping,
                               state->tiles, state->imm->barriers);
}

struct game_ui {
    int org_x, org_y; /* origin */
    int cx, cy;       /* source tile (game coordinates) */
    int cur_x, cur_y;
    bool cur_visible;
    random_state *rs; /* used for jumbling */
#ifdef USE_DRAGGING
    int dragtilex, dragtiley, dragstartx, dragstarty;
    bool dragged;
#endif
};

static game_ui *new_ui(const game_state *state)
{
    void *seed;
    int seedsize;
    game_ui *ui = snew(game_ui);
    ui->org_x = ui->org_y = 0;
    ui->cur_x = ui->cx = state->width / 2;
    ui->cur_y = ui->cy = state->height / 2;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    get_random_seed(&seed, &seedsize);
    ui->rs = random_new(seed, seedsize);
    sfree(seed);

    return ui;
}

static void free_ui(game_ui *ui)
{
    random_free(ui->rs);
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    char buf[120];
    /*
     * We preserve the origin and centre-point coordinates over a
     * serialise.
     */
    sprintf(buf, "O%d,%d;C%d,%d", ui->org_x, ui->org_y, ui->cx, ui->cy);
    return dupstr(buf);
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    sscanf(encoding, "O%d,%d;C%d,%d",
	   &ui->org_x, &ui->org_y, &ui->cx, &ui->cy);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (tile(state, ui->cur_x, ui->cur_y) & LOCKED) {
        if (button == CURSOR_SELECT2) return "Unlock";
    } else {
        if (button == CURSOR_SELECT) return "Rotate";
        if (button == CURSOR_SELECT2) return "Lock";
    }
    return "";
}

struct game_drawstate {
    int width, height;
    int tilesize;
    unsigned long *visible, *to_draw;
};

/* ----------------------------------------------------------------------
 * Process a move.
 */
static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    char *nullret;
    int tx = -1, ty = -1, dir = 0;
    bool shift = button & MOD_SHFT, ctrl = button & MOD_CTRL;
    enum {
        NONE, ROTATE_LEFT, ROTATE_180, ROTATE_RIGHT, TOGGLE_LOCK, JUMBLE,
        MOVE_ORIGIN, MOVE_SOURCE, MOVE_ORIGIN_AND_SOURCE, MOVE_CURSOR
    } action;

    button &= ~MOD_MASK;
    nullret = NULL;
    action = NONE;

    if (button == LEFT_BUTTON ||
	button == MIDDLE_BUTTON ||
#ifdef USE_DRAGGING
	button == LEFT_DRAG ||
	button == LEFT_RELEASE ||
	button == RIGHT_DRAG ||
	button == RIGHT_RELEASE ||
#endif
	button == RIGHT_BUTTON) {

	if (ui->cur_visible) {
	    ui->cur_visible = false;
	    nullret = UI_UPDATE;
	}

	/*
	 * The button must have been clicked on a valid tile.
	 */
	x -= WINDOW_OFFSET + LINE_THICK;
	y -= WINDOW_OFFSET + LINE_THICK;
	if (x < 0 || y < 0)
	    return nullret;
	tx = x / TILE_SIZE;
	ty = y / TILE_SIZE;
	if (tx >= state->width || ty >= state->height)
	    return nullret;
        /* Transform from physical to game coords */
        tx = (tx + ui->org_x) % state->width;
        ty = (ty + ui->org_y) % state->height;
	if (x % TILE_SIZE >= TILE_SIZE - LINE_THICK ||
	    y % TILE_SIZE >= TILE_SIZE - LINE_THICK)
	    return nullret;

#ifdef USE_DRAGGING

        if (button == MIDDLE_BUTTON
#ifdef STYLUS_BASED
	    || button == RIGHT_BUTTON  /* with a stylus, `right-click' locks */
#endif
	    ) {
            /*
             * Middle button never drags: it only toggles the lock.
             */
            action = TOGGLE_LOCK;
        } else if (button == LEFT_BUTTON
#ifndef STYLUS_BASED
                   || button == RIGHT_BUTTON /* (see above) */
#endif
                  ) {
            /*
             * Otherwise, we note down the start point for a drag.
             */
            ui->dragtilex = tx;
            ui->dragtiley = ty;
            ui->dragstartx = x % TILE_SIZE;
            ui->dragstarty = y % TILE_SIZE;
            ui->dragged = false;
            return nullret;            /* no actual action */
        } else if (button == LEFT_DRAG
#ifndef STYLUS_BASED
                   || button == RIGHT_DRAG
#endif
                  ) {
            /*
             * Find the new drag point and see if it necessitates a
             * rotation.
             */
            int x0,y0, xA,yA, xC,yC, xF,yF;
            int mx, my;
            int d0, dA, dC, dF, dmin;

            tx = ui->dragtilex;
            ty = ui->dragtiley;

            mx = x - (ui->dragtilex * TILE_SIZE);
            my = y - (ui->dragtiley * TILE_SIZE);

            x0 = ui->dragstartx;
            y0 = ui->dragstarty;
            xA = ui->dragstarty;
            yA = TILE_SIZE-1 - ui->dragstartx;
            xF = TILE_SIZE-1 - ui->dragstartx;
            yF = TILE_SIZE-1 - ui->dragstarty;
            xC = TILE_SIZE-1 - ui->dragstarty;
            yC = ui->dragstartx;

            d0 = (mx-x0)*(mx-x0) + (my-y0)*(my-y0);
            dA = (mx-xA)*(mx-xA) + (my-yA)*(my-yA);
            dF = (mx-xF)*(mx-xF) + (my-yF)*(my-yF);
            dC = (mx-xC)*(mx-xC) + (my-yC)*(my-yC);

            dmin = min(min(d0,dA),min(dF,dC));

            if (d0 == dmin) {
                return nullret;
            } else if (dF == dmin) {
                action = ROTATE_180;
                ui->dragstartx = xF;
                ui->dragstarty = yF;
                ui->dragged = true;
            } else if (dA == dmin) {
                action = ROTATE_LEFT;
                ui->dragstartx = xA;
                ui->dragstarty = yA;
                ui->dragged = true;
            } else /* dC == dmin */ {
                action = ROTATE_RIGHT;
                ui->dragstartx = xC;
                ui->dragstarty = yC;
                ui->dragged = true;
            }
        } else if (button == LEFT_RELEASE
#ifndef STYLUS_BASED
                   || button == RIGHT_RELEASE
#endif
                  ) {
            if (!ui->dragged) {
                /*
                 * There was a click but no perceptible drag:
                 * revert to single-click behaviour.
                 */
                tx = ui->dragtilex;
                ty = ui->dragtiley;

                if (button == LEFT_RELEASE)
                    action = ROTATE_LEFT;
                else
                    action = ROTATE_RIGHT;
            } else
                return nullret;        /* no action */
        }

#else /* USE_DRAGGING */

	action = (button == LEFT_BUTTON ? ROTATE_LEFT :
		  button == RIGHT_BUTTON ? ROTATE_RIGHT : TOGGLE_LOCK);

#endif /* USE_DRAGGING */

    } else if (IS_CURSOR_MOVE(button)) {
        switch (button) {
          case CURSOR_UP:       dir = U; break;
          case CURSOR_DOWN:     dir = D; break;
          case CURSOR_LEFT:     dir = L; break;
          case CURSOR_RIGHT:    dir = R; break;
          default:              return nullret;
        }
        if (shift && ctrl) action = MOVE_ORIGIN_AND_SOURCE;
        else if (shift)    action = MOVE_ORIGIN;
        else if (ctrl)     action = MOVE_SOURCE;
        else               action = MOVE_CURSOR;
    } else if (button == 'a' || button == 's' || button == 'd' ||
	       button == 'A' || button == 'S' || button == 'D' ||
               button == 'f' || button == 'F' ||
               IS_CURSOR_SELECT(button)) {
	tx = ui->cur_x;
	ty = ui->cur_y;
	if (button == 'a' || button == 'A' || button == CURSOR_SELECT)
	    action = ROTATE_LEFT;
	else if (button == 's' || button == 'S' || button == CURSOR_SELECT2)
	    action = TOGGLE_LOCK;
	else if (button == 'd' || button == 'D')
	    action = ROTATE_RIGHT;
        else if (button == 'f' || button == 'F')
            action = ROTATE_180;
        ui->cur_visible = true;
    } else if (button == 'j' || button == 'J') {
	/* XXX should we have some mouse control for this? */
	action = JUMBLE;
    } else
	return nullret;

    /*
     * The middle button locks or unlocks a tile. (A locked tile
     * cannot be turned, and is visually marked as being locked.
     * This is a convenience for the player, so that once they are
     * sure which way round a tile goes, they can lock it and thus
     * avoid forgetting later on that they'd already done that one;
     * and the locking also prevents them turning the tile by
     * accident. If they change their mind, another middle click
     * unlocks it.)
     */
    if (action == TOGGLE_LOCK) {
	char buf[80];
	sprintf(buf, "L%d,%d", tx, ty);
	return dupstr(buf);
    } else if (action == ROTATE_LEFT || action == ROTATE_RIGHT ||
               action == ROTATE_180) {
	char buf[80];

        /*
         * The left and right buttons have no effect if clicked on a
         * locked tile.
         */
        if (tile(state, tx, ty) & LOCKED)
            return nullret;

        /*
         * Otherwise, turn the tile one way or the other. Left button
         * turns anticlockwise; right button turns clockwise.
         */
	sprintf(buf, "%c%d,%d", (int)(action == ROTATE_LEFT ? 'A' :
                                      action == ROTATE_RIGHT ? 'C' : 'F'), tx, ty);
	return dupstr(buf);
    } else if (action == JUMBLE) {
        /*
         * Jumble all unlocked tiles to random orientations.
         */

        int jx, jy, maxlen;
	char *ret, *p;

	/*
	 * Maximum string length assumes no int can be converted to
	 * decimal and take more than 11 digits!
	 */
	maxlen = state->width * state->height * 25 + 3;

	ret = snewn(maxlen, char);
	p = ret;
	*p++ = 'J';

        for (jy = 0; jy < state->height; jy++) {
            for (jx = 0; jx < state->width; jx++) {
                if (!(tile(state, jx, jy) & LOCKED)) {
                    int rot = random_upto(ui->rs, 4);
		    if (rot) {
			p += sprintf(p, ";%c%d,%d", "AFC"[rot-1], jx, jy);
		    }
                }
            }
        }
	*p++ = '\0';
	assert(p - ret < maxlen);
	ret = sresize(ret, p - ret, char);

	return ret;
    } else if (action == MOVE_ORIGIN || action == MOVE_SOURCE ||
               action == MOVE_ORIGIN_AND_SOURCE || action == MOVE_CURSOR) {
        assert(dir != 0);
        if (action == MOVE_ORIGIN || action == MOVE_ORIGIN_AND_SOURCE) {
            if (state->wrapping) {
                 OFFSET(ui->org_x, ui->org_y, ui->org_x, ui->org_y, dir, state);
            } else return nullret; /* disallowed for non-wrapping grids */
        }
        if (action == MOVE_SOURCE || action == MOVE_ORIGIN_AND_SOURCE) {
            OFFSET(ui->cx, ui->cy, ui->cx, ui->cy, dir, state);
        }
        if (action == MOVE_CURSOR) {
            OFFSET(ui->cur_x, ui->cur_y, ui->cur_x, ui->cur_y, dir, state);
            ui->cur_visible = true;
        }
        return UI_UPDATE;
    } else {
	return NULL;
    }
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    int tx = -1, ty = -1, n, orig;
    bool noanim;

    ret = dup_game(from);

    if (move[0] == 'J' || move[0] == 'S') {
	if (move[0] == 'S')
	    ret->used_solve = true;

	move++;
	if (*move == ';')
	    move++;
	noanim = true;
    } else
	noanim = false;

    ret->last_rotate_dir = 0;	       /* suppress animation */
    ret->last_rotate_x = ret->last_rotate_y = 0;

    while (*move) {
	if ((move[0] == 'A' || move[0] == 'C' ||
	     move[0] == 'F' || move[0] == 'L') &&
	    sscanf(move+1, "%d,%d%n", &tx, &ty, &n) >= 2 &&
	    tx >= 0 && tx < from->width && ty >= 0 && ty < from->height) {
	    orig = tile(ret, tx, ty);
	    if (move[0] == 'A') {
		tile(ret, tx, ty) = A(orig);
		if (!noanim)
		    ret->last_rotate_dir = +1;
	    } else if (move[0] == 'F') {
		tile(ret, tx, ty) = F(orig);
		if (!noanim)
                    ret->last_rotate_dir = +2; /* + for sake of argument */
	    } else if (move[0] == 'C') {
		tile(ret, tx, ty) = C(orig);
		if (!noanim)
		    ret->last_rotate_dir = -1;
	    } else {
		assert(move[0] == 'L');
		tile(ret, tx, ty) ^= LOCKED;
	    }

	    move += 1 + n;
	    if (*move == ';') move++;
	} else {
	    free_game(ret);
	    return NULL;
	}
    }
    if (!noanim) {
        if (tx == -1 || ty == -1) { free_game(ret); return NULL; }
	ret->last_rotate_x = tx;
	ret->last_rotate_y = ty;
    }

    /*
     * Check whether the game has been completed.
     * 
     * For this purpose it doesn't matter where the source square is,
     * because we can start from anywhere (or, at least, any square
     * that's non-empty!), and correctly determine whether the game is
     * completed.
     */
    {
	unsigned char *active;
	int pos;
        bool complete = true;

	for (pos = 0; pos < ret->width * ret->height; pos++)
            if (ret->tiles[pos] & 0xF)
                break;

        if (pos < ret->width * ret->height) {
            active = compute_active(ret, pos % ret->width, pos / ret->width);

            for (pos = 0; pos < ret->width * ret->height; pos++)
                if ((ret->tiles[pos] & 0xF) && !active[pos]) {
		    complete = false;
                    break;
                }

            sfree(active);
        }

	if (complete)
	    ret->completed = true;
    }

    return ret;
}


/* ----------------------------------------------------------------------
 * Routines for drawing the game position on the screen.
 */

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);
    int i, ncells;

    ds->width = state->width;
    ds->height = state->height;
    ncells = (state->width+2) * (state->height+2);
    ds->visible = snewn(ncells, unsigned long);
    ds->to_draw = snewn(ncells, unsigned long);
    ds->tilesize = 0;                  /* undecided yet */
    for (i = 0; i < ncells; i++)
        ds->visible[i] = -1;

    return ds;
}

#define dsindex(ds, field, x, y) ((ds)->field[((y)+1)*((ds)->width+2)+((x)+1)])
#define visible(ds, x, y) dsindex(ds, visible, x, y)
#define todraw(ds, x, y) dsindex(ds, to_draw, x, y)

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds->to_draw);
    sfree(ds);
}

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = WINDOW_OFFSET * 2 + TILE_SIZE * params->width + LINE_THICK;
    *y = WINDOW_OFFSET * 2 + TILE_SIZE * params->height + LINE_THICK;
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
     * Highlighted errors are red as well.
     */
    ret[COL_ERR * 3 + 0] = 1.0F;
    ret[COL_ERR * 3 + 1] = 0.0F;
    ret[COL_ERR * 3 + 2] = 0.0F;

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
     * Locked tiles are a grey in between those two.
     */
    ret[COL_LOCKED * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_LOCKED * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_LOCKED * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    return ret;
}

static void rotated_coords(float *ox, float *oy, const float matrix[4],
                           float cx, float cy, float ix, float iy)
{
    *ox = matrix[0] * ix + matrix[2] * iy + cx;
    *oy = matrix[1] * ix + matrix[3] * iy + cy;
}

/* Flags describing the visible features of a tile. */
#define TILE_BARRIER_SHIFT            0  /* 4 bits: R U L D */
#define TILE_BARRIER_CORNER_SHIFT     4  /* 4 bits: RU UL LD DR */
#define TILE_KEYBOARD_CURSOR      (1<<8) /* 1 bit if cursor is here */
#define TILE_WIRE_SHIFT               9  /* 8 bits: RR UU LL DD
                                          * Each pair: 0=no wire, 1=unpowered,
                                          * 2=powered, 3=error highlight */
#define TILE_ENDPOINT_SHIFT          17  /* 2 bits: 0=no endpoint, 1=unpowered,
                                          * 2=powered, 3=power-source square */
#define TILE_WIRE_ON_EDGE_SHIFT      19  /* 8 bits: RR UU LL DD,
                                          * same encoding as TILE_WIRE_SHIFT */
#define TILE_ROTATING          (1UL<<27) /* 1 bit if tile is rotating */
#define TILE_LOCKED            (1UL<<28) /* 1 bit if tile is locked */

static void draw_wires(drawing *dr, int cx, int cy, int radius,
                       unsigned long tile, int bitmap,
                       int colour, int halfwidth, const float matrix[4])
{
    float fpoints[12*2];
    int points[12*2];
    int npoints, d, dsh, i;
    bool any_wire_this_colour = false;
    float xf, yf;

    npoints = 0;
    for (d = 1, dsh = 0; d < 16; d *= 2, dsh++) {
        int wiretype = (tile >> (TILE_WIRE_SHIFT + 2*dsh)) & 3;

        fpoints[2*npoints+0] = halfwidth * (X(d) + X(C(d)));
        fpoints[2*npoints+1] = halfwidth * (Y(d) + Y(C(d)));
        npoints++;

        if (bitmap & (1 << wiretype)) {
            fpoints[2*npoints+0] = radius * X(d) + halfwidth * X(C(d));
            fpoints[2*npoints+1] = radius * Y(d) + halfwidth * Y(C(d));
            npoints++;
            fpoints[2*npoints+0] = radius * X(d) + halfwidth * X(A(d));
            fpoints[2*npoints+1] = radius * Y(d) + halfwidth * Y(A(d));
            npoints++;

            any_wire_this_colour = true;
        }
    }

    if (!any_wire_this_colour)
        return;

    for (i = 0; i < npoints; i++) {
        rotated_coords(&xf, &yf, matrix, cx, cy, fpoints[2*i], fpoints[2*i+1]);
        points[2*i] = 0.5F + xf;
        points[2*i+1] = 0.5F + yf;
    }

    draw_polygon(dr, points, npoints, colour, colour);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y,
                      unsigned long tile, float angle)
{
    int tx, ty;
    int clipx, clipy, clipX, clipY, clipw, cliph;
    int border_br = LINE_THICK/2, border_tl = LINE_THICK - border_br;
    int barrier_outline_thick = (LINE_THICK+1)/2;
    int bg, d, dsh, pass;
    int cx, cy, radius;
    float matrix[4];

    tx = WINDOW_OFFSET + TILE_SIZE * x + border_br;
    ty = WINDOW_OFFSET + TILE_SIZE * y + border_br;

    /*
     * Clip to the tile boundary, with adjustments if we're drawing
     * just outside the grid.
     */
    clipx = tx; clipX = tx + TILE_SIZE;
    clipy = ty; clipY = ty + TILE_SIZE;
    if (x == -1) {
        clipx = clipX - border_br - barrier_outline_thick;
    } else if (x == ds->width) {
        clipX = clipx + border_tl + barrier_outline_thick;
    }
    if (y == -1) {
        clipy = clipY - border_br - barrier_outline_thick;
    } else if (y == ds->height) {
        clipY = clipy + border_tl + barrier_outline_thick;
    }
    clipw = clipX - clipx;
    cliph = clipY - clipy;
    clip(dr, clipx, clipy, clipw, cliph);

    /*
     * Clear the clip region.
     */
    bg = (tile & TILE_LOCKED) ? COL_LOCKED : COL_BACKGROUND;
    draw_rect(dr, clipx, clipy, clipw, cliph, bg);

    /*
     * Draw the grid lines.
     */
    {
        int gridl = (x == -1 ? tx+TILE_SIZE-border_br : tx);
        int gridr = (x == ds->width ? tx+border_tl : tx+TILE_SIZE);
        int gridu = (y == -1 ? ty+TILE_SIZE-border_br : ty);
        int gridd = (y == ds->height ? ty+border_tl : ty+TILE_SIZE);
        if (x >= 0)
            draw_rect(dr, tx, gridu, border_tl, gridd-gridu, COL_BORDER);
        if (y >= 0)
            draw_rect(dr, gridl, ty, gridr-gridl, border_tl, COL_BORDER);
        if (x < ds->width)
            draw_rect(dr, tx+TILE_SIZE-border_br, gridu,
                      border_br, gridd-gridu, COL_BORDER);
        if (y < ds->height)
            draw_rect(dr, gridl, ty+TILE_SIZE-border_br,
                      gridr-gridl, border_br, COL_BORDER);
    }

    /*
     * Draw the keyboard cursor.
     */
    if (tile & TILE_KEYBOARD_CURSOR) {
        int cursorcol = (tile & TILE_LOCKED) ? COL_BACKGROUND : COL_LOCKED;
        int inset_outer = TILE_SIZE/8, inset_inner = inset_outer + LINE_THICK;
        draw_rect(dr, tx + inset_outer, ty + inset_outer,
                  TILE_SIZE - 2*inset_outer, TILE_SIZE - 2*inset_outer,
                  cursorcol);
        draw_rect(dr, tx + inset_inner, ty + inset_inner,
                  TILE_SIZE - 2*inset_inner, TILE_SIZE - 2*inset_inner,
                  bg);
    }

    radius = (TILE_SIZE+1)/2;
    cx = tx + radius;
    cy = ty + radius;
    radius++;

    /*
     * Draw protrusions into this cell's edges of wires in
     * neighbouring cells, as given by the TILE_WIRE_ON_EDGE_SHIFT
     * flags. We only draw each of these if there _isn't_ a wire of
     * our own that's going to overlap it, which means either the
     * corresponding TILE_WIRE_SHIFT flag is zero, or else the
     * TILE_ROTATING flag is set (so that our main wire won't be drawn
     * in quite that place anyway).
     */
    for (d = 1, dsh = 0; d < 16; d *= 2, dsh++) {
        int edgetype = ((tile >> (TILE_WIRE_ON_EDGE_SHIFT + 2*dsh)) & 3);
        if (edgetype == 0)
            continue;             /* there isn't a wire on the edge */
        if (!(tile & TILE_ROTATING) &&
            ((tile >> (TILE_WIRE_SHIFT + 2*dsh)) & 3) != 0)
            continue;     /* wire on edge would be overdrawn anyway */

        for (pass = 0; pass < 2; pass++) {
            int x, y, w, h;
            int col = (pass == 0 || edgetype == 1 ? COL_WIRE :
                       edgetype == 2 ? COL_POWERED : COL_ERR);
            int halfwidth = pass == 0 ? 2*LINE_THICK-1 : LINE_THICK-1;

            if (X(d) < 0) {
                x = tx;
                w = border_tl;
            } else if (X(d) > 0) {
                x = tx + TILE_SIZE - border_br;
                w = border_br;
            } else {
                x = cx - halfwidth;
                w = 2 * halfwidth + 1;
            }

            if (Y(d) < 0) {
                y = ty;
                h = border_tl;
            } else if (Y(d) > 0) {
                y = ty + TILE_SIZE - border_br;
                h = border_br;
            } else {
                y = cy - halfwidth;
                h = 2 * halfwidth + 1;
            }

            draw_rect(dr, x, y, w, h, col);
        }
    }

    /*
     * Set up the rotation matrix for the main cell contents, i.e.
     * everything that is centred in the grid square and optionally
     * rotated by an arbitrary angle about that centre point.
     */
    if (tile & TILE_ROTATING) {
        matrix[0] = (float)cos(angle * (float)PI / 180.0F);
        matrix[2] = (float)sin(angle * (float)PI / 180.0F);
    } else {
        matrix[0] = 1.0F;
        matrix[2] = 0.0F;
    }
    matrix[3] = matrix[0];
    matrix[1] = -matrix[2];

    /*
     * Draw the wires.
     */
    draw_wires(dr, cx, cy, radius, tile,
               0xE, COL_WIRE, 2*LINE_THICK-1, matrix);
    draw_wires(dr, cx, cy, radius, tile,
               0x4, COL_POWERED, LINE_THICK-1, matrix);
    draw_wires(dr, cx, cy, radius, tile,
               0x8, COL_ERR, LINE_THICK-1, matrix);

    /*
     * Draw the central box.
     */
    for (pass = 0; pass < 2; pass++) {
        int endtype = (tile >> TILE_ENDPOINT_SHIFT) & 3;
        if (endtype) {
            int i, points[8], col;
            float boxr = TILE_SIZE * 0.24F + (pass == 0 ? LINE_THICK-1 : 0);

            col = (pass == 0 || endtype == 3 ? COL_WIRE :
                   endtype == 2 ? COL_POWERED : COL_ENDPOINT);

            points[0] = +1; points[1] = +1;
            points[2] = +1; points[3] = -1;
            points[4] = -1; points[5] = -1;
            points[6] = -1; points[7] = +1;

            for (i = 0; i < 8; i += 2) {
                float x, y;
                rotated_coords(&x, &y, matrix, cx, cy,
                               boxr * points[i], boxr * points[i+1]);
                points[i] = x + 0.5F;
                points[i+1] = y + 0.5F;
            }

            draw_polygon(dr, points, 4, col, COL_WIRE);
        }
    }

    /*
     * Draw barriers along grid edges.
     */
    for (pass = 0; pass < 2; pass++) {
        int btl = border_tl, bbr = border_br, col = COL_BARRIER;
        if (pass == 0) {
            btl += barrier_outline_thick;
            bbr += barrier_outline_thick;
            col = COL_WIRE;
        }

        if (tile & (L << TILE_BARRIER_SHIFT))
            draw_rect(dr, tx, ty, btl, TILE_SIZE, col);
        if (tile & (R << TILE_BARRIER_SHIFT))
            draw_rect(dr, tx+TILE_SIZE-bbr, ty, bbr, TILE_SIZE, col);
        if (tile & (U << TILE_BARRIER_SHIFT))
            draw_rect(dr, tx, ty, TILE_SIZE, btl, col);
        if (tile & (D << TILE_BARRIER_SHIFT))
            draw_rect(dr, tx, ty+TILE_SIZE-bbr, TILE_SIZE, bbr, col);

        if (tile & (R << TILE_BARRIER_CORNER_SHIFT))
            draw_rect(dr, tx+TILE_SIZE-bbr, ty, bbr, btl, col);
        if (tile & (U << TILE_BARRIER_CORNER_SHIFT))
            draw_rect(dr, tx, ty, btl, btl, col);
        if (tile & (L << TILE_BARRIER_CORNER_SHIFT))
            draw_rect(dr, tx, ty+TILE_SIZE-bbr, btl, bbr, col);
        if (tile & (D << TILE_BARRIER_CORNER_SHIFT))
            draw_rect(dr, tx+TILE_SIZE-bbr, ty+TILE_SIZE-bbr, bbr, bbr, col);
    }

    /*
     * Unclip and draw update, to finish.
     */
    unclip(dr);
    draw_update(dr, clipx, clipy, clipw, cliph);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float t, float ft)
{
    int tx, ty, dx, dy, d, dsh, last_rotate_dir, frame;
    unsigned char *active;
    int *loops;
    float angle = 0.0;

    tx = ty = -1;
    last_rotate_dir = dir==-1 ? oldstate->last_rotate_dir :
                                state->last_rotate_dir;
    if (oldstate && (t < ROTATE_TIME) && last_rotate_dir) {
        /*
         * We're animating a single tile rotation. Find the turning
         * tile.
         */
        tx = (dir==-1 ? oldstate->last_rotate_x : state->last_rotate_x);
        ty = (dir==-1 ? oldstate->last_rotate_y : state->last_rotate_y);
        angle = last_rotate_dir * dir * 90.0F * (t / ROTATE_TIME);
        state = oldstate;
    }

    if (ft > 0) {
        /*
         * We're animating a completion flash. Find which frame
         * we're at.
         */
        frame = (int)(ft / FLASH_FRAME);
    } else {
        frame = 0;
    }

    /*
     * Build up a map of what we want every tile to look like. We
     * include tiles one square outside the grid, for the outer edges
     * of barriers.
     */
    active = compute_active(state, ui->cx, ui->cy);
    loops = compute_loops(state);

    for (dy = -1; dy < ds->height+1; dy++) {
        for (dx = -1; dx < ds->width+1; dx++) {
            todraw(ds, dx, dy) = 0;
        }
    }

    for (dy = 0; dy < ds->height; dy++) {
        int gy = (dy + ui->org_y) % ds->height;
        for (dx = 0; dx < ds->width; dx++) {
            int gx = (dx + ui->org_x) % ds->width;
            int t = (tile(state, gx, gy) |
                     index(state, loops, gx, gy) |
                     index(state, active, gx, gy));

            for (d = 1, dsh = 0; d < 16; d *= 2, dsh++) {
                if (barrier(state, gx, gy) & d) {
                    todraw(ds, dx, dy) |=
                        d << TILE_BARRIER_SHIFT;
                    todraw(ds, dx + X(d), dy + Y(d)) |=
                        F(d) << TILE_BARRIER_SHIFT;
                    todraw(ds, dx + X(A(d)), dy + Y(A(d))) |=
                        C(d) << TILE_BARRIER_CORNER_SHIFT;
                    todraw(ds, dx + X(A(d)) + X(d), dy + Y(A(d)) + Y(d)) |=
                        F(d) << TILE_BARRIER_CORNER_SHIFT;
                    todraw(ds, dx + X(C(d)), dy + Y(C(d))) |=
                        d << TILE_BARRIER_CORNER_SHIFT;
                    todraw(ds, dx + X(C(d)) + X(d), dy + Y(C(d)) + Y(d)) |=
                        A(d) << TILE_BARRIER_CORNER_SHIFT;
                }

                if (t & d) {
                    int edgeval;

                    /* Highlight as an error any edge in a locked tile that
                     * is adjacent to a lack-of-edge in another locked tile,
                     * or to a barrier */
                    if (t & LOCKED) {
                        if (barrier(state, gx, gy) & d) {
                            t |= ERR(d);
                        } else {
                            int ox, oy, t2;
                            OFFSET(ox, oy, gx, gy, d, state);
                            t2 = tile(state, ox, oy);
                            if ((t2 & LOCKED) && !(t2 & F(d))) {
                                t |= ERR(d);
                            }
                        }
                    }

                    edgeval = (t & ERR(d) ? 3 : t & ACTIVE ? 2 : 1);
                    todraw(ds, dx, dy) |= edgeval << (TILE_WIRE_SHIFT + dsh*2);
                    if (!(gx == tx && gy == ty)) {
                        todraw(ds, dx + X(d), dy + Y(d)) |=
                            edgeval << (TILE_WIRE_ON_EDGE_SHIFT + (dsh ^ 2)*2);
                    }
                }
            }

            if (ui->cur_visible && gx == ui->cur_x && gy == ui->cur_y)
                todraw(ds, dx, dy) |= TILE_KEYBOARD_CURSOR;

            if (gx == tx && gy == ty)
                todraw(ds, dx, dy) |= TILE_ROTATING;

            if (gx == ui->cx && gy == ui->cy) {
                todraw(ds, dx, dy) |= 3 << TILE_ENDPOINT_SHIFT;
            } else if ((t & 0xF) != R && (t & 0xF) != U && 
                       (t & 0xF) != L && (t & 0xF) != D) {
                /* this is not an endpoint tile */
            } else if (t & ACTIVE) {
                todraw(ds, dx, dy) |= 2 << TILE_ENDPOINT_SHIFT;
            } else {
                todraw(ds, dx, dy) |= 1 << TILE_ENDPOINT_SHIFT;
            }

            if (t & LOCKED)
                todraw(ds, dx, dy) |= TILE_LOCKED;

            /*
             * In a completion flash, we adjust the LOCKED bit
             * depending on our distance from the centre point and
             * the frame number.
             */
            if (frame >= 0) {
                int rcx = (ui->cx + ds->width - ui->org_x) % ds->width;
                int rcy = (ui->cy + ds->height - ui->org_y) % ds->height;
                int xdist, ydist, dist;
                xdist = (dx < rcx ? rcx - dx : dx - rcx);
                ydist = (dy < rcy ? rcy - dy : dy - rcy);
                dist = (xdist > ydist ? xdist : ydist);

                if (frame >= dist && frame < dist+4 &&
                    ((frame - dist) & 1))
                    todraw(ds, dx, dy) ^= TILE_LOCKED;
            }
        }
    }

    /*
     * Now draw any tile that differs from the way it was last drawn.
     * An exception is that if either the previous _or_ current state
     * has the TILE_ROTATING bit set, we must draw it regardless,
     * because it will have rotated to a different angle.q
     */
    for (dy = -1; dy < ds->height+1; dy++) {
        for (dx = -1; dx < ds->width+1; dx++) {
            int prev = visible(ds, dx, dy);
            int curr = todraw(ds, dx, dy);
            if (prev != curr || ((prev | curr) & TILE_ROTATING) != 0) {
                draw_tile(dr, ds, dx, dy, curr, angle);
                visible(ds, dx, dy) = curr;
            }
        }
    }

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256], *p;
	int i, n, n2, a;
        bool complete = false;

        p = statusbuf;
        *p = '\0';     /* ensure even an empty status string is terminated */

        if (state->used_solve) {
            p += sprintf(p, "Auto-solved. ");
            complete = true;
        } else if (state->completed) {
            p += sprintf(p, "COMPLETED! ");
            complete = true;
        }

        /*
         * Omit the 'Active: n/N' counter completely if the source
         * tile is a completely empty one, because then the active
         * count can't help but read '1'.
         */
        if (tile(state, ui->cx, ui->cy) & 0xF) {
            n = state->width * state->height;
            for (i = a = n2 = 0; i < n; i++) {
                if (active[i])
                    a++;
                if (state->tiles[i] & 0xF)
                    n2++;
            }

            /*
             * Also, if we're displaying a completion indicator and
             * the game is still in its completed state (i.e. every
             * tile is active), we might as well omit this too.
             */
            if (!complete || a < n2)
                p += sprintf(p, "Active: %d/%d", a, n2);
        }

	status_bar(dr, statusbuf);
    }

    sfree(active);
    sfree(loops);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    int last_rotate_dir;

    /*
     * Don't animate if last_rotate_dir is zero.
     */
    last_rotate_dir = dir==-1 ? oldstate->last_rotate_dir :
                                newstate->last_rotate_dir;
    if (last_rotate_dir)
        return ROTATE_TIME;

    return 0.0F;
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
        int size = 0;
        if (size < newstate->width)
            size = newstate->width;
        if (size < newstate->height)
            size = newstate->height;
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
        *x = WINDOW_OFFSET + TILE_SIZE * ui->cur_x;
        *y = WINDOW_OFFSET + TILE_SIZE * ui->cur_y;

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
     * I'll use 8mm squares by default.
     */
    game_compute_size(params, 800, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void draw_diagram(drawing *dr, game_drawstate *ds, int x, int y,
			 bool topleft, int v, bool drawlines, int ink)
{
    int tx, ty, cx, cy, r, br, k, thick;

    tx = WINDOW_OFFSET + TILE_SIZE * x;
    ty = WINDOW_OFFSET + TILE_SIZE * y;

    /*
     * Find our centre point.
     */
    if (topleft) {
	cx = tx + (v & L ? TILE_SIZE / 4 : TILE_SIZE / 6);
	cy = ty + (v & U ? TILE_SIZE / 4 : TILE_SIZE / 6);
	r = TILE_SIZE / 8;
	br = TILE_SIZE / 32;
    } else {
	cx = tx + TILE_SIZE / 2;
	cy = ty + TILE_SIZE / 2;
	r = TILE_SIZE / 2;
	br = TILE_SIZE / 8;
    }
    thick = r / 20;

    /*
     * Draw the square block if we have an endpoint.
     */
    if (v == 1 || v == 2 || v == 4 || v == 8)
	draw_rect(dr, cx - br, cy - br, br*2, br*2, ink);

    /*
     * Draw each radial line.
     */
    if (drawlines) {
	for (k = 1; k < 16; k *= 2)
	    if (v & k) {
		int x1 = min(cx, cx + (r-thick) * X(k));
		int x2 = max(cx, cx + (r-thick) * X(k));
		int y1 = min(cy, cy + (r-thick) * Y(k));
		int y2 = max(cy, cy + (r-thick) * Y(k));
		draw_rect(dr, x1 - thick, y1 - thick,
			  (x2 - x1) + 2*thick, (y2 - y1) + 2*thick, ink);
	    }
    }
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->width, h = state->height;
    int ink = print_mono_colour(dr, 0);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, TILE_SIZE / (state->wrapping ? 128 : 12));
    draw_rect_outline(dr, WINDOW_OFFSET, WINDOW_OFFSET,
		      TILE_SIZE * w, TILE_SIZE * h, ink);

    /*
     * Grid.
     */
    print_line_width(dr, TILE_SIZE / 128);
    for (x = 1; x < w; x++)
	draw_line(dr, WINDOW_OFFSET + TILE_SIZE * x, WINDOW_OFFSET,
		  WINDOW_OFFSET + TILE_SIZE * x, WINDOW_OFFSET + TILE_SIZE * h,
		  ink);
    for (y = 1; y < h; y++)
	draw_line(dr, WINDOW_OFFSET, WINDOW_OFFSET + TILE_SIZE * y,
		  WINDOW_OFFSET + TILE_SIZE * w, WINDOW_OFFSET + TILE_SIZE * y,
		  ink);

    /*
     * Barriers.
     */
    for (y = 0; y <= h; y++)
	for (x = 0; x <= w; x++) {
	    int b = barrier(state, x % w, y % h);
	    if (x < w && (b & U))
		draw_rect(dr, WINDOW_OFFSET + TILE_SIZE * x - TILE_SIZE/24,
			  WINDOW_OFFSET + TILE_SIZE * y - TILE_SIZE/24,
			  TILE_SIZE + TILE_SIZE/24 * 2, TILE_SIZE/24 * 2, ink);
	    if (y < h && (b & L))
		draw_rect(dr, WINDOW_OFFSET + TILE_SIZE * x - TILE_SIZE/24,
			  WINDOW_OFFSET + TILE_SIZE * y - TILE_SIZE/24,
			  TILE_SIZE/24 * 2, TILE_SIZE + TILE_SIZE/24 * 2, ink);
	}

    /*
     * Grid contents.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    int vx, v = tile(state, x, y);
	    int locked = v & LOCKED;

	    v &= 0xF;

	    /*
	     * Rotate into a standard orientation for the top left
	     * corner diagram.
	     */
	    vx = v;
	    while (vx != 0 && vx != 15 && vx != 1 && vx != 9 && vx != 13 &&
		   vx != 5)
		vx = A(vx);

	    /*
	     * Draw the top left corner diagram.
	     */
	    draw_diagram(dr, ds, x, y, true, vx, true, ink);

	    /*
	     * Draw the real solution diagram, if we're doing so.
	     */
	    draw_diagram(dr, ds, x, y, false, v, locked, ink);
	}
}

#ifdef COMBINED
#define thegame net
#endif

const struct game thegame = {
    "Net", "games.net", "net",
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
    true, false, game_print_size, game_print,
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};
