/*
 * pegs.c: the classic Peg Solitaire game.
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

#define GRID_HOLE 0
#define GRID_PEG  1
#define GRID_OBST 2

#define GRID_CURSOR 10
#define GRID_JUMPING 20

enum {
    COL_BACKGROUND,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    COL_PEG,
    COL_CURSOR,
    NCOLOURS
};

/*
 * Grid shapes. I do some macro ickery here to ensure that my enum
 * and the various forms of my name list always match up.
 */
#define TYPELIST(A) \
    A(CROSS,Cross,cross) \
    A(OCTAGON,Octagon,octagon) \
    A(RANDOM,Random,random)
#define ENUM(upper,title,lower) TYPE_ ## upper,
#define TITLE(upper,title,lower) #title,
#define LOWER(upper,title,lower) #lower,
#define CONFIG(upper,title,lower) ":" #title

enum { TYPELIST(ENUM) TYPECOUNT };
static char const *const pegs_titletypes[] = { TYPELIST(TITLE) };
static char const *const pegs_lowertypes[] = { TYPELIST(LOWER) };
#define TYPECONFIG TYPELIST(CONFIG)

#define FLASH_FRAME 0.13F

struct game_params {
    int w, h;
    int type;
};

struct game_state {
    int w, h;
    bool completed;
    unsigned char *grid;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 7;
    ret->type = TYPE_CROSS;

    return ret;
}

static const struct game_params pegs_presets[] = {
    {5, 7, TYPE_CROSS},
    {7, 7, TYPE_CROSS},
    {5, 9, TYPE_CROSS},
    {7, 9, TYPE_CROSS},
    {9, 9, TYPE_CROSS},
    {7, 7, TYPE_OCTAGON},
    {5, 5, TYPE_RANDOM},
    {7, 7, TYPE_RANDOM},
    {9, 9, TYPE_RANDOM},
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(pegs_presets))
        return false;

    ret = snew(game_params);
    *ret = pegs_presets[i];

    strcpy(str, pegs_titletypes[ret->type]);
    if (ret->type == TYPE_CROSS || ret->type == TYPE_RANDOM)
	sprintf(str + strlen(str), " %dx%d", ret->w, ret->h);

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
    int i;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        params->h = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    } else {
        params->h = params->w;
    }

    for (i = 0; i < lenof(pegs_lowertypes); i++)
	if (!strcmp(p, pegs_lowertypes[i]))
	    params->type = i;
}

static char *encode_params(const game_params *params, bool full)
{
    char str[80];

    sprintf(str, "%dx%d", params->w, params->h);
    if (full) {
	assert(params->type >= 0 && params->type < lenof(pegs_lowertypes));
	strcat(str, pegs_lowertypes[params->type]);
    }
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[80];

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Board type";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = TYPECONFIG;
    ret[2].u.choices.selected = params->type;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->type = cfg[2].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (full && (params->w <= 3 || params->h <= 3))
	return "Width and height must both be greater than three";
    if (params->w < 1 || params->h < 1)
	return "Width and height must both be at least one";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";

    /*
     * At http://www.gibell.net/pegsolitaire/GenCross/GenCrossBoards0.html
     * George I. Bell asserts that various generalised cross-shaped
     * boards are soluble starting (and finishing) with the centre
     * hole.  We permit the symmetric ones.  Bell's notation for each
     * soluble board is listed.
     */
    if (full && params->type == TYPE_CROSS) {
        if (!((params->w == 9 && params->h == 5) || /* (3,1,3,1) */
              (params->w == 5 && params->h == 9) || /* (1,3,1,3) */
              (params->w == 9 && params->h == 9) || /* (3,3,3,3) */
              (params->w == 7 && params->h == 5) || /* (2,1,2,1) */
              (params->w == 5 && params->h == 7) || /* (1,2,1,2) */
              (params->w == 9 && params->h == 7) || /* (3,2,3,2) */
              (params->w == 7 && params->h == 9) || /* (2,3,2,3) */
              (params->w == 7 && params->h == 7)))  /* (2,2,2,2) */
            return "This board type is only supported at "
                "5x7, 5x9, 7x7, 7x9, and 9x9";
    }

    /*
     * It might be possible to implement generalisations of
     * Octagon, but only if I can find a proof that they're all
     * soluble. For the moment, therefore, I'm going to disallow
     * it at any size other than the standard one.
     */
    if (full && params->type == TYPE_OCTAGON) {
	if (params->w != 7 || params->h != 7)
	    return "This board type is only supported at 7x7";
    }
    return NULL;
}

/* ----------------------------------------------------------------------
 * Beginning of code to generate random Peg Solitaire boards.
 * 
 * This procedure is done with no aesthetic judgment, no effort at
 * symmetry, no difficulty grading and generally no finesse
 * whatsoever. We simply begin with an empty board containing a
 * single peg, and repeatedly make random reverse moves until it's
 * plausibly full. This typically yields a scrappy haphazard mess
 * with several holes, an uneven shape, and no redeeming features
 * except guaranteed solubility.
 *
 * My only concessions to sophistication are (a) to repeat the
 * generation process until I at least get a grid that touches
 * every edge of the specified board size, and (b) to try when
 * selecting moves to reuse existing space rather than expanding
 * into new space (so that non-rectangular board shape becomes a
 * factor during play).
 */

struct move {
    /*
     * x,y are the start point of the move during generation (hence
     * its endpoint during normal play).
     * 
     * dx,dy are the direction of the move during generation.
     * Absolute value 1. Hence, for example, x=3,y=5,dx=1,dy=0
     * means that the move during generation starts at (3,5) and
     * ends at (5,5), and vice versa during normal play.
     */
    int x, y, dx, dy;
    /*
     * cost is 0, 1 or 2, depending on how many GRID_OBSTs we must
     * turn into GRID_HOLEs to play this move.
     */
    int cost;
};

static int movecmp(void *av, void *bv)
{
    struct move *a = (struct move *)av;
    struct move *b = (struct move *)bv;

    if (a->y < b->y)
	return -1;
    else if (a->y > b->y)
	return +1;

    if (a->x < b->x)
	return -1;
    else if (a->x > b->x)
	return +1;

    if (a->dy < b->dy)
	return -1;
    else if (a->dy > b->dy)
	return +1;

    if (a->dx < b->dx)
	return -1;
    else if (a->dx > b->dx)
	return +1;

    return 0;
}

static int movecmpcost(void *av, void *bv)
{
    struct move *a = (struct move *)av;
    struct move *b = (struct move *)bv;

    if (a->cost < b->cost)
	return -1;
    else if (a->cost > b->cost)
	return +1;

    return movecmp(av, bv);
}

struct movetrees {
    tree234 *bymove, *bycost;
};

static void update_moves(unsigned char *grid, int w, int h, int x, int y,
			 struct movetrees *trees)
{
    struct move move;
    int dir, pos;

    /*
     * There are twelve moves that can include (x,y): three in each
     * of four directions. Check each one to see if it's possible.
     */
    for (dir = 0; dir < 4; dir++) {
	int dx, dy;

	if (dir & 1)
	    dx = 0, dy = dir - 2;
	else
	    dy = 0, dx = dir - 1;

	assert(abs(dx) + abs(dy) == 1);

	for (pos = 0; pos < 3; pos++) {
	    int v1, v2, v3;

	    move.dx = dx;
	    move.dy = dy;
	    move.x = x - pos*dx;
	    move.y = y - pos*dy;

	    if (move.x < 0 || move.x >= w || move.y < 0 || move.y >= h)
		continue;	       /* completely invalid move */
	    if (move.x+2*move.dx < 0 || move.x+2*move.dx >= w ||
		move.y+2*move.dy < 0 || move.y+2*move.dy >= h)
		continue;	       /* completely invalid move */

	    v1 = grid[move.y * w + move.x];
	    v2 = grid[(move.y+move.dy) * w + (move.x+move.dx)];
	    v3 = grid[(move.y+2*move.dy)*w + (move.x+2*move.dx)];
	    if (v1 == GRID_PEG && v2 != GRID_PEG && v3 != GRID_PEG) {
		struct move *m;

		move.cost = (v2 == GRID_OBST) + (v3 == GRID_OBST);

		/*
		 * This move is possible. See if it's already in
		 * the tree.
		 */
		m = find234(trees->bymove, &move, NULL);
		if (m && m->cost != move.cost) {
		    /*
		     * It's in the tree but listed with the wrong
		     * cost. Remove the old version.
		     */
#ifdef GENERATION_DIAGNOSTICS
		    printf("correcting %d%+d,%d%+d at cost %d\n",
			   m->x, m->dx, m->y, m->dy, m->cost);
#endif
		    del234(trees->bymove, m);
		    del234(trees->bycost, m);
		    sfree(m);
		    m = NULL;
		}
		if (!m) {
		    struct move *m, *m2;
		    m = snew(struct move);
		    *m = move;
		    m2 = add234(trees->bymove, m);
		    m2 = add234(trees->bycost, m);
		    assert(m2 == m);
#ifdef GENERATION_DIAGNOSTICS
		    printf("adding %d%+d,%d%+d at cost %d\n",
			   move.x, move.dx, move.y, move.dy, move.cost);
#endif
		} else {
#ifdef GENERATION_DIAGNOSTICS
		    printf("not adding %d%+d,%d%+d at cost %d\n",
			   move.x, move.dx, move.y, move.dy, move.cost);
#endif
		}
	    } else {
		/*
		 * This move is impossible. If it is already in the
		 * tree, delete it.
		 * 
		 * (We make use here of the fact that del234
		 * doesn't have to be passed a pointer to the
		 * _actual_ element it's deleting: it merely needs
		 * one that compares equal to it, and it will
		 * return the one it deletes.)
		 */
		struct move *m = del234(trees->bymove, &move);
#ifdef GENERATION_DIAGNOSTICS
		printf("%sdeleting %d%+d,%d%+d\n", m ? "" : "not ",
		       move.x, move.dx, move.y, move.dy);
#endif
		if (m) {
		    del234(trees->bycost, m);
		    sfree(m);
		}
	    }
	}
    }
}

static void pegs_genmoves(unsigned char *grid, int w, int h, random_state *rs)
{
    struct movetrees atrees, *trees = &atrees;
    struct move *m;
    int x, y, i, nmoves;

    trees->bymove = newtree234(movecmp);
    trees->bycost = newtree234(movecmpcost);

    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (grid[y*w+x] == GRID_PEG)
		update_moves(grid, w, h, x, y, trees);

    nmoves = 0;

    while (1) {
	int limit, maxcost, index;
	struct move mtmp, move, *m;

	/*
	 * See how many moves we can make at zero cost. Make one,
	 * if possible. Failing that, make a one-cost move, and
	 * then a two-cost one.
	 * 
	 * After filling at least half the input grid, we no longer
	 * accept cost-2 moves: if that's our only option, we give
	 * up and finish.
	 */
	mtmp.y = h+1;
	maxcost = (nmoves < w*h/2 ? 2 : 1);
	m = NULL;		       /* placate optimiser */
	for (mtmp.cost = 0; mtmp.cost <= maxcost; mtmp.cost++) {
	    limit = -1;
	    m = findrelpos234(trees->bycost, &mtmp, NULL, REL234_LT, &limit);
#ifdef GENERATION_DIAGNOSTICS
	    printf("%d moves available with cost %d\n", limit+1, mtmp.cost);
#endif
	    if (m)
		break;
	}
	if (!m)
	    break;

	index = random_upto(rs, limit+1);
	move = *(struct move *)index234(trees->bycost, index);

#ifdef GENERATION_DIAGNOSTICS
	printf("selecting move %d%+d,%d%+d at cost %d\n",
	       move.x, move.dx, move.y, move.dy, move.cost);
#endif

	grid[move.y * w + move.x] = GRID_HOLE;
	grid[(move.y+move.dy) * w + (move.x+move.dx)] = GRID_PEG;
	grid[(move.y+2*move.dy)*w + (move.x+2*move.dx)] = GRID_PEG;

	for (i = 0; i <= 2; i++) {
	    int tx = move.x + i*move.dx;
	    int ty = move.y + i*move.dy;
	    update_moves(grid, w, h, tx, ty, trees);
	}

	nmoves++;
    }

    while ((m = delpos234(trees->bymove, 0)) != NULL) {
	del234(trees->bycost, m);
	sfree(m);
    }
    freetree234(trees->bymove);
    freetree234(trees->bycost);
}

static void pegs_generate(unsigned char *grid, int w, int h, random_state *rs)
{
    while (1) {
	int x, y, extremes;

	memset(grid, GRID_OBST, w*h);
	grid[(h/2) * w + (w/2)] = GRID_PEG;
#ifdef GENERATION_DIAGNOSTICS
	printf("beginning move selection\n");
#endif
	pegs_genmoves(grid, w, h, rs);
#ifdef GENERATION_DIAGNOSTICS
	printf("finished move selection\n");
#endif

	extremes = 0;
	for (y = 0; y < h; y++) {
	    if (grid[y*w+0] != GRID_OBST)
		extremes |= 1;
	    if (grid[y*w+w-1] != GRID_OBST)
		extremes |= 2;
	}
	for (x = 0; x < w; x++) {
	    if (grid[0*w+x] != GRID_OBST)
		extremes |= 4;
	    if (grid[(h-1)*w+x] != GRID_OBST)
		extremes |= 8;
	}

	if (extremes == 15)
	    break;
#ifdef GENERATION_DIAGNOSTICS
	printf("insufficient extent; trying again\n");
#endif
    }
#ifdef GENERATION_DIAGNOSTICS
    fflush(stdout);
#endif
}

/* ----------------------------------------------------------------------
 * End of board generation code. Now for the client code which uses
 * it as part of the puzzle.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h;
    unsigned char *grid;
    char *ret;
    int i;

    grid = snewn(w*h, unsigned char);
    if (params->type == TYPE_RANDOM) {
	pegs_generate(grid, w, h, rs);
    } else {
	int x, y, cx, cy, v;

	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		v = GRID_OBST;	       /* placate optimiser */
		switch (params->type) {
		  case TYPE_CROSS:
		    cx = abs(x - w/2);
		    cy = abs(y - h/2);
		    if (cx == 0 && cy == 0)
			v = GRID_HOLE;
		    else if (cx > 1 && cy > 1)
			v = GRID_OBST;
		    else
			v = GRID_PEG;
		    break;
		  case TYPE_OCTAGON:
		    cx = abs(x - w/2);
		    cy = abs(y - h/2);
		    if (cx + cy > 1 + max(w,h)/2)
			v = GRID_OBST;
		    else
			v = GRID_PEG;
		    break;
		}
		grid[y*w+x] = v;
	    }

	if (params->type == TYPE_OCTAGON) {
	    /*
	     * The octagonal (European) solitaire layout is
	     * actually _insoluble_ with the starting hole at the
	     * centre. Here's a proof:
	     * 
	     * Colour the squares of the board diagonally in
	     * stripes of three different colours, which I'll call
	     * A, B and C. So the board looks like this:
	     * 
	     *     A B C
	     *   A B C A B
	     * A B C A B C A
	     * B C A B C A B
	     * C A B C A B C
	     *   B C A B C
	     *     A B C
	     * 
	     * Suppose we keep running track of the number of pegs
	     * occuping each colour of square. This colouring has
	     * the property that any valid move whatsoever changes
	     * all three of those counts by one (two of them go
	     * down and one goes up), which means that the _parity_
	     * of every count flips on every move.
	     * 
	     * If the centre square starts off unoccupied, then
	     * there are twelve pegs on each colour and all three
	     * counts start off even; therefore, after 35 moves all
	     * three counts would have to be odd, which isn't
	     * possible if there's only one peg left. []
	     * 
	     * This proof works just as well if the starting hole
	     * is _any_ of the thirteen positions labelled B. Also,
	     * we can stripe the board in the opposite direction
	     * and rule out any square labelled B in that colouring
	     * as well. This leaves:
	     * 
	     *     Y n Y
	     *   n n Y n n
	     * Y n n Y n n Y
	     * n Y Y n Y Y n
	     * Y n n Y n n Y
	     *   n n Y n n
	     *     Y n Y
	     * 
	     * where the ns are squares we've proved insoluble, and
	     * the Ys are the ones remaining.
	     * 
	     * That doesn't prove all those starting positions to
	     * be soluble, of course; they're merely the ones we
	     * _haven't_ proved to be impossible. Nevertheless, it
	     * turns out that they are all soluble, so when the
	     * user requests an Octagon board the simplest thing is
	     * to pick one of these at random.
	     * 
	     * Rather than picking equiprobably from those twelve
	     * positions, we'll pick equiprobably from the three
	     * equivalence classes
	     */
	    switch (random_upto(rs, 3)) {
	      case 0:
		/* Remove a random corner piece. */
		{
		    int dx, dy;

		    dx = random_upto(rs, 2) * 2 - 1;   /* +1 or -1 */
		    dy = random_upto(rs, 2) * 2 - 1;   /* +1 or -1 */
		    if (random_upto(rs, 2))
			dy *= 3;
		    else
			dx *= 3;
		    grid[(3+dy)*w+(3+dx)] = GRID_HOLE;
		}
		break;
	      case 1:
		/* Remove a random piece two from the centre. */
		{
		    int dx, dy;
		    dx = 2 * (random_upto(rs, 2) * 2 - 1);
		    if (random_upto(rs, 2))
			dy = 0;
		    else
			dy = dx, dx = 0;
		    grid[(3+dy)*w+(3+dx)] = GRID_HOLE;
		}
		break;
	      default /* case 2 */:
		/* Remove a random piece one from the centre. */
		{
		    int dx, dy;
		    dx = random_upto(rs, 2) * 2 - 1;
		    if (random_upto(rs, 2))
			dy = 0;
		    else
			dy = dx, dx = 0;
		    grid[(3+dy)*w+(3+dx)] = GRID_HOLE;
		}
		break;
	    }
	}
    }

    /*
     * Encode a game description which is simply a long list of P
     * for peg, H for hole or O for obstacle.
     */
    ret = snewn(w*h+1, char);
    for (i = 0; i < w*h; i++)
	ret[i] = (grid[i] == GRID_PEG ? 'P' :
		  grid[i] == GRID_HOLE ? 'H' : 'O');
    ret[w*h] = '\0';

    sfree(grid);

    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int len, i, npeg = 0, nhole = 0;

    len = params->w * params->h;

    if (len != strlen(desc))
	return "Game description is wrong length";
    if (len != strspn(desc, "PHO"))
	return "Invalid character in game description";
    for (i = 0; i < len; i++) {
        npeg += desc[i] == 'P';
        nhole += desc[i] == 'H';
    }
    /* The minimal soluble game has two pegs and a hole: "3x1:PPH". */
    if (npeg < 2)
        return "Too few pegs in game description";
    if (nhole < 1)
        return "Too few holes in game description";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, h = params->h;
    game_state *state = snew(game_state);
    int i;

    state->w = w;
    state->h = h;
    state->completed = false;
    state->grid = snewn(w*h, unsigned char);
    for (i = 0; i < w*h; i++)
	state->grid[i] = (desc[i] == 'P' ? GRID_PEG :
			  desc[i] == 'H' ? GRID_HOLE : GRID_OBST);

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int w = state->w, h = state->h;
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->completed = state->completed;
    ret->grid = snewn(w*h, unsigned char);
    memcpy(ret->grid, state->grid, w*h);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->w, h = state->h;
    int x, y;
    char *ret;

    ret = snewn((w+1)*h + 1, char);

    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++)
	    ret[y*(w+1)+x] = (state->grid[y*w+x] == GRID_HOLE ? '-' :
			      state->grid[y*w+x] == GRID_PEG ? '*' : ' ');
	ret[y*(w+1)+w] = '\n';
    }
    ret[h*(w+1)] = '\0';

    return ret;
}

struct game_ui {
    bool dragging;                     /* is a drag in progress? */
    int sx, sy;			       /* grid coords of drag start cell */
    int dx, dy;			       /* pixel coords of current drag posn */
    int cur_x, cur_y;
    bool cur_visible, cur_jumping;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    int x, y, v;

    ui->sx = ui->sy = ui->dx = ui->dy = 0;
    ui->dragging = false;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->cur_jumping = false;

    /* make sure we start the cursor somewhere on the grid. */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            v = state->grid[y*state->w+x];
            if (v == GRID_PEG || v == GRID_HOLE) {
                ui->cur_x = x; ui->cur_y = y;
                goto found;
            }
        }
    }
    assert(!"new_ui found nowhere for cursor");
found:

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
    /*
     * Cancel a drag, in case the source square has become
     * unoccupied.
     */
    ui->dragging = false;

    /*
     * Also, cancel a keyboard-driven jump if one is half way to being
     * input.
     */
    ui->cur_jumping = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int w = state->w;

    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) return "";
        if (ui->cur_jumping) return "Cancel";
        if (state->grid[ui->cur_y*w+ui->cur_x] == GRID_PEG) return "Select";
    }
    return "";
}

#define PREFERRED_TILE_SIZE 33
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE / 2)

#define HIGHLIGHT_WIDTH (TILESIZE / 16)

#define COORD(x)     ( BORDER + (x) * TILESIZE )
#define FROMCOORD(x) ( ((x) + TILESIZE - BORDER) / TILESIZE - 1 )

struct game_drawstate {
    int tilesize;
    blitter *drag_background;
    bool dragging;
    int dragx, dragy;
    int w, h;
    unsigned char *grid;
    bool started;
    int bgcolour;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->w, h = state->h;
    char buf[80];

    if (button == LEFT_BUTTON) {
	int tx, ty;

	/*
	 * Left button down: we attempt to start a drag.
	 */
	
	/*
	 * There certainly shouldn't be a current drag in progress,
	 * unless the midend failed to send us button events in
	 * order; it has a responsibility to always get that right,
	 * so we can legitimately punish it by failing an
	 * assertion.
	 */
	assert(!ui->dragging);

	tx = FROMCOORD(x);
	ty = FROMCOORD(y);
	if (tx >= 0 && tx < w && ty >= 0 && ty < h &&
	    state->grid[ty*w+tx] == GRID_PEG) {
	    ui->dragging = true;
	    ui->sx = tx;
	    ui->sy = ty;
	    ui->dx = x;
	    ui->dy = y;
            ui->cur_visible = false;
            ui->cur_jumping = false;
	    return UI_UPDATE;
	}
    } else if (button == LEFT_DRAG && ui->dragging) {
	/*
	 * Mouse moved; just move the peg being dragged.
	 */
	ui->dx = x;
	ui->dy = y;
	return UI_UPDATE;
    } else if (button == LEFT_RELEASE && ui->dragging) {
	int tx, ty, dx, dy;

	/*
	 * Button released. Identify the target square of the drag,
	 * see if it represents a valid move, and if so make it.
	 */
	ui->dragging = false;	       /* cancel the drag no matter what */
	tx = FROMCOORD(x);
	ty = FROMCOORD(y);
	if (tx < 0 || tx >= w || ty < 0 || ty >= h)
	    return UI_UPDATE;	       /* target out of range */
	dx = tx - ui->sx;
	dy = ty - ui->sy;
	if (max(abs(dx),abs(dy)) != 2 || min(abs(dx),abs(dy)) != 0)
	    return UI_UPDATE;	       /* move length was wrong */
	dx /= 2;
	dy /= 2;

	if (state->grid[ty*w+tx] != GRID_HOLE ||
	    state->grid[(ty-dy)*w+(tx-dx)] != GRID_PEG ||
	    state->grid[ui->sy*w+ui->sx] != GRID_PEG)
	    return UI_UPDATE;	       /* grid contents were invalid */

	/*
	 * We have a valid move. Encode it simply as source and
	 * destination coordinate pairs.
	 */
	sprintf(buf, "%d,%d-%d,%d", ui->sx, ui->sy, tx, ty);
	return dupstr(buf);
    } else if (IS_CURSOR_MOVE(button)) {
        if (!ui->cur_jumping) {
            /* Not jumping; move cursor as usual, making sure we don't
             * leave the gameboard (which may be an irregular shape) */
            int cx = ui->cur_x, cy = ui->cur_y;
            move_cursor(button, &cx, &cy, w, h, false);
            ui->cur_visible = true;
            if (state->grid[cy*w+cx] == GRID_HOLE ||
                state->grid[cy*w+cx] == GRID_PEG) {
                ui->cur_x = cx;
                ui->cur_y = cy;
            }
            return UI_UPDATE;
        } else {
            int dx, dy, mx, my, jx, jy;

            /* We're jumping; if the requested direction has a hole, and
             * there's a peg in the way, */
            assert(state->grid[ui->cur_y*w+ui->cur_x] == GRID_PEG);
            dx = (button == CURSOR_RIGHT) ? 1 : (button == CURSOR_LEFT) ? -1 : 0;
            dy = (button == CURSOR_DOWN) ? 1 : (button == CURSOR_UP) ? -1 : 0;

            mx = ui->cur_x+dx; my = ui->cur_y+dy;
            jx = mx+dx; jy = my+dy;

            ui->cur_jumping = false; /* reset, whatever. */
            if (jx >= 0 && jy >= 0 && jx < w && jy < h &&
                state->grid[my*w+mx] == GRID_PEG &&
                state->grid[jy*w+jx] == GRID_HOLE) {
                /* Move cursor to the jumped-to location (this felt more
                 * natural while playtesting) */
                sprintf(buf, "%d,%d-%d,%d", ui->cur_x, ui->cur_y, jx, jy);
                ui->cur_x = jx; ui->cur_y = jy;
                return dupstr(buf);
            }
            return UI_UPDATE;
        }
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        if (ui->cur_jumping) {
            ui->cur_jumping = false;
            return UI_UPDATE;
        }
        if (state->grid[ui->cur_y*w+ui->cur_x] == GRID_PEG) {
            /* cursor is on peg: next arrow-move wil jump. */
            ui->cur_jumping = true;
            return UI_UPDATE;
        }
        return NULL;
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->w, h = state->h;
    int sx, sy, tx, ty;
    game_state *ret;

    if (sscanf(move, "%d,%d-%d,%d", &sx, &sy, &tx, &ty) == 4) {
	int mx, my, dx, dy;

	if (sx < 0 || sx >= w || sy < 0 || sy >= h)
	    return NULL;	       /* source out of range */
	if (tx < 0 || tx >= w || ty < 0 || ty >= h)
	    return NULL;	       /* target out of range */

	dx = tx - sx;
	dy = ty - sy;
	if (max(abs(dx),abs(dy)) != 2 || min(abs(dx),abs(dy)) != 0)
	    return NULL;	       /* move length was wrong */
	mx = sx + dx/2;
	my = sy + dy/2;

	if (state->grid[sy*w+sx] != GRID_PEG ||
	    state->grid[my*w+mx] != GRID_PEG ||
	    state->grid[ty*w+tx] != GRID_HOLE)
	    return NULL;	       /* grid contents were invalid */

	ret = dup_game(state);
	ret->grid[sy*w+sx] = GRID_HOLE;
	ret->grid[my*w+mx] = GRID_HOLE;
	ret->grid[ty*w+tx] = GRID_PEG;

        /*
         * Opinion varies on whether getting to a single peg counts as
         * completing the game, or whether that peg has to be at a
         * specific location (central in the classic cross game, for
         * instance). For now we take the former, rather lax position.
         */
        if (!ret->completed) {
            int count = 0, i;
            for (i = 0; i < w*h; i++)
                if (ret->grid[i] == GRID_PEG)
                    count++;
            if (count == 1)
                ret->completed = true;
        }

	return ret;
    }
    return NULL;
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

    *x = TILESIZE * params->w + 2 * BORDER;
    *y = TILESIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;

    assert(TILESIZE > 0);

    assert(!ds->drag_background);      /* set_size is never called twice */
    ds->drag_background = blitter_new(dr, TILESIZE, TILESIZE);
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_PEG * 3 + 0] = 0.0F;
    ret[COL_PEG * 3 + 1] = 0.0F;
    ret[COL_PEG * 3 + 2] = 1.0F;

    ret[COL_CURSOR * 3 + 0] = 0.5F;
    ret[COL_CURSOR * 3 + 1] = 0.5F;
    ret[COL_CURSOR * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->w, h = state->h;
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;		       /* not decided yet */

    /* We can't allocate the blitter rectangle for the drag background
     * until we know what size to make it. */
    ds->drag_background = NULL;
    ds->dragging = false;

    ds->w = w;
    ds->h = h;
    ds->grid = snewn(w*h, unsigned char);
    memset(ds->grid, 255, w*h);

    ds->started = false;
    ds->bgcolour = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    if (ds->drag_background)
	blitter_free(dr, ds->drag_background);
    sfree(ds->grid);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds,
		      int x, int y, int v, int bgcolour)
{
    bool cursor = false, jumping = false;
    int bg;

    if (bgcolour >= 0) {
	draw_rect(dr, x, y, TILESIZE, TILESIZE, bgcolour);
    }
    if (v >= GRID_JUMPING) {
        jumping = true; v -= GRID_JUMPING;
    }
    if (v >= GRID_CURSOR) {
        cursor = true; v -= GRID_CURSOR;
    }

    if (v == GRID_HOLE) {
        bg = cursor ? COL_HIGHLIGHT : COL_LOWLIGHT;
        assert(!jumping); /* can't jump from a hole! */
	draw_circle(dr, x+TILESIZE/2, y+TILESIZE/2, TILESIZE/4,
                    bg, bg);
    } else if (v == GRID_PEG) {
        bg = (cursor || jumping) ? COL_CURSOR : COL_PEG;
	draw_circle(dr, x+TILESIZE/2, y+TILESIZE/2, TILESIZE/3,
		    bg, bg);
        bg = (!cursor || jumping) ? COL_PEG : COL_CURSOR;
        draw_circle(dr, x+TILESIZE/2, y+TILESIZE/2, TILESIZE/4,
                    bg, bg);
    }

    draw_update(dr, x, y, TILESIZE, TILESIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->w, h = state->h;
    int x, y;
    int bgcolour;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
        bgcolour = COL_BACKGROUND;

    /*
     * Erase the sprite currently being dragged, if any.
     */
    if (ds->dragging) {
	assert(ds->drag_background);
        blitter_load(dr, ds->drag_background, ds->dragx, ds->dragy);
        draw_update(dr, ds->dragx, ds->dragy, TILESIZE, TILESIZE);
	ds->dragging = false;
    }

    if (!ds->started) {
	/*
	 * Draw relief marks around all the squares that aren't
	 * GRID_OBST.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		if (state->grid[y*w+x] != GRID_OBST) {
		    /*
		     * First pass: draw the full relief square.
		     */
		    int coords[6];
		    coords[0] = COORD(x+1) + HIGHLIGHT_WIDTH - 1;
		    coords[1] = COORD(y) - HIGHLIGHT_WIDTH;
		    coords[2] = COORD(x) - HIGHLIGHT_WIDTH;
		    coords[3] = COORD(y+1) + HIGHLIGHT_WIDTH - 1;
		    coords[4] = COORD(x) - HIGHLIGHT_WIDTH;
		    coords[5] = COORD(y) - HIGHLIGHT_WIDTH;
		    draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
		    coords[4] = COORD(x+1) + HIGHLIGHT_WIDTH - 1;
		    coords[5] = COORD(y+1) + HIGHLIGHT_WIDTH - 1;
		    draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);
		}
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		if (state->grid[y*w+x] != GRID_OBST) {
		    /*
		     * Second pass: draw everything but the two
		     * diagonal corners.
		     */
		    draw_rect(dr, COORD(x) - HIGHLIGHT_WIDTH,
			      COORD(y) - HIGHLIGHT_WIDTH,
			      TILESIZE + HIGHLIGHT_WIDTH,
			      TILESIZE + HIGHLIGHT_WIDTH, COL_HIGHLIGHT);
		    draw_rect(dr, COORD(x),
			      COORD(y),
			      TILESIZE + HIGHLIGHT_WIDTH,
			      TILESIZE + HIGHLIGHT_WIDTH, COL_LOWLIGHT);
		}
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		if (state->grid[y*w+x] != GRID_OBST) {
		    /*
		     * Third pass: draw a trapezium on each edge.
		     */
		    int coords[8];
		    int dx, dy, s, sn, c;

		    for (dx = 0; dx < 2; dx++) {
			dy = 1 - dx;
			for (s = 0; s < 2; s++) {
			    sn = 2*s - 1;
			    c = s ? COL_LOWLIGHT : COL_HIGHLIGHT;

			    coords[0] = COORD(x) + (s*dx)*(TILESIZE-1);
			    coords[1] = COORD(y) + (s*dy)*(TILESIZE-1);
			    coords[2] = COORD(x) + (s*dx+dy)*(TILESIZE-1);
			    coords[3] = COORD(y) + (s*dy+dx)*(TILESIZE-1);
			    coords[4] = coords[2] - HIGHLIGHT_WIDTH * (dy-sn*dx);
			    coords[5] = coords[3] - HIGHLIGHT_WIDTH * (dx-sn*dy);
			    coords[6] = coords[0] + HIGHLIGHT_WIDTH * (dy+sn*dx);
			    coords[7] = coords[1] + HIGHLIGHT_WIDTH * (dx+sn*dy);
			    draw_polygon(dr, coords, 4, c, c);
			}
		    }
		}
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		if (state->grid[y*w+x] != GRID_OBST) {
		    /*
		     * Second pass: draw everything but the two
		     * diagonal corners.
		     */
		    draw_rect(dr, COORD(x),
			      COORD(y),
			      TILESIZE,
			      TILESIZE, COL_BACKGROUND);
		}

	ds->started = true;

	draw_update(dr, 0, 0,
		    TILESIZE * state->w + 2 * BORDER,
		    TILESIZE * state->h + 2 * BORDER);
    }

    /*
     * Loop over the grid redrawing anything that looks as if it
     * needs it.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    int v;

	    v = state->grid[y*w+x];
	    /*
	     * Blank the source of a drag so it looks as if the
	     * user picked the peg up physically.
	     */
	    if (ui->dragging && ui->sx == x && ui->sy == y && v == GRID_PEG)
		v = GRID_HOLE;

            if (ui->cur_visible && ui->cur_x == x && ui->cur_y == y)
                v += ui->cur_jumping ? GRID_JUMPING : GRID_CURSOR;

	    if (v != GRID_OBST &&
                (bgcolour != ds->bgcolour || /* always redraw when flashing */
                 v != ds->grid[y*w+x])) {
		draw_tile(dr, ds, COORD(x), COORD(y), v, bgcolour);
                ds->grid[y*w+x] = v;
	    }
	}

    /*
     * Draw the dragging sprite if any.
     */
    if (ui->dragging) {
	ds->dragging = true;
	ds->dragx = ui->dx - TILESIZE/2;
	ds->dragy = ui->dy - TILESIZE/2;
	blitter_save(dr, ds->drag_background, ds->dragx, ds->dragy);
	draw_tile(dr, ds, ds->dragx, ds->dragy, GRID_PEG, -1);
    }

    ds->bgcolour = bgcolour;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed)
        return 2 * FLASH_FRAME;
    else
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
        *w = *h = TILESIZE;
    }
}

static int game_status(const game_state *state)
{
    /*
     * Dead-end situations are assumed to be rescuable by Undo, so we
     * don't bother to identify them and return -1.
     */
    return state->completed ? +1 : 0;
}

#ifdef COMBINED
#define thegame pegs
#endif

const struct game thegame = {
    "Pegs", "games.pegs", "pegs",
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
    false, NULL, /* solve */
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
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
