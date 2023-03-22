/*
 * mines.c: Minesweeper clone with sophisticated grid generation.
 * 
 * Still TODO:
 *
 *  - think about configurably supporting question marks. Once,
 *    that is, we've thought about configurability in general!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "tree234.h"
#include "puzzles.h"

enum {
    COL_BACKGROUND, COL_BACKGROUND2,
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8,
    COL_MINE, COL_BANG, COL_CROSS, COL_FLAG, COL_FLAGBASE, COL_QUERY,
    COL_HIGHLIGHT, COL_LOWLIGHT,
    COL_WRONGNUMBER,
    COL_CURSOR,
    NCOLOURS
};

#define PREFERRED_TILE_SIZE 20
#define TILE_SIZE (ds->tilesize)
#ifdef SMALL_SCREEN
#define BORDER 8
#else
#define BORDER (TILE_SIZE * 3 / 2)
#endif
#define HIGHLIGHT_WIDTH (TILE_SIZE / 10 ? TILE_SIZE / 10 : 1)
#define OUTER_HIGHLIGHT_WIDTH (BORDER / 10 ? BORDER / 10 : 1)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define FLASH_FRAME 0.13F

struct game_params {
    int w, h, n;
    bool unique;
};

struct mine_layout {
    /*
     * This structure is shared between all the game_states for a
     * given instance of the puzzle, so we reference-count it.
     */
    int refcount;
    bool *mines;
    /*
     * If we haven't yet actually generated the mine layout, here's
     * all the data we will need to do so.
     */
    int n;
    bool unique;
    random_state *rs;
    midend *me;		       /* to give back the new game desc */
};

struct game_state {
    int w, h, n;
    bool dead, won, used_solve;
    struct mine_layout *layout;	       /* real mine positions */
    signed char *grid;			       /* player knowledge */
    /*
     * Each item in the `grid' array is one of the following values:
     * 
     * 	- 0 to 8 mean the square is open and has a surrounding mine
     * 	  count.
     * 
     *  - -1 means the square is marked as a mine.
     * 
     *  - -2 means the square is unknown.
     * 
     * 	- -3 means the square is marked with a question mark
     * 	  (FIXME: do we even want to bother with this?).
     * 
     * 	- 64 means the square has had a mine revealed when the game
     * 	  was lost.
     * 
     * 	- 65 means the square had a mine revealed and this was the
     * 	  one the player hits.
     * 
     * 	- 66 means the square has a crossed-out mine because the
     * 	  player had incorrectly marked it.
     */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 9;
    ret->n = 10;
    ret->unique = true;

    return ret;
}

static const struct game_params mines_presets[] = {
  {9, 9, 10, true},
  {9, 9, 35, true},
  {16, 16, 40, true},
  {16, 16, 99, true},
#ifndef SMALL_SCREEN
  {30, 16, 99, true},
  {30, 16, 170, true},
#endif
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(mines_presets))
        return false;

    ret = snew(game_params);
    *ret = mines_presets[i];

    sprintf(str, "%dx%d, %d mines", ret->w, ret->h, ret->n);

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

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        params->h = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    } else {
        params->h = params->w;
    }
    if (*p == 'n') {
	p++;
	params->n = atoi(p);
	while (*p && (*p == '.' || isdigit((unsigned char)*p))) p++;
    } else {
        if (params->h > 0 && params->w > 0 &&
            params->w <= INT_MAX / params->h)
            params->n = params->w * params->h / 10;
    }

    while (*p) {
	if (*p == 'a') {
            p++;
	    params->unique = false;
	} else
	    p++;		       /* skip any other gunk */
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[400];
    int len;

    len = sprintf(ret, "%dx%d", params->w, params->h);
    /*
     * Mine count is a generation-time parameter, since it can be
     * deduced from the mine bitmap!
     */
    if (full)
	len += sprintf(ret+len, "n%d", params->n);
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

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Mines";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Ensure solubility";
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
    ret->n = atoi(cfg[2].u.string.sval);
    if (strchr(cfg[2].u.string.sval, '%'))
	ret->n = ret->n * (ret->w * ret->h) / 100;
    ret->unique = cfg[3].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    /*
     * Lower limit on grid size: each dimension must be at least 3.
     * 1 is theoretically workable if rather boring, but 2 is a
     * real problem: there is often _no_ way to generate a uniquely
     * solvable 2xn Mines grid. You either run into two mines
     * blocking the way and no idea what's behind them, or one mine
     * and no way to know which of the two rows it's in. If the
     * mine count is even you can create a soluble grid by packing
     * all the mines at one end (so that when you hit a two-mine
     * wall there are only as many covered squares left as there
     * are mines); but if it's odd, you are doomed, because you
     * _have_ to have a gap somewhere which you can't determine the
     * position of.
     */
    if (full && params->unique && (params->w <= 2 || params->h <= 2))
	return "Width and height must both be greater than two";
    if (params->w < 1 || params->h < 1)
	return "Width and height must both be at least one";
    if (params->w > SHRT_MAX || params->h > SHRT_MAX)
        return "Neither width nor height may be unreasonably large";
    /*
     * We use random_upto() to place mines, and its maximum limit is 2^28-1.
     */
#if (1<<28)-1 < INT_MAX
    if (params->w > ((1<<28)-1) / params->h)
#else
    if (params->w > INT_MAX / params->h)
#endif
        return "Width times height must not be unreasonably large";
    if (params->n < 0)
	return "Mine count may not be negative";
    if (params->n > params->w * params->h - 9)
	return "Too many mines for grid size";

    /*
     * FIXME: Need more constraints here. Not sure what the
     * sensible limits for Minesweeper actually are. The limits
     * probably ought to change, however, depending on uniqueness.
     */

    return NULL;
}

/* ----------------------------------------------------------------------
 * Minesweeper solver, used to ensure the generated grids are
 * solvable without having to take risks.
 */

/*
 * Count the bits in a word. Only needs to cope with 16 bits.
 */
static int bitcount16(int inword)
{
    unsigned int word = inword;

    word = ((word & 0xAAAA) >> 1) + (word & 0x5555);
    word = ((word & 0xCCCC) >> 2) + (word & 0x3333);
    word = ((word & 0xF0F0) >> 4) + (word & 0x0F0F);
    word = ((word & 0xFF00) >> 8) + (word & 0x00FF);

    return (int)word;
}

/*
 * We use a tree234 to store a large number of small localised
 * sets, each with a mine count. We also keep some of those sets
 * linked together into a to-do list.
 */
struct set {
    short x, y, mask, mines;
    bool todo;
    struct set *prev, *next;
};

static int setcmp(void *av, void *bv)
{
    struct set *a = (struct set *)av;
    struct set *b = (struct set *)bv;

    if (a->y < b->y)
	return -1;
    else if (a->y > b->y)
	return +1;
    else if (a->x < b->x)
	return -1;
    else if (a->x > b->x)
	return +1;
    else if (a->mask < b->mask)
	return -1;
    else if (a->mask > b->mask)
	return +1;
    else
	return 0;
}

struct setstore {
    tree234 *sets;
    struct set *todo_head, *todo_tail;
};

static struct setstore *ss_new(void)
{
    struct setstore *ss = snew(struct setstore);
    ss->sets = newtree234(setcmp);
    ss->todo_head = ss->todo_tail = NULL;
    return ss;
}

/*
 * Take two input sets, in the form (x,y,mask). Munge the first by
 * taking either its intersection with the second or its difference
 * with the second. Return the new mask part of the first set.
 */
static int setmunge(int x1, int y1, int mask1, int x2, int y2, int mask2,
		    bool diff)
{
    /*
     * Adjust the second set so that it has the same x,y
     * coordinates as the first.
     */
    if (abs(x2-x1) >= 3 || abs(y2-y1) >= 3) {
	mask2 = 0;
    } else {
	while (x2 > x1) {
	    mask2 &= ~(4|32|256);
	    mask2 <<= 1;
	    x2--;
	}
	while (x2 < x1) {
	    mask2 &= ~(1|8|64);
	    mask2 >>= 1;
	    x2++;
	}
	while (y2 > y1) {
	    mask2 &= ~(64|128|256);
	    mask2 <<= 3;
	    y2--;
	}
	while (y2 < y1) {
	    mask2 &= ~(1|2|4);
	    mask2 >>= 3;
	    y2++;
	}
    }

    /*
     * Invert the second set if `diff' is set (we're after A &~ B
     * rather than A & B).
     */
    if (diff)
	mask2 ^= 511;

    /*
     * Now all that's left is a logical AND.
     */
    return mask1 & mask2;
}

static void ss_add_todo(struct setstore *ss, struct set *s)
{
    if (s->todo)
	return;			       /* already on it */

#ifdef SOLVER_DIAGNOSTICS
    printf("adding set on todo list: %d,%d %03x %d\n",
	   s->x, s->y, s->mask, s->mines);
#endif

    s->prev = ss->todo_tail;
    if (s->prev)
	s->prev->next = s;
    else
	ss->todo_head = s;
    ss->todo_tail = s;
    s->next = NULL;
    s->todo = true;
}

static void ss_add(struct setstore *ss, int x, int y, int mask, int mines)
{
    struct set *s;

    assert(mask != 0);

    /*
     * Normalise so that x and y are genuinely the bounding
     * rectangle.
     */
    while (!(mask & (1|8|64)))
	mask >>= 1, x++;
    while (!(mask & (1|2|4)))
	mask >>= 3, y++;

    /*
     * Create a set structure and add it to the tree.
     */
    s = snew(struct set);
    assert(SHRT_MIN <= x && x <= SHRT_MAX);
    s->x = x;
    assert(SHRT_MIN <= y && y <= SHRT_MAX);
    s->y = y;
    s->mask = mask;
    s->mines = mines;
    s->todo = false;
    if (add234(ss->sets, s) != s) {
	/*
	 * This set already existed! Free it and return.
	 */
	sfree(s);
	return;
    }

    /*
     * We've added a new set to the tree, so put it on the todo
     * list.
     */
    ss_add_todo(ss, s);
}

static void ss_remove(struct setstore *ss, struct set *s)
{
    struct set *next = s->next, *prev = s->prev;

#ifdef SOLVER_DIAGNOSTICS
    printf("removing set %d,%d %03x\n", s->x, s->y, s->mask);
#endif
    /*
     * Remove s from the todo list.
     */
    if (prev)
	prev->next = next;
    else if (s == ss->todo_head)
	ss->todo_head = next;

    if (next)
	next->prev = prev;
    else if (s == ss->todo_tail)
	ss->todo_tail = prev;

    s->todo = false;

    /*
     * Remove s from the tree.
     */
    del234(ss->sets, s);

    /*
     * Destroy the actual set structure.
     */
    sfree(s);
}

/*
 * Return a dynamically allocated list of all the sets which
 * overlap a provided input set.
 */
static struct set **ss_overlap(struct setstore *ss, int x, int y, int mask)
{
    struct set **ret = NULL;
    int nret = 0, retsize = 0;
    int xx, yy;

    for (xx = x-3; xx < x+3; xx++)
	for (yy = y-3; yy < y+3; yy++) {
	    struct set stmp, *s;
	    int pos;

	    /*
	     * Find the first set with these top left coordinates.
	     */
            assert(SHRT_MIN <= xx && xx <= SHRT_MAX);
	    stmp.x = xx;
            assert(SHRT_MIN <= yy && yy <= SHRT_MAX);
	    stmp.y = yy;
	    stmp.mask = 0;

	    if (findrelpos234(ss->sets, &stmp, NULL, REL234_GE, &pos)) {
		while ((s = index234(ss->sets, pos)) != NULL &&
		       s->x == xx && s->y == yy) {
		    /*
		     * This set potentially overlaps the input one.
		     * Compute the intersection to see if they
		     * really overlap, and add it to the list if
		     * so.
		     */
		    if (setmunge(x, y, mask, s->x, s->y, s->mask, false)) {
			/*
			 * There's an overlap.
			 */
			if (nret >= retsize) {
			    retsize = nret + 32;
			    ret = sresize(ret, retsize, struct set *);
			}
			ret[nret++] = s;
		    }

		    pos++;
		}
	    }
	}

    ret = sresize(ret, nret+1, struct set *);
    ret[nret] = NULL;

    return ret;
}

/*
 * Get an element from the head of the set todo list.
 */
static struct set *ss_todo(struct setstore *ss)
{
    if (ss->todo_head) {
	struct set *ret = ss->todo_head;
	ss->todo_head = ret->next;
	if (ss->todo_head)
	    ss->todo_head->prev = NULL;
	else
	    ss->todo_tail = NULL;
	ret->next = ret->prev = NULL;
	ret->todo = false;
	return ret;
    } else {
	return NULL;
    }
}

struct squaretodo {
    int *next;
    int head, tail;
};

static void std_add(struct squaretodo *std, int i)
{
    if (std->tail >= 0)
	std->next[std->tail] = i;
    else
	std->head = i;
    std->tail = i;
    std->next[i] = -1;
}

typedef int (*open_cb)(void *, int, int);

static void known_squares(int w, int h, struct squaretodo *std,
                          signed char *grid,
			  open_cb open, void *openctx,
			  int x, int y, int mask, bool mine)
{
    int xx, yy, bit;

    bit = 1;

    for (yy = 0; yy < 3; yy++)
	for (xx = 0; xx < 3; xx++) {
	    if (mask & bit) {
		int i = (y + yy) * w + (x + xx);

		/*
		 * It's possible that this square is _already_
		 * known, in which case we don't try to add it to
		 * the list twice.
		 */
		if (grid[i] == -2) {

		    if (mine) {
			grid[i] = -1;   /* and don't open it! */
		    } else {
			grid[i] = open(openctx, x + xx, y + yy);
			assert(grid[i] != -1);   /* *bang* */
		    }
		    std_add(std, i);

		}
	    }
	    bit <<= 1;
	}
}

/*
 * This is data returned from the `perturb' function. It details
 * which squares have become mines and which have become clear. The
 * solver is (of course) expected to honourably not use that
 * knowledge directly, but to efficently adjust its internal data
 * structures and proceed based on only the information it
 * legitimately has.
 */
struct perturbation {
    int x, y;
    int delta;			       /* +1 == become a mine; -1 == cleared */
};
struct perturbations {
    int n;
    struct perturbation *changes;
};

/*
 * Main solver entry point. You give it a grid of existing
 * knowledge (-1 for a square known to be a mine, 0-8 for empty
 * squares with a given number of neighbours, -2 for completely
 * unknown), plus a function which you can call to open new squares
 * once you're confident of them. It fills in as much more of the
 * grid as it can.
 * 
 * Return value is:
 * 
 *  - -1 means deduction stalled and nothing could be done
 *  - 0 means deduction succeeded fully
 *  - >0 means deduction succeeded but some number of perturbation
 *    steps were required; the exact return value is the number of
 *    perturb calls.
 */

typedef struct perturbations *(*perturb_cb) (void *, signed char *, int, int, int);

static int minesolve(int w, int h, int n, signed char *grid,
		     open_cb open,
                     perturb_cb perturb,
		     void *ctx, random_state *rs)
{
    struct setstore *ss = ss_new();
    struct set **list;
    struct squaretodo astd, *std = &astd;
    int x, y, i, j;
    int nperturbs = 0;

    /*
     * Set up a linked list of squares with known contents, so that
     * we can process them one by one.
     */
    std->next = snewn(w*h, int);
    std->head = std->tail = -1;

    /*
     * Initialise that list with all known squares in the input
     * grid.
     */
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    i = y*w+x;
	    if (grid[i] != -2)
		std_add(std, i);
	}
    }

    /*
     * Main deductive loop.
     */
    while (1) {
	bool done_something = false;
	struct set *s;

	/*
	 * If there are any known squares on the todo list, process
	 * them and construct a set for each.
	 */
	while (std->head != -1) {
	    i = std->head;
#ifdef SOLVER_DIAGNOSTICS
	    printf("known square at %d,%d [%d]\n", i%w, i/w, grid[i]);
#endif
	    std->head = std->next[i];
	    if (std->head == -1)
		std->tail = -1;

	    x = i % w;
	    y = i / w;

	    if (grid[i] >= 0) {
		int dx, dy, mines, bit, val;
#ifdef SOLVER_DIAGNOSTICS
		printf("creating set around this square\n");
#endif
		/*
		 * Empty square. Construct the set of non-known squares
		 * around this one, and determine its mine count.
		 */
		mines = grid[i];
		bit = 1;
		val = 0;
		for (dy = -1; dy <= +1; dy++) {
		    for (dx = -1; dx <= +1; dx++) {
#ifdef SOLVER_DIAGNOSTICS
			printf("grid %d,%d = %d\n", x+dx, y+dy, grid[i+dy*w+dx]);
#endif
			if (x+dx < 0 || x+dx >= w || y+dy < 0 || y+dy >= h)
			    /* ignore this one */;
			else if (grid[i+dy*w+dx] == -1)
			    mines--;
			else if (grid[i+dy*w+dx] == -2)
			    val |= bit;
			bit <<= 1;
		    }
		}
		if (val)
		    ss_add(ss, x-1, y-1, val, mines);
	    }

	    /*
	     * Now, whether the square is empty or full, we must
	     * find any set which contains it and replace it with
	     * one which does not.
	     */
	    {
#ifdef SOLVER_DIAGNOSTICS
		printf("finding sets containing known square %d,%d\n", x, y);
#endif
		list = ss_overlap(ss, x, y, 1);

		for (j = 0; list[j]; j++) {
		    int newmask, newmines;

		    s = list[j];

		    /*
		     * Compute the mask for this set minus the
		     * newly known square.
		     */
		    newmask = setmunge(s->x, s->y, s->mask, x, y, 1, true);

		    /*
		     * Compute the new mine count.
		     */
		    newmines = s->mines - (grid[i] == -1);

		    /*
		     * Insert the new set into the collection,
		     * unless it's been whittled right down to
		     * nothing.
		     */
		    if (newmask)
			ss_add(ss, s->x, s->y, newmask, newmines);

		    /*
		     * Destroy the old one; it is actually obsolete.
		     */
		    ss_remove(ss, s);
		}

		sfree(list);
	    }

	    /*
	     * Marking a fresh square as known certainly counts as
	     * doing something.
	     */
	    done_something = true;
	}

	/*
	 * Now pick a set off the to-do list and attempt deductions
	 * based on it.
	 */
	if ((s = ss_todo(ss)) != NULL) {

#ifdef SOLVER_DIAGNOSTICS
	    printf("set to do: %d,%d %03x %d\n", s->x, s->y, s->mask, s->mines);
#endif
	    /*
	     * Firstly, see if this set has a mine count of zero or
	     * of its own cardinality.
	     */
	    if (s->mines == 0 || s->mines == bitcount16(s->mask)) {
		/*
		 * If so, we can immediately mark all the squares
		 * in the set as known.
		 */
#ifdef SOLVER_DIAGNOSTICS
		printf("easy\n");
#endif
		known_squares(w, h, std, grid, open, ctx,
			      s->x, s->y, s->mask, (s->mines != 0));

		/*
		 * Having done that, we need do nothing further
		 * with this set; marking all the squares in it as
		 * known will eventually eliminate it, and will
		 * also permit further deductions about anything
		 * that overlaps it.
		 */
		continue;
	    }

	    /*
	     * Failing that, we now search through all the sets
	     * which overlap this one.
	     */
	    list = ss_overlap(ss, s->x, s->y, s->mask);

	    for (j = 0; list[j]; j++) {
		struct set *s2 = list[j];
		int swing, s2wing, swc, s2wc;

		/*
		 * Find the non-overlapping parts s2-s and s-s2,
		 * and their cardinalities.
		 * 
		 * I'm going to refer to these parts as `wings'
		 * surrounding the central part common to both
		 * sets. The `s wing' is s-s2; the `s2 wing' is
		 * s2-s.
		 */
		swing = setmunge(s->x, s->y, s->mask, s2->x, s2->y, s2->mask,
				 true);
		s2wing = setmunge(s2->x, s2->y, s2->mask, s->x, s->y, s->mask,
				 true);
		swc = bitcount16(swing);
		s2wc = bitcount16(s2wing);

		/*
		 * If one set has more mines than the other, and
		 * the number of extra mines is equal to the
		 * cardinality of that set's wing, then we can mark
		 * every square in the wing as a known mine, and
		 * every square in the other wing as known clear.
		 */
		if (swc == s->mines - s2->mines ||
		    s2wc == s2->mines - s->mines) {
		    known_squares(w, h, std, grid, open, ctx,
				  s->x, s->y, swing,
				  (swc == s->mines - s2->mines));
		    known_squares(w, h, std, grid, open, ctx,
				  s2->x, s2->y, s2wing,
				  (s2wc == s2->mines - s->mines));
		    continue;
		}

		/*
		 * Failing that, see if one set is a subset of the
		 * other. If so, we can divide up the mine count of
		 * the larger set between the smaller set and its
		 * complement, even if neither smaller set ends up
		 * being immediately clearable.
		 */
		if (swc == 0 && s2wc != 0) {
		    /* s is a subset of s2. */
		    assert(s2->mines > s->mines);
		    ss_add(ss, s2->x, s2->y, s2wing, s2->mines - s->mines);
		} else if (s2wc == 0 && swc != 0) {
		    /* s2 is a subset of s. */
		    assert(s->mines > s2->mines);
		    ss_add(ss, s->x, s->y, swing, s->mines - s2->mines);
		}
	    }

	    sfree(list);

	    /*
	     * In this situation we have definitely done
	     * _something_, even if it's only reducing the size of
	     * our to-do list.
	     */
	    done_something = true;
	} else if (n >= 0) {
	    /*
	     * We have nothing left on our todo list, which means
	     * all localised deductions have failed. Our next step
	     * is to resort to global deduction based on the total
	     * mine count. This is computationally expensive
	     * compared to any of the above deductions, which is
	     * why we only ever do it when all else fails, so that
	     * hopefully it won't have to happen too often.
	     * 
	     * If you pass n<0 into this solver, that informs it
	     * that you do not know the total mine count, so it
	     * won't even attempt these deductions.
	     */

	    int minesleft, squaresleft;
	    int nsets, cursor;
            bool setused[10];

	    /*
	     * Start by scanning the current grid state to work out
	     * how many unknown squares we still have, and how many
	     * mines are to be placed in them.
	     */
	    squaresleft = 0;
	    minesleft = n;
	    for (i = 0; i < w*h; i++) {
		if (grid[i] == -1)
		    minesleft--;
		else if (grid[i] == -2)
		    squaresleft++;
	    }

#ifdef SOLVER_DIAGNOSTICS
	    printf("global deduction time: squaresleft=%d minesleft=%d\n",
		   squaresleft, minesleft);
	    for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
		    int v = grid[y*w+x];
		    if (v == -1)
			putchar('*');
		    else if (v == -2)
			putchar('?');
		    else if (v == 0)
			putchar('-');
		    else
			putchar('0' + v);
		}
		putchar('\n');
	    }
#endif

	    /*
	     * If there _are_ no unknown squares, we have actually
	     * finished.
	     */
	    if (squaresleft == 0) {
		assert(minesleft == 0);
		break;
	    }

	    /*
	     * First really simple case: if there are no more mines
	     * left, or if there are exactly as many mines left as
	     * squares to play them in, then it's all easy.
	     */
	    if (minesleft == 0 || minesleft == squaresleft) {
		for (i = 0; i < w*h; i++)
		    if (grid[i] == -2)
			known_squares(w, h, std, grid, open, ctx,
				      i % w, i / w, 1, minesleft != 0);
		continue;	       /* now go back to main deductive loop */
	    }

	    /*
	     * Failing that, we have to do some _real_ work.
	     * Ideally what we do here is to try every single
	     * combination of the currently available sets, in an
	     * attempt to find a disjoint union (i.e. a set of
	     * squares with a known mine count between them) such
	     * that the remaining unknown squares _not_ contained
	     * in that union either contain no mines or are all
	     * mines.
	     * 
	     * Actually enumerating all 2^n possibilities will get
	     * a bit slow for large n, so I artificially cap this
	     * recursion at n=10 to avoid too much pain.
	     */
	    nsets = count234(ss->sets);
	    if (nsets <= lenof(setused)) {
		/*
		 * Doing this with actual recursive function calls
		 * would get fiddly because a load of local
		 * variables from this function would have to be
		 * passed down through the recursion. So instead
		 * I'm going to use a virtual recursion within this
		 * function. The way this works is:
		 * 
		 *  - we have an array `setused', such that setused[n]
		 *    is true if set n is currently in the union we
		 *    are considering.
		 * 
		 *  - we have a value `cursor' which indicates how
		 *    much of `setused' we have so far filled in.
		 *    It's conceptually the recursion depth.
		 * 
		 * We begin by setting `cursor' to zero. Then:
		 * 
		 *  - if cursor can advance, we advance it by one. We
		 *    set the value in `setused' that it went past to
		 *    true if that set is disjoint from anything else
		 *    currently in `setused', or to false otherwise.
		 * 
		 *  - If cursor cannot advance because it has
		 *    reached the end of the setused list, then we
		 *    have a maximal disjoint union. Check to see
		 *    whether its mine count has any useful
		 *    properties. If so, mark all the squares not
		 *    in the union as known and terminate.
		 * 
		 *  - If cursor has reached the end of setused and the
		 *    algorithm _hasn't_ terminated, back cursor up to
		 *    the nearest true entry, reset it to false, and
		 *    advance cursor just past it.
		 * 
		 *  - If we attempt to back up to the nearest 1 and
		 *    there isn't one at all, then we have gone
		 *    through all disjoint unions of sets in the
		 *    list and none of them has been helpful, so we
		 *    give up.
		 */
		struct set *sets[lenof(setused)];
		for (i = 0; i < nsets; i++)
		    sets[i] = index234(ss->sets, i);

		cursor = 0;
		while (1) {

		    if (cursor < nsets) {
			bool ok = true;

			/* See if any existing set overlaps this one. */
			for (i = 0; i < cursor; i++)
			    if (setused[i] &&
				setmunge(sets[cursor]->x,
					 sets[cursor]->y,
					 sets[cursor]->mask,
					 sets[i]->x, sets[i]->y, sets[i]->mask,
					 false)) {
				ok = false;
				break;
			    }

			if (ok) {
			    /*
			     * We're adding this set to our union,
			     * so adjust minesleft and squaresleft
			     * appropriately.
			     */
			    minesleft -= sets[cursor]->mines;
			    squaresleft -= bitcount16(sets[cursor]->mask);
			}

			setused[cursor++] = ok;
		    } else {
#ifdef SOLVER_DIAGNOSTICS
			printf("trying a set combination with %d %d\n",
			       squaresleft, minesleft);
#endif /* SOLVER_DIAGNOSTICS */

			/*
			 * We've reached the end. See if we've got
			 * anything interesting.
			 */
			if (squaresleft > 0 &&
			    (minesleft == 0 || minesleft == squaresleft)) {
			    /*
			     * We have! There is at least one
			     * square not contained within the set
			     * union we've just found, and we can
			     * deduce that either all such squares
			     * are mines or all are not (depending
			     * on whether minesleft==0). So now all
			     * we have to do is actually go through
			     * the grid, find those squares, and
			     * mark them.
			     */
			    for (i = 0; i < w*h; i++)
				if (grid[i] == -2) {
                                    bool outside = true;
				    y = i / w;
				    x = i % w;
				    for (j = 0; j < nsets; j++)
					if (setused[j] &&
					    setmunge(sets[j]->x, sets[j]->y,
						     sets[j]->mask, x, y, 1,
						     false)) {
					    outside = false;
					    break;
					}
				    if (outside)
					known_squares(w, h, std, grid,
						      open, ctx,
						      x, y, 1, minesleft != 0);
				}

			    done_something = true;
			    break;     /* return to main deductive loop */
			}

			/*
			 * If we reach here, then this union hasn't
			 * done us any good, so move on to the
			 * next. Backtrack cursor to the nearest 1,
			 * change it to a 0 and continue.
			 */
			while (--cursor >= 0 && !setused[cursor]);
			if (cursor >= 0) {
			    assert(setused[cursor]);

			    /*
			     * We're removing this set from our
			     * union, so re-increment minesleft and
			     * squaresleft.
			     */
			    minesleft += sets[cursor]->mines;
			    squaresleft += bitcount16(sets[cursor]->mask);

			    setused[cursor++] = false;
			} else {
			    /*
			     * We've backtracked all the way to the
			     * start without finding a single 1,
			     * which means that our virtual
			     * recursion is complete and nothing
			     * helped.
			     */
			    break;
			}
		    }

		}

	    }
	}

	if (done_something)
	    continue;

#ifdef SOLVER_DIAGNOSTICS
	/*
	 * Dump the current known state of the grid.
	 */
	printf("solver ran out of steam, ret=%d, grid:\n", nperturbs);
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int v = grid[y*w+x];
		if (v == -1)
		    putchar('*');
		else if (v == -2)
		    putchar('?');
		else if (v == 0)
		    putchar('-');
		else
		    putchar('0' + v);
	    }
	    putchar('\n');
	}

	{
	    struct set *s;

	    for (i = 0; (s = index234(ss->sets, i)) != NULL; i++)
		printf("remaining set: %d,%d %03x %d\n", s->x, s->y, s->mask, s->mines);
	}
#endif

	/*
	 * Now we really are at our wits' end as far as solving
	 * this grid goes. Our only remaining option is to call
	 * a perturb function and ask it to modify the grid to
	 * make it easier.
	 */
	if (perturb) {
	    struct perturbations *ret;
	    struct set *s;

	    nperturbs++;

	    /*
	     * Choose a set at random from the current selection,
	     * and ask the perturb function to either fill or empty
	     * it.
	     * 
	     * If we have no sets at all, we must give up.
	     */
	    if (count234(ss->sets) == 0) {
#ifdef SOLVER_DIAGNOSTICS
		printf("perturbing on entire unknown set\n");
#endif
		ret = perturb(ctx, grid, 0, 0, 0);
	    } else {
		s = index234(ss->sets, random_upto(rs, count234(ss->sets)));
#ifdef SOLVER_DIAGNOSTICS
		printf("perturbing on set %d,%d %03x\n", s->x, s->y, s->mask);
#endif
		ret = perturb(ctx, grid, s->x, s->y, s->mask);
	    }

	    if (ret) {
		assert(ret->n > 0);    /* otherwise should have been NULL */

		/*
		 * A number of squares have been fiddled with, and
		 * the returned structure tells us which. Adjust
		 * the mine count in any set which overlaps one of
		 * those squares, and put them back on the to-do
		 * list. Also, if the square itself is marked as a
		 * known non-mine, put it back on the squares-to-do
		 * list.
		 */
		for (i = 0; i < ret->n; i++) {
#ifdef SOLVER_DIAGNOSTICS
		    printf("perturbation %s mine at %d,%d\n",
			   ret->changes[i].delta > 0 ? "added" : "removed",
			   ret->changes[i].x, ret->changes[i].y);
#endif

		    if (ret->changes[i].delta < 0 &&
			grid[ret->changes[i].y*w+ret->changes[i].x] != -2) {
			std_add(std, ret->changes[i].y*w+ret->changes[i].x);
		    }

		    list = ss_overlap(ss,
				      ret->changes[i].x, ret->changes[i].y, 1);

		    for (j = 0; list[j]; j++) {
			list[j]->mines += ret->changes[i].delta;
			ss_add_todo(ss, list[j]);
		    }

		    sfree(list);
		}

		/*
		 * Now free the returned data.
		 */
		sfree(ret->changes);
		sfree(ret);

#ifdef SOLVER_DIAGNOSTICS
		/*
		 * Dump the current known state of the grid.
		 */
		printf("state after perturbation:\n");
		for (y = 0; y < h; y++) {
		    for (x = 0; x < w; x++) {
			int v = grid[y*w+x];
			if (v == -1)
			    putchar('*');
			else if (v == -2)
			    putchar('?');
			else if (v == 0)
			    putchar('-');
			else
			    putchar('0' + v);
		    }
		    putchar('\n');
		}

		{
		    struct set *s;

		    for (i = 0; (s = index234(ss->sets, i)) != NULL; i++)
			printf("remaining set: %d,%d %03x %d\n", s->x, s->y, s->mask, s->mines);
		}
#endif

		/*
		 * And now we can go back round the deductive loop.
		 */
		continue;
	    }
	}

	/*
	 * If we get here, even that didn't work (either we didn't
	 * have a perturb function or it returned failure), so we
	 * give up entirely.
	 */
	break;
    }

    /*
     * See if we've got any unknown squares left.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (grid[y*w+x] == -2) {
		nperturbs = -1;	       /* failed to complete */
		break;
	    }

    /*
     * Free the set list and square-todo list.
     */
    {
	struct set *s;
	while ((s = delpos234(ss->sets, 0)) != NULL)
	    sfree(s);
	freetree234(ss->sets);
	sfree(ss);
	sfree(std->next);
    }

    return nperturbs;
}

/* ----------------------------------------------------------------------
 * Grid generator which uses the above solver.
 */

struct minectx {
    bool *grid;
    int w, h;
    int sx, sy;
    bool allow_big_perturbs;
    random_state *rs;
};

static int mineopen(void *vctx, int x, int y)
{
    struct minectx *ctx = (struct minectx *)vctx;
    int i, j, n;

    assert(x >= 0 && x < ctx->w && y >= 0 && y < ctx->h);
    if (ctx->grid[y * ctx->w + x])
	return -1;		       /* *bang* */

    n = 0;
    for (i = -1; i <= +1; i++) {
	if (x + i < 0 || x + i >= ctx->w)
	    continue;
	for (j = -1; j <= +1; j++) {
	    if (y + j < 0 || y + j >= ctx->h)
		continue;
	    if (i == 0 && j == 0)
		continue;
	    if (ctx->grid[(y+j) * ctx->w + (x+i)])
		n++;
	}
    }

    return n;
}

/* Structure used internally to mineperturb(). */
struct square {
    int x, y, type, random;
};
static int squarecmp(const void *av, const void *bv)
{
    const struct square *a = (const struct square *)av;
    const struct square *b = (const struct square *)bv;
    if (a->type < b->type)
	return -1;
    else if (a->type > b->type)
	return +1;
    else if (a->random < b->random)
	return -1;
    else if (a->random > b->random)
	return +1;
    else if (a->y < b->y)
	return -1;
    else if (a->y > b->y)
	return +1;
    else if (a->x < b->x)
	return -1;
    else if (a->x > b->x)
	return +1;
    return 0;
}

/*
 * Normally this function is passed an (x,y,mask) set description.
 * On occasions, though, there is no _localised_ set being used,
 * and the set being perturbed is supposed to be the entirety of
 * the unreachable area. This is signified by the special case
 * mask==0: in this case, anything labelled -2 in the grid is part
 * of the set.
 * 
 * Allowing perturbation in this special case appears to make it
 * guaranteeably possible to generate a workable grid for any mine
 * density, but they tend to be a bit boring, with mines packed
 * densely into far corners of the grid and the remainder being
 * less dense than one might like. Therefore, to improve overall
 * grid quality I disable this feature for the first few attempts,
 * and fall back to it after no useful grid has been generated.
 */
static struct perturbations *mineperturb(void *vctx, signed char *grid,
					 int setx, int sety, int mask)
{
    struct minectx *ctx = (struct minectx *)vctx;
    struct square *sqlist;
    int x, y, dx, dy, i, n, nfull, nempty;
    struct square **tofill, **toempty, **todo;
    int ntofill, ntoempty, ntodo, dtodo, dset;
    struct perturbations *ret;
    int *setlist;

    if (!mask && !ctx->allow_big_perturbs)
	return NULL;

    /*
     * Make a list of all the squares in the grid which we can
     * possibly use. This list should be in preference order, which
     * means
     * 
     *  - first, unknown squares on the boundary of known space
     *  - next, unknown squares beyond that boundary
     * 	- as a very last resort, known squares, but not within one
     * 	  square of the starting position.
     * 
     * Each of these sections needs to be shuffled independently.
     * We do this by preparing list of all squares and then sorting
     * it with a random secondary key.
     */
    sqlist = snewn(ctx->w * ctx->h, struct square);
    n = 0;
    for (y = 0; y < ctx->h; y++)
	for (x = 0; x < ctx->w; x++) {
	    /*
	     * If this square is too near the starting position,
	     * don't put it on the list at all.
	     */
	    if (abs(y - ctx->sy) <= 1 && abs(x - ctx->sx) <= 1)
		continue;

	    /*
	     * If this square is in the input set, also don't put
	     * it on the list!
	     */
	    if ((mask == 0 && grid[y*ctx->w+x] == -2) ||
		(x >= setx && x < setx + 3 &&
		 y >= sety && y < sety + 3 &&
		 mask & (1 << ((y-sety)*3+(x-setx)))))
		continue;

	    sqlist[n].x = x;
	    sqlist[n].y = y;

	    if (grid[y*ctx->w+x] != -2) {
		sqlist[n].type = 3;    /* known square */
	    } else {
		/*
		 * Unknown square. Examine everything around it and
		 * see if it borders on any known squares. If it
		 * does, it's class 1, otherwise it's 2.
		 */

		sqlist[n].type = 2;

		for (dy = -1; dy <= +1; dy++)
		    for (dx = -1; dx <= +1; dx++)
			if (x+dx >= 0 && x+dx < ctx->w &&
			    y+dy >= 0 && y+dy < ctx->h &&
			    grid[(y+dy)*ctx->w+(x+dx)] != -2) {
			    sqlist[n].type = 1;
			    break;
			}
	    }

	    /*
	     * Finally, a random number to cause qsort to
	     * shuffle within each group.
	     */
	    sqlist[n].random = random_bits(ctx->rs, 31);

	    n++;
	}

    qsort(sqlist, n, sizeof(struct square), squarecmp);

    /*
     * Now count up the number of full and empty squares in the set
     * we've been provided.
     */
    nfull = nempty = 0;
    if (mask) {
	for (dy = 0; dy < 3; dy++)
	    for (dx = 0; dx < 3; dx++)
		if (mask & (1 << (dy*3+dx))) {
		    assert(setx+dx <= ctx->w);
		    assert(sety+dy <= ctx->h);
		    if (ctx->grid[(sety+dy)*ctx->w+(setx+dx)])
			nfull++;
		    else
			nempty++;
		}
    } else {
	for (y = 0; y < ctx->h; y++)
	    for (x = 0; x < ctx->w; x++)
		if (grid[y*ctx->w+x] == -2) {
		    if (ctx->grid[y*ctx->w+x])
			nfull++;
		    else
			nempty++;
		}
    }

    /*
     * Now go through our sorted list until we find either `nfull'
     * empty squares, or `nempty' full squares; these will be
     * swapped with the appropriate squares in the set to either
     * fill or empty the set while keeping the same number of mines
     * overall.
     */
    ntofill = ntoempty = 0;
    if (mask) {
	tofill = snewn(9, struct square *);
	toempty = snewn(9, struct square *);
    } else {
	tofill = snewn(ctx->w * ctx->h, struct square *);
	toempty = snewn(ctx->w * ctx->h, struct square *);
    }
    for (i = 0; i < n; i++) {
	struct square *sq = &sqlist[i];
	if (ctx->grid[sq->y * ctx->w + sq->x])
	    toempty[ntoempty++] = sq;
	else
	    tofill[ntofill++] = sq;
	if (ntofill == nfull || ntoempty == nempty)
	    break;
    }

    /*
     * If we haven't found enough empty squares outside the set to
     * empty it into _or_ enough full squares outside it to fill it
     * up with, we'll have to settle for doing only a partial job.
     * In this case we choose to always _fill_ the set (because
     * this case will tend to crop up when we're working with very
     * high mine densities and the only way to get a solvable grid
     * is going to be to pack most of the mines solidly around the
     * edges). So now our job is to make a list of the empty
     * squares in the set, and shuffle that list so that we fill a
     * random selection of them.
     */
    if (ntofill != nfull && ntoempty != nempty) {
	int k;

	assert(ntoempty != 0);

	setlist = snewn(ctx->w * ctx->h, int);
	i = 0;
	if (mask) {
	    for (dy = 0; dy < 3; dy++)
		for (dx = 0; dx < 3; dx++)
		    if (mask & (1 << (dy*3+dx))) {
			assert(setx+dx <= ctx->w);
			assert(sety+dy <= ctx->h);
			if (!ctx->grid[(sety+dy)*ctx->w+(setx+dx)])
			    setlist[i++] = (sety+dy)*ctx->w+(setx+dx);
		    }
	} else {
	    for (y = 0; y < ctx->h; y++)
		for (x = 0; x < ctx->w; x++)
		    if (grid[y*ctx->w+x] == -2) {
			if (!ctx->grid[y*ctx->w+x])
			    setlist[i++] = y*ctx->w+x;
		    }
	}
	assert(i > ntoempty);
	/*
	 * Now pick `ntoempty' items at random from the list.
	 */
	for (k = 0; k < ntoempty; k++) {
	    int index = k + random_upto(ctx->rs, i - k);
	    int tmp;

	    tmp = setlist[k];
	    setlist[k] = setlist[index];
	    setlist[index] = tmp;
	}
    } else
	setlist = NULL;

    /*
     * Now we're pretty much there. We need to either
     * 	(a) put a mine in each of the empty squares in the set, and
     * 	    take one out of each square in `toempty'
     * 	(b) take a mine out of each of the full squares in the set,
     * 	    and put one in each square in `tofill'
     * depending on which one we've found enough squares to do.
     * 
     * So we start by constructing our list of changes to return to
     * the solver, so that it can update its data structures
     * efficiently rather than having to rescan the whole grid.
     */
    ret = snew(struct perturbations);
    if (ntofill == nfull) {
	todo = tofill;
	ntodo = ntofill;
	dtodo = +1;
	dset = -1;
	sfree(toempty);
    } else {
	/*
	 * (We also fall into this case if we've constructed a
	 * setlist.)
	 */
	todo = toempty;
	ntodo = ntoempty;
	dtodo = -1;
	dset = +1;
	sfree(tofill);
    }
    ret->n = 2 * ntodo;
    ret->changes = snewn(ret->n, struct perturbation);
    for (i = 0; i < ntodo; i++) {
	ret->changes[i].x = todo[i]->x;
	ret->changes[i].y = todo[i]->y;
	ret->changes[i].delta = dtodo;
    }
    /* now i == ntodo */
    if (setlist) {
	int j;
	assert(todo == toempty);
	for (j = 0; j < ntoempty; j++) {
	    ret->changes[i].x = setlist[j] % ctx->w;
	    ret->changes[i].y = setlist[j] / ctx->w;
	    ret->changes[i].delta = dset;
	    i++;
	}
	sfree(setlist);
    } else if (mask) {
	for (dy = 0; dy < 3; dy++)
	    for (dx = 0; dx < 3; dx++)
		if (mask & (1 << (dy*3+dx))) {
		    int currval = (ctx->grid[(sety+dy)*ctx->w+(setx+dx)] ? +1 : -1);
		    if (dset == -currval) {
			ret->changes[i].x = setx + dx;
			ret->changes[i].y = sety + dy;
			ret->changes[i].delta = dset;
			i++;
		    }
		}
    } else {
	for (y = 0; y < ctx->h; y++)
	    for (x = 0; x < ctx->w; x++)
		if (grid[y*ctx->w+x] == -2) {
		    int currval = (ctx->grid[y*ctx->w+x] ? +1 : -1);
		    if (dset == -currval) {
			ret->changes[i].x = x;
			ret->changes[i].y = y;
			ret->changes[i].delta = dset;
			i++;
		    }
		}
    }
    assert(i == ret->n);

    sfree(sqlist);
    sfree(todo);

    /*
     * Having set up the precise list of changes we're going to
     * make, we now simply make them and return.
     */
    for (i = 0; i < ret->n; i++) {
	int delta;

	x = ret->changes[i].x;
	y = ret->changes[i].y;
	delta = ret->changes[i].delta;

	/*
	 * Check we're not trying to add an existing mine or remove
	 * an absent one.
	 */
	assert((delta < 0) ^ (ctx->grid[y*ctx->w+x] == 0));

	/*
	 * Actually make the change.
	 */
	ctx->grid[y*ctx->w+x] = (delta > 0);

	/*
	 * Update any numbers already present in the grid.
	 */
	for (dy = -1; dy <= +1; dy++)
	    for (dx = -1; dx <= +1; dx++)
		if (x+dx >= 0 && x+dx < ctx->w &&
		    y+dy >= 0 && y+dy < ctx->h &&
		    grid[(y+dy)*ctx->w+(x+dx)] != -2) {
		    if (dx == 0 && dy == 0) {
			/*
			 * The square itself is marked as known in
			 * the grid. Mark it as a mine if it's a
			 * mine, or else work out its number.
			 */
			if (delta > 0) {
			    grid[y*ctx->w+x] = -1;
			} else {
			    int dx2, dy2, minecount = 0;
			    for (dy2 = -1; dy2 <= +1; dy2++)
				for (dx2 = -1; dx2 <= +1; dx2++)
				    if (x+dx2 >= 0 && x+dx2 < ctx->w &&
					y+dy2 >= 0 && y+dy2 < ctx->h &&
					ctx->grid[(y+dy2)*ctx->w+(x+dx2)])
					minecount++;
			    grid[y*ctx->w+x] = minecount;
			}
		    } else {
			if (grid[(y+dy)*ctx->w+(x+dx)] >= 0)
			    grid[(y+dy)*ctx->w+(x+dx)] += delta;
		    }
		}
    }

#ifdef GENERATION_DIAGNOSTICS
    {
	int yy, xx;
	printf("grid after perturbing:\n");
	for (yy = 0; yy < ctx->h; yy++) {
	    for (xx = 0; xx < ctx->w; xx++) {
		int v = ctx->grid[yy*ctx->w+xx];
		if (yy == ctx->sy && xx == ctx->sx) {
		    assert(!v);
		    putchar('S');
		} else if (v) {
		    putchar('*');
		} else {
		    putchar('-');
		}
	    }
	    putchar('\n');
	}
	printf("\n");
    }
#endif

    return ret;
}

static bool *minegen(int w, int h, int n, int x, int y, bool unique,
		     random_state *rs)
{
    bool *ret = snewn(w*h, bool);
    bool success;
    int ntries = 0;

    do {
	success = false;
	ntries++;

	memset(ret, 0, w*h);

	/*
	 * Start by placing n mines, none of which is at x,y or within
	 * one square of it.
	 */
	{
	    int *tmp = snewn(w*h, int);
	    int i, j, k, nn;

	    /*
	     * Write down the list of possible mine locations.
	     */
	    k = 0;
	    for (i = 0; i < h; i++)
		for (j = 0; j < w; j++)
		    if (abs(i - y) > 1 || abs(j - x) > 1)
			tmp[k++] = i*w+j;

	    /*
	     * Now pick n off the list at random.
	     */
	    nn = n;
	    while (nn-- > 0) {
		i = random_upto(rs, k);
		ret[tmp[i]] = true;
		tmp[i] = tmp[--k];
	    }

	    sfree(tmp);
	}

#ifdef GENERATION_DIAGNOSTICS
	{
	    int yy, xx;
	    printf("grid after initial generation:\n");
	    for (yy = 0; yy < h; yy++) {
		for (xx = 0; xx < w; xx++) {
		    int v = ret[yy*w+xx];
		    if (yy == y && xx == x) {
			assert(!v);
			putchar('S');
		    } else if (v) {
			putchar('*');
		    } else {
			putchar('-');
		    }
		}
		putchar('\n');
	    }
	    printf("\n");
	}
#endif

	/*
	 * Now set up a results grid to run the solver in, and a
	 * context for the solver to open squares. Then run the solver
	 * repeatedly; if the number of perturb steps ever goes up or
	 * it ever returns -1, give up completely.
	 *
	 * We bypass this bit if we're not after a unique grid.
         */
	if (unique) {
	    signed char *solvegrid = snewn(w*h, signed char);
	    struct minectx actx, *ctx = &actx;
	    int solveret, prevret = -2;

	    ctx->grid = ret;
	    ctx->w = w;
	    ctx->h = h;
	    ctx->sx = x;
	    ctx->sy = y;
	    ctx->rs = rs;
	    ctx->allow_big_perturbs = (ntries > 100);

	    while (1) {
		memset(solvegrid, -2, w*h);
		solvegrid[y*w+x] = mineopen(ctx, x, y);
		assert(solvegrid[y*w+x] == 0); /* by deliberate arrangement */

		solveret =
		    minesolve(w, h, n, solvegrid, mineopen, mineperturb, ctx, rs);
		if (solveret < 0 || (prevret >= 0 && solveret >= prevret)) {
		    success = false;
		    break;
		} else if (solveret == 0) {
		    success = true;
		    break;
		}
	    }

	    sfree(solvegrid);
	} else {
	    success = true;
	}

    } while (!success);

    return ret;
}

static char *describe_layout(bool *grid, int area, int x, int y,
                             bool obfuscate)
{
    char *ret, *p;
    unsigned char *bmp;
    int i;

    /*
     * Set up the mine bitmap and obfuscate it.
     */
    bmp = snewn((area + 7) / 8, unsigned char);
    memset(bmp, 0, (area + 7) / 8);
    for (i = 0; i < area; i++) {
        if (grid[i])
            bmp[i / 8] |= 0x80 >> (i % 8);
    }
    if (obfuscate)
        obfuscate_bitmap(bmp, area, false);

    /*
     * Now encode the resulting bitmap in hex. We can work to
     * nibble rather than byte granularity, since the obfuscation
     * function guarantees to return a bit string of the same
     * length as its input.
     */
    ret = snewn((area+3)/4 + 100, char);
    p = ret + sprintf(ret, "%d,%d,%s", x, y,
                      obfuscate ? "m" : "u");   /* 'm' == masked */
    for (i = 0; i < (area+3)/4; i++) {
        int v = bmp[i/2];
        if (i % 2 == 0)
            v >>= 4;
        *p++ = "0123456789abcdef"[v & 0xF];
    }
    *p = '\0';

    sfree(bmp);

    return ret;
}

static bool *new_mine_layout(int w, int h, int n, int x, int y, bool unique,
			     random_state *rs, char **game_desc)
{
    bool *grid;

#ifdef TEST_OBFUSCATION
    static int tested_obfuscation = false;
    if (!tested_obfuscation) {
	/*
	 * A few simple test vectors for the obfuscator.
	 * 
	 * First test: the 28-bit stream 1234567. This divides up
	 * into 1234 and 567[0]. The SHA of 56 70 30 (appending
	 * "0") is 15ce8ab946640340bbb99f3f48fd2c45d1a31d30. Thus,
	 * we XOR the 16-bit string 15CE into the input 1234 to get
	 * 07FA. Next, we SHA that with "0": the SHA of 07 FA 30 is
	 * 3370135c5e3da4fed937adc004a79533962b6391. So we XOR the
	 * 12-bit string 337 into the input 567 to get 650. Thus
	 * our output is 07FA650.
	 */
	{
	    unsigned char bmp1[] = "\x12\x34\x56\x70";
	    obfuscate_bitmap(bmp1, 28, false);
	    printf("test 1 encode: %s\n",
		   memcmp(bmp1, "\x07\xfa\x65\x00", 4) ? "failed" : "passed");
	    obfuscate_bitmap(bmp1, 28, true);
	    printf("test 1 decode: %s\n",
		   memcmp(bmp1, "\x12\x34\x56\x70", 4) ? "failed" : "passed");
	}
	/*
	 * Second test: a long string to make sure we switch from
	 * one SHA to the next correctly. My input string this time
	 * is simply fifty bytes of zeroes.
	 */
	{
	    unsigned char bmp2[50];
	    unsigned char bmp2a[50];
	    memset(bmp2, 0, 50);
	    memset(bmp2a, 0, 50);
	    obfuscate_bitmap(bmp2, 50 * 8, false);
	    /*
	     * SHA of twenty-five zero bytes plus "0" is
	     * b202c07b990c01f6ff2d544707f60e506019b671. SHA of
	     * twenty-five zero bytes plus "1" is
	     * fcb1d8b5a2f6b592fe6780b36aa9d65dd7aa6db9. Thus our
	     * first half becomes
	     * b202c07b990c01f6ff2d544707f60e506019b671fcb1d8b5a2.
	     * 
	     * SHA of that lot plus "0" is
	     * 10b0af913db85d37ca27f52a9f78bba3a80030db. SHA of the
	     * same string plus "1" is
	     * 3d01d8df78e76d382b8106f480135a1bc751d725. So the
	     * second half becomes
	     * 10b0af913db85d37ca27f52a9f78bba3a80030db3d01d8df78.
	     */
	    printf("test 2 encode: %s\n",
		   memcmp(bmp2, "\xb2\x02\xc0\x7b\x99\x0c\x01\xf6\xff\x2d\x54"
			  "\x47\x07\xf6\x0e\x50\x60\x19\xb6\x71\xfc\xb1\xd8"
			  "\xb5\xa2\x10\xb0\xaf\x91\x3d\xb8\x5d\x37\xca\x27"
			  "\xf5\x2a\x9f\x78\xbb\xa3\xa8\x00\x30\xdb\x3d\x01"
			  "\xd8\xdf\x78", 50) ? "failed" : "passed");
	    obfuscate_bitmap(bmp2, 50 * 8, true);
	    printf("test 2 decode: %s\n",
		   memcmp(bmp2, bmp2a, 50) ? "failed" : "passed");
	}
    }
#endif

    grid = minegen(w, h, n, x, y, unique, rs);

    if (game_desc)
        *game_desc = describe_layout(grid, w * h, x, y, true);

    return grid;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    /*
     * We generate the coordinates of an initial click even if they
     * aren't actually used. This has the effect of harmonising the
     * random number usage between interactive and batch use: if
     * you use `mines --generate' with an explicit random seed, you
     * should get exactly the same results as if you type the same
     * random seed into the interactive game and click in the same
     * initial location. (Of course you won't get the same grid if
     * you click in a _different_ initial location, but there's
     * nothing to be done about that.)
     */
    int x = random_upto(rs, params->w);
    int y = random_upto(rs, params->h);

    if (!interactive) {
	/*
	 * For batch-generated grids, pre-open one square.
	 */
	bool *grid;
	char *desc;

	grid = new_mine_layout(params->w, params->h, params->n,
			       x, y, params->unique, rs, &desc);
	sfree(grid);
	return desc;
    } else {
	char *rsdesc, *desc;

	rsdesc = random_state_encode(rs);
	desc = snewn(strlen(rsdesc) + 100, char);
	sprintf(desc, "r%d,%c,%s", params->n, (char)(params->unique ? 'u' : 'a'), rsdesc);
	sfree(rsdesc);
	return desc;
    }
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int wh = params->w * params->h;
    int x, y;

    if (*desc == 'r') {
        desc++;
	if (!*desc || !isdigit((unsigned char)*desc))
	    return "No initial mine count in game description";
	if (atoi(desc) > wh - 9)
            return "Too many mines for grid size";
	while (*desc && isdigit((unsigned char)*desc))
	    desc++;		       /* skip over mine count */
	if (*desc != ',')
	    return "No ',' after initial x-coordinate in game description";
	desc++;
	if (*desc != 'u' && *desc != 'a')
	    return "No uniqueness specifier in game description";
	desc++;
	if (*desc != ',')
	    return "No ',' after uniqueness specifier in game description";
	/* now ignore the rest */
    } else {
	if (*desc && isdigit((unsigned char)*desc)) {
	    x = atoi(desc);
	    if (x < 0 || x >= params->w)
		return "Initial x-coordinate was out of range";
	    while (*desc && isdigit((unsigned char)*desc))
		desc++;		       /* skip over x coordinate */
	    if (*desc != ',')
		return "No ',' after initial x-coordinate in game description";
	    desc++;		       /* eat comma */
	    if (!*desc || !isdigit((unsigned char)*desc))
		return "No initial y-coordinate in game description";
	    y = atoi(desc);
	    if (y < 0 || y >= params->h)
		return "Initial y-coordinate was out of range";
	    while (*desc && isdigit((unsigned char)*desc))
		desc++;		       /* skip over y coordinate */
	    if (*desc != ',')
		return "No ',' after initial y-coordinate in game description";
	    desc++;		       /* eat comma */
	}
	/* eat `m' for `masked' or `u' for `unmasked', if present */
	if (*desc == 'm' || *desc == 'u')
	    desc++;
	/* now just check length of remainder */
	if (strlen(desc) != (wh+3)/4)
	    return "Game description is wrong length";
    }

    return NULL;
}

static int open_square(game_state *state, int x, int y)
{
    int w = state->w, h = state->h;
    int xx, yy, nmines, ncovered;

    if (!state->layout->mines) {
	/*
	 * We have a preliminary game in which the mine layout
	 * hasn't been generated yet. Generate it based on the
	 * initial click location.
	 */
	char *desc, *privdesc;
	state->layout->mines = new_mine_layout(w, h, state->layout->n,
					       x, y, state->layout->unique,
					       state->layout->rs,
					       &desc);
	/*
	 * Find the trailing substring of the game description
	 * corresponding to just the mine layout; we will use this
	 * as our second `private' game ID for serialisation.
	 */
	privdesc = desc;
	while (*privdesc && isdigit((unsigned char)*privdesc)) privdesc++;
	if (*privdesc == ',') privdesc++;
	while (*privdesc && isdigit((unsigned char)*privdesc)) privdesc++;
	if (*privdesc == ',') privdesc++;
	assert(*privdesc == 'm');
	midend_supersede_game_desc(state->layout->me, desc, privdesc);
	sfree(desc);
	random_free(state->layout->rs);
	state->layout->rs = NULL;
    }

    if (state->layout->mines[y*w+x]) {
	/*
	 * The player has landed on a mine. Bad luck. Expose the
	 * mine that killed them, but not the rest (in case they
	 * want to Undo and carry on playing).
	 */
	state->dead = true;
	state->grid[y*w+x] = 65;
	return -1;
    }

    /*
     * Otherwise, the player has opened a safe square. Mark it to-do.
     */
    state->grid[y*w+x] = -10;	       /* `todo' value internal to this func */

    /*
     * Now go through the grid finding all `todo' values and
     * opening them. Every time one of them turns out to have no
     * neighbouring mines, we add all its unopened neighbours to
     * the list as well.
     * 
     * FIXME: We really ought to be able to do this better than
     * using repeated N^2 scans of the grid.
     */
    while (1) {
	bool done_something = false;

	for (yy = 0; yy < h; yy++)
	    for (xx = 0; xx < w; xx++)
		if (state->grid[yy*w+xx] == -10) {
		    int dx, dy, v;

		    assert(!state->layout->mines[yy*w+xx]);

		    v = 0;

		    for (dx = -1; dx <= +1; dx++)
			for (dy = -1; dy <= +1; dy++)
			    if (xx+dx >= 0 && xx+dx < state->w &&
				yy+dy >= 0 && yy+dy < state->h &&
				state->layout->mines[(yy+dy)*w+(xx+dx)])
				v++;

		    state->grid[yy*w+xx] = v;

		    if (v == 0) {
			for (dx = -1; dx <= +1; dx++)
			    for (dy = -1; dy <= +1; dy++)
				if (xx+dx >= 0 && xx+dx < state->w &&
				    yy+dy >= 0 && yy+dy < state->h &&
				    state->grid[(yy+dy)*w+(xx+dx)] == -2)
				    state->grid[(yy+dy)*w+(xx+dx)] = -10;
		    }

		    done_something = true;
		}

	if (!done_something)
	    break;
    }

    /* If the player has already lost, don't let them win as well. */
    if (state->dead) return 0;
    /*
     * Finally, scan the grid and see if exactly as many squares
     * are still covered as there are mines. If so, set the `won'
     * flag and fill in mine markers on all covered squares.
     */
    nmines = ncovered = 0;
    for (yy = 0; yy < h; yy++)
	for (xx = 0; xx < w; xx++) {
	    if (state->grid[yy*w+xx] < 0)
		ncovered++;
	    if (state->layout->mines[yy*w+xx])
		nmines++;
	}
    assert(ncovered >= nmines);
    if (ncovered == nmines) {
	for (yy = 0; yy < h; yy++)
	    for (xx = 0; xx < w; xx++) {
		if (state->grid[yy*w+xx] < 0)
		    state->grid[yy*w+xx] = -1;
	}
	state->won = true;
    }

    return 0;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int i, wh, x, y;
    bool masked;
    unsigned char *bmp;

    state->w = params->w;
    state->h = params->h;
    state->n = params->n;
    state->dead = state->won = false;
    state->used_solve = false;

    wh = state->w * state->h;

    state->layout = snew(struct mine_layout);
    memset(state->layout, 0, sizeof(struct mine_layout));
    state->layout->refcount = 1;

    state->grid = snewn(wh, signed char);
    memset(state->grid, -2, wh);

    if (*desc == 'r') {
	desc++;
	state->layout->n = atoi(desc);
	while (*desc && isdigit((unsigned char)*desc))
	    desc++;		       /* skip over mine count */
	if (*desc) desc++;	       /* eat comma */
	if (*desc == 'a')
	    state->layout->unique = false;
	else
	    state->layout->unique = true;
	desc++;
	if (*desc) desc++;	       /* eat comma */

	state->layout->mines = NULL;
	state->layout->rs = random_state_decode(desc);
	state->layout->me = me;

    } else {
	state->layout->rs = NULL;
	state->layout->me = NULL;
	state->layout->mines = snewn(wh, bool);

	if (*desc && isdigit((unsigned char)*desc)) {
	    x = atoi(desc);
	    while (*desc && isdigit((unsigned char)*desc))
		desc++;		       /* skip over x coordinate */
	    if (*desc) desc++;	       /* eat comma */
	    y = atoi(desc);
	    while (*desc && isdigit((unsigned char)*desc))
		desc++;		       /* skip over y coordinate */
	    if (*desc) desc++;	       /* eat comma */
	} else {
	    x = y = -1;
	}

	if (*desc == 'm') {
	    masked = true;
	    desc++;
	} else {
	    if (*desc == 'u')
		desc++;
	    /*
	     * We permit game IDs to be entered by hand without the
	     * masking transformation.
	     */
	    masked = false;
	}

	bmp = snewn((wh + 7) / 8, unsigned char);
	memset(bmp, 0, (wh + 7) / 8);
	for (i = 0; i < (wh+3)/4; i++) {
	    int c = desc[i];
	    int v;

	    assert(c != 0);	       /* validate_desc should have caught */
	    if (c >= '0' && c <= '9')
		v = c - '0';
	    else if (c >= 'a' && c <= 'f')
		v = c - 'a' + 10;
	    else if (c >= 'A' && c <= 'F')
		v = c - 'A' + 10;
	    else
		v = 0;

	    bmp[i / 2] |= v << (4 * (1 - (i % 2)));
	}

	if (masked)
	    obfuscate_bitmap(bmp, wh, true);

	memset(state->layout->mines, 0, wh * sizeof(bool));
	for (i = 0; i < wh; i++) {
	    if (bmp[i / 8] & (0x80 >> (i % 8)))
		state->layout->mines[i] = true;
	}

	if (x >= 0 && y >= 0)
	    open_square(state, x, y);
        sfree(bmp);
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->dead = state->dead;
    ret->won = state->won;
    ret->used_solve = state->used_solve;
    ret->layout = state->layout;
    ret->layout->refcount++;
    ret->grid = snewn(ret->w * ret->h, signed char);
    memcpy(ret->grid, state->grid, ret->w * ret->h);

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->layout->refcount <= 0) {
	sfree(state->layout->mines);
	if (state->layout->rs)
	    random_free(state->layout->rs);
	sfree(state->layout);
    }
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    if (!state->layout->mines) {
	*error = "Game has not been started yet";
	return NULL;
    }

    return dupstr("S");
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char *ret;
    int x, y;

    ret = snewn((state->w + 1) * state->h + 1, char);
    for (y = 0; y < state->h; y++) {
	for (x = 0; x < state->w; x++) {
	    int v = state->grid[y*state->w+x];
	    if (v == 0)
		v = '-';
	    else if (v >= 1 && v <= 8)
		v = '0' + v;
	    else if (v == -1)
		v = '*';
	    else if (v == -2 || v == -3)
		v = '?';
	    else if (v >= 64)
		v = '!';
	    ret[y * (state->w+1) + x] = v;
	}
	ret[y * (state->w+1) + state->w] = '\n';
    }
    ret[(state->w + 1) * state->h] = '\0';

    return ret;
}

struct game_ui {
    int hx, hy, hradius;	       /* for mouse-down highlights */
    int validradius;
    bool flash_is_death;
    int deaths;
    bool completed;
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->hx = ui->hy = -1;
    ui->hradius = ui->validradius = 0;
    ui->deaths = 0;
    ui->completed = false;
    ui->flash_is_death = false;	       /* *shrug* */
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    char buf[80];
    /*
     * The deaths counter and completion status need preserving
     * across a serialisation.
     */
    sprintf(buf, "D%d", ui->deaths);
    if (ui->completed)
	strcat(buf, "C");
    return dupstr(buf);
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    int p= 0;
    sscanf(encoding, "D%d%n", &ui->deaths, &p);
    if (encoding[p] == 'C')
	ui->completed = true;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    if (newstate->won)
	ui->completed = true;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int cx = ui->cur_x, cy = ui->cur_y;
    int v = state->grid[cy * state->w + cx];

    if (state->dead || state->won || !ui->cur_visible) return "";
    if (button == CURSOR_SELECT2) {
        if (v == -2) return "Mark";
        if (v == -1) return "Unmark";
        return "";
    }
    if (button == CURSOR_SELECT) {
        int dy, dx, n = 0;
        if (v == -2 || v == -3) return "Uncover";
        if (v == 0) return "";
        /* Count mine markers. */
        for (dy = -1; dy <= +1; dy++)
            for (dx = -1; dx <= +1; dx++)
                if (cx+dx >= 0 && cx+dx < state->w &&
			cy+dy >= 0 && cy+dy < state->h) {
			if (state->grid[(cy+dy)*state->w+(cx+dx)] == -1)
			    n++;
		    }
        if (n == v) return "Clear";
    }
    return "";
}

struct game_drawstate {
    int w, h, tilesize, bg;
    bool started;
    signed char *grid;
    /*
     * Items in this `grid' array have all the same values as in
     * the game_state grid, and in addition:
     * 
     * 	- -10 means the tile was drawn `specially' as a result of a
     * 	  flash, so it will always need redrawing.
     * 
     * 	- -22 and -23 mean the tile is highlighted for a possible
     * 	  click.
     */
    int cur_x, cur_y; /* -1, -1 for no cursor displayed. */
};

static char *interpret_move(const game_state *from, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int cx, cy;
    char buf[256];

    if (from->dead || from->won)
	return NULL;		       /* no further moves permitted */

    cx = FROMCOORD(x);
    cy = FROMCOORD(y);

    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cur_x, &ui->cur_y, from->w, from->h, false);
        ui->cur_visible = true;
        return UI_UPDATE;
    }
    if (IS_CURSOR_SELECT(button)) {
        int v = from->grid[ui->cur_y * from->w + ui->cur_x];

        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        if (button == CURSOR_SELECT2) {
            /* As for RIGHT_BUTTON; only works on covered square. */
            if (v != -2 && v != -1)
                return NULL;
            sprintf(buf, "F%d,%d", ui->cur_x, ui->cur_y);
            return dupstr(buf);
        }
        /* Otherwise, treat as LEFT_BUTTON, for a single square. */
        if (v == -2 || v == -3) {
            if (from->layout->mines &&
                from->layout->mines[ui->cur_y * from->w + ui->cur_x])
                ui->deaths++;

            sprintf(buf, "O%d,%d", ui->cur_x, ui->cur_y);
            return dupstr(buf);
        }
        cx = ui->cur_x; cy = ui->cur_y;
        ui->validradius = 1;
        goto uncover;
    }

    if (button == LEFT_BUTTON || button == LEFT_DRAG ||
	button == MIDDLE_BUTTON || button == MIDDLE_DRAG) {
	if (cx < 0 || cx >= from->w || cy < 0 || cy >= from->h)
	    return NULL;

	/*
	 * Mouse-downs and mouse-drags just cause highlighting
	 * updates.
	 */
	ui->hx = cx;
	ui->hy = cy;
	ui->hradius = (from->grid[cy*from->w+cx] >= 0 ? 1 : 0);
	if (button == LEFT_BUTTON)
	    ui->validradius = ui->hradius;
	else if (button == MIDDLE_BUTTON)
	    ui->validradius = 1;
        ui->cur_visible = false;
	return UI_UPDATE;
    }

    if (button == RIGHT_BUTTON) {
	if (cx < 0 || cx >= from->w || cy < 0 || cy >= from->h)
	    return NULL;

	/*
	 * Right-clicking only works on a covered square, and it
	 * toggles between -1 (marked as mine) and -2 (not marked
	 * as mine).
	 *
	 * FIXME: question marks.
	 */
	if (from->grid[cy * from->w + cx] != -2 &&
	    from->grid[cy * from->w + cx] != -1)
	    return NULL;

	sprintf(buf, "F%d,%d", cx, cy);
	return dupstr(buf);
    }

    if (button == LEFT_RELEASE || button == MIDDLE_RELEASE) {
	ui->hx = ui->hy = -1;
	ui->hradius = 0;

	/*
	 * At this stage we must never return NULL: we have adjusted
	 * the ui, so at worst we return UI_UPDATE.
	 */
	if (cx < 0 || cx >= from->w || cy < 0 || cy >= from->h)
	    return UI_UPDATE;

	/*
	 * Left-clicking on a covered square opens a tile. Not
	 * permitted if the tile is marked as a mine, for safety.
	 * (Unmark it and _then_ open it.)
	 */
	if (button == LEFT_RELEASE &&
	    (from->grid[cy * from->w + cx] == -2 ||
	     from->grid[cy * from->w + cx] == -3) &&
	    ui->validradius == 0) {
	    /* Check if you've killed yourself. */
	    if (from->layout->mines && from->layout->mines[cy * from->w + cx])
		ui->deaths++;

	    sprintf(buf, "O%d,%d", cx, cy);
	    return dupstr(buf);
	}
        goto uncover;
    }
    return NULL;

uncover:
    {
	/*
	 * Left-clicking or middle-clicking on an uncovered tile:
	 * first we check to see if the number of mine markers
	 * surrounding the tile is equal to its mine count, and if
	 * so then we open all other surrounding squares.
	 */
	if (from->grid[cy * from->w + cx] > 0 && ui->validradius == 1) {
	    int dy, dx, n;

	    /* Count mine markers. */
	    n = 0;
	    for (dy = -1; dy <= +1; dy++)
		for (dx = -1; dx <= +1; dx++)
		    if (cx+dx >= 0 && cx+dx < from->w &&
			cy+dy >= 0 && cy+dy < from->h) {
			if (from->grid[(cy+dy)*from->w+(cx+dx)] == -1)
			    n++;
		    }

	    if (n == from->grid[cy * from->w + cx]) {

		/*
		 * Now see if any of the squares we're clearing
		 * contains a mine (which will happen iff you've
		 * incorrectly marked the mines around the clicked
		 * square). If so, we open _just_ those squares, to
		 * reveal as little additional information as we
		 * can.
		 */
		char *p = buf;
		const char *sep = "";

		for (dy = -1; dy <= +1; dy++)
		    for (dx = -1; dx <= +1; dx++)
			if (cx+dx >= 0 && cx+dx < from->w &&
			    cy+dy >= 0 && cy+dy < from->h) {
			    if (from->grid[(cy+dy)*from->w+(cx+dx)] != -1 &&
				from->layout->mines &&
				from->layout->mines[(cy+dy)*from->w+(cx+dx)]) {
				p += sprintf(p, "%sO%d,%d", sep, cx+dx, cy+dy);
				sep = ";";
			    }
			}

		if (p > buf) {
		    ui->deaths++;
		} else {
		    sprintf(buf, "C%d,%d", cx, cy);
		}

		return dupstr(buf);
	    }
	}

	return UI_UPDATE;
    }
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int cy, cx;
    game_state *ret;

    if (!strcmp(move, "S")) {
	int yy, xx;

        if (!from->layout->mines) return NULL; /* Game not started. */
	ret = dup_game(from);
        if (!ret->dead) {
            /*
             * If the player is still alive at the moment of pressing
             * Solve, expose the entire grid as if it were a completed
             * solution.
             */
            for (yy = 0; yy < ret->h; yy++)
                for (xx = 0; xx < ret->w; xx++) {

                    if (ret->layout->mines[yy*ret->w+xx]) {
                        ret->grid[yy*ret->w+xx] = -1;
                    } else {
                        int dx, dy, v;

                        v = 0;

                        for (dx = -1; dx <= +1; dx++)
                            for (dy = -1; dy <= +1; dy++)
                                if (xx+dx >= 0 && xx+dx < ret->w &&
                                    yy+dy >= 0 && yy+dy < ret->h &&
                                    ret->layout->mines[(yy+dy)*ret->w+(xx+dx)])
                                    v++;

                        ret->grid[yy*ret->w+xx] = v;
                    }
                }
        } else {
            /*
             * If the player pressed Solve _after dying_, show a full
             * corrections grid in the style of standard Minesweeper.
             * Players who don't like Mines's behaviour on death of
             * only showing the mine that killed you (so that in case
             * of a typo you can undo and carry on without the rest of
             * the grid being spoiled) can use this to get the display
             * that ordinary Minesweeper would have given them.
             */
            for (yy = 0; yy < ret->h; yy++)
                for (xx = 0; xx < ret->w; xx++) {
                    int pos = yy*ret->w+xx;
                    if ((ret->grid[pos] == -2 || ret->grid[pos] == -3) &&
                        ret->layout->mines[pos]) {
                        ret->grid[pos] = 64;
                    } else if (ret->grid[pos] == -1 &&
                               !ret->layout->mines[pos]) {
                        ret->grid[pos] = 66;
                    }
                }
        }
        ret->used_solve = true;

	return ret;
    } else {
        /* Dead players should stop trying to move. */
        if (from->dead)
            return NULL;
	ret = dup_game(from);

	while (*move) {
	    if (move[0] == 'F' &&
		sscanf(move+1, "%d,%d", &cx, &cy) == 2 &&
		cx >= 0 && cx < from->w && cy >= 0 && cy < from->h &&
                (ret->grid[cy * from->w + cx] == -1 ||
                 ret->grid[cy * from->w + cx] == -2)) {
		ret->grid[cy * from->w + cx] ^= (-2 ^ -1);
	    } else if (move[0] == 'O' &&
		       sscanf(move+1, "%d,%d", &cx, &cy) == 2 &&
		       cx >= 0 && cx < from->w && cy >= 0 && cy < from->h) {
		open_square(ret, cx, cy);
	    } else if (move[0] == 'C' &&
		       sscanf(move+1, "%d,%d", &cx, &cy) == 2 &&
		       cx >= 0 && cx < from->w && cy >= 0 && cy < from->h) {
		int dx, dy;

		for (dy = -1; dy <= +1; dy++)
		    for (dx = -1; dx <= +1; dx++)
			if (cx+dx >= 0 && cx+dx < ret->w &&
			    cy+dy >= 0 && cy+dy < ret->h &&
			    (ret->grid[(cy+dy)*ret->w+(cx+dx)] == -2 ||
			     ret->grid[(cy+dy)*ret->w+(cx+dx)] == -3))
			    open_square(ret, cx+dx, cy+dy);
	    } else {
		free_game(ret);
		return NULL;
	    }

	    while (*move && *move != ';') move++;
	    if (*move) move++;
	}

	return ret;
    }
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

    *x = BORDER * 2 + TILE_SIZE * params->w;
    *y = BORDER * 2 + TILE_SIZE * params->h;
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

    ret[COL_BACKGROUND2 * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 19.0F / 20.0F;
    ret[COL_BACKGROUND2 * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 19.0F / 20.0F;
    ret[COL_BACKGROUND2 * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 19.0F / 20.0F;

    ret[COL_1 * 3 + 0] = 0.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 1.0F;

    ret[COL_2 * 3 + 0] = 0.0F;
    ret[COL_2 * 3 + 1] = 0.5F;
    ret[COL_2 * 3 + 2] = 0.0F;

    ret[COL_3 * 3 + 0] = 1.0F;
    ret[COL_3 * 3 + 1] = 0.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    ret[COL_4 * 3 + 0] = 0.0F;
    ret[COL_4 * 3 + 1] = 0.0F;
    ret[COL_4 * 3 + 2] = 0.5F;

    ret[COL_5 * 3 + 0] = 0.5F;
    ret[COL_5 * 3 + 1] = 0.0F;
    ret[COL_5 * 3 + 2] = 0.0F;

    ret[COL_6 * 3 + 0] = 0.0F;
    ret[COL_6 * 3 + 1] = 0.5F;
    ret[COL_6 * 3 + 2] = 0.5F;

    ret[COL_7 * 3 + 0] = 0.0F;
    ret[COL_7 * 3 + 1] = 0.0F;
    ret[COL_7 * 3 + 2] = 0.0F;

    ret[COL_8 * 3 + 0] = 0.5F;
    ret[COL_8 * 3 + 1] = 0.5F;
    ret[COL_8 * 3 + 2] = 0.5F;

    ret[COL_MINE * 3 + 0] = 0.0F;
    ret[COL_MINE * 3 + 1] = 0.0F;
    ret[COL_MINE * 3 + 2] = 0.0F;

    ret[COL_BANG * 3 + 0] = 1.0F;
    ret[COL_BANG * 3 + 1] = 0.0F;
    ret[COL_BANG * 3 + 2] = 0.0F;

    ret[COL_CROSS * 3 + 0] = 1.0F;
    ret[COL_CROSS * 3 + 1] = 0.0F;
    ret[COL_CROSS * 3 + 2] = 0.0F;

    ret[COL_FLAG * 3 + 0] = 1.0F;
    ret[COL_FLAG * 3 + 1] = 0.0F;
    ret[COL_FLAG * 3 + 2] = 0.0F;

    ret[COL_FLAGBASE * 3 + 0] = 0.0F;
    ret[COL_FLAGBASE * 3 + 1] = 0.0F;
    ret[COL_FLAGBASE * 3 + 2] = 0.0F;

    ret[COL_QUERY * 3 + 0] = 0.0F;
    ret[COL_QUERY * 3 + 1] = 0.0F;
    ret[COL_QUERY * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_LOWLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0F / 3.0F;
    ret[COL_LOWLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0F / 3.0F;
    ret[COL_LOWLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0F / 3.0F;

    ret[COL_WRONGNUMBER * 3 + 0] = 1.0F;
    ret[COL_WRONGNUMBER * 3 + 1] = 0.6F;
    ret[COL_WRONGNUMBER * 3 + 2] = 0.6F;

    /* Red tinge to a light colour, for the cursor. */
    ret[COL_CURSOR * 3 + 0] = ret[COL_HIGHLIGHT * 3 + 0];
    ret[COL_CURSOR * 3 + 1] = ret[COL_HIGHLIGHT * 3 + 0] / 2.0F;
    ret[COL_CURSOR * 3 + 2] = ret[COL_HIGHLIGHT * 3 + 0] / 2.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->w = state->w;
    ds->h = state->h;
    ds->started = false;
    ds->tilesize = 0;                  /* not decided yet */
    ds->grid = snewn(ds->w * ds->h, signed char);
    ds->bg = -1;
    ds->cur_x = ds->cur_y = -1;

    memset(ds->grid, -99, ds->w * ds->h);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds,
                      int x, int y, int v, int bg)
{
    if (v < 0) {
        int coords[12];
	int hl = 0;

	if (v == -22 || v == -23) {
	    v += 20;

	    /*
	     * Omit the highlights in this case.
	     */
	    draw_rect(dr, x, y, TILE_SIZE, TILE_SIZE,
                      bg == COL_BACKGROUND ? COL_BACKGROUND2 : bg);
	    draw_line(dr, x, y, x + TILE_SIZE - 1, y, COL_LOWLIGHT);
	    draw_line(dr, x, y, x, y + TILE_SIZE - 1, COL_LOWLIGHT);
	} else {
	    /*
	     * Draw highlights to indicate the square is covered.
	     */
	    coords[0] = x + TILE_SIZE - 1;
	    coords[1] = y + TILE_SIZE - 1;
	    coords[2] = x + TILE_SIZE - 1;
	    coords[3] = y;
	    coords[4] = x;
	    coords[5] = y + TILE_SIZE - 1;
	    draw_polygon(dr, coords, 3, COL_LOWLIGHT ^ hl, COL_LOWLIGHT ^ hl);

	    coords[0] = x;
	    coords[1] = y;
	    draw_polygon(dr, coords, 3, COL_HIGHLIGHT ^ hl,
			 COL_HIGHLIGHT ^ hl);

	    draw_rect(dr, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
		      TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
		      bg);
	}

	if (v == -1) {
	    /*
	     * Draw a flag.
	     */
#define SETCOORD(n, dx, dy) do { \
    coords[(n)*2+0] = x + (int)(TILE_SIZE * (dx)); \
    coords[(n)*2+1] = y + (int)(TILE_SIZE * (dy)); \
} while (0)
	    SETCOORD(0, 0.6F,  0.35F);
	    SETCOORD(1, 0.6F,  0.7F);
	    SETCOORD(2, 0.8F,  0.8F);
	    SETCOORD(3, 0.25F, 0.8F);
	    SETCOORD(4, 0.55F, 0.7F);
	    SETCOORD(5, 0.55F, 0.35F);
	    draw_polygon(dr, coords, 6, COL_FLAGBASE, COL_FLAGBASE);

	    SETCOORD(0, 0.6F, 0.2F);
	    SETCOORD(1, 0.6F, 0.5F);
	    SETCOORD(2, 0.2F, 0.35F);
	    draw_polygon(dr, coords, 3, COL_FLAG, COL_FLAG);
#undef SETCOORD

	} else if (v == -3) {
	    /*
	     * Draw a question mark.
	     */
	    draw_text(dr, x + TILE_SIZE / 2, y + TILE_SIZE / 2,
		      FONT_VARIABLE, TILE_SIZE * 6 / 8,
		      ALIGN_VCENTRE | ALIGN_HCENTRE,
		      COL_QUERY, "?");
	}
    } else {
	/*
	 * Clear the square to the background colour, and draw thin
	 * grid lines along the top and left.
	 * 
	 * Exception is that for value 65 (mine we've just trodden
	 * on), we clear the square to COL_BANG.
	 */
        if (v & 32) {
            bg = COL_WRONGNUMBER;
            v &= ~32;
        }
        draw_rect(dr, x, y, TILE_SIZE, TILE_SIZE,
		  (v == 65 ? COL_BANG :
                   bg == COL_BACKGROUND ? COL_BACKGROUND2 : bg));
	draw_line(dr, x, y, x + TILE_SIZE - 1, y, COL_LOWLIGHT);
	draw_line(dr, x, y, x, y + TILE_SIZE - 1, COL_LOWLIGHT);

	if (v > 0 && v <= 8) {
	    /*
	     * Mark a number.
	     */
	    char str[2];
	    str[0] = v + '0';
	    str[1] = '\0';
	    draw_text(dr, x + TILE_SIZE / 2, y + TILE_SIZE / 2,
		      FONT_VARIABLE, TILE_SIZE * 7 / 8,
		      ALIGN_VCENTRE | ALIGN_HCENTRE,
		      (COL_1 - 1) + v, str);

	} else if (v >= 64) {
	    /*
	     * Mark a mine.
	     */
	    {
		int cx = x + TILE_SIZE / 2;
		int cy = y + TILE_SIZE / 2;
		int r = TILE_SIZE / 2 - 3;

		draw_circle(dr, cx, cy, 5*r/6, COL_MINE, COL_MINE);
		draw_rect(dr, cx - r/6, cy - r, 2*(r/6)+1, 2*r+1, COL_MINE);
		draw_rect(dr, cx - r, cy - r/6, 2*r+1, 2*(r/6)+1, COL_MINE);
		draw_rect(dr, cx-r/3, cy-r/3, r/3, r/4, COL_HIGHLIGHT);
	    }

	    if (v == 66) {
		/*
		 * Cross through the mine.
		 */
		int dx;
		for (dx = -1; dx <= +1; dx++) {
		    draw_line(dr, x + 3 + dx, y + 2,
			      x + TILE_SIZE - 3 + dx,
			      y + TILE_SIZE - 2, COL_CROSS);
		    draw_line(dr, x + TILE_SIZE - 3 + dx, y + 2,
			      x + 3 + dx, y + TILE_SIZE - 2,
			      COL_CROSS);
		}
	    }
	}
    }

    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y;
    int mines, markers, closed, bg;
    int cx = -1, cy = -1;
    bool cmoved;

    if (flashtime) {
	int frame = (int)(flashtime / FLASH_FRAME);
	if (frame % 2)
	    bg = (ui->flash_is_death ? COL_BACKGROUND : COL_LOWLIGHT);
	else
	    bg = (ui->flash_is_death ? COL_BANG : COL_HIGHLIGHT);
    } else
	bg = COL_BACKGROUND;

    if (!ds->started) {
        int coords[10];

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(state->w) + OUTER_HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(state->h) + OUTER_HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(state->w) + OUTER_HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - OUTER_HIGHLIGHT_WIDTH;
        coords[4] = coords[2] - TILE_SIZE;
        coords[5] = coords[3] + TILE_SIZE;
        coords[8] = COORD(0) - OUTER_HIGHLIGHT_WIDTH;
        coords[9] = COORD(state->h) + OUTER_HIGHLIGHT_WIDTH - 1;
        coords[6] = coords[8] + TILE_SIZE;
        coords[7] = coords[9] - TILE_SIZE;
        draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

        coords[1] = COORD(0) - OUTER_HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - OUTER_HIGHLIGHT_WIDTH;
        draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);

        ds->started = true;
    }

    if (ui->cur_visible) cx = ui->cur_x;
    if (ui->cur_visible) cy = ui->cur_y;
    cmoved = (cx != ds->cur_x || cy != ds->cur_y);

    /*
     * Now draw the tiles. Also in this loop, count up the number
     * of mines, mine markers, and closed squares.
     */
    mines = markers = closed = 0;
    for (y = 0; y < ds->h; y++)
	for (x = 0; x < ds->w; x++) {
	    int v = state->grid[y*ds->w+x];
            bool cc = false;

            if (v < 0)
                closed++;
	    if (v == -1)
		markers++;
	    if (state->layout->mines && state->layout->mines[y*ds->w+x])
		mines++;

            if (v >= 0 && v <= 8) {
                /*
                 * Count up the flags around this tile, and if
                 * there are too _many_, highlight the tile.
                 */
                int dx, dy, flags = 0;

                for (dy = -1; dy <= +1; dy++)
                    for (dx = -1; dx <= +1; dx++) {
                        int nx = x+dx, ny = y+dy;
                        if (nx >= 0 && nx < ds->w &&
                            ny >= 0 && ny < ds->h &&
                            state->grid[ny*ds->w+nx] == -1)
                            flags++;
                    }

                if (flags > v)
                    v |= 32;
            }

	    if ((v == -2 || v == -3) &&
		(abs(x-ui->hx) <= ui->hradius && abs(y-ui->hy) <= ui->hradius))
		v -= 20;

            if (cmoved && /* if cursor has moved, force redraw of curr and prev pos */
                ((x == cx && y == cy) || (x == ds->cur_x && y == ds->cur_y)))
              cc = true;

	    if (ds->grid[y*ds->w+x] != v || bg != ds->bg || cc) {
		draw_tile(dr, ds, COORD(x), COORD(y), v,
                          (x == cx && y == cy) ? COL_CURSOR : bg);
		ds->grid[y*ds->w+x] = v;
	    }
	}
    ds->bg = bg;
    ds->cur_x = cx; ds->cur_y = cy;

    if (!state->layout->mines)
	mines = state->layout->n;

    /*
     * Update the status bar.
     */
    {
	char statusbar[512];
	if (state->dead) {
	    sprintf(statusbar, "DEAD!");
	} else if (state->won) {
            if (state->used_solve)
                sprintf(statusbar, "Auto-solved.");
            else
                sprintf(statusbar, "COMPLETED!");
	} else {
            int safe_closed = closed - mines;
	    sprintf(statusbar, "Marked: %d / %d", markers, mines);
            if (safe_closed > 0 && safe_closed <= 9) {
                /*
                 * In the situation where there's a very small number
                 * of _non_-mine squares left unopened, it's helpful
                 * to mention that number in the status line, to save
                 * the player from having to count it up
                 * painstakingly. This is particularly important if
                 * the player has turned up the mine density to the
                 * point where game generation resorts to its weird
                 * pathological fallback of a very dense mine area
                 * with a clearing in the middle, because that often
                 * leads to a deduction you can only make by knowing
                 * that there is (say) exactly one non-mine square to
                 * find, and it's a real pain to have to count up two
                 * large numbers of squares and subtract them to get
                 * that value of 1.
                 *
                 * The threshold value of 8 for displaying this
                 * information is because that's the largest number of
                 * non-mine squares that might conceivably fit around
                 * a single central square, and the most likely way to
                 * _use_ this information is to observe that if all
                 * the remaining safe squares are adjacent to _this_
                 * square then everything else can be immediately
                 * flagged as a mine.
                 */
                if (safe_closed == 1) {
                    sprintf(statusbar + strlen(statusbar),
                            " (1 safe square remains)");
                } else {
                    sprintf(statusbar + strlen(statusbar),
                            " (%d safe squares remain)", safe_closed);
                }
            }
	}
        if (ui->deaths)
            sprintf(statusbar + strlen(statusbar),
                    "  Deaths: %d", ui->deaths);
	status_bar(dr, statusbar);
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
    if (oldstate->used_solve || newstate->used_solve)
        return 0.0F;

    if (dir > 0 && !oldstate->dead && !oldstate->won) {
	if (newstate->dead) {
	    ui->flash_is_death = true;
	    return 3 * FLASH_FRAME;
	}
	if (newstate->won) {
	    ui->flash_is_death = false;
	    return 2 * FLASH_FRAME;
	}
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
        *x = COORD(ui->cur_x);
        *y = COORD(ui->cur_y);
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    /*
     * We report the game as lost only if the player has used the
     * Solve function to reveal all the mines. Otherwise, we assume
     * they'll undo and continue play.
     */
    return state->won ? (state->used_solve ? -1 : +1) : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    if (state->dead || state->won || ui->completed || !state->layout->mines)
	return false;
    return true;
}

#ifdef COMBINED
#define thegame mines
#endif

const struct game thegame = {
    "Mines", "games.mines", "mines",
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
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    true, game_timing_state,
    BUTTON_BEATS(LEFT_BUTTON, RIGHT_BUTTON) | REQUIRE_RBUTTON,
};

#ifdef STANDALONE_OBFUSCATOR

/*
 * Vaguely useful stand-alone program which translates between
 * obfuscated and clear Mines game descriptions. Pass in a game
 * description on the command line, and if it's clear it will be
 * obfuscated and vice versa. The output text should also be a
 * valid game ID describing the same game. Like this:
 *
 * $ ./mineobfusc 9x9:4,4,mb071b49fbd1cb6a0d5868
 * 9x9:4,4,004000007c00010022080
 * $ ./mineobfusc 9x9:4,4,004000007c00010022080
 * 9x9:4,4,mb071b49fbd1cb6a0d5868
 */

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc;
    const char *err;
    int y, x;

    while (--argc > 0) {
        char *p = *++argv;
	if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s <game_id>\n", argv[0]);
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

    x = atoi(desc);
    while (*desc && *desc != ',') desc++;
    if (*desc) desc++;
    y = atoi(desc);
    while (*desc && *desc != ',') desc++;
    if (*desc) desc++;

    printf("%s:%s\n", id, describe_layout(s->layout->mines,
                                          p->w * p->h,
                                          x, y,
                                          (*desc != 'm')));

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
