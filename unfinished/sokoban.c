/*
 * sokoban.c: An implementation of the well-known Sokoban barrel-
 * pushing game. Random generation is too simplistic to be
 * credible, but the rest of the gameplay works well enough to use
 * it with hand-written level descriptions.
 */

/*
 * TODO:
 * 
 *  - I think it would be better to ditch the `prev' array, and
 *    instead make the `dist' array strictly monotonic (by having
 *    each distance be something like I*A+S, where A is the grid
 *    area, I the number of INITIAL squares trampled on, and S the
 *    number of harmless spaces moved through). This would permit
 *    the path-tracing when a pull is actually made to choose
 *    randomly from all the possible shortest routes, which would
 *    be superior in terms of eliminating directional bias.
 *     + So when tracing the path back to the current px,py, we
 * 	 look at all four adjacent squares, find the minimum
 * 	 distance, check that it's _strictly smaller_ than that of
 * 	 the current square, and restrict our choice to precisely
 * 	 those squares with that minimum distance.
 *     + The other place `prev' is currently used is in the check
 * 	 for consistency of a pull. We would have to replace the
 * 	 check for whether prev[ny*w+nx]==oy*w+ox with a check that
 * 	 made sure there was at least one adjacent square with a
 * 	 smaller distance which _wasn't_ oy*w+ox. Then when we did
 * 	 the path-tracing we'd also have to take this special case
 * 	 into account.
 * 
 *  - More discriminating choice of pull. (Snigger.)
 *     + favour putting targets in clumps
 *     + try to shoot for a reasonably consistent number of barrels
 * 	 (adjust willingness to generate a new barrel depending on
 * 	 how many are already present)
 *     + adjust willingness to break new ground depending on how
 * 	 much is already broken
 * 
 *  - generation time parameters:
 *     + enable NetHack mode (and find a better place for the hole)
 *     + decide how many of the remaining Is should be walls
 * 
 *  - at the end of generation, randomly position the starting
 *    player coordinates, probably by (somehow) reusing the same
 *    bfs currently inside the loop.
 * 
 *  - possible backtracking?
 * 
 *  - IWBNI we could spot completely unreachable bits of level at
 *    the outside, and not bother drawing grid lines for them. The
 *    NH levels currently look a bit weird with grid lines on the
 *    outside of the boundary.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/*
 * Various subsets of these constants are used during game
 * generation, game play, game IDs and the game_drawstate.
 */
#define INITIAL      'i'               /* used only in game generation */
#define SPACE        's'
#define WALL         'w'
#define PIT          'p'
#define DEEP_PIT     'd'
#define TARGET       't'
#define BARREL       'b'
#define BARRELTARGET 'f'               /* target is 'f'illed */
#define PLAYER       'u'               /* yo'u'; used in game IDs */
#define PLAYERTARGET 'v'               /* bad letter: v is to u as t is to s */
#define INVALID      '!'               /* used in drawstate to force redraw */
/*
 * We also support the use of any capital letter as a barrel, which
 * will be displayed with that letter as a label. (This facilitates
 * people distributing annotated game IDs for particular Sokoban
 * levels, so they can accompany them with verbal instructions
 * about pushing particular barrels in particular ways.) Therefore,
 * to find out whether something is a barrel, we need a test
 * function which does a bit more than just comparing to BARREL.
 * 
 * When resting on target squares, capital-letter barrels are
 * replaced with their control-character value (A -> ^A).
 */
#define IS_PLAYER(c) ( (c)==PLAYER || (c)==PLAYERTARGET )
#define IS_BARREL(c) ( (c)==BARREL || (c)==BARRELTARGET || \
                       ((c)>='A' && (c)<='Z') || ((c)>=1 && (c)<=26) )
#define IS_ON_TARGET(c) ( (c)==TARGET || (c)==BARRELTARGET || \
                          (c)==PLAYERTARGET || ((c)>=1 && (c)<=26) )
#define TARGETISE(b) ( (b)==BARREL ? BARRELTARGET : (b)-('A'-1) )
#define DETARGETISE(b) ( (b)==BARRELTARGET ? BARREL : (b)+('A'-1) )
#define BARREL_LABEL(b) ( (b)>='A'&&(b)<='Z' ? (b) : \
                          (b)>=1 && (b)<=26 ? (b)+('A'-1) : 0 )

#define DX(d) (d == 0 ? -1 : d == 2 ? +1 : 0)
#define DY(d) (d == 1 ? -1 : d == 3 ? +1 : 0)

#define FLASH_LENGTH 0.3F

enum {
    COL_BACKGROUND,
    COL_TARGET,
    COL_PIT,
    COL_DEEP_PIT,
    COL_BARREL,
    COL_PLAYER,
    COL_TEXT,
    COL_GRID,
    COL_OUTLINE,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    COL_WALL,
    NCOLOURS
};

struct game_params {
    int w, h;
    /*
     * FIXME: a parameter involving degree of filling in?
     */
};

struct game_state {
    game_params p;
    unsigned char *grid;
    int px, py;
    bool completed;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 12;
    ret->h = 10;

    return ret;
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

static const struct game_params sokoban_presets[] = {
    { 12, 10 },
    { 16, 12 },
    { 20, 16 },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params p, *ret;
    char *retname;
    char namebuf[80];

    if (i < 0 || i >= lenof(sokoban_presets))
	return false;

    p = sokoban_presets[i];
    ret = dup_params(&p);
    sprintf(namebuf, "%dx%d", ret->w, ret->h);
    retname = dupstr(namebuf);

    *params = ret;
    *name = retname;
    return true;
}

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 4 || params->h < 4)
	return "Width and height must both be at least 4";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Game generation mechanism.
 * 
 * To generate a Sokoban level, we begin with a completely blank
 * grid and make valid inverse moves. Grid squares can be in a
 * number of states. The states are:
 * 
 *  - INITIAL: this square has not as yet been touched by any
 *    inverse move, which essentially means we haven't decided what
 *    it is yet.
 * 
 *  - SPACE: this square is a space.
 * 
 *  - TARGET: this square is a space which is also the target for a
 *    barrel.
 * 
 *  - BARREL: this square contains a barrel.
 * 
 *  - BARRELTARGET: this square contains a barrel _on_ a target.
 * 
 *  - WALL: this square is a wall.
 * 
 *  - PLAYER: this square contains the player.
 * 
 *  - PLAYERTARGET: this square contains the player on a target.
 * 
 * We begin with every square of the in state INITIAL, apart from a
 * solid ring of WALLs around the edge. We randomly position the
 * PLAYER somewhere. Thereafter our valid moves are:
 * 
 *  - to move the PLAYER in one direction _pulling_ a barrel after
 *    us. For this to work, we must have SPACE or INITIAL in the
 *    direction we're moving, and BARREL or BARRELTARGET in the
 *    direction we're moving away from. We leave SPACE or TARGET
 *    respectively in the vacated square.
 * 
 *  - to create a new barrel by transforming an INITIAL square into
 *    BARRELTARGET.
 * 
 *  - to move the PLAYER freely through SPACE and TARGET squares,
 *    leaving SPACE or TARGET where it started.
 * 
 *  - to move the player through INITIAL squares, carving a tunnel
 *    of SPACEs as it goes.
 * 
 * We try to avoid destroying INITIAL squares wherever possible (if
 * there's a path to where we want to be using only SPACE, then we
 * should always use that). At the end of generation, every square
 * still in state INITIAL is one which was not required at any
 * point during generation, which means we can randomly choose
 * whether to make it SPACE or WALL.
 * 
 * It's unclear as yet what the right strategy for wall placement
 * should be. Too few WALLs will yield many alternative solutions
 * to the puzzle, whereas too many might rule out so many
 * possibilities that the intended solution becomes obvious.
 */

static void sokoban_generate(int w, int h, unsigned char *grid, int moves,
			     bool nethack, random_state *rs)
{
    struct pull {
	int ox, oy, nx, ny, score;
    };

    struct pull *pulls;
    int *dist, *prev, *heap;
    int x, y, px, py, i, j, d, heapsize, npulls;

    pulls = snewn(w * h * 4, struct pull);
    dist = snewn(w * h, int);
    prev = snewn(w * h, int);
    heap = snewn(w * h, int);

    /*
     * Configure the initial grid.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    grid[y*w+x] = (x == 0 || y == 0 || x == w-1 || y == h-1 ?
			   WALL : INITIAL);
    if (nethack)
	grid[1] = DEEP_PIT;

    /*
     * Place the player.
     */
    i = random_upto(rs, (w-2) * (h-2));
    x = 1 + i % (w-2);
    y = 1 + i / (w-2);
    grid[y*w+x] = SPACE;
    px = x;
    py = y;

    /*
     * Now loop around making random inverse Sokoban moves. In this
     * loop we aim to make one actual barrel-pull per iteration,
     * plus as many free moves as are necessary to get into
     * position for that pull.
     */
    while (moves-- >= 0) {
	/*
	 * First enumerate all the viable barrel-pulls we can
	 * possibly make, counting two pulls of the same barrel in
	 * different directions as different. We also include pulls
	 * we can perform by creating a new barrel. Each pull is
	 * marked with the amount of violence it would have to do
	 * to the grid.
	 */
	npulls = 0;
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		for (d = 0; d < 4; d++) {
		    int dx = DX(d);
		    int dy = DY(d);
		    int nx = x + dx, ny = y + dy;
		    int npx = nx + dx, npy = ny + dy;
		    int score = 0;

		    /*
		     * The candidate move is to put the player at
		     * (nx,ny), and move him to (npx,npy), pulling
		     * a barrel at (x,y) to (nx,ny). So first we
		     * must check that all those squares are within
		     * the boundaries of the grid. For this it is
		     * sufficient to check npx,npy.
		     */
		    if (npx < 0 || npx >= w || npy < 0 || npy >= h)
			continue;

		    /*
		     * (x,y) must either be a barrel, or a square
		     * which we can convert into a barrel.
		     */
		    switch (grid[y*w+x]) {
		      case BARREL: case BARRELTARGET:
			break;
		      case INITIAL:
			if (nethack)
			    continue;
			score += 10 /* new_barrel_score */;
			break;
		      case DEEP_PIT:
			if (!nethack)
			    continue;
			break;
		      default:
			continue;
		    }

		    /*
		     * (nx,ny) must either be a space, or a square
		     * which we can convert into a space.
		     */
		    switch (grid[ny*w+nx]) {
		      case SPACE: case TARGET:
			break;
		      case INITIAL:
			score += 3 /* new_space_score */;
			break;
		      default:
			continue;
		    }

		    /*
		     * (npx,npy) must also either be a space, or a
		     * square which we can convert into a space.
		     */
		    switch (grid[npy*w+npx]) {
		      case SPACE: case TARGET:
			break;
		      case INITIAL:
			score += 3 /* new_space_score */;
			break;
		      default:
			continue;
		    }

		    /*
		     * That's sufficient to tag this as a possible
		     * pull right now. We still don't know if we
		     * can reach the required player position, but
		     * that's a job for the subsequent BFS phase to
		     * tell us.
		     */
		    pulls[npulls].ox = x;
		    pulls[npulls].oy = y;
		    pulls[npulls].nx = nx;
		    pulls[npulls].ny = ny;
		    pulls[npulls].score = score;
#ifdef GENERATION_DIAGNOSTICS
		    printf("found potential pull: (%d,%d)-(%d,%d) cost %d\n",
			   pulls[npulls].ox, pulls[npulls].oy,
			   pulls[npulls].nx, pulls[npulls].ny,
			   pulls[npulls].score);
#endif
		    npulls++;
		}
#ifdef GENERATION_DIAGNOSTICS
	printf("found %d potential pulls\n", npulls);
#endif

	/*
	 * If there are no pulls available at all, we give up.
	 * 
	 * (FIXME: or perhaps backtrack?)
	 */
	if (npulls == 0)
	    break;

	/*
	 * Now we do a BFS from our current position, to find all
	 * the squares we can get the player into.
	 * 
	 * This BFS is unusually tricky. We want to give a positive
	 * distance only to squares which we have to carve through
	 * INITIALs to get to, which means we can't just stick
	 * every square we reach on the end of our to-do list.
	 * Instead, we must maintain our list as a proper priority
	 * queue.
	 */
	for (i = 0; i < w*h; i++)
	    dist[i] = prev[i] = -1;

	heap[0] = py*w+px;
	heapsize = 1;
	dist[py*w+px] = 0;

#define PARENT(n) ( ((n)-1)/2 )
#define LCHILD(n) ( 2*(n)+1 )
#define RCHILD(n) ( 2*(n)+2 )
#define SWAP(i,j) do { int swaptmp = (i); (i) = (j); (j) = swaptmp; } while (0)

	while (heapsize > 0) {
	    /*
	     * Pull the smallest element off the heap: it's at
	     * position 0. Move the arbitrary element from the very
	     * end of the heap into position 0.
	     */
	    y = heap[0] / w;
	    x = heap[0] % w;

	    heapsize--;
	    heap[0] = heap[heapsize];

	    /*
	     * Now repeatedly move that arbitrary element down the
	     * heap by swapping it with the more suitable of its
	     * children.
	     */
	    i = 0;
	    while (1) {
		int lc, rc;

		lc = LCHILD(i);
		rc = RCHILD(i);

		if (lc >= heapsize)
		    break;	       /* we've hit bottom */

		if (rc >= heapsize) {
		    /*
		     * Special case: there is only one child to
		     * check.
		     */
		    if (dist[heap[i]] > dist[heap[lc]])
			SWAP(heap[i], heap[lc]);

		    /* _Now_ we've hit bottom. */
		    break;
		} else {
		    /*
		     * The common case: there are two children and
		     * we must check them both.
		     */
		    if (dist[heap[i]] > dist[heap[lc]] ||
			dist[heap[i]] > dist[heap[rc]]) {
			/*
			 * Pick the more appropriate child to swap with
			 * (i.e. the one which would want to be the
			 * parent if one were above the other - as one
			 * is about to be).
			 */
			if (dist[heap[lc]] > dist[heap[rc]]) {
			    SWAP(heap[i], heap[rc]);
			    i = rc;
			} else {
			    SWAP(heap[i], heap[lc]);
			    i = lc;
			}
		    } else {
			/* This element is in the right place; we're done. */
			break;
		    }
		}
	    }

	    /*
	     * OK, that's given us (x,y) for this phase of the
	     * search. Now try all directions from here.
	     */

	    for (d = 0; d < 4; d++) {
		int dx = DX(d);
		int dy = DY(d);
		int nx = x + dx, ny = y + dy;
		if (nx < 0 || nx >= w || ny < 0 || ny >= h)
		    continue;
		if (grid[ny*w+nx] != SPACE && grid[ny*w+nx] != TARGET &&
		    grid[ny*w+nx] != INITIAL)
		    continue;
		if (dist[ny*w+nx] == -1) {
		    dist[ny*w+nx] = dist[y*w+x] + (grid[ny*w+nx] == INITIAL);
		    prev[ny*w+nx] = y*w+x;

		    /*
		     * Now insert ny*w+nx at the end of the heap,
		     * and move it down to its appropriate resting
		     * place.
		     */
		    i = heapsize;
		    heap[heapsize++] = ny*w+nx;

		    /*
		     * Swap element n with its parent repeatedly to
		     * preserve the heap property.
		     */

		    while (i > 0) {
			int p = PARENT(i);

			if (dist[heap[p]] > dist[heap[i]]) {
			    SWAP(heap[p], heap[i]);
			    i = p;
			} else
			    break;
		    }
		}
	    }
	}

#undef PARENT
#undef LCHILD
#undef RCHILD
#undef SWAP

#ifdef GENERATION_DIAGNOSTICS
	printf("distance map:\n");
	for (i = 0; i < h; i++) {
	    for (j = 0; j < w; j++) {
		int d = dist[i*w+j];
		int c;
		if (d < 0)
		    c = '#';
		else if (d >= 36)
		    c = '!';
		else if (d >= 10)
		    c = 'A' - 10 + d;
		else
		    c = '0' + d;
		putchar(c);
	    }
	    putchar('\n');
	}
#endif

	/*
	 * Now we can go back through the `pulls' array, adjusting
	 * the score for each pull depending on how hard it is to
	 * reach its starting point, and also throwing out any
	 * whose starting points are genuinely unreachable even
	 * with the possibility of carving through INITIAL squares.
	 */
	for (i = j = 0; i < npulls; i++) {
#ifdef GENERATION_DIAGNOSTICS
	    printf("potential pull (%d,%d)-(%d,%d)",
		   pulls[i].ox, pulls[i].oy,
		   pulls[i].nx, pulls[i].ny);
#endif
	    x = pulls[i].nx;
	    y = pulls[i].ny;
	    if (dist[y*w+x] < 0) {
#ifdef GENERATION_DIAGNOSTICS
		printf(" unreachable\n");
#endif
		continue;	       /* this pull isn't feasible at all */
	    } else {
		/*
		 * Another nasty special case we have to check is
		 * whether the initial barrel location (ox,oy) is
		 * on the path used to reach the square. This can
		 * occur if that square is in state INITIAL: the
		 * pull is initially considered valid on the basis
		 * that the INITIAL can become BARRELTARGET, and
		 * it's also considered reachable on the basis that
		 * INITIAL can be turned into SPACE, but it can't
		 * be both at once.
		 * 
		 * Fortunately, if (ox,oy) is on the path at all,
		 * it must be only one space from the end, so this
		 * is easy to spot and rule out.
		 */
		if (prev[y*w+x] == pulls[i].oy*w+pulls[i].ox) {
#ifdef GENERATION_DIAGNOSTICS
		    printf(" goes through itself\n");
#endif
		    continue;	       /* this pull isn't feasible at all */
		}
		pulls[j] = pulls[i];   /* structure copy */
		pulls[j].score += dist[y*w+x] * 3 /* new_space_score */;
#ifdef GENERATION_DIAGNOSTICS
		printf(" reachable at distance %d (cost now %d)\n",
		       dist[y*w+x], pulls[j].score);
#endif
		j++;
	    }
	}
	npulls = j;

	/*
	 * Again, if there are no pulls available at all, we give
	 * up.
	 * 
	 * (FIXME: or perhaps backtrack?)
	 */
	if (npulls == 0)
	    break;

	/*
	 * Now choose which pull to make. On the one hand we should
	 * prefer pulls which do less damage to the INITIAL squares
	 * (thus, ones for which we can already get into position
	 * via existing SPACEs, and for which the barrel already
	 * exists and doesn't have to be invented); on the other,
	 * we want to avoid _always_ preferring such pulls, on the
	 * grounds that that will lead to levels without very much
	 * stuff in.
	 * 
	 * When creating new barrels, we prefer creations which are
	 * next to existing TARGET squares.
	 * 
	 * FIXME: for the moment I'll make this very simple indeed.
	 */
	i = random_upto(rs, npulls);

	/*
	 * Actually make the pull, including carving a path to get
	 * to the site if necessary.
	 */
	x = pulls[i].nx;
	y = pulls[i].ny;
	while (prev[y*w+x] >= 0) {
	    int p;

	    if (grid[y*w+x] == INITIAL)
		grid[y*w+x] = SPACE;

	    p = prev[y*w+x];
	    y = p / w;
	    x = p % w;
	}
	px = 2*pulls[i].nx - pulls[i].ox;
	py = 2*pulls[i].ny - pulls[i].oy;
	if (grid[py*w+px] == INITIAL)
	    grid[py*w+px] = SPACE;
	if (grid[pulls[i].ny*w+pulls[i].nx] == TARGET)
	    grid[pulls[i].ny*w+pulls[i].nx] = BARRELTARGET;
	else
	    grid[pulls[i].ny*w+pulls[i].nx] = BARREL;
	if (grid[pulls[i].oy*w+pulls[i].ox] == BARREL)
	    grid[pulls[i].oy*w+pulls[i].ox] = SPACE;
	else if (grid[pulls[i].oy*w+pulls[i].ox] != DEEP_PIT)
	    grid[pulls[i].oy*w+pulls[i].ox] = TARGET;
    }

    sfree(heap);
    sfree(prev);
    sfree(dist);
    sfree(pulls);

    if (grid[py*w+px] == TARGET)
	grid[py*w+px] = PLAYERTARGET;
    else
	grid[py*w+px] = PLAYER;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h;
    char *desc;
    int desclen, descpos, descsize, prev, count;
    unsigned char *grid;
    int i, j;

    /*
     * FIXME: perhaps some more interesting means of choosing how
     * many moves to try?
     */
    grid = snewn(w*h, unsigned char);
    sokoban_generate(w, h, grid, w*h, false, rs);

    desclen = descpos = descsize = 0;
    desc = NULL;
    prev = -1;
    count = 0;
    for (i = 0; i < w*h; i++) {
        if (descsize < desclen + 40) {
            descsize = desclen + 100;
            desc = sresize(desc, descsize, char);
            desc[desclen] = '\0';
        }
        switch (grid[i]) {
          case INITIAL:
            j = 'w';                   /* FIXME: make some of these 's'? */
            break;
          case SPACE:
            j = 's';
            break;
          case WALL:
            j = 'w';
            break;
          case TARGET:
            j = 't';
            break;
          case BARREL:
            j = 'b';
            break;
          case BARRELTARGET:
            j = 'f';
            break;
          case DEEP_PIT:
            j = 'd';
            break;
          case PLAYER:
            j = 'u';
            break;
          case PLAYERTARGET:
            j = 'v';
            break;
          default:
            j = '?';
            break;
        }
        assert(j != '?');
        if (j != prev) {
            desc[desclen++] = j;
            descpos = desclen;
            prev = j;
            count = 1;
        } else {
            count++;
            desclen = descpos + sprintf(desc+descpos, "%d", count);
        }
    }

    sfree(grid);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, h = params->h;
    int area = 0;
    int nplayers = 0;

    while (*desc) {
        int c = *desc++;
        int n = 1;
        if (*desc && isdigit((unsigned char)*desc)) {
            n = atoi(desc);
            while (*desc && isdigit((unsigned char)*desc)) desc++;
        }

        area += n;

        if (c == PLAYER || c == PLAYERTARGET)
            nplayers += n;
        else if (c == INITIAL || c == SPACE || c == WALL || c == TARGET ||
                 c == PIT || c == DEEP_PIT || IS_BARREL(c))
            /* ok */;
        else
            return "Invalid character in game description";
    }

    if (area > w*h)
        return "Too much data in game description";
    if (area < w*h)
        return "Too little data in game description";
    if (nplayers < 1)
        return "No starting player position specified";
    if (nplayers > 1)
        return "More than one starting player position specified";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, h = params->h;
    game_state *state = snew(game_state);
    int i;

    state->p = *params;                /* structure copy */
    state->grid = snewn(w*h, unsigned char);
    state->px = state->py = -1;
    state->completed = false;

    i = 0;

    while (*desc) {
        int c = *desc++;
        int n = 1;
        if (*desc && isdigit((unsigned char)*desc)) {
            n = atoi(desc);
            while (*desc && isdigit((unsigned char)*desc)) desc++;
        }

        if (c == PLAYER || c == PLAYERTARGET) {
            state->py = i / w;
            state->px = i % w;
            c = IS_ON_TARGET(c) ? TARGET : SPACE;
        }

        while (n-- > 0)
            state->grid[i++] = c;
    }

    assert(i == w*h);
    assert(state->px != -1 && state->py != -1);

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int w = state->p.w, h = state->p.h;
    game_state *ret = snew(game_state);

    ret->p = state->p;                 /* structure copy */
    ret->grid = snewn(w*h, unsigned char);
    memcpy(ret->grid, state->grid, w*h);
    ret->px = state->px;
    ret->py = state->py;
    ret->completed = state->completed;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return NULL;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

static game_ui *new_ui(const game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
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
}

struct game_drawstate {
    game_params p;
    int tilesize;
    bool started;
    unsigned short *grid;
};

#define PREFERRED_TILESIZE 32
#define TILESIZE (ds->tilesize)
#define BORDER    (TILESIZE)
#define HIGHLIGHT_WIDTH (TILESIZE / 10)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )

/*
 * I'm going to need to do most of the move-type analysis in both
 * interpret_move and execute_move, so I'll abstract it out into a
 * subfunction. move_type() returns -1 for an illegal move, 0 for a
 * movement, and 1 for a push.
 */
static int move_type(const game_state *state, int dx, int dy)
{
    int w = state->p.w, h = state->p.h;
    int px = state->px, py = state->py;
    int nx, ny, nbx, nby;

    assert(dx >= -1 && dx <= +1);
    assert(dy >= -1 && dy <= +1);
    assert(dx || dy);

    nx = px + dx;
    ny = py + dy;

    /*
     * Disallow any move that goes off the grid.
     */
    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
        return -1;

    /*
     * Examine the target square of the move to see whether it's a
     * space, a barrel, or a wall.
     */

    if (state->grid[ny*w+nx] == WALL ||
        state->grid[ny*w+nx] == PIT ||
        state->grid[ny*w+nx] == DEEP_PIT)
        return -1;                     /* this one's easy; just disallow it */

    if (IS_BARREL(state->grid[ny*w+nx])) {
        /*
         * This is a push move. For a start, that means it must not
         * be diagonal.
         */
        if (dy && dx)
            return -1;

        /*
         * Now find the location of the third square involved in
         * the push, and stop if it's off the edge.
         */
        nbx = nx + dx;
        nby = ny + dy;
        if (nbx < 0 || nbx >= w || nby < 0 || nby >= h)
            return -1;

        /*
         * That third square must be able to accept a barrel.
         */
        if (state->grid[nby*w+nbx] == SPACE ||
            state->grid[nby*w+nbx] == TARGET ||
            state->grid[nby*w+nbx] == PIT ||
            state->grid[nby*w+nbx] == DEEP_PIT) {
            /*
             * The push is valid.
             */
            return 1;
        } else {
            return -1;
        }
    } else {
        /*
         * This is just an ordinary move. We've already checked the
         * target square, so the only thing left to check is that a
         * diagonal move has a space on one side to have notionally
         * gone through.
         */
        if (dx && dy &&
            state->grid[(py+dy)*w+px] != SPACE &&
            state->grid[(py+dy)*w+px] != TARGET &&
            state->grid[py*w+(px+dx)] != SPACE &&
            state->grid[py*w+(px+dx)] != TARGET)
            return -1;

        /*
         * Otherwise, the move is valid.
         */
        return 0;
    }
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int dx=0, dy=0;
    char *move;

    /*
     * Diagonal movement is supported as it is in NetHack: it's
     * for movement only (never pushing), and one of the two
     * squares adjacent to both the source and destination
     * squares must be free to move through. In other words, it
     * is only a shorthand for two orthogonal moves and cannot
     * change the nature of the actual puzzle game.
     */
    if (button == CURSOR_UP || button == (MOD_NUM_KEYPAD | '8'))
        dx = 0, dy = -1;
    else if (button == CURSOR_DOWN || button == (MOD_NUM_KEYPAD | '2'))
        dx = 0, dy = +1;
    else if (button == CURSOR_LEFT || button == (MOD_NUM_KEYPAD | '4'))
        dx = -1, dy = 0;
    else if (button == CURSOR_RIGHT || button == (MOD_NUM_KEYPAD | '6'))
        dx = +1, dy = 0;
    else if (button == (MOD_NUM_KEYPAD | '7'))
        dx = -1, dy = -1;
    else if (button == (MOD_NUM_KEYPAD | '9'))
        dx = +1, dy = -1;
    else if (button == (MOD_NUM_KEYPAD | '1'))
        dx = -1, dy = +1;
    else if (button == (MOD_NUM_KEYPAD | '3'))
        dx = +1, dy = +1;
    else if (button == LEFT_BUTTON)
    {
        if(x < COORD(state->px))
            dx = -1;
        else if (x > COORD(state->px + 1))
            dx = 1;
        if(y < COORD(state->py))
            dy = -1;
        else if (y > COORD(state->py + 1))
            dy = 1;
    }
    else
        return NULL;

    if((dx == 0) && (dy == 0))
        return(NULL);

    if (move_type(state, dx, dy) < 0)
        return NULL;

    move = snewn(2, char);
    move[1] = '\0';
    move[0] = '5' - 3*dy + dx;
    return move;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->p.w, h = state->p.h;
    int px = state->px, py = state->py;
    int dx, dy, nx, ny, nbx, nby, type, m, i;
    bool freebarrels, freetargets;
    game_state *ret;

    if (*move < '1' || *move == '5' || *move > '9' || move[1])
        return NULL;                   /* invalid move string */

    m = *move - '0';
    dx = (m + 2) % 3 - 1;
    dy = 2 - (m + 2) / 3;
    type = move_type(state, dx, dy);
    if (type < 0)
        return NULL;

    ret = dup_game(state);

    nx = px + dx;
    ny = py + dy;
    nbx = nx + dx;
    nby = ny + dy;

    if (type) {
        int b;

        /*
         * Push.
         */
        b = ret->grid[ny*w+nx];
        if (IS_ON_TARGET(b)) {
            ret->grid[ny*w+nx] = TARGET;
            b = DETARGETISE(b);
        } else
            ret->grid[ny*w+nx] = SPACE;

        if (ret->grid[nby*w+nbx] == PIT)
            ret->grid[nby*w+nbx] = SPACE;
        else if (ret->grid[nby*w+nbx] == DEEP_PIT)
            /* do nothing - the pit eats the barrel and remains there */;
        else if (ret->grid[nby*w+nbx] == TARGET)
            ret->grid[nby*w+nbx] = TARGETISE(b);
        else
            ret->grid[nby*w+nbx] = b;
    }

    ret->px = nx;
    ret->py = ny;

    /*
     * Check for completion. This is surprisingly complicated,
     * given the presence of pits and deep pits, and also the fact
     * that some Sokoban levels with pits have fewer pits than
     * barrels (due to providing spares, e.g. NetHack's). I think
     * the completion condition in fact must be that the game
     * cannot become any _more_ complete. That is, _either_ there
     * are no remaining barrels not on targets, _or_ there is a
     * good reason why any such barrels cannot be placed. The only
     * available good reason is that there are no remaining pits,
     * no free target squares, and no deep pits at all.
     */
    if (!ret->completed) {
        freebarrels = false;
        freetargets = false;
        for (i = 0; i < w*h; i++) {
            int v = ret->grid[i];

            if (IS_BARREL(v) && !IS_ON_TARGET(v))
                freebarrels = true;
            if (v == DEEP_PIT || v == PIT ||
                (!IS_BARREL(v) && IS_ON_TARGET(v)))
                freetargets = true;
        }

        if (!freebarrels || !freetargets)
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
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = 2 * BORDER + 1 + params->w * TILESIZE;
    *y = 2 * BORDER + 1 + params->h * TILESIZE;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    ret[COL_PLAYER * 3 + 0] = 0.0F;
    ret[COL_PLAYER * 3 + 1] = 1.0F;
    ret[COL_PLAYER * 3 + 2] = 0.0F;

    ret[COL_BARREL * 3 + 0] = 0.6F;
    ret[COL_BARREL * 3 + 1] = 0.3F;
    ret[COL_BARREL * 3 + 2] = 0.0F;

    ret[COL_TARGET * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0];
    ret[COL_TARGET * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1];
    ret[COL_TARGET * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2];

    ret[COL_PIT * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0] / 2;
    ret[COL_PIT * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1] / 2;
    ret[COL_PIT * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2] / 2;

    ret[COL_DEEP_PIT * 3 + 0] = 0.0F;
    ret[COL_DEEP_PIT * 3 + 1] = 0.0F;
    ret[COL_DEEP_PIT * 3 + 2] = 0.0F;

    ret[COL_TEXT * 3 + 0] = 1.0F;
    ret[COL_TEXT * 3 + 1] = 1.0F;
    ret[COL_TEXT * 3 + 2] = 1.0F;

    ret[COL_GRID * 3 + 0] = ret[COL_LOWLIGHT * 3 + 0];
    ret[COL_GRID * 3 + 1] = ret[COL_LOWLIGHT * 3 + 1];
    ret[COL_GRID * 3 + 2] = ret[COL_LOWLIGHT * 3 + 2];

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    for (i = 0; i < 3; i++) {
	ret[COL_WALL * 3 + i] = (3 * ret[COL_BACKGROUND * 3 + i] +
				 1 * ret[COL_HIGHLIGHT * 3 + i]) / 4;
    }

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->p.w, h = state->p.h;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->p = state->p;                  /* structure copy */
    ds->grid = snewn(w*h, unsigned short);
    for (i = 0; i < w*h; i++)
        ds->grid[i] = INVALID;
    ds->started = false;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, int v)
{
    int tx = COORD(x), ty = COORD(y);
    int bg = (v & 0x100 ? COL_HIGHLIGHT : COL_BACKGROUND);

    v &= 0xFF;

    clip(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1);
    draw_rect(dr, tx+1, ty+1, TILESIZE-1, TILESIZE-1, bg);

    if (v == WALL) {
	int coords[6];

        coords[0] = tx + TILESIZE;
        coords[1] = ty + TILESIZE;
        coords[2] = tx + TILESIZE;
        coords[3] = ty + 1;
        coords[4] = tx + 1;
        coords[5] = ty + TILESIZE;
        draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);

        coords[0] = tx + 1;
        coords[1] = ty + 1;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);

        draw_rect(dr, tx + 1 + HIGHLIGHT_WIDTH, ty + 1 + HIGHLIGHT_WIDTH,
                  TILESIZE - 2*HIGHLIGHT_WIDTH,
		  TILESIZE - 2*HIGHLIGHT_WIDTH, COL_WALL);
    } else if (v == PIT) {
        draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                    TILESIZE*3/7, COL_PIT, COL_OUTLINE);
    } else if (v == DEEP_PIT) {
        draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                    TILESIZE*3/7, COL_DEEP_PIT, COL_OUTLINE);
    } else {
        if (IS_ON_TARGET(v)) {
            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE*3/7, COL_TARGET, COL_OUTLINE);
        }
        if (IS_PLAYER(v)) {
            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE/3, COL_PLAYER, COL_OUTLINE);
        } else if (IS_BARREL(v)) {
            char str[2];

            draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                        TILESIZE/3, COL_BARREL, COL_OUTLINE);
            str[1] = '\0';
            str[0] = BARREL_LABEL(v);
            if (str[0]) {
                draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2,
                          FONT_VARIABLE, TILESIZE/2,
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_TEXT, str);
            }
        }
    }

    unclip(dr);
    draw_update(dr, tx, ty, TILESIZE, TILESIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->p.w, h = state->p.h /*, wh = w*h */;
    int x, y;
    int flashtype;

    if (flashtime &&
	!((int)(flashtime * 3 / FLASH_LENGTH) % 2))
	flashtype = 0x100;
    else
	flashtype = 0;

    /*
     * Initialise a fresh drawstate.
     */
    if (!ds->started) {
	/*
	 * Draw the grid lines.
	 */
	for (y = 0; y <= h; y++)
	    draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y),
		      COL_LOWLIGHT);
	for (x = 0; x <= w; x++)
	    draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h),
		      COL_LOWLIGHT);

	ds->started = true;
    }

    /*
     * Draw the grid contents.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
            int v = state->grid[y*w+x];
            if (y == state->py && x == state->px) {
                if (v == TARGET)
                    v = PLAYERTARGET;
                else {
                    assert(v == SPACE);
                    v = PLAYER;
                }
            }

	    v |= flashtype;

	    if (ds->grid[y*w+x] != v) {
		draw_tile(dr, ds, x, y, v);
		ds->grid[y*w+x] = v;
	    }
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
    if (!oldstate->completed && newstate->completed)
        return FLASH_LENGTH;
    else
        return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
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
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame sokoban
#endif

const struct game thegame = {
    "Sokoban", NULL, NULL,
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
    false, solve_game,
    false, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, game_timing_state,
    0,				       /* flags */
};
