/*
 * pearl.c: Nikoli's `Masyu' puzzle. 
 */

/*
 * TODO:
 *
 *  - The current keyboard cursor mechanism works well on ordinary PC
 *    keyboards, but for platforms with only arrow keys and a select
 *    button or two, we may at some point need a simpler one which can
 *    handle 'x' markings without needing shift keys. For instance, a
 *    cursor with twice the grid resolution, so that it can range
 *    across face centres, edge centres and vertices; 'clicks' on face
 *    centres begin a drag as currently, clicks on edges toggle
 *    markings, and clicks on vertices are ignored (but it would be
 *    too confusing not to let the cursor rest on them). But I'm
 *    pretty sure that would be less pleasant to play on a full
 *    keyboard, so probably a #ifdef would be the thing.
 *
 *  - Generation is still pretty slow, due to difficulty coming up in
 *    the first place with a loop that makes a soluble puzzle even
 *    with all possible clues filled in.
 *     + A possible alternative strategy to further tuning of the
 * 	 existing loop generator would be to throw the entire
 * 	 mechanism out and instead write a different generator from
 * 	 scratch which evolves the solution along with the puzzle:
 * 	 place a few clues, nail down a bit of the loop, place another
 * 	 clue, nail down some more, etc. However, I don't have a
 * 	 detailed plan for any such mechanism, so it may be a pipe
 * 	 dream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"
#include "grid.h"
#include "loopgen.h"

#define SWAP(i,j) do { int swaptmp = (i); (i) = (j); (j) = swaptmp; } while (0)

#define NOCLUE 0
#define CORNER 1
#define STRAIGHT 2

#define R 1
#define U 2
#define L 4
#define D 8

#define DX(d) ( ((d)==R) - ((d)==L) )
#define DY(d) ( ((d)==D) - ((d)==U) )

#define F(d) (((d << 2) | (d >> 2)) & 0xF)
#define C(d) (((d << 3) | (d >> 1)) & 0xF)
#define A(d) (((d << 1) | (d >> 3)) & 0xF)

#define LR (L | R)
#define RL (R | L)
#define UD (U | D)
#define DU (D | U)
#define LU (L | U)
#define UL (U | L)
#define LD (L | D)
#define DL (D | L)
#define RU (R | U)
#define UR (U | R)
#define RD (R | D)
#define DR (D | R)
#define BLANK 0
#define UNKNOWN 15

#define bLR (1 << LR)
#define bRL (1 << RL)
#define bUD (1 << UD)
#define bDU (1 << DU)
#define bLU (1 << LU)
#define bUL (1 << UL)
#define bLD (1 << LD)
#define bDL (1 << DL)
#define bRU (1 << RU)
#define bUR (1 << UR)
#define bRD (1 << RD)
#define bDR (1 << DR)
#define bBLANK (1 << BLANK)

enum {
    COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT,
    COL_CURSOR_BACKGROUND = COL_LOWLIGHT,
    COL_BLACK, COL_WHITE,
    COL_ERROR, COL_GRID, COL_FLASH,
    COL_DRAGON, COL_DRAGOFF,
    NCOLOURS
};

/* Macro ickery copied from slant.c */
#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(TRICKY,Tricky,t)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const pearl_diffnames[] = { DIFFLIST(TITLE) "(count)" };
static char const pearl_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    int w, h;
    int difficulty;
    bool nosolve;        /* XXX remove me! */
};

struct shared_state {
    int w, h, sz;
    char *clues;         /* size w*h */
    int refcnt;
};

#define INGRID(state, gx, gy) ((gx) >= 0 && (gx) < (state)->shared->w && \
                               (gy) >= 0 && (gy) < (state)->shared->h)
struct game_state {
    struct shared_state *shared;
    char *lines;        /* size w*h: lines placed */
    char *errors;       /* size w*h: errors detected */
    char *marks;        /* size w*h: 'no line here' marks placed. */
    bool completed, used_solve;
};

#define DEFAULT_PRESET 3

static const struct game_params pearl_presets[] = {
    {6, 6,      DIFF_EASY},
    {6, 6,      DIFF_TRICKY},
    {8, 8,      DIFF_EASY},
    {8, 8,      DIFF_TRICKY},
    {10, 10,    DIFF_EASY},
    {10, 10,    DIFF_TRICKY},
    {12, 8,     DIFF_EASY},
    {12, 8,     DIFF_TRICKY},
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    *ret = pearl_presets[DEFAULT_PRESET];
    ret->nosolve = false;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[64];

    if (i < 0 || i >= lenof(pearl_presets)) return false;

    ret = default_params();
    *ret = pearl_presets[i]; /* struct copy */
    *params = ret;

    sprintf(buf, "%dx%d %s",
            pearl_presets[i].w, pearl_presets[i].h,
            pearl_diffnames[pearl_presets[i].difficulty]);
    *name = dupstr(buf);

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
    while (*string && isdigit((unsigned char) *string)) ++string;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }

    ret->difficulty = DIFF_EASY;
    if (*string == 'd') {
	int i;
	string++;
	for (i = 0; i < DIFFCOUNT; i++)
	    if (*string == pearl_diffchars[i])
		ret->difficulty = i;
	if (*string) string++;
    }

    ret->nosolve = false;
    if (*string == 'n') {
        ret->nosolve = true;
        string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%d", params->w, params->h);
    if (full)
        sprintf(buf + strlen(buf), "d%c%s",
                pearl_diffchars[params->difficulty],
                params->nosolve ? "n" : "");
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[64];

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = DIFFCONFIG;
    ret[2].u.choices.selected = params->difficulty;

    ret[3].name = "Allow unsoluble";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->nosolve;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->difficulty = cfg[2].u.choices.selected;
    ret->nosolve = cfg[3].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 5) return "Width must be at least five";
    if (params->h < 5) return "Height must be at least five";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (params->difficulty < 0 || params->difficulty >= DIFFCOUNT)
        return "Unknown difficulty level";
    if (params->difficulty >= DIFF_TRICKY && params->w + params->h < 11)
	return "Width or height must be at least six for Tricky";

    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

static int pearl_solve(int w, int h, char *clues, char *result,
                       int difficulty, bool partial)
{
    int W = 2*w+1, H = 2*h+1;
    short *workspace;
    int *dsf, *dsfsize;
    int x, y, b, d;
    int ret = -1;

    /*
     * workspace[(2*y+1)*W+(2*x+1)] indicates the possible nature
     * of the square (x,y), as a logical OR of bitfields.
     * 
     * workspace[(2*y)*W+(2*x+1)], for x odd and y even, indicates
     * whether the horizontal edge between (x,y) and (x+1,y) is
     * connected (1), disconnected (2) or unknown (3).
     * 
     * workspace[(2*y+1)*W+(2*x)], indicates the same about the
     * vertical edge between (x,y) and (x,y+1).
     * 
     * Initially, every square is considered capable of being in
     * any of the seven possible states (two straights, four
     * corners and empty), except those corresponding to clue
     * squares which are more restricted.
     * 
     * Initially, all edges are unknown, except the ones around the
     * grid border which are known to be disconnected.
     */
    workspace = snewn(W*H, short);
    for (x = 0; x < W*H; x++)
	workspace[x] = 0;
    /* Square states */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    switch (clues[y*w+x]) {
	      case CORNER:
		workspace[(2*y+1)*W+(2*x+1)] = bLU|bLD|bRU|bRD;
		break;
	      case STRAIGHT:
		workspace[(2*y+1)*W+(2*x+1)] = bLR|bUD;
		break;
	      default:
		workspace[(2*y+1)*W+(2*x+1)] = bLR|bUD|bLU|bLD|bRU|bRD|bBLANK;
		break;
	    }
    /* Horizontal edges */
    for (y = 0; y <= h; y++)
	for (x = 0; x < w; x++)
	    workspace[(2*y)*W+(2*x+1)] = (y==0 || y==h ? 2 : 3);
    /* Vertical edges */
    for (y = 0; y < h; y++)
	for (x = 0; x <= w; x++)
	    workspace[(2*y+1)*W+(2*x)] = (x==0 || x==w ? 2 : 3);

    /*
     * We maintain a dsf of connected squares, together with a
     * count of the size of each equivalence class.
     */
    dsf = snewn(w*h, int);
    dsfsize = snewn(w*h, int);

    /*
     * Now repeatedly try to find something we can do.
     */
    while (1) {
	bool done_something = false;

#ifdef SOLVER_DIAGNOSTICS
	for (y = 0; y < H; y++) {
	    for (x = 0; x < W; x++)
		printf("%*x", (x&1) ? 5 : 2, workspace[y*W+x]);
	    printf("\n");
	}
#endif

	/*
	 * Go through the square state words, and discard any
	 * square state which is inconsistent with known facts
	 * about the edges around the square.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		for (b = 0; b < 0xD; b++)
		    if (workspace[(2*y+1)*W+(2*x+1)] & (1<<b)) {
			/*
			 * If any edge of this square is known to
			 * be connected when state b would require
			 * it disconnected, or vice versa, discard
			 * the state.
			 */
			for (d = 1; d <= 8; d += d) {
			    int ex = 2*x+1 + DX(d), ey = 2*y+1 + DY(d);
			    if (workspace[ey*W+ex] ==
				((b & d) ? 2 : 1)) {
				workspace[(2*y+1)*W+(2*x+1)] &= ~(1<<b);
#ifdef SOLVER_DIAGNOSTICS
				printf("edge (%d,%d)-(%d,%d) rules out state"
				       " %d for square (%d,%d)\n",
				       ex/2, ey/2, (ex+1)/2, (ey+1)/2,
				       b, x, y);
#endif
				done_something = true;
				break;
			    }
			}
		    }

		/*
		 * Consistency check: each square must have at
		 * least one state left!
		 */
		if (!workspace[(2*y+1)*W+(2*x+1)]) {
#ifdef SOLVER_DIAGNOSTICS
		    printf("edge check at (%d,%d): inconsistency\n", x, y);
#endif
		    ret = 0;
		    goto cleanup;
		}
	    }

	/*
	 * Now go through the states array again, and nail down any
	 * unknown edge if one of its neighbouring squares makes it
	 * known.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		int edgeor = 0, edgeand = 15;

		for (b = 0; b < 0xD; b++)
		    if (workspace[(2*y+1)*W+(2*x+1)] & (1<<b)) {
			edgeor |= b;
			edgeand &= b;
		    }

		/*
		 * Now any bit clear in edgeor marks a disconnected
		 * edge, and any bit set in edgeand marks a
		 * connected edge.
		 */

		/* First check consistency: neither bit is both! */
		if (edgeand & ~edgeor) {
#ifdef SOLVER_DIAGNOSTICS
		    printf("square check at (%d,%d): inconsistency\n", x, y);
#endif
		    ret = 0;
		    goto cleanup;
		}

		for (d = 1; d <= 8; d += d) {
		    int ex = 2*x+1 + DX(d), ey = 2*y+1 + DY(d);

		    if (!(edgeor & d) && workspace[ey*W+ex] == 3) {
			workspace[ey*W+ex] = 2;
			done_something = true;
#ifdef SOLVER_DIAGNOSTICS
			printf("possible states of square (%d,%d) force edge"
			       " (%d,%d)-(%d,%d) to be disconnected\n",
			       x, y, ex/2, ey/2, (ex+1)/2, (ey+1)/2);
#endif
		    } else if ((edgeand & d) && workspace[ey*W+ex] == 3) {
			workspace[ey*W+ex] = 1;
			done_something = true;
#ifdef SOLVER_DIAGNOSTICS
			printf("possible states of square (%d,%d) force edge"
			       " (%d,%d)-(%d,%d) to be connected\n",
			       x, y, ex/2, ey/2, (ex+1)/2, (ey+1)/2);
#endif
		    }
		}
	    }

	if (done_something)
	    continue;

	/*
	 * Now for longer-range clue-based deductions (using the
	 * rules that a corner clue must connect to two straight
	 * squares, and a straight clue must connect to at least
	 * one corner square).
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++)
		switch (clues[y*w+x]) {
		  case CORNER:
		    for (d = 1; d <= 8; d += d) {
			int ex = 2*x+1 + DX(d), ey = 2*y+1 + DY(d);
			int fx = ex + DX(d), fy = ey + DY(d);
			int type = d | F(d);

			if (workspace[ey*W+ex] == 1) {
			    /*
			     * If a corner clue is connected on any
			     * edge, then we can immediately nail
			     * down the square beyond that edge as
			     * being a straight in the appropriate
			     * direction.
			     */
			    if (workspace[fy*W+fx] != (1<<type)) {
				workspace[fy*W+fx] = (1<<type);
				done_something = true;
#ifdef SOLVER_DIAGNOSTICS
				printf("corner clue at (%d,%d) forces square "
				       "(%d,%d) into state %d\n", x, y,
				       fx/2, fy/2, type);
#endif
				
			    }
			} else if (workspace[ey*W+ex] == 3) {
			    /*
			     * Conversely, if a corner clue is
			     * separated by an unknown edge from a
			     * square which _cannot_ be a straight
			     * in the appropriate direction, we can
			     * mark that edge as disconnected.
			     */
			    if (!(workspace[fy*W+fx] & (1<<type))) {
				workspace[ey*W+ex] = 2;
				done_something = true;
#ifdef SOLVER_DIAGNOSTICS
				printf("corner clue at (%d,%d), plus square "
				       "(%d,%d) not being state %d, "
				       "disconnects edge (%d,%d)-(%d,%d)\n",
				       x, y, fx/2, fy/2, type,
				       ex/2, ey/2, (ex+1)/2, (ey+1)/2);
#endif

			    }
			}
		    }

		    break;
		  case STRAIGHT:
		    /*
		     * If a straight clue is between two squares
		     * neither of which is capable of being a
		     * corner connected to it, then the straight
		     * clue cannot point in that direction.
		     */
		    for (d = 1; d <= 2; d += d) {
			int fx = 2*x+1 + 2*DX(d), fy = 2*y+1 + 2*DY(d);
			int gx = 2*x+1 - 2*DX(d), gy = 2*y+1 - 2*DY(d);
			int type = d | F(d);

			if (!(workspace[(2*y+1)*W+(2*x+1)] & (1<<type)))
			    continue;

			if (!(workspace[fy*W+fx] & ((1<<(F(d)|A(d))) |
						    (1<<(F(d)|C(d))))) &&
			    !(workspace[gy*W+gx] & ((1<<(  d |A(d))) |
						    (1<<(  d |C(d)))))) {
			    workspace[(2*y+1)*W+(2*x+1)] &= ~(1<<type);
			    done_something = true;
#ifdef SOLVER_DIAGNOSTICS
			    printf("straight clue at (%d,%d) cannot corner at "
				   "(%d,%d) or (%d,%d) so is not state %d\n",
				   x, y, fx/2, fy/2, gx/2, gy/2, type);
#endif
			}
						    
		    }

		    /*
		     * If a straight clue with known direction is
		     * connected on one side to a known straight,
		     * then on the other side it must be a corner.
		     */
		    for (d = 1; d <= 8; d += d) {
			int fx = 2*x+1 + 2*DX(d), fy = 2*y+1 + 2*DY(d);
			int gx = 2*x+1 - 2*DX(d), gy = 2*y+1 - 2*DY(d);
			int type = d | F(d);

			if (workspace[(2*y+1)*W+(2*x+1)] != (1<<type))
			    continue;

			if (!(workspace[fy*W+fx] &~ (bLR|bUD)) &&
			    (workspace[gy*W+gx] &~ (bLU|bLD|bRU|bRD))) {
			    workspace[gy*W+gx] &= (bLU|bLD|bRU|bRD);
			    done_something = true;
#ifdef SOLVER_DIAGNOSTICS
			    printf("straight clue at (%d,%d) connecting to "
				   "straight at (%d,%d) makes (%d,%d) a "
				   "corner\n", x, y, fx/2, fy/2, gx/2, gy/2);
#endif
			}
						    
		    }
		    break;
		}

	if (done_something)
	    continue;

	/*
	 * Now detect shortcut loops.
	 */

	{
	    int nonblanks, loopclass;

	    dsf_init(dsf, w*h);
	    for (x = 0; x < w*h; x++)
		dsfsize[x] = 1;

	    /*
	     * First go through the edge entries and update the dsf
	     * of which squares are connected to which others. We
	     * also track the number of squares in each equivalence
	     * class, and count the overall number of
	     * known-non-blank squares.
	     *
	     * In the process of doing this, we must notice if a
	     * loop has already been formed. If it has, we blank
	     * out any square which isn't part of that loop
	     * (failing a consistency check if any such square does
	     * not have BLANK as one of its remaining options) and
	     * exit the deduction loop with success.
	     */
	    nonblanks = 0;
	    loopclass = -1;
	    for (y = 1; y < H-1; y++)
		for (x = 1; x < W-1; x++)
		    if ((y ^ x) & 1) {
			/*
			 * (x,y) are the workspace coordinates of
			 * an edge field. Compute the normal-space
			 * coordinates of the squares it connects.
			 */
			int ax = (x-1)/2, ay = (y-1)/2, ac = ay*w+ax;
			int bx = x/2, by = y/2, bc = by*w+bx;

			/*
			 * If the edge is connected, do the dsf
			 * thing.
			 */
			if (workspace[y*W+x] == 1) {
			    int ae, be;

			    ae = dsf_canonify(dsf, ac);
			    be = dsf_canonify(dsf, bc);

			    if (ae == be) {
				/*
				 * We have a loop!
				 */
				if (loopclass != -1) {
				    /*
				     * In fact, we have two
				     * separate loops, which is
				     * doom.
				     */
#ifdef SOLVER_DIAGNOSTICS
				    printf("two loops found in grid!\n");
#endif
				    ret = 0;
				    goto cleanup;
				}
				loopclass = ae;
			    } else {
				/*
				 * Merge the two equivalence
				 * classes.
				 */
				int size = dsfsize[ae] + dsfsize[be];
				dsf_merge(dsf, ac, bc);
				ae = dsf_canonify(dsf, ac);
				dsfsize[ae] = size;
			    }
			}
		    } else if ((y & x) & 1) {
			/*
			 * (x,y) are the workspace coordinates of a
			 * square field. If the square is
			 * definitely not blank, count it.
			 */
			if (!(workspace[y*W+x] & bBLANK))
			    nonblanks++;
		    }

	    /*
	     * If we discovered an existing loop above, we must now
	     * blank every square not part of it, and exit the main
	     * deduction loop.
	     */
	    if (loopclass != -1) {
#ifdef SOLVER_DIAGNOSTICS
		printf("loop found in grid!\n");
#endif
		for (y = 0; y < h; y++)
		    for (x = 0; x < w; x++)
			if (dsf_canonify(dsf, y*w+x) != loopclass) {
			    if (workspace[(y*2+1)*W+(x*2+1)] & bBLANK) {
				workspace[(y*2+1)*W+(x*2+1)] = bBLANK;
			    } else {
				/*
				 * This square is not part of the
				 * loop, but is known non-blank. We
				 * have goofed.
				 */
#ifdef SOLVER_DIAGNOSTICS
				printf("non-blank square (%d,%d) found outside"
				       " loop!\n", x, y);
#endif
				ret = 0;
				goto cleanup;
			    }
			}
		/*
		 * And we're done.
		 */
		ret = 1;
		break;
	    }

            /* Further deductions are considered 'tricky'. */
            if (difficulty == DIFF_EASY) goto done_deductions;

	    /*
	     * Now go through the workspace again and mark any edge
	     * which would cause a shortcut loop (i.e. would
	     * connect together two squares in the same equivalence
	     * class, and that equivalence class does not contain
	     * _all_ the known-non-blank squares currently in the
	     * grid) as disconnected. Also, mark any _square state_
	     * which would cause a shortcut loop as disconnected.
	     */
	    for (y = 1; y < H-1; y++)
		for (x = 1; x < W-1; x++)
		    if ((y ^ x) & 1) {
			/*
			 * (x,y) are the workspace coordinates of
			 * an edge field. Compute the normal-space
			 * coordinates of the squares it connects.
			 */
			int ax = (x-1)/2, ay = (y-1)/2, ac = ay*w+ax;
			int bx = x/2, by = y/2, bc = by*w+bx;

			/*
			 * If the edge is currently unknown, and
			 * sits between two squares in the same
			 * equivalence class, and the size of that
			 * class is less than nonblanks, then
			 * connecting this edge would be a shortcut
			 * loop and so we must not do so.
			 */
			if (workspace[y*W+x] == 3) {
			    int ae, be;

			    ae = dsf_canonify(dsf, ac);
			    be = dsf_canonify(dsf, bc);

			    if (ae == be) {
				/*
				 * We have a loop. Is it a shortcut?
				 */
				if (dsfsize[ae] < nonblanks) {
				    /*
				     * Yes! Mark this edge disconnected.
				     */
				    workspace[y*W+x] = 2;
				    done_something = true;
#ifdef SOLVER_DIAGNOSTICS
				    printf("edge (%d,%d)-(%d,%d) would create"
					   " a shortcut loop, hence must be"
					   " disconnected\n", x/2, y/2,
					   (x+1)/2, (y+1)/2);
#endif
				}
			    }
			}
		    } else if ((y & x) & 1) {
			/*
			 * (x,y) are the workspace coordinates of a
			 * square field. Go through its possible
			 * (non-blank) states and see if any gives
			 * rise to a shortcut loop.
			 * 
			 * This is slightly fiddly, because we have
			 * to check whether this square is already
			 * part of the same equivalence class as
			 * the things it's joining.
			 */
			int ae = dsf_canonify(dsf, (y/2)*w+(x/2));

			for (b = 2; b < 0xD; b++)
			    if (workspace[y*W+x] & (1<<b)) {
				/*
				 * Find the equivalence classes of
				 * the two squares this one would
				 * connect if it were in this
				 * state.
				 */
				int e = -1;

				for (d = 1; d <= 8; d += d) if (b & d) {
				    int xx = x/2 + DX(d), yy = y/2 + DY(d);
				    int ee = dsf_canonify(dsf, yy*w+xx);

				    if (e == -1)
					ee = e;
				    else if (e != ee)
					e = -2;
				}

				if (e >= 0) {
				    /*
				     * This square state would form
				     * a loop on equivalence class
				     * e. Measure the size of that
				     * loop, and see if it's a
				     * shortcut.
				     */
				    int loopsize = dsfsize[e];
				    if (e != ae)
					loopsize++;/* add the square itself */
				    if (loopsize < nonblanks) {
					/*
					 * It is! Mark this square
					 * state invalid.
					 */
					workspace[y*W+x] &= ~(1<<b);
					done_something = true;
#ifdef SOLVER_DIAGNOSTICS
					printf("square (%d,%d) would create a "
					       "shortcut loop in state %d, "
					       "hence cannot be\n",
					       x/2, y/2, b);
#endif
				    }
				}
			    }
		    }
	}

done_deductions:

	if (done_something)
	    continue;

	/*
	 * If we reach here, there is nothing left we can do.
	 * Return 2 for ambiguous puzzle.
	 */
	ret = 2;
	break;
    }

cleanup:

    /*
     * If ret = 1 then we've successfully achieved a solution. This
     * means that we expect every square to be nailed down to
     * exactly one possibility. If this is the case, or if the caller
     * asked for a partial solution anyway, transcribe those
     * possibilities into the result array.
     */
    if (ret == 1 || partial) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                for (b = 0; b < 0xD; b++)
                    if (workspace[(2*y+1)*W+(2*x+1)] == (1<<b)) {
                        result[y*w+x] = b;
                        break;
                    }
               if (ret == 1) assert(b < 0xD); /* we should have had a break by now */
            }
        }

        /*
         * Ensure we haven't left the _data structure_ inconsistent,
         * regardless of the consistency of the _puzzle_. In
         * particular, we should never have marked one square as
         * linked to its neighbour if the neighbour is not
         * reciprocally linked back to the original square.
         *
         * This can happen if we get part way through solving an
         * impossible puzzle and then give up trying to make further
         * progress. So here we fix it up to avoid confusing the rest
         * of the game.
         */
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                for (d = 1; d <= 8; d += d) {
                    int nx = x + DX(d), ny = y + DY(d);
                    int rlink;
                    if (0 <= nx && nx < w && 0 <= ny && ny < h)
                        rlink = result[ny*w+nx] & F(d);
                    else
                        rlink = 0;     /* off-board squares don't link back */

                    /* If other square doesn't link to us, don't link to it */
                    if (!rlink)
                        result[y*w+x] &= ~d;
                }
            }
        }
    }

    sfree(dsfsize);
    sfree(dsf);
    sfree(workspace);
    assert(ret >= 0);
    return ret;
}

/* ----------------------------------------------------------------------
 * Loop generator.
 */

/*
 * We use the loop generator code from loopy, hard-coding to a square
 * grid of the appropriate size. Knowing the grid layout and the tile
 * size we can shrink that to our small grid and then make our line
 * layout from the face colour info.
 *
 * We provide a bias function to the loop generator which tries to
 * bias in favour of loops with more scope for Pearl black clues. This
 * seems to improve the success rate of the puzzle generator, in that
 * such loops have a better chance of being soluble with all valid
 * clues put in.
 */

struct pearl_loopgen_bias_ctx {
    /*
     * Our bias function counts the number of 'black clue' corners
     * (i.e. corners adjacent to two straights) in both the
     * BLACK/nonBLACK and WHITE/nonWHITE boundaries. In order to do
     * this, we must:
     *
     *  - track the edges that are part of each of those loops
     *  - track the types of vertex in each loop (corner, straight,
     *    none)
     *  - track the current black-clue status of each vertex in each
     *    loop.
     *
     * Each of these chunks of data is updated incrementally from the
     * previous one, to avoid slowdown due to the bias function
     * rescanning the whole grid every time it's called.
     *
     * So we need a lot of separate arrays, plus a tdq for each one,
     * and we must repeat it all twice for the BLACK and WHITE
     * boundaries.
     */
    struct pearl_loopgen_bias_ctx_boundary {
        int colour;                    /* FACE_WHITE or FACE_BLACK */

        bool *edges;                   /* is each edge part of the loop? */
        tdq *edges_todo;

        char *vertextypes;             /* bits 0-3 == outgoing edge bitmap;
                                        * bit 4 set iff corner clue.
                                        * Hence, 0 means non-vertex;
                                        * nonzero but bit 4 zero = straight. */
        int *neighbour[2];          /* indices of neighbour vertices in loop */
        tdq *vertextypes_todo;

        char *blackclues;              /* is each vertex a black clue site? */
        tdq *blackclues_todo;
    } boundaries[2];                   /* boundaries[0]=WHITE, [1]=BLACK */

    char *faces;          /* remember last-seen colour of each face */
    tdq *faces_todo;

    int score;

    grid *g;
};
static int pearl_loopgen_bias(void *vctx, char *board, int face)
{
    struct pearl_loopgen_bias_ctx *ctx = (struct pearl_loopgen_bias_ctx *)vctx;
    grid *g = ctx->g;
    int oldface, newface;
    int i, j, k;

    tdq_add(ctx->faces_todo, face);
    while ((j = tdq_remove(ctx->faces_todo)) >= 0) {
        oldface = ctx->faces[j];
        ctx->faces[j] = newface = board[j];
        for (i = 0; i < 2; i++) {
            struct pearl_loopgen_bias_ctx_boundary *b = &ctx->boundaries[i];
            int c = b->colour;

            /*
             * If the face has changed either from or to colour c, we need
             * to reprocess the edges for this boundary.
             */
            if (oldface == c || newface == c) {
                grid_face *f = &g->faces[face];
                for (k = 0; k < f->order; k++)
                    tdq_add(b->edges_todo, f->edges[k] - g->edges);
            }
        }
    }

    for (i = 0; i < 2; i++) {
        struct pearl_loopgen_bias_ctx_boundary *b = &ctx->boundaries[i];
        int c = b->colour;

        /*
         * Go through the to-do list of edges. For each edge, decide
         * anew whether it's part of this boundary or not. Any edge
         * that changes state has to have both its endpoints put on
         * the vertextypes_todo list.
         */
        while ((j = tdq_remove(b->edges_todo)) >= 0) {
            grid_edge *e = &g->edges[j];
            int fc1 = e->face1 ? board[e->face1 - g->faces] : FACE_BLACK;
            int fc2 = e->face2 ? board[e->face2 - g->faces] : FACE_BLACK;
            bool oldedge = b->edges[j];
            bool newedge = (fc1==c) ^ (fc2==c);
            if (oldedge != newedge) {
                b->edges[j] = newedge;
                tdq_add(b->vertextypes_todo, e->dot1 - g->dots);
                tdq_add(b->vertextypes_todo, e->dot2 - g->dots);
            }
        }

        /*
         * Go through the to-do list of vertices whose types need
         * refreshing. For each one, decide whether it's a corner, a
         * straight, or a vertex not in the loop, and in the former
         * two cases also work out the indices of its neighbour
         * vertices along the loop. Any vertex that changes state must
         * be put back on the to-do list for deciding if it's a black
         * clue site, and so must its two new neighbours _and_ its two
         * old neighbours.
         */
        while ((j = tdq_remove(b->vertextypes_todo)) >= 0) {
            grid_dot *d = &g->dots[j];
            int neighbours[2], type = 0, n = 0;
            
            for (k = 0; k < d->order; k++) {
                grid_edge *e = d->edges[k];
                grid_dot *d2 = (e->dot1 == d ? e->dot2 : e->dot1);
                /* dir == 0,1,2,3 for an edge going L,U,R,D */
                int dir = (d->y == d2->y) + 2*(d->x+d->y > d2->x+d2->y);
                int ei = e - g->edges;
                if (b->edges[ei]) {
                    type |= 1 << dir;
                    neighbours[n] = d2 - g->dots; 
                    n++;
                }
            }

            /*
             * Decide if it's a corner, and set the corner flag if so.
             */
            if (type != 0 && type != 0x5 && type != 0xA)
                type |= 0x10;

            if (type != b->vertextypes[j]) {
                /*
                 * Recompute old neighbours, if any.
                 */
                if (b->vertextypes[j]) {
                    tdq_add(b->blackclues_todo, b->neighbour[0][j]);
                    tdq_add(b->blackclues_todo, b->neighbour[1][j]);
                }
                /*
                 * Recompute this vertex.
                 */
                tdq_add(b->blackclues_todo, j);
                b->vertextypes[j] = type;
                /*
                 * Recompute new neighbours, if any.
                 */
                if (b->vertextypes[j]) {
                    b->neighbour[0][j] = neighbours[0];
                    b->neighbour[1][j] = neighbours[1];
                    tdq_add(b->blackclues_todo, b->neighbour[0][j]);
                    tdq_add(b->blackclues_todo, b->neighbour[1][j]);
                }
            }
        }

        /*
         * Go through the list of vertices which we must check to see
         * if they're black clue sites. Each one is a black clue site
         * iff it is a corner and its loop neighbours are non-corners.
         * Adjust the running total of black clues we've counted.
         */
        while ((j = tdq_remove(b->blackclues_todo)) >= 0) {
            ctx->score -= b->blackclues[j];
            b->blackclues[j] = ((b->vertextypes[j] & 0x10) &&
                                !((b->vertextypes[b->neighbour[0][j]] |
                                   b->vertextypes[b->neighbour[1][j]])
                                  & 0x10));
            ctx->score += b->blackclues[j];
        }
    }

    return ctx->score;
}

static void pearl_loopgen(int w, int h, char *lines, random_state *rs)
{
    grid *g = grid_new(GRID_SQUARE, w-1, h-1, NULL);
    char *board = snewn(g->num_faces, char);
    int i, s = g->tilesize;
    struct pearl_loopgen_bias_ctx biasctx;

    memset(lines, 0, w*h);

    /*
     * Initialise the context for the bias function. Initially we fill
     * all the to-do lists, so that the first call will scan
     * everything; thereafter the lists stay empty so we make
     * incremental changes.
     */
    biasctx.g = g;
    biasctx.faces = snewn(g->num_faces, char);
    biasctx.faces_todo = tdq_new(g->num_faces);
    tdq_fill(biasctx.faces_todo);
    biasctx.score = 0;
    memset(biasctx.faces, FACE_GREY, g->num_faces);
    for (i = 0; i < 2; i++) {
        biasctx.boundaries[i].edges = snewn(g->num_edges, bool);
        memset(biasctx.boundaries[i].edges, 0, g->num_edges * sizeof(bool));
        biasctx.boundaries[i].edges_todo = tdq_new(g->num_edges);
        tdq_fill(biasctx.boundaries[i].edges_todo);
        biasctx.boundaries[i].vertextypes = snewn(g->num_dots, char);
        memset(biasctx.boundaries[i].vertextypes, 0, g->num_dots);
        biasctx.boundaries[i].neighbour[0] = snewn(g->num_dots, int);
        biasctx.boundaries[i].neighbour[1] = snewn(g->num_dots, int);
        biasctx.boundaries[i].vertextypes_todo = tdq_new(g->num_dots);
        tdq_fill(biasctx.boundaries[i].vertextypes_todo);
        biasctx.boundaries[i].blackclues = snewn(g->num_dots, char);
        memset(biasctx.boundaries[i].blackclues, 0, g->num_dots);
        biasctx.boundaries[i].blackclues_todo = tdq_new(g->num_dots);
        tdq_fill(biasctx.boundaries[i].blackclues_todo);
    }
    biasctx.boundaries[0].colour = FACE_WHITE;
    biasctx.boundaries[1].colour = FACE_BLACK;
    generate_loop(g, board, rs, pearl_loopgen_bias, &biasctx);
    sfree(biasctx.faces);
    tdq_free(biasctx.faces_todo);
    for (i = 0; i < 2; i++) {
        sfree(biasctx.boundaries[i].edges);
        tdq_free(biasctx.boundaries[i].edges_todo);
        sfree(biasctx.boundaries[i].vertextypes);
        sfree(biasctx.boundaries[i].neighbour[0]);
        sfree(biasctx.boundaries[i].neighbour[1]);
        tdq_free(biasctx.boundaries[i].vertextypes_todo);
        sfree(biasctx.boundaries[i].blackclues);
        tdq_free(biasctx.boundaries[i].blackclues_todo);
    }

    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        enum face_colour c1 = FACE_COLOUR(e->face1);
        enum face_colour c2 = FACE_COLOUR(e->face2);
        assert(c1 != FACE_GREY);
        assert(c2 != FACE_GREY);
        if (c1 != c2) {
            /* This grid edge is on the loop: lay line along it */
            int x1 = e->dot1->x/s, y1 = e->dot1->y/s;
            int x2 = e->dot2->x/s, y2 = e->dot2->y/s;

            /* (x1,y1) and (x2,y2) are now in our grid coords (0-w,0-h). */
            if (x1 == x2) {
                if (y1 > y2) SWAP(y1,y2);

                assert(y1+1 == y2);
                lines[y1*w+x1] |= D;
                lines[y2*w+x1] |= U;
            } else if (y1 == y2) {
                if (x1 > x2) SWAP(x1,x2);

                assert(x1+1 == x2);
                lines[y1*w+x1] |= R;
                lines[y1*w+x2] |= L;
            } else
                assert(!"grid with diagonal coords?!");
        }
    }

    grid_free(g);
    sfree(board);

#if defined LOOPGEN_DIAGNOSTICS && !defined GENERATION_DIAGNOSTICS
    printf("as returned:\n");
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    int type = lines[y*w+x];
	    char s[5], *p = s;
	    if (type & L) *p++ = 'L';
	    if (type & R) *p++ = 'R';
	    if (type & U) *p++ = 'U';
	    if (type & D) *p++ = 'D';
	    *p = '\0';
	    printf("%3s", s);
	}
	printf("\n");
    }
    printf("\n");
#endif
}

static int new_clues(const game_params *params, random_state *rs,
                     char *clues, char *grid)
{
    int w = params->w, h = params->h, diff = params->difficulty;
    int ngen = 0, x, y, d, ret, i;


    /*
     * Difficulty exception: 5x5 Tricky is not generable (the
     * generator will spin forever trying) and so we fudge it to Easy.
     */
    if (w == 5 && h == 5 && diff > DIFF_EASY)
        diff = DIFF_EASY;

    while (1) {
        ngen++;
	pearl_loopgen(w, h, grid, rs);

#ifdef GENERATION_DIAGNOSTICS
	printf("grid array:\n");
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int type = grid[y*w+x];
		char s[5], *p = s;
		if (type & L) *p++ = 'L';
		if (type & R) *p++ = 'R';
		if (type & U) *p++ = 'U';
		if (type & D) *p++ = 'D';
		*p = '\0';
		printf("%2s ", s);
	    }
	    printf("\n");
	}
	printf("\n");
#endif

	/*
	 * Set up the maximal clue array.
	 */
	for (y = 0; y < h; y++)
	    for (x = 0; x < w; x++) {
		int type = grid[y*w+x];

		clues[y*w+x] = NOCLUE;

		if ((bLR|bUD) & (1 << type)) {
		    /*
		     * This is a straight; see if it's a viable
		     * candidate for a straight clue. It qualifies if
		     * at least one of the squares it connects to is a
		     * corner.
		     */
		    for (d = 1; d <= 8; d += d) if (type & d) {
			int xx = x + DX(d), yy = y + DY(d);
			assert(xx >= 0 && xx < w && yy >= 0 && yy < h);
			if ((bLU|bLD|bRU|bRD) & (1 << grid[yy*w+xx]))
			    break;
		    }
		    if (d <= 8)	       /* we found one */
			clues[y*w+x] = STRAIGHT;
		} else if ((bLU|bLD|bRU|bRD) & (1 << type)) {
		    /*
		     * This is a corner; see if it's a viable candidate
		     * for a corner clue. It qualifies if all the
		     * squares it connects to are straights.
		     */
		    for (d = 1; d <= 8; d += d) if (type & d) {
			int xx = x + DX(d), yy = y + DY(d);
			assert(xx >= 0 && xx < w && yy >= 0 && yy < h);
			if (!((bLR|bUD) & (1 << grid[yy*w+xx])))
			    break;
		    }
		    if (d > 8)	       /* we didn't find a counterexample */
			clues[y*w+x] = CORNER;
		}
	    }

#ifdef GENERATION_DIAGNOSTICS
	printf("clue array:\n");
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		printf("%c", " *O"[(unsigned char)clues[y*w+x]]);
	    }
	    printf("\n");
	}
	printf("\n");
#endif

        if (!params->nosolve) {
            int *cluespace, *straights, *corners;
            int nstraights, ncorners, nstraightpos, ncornerpos;

            /*
             * See if we can solve the puzzle just like this.
             */
            ret = pearl_solve(w, h, clues, grid, diff, false);
            assert(ret > 0);	       /* shouldn't be inconsistent! */
            if (ret != 1)
                continue;		       /* go round and try again */

            /*
             * Check this puzzle isn't too easy.
             */
            if (diff > DIFF_EASY) {
                ret = pearl_solve(w, h, clues, grid, diff-1, false);
                assert(ret > 0);
                if (ret == 1)
                    continue; /* too easy: try again */
            }

            /*
             * Now shuffle the grid points and gradually remove the
             * clues to find a minimal set which still leaves the
             * puzzle soluble.
             *
             * We preferentially attempt to remove whichever type of
             * clue is currently most numerous, to combat a general
             * tendency of plain random generation to bias in favour
             * of many white clues and few black.
             *
             * 'nstraights' and 'ncorners' count the number of clues
             * of each type currently remaining in the grid;
             * 'nstraightpos' and 'ncornerpos' count the clues of each
             * type we have left to try to remove. (Clues which we
             * have tried and failed to remove are counted by the
             * former but not the latter.)
             */
            cluespace = snewn(w*h, int);
            straights = cluespace;
            nstraightpos = 0;
            for (i = 0; i < w*h; i++)
                if (clues[i] == STRAIGHT)
                    straights[nstraightpos++] = i;
            corners = straights + nstraightpos;
            ncornerpos = 0;
            for (i = 0; i < w*h; i++)
                if (clues[i] == STRAIGHT)
                    corners[ncornerpos++] = i;
            nstraights = nstraightpos;
            ncorners = ncornerpos;

            shuffle(straights, nstraightpos, sizeof(*straights), rs);
            shuffle(corners, ncornerpos, sizeof(*corners), rs);
            while (nstraightpos > 0 || ncornerpos > 0) {
                int cluepos;
                int clue;

                /*
                 * Decide which clue to try to remove next. If both
                 * types are available, we choose whichever kind is
                 * currently overrepresented; otherwise we take
                 * whatever we can get.
                 */
                if (nstraightpos > 0 && ncornerpos > 0) {
                    if (nstraights >= ncorners)
                        cluepos = straights[--nstraightpos];
                    else
                        cluepos = straights[--ncornerpos];
                } else {
                    if (nstraightpos > 0)
                        cluepos = straights[--nstraightpos];
                    else
                        cluepos = straights[--ncornerpos];
                }

                y = cluepos / w;
                x = cluepos % w;

                clue = clues[y*w+x];
                clues[y*w+x] = 0;	       /* try removing this clue */

                ret = pearl_solve(w, h, clues, grid, diff, false);
                assert(ret > 0);
                if (ret != 1)
                    clues[y*w+x] = clue;   /* oops, put it back again */
            }
            sfree(cluespace);
        }

#ifdef FINISHED_PUZZLE
	printf("clue array:\n");
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		printf("%c", " *O"[(unsigned char)clues[y*w+x]]);
	    }
	    printf("\n");
	}
	printf("\n");
#endif

	break;			       /* got it */
    }

    debug(("%d %dx%d loops before finished puzzle.\n", ngen, w, h));

    return ngen;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    char *grid, *clues;
    char *desc;
    int w = params->w, h = params->h, i, j;

    grid = snewn(w*h, char);
    clues = snewn(w*h, char);

    new_clues(params, rs, clues, grid);

    desc = snewn(w * h + 1, char);
    for (i = j = 0; i < w*h; i++) {
        if (clues[i] == NOCLUE && j > 0 &&
            desc[j-1] >= 'a' && desc[j-1] < 'z')
            desc[j-1]++;
        else if (clues[i] == NOCLUE)
            desc[j++] = 'a';
        else if (clues[i] == CORNER)
            desc[j++] = 'B';
        else if (clues[i] == STRAIGHT)
            desc[j++] = 'W';
    }
    desc[j] = '\0';

    *aux = snewn(w*h+1, char);
    for (i = 0; i < w*h; i++)
        (*aux)[i] = (grid[i] < 10) ? (grid[i] + '0') : (grid[i] + 'A' - 10);
    (*aux)[w*h] = '\0';

    sfree(grid);
    sfree(clues);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int i, sizesofar;
    const int totalsize = params->w * params->h;

    sizesofar = 0;
    for (i = 0; desc[i]; i++) {
        if (desc[i] >= 'a' && desc[i] <= 'z')
            sizesofar += desc[i] - 'a' + 1;
        else if (desc[i] == 'B' || desc[i] == 'W')
            sizesofar++;
        else
            return "unrecognised character in string";
    }

    if (sizesofar > totalsize)
        return "string too long";
    else if (sizesofar < totalsize)
        return "string too short";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int i, j, sz = params->w*params->h;

    state->completed = false;
    state->used_solve = false;
    state->shared = snew(struct shared_state);

    state->shared->w = params->w;
    state->shared->h = params->h;
    state->shared->sz = sz;
    state->shared->refcnt = 1;
    state->shared->clues = snewn(sz, char);
    for (i = j = 0; desc[i]; i++) {
        assert(j < sz);
        if (desc[i] >= 'a' && desc[i] <= 'z') {
            int n = desc[i] - 'a' + 1;
            assert(j + n <= sz);
            while (n-- > 0)
                state->shared->clues[j++] = NOCLUE;
        } else if (desc[i] == 'B') {
            state->shared->clues[j++] = CORNER;
        } else if (desc[i] == 'W') {
            state->shared->clues[j++] = STRAIGHT;
        }
    }

    state->lines = snewn(sz, char);
    state->errors = snewn(sz, char);
    state->marks = snewn(sz, char);
    for (i = 0; i < sz; i++)
        state->lines[i] = state->errors[i] = state->marks[i] = BLANK;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int sz = state->shared->sz, i;

    ret->shared = state->shared;
    ret->completed = state->completed;
    ret->used_solve = state->used_solve;
    ++ret->shared->refcnt;

    ret->lines = snewn(sz, char);
    ret->errors = snewn(sz, char);
    ret->marks = snewn(sz, char);
    for (i = 0; i < sz; i++) {
        ret->lines[i] = state->lines[i];
        ret->errors[i] = state->errors[i];
        ret->marks[i] = state->marks[i];
    }

    return ret;
}

static void free_game(game_state *state)
{
    assert(state);
    if (--state->shared->refcnt == 0) {
        sfree(state->shared->clues);
        sfree(state->shared);
    }
    sfree(state->lines);
    sfree(state->errors);
    sfree(state->marks);
    sfree(state);
}

static char nbits[16] = { 0, 1, 1, 2,
                          1, 2, 2, 3,
                          1, 2, 2, 3,
                          2, 3, 3, 4 };
#define NBITS(l) ( ((l) < 0 || (l) > 15) ? 4 : nbits[l] )

#define ERROR_CLUE 16

/* Returns false if the state is invalid. */
static bool dsf_update_completion(game_state *state, int ax, int ay, char dir,
                                 int *dsf)
{
    int w = state->shared->w /*, h = state->shared->h */;
    int ac = ay*w+ax, bx, by, bc;

    if (!(state->lines[ac] & dir)) return true; /* no link */
    bx = ax + DX(dir); by = ay + DY(dir);

    if (!INGRID(state, bx, by))
        return false; /* should not have a link off grid */

    bc = by*w+bx;
    if (!(state->lines[bc] & F(dir)))
        return false; /* should have reciprocal link */
    if (!(state->lines[bc] & F(dir))) return true;

    dsf_merge(dsf, ac, bc);
    return true;
}

/* Returns false if the state is invalid. */
static bool check_completion(game_state *state, bool mark)
{
    int w = state->shared->w, h = state->shared->h, x, y, i, d;
    bool had_error = false;
    int *dsf, *component_state;
    int nsilly, nloop, npath, largest_comp, largest_size, total_pathsize;
    enum { COMP_NONE, COMP_LOOP, COMP_PATH, COMP_SILLY, COMP_EMPTY };

    if (mark) {
        for (i = 0; i < w*h; i++) {
            state->errors[i] = 0;
        }
    }

#define ERROR(x,y,e) do { had_error = true; if (mark) state->errors[(y)*w+(x)] |= (e); } while(0)

    /*
     * Analyse the solution into loops, paths and stranger things.
     * Basic strategy here is all the same as in Loopy - see the big
     * comment in loopy.c's check_completion() - and for exactly the
     * same reasons, since Loopy and Pearl have basically the same
     * form of expected solution.
     */
    dsf = snew_dsf(w*h);

    /* Build the dsf. */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            if (!dsf_update_completion(state, x, y, R, dsf) ||
                !dsf_update_completion(state, x, y, D, dsf)) {
                sfree(dsf);
                return false;
            }
        }
    }

    /* Initialise a state variable for each connected component. */
    component_state = snewn(w*h, int);
    for (i = 0; i < w*h; i++) {
        if (dsf_canonify(dsf, i) == i)
            component_state[i] = COMP_LOOP;
        else
            component_state[i] = COMP_NONE;
    }

    /*
     * Classify components, and mark errors where a square has more
     * than two line segments.
     */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            int type = state->lines[y*w+x];
            int degree = NBITS(type);
            int comp = dsf_canonify(dsf, y*w+x);
            if (degree > 2) {
                ERROR(x,y,type);
                component_state[comp] = COMP_SILLY;
            } else if (degree == 0) {
                component_state[comp] = COMP_EMPTY;
            } else if (degree == 1) {
                if (component_state[comp] != COMP_SILLY)
                    component_state[comp] = COMP_PATH;
            }
        }
    }

    /* Count the components, and find the largest sensible one. */
    nsilly = nloop = npath = 0;
    total_pathsize = 0;
    largest_comp = largest_size = -1;
    for (i = 0; i < w*h; i++) {
        if (component_state[i] == COMP_SILLY) {
            nsilly++;
        } else if (component_state[i] == COMP_PATH) {
            total_pathsize += dsf_size(dsf, i);
            npath = 1;
        } else if (component_state[i] == COMP_LOOP) {
            int this_size;

            nloop++;

            if ((this_size = dsf_size(dsf, i)) > largest_size) {
                largest_comp = i;
                largest_size = this_size;
            }
        }
    }
    if (largest_size < total_pathsize) {
        largest_comp = -1;             /* means the paths */
        largest_size = total_pathsize;
    }

    if (nloop > 0 && nloop + npath > 1) {
        /*
         * If there are at least two sensible components including at
         * least one loop, highlight every sensible component that is
         * not the largest one.
         */
        for (i = 0; i < w*h; i++) {
            int comp = dsf_canonify(dsf, i);
            if ((component_state[comp] == COMP_PATH &&
                 -1 != largest_comp) ||
                (component_state[comp] == COMP_LOOP &&
                 comp != largest_comp))
                ERROR(i%w, i/w, state->lines[i]);
        }
    }

    /* Now we've finished with the dsf and component states. The only
     * thing we'll need to remember later on is whether all edges were
     * part of a single loop, for which our counter variables
     * nsilly,nloop,npath are enough. */
    sfree(component_state);
    sfree(dsf);

    /*
     * Check that no clues are contradicted. This code is similar to
     * the code that sets up the maximal clue array for any given
     * loop.
     */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            int type = state->lines[y*w+x];
            if (state->shared->clues[y*w+x] == CORNER) {
                /* Supposed to be a corner: will find a contradiction if
                 * it actually contains a straight line, or if it touches any
                 * corners. */
                if ((bLR|bUD) & (1 << type)) {
                    ERROR(x,y,ERROR_CLUE); /* actually straight */
                }
                for (d = 1; d <= 8; d += d) if (type & d) {
                    int xx = x + DX(d), yy = y + DY(d);
                    if (!INGRID(state, xx, yy)) {
                        ERROR(x,y,d); /* leads off grid */
                    } else {
                        if ((bLU|bLD|bRU|bRD) & (1 << state->lines[yy*w+xx])) {
                            ERROR(x,y,ERROR_CLUE); /* touches corner */
                        }
                    }
                }
            } else if (state->shared->clues[y*w+x] == STRAIGHT) {
                /* Supposed to be straight: will find a contradiction if
                 * it actually contains a corner, or if it only touches
                 * straight lines. */
                if ((bLU|bLD|bRU|bRD) & (1 << type)) {
                    ERROR(x,y,ERROR_CLUE); /* actually a corner */
                }
                i = 0;
                for (d = 1; d <= 8; d += d) if (type & d) {
                    int xx = x + DX(d), yy = y + DY(d);
                    if (!INGRID(state, xx, yy)) {
                        ERROR(x,y,d); /* leads off grid */
                    } else {
                        if ((bLR|bUD) & (1 << state->lines[yy*w+xx]))
                            i++; /* a straight */
                    }
                }
                if (i >= 2 && NBITS(type) >= 2) {
                    ERROR(x,y,ERROR_CLUE); /* everything touched is straight */
                }
            }
        }
    }

    if (nloop == 1 && nsilly == 0 && npath == 0) {
        /*
         * If there's exactly one loop (so that the puzzle is at least
         * potentially complete), we need to ensure it hasn't left any
         * clue out completely.
         */
        for (x = 0; x < w; x++) {
            for (y = 0; y < h; y++) {
                if (state->lines[y*w+x] == BLANK) {
                    if (state->shared->clues[y*w+x] != NOCLUE) {
                        /* the loop doesn't include this clue square! */
                        ERROR(x, y, ERROR_CLUE);
                    }
                }
            }
        }

        /*
         * But if not, then we're done!
         */
        if (!had_error)
            state->completed = true;
    }
    return true;
}

/* completion check:
 *
 * - no clues must be contradicted (highlight clue itself in error if so)
 * - if there is a closed loop it must include every line segment laid
 *    - if there's a smaller closed loop then highlight whole loop as error
 * - no square must have more than 2 lines radiating from centre point
 *   (highlight all lines in that square as error if so)
 */

static char *solve_for_diff(game_state *state, char *old_lines, char *new_lines)
{
    int w = state->shared->w, h = state->shared->h, i;
    char *move = snewn(w*h*40, char), *p = move;

    *p++ = 'S';
    for (i = 0; i < w*h; i++) {
        if (old_lines[i] != new_lines[i]) {
            p += sprintf(p, ";R%d,%d,%d", new_lines[i], i%w, i/w);
        }
    }
    *p++ = '\0';
    move = sresize(move, p - move, char);

    return move;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *solved = dup_game(state);
    int i, ret, sz = state->shared->sz;
    char *move;

    if (aux) {
        for (i = 0; i < sz; i++) {
            if (aux[i] >= '0' && aux[i] <= '9')
                solved->lines[i] = aux[i] - '0';
            else if (aux[i] >= 'A' && aux[i] <= 'F')
                solved->lines[i] = aux[i] - 'A' + 10;
            else {
                *error = "invalid char in aux";
                move = NULL;
                goto done;
            }
        }
        ret = 1;
    } else {
        /* Try to solve with present (half-solved) state first: if there's no
         * solution from there go back to original state. */
        ret = pearl_solve(currstate->shared->w, currstate->shared->h,
                          currstate->shared->clues, solved->lines,
                          DIFFCOUNT, false);
        if (ret < 1)
            ret = pearl_solve(state->shared->w, state->shared->h,
                              state->shared->clues, solved->lines,
                              DIFFCOUNT, false);

    }

    if (ret < 1) {
        *error = "Unable to find solution";
        move = NULL;
    } else {
        move = solve_for_diff(solved, currstate->lines, solved->lines);
    }

done:
    free_game(solved);
    return move;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->shared->w, h = state->shared->h, cw = 4, ch = 2;
    int gw = cw*(w-1) + 2, gh = ch*(h-1) + 1, len = gw * gh, r, c, j;
    char *board = snewn(len + 1, char);

    assert(board);
    memset(board, ' ', len);

    for (r = 0; r < h; ++r) {
	for (c = 0; c < w; ++c) {
	    int i = r*w + c, cell = r*ch*gw + c*cw;
	    board[cell] = "+BW"[(unsigned char)state->shared->clues[i]];
	    if (c < w - 1 && (state->lines[i] & R || state->lines[i+1] & L))
		memset(board + cell + 1, '-', cw - 1);
	    if (r < h - 1 && (state->lines[i] & D || state->lines[i+w] & U))
		for (j = 1; j < ch; ++j) board[cell + j*gw] = '|';
	    if (c < w - 1 && (state->marks[i] & R || state->marks[i+1] & L))
		board[cell + cw/2] = 'x';
	    if (r < h - 1 && (state->marks[i] & D || state->marks[i+w] & U))
		board[cell + (ch/2 * gw)] = 'x';
	}

	for (j = 0; j < (r == h - 1 ? 1 : ch); ++j)
	    board[r*ch*gw + (gw - 1) + j*gw] = '\n';
    }

    board[len] = '\0';
    return board;
}

struct game_ui {
    int *dragcoords;       /* list of (y*w+x) coords in drag so far */
    int ndragcoords;       /* number of entries in dragcoords.
                            * 0 = click but no drag yet. -1 = no drag at all */
    int clickx, clicky;    /* pixel position of initial click */

    int curx, cury;        /* grid position of keyboard cursor */
    bool cursor_active;    /* true iff cursor is shown */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    int sz = state->shared->sz;

    ui->ndragcoords = -1;
    ui->dragcoords = snewn(sz, int);
    ui->cursor_active = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->curx = ui->cury = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui->dragcoords);
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
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cursor_active) {
        if (button == CURSOR_SELECT) {
            if (ui->ndragcoords == -1) return "Start";
            return "Stop";
        }
        if (button == CURSOR_SELECT2 && ui->ndragcoords >= 0)
            return "Cancel";
    }
    return "";
}

#define PREFERRED_TILE_SIZE 31
#define HALFSZ (ds->halfsz)
#define TILE_SIZE (ds->halfsz*2 + 1)

#define BORDER ((get_gui_style() == GUI_LOOPY) ? (TILE_SIZE/8) : (TILE_SIZE/2))

#define BORDER_WIDTH (max(TILE_SIZE / 32, 1))

#define COORD(x) ( (x) * TILE_SIZE + BORDER )
#define CENTERED_COORD(x) ( COORD(x) + TILE_SIZE/2 )
#define FROMCOORD(x) ( ((x) < BORDER) ? -1 : ( ((x) - BORDER) / TILE_SIZE) )

#define DS_ESHIFT 4     /* R/U/L/D shift, for error flags */
#define DS_DSHIFT 8     /* R/U/L/D shift, for drag-in-progress flags */
#define DS_MSHIFT 12    /* shift for no-line mark */

#define DS_ERROR_CLUE (1 << 20)
#define DS_FLASH (1 << 21)
#define DS_CURSOR (1 << 22)

enum { GUI_MASYU, GUI_LOOPY };

static int get_gui_style(void)
{
    static int gui_style = -1;

    if (gui_style == -1) {
        if (getenv_bool("PEARL_GUI_LOOPY", false))
            gui_style = GUI_LOOPY;
        else
            gui_style = GUI_MASYU;
    }
    return gui_style;
}

struct game_drawstate {
    int halfsz;
    bool started;

    int w, h, sz;
    unsigned int *lflags;       /* size w*h */

    char *draglines;            /* size w*h; lines flipped by current drag */
};

/*
 * Routine shared between multiple callers to work out the intended
 * effect of a drag path on the grid.
 *
 * Call it in a loop, like this:
 *
 *     bool clearing = true;
 *     for (i = 0; i < ui->ndragcoords - 1; i++) {
 *         int sx, sy, dx, dy, dir, oldstate, newstate;
 *         interpret_ui_drag(state, ui, &clearing, i, &sx, &sy, &dx, &dy,
 *                           &dir, &oldstate, &newstate);
 *
 *         [do whatever is needed to handle the fact that the drag
 *         wants the edge from sx,sy to dx,dy (heading in direction
 *         'dir' at the sx,sy end) to be changed from state oldstate
 *         to state newstate, each of which equals either 0 or dir]
 *     }
 */
static void interpret_ui_drag(const game_state *state, const game_ui *ui,
                              bool *clearing, int i, int *sx, int *sy,
                              int *dx, int *dy, int *dir,
                              int *oldstate, int *newstate)
{
    int w = state->shared->w;
    int sp = ui->dragcoords[i], dp = ui->dragcoords[i+1];
    *sy = sp/w;
    *sx = sp%w;
    *dy = dp/w;
    *dx = dp%w;
    *dir = (*dy>*sy ? D : *dy<*sy ? U : *dx>*sx ? R : L);
    *oldstate = state->lines[sp] & *dir;
    if (*oldstate) {
        /*
         * The edge we've dragged over was previously
         * present. Set it to absent, unless we've already
         * stopped doing that.
         */
        *newstate = *clearing ? 0 : *dir;
    } else {
        /*
         * The edge we've dragged over was previously
         * absent. Set it to present, and cancel the
         * 'clearing' flag so that all subsequent edges in
         * the drag are set rather than cleared.
         */
        *newstate = *dir;
        *clearing = false;
    }
}

static void update_ui_drag(const game_state *state, game_ui *ui,
                           int gx, int gy)
{
    int /* sz = state->shared->sz, */ w = state->shared->w;
    int i, ox, oy, pos;
    int lastpos;

    if (!INGRID(state, gx, gy))
        return;                        /* square is outside grid */

    if (ui->ndragcoords < 0)
        return;                        /* drag not in progress anyway */

    pos = gy * w + gx;

    lastpos = ui->dragcoords[ui->ndragcoords > 0 ? ui->ndragcoords-1 : 0];
    if (pos == lastpos)
        return;             /* same square as last visited one */

    /* Drag confirmed, if it wasn't already. */
    if (ui->ndragcoords == 0)
        ui->ndragcoords = 1;

    /*
     * Dragging the mouse into a square that's already been visited by
     * the drag path so far has the effect of truncating the path back
     * to that square, so a player can back out part of an uncommitted
     * drag without having to let go of the mouse.
     *
     * An exception is that you're allowed to drag round in a loop
     * back to the very start of the drag, provided that doesn't
     * create a vertex of the wrong degree. This allows a player who's
     * after an extra challenge to draw the entire loop in a single
     * drag, without it cancelling itself just before release.
     */
    for (i = 1; i < ui->ndragcoords; i++)
        if (pos == ui->dragcoords[i]) {
            ui->ndragcoords = i+1;
            return;
        }

    if (pos == ui->dragcoords[0]) {
        /* More complex check for a loop-shaped drag, which has to go
         * through interpret_ui_drag to decide on the final degree of
         * the start/end vertex. */
        ui->dragcoords[ui->ndragcoords] = pos;
        bool clearing = true;
        int lines = state->lines[pos] & (L|R|U|D);
        for (i = 0; i < ui->ndragcoords; i++) {
            int sx, sy, dx, dy, dir, oldstate, newstate;
            interpret_ui_drag(state, ui, &clearing, i, &sx, &sy, &dx, &dy,
                              &dir, &oldstate, &newstate);
            if (sx == gx && sy == gy)
                lines ^= (oldstate ^ newstate);
            if (dx == gx && dy == gy)
                lines ^= (F(oldstate) ^ F(newstate));
        }
        if (NBITS(lines) > 2) {
            /* Bad vertex degree: fall back to the backtracking behaviour. */
            ui->ndragcoords = 1;
            return;
        }
    }

    /*
     * Otherwise, dragging the mouse into a square that's a rook-move
     * away from the last one on the path extends the path.
     */
    oy = ui->dragcoords[ui->ndragcoords-1] / w;
    ox = ui->dragcoords[ui->ndragcoords-1] % w;
    if (ox == gx || oy == gy) {
        int dx = (gx < ox ? -1 : gx > ox ? +1 : 0);
        int dy = (gy < oy ? -1 : gy > oy ? +1 : 0);
        int dir = (dy>0 ? D : dy<0 ? U : dx>0 ? R : L);
        while (ox != gx || oy != gy) {
            /*
             * If the drag attempts to cross a 'no line here' mark,
             * stop there. We physically don't allow the user to drag
             * over those marks.
             */
            if (state->marks[oy*w+ox] & dir)
                break;
            ox += dx;
            oy += dy;
            ui->dragcoords[ui->ndragcoords++] = oy * w + ox;
        }
    }

    /*
     * Failing that, we do nothing at all: if the user has dragged
     * diagonally across the board, they'll just have to return the
     * mouse to the last known position and do whatever they meant to
     * do again, more slowly and clearly.
     */
}

static char *mark_in_direction(const game_state *state, int x, int y, int dir,
			       bool primary, char *buf)
{
    int w = state->shared->w /*, h = state->shared->h, sz = state->shared->sz */;
    int x2 = x + DX(dir);
    int y2 = y + DY(dir);
    int dir2 = F(dir);

    char ch = primary ? 'F' : 'M', *other;

    if (!INGRID(state, x, y) || !INGRID(state, x2, y2)) return UI_UPDATE;

    /* disallow laying a mark over a line, or vice versa. */
    other = primary ? state->marks : state->lines;
    if (other[y*w+x] & dir || other[y2*w+x2] & dir2) return UI_UPDATE;
    
    sprintf(buf, "%c%d,%d,%d;%c%d,%d,%d", ch, dir, x, y, ch, dir2, x2, y2);
    return dupstr(buf);
}

#define KEY_DIRECTION(btn) (\
    (btn) == CURSOR_DOWN ? D : (btn) == CURSOR_UP ? U :\
    (btn) == CURSOR_LEFT ? L : R)

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->shared->w, h = state->shared->h /*, sz = state->shared->sz */;
    int gx = FROMCOORD(x), gy = FROMCOORD(y), i;
    bool release = false;
    char tmpbuf[80];

    bool shift = button & MOD_SHFT, control = button & MOD_CTRL;
    button &= ~MOD_MASK;

    if (IS_MOUSE_DOWN(button)) {
	ui->cursor_active = false;

        if (!INGRID(state, gx, gy)) {
            ui->ndragcoords = -1;
            return NULL;
        }

        ui->clickx = x; ui->clicky = y;
        ui->dragcoords[0] = gy * w + gx;
        ui->ndragcoords = 0;           /* will be 1 once drag is confirmed */

        return UI_UPDATE;
    }

    if (button == LEFT_DRAG && ui->ndragcoords >= 0) {
        update_ui_drag(state, ui, gx, gy);
        return UI_UPDATE;
    }

    if (IS_MOUSE_RELEASE(button)) release = true;

    if (IS_CURSOR_MOVE(button)) {
	if (!ui->cursor_active) {
	    ui->cursor_active = true;
	} else if (control || shift) {
	    char *move;
	    if (ui->ndragcoords > 0) return NULL;
	    ui->ndragcoords = -1;
	    move = mark_in_direction(state, ui->curx, ui->cury,
				     KEY_DIRECTION(button), control, tmpbuf);
	    if (control && !shift && *move)
		move_cursor(button, &ui->curx, &ui->cury, w, h, false);
	    return move;
	} else {
	    move_cursor(button, &ui->curx, &ui->cury, w, h, false);
	    if (ui->ndragcoords >= 0)
		update_ui_drag(state, ui, ui->curx, ui->cury);
	}
	return UI_UPDATE;
    }

    if (IS_CURSOR_SELECT(button)) {
	if (!ui->cursor_active) {
	    ui->cursor_active = true;
	    return UI_UPDATE;
	} else if (button == CURSOR_SELECT) {
	    if (ui->ndragcoords == -1) {
		ui->ndragcoords = 0;
		ui->dragcoords[0] = ui->cury * w + ui->curx;
		ui->clickx = CENTERED_COORD(ui->curx);
		ui->clicky = CENTERED_COORD(ui->cury);
		return UI_UPDATE;
	    } else release = true;
	} else if (button == CURSOR_SELECT2 && ui->ndragcoords >= 0) {
	    ui->ndragcoords = -1;
	    return UI_UPDATE;
	}
    }

    if ((button == 27 || button == '\b') && ui->ndragcoords >= 0) {
        ui->ndragcoords = -1;
        return UI_UPDATE;
    }

    if (release) {
        if (ui->ndragcoords > 0) {
            /* End of a drag: process the cached line data. */
            int buflen = 0, bufsize = 256, tmplen;
            char *buf = NULL;
            const char *sep = "";
            bool clearing = true;

            for (i = 0; i < ui->ndragcoords - 1; i++) {
                int sx, sy, dx, dy, dir, oldstate, newstate;
                interpret_ui_drag(state, ui, &clearing, i, &sx, &sy, &dx, &dy,
                                  &dir, &oldstate, &newstate);

                if (oldstate != newstate) {
                    if (!buf) buf = snewn(bufsize, char);
                    tmplen = sprintf(tmpbuf, "%sF%d,%d,%d;F%d,%d,%d", sep,
                                     dir, sx, sy, F(dir), dx, dy);
                    if (buflen + tmplen >= bufsize) {
                        bufsize = (buflen + tmplen) * 5 / 4 + 256;
                        buf = sresize(buf, bufsize, char);
                    }
                    strcpy(buf + buflen, tmpbuf);
                    buflen += tmplen;
                    sep = ";";
                }
            }

            ui->ndragcoords = -1;

            return buf ? buf : UI_UPDATE;
        } else if (ui->ndragcoords == 0) {
            /* Click (or tiny drag). Work out which edge we were
             * closest to. */
            int cx, cy;

            ui->ndragcoords = -1;

            /*
             * We process clicks based on the mouse-down location,
             * because that's more natural for a user to carefully
             * control than the mouse-up.
             */
            x = ui->clickx;
            y = ui->clicky;

            gx = FROMCOORD(x);
            gy = FROMCOORD(y);
            cx = CENTERED_COORD(gx);
            cy = CENTERED_COORD(gy);

            if (!INGRID(state, gx, gy)) return UI_UPDATE;

            if (max(abs(x-cx),abs(y-cy)) < TILE_SIZE/4) {
                /* TODO closer to centre of grid: process as a cell click not an edge click. */

                return UI_UPDATE;
            } else {
		int direction;
                if (abs(x-cx) < abs(y-cy)) {
                    /* Closest to top/bottom edge. */
                    direction = (y < cy) ? U : D;
                } else {
                    /* Closest to left/right edge. */
                    direction = (x < cx) ? L : R;
                }
		return mark_in_direction(state, gx, gy, direction,
					 (button == LEFT_RELEASE), tmpbuf);
            }
        }
    }

    if (button == 'H' || button == 'h')
        return dupstr("H");

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->shared->w, h = state->shared->h;
    char c;
    int x, y, l, n;
    game_state *ret = dup_game(state);

    debug(("move: %s\n", move));

    while (*move) {
        c = *move;
        if (c == 'S') {
            ret->used_solve = true;
            move++;
        } else if (c == 'L' || c == 'N' || c == 'R' || c == 'F' || c == 'M') {
            /* 'line' or 'noline' or 'replace' or 'flip' or 'mark' */
            move++;
            if (sscanf(move, "%d,%d,%d%n", &l, &x, &y, &n) != 3)
                goto badmove;
            if (!INGRID(state, x, y)) goto badmove;
            if (l < 0 || l > 15) goto badmove;

            if (c == 'L')
                ret->lines[y*w + x] |= (char)l;
            else if (c == 'N')
                ret->lines[y*w + x] &= ~((char)l);
            else if (c == 'R') {
                ret->lines[y*w + x] = (char)l;
                ret->marks[y*w + x] &= ~((char)l); /* erase marks too */
            } else if (c == 'F')
                ret->lines[y*w + x] ^= (char)l;
            else if (c == 'M')
                ret->marks[y*w + x] ^= (char)l;

            /*
             * If we ended up trying to lay a line _over_ a mark,
             * that's a failed move: interpret_move() should have
             * ensured we never received a move string like that in
             * the first place.
             */
            if ((ret->lines[y*w + x] & (char)l) &&
                (ret->marks[y*w + x] & (char)l))
                goto badmove;

            move += n;
        } else if (strcmp(move, "H") == 0) {
            pearl_solve(ret->shared->w, ret->shared->h,
                        ret->shared->clues, ret->lines, DIFFCOUNT, true);
            for (n = 0; n < w*h; n++)
                ret->marks[n] &= ~ret->lines[n]; /* erase marks too */
            move++;
        } else {
            goto badmove;
        }
        if (*move == ';')
            move++;
        else if (*move)
            goto badmove;
    }

    if (!check_completion(ret, true)) goto badmove;

    return ret;

badmove:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define FLASH_TIME 0.5F

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int halfsz; } ads, *ds = &ads;
    ads.halfsz = (tilesize-1)/2;

    *x = (params->w) * TILE_SIZE + 2 * BORDER;
    *y = (params->h) * TILE_SIZE + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->halfsz = (tilesize-1)/2;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_BLACK * 3 + i] = 0.0F;
        ret[COL_WHITE * 3 + i] = 1.0F;
        ret[COL_GRID * 3 + i] = 0.4F;
    }

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_DRAGON * 3 + 0] = 0.0F;
    ret[COL_DRAGON * 3 + 1] = 0.0F;
    ret[COL_DRAGON * 3 + 2] = 1.0F;

    ret[COL_DRAGOFF * 3 + 0] = 0.8F;
    ret[COL_DRAGOFF * 3 + 1] = 0.8F;
    ret[COL_DRAGOFF * 3 + 2] = 1.0F;

    ret[COL_FLASH * 3 + 0] = 1.0F;
    ret[COL_FLASH * 3 + 1] = 1.0F;
    ret[COL_FLASH * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;

    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->halfsz = 0;
    ds->started = false;

    ds->w = state->shared->w;
    ds->h = state->shared->h;
    ds->sz = state->shared->sz;
    ds->lflags = snewn(ds->sz, unsigned int);
    for (i = 0; i < ds->sz; i++)
        ds->lflags[i] = 0;

    ds->draglines = snewn(ds->sz, char);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->draglines);
    sfree(ds->lflags);
    sfree(ds);
}

static void draw_lines_specific(drawing *dr, game_drawstate *ds,
                                int x, int y, unsigned int lflags,
                                unsigned int shift, int c)
{
    int ox = COORD(x), oy = COORD(y);
    int t2 = HALFSZ, t16 = HALFSZ/4;
    int cx = ox + t2, cy = oy + t2;
    int d;

    /* Draw each of the four directions, where laid (or error, or drag, etc.) */
    for (d = 1; d < 16; d *= 2) {
        int xoff = t2 * DX(d), yoff = t2 * DY(d);
        int xnudge = abs(t16 * DX(C(d))), ynudge = abs(t16 * DY(C(d)));

        if ((lflags >> shift) & d) {
            int lx = cx + ((xoff < 0) ? xoff : 0) - xnudge;
            int ly = cy + ((yoff < 0) ? yoff : 0) - ynudge;

            if (c == COL_DRAGOFF && !(lflags & d))
                continue;
            if (c == COL_DRAGON && (lflags & d))
                continue;

            draw_rect(dr, lx, ly,
                      abs(xoff)+2*xnudge+1,
                      abs(yoff)+2*ynudge+1, c);
            /* end cap */
            draw_rect(dr, cx - t16, cy - t16, 2*t16+1, 2*t16+1, c);
        }
    }
}

static void draw_square(drawing *dr, game_drawstate *ds, const game_ui *ui,
                        int x, int y, unsigned int lflags, char clue)
{
    int ox = COORD(x), oy = COORD(y);
    int t2 = HALFSZ, t16 = HALFSZ/4;
    int cx = ox + t2, cy = oy + t2;
    int d;

    assert(dr);

    /* Clip to the grid square. */
    clip(dr, ox, oy, TILE_SIZE, TILE_SIZE);

    /* Clear the square. */
    draw_rect(dr, ox, oy, TILE_SIZE, TILE_SIZE,
	      (lflags & DS_CURSOR) ?
	      COL_CURSOR_BACKGROUND : COL_BACKGROUND);
	      

    if (get_gui_style() == GUI_LOOPY) {
        /* Draw small dot, underneath any lines. */
        draw_circle(dr, cx, cy, t16, COL_GRID, COL_GRID);
    } else {
        /* Draw outline of grid square */
        draw_line(dr, ox, oy, COORD(x+1), oy, COL_GRID);
        draw_line(dr, ox, oy, ox, COORD(y+1), COL_GRID);
    }

    /* Draw grid: either thin gridlines, or no-line marks.
     * We draw these first because the thick laid lines should be on top. */
    for (d = 1; d < 16; d *= 2) {
        int xoff = t2 * DX(d), yoff = t2 * DY(d);

        if ((x == 0 && d == L) ||
            (y == 0 && d == U) ||
            (x == ds->w-1 && d == R) ||
            (y == ds->h-1 && d == D))
            continue; /* no gridlines out to the border. */

        if ((lflags >> DS_MSHIFT) & d) {
            /* either a no-line mark ... */
            int mx = cx + xoff, my = cy + yoff, msz = t16;

            draw_line(dr, mx-msz, my-msz, mx+msz, my+msz, COL_BLACK);
            draw_line(dr, mx-msz, my+msz, mx+msz, my-msz, COL_BLACK);
        } else {
            if (get_gui_style() == GUI_LOOPY) {
                /* draw grid lines connecting centre of cells */
                draw_line(dr, cx, cy, cx+xoff, cy+yoff, COL_GRID);
            }
        }
    }

    /* Draw each of the four directions, where laid (or error, or drag, etc.)
     * Order is important here, specifically for the eventual colours of the
     * exposed end caps. */
    draw_lines_specific(dr, ds, x, y, lflags, 0,
                        (lflags & DS_FLASH ? COL_FLASH : COL_BLACK));
    draw_lines_specific(dr, ds, x, y, lflags, DS_ESHIFT, COL_ERROR);
    draw_lines_specific(dr, ds, x, y, lflags, DS_DSHIFT, COL_DRAGOFF);
    draw_lines_specific(dr, ds, x, y, lflags, DS_DSHIFT, COL_DRAGON);

    /* Draw a clue, if present */
    if (clue != NOCLUE) {
        int c = (lflags & DS_FLASH) ? COL_FLASH :
                (clue == STRAIGHT) ? COL_WHITE : COL_BLACK;

        if (lflags & DS_ERROR_CLUE) /* draw a bigger 'error' clue circle. */
            draw_circle(dr, cx, cy, TILE_SIZE*3/8, COL_ERROR, COL_ERROR);

        draw_circle(dr, cx, cy, TILE_SIZE/4, c, COL_BLACK);
    }

    unclip(dr);
    draw_update(dr, ox, oy, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->shared->w, h = state->shared->h, sz = state->shared->sz;
    int x, y, flashing = 0;
    bool force = false;

    if (!ds->started) {
        if (get_gui_style() == GUI_MASYU) {
            /*
             * Black rectangle which is the main grid.
             */
            draw_rect(dr, BORDER - BORDER_WIDTH, BORDER - BORDER_WIDTH,
                      w*TILE_SIZE + 2*BORDER_WIDTH + 1,
                      h*TILE_SIZE + 2*BORDER_WIDTH + 1,
                      COL_GRID);
        }

        draw_update(dr, 0, 0, w*TILE_SIZE + 2*BORDER, h*TILE_SIZE + 2*BORDER);

        ds->started = true;
        force = true;
    }

    if (flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3))
        flashing = DS_FLASH;

    memset(ds->draglines, 0, sz);
    if (ui->ndragcoords > 0) {
        int i;
        bool clearing = true;
        for (i = 0; i < ui->ndragcoords - 1; i++) {
            int sx, sy, dx, dy, dir, oldstate, newstate;
            interpret_ui_drag(state, ui, &clearing, i, &sx, &sy, &dx, &dy,
                              &dir, &oldstate, &newstate);
            ds->draglines[sy*w+sx] ^= (oldstate ^ newstate);
            ds->draglines[dy*w+dx] ^= (F(oldstate) ^ F(newstate));
        }
    }	

    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            unsigned int f = (unsigned int)state->lines[y*w+x];
            unsigned int eline = (unsigned int)(state->errors[y*w+x] & (R|U|L|D));

            f |= eline << DS_ESHIFT;
            f |= ((unsigned int)ds->draglines[y*w+x]) << DS_DSHIFT;
            f |= ((unsigned int)state->marks[y*w+x]) << DS_MSHIFT;

            if (state->errors[y*w+x] & ERROR_CLUE)
                f |= DS_ERROR_CLUE;

            f |= flashing;

	    if (ui->cursor_active && x == ui->curx && y == ui->cury)
		f |= DS_CURSOR;

            if (f != ds->lflags[y*w+x] || force) {
                ds->lflags[y*w+x] = f;
                draw_square(dr, ds, ui, x, y, f, state->shared->clues[y*w+x]);
            }
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
    if (!oldstate->completed && newstate->completed &&
        !oldstate->used_solve && !newstate->used_solve)
        return FLASH_TIME;
    else
        return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cursor_active) {
        *x = COORD(ui->curx);
        *y = COORD(ui->cury);
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
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->shared->w, h = state->shared->h, x, y;
    int black = print_mono_colour(dr, 0);
    int white = print_mono_colour(dr, 1);

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate *ds = game_new_drawstate(dr, state);
    game_set_size(dr, ds, NULL, tilesize);

    if (get_gui_style() == GUI_MASYU) {
        /* Draw grid outlines (black). */
        for (x = 0; x <= w; x++)
            draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), black);
        for (y = 0; y <= h; y++)
            draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), black);
    } else {
        /* Draw small dots, and dotted lines connecting them. For
         * added clarity, try to start and end the dotted lines a
         * little way away from the dots. */
	print_line_width(dr, TILE_SIZE / 40);
	print_line_dotted(dr, true);
        for (x = 0; x < w; x++) {
            for (y = 0; y < h; y++) {
                int cx = COORD(x) + HALFSZ, cy = COORD(y) + HALFSZ;
                draw_circle(dr, cx, cy, tilesize/10, black, black);
                if (x+1 < w)
                    draw_line(dr, cx+tilesize/5, cy,
                              cx+tilesize-tilesize/5, cy, black);
                if (y+1 < h)
                    draw_line(dr, cx, cy+tilesize/5,
                              cx, cy+tilesize-tilesize/5, black);
            }
        }
	print_line_dotted(dr, false);
    }

    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            int cx = COORD(x) + HALFSZ, cy = COORD(y) + HALFSZ;
            int clue = state->shared->clues[y*w+x];

            draw_lines_specific(dr, ds, x, y, state->lines[y*w+x], 0, black);

            if (clue != NOCLUE) {
                int c = (clue == CORNER) ? black : white;
                draw_circle(dr, cx, cy, TILE_SIZE/4, c, black);
            }
        }
    }

    game_free_drawstate(dr, ds);
}

#ifdef COMBINED
#define thegame pearl
#endif

const struct game thegame = {
    "Pearl", "games.pearl", "pearl",
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
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

#ifdef STANDALONE_SOLVER

#include <time.h>
#include <stdarg.h>

static const char *quis = NULL;

static void usage(FILE *out) {
    fprintf(out, "usage: %s <params>\n", quis);
}

static void pnum(int n, int ntot, const char *desc)
{
    printf("%2.1f%% (%d) %s", (double)n*100.0 / (double)ntot, n, desc);
}

static void start_soak(game_params *p, random_state *rs, int nsecs)
{
    time_t tt_start, tt_now, tt_last;
    int n = 0, nsolved = 0, nimpossible = 0, ret;
    char *grid, *clues;

    tt_start = tt_last = time(NULL);

    /* Currently this generates puzzles of any difficulty (trying to solve it
     * on the maximum difficulty level and not checking it's not too easy). */
    printf("Soak-testing a %dx%d grid (any difficulty)", p->w, p->h);
    if (nsecs > 0) printf(" for %d seconds", nsecs);
    printf(".\n");

    p->nosolve = true;

    grid = snewn(p->w*p->h, char);
    clues = snewn(p->w*p->h, char);

    while (1) {
        n += new_clues(p, rs, clues, grid); /* should be 1, with nosolve */

        ret = pearl_solve(p->w, p->h, clues, grid, DIFF_TRICKY, false);
        if (ret <= 0) nimpossible++;
        if (ret == 1) nsolved++;

        tt_now = time(NULL);
        if (tt_now > tt_last) {
            tt_last = tt_now;

            printf("%d total, %3.1f/s, ",
                   n, (double)n / ((double)tt_now - tt_start));
            pnum(nsolved, n, "solved"); printf(", ");
            printf("%3.1f/s", (double)nsolved / ((double)tt_now - tt_start));
            if (nimpossible > 0)
                pnum(nimpossible, n, "impossible");
            printf("\n");
        }
        if (nsecs > 0 && (tt_now - tt_start) > nsecs) {
            printf("\n");
            break;
        }
    }

    sfree(grid);
    sfree(clues);
}

int main(int argc, char *argv[])
{
    game_params *p = NULL;
    random_state *rs = NULL;
    time_t seed = time(NULL);
    char *id = NULL;
    const char *err;

    setvbuf(stdout, NULL, _IONBF, 0);

    quis = argv[0];

    while (--argc > 0) {
        char *p = (char*)(*++argv);
        if (!strcmp(p, "-e") || !strcmp(p, "--seed")) {
            seed = atoi(*++argv);
            argc--;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            usage(stderr);
            exit(1);
        } else {
            id = p;
        }
    }

    rs = random_new((void*)&seed, sizeof(time_t));
    p = default_params();

    if (id) {
        if (strchr(id, ':')) {
            fprintf(stderr, "soak takes params only.\n");
            goto done;
        }

        decode_params(p, id);
        err = validate_params(p, true);
        if (err) {
            fprintf(stderr, "%s: %s", argv[0], err);
            goto done;
        }

        start_soak(p, rs, 0); /* run forever */
    } else {
        int i;

        for (i = 5; i <= 12; i++) {
            p->w = p->h = i;
            start_soak(p, rs, 5);
        }
    }

done:
    free_params(p);
    random_free(rs);

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
