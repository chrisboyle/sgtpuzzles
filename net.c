/*
 * net.c: Net game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

#define PI 3.141592653589793238462643383279502884197169399

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
#define LOCKED 0x10
#define ACTIVE 0x20
/* Corner flags go in the barriers array */
#define RU 0x10
#define UL 0x20
#define LD 0x40
#define DR 0x80

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

#define TILE_SIZE 32
#define TILE_BORDER 1
#define WINDOW_OFFSET 16

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
    NCOLOURS
};

struct game_params {
    int width;
    int height;
    int wrapping;
    int unique;
    float barrier_probability;
};

struct game_aux_info {
    int width, height;
    unsigned char *tiles;
};

struct game_state {
    int width, height, cx, cy, wrapping, completed;
    int last_rotate_x, last_rotate_y, last_rotate_dir;
    int used_solve, just_used_solve;
    unsigned char *tiles;
    unsigned char *barriers;
};

#define OFFSETWH(x2,y2,x1,y1,dir,width,height) \
    ( (x2) = ((x1) + width + X((dir))) % width, \
      (y2) = ((y1) + height + Y((dir))) % height)

#define OFFSET(x2,y2,x1,y1,dir,state) \
	OFFSETWH(x2,y2,x1,y1,dir,(state)->width,(state)->height)

#define index(state, a, x, y) ( a[(y) * (state)->width + (x)] )
#define tile(state, x, y)     index(state, (state)->tiles, x, y)
#define barrier(state, x, y)  index(state, (state)->barriers, x, y)

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
};

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
    ret->wrapping = FALSE;
    ret->unique = TRUE;
    ret->barrier_probability = 0.0;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];
    static const struct { int x, y, wrap; } values[] = {
        {5, 5, FALSE},
        {7, 7, FALSE},
        {9, 9, FALSE},
        {11, 11, FALSE},
        {13, 11, FALSE},
        {5, 5, TRUE},
        {7, 7, TRUE},
        {9, 9, TRUE},
        {11, 11, TRUE},
        {13, 11, TRUE},
    };

    if (i < 0 || i >= lenof(values))
        return FALSE;

    ret = snew(game_params);
    ret->width = values[i].x;
    ret->height = values[i].y;
    ret->wrapping = values[i].wrap;
    ret->unique = TRUE;
    ret->barrier_probability = 0.0;

    sprintf(str, "%dx%d%s", ret->width, ret->height,
            ret->wrapping ? " wrapping" : "");

    *name = dupstr(str);
    *params = ret;
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
	    ret->wrapping = TRUE;
	} else if (*p == 'b') {
	    p++;
            ret->barrier_probability = atof(p);
	    while (*p && (*p == '.' || isdigit((unsigned char)*p))) p++;
	} else if (*p == 'a') {
            p++;
	    ret->unique = FALSE;
	} else
	    p++;		       /* skip any other gunk */
    }
}

static char *encode_params(game_params *params, int full)
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

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->width);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->height);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "Walls wrap around";
    ret[2].type = C_BOOLEAN;
    ret[2].sval = NULL;
    ret[2].ival = params->wrapping;

    ret[3].name = "Barrier probability";
    ret[3].type = C_STRING;
    sprintf(buf, "%g", params->barrier_probability);
    ret[3].sval = dupstr(buf);
    ret[3].ival = 0;

    ret[4].name = "Ensure unique solution";
    ret[4].type = C_BOOLEAN;
    ret[4].sval = NULL;
    ret[4].ival = params->unique;

    ret[5].name = NULL;
    ret[5].type = C_END;
    ret[5].sval = NULL;
    ret[5].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->width = atoi(cfg[0].sval);
    ret->height = atoi(cfg[1].sval);
    ret->wrapping = cfg[2].ival;
    ret->barrier_probability = (float)atof(cfg[3].sval);
    ret->unique = cfg[4].ival;

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->width <= 0 && params->height <= 0)
	return "Width and height must both be greater than zero";
    if (params->width <= 0)
	return "Width must be greater than zero";
    if (params->height <= 0)
	return "Height must be greater than zero";
    if (params->width <= 1 && params->height <= 1)
	return "At least one of width and height must be greater than one";
    if (params->barrier_probability < 0)
	return "Barrier probability may not be negative";
    if (params->barrier_probability > 1)
	return "Barrier probability may not be greater than 1";
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

static int dsf_canonify(int *dsf, int val)
{
    int v2 = val;

    while (dsf[val] != val)
	val = dsf[val];

    while (v2 != val) {
	int tmp = dsf[v2];
	dsf[v2] = val;
	v2 = tmp;
    }

    return val;
}

static void dsf_merge(int *dsf, int v1, int v2)
{
    v1 = dsf_canonify(dsf, v1);
    v2 = dsf_canonify(dsf, v2);
    dsf[v2] = v1;
}

struct todo {
    unsigned char *marked;
    int *buffer;
    int buflen;
    int head, tail;
};

static struct todo *todo_new(int maxsize)
{
    struct todo *todo = snew(struct todo);
    todo->marked = snewn(maxsize, unsigned char);
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
    todo->marked[index] = TRUE;
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
    todo->marked[ret] = FALSE;

    return ret;
}

static int net_solver(int w, int h, unsigned char *tiles,
		      unsigned char *barriers, int wrapping)
{
    unsigned char *tilestate;
    unsigned char *edgestate;
    int *deadends;
    int *equivalence;
    struct todo *todo;
    int i, j, x, y;
    int area;
    int done_something;

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
    equivalence = snewn(w * h, int);
    for (i = 0; i < w*h; i++)
	equivalence[i] = i;	       /* initially all distinct */

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
    done_something = TRUE;	       /* prevent instant termination! */
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
	     * I also set `done_something' to FALSE at this point;
	     * if we later come back here and find it still FALSE,
	     * we will know we've scanned the entire grid without
	     * finding anything new to do, and we can terminate.
	     */
	    if (!done_something)
		break;
	    for (i = 0; i < w*h; i++)
		todo_add(todo, i);
	    done_something = FALSE;

	    index = todo_get(todo);
	}

	y = index / w;
	x = index % w;
	{
	    int d, ourclass = dsf_canonify(equivalence, y*w+x);
	    int deadendmax[9];

	    deadendmax[1] = deadendmax[2] = deadendmax[4] = deadendmax[8] = 0;

	    for (i = j = 0; i < 4 && tilestate[(y*w+x) * 4 + i] != 255; i++) {
		int valid;
		int nnondeadends, nondeadends[4], deadendtotal;
		int nequiv, equiv[5];
		int val = tilestate[(y*w+x) * 4 + i];

		valid = TRUE;
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
			valid = FALSE;

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
				valid = FALSE;
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
			valid = FALSE;
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

	    assert(j > 0);	       /* we can't lose _all_ possibilities! */

	    if (j < i) {
		done_something = TRUE;

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
			    done_something = TRUE;
			    todo_add(todo, y2*w+x2);
			} else if (!(o & d)) {
			    /* This edge is closed in all orientations. */
#ifdef SOLVER_DIAGNOSTICS
			    printf("marking edge %d,%d:%d closed\n", x, y, d);
#endif
			    edgestate[(y*w+x) * 5 + d] = 2;
			    edgestate[(y2*w+x2) * 5 + d2] = 2;
			    done_something = TRUE;
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
		    done_something = TRUE;
		    todo_add(todo, y2*w+x2);
		}
	    }

	}
    }

    /*
     * Mark all completely determined tiles as locked.
     */
    j = TRUE;
    for (i = 0; i < w*h; i++) {
	if (tilestate[i * 4 + 1] == 255) {
	    assert(tilestate[i * 4 + 0] != 255);
	    tiles[i] = tilestate[i * 4] | LOCKED;
	} else {
	    tiles[i] &= ~LOCKED;
	    j = FALSE;
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
static void perturb(int w, int h, unsigned char *tiles, int wrapping,
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
    for (i = nperim; --i ;) {
	int j = random_upto(rs, i+1);
	struct xyd t;

	t = perim2[j];
	perim2[j] = perim2[i];
	perim2[i] = t;
    }
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

    if (i == nperim)
	return;			       /* nothing we can do! */

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

static char *new_game_desc(game_params *params, random_state *rs,
			   game_aux_info **aux)
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
#ifdef DEBUG
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
#ifdef DEBUG
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
#ifdef DEBUG
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

#ifdef DEBUG
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
	while (!net_solver(w, h, tiles, NULL, params->wrapping)) {
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
     * Save the unshuffled grid in an aux_info.
     */
    {
	game_aux_info *solution;

	solution = snew(game_aux_info);
	solution->width = w;
	solution->height = h;
	solution->tiles = snewn(w * h, unsigned char);
	memcpy(solution->tiles, tiles, w * h);

	*aux = solution;
    }

    /*
     * Now shuffle the grid.
     */
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    int orig = index(params, tiles, x, y);
	    int rot = random_upto(rs, 4);
	    index(params, tiles, x, y) = ROT(orig, rot);
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

static void game_free_aux_info(game_aux_info *aux)
{
    sfree(aux->tiles);
    sfree(aux);
}

static char *validate_desc(game_params *params, char *desc)
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

static game_state *new_game(game_params *params, char *desc)
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
    state->last_rotate_dir = state->last_rotate_x = state->last_rotate_y = 0;
    state->completed = state->used_solve = state->just_used_solve = FALSE;
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
                int corner = FALSE;

                if (!(barrier(state, x, y) & dir))
                    continue;

                if (barrier(state, x, y) & dir2)
                    corner = TRUE;

                x1 = x + X(dir), y1 = y + Y(dir);
                if (x1 >= 0 && x1 < state->width &&
                    y1 >= 0 && y1 < state->height &&
                    (barrier(state, x1, y1) & dir2))
                    corner = TRUE;

                x2 = x + X(dir2), y2 = y + Y(dir2);
                if (x2 >= 0 && x2 < state->width &&
                    y2 >= 0 && y2 < state->height &&
                    (barrier(state, x2, y2) & dir))
                    corner = TRUE;

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

static game_state *dup_game(game_state *state)
{
    game_state *ret;

    ret = snew(game_state);
    ret->width = state->width;
    ret->height = state->height;
    ret->cx = state->cx;
    ret->cy = state->cy;
    ret->wrapping = state->wrapping;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ret->just_used_solve = state->just_used_solve;
    ret->last_rotate_dir = state->last_rotate_dir;
    ret->last_rotate_x = state->last_rotate_x;
    ret->last_rotate_y = state->last_rotate_y;
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

static game_state *solve_game(game_state *state, game_aux_info *aux,
			      char **error)
{
    game_state *ret;

    if (!aux) {
	/*
	 * Run the internal solver on the provided grid. This might
	 * not yield a complete solution.
	 */
	ret = dup_game(state);
	net_solver(ret->width, ret->height, ret->tiles,
		   ret->barriers, ret->wrapping);
    } else {
	assert(aux->width == state->width);
	assert(aux->height == state->height);
	ret = dup_game(state);
	memcpy(ret->tiles, aux->tiles, ret->width * ret->height);
	ret->used_solve = ret->just_used_solve = TRUE;
	ret->completed = TRUE;
    }

    return ret;
}

static char *game_text_format(game_state *state)
{
    return NULL;
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
static unsigned char *compute_active(game_state *state)
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

struct game_ui {
    int cur_x, cur_y;
    int cur_visible;
    random_state *rs; /* used for jumbling */
};

static game_ui *new_ui(game_state *state)
{
    void *seed;
    int seedsize;
    game_ui *ui = snew(game_ui);
    ui->cur_x = state->width / 2;
    ui->cur_y = state->height / 2;
    ui->cur_visible = FALSE;
    get_random_seed(&seed, &seedsize);
    ui->rs = random_init(seed, seedsize);
    sfree(seed);

    return ui;
}

static void free_ui(game_ui *ui)
{
    random_free(ui->rs);
    sfree(ui);
}

/* ----------------------------------------------------------------------
 * Process a move.
 */
static game_state *make_move(game_state *state, game_ui *ui,
			     int x, int y, int button)
{
    game_state *ret, *nullret;
    int tx, ty, orig;

    nullret = NULL;

    if (button == LEFT_BUTTON ||
	button == MIDDLE_BUTTON ||
	button == RIGHT_BUTTON) {

	if (ui->cur_visible) {
	    ui->cur_visible = FALSE;
	    nullret = state;
	}

	/*
	 * The button must have been clicked on a valid tile.
	 */
	x -= WINDOW_OFFSET + TILE_BORDER;
	y -= WINDOW_OFFSET + TILE_BORDER;
	if (x < 0 || y < 0)
	    return nullret;
	tx = x / TILE_SIZE;
	ty = y / TILE_SIZE;
	if (tx >= state->width || ty >= state->height)
	    return nullret;
	if (x % TILE_SIZE >= TILE_SIZE - TILE_BORDER ||
	    y % TILE_SIZE >= TILE_SIZE - TILE_BORDER)
	    return nullret;
    } else if (button == CURSOR_UP || button == CURSOR_DOWN ||
	       button == CURSOR_RIGHT || button == CURSOR_LEFT) {
	if (button == CURSOR_UP && ui->cur_y > 0)
	    ui->cur_y--;
	else if (button == CURSOR_DOWN && ui->cur_y < state->height-1)
	    ui->cur_y++;
	else if (button == CURSOR_LEFT && ui->cur_x > 0)
	    ui->cur_x--;
	else if (button == CURSOR_RIGHT && ui->cur_x < state->width-1)
	    ui->cur_x++;
	else
	    return nullret;	       /* no cursor movement */
	ui->cur_visible = TRUE;
	return state;		       /* UI activity has occurred */
    } else if (button == 'a' || button == 's' || button == 'd' ||
	       button == 'A' || button == 'S' || button == 'D') {
	tx = ui->cur_x;
	ty = ui->cur_y;
	if (button == 'a' || button == 'A')
	    button = LEFT_BUTTON;
	else if (button == 's' || button == 'S')
	    button = MIDDLE_BUTTON;
	else if (button == 'd' || button == 'D')
	    button = RIGHT_BUTTON;
        ui->cur_visible = TRUE;
    } else if (button == 'j' || button == 'J') {
	/* XXX should we have some mouse control for this? */
	button = 'J';   /* canonify */
	tx = ty = -1;   /* shut gcc up :( */
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
    if (button == MIDDLE_BUTTON) {

	ret = dup_game(state);
	ret->just_used_solve = FALSE;
	tile(ret, tx, ty) ^= LOCKED;
	ret->last_rotate_dir = ret->last_rotate_x = ret->last_rotate_y = 0;
	return ret;

    } else if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {

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
        ret = dup_game(state);
	ret->just_used_solve = FALSE;
        orig = tile(ret, tx, ty);
        if (button == LEFT_BUTTON) {
            tile(ret, tx, ty) = A(orig);
            ret->last_rotate_dir = +1;
        } else {
            tile(ret, tx, ty) = C(orig);
            ret->last_rotate_dir = -1;
        }
        ret->last_rotate_x = tx;
        ret->last_rotate_y = ty;

    } else if (button == 'J') {

        /*
         * Jumble all unlocked tiles to random orientations.
         */
        int jx, jy;
        ret = dup_game(state);
	ret->just_used_solve = FALSE;
        for (jy = 0; jy < ret->height; jy++) {
            for (jx = 0; jx < ret->width; jx++) {
                if (!(tile(ret, jx, jy) & LOCKED)) {
                    int rot = random_upto(ui->rs, 4);
                    orig = tile(ret, jx, jy);
                    tile(ret, jx, jy) = ROT(orig, rot);
                }
            }
        }
        ret->last_rotate_dir = 0; /* suppress animation */
        ret->last_rotate_x = ret->last_rotate_y = 0;

    } else assert(0);

    /*
     * Check whether the game has been completed.
     */
    {
	unsigned char *active = compute_active(ret);
	int x1, y1;
	int complete = TRUE;

	for (x1 = 0; x1 < ret->width; x1++)
	    for (y1 = 0; y1 < ret->height; y1++)
		if ((tile(ret, x1, y1) & 0xF) && !index(ret, active, x1, y1)) {
		    complete = FALSE;
		    goto break_label;  /* break out of two loops at once */
		}
	break_label:

	sfree(active);

	if (complete)
	    ret->completed = TRUE;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Routines for drawing the game position on the screen.
 */

struct game_drawstate {
    int started;
    int width, height;
    unsigned char *visible;
};

static game_drawstate *game_new_drawstate(game_state *state)
{
    game_drawstate *ds = snew(game_drawstate);

    ds->started = FALSE;
    ds->width = state->width;
    ds->height = state->height;
    ds->visible = snewn(state->width * state->height, unsigned char);
    memset(ds->visible, 0xFF, state->width * state->height);

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

static void game_size(game_params *params, int *x, int *y)
{
    *x = WINDOW_OFFSET * 2 + TILE_SIZE * params->width + TILE_BORDER;
    *y = WINDOW_OFFSET * 2 + TILE_SIZE * params->height + TILE_BORDER;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
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
     * Locked tiles are a grey in between those two.
     */
    ret[COL_LOCKED * 3 + 0] = 0.75F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_LOCKED * 3 + 1] = 0.75F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_LOCKED * 3 + 2] = 0.75F * ret[COL_BACKGROUND * 3 + 2];

    return ret;
}

static void draw_thick_line(frontend *fe, int x1, int y1, int x2, int y2,
                            int colour)
{
    draw_line(fe, x1-1, y1, x2-1, y2, COL_WIRE);
    draw_line(fe, x1+1, y1, x2+1, y2, COL_WIRE);
    draw_line(fe, x1, y1-1, x2, y2-1, COL_WIRE);
    draw_line(fe, x1, y1+1, x2, y2+1, COL_WIRE);
    draw_line(fe, x1, y1, x2, y2, colour);
}

static void draw_rect_coords(frontend *fe, int x1, int y1, int x2, int y2,
                             int colour)
{
    int mx = (x1 < x2 ? x1 : x2);
    int my = (y1 < y2 ? y1 : y2);
    int dx = (x2 + x1 - 2*mx + 1);
    int dy = (y2 + y1 - 2*my + 1);

    draw_rect(fe, mx, my, dx, dy, colour);
}

static void draw_barrier_corner(frontend *fe, int x, int y, int dir, int phase)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, dx, dy, dir2;

    dir >>= 4;

    dir2 = A(dir);
    dx = X(dir) + X(dir2);
    dy = Y(dir) + Y(dir2);
    x1 = (dx > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);
    y1 = (dy > 0 ? TILE_SIZE+TILE_BORDER-1 : 0);

    if (phase == 0) {
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-TILE_BORDER*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_WIRE);
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-TILE_BORDER*dy,
                         COL_WIRE);
    } else {
        draw_rect_coords(fe, bx+x1, by+y1,
                         bx+x1-(TILE_BORDER-1)*dx, by+y1-(TILE_BORDER-1)*dy,
                         COL_BARRIER);
    }
}

static void draw_barrier(frontend *fe, int x, int y, int dir, int phase)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    int x1, y1, w, h;

    x1 = (X(dir) > 0 ? TILE_SIZE : X(dir) == 0 ? TILE_BORDER : 0);
    y1 = (Y(dir) > 0 ? TILE_SIZE : Y(dir) == 0 ? TILE_BORDER : 0);
    w = (X(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);
    h = (Y(dir) ? TILE_BORDER : TILE_SIZE - TILE_BORDER);

    if (phase == 0) {
        draw_rect(fe, bx+x1-X(dir), by+y1-Y(dir), w, h, COL_WIRE);
    } else {
        draw_rect(fe, bx+x1, by+y1, w, h, COL_BARRIER);
    }
}

static void draw_tile(frontend *fe, game_state *state, int x, int y, int tile,
                      float angle, int cursor)
{
    int bx = WINDOW_OFFSET + TILE_SIZE * x;
    int by = WINDOW_OFFSET + TILE_SIZE * y;
    float matrix[4];
    float cx, cy, ex, ey, tx, ty;
    int dir, col, phase;

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
    draw_rect(fe, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER,
              COL_BORDER);
    draw_rect(fe, bx+TILE_BORDER, by+TILE_BORDER,
              TILE_SIZE-TILE_BORDER, TILE_SIZE-TILE_BORDER,
              tile & LOCKED ? COL_LOCKED : COL_BACKGROUND);

    /*
     * Draw an inset outline rectangle as a cursor, in whichever of
     * COL_LOCKED and COL_BACKGROUND we aren't currently drawing
     * in.
     */
    if (cursor) {
	draw_line(fe, bx+TILE_SIZE/8, by+TILE_SIZE/8,
		  bx+TILE_SIZE/8, by+TILE_SIZE-TILE_SIZE/8,
		  tile & LOCKED ? COL_BACKGROUND : COL_LOCKED);
	draw_line(fe, bx+TILE_SIZE/8, by+TILE_SIZE/8,
		  bx+TILE_SIZE-TILE_SIZE/8, by+TILE_SIZE/8,
		  tile & LOCKED ? COL_BACKGROUND : COL_LOCKED);
	draw_line(fe, bx+TILE_SIZE-TILE_SIZE/8, by+TILE_SIZE/8,
		  bx+TILE_SIZE-TILE_SIZE/8, by+TILE_SIZE-TILE_SIZE/8,
		  tile & LOCKED ? COL_BACKGROUND : COL_LOCKED);
	draw_line(fe, bx+TILE_SIZE/8, by+TILE_SIZE-TILE_SIZE/8,
		  bx+TILE_SIZE-TILE_SIZE/8, by+TILE_SIZE-TILE_SIZE/8,
		  tile & LOCKED ? COL_BACKGROUND : COL_LOCKED);
    }

    /*
     * Set up the rotation matrix.
     */
    matrix[0] = (float)cos(angle * PI / 180.0);
    matrix[1] = (float)-sin(angle * PI / 180.0);
    matrix[2] = (float)sin(angle * PI / 180.0);
    matrix[3] = (float)cos(angle * PI / 180.0);

    /*
     * Draw the wires.
     */
    cx = cy = TILE_BORDER + (TILE_SIZE-TILE_BORDER) / 2.0F - 0.5F;
    col = (tile & ACTIVE ? COL_POWERED : COL_WIRE);
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            MATMUL(tx, ty, matrix, ex, ey);
            draw_thick_line(fe, bx+(int)cx, by+(int)cy,
			    bx+(int)(cx+tx), by+(int)(cy+ty),
                            COL_WIRE);
        }
    }
    for (dir = 1; dir < 0x10; dir <<= 1) {
        if (tile & dir) {
            ex = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * X(dir);
            ey = (TILE_SIZE - TILE_BORDER - 1.0F) / 2.0F * Y(dir);
            MATMUL(tx, ty, matrix, ex, ey);
            draw_line(fe, bx+(int)cx, by+(int)cy,
		      bx+(int)(cx+tx), by+(int)(cy+ty), col);
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
            MATMUL(tx, ty, matrix, ex, ey);
            points[i] = bx+(int)(cx+tx);
            points[i+1] = by+(int)(cy+ty);
        }

        draw_polygon(fe, points, 4, TRUE, col);
        draw_polygon(fe, points, 4, FALSE, COL_WIRE);
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

        if (angle == 0.0 && (tile & dir)) {
            /*
             * If we are fully connected to the other tile, we must
             * draw right across the tile border. (We can use our
             * own ACTIVE state to determine what colour to do this
             * in: if we are fully connected to the other tile then
             * the two ACTIVE states will be the same.)
             */
            draw_rect_coords(fe, px-vx, py-vy, px+lx+vx, py+ly+vy, COL_WIRE);
            draw_rect_coords(fe, px, py, px+lx, py+ly,
                             (tile & ACTIVE) ? COL_POWERED : COL_WIRE);
        } else {
            /*
             * The other tile extends into our border, but isn't
             * actually connected to us. Just draw a single black
             * dot.
             */
            draw_rect_coords(fe, px, py, px, py, COL_WIRE);
        }
    }

    /*
     * Draw barrier corners, and then barriers.
     */
    for (phase = 0; phase < 2; phase++) {
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & (dir << 4))
                draw_barrier_corner(fe, x, y, dir << 4, phase);
        for (dir = 1; dir < 0x10; dir <<= 1)
            if (barrier(state, x, y) & dir)
                draw_barrier(fe, x, y, dir, phase);
    }

    draw_update(fe, bx, by, TILE_SIZE+TILE_BORDER, TILE_SIZE+TILE_BORDER);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, int dir, game_ui *ui, float t, float ft)
{
    int x, y, tx, ty, frame, last_rotate_dir;
    unsigned char *active;
    float angle = 0.0;

    /*
     * Clear the screen and draw the exterior barrier lines if this
     * is our first call.
     */
    if (!ds->started) {
        int phase;

        ds->started = TRUE;

        draw_rect(fe, 0, 0, 
                  WINDOW_OFFSET * 2 + TILE_SIZE * state->width + TILE_BORDER,
                  WINDOW_OFFSET * 2 + TILE_SIZE * state->height + TILE_BORDER,
                  COL_BACKGROUND);
        draw_update(fe, 0, 0, 
                    WINDOW_OFFSET*2 + TILE_SIZE*state->width + TILE_BORDER,
                    WINDOW_OFFSET*2 + TILE_SIZE*state->height + TILE_BORDER);

        for (phase = 0; phase < 2; phase++) {

            for (x = 0; x < ds->width; x++) {
                if (barrier(state, x, 0) & UL)
                    draw_barrier_corner(fe, x, -1, LD, phase);
                if (barrier(state, x, 0) & RU)
                    draw_barrier_corner(fe, x, -1, DR, phase);
                if (barrier(state, x, 0) & U)
                    draw_barrier(fe, x, -1, D, phase);
                if (barrier(state, x, ds->height-1) & DR)
                    draw_barrier_corner(fe, x, ds->height, RU, phase);
                if (barrier(state, x, ds->height-1) & LD)
                    draw_barrier_corner(fe, x, ds->height, UL, phase);
                if (barrier(state, x, ds->height-1) & D)
                    draw_barrier(fe, x, ds->height, U, phase);
            }

            for (y = 0; y < ds->height; y++) {
                if (barrier(state, 0, y) & UL)
                    draw_barrier_corner(fe, -1, y, RU, phase);
                if (barrier(state, 0, y) & LD)
                    draw_barrier_corner(fe, -1, y, DR, phase);
                if (barrier(state, 0, y) & L)
                    draw_barrier(fe, -1, y, R, phase);
                if (barrier(state, ds->width-1, y) & RU)
                    draw_barrier_corner(fe, ds->width, y, UL, phase);
                if (barrier(state, ds->width-1, y) & DR)
                    draw_barrier_corner(fe, ds->width, y, LD, phase);
                if (barrier(state, ds->width-1, y) & R)
                    draw_barrier(fe, ds->width, y, L, phase);
            }
        }
    }

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
    active = compute_active(state);

    for (x = 0; x < ds->width; x++)
        for (y = 0; y < ds->height; y++) {
            unsigned char c = tile(state, x, y) | index(state, active, x, y);

            /*
             * In a completion flash, we adjust the LOCKED bit
             * depending on our distance from the centre point and
             * the frame number.
             */
            if (frame >= 0) {
                int xdist, ydist, dist;
                xdist = (x < state->cx ? state->cx - x : x - state->cx);
                ydist = (y < state->cy ? state->cy - y : y - state->cy);
                dist = (xdist > ydist ? xdist : ydist);

                if (frame >= dist && frame < dist+4) {
                    int lock = (frame - dist) & 1;
                    lock = lock ? LOCKED : 0;
                    c = (c &~ LOCKED) | lock;
                }
            }

            if (index(state, ds->visible, x, y) != c ||
                index(state, ds->visible, x, y) == 0xFF ||
                (x == tx && y == ty) ||
		(ui->cur_visible && x == ui->cur_x && y == ui->cur_y)) {
                draw_tile(fe, state, x, y, c,
                          (x == tx && y == ty ? angle : 0.0F),
			  (ui->cur_visible && x == ui->cur_x && y == ui->cur_y));
                if ((x == tx && y == ty) ||
		    (ui->cur_visible && x == ui->cur_x && y == ui->cur_y))
                    index(state, ds->visible, x, y) = 0xFF;
                else
                    index(state, ds->visible, x, y) = c;
            }
        }

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];
	int i, n, n2, a;

	n = state->width * state->height;
	for (i = a = n2 = 0; i < n; i++) {
	    if (active[i])
		a++;
            if (state->tiles[i] & 0xF)
                n2++;
        }

	sprintf(statusbuf, "%sActive: %d/%d",
		(state->used_solve ? "Auto-solved. " :
		 state->completed ? "COMPLETED! " : ""), a, n2);

	status_bar(fe, statusbuf);
    }

    sfree(active);
}

static float game_anim_length(game_state *oldstate,
			      game_state *newstate, int dir)
{
    int last_rotate_dir;

    /*
     * Don't animate an auto-solve move.
     */
    if ((dir > 0 && newstate->just_used_solve) ||
       (dir < 0 && oldstate->just_used_solve))
       return 0.0F;

    /*
     * Don't animate if last_rotate_dir is zero.
     */
    last_rotate_dir = dir==-1 ? oldstate->last_rotate_dir :
                                newstate->last_rotate_dir;
    if (last_rotate_dir)
        return ROTATE_TIME;

    return 0.0F;
}

static float game_flash_length(game_state *oldstate,
			       game_state *newstate, int dir)
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

static int game_wants_statusbar(void)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame net
#endif

const struct game thegame = {
    "Net", "games.net",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    game_free_aux_info,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    make_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
};
