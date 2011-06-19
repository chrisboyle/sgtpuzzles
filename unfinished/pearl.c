/*
 * pearl.c: Nikoli's `Masyu' puzzle. Currently this is a blank
 * puzzle file with nothing but a test solver-generator.
 */

/*
 * TODO:
 * 
 *  - The generation method appears to be fundamentally flawed. I
 *    think generating a random loop and then choosing a clue set
 *    is simply not a viable approach, because on a test run of
 *    10,000 attempts, it generated _six_ viable puzzles. All the
 *    rest of the randomly generated loops failed to be soluble
 *    even given a maximal clue set. Also, the vast majority of the
 *    clues were white circles (straight clues); black circles
 *    (corners) seem very uncommon.
 *     + So what can we do? One possible approach would be to
 * 	 adjust the random loop generation so that it created loops
 * 	 which were in some heuristic sense more likely to be
 * 	 viable Masyu puzzles. Certainly a good start on that would
 * 	 be to arrange that black clues actually _came up_ slightly
 * 	 more often, but I have no idea whether that would be
 * 	 sufficient.
 *     + A second option would be to throw the entire mechanism out
 * 	 and instead write a different generator from scratch which
 * 	 evolves the solution along with the puzzle: place a few
 * 	 clues, nail down a bit of the loop, place another clue,
 * 	 nail down some more, etc. It's unclear whether this can
 * 	 sensibly be done, though.
 * 
 *  - Puzzle playing UI and everything else apart from the
 *    generator...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

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
    COL_BACKGROUND,
    NCOLOURS
};

struct game_params {
    int FIXME;
};

struct game_state {
    int FIXME;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->FIXME = 0;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
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

static void decode_params(game_params *params, char const *string)
{
}

static char *encode_params(game_params *params, int full)
{
    return dupstr("FIXME");
}

static config_item *game_configure(game_params *params)
{
    return NULL;
}

static game_params *custom_params(config_item *cfg)
{
    return NULL;
}

static char *validate_params(game_params *params, int full)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

int pearl_solve(int w, int h, char *clues, char *result)
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
	int done_something = FALSE;

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
				done_something = TRUE;
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
			done_something = TRUE;
#ifdef SOLVER_DIAGNOSTICS
			printf("possible states of square (%d,%d) force edge"
			       " (%d,%d)-(%d,%d) to be disconnected\n",
			       x, y, ex/2, ey/2, (ex+1)/2, (ey+1)/2);
#endif
		    } else if ((edgeand & d) && workspace[ey*W+ex] == 3) {
			workspace[ey*W+ex] = 1;
			done_something = TRUE;
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
				done_something = TRUE;
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
				done_something = TRUE;
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
			    done_something = TRUE;
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
			    done_something = TRUE;
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
				    done_something = TRUE;
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
					done_something = TRUE;
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

	if (done_something)
	    continue;

	/*
	 * If we reach here, there is nothing left we can do.
	 * Return 2 for ambiguous puzzle.
	 */
	ret = 2;
	goto cleanup;
    }

    /*
     * If we reach _here_, it's by `break' out of the main loop,
     * which means we've successfully achieved a solution. This
     * means that we expect every square to be nailed down to
     * exactly one possibility. Transcribe those possibilities into
     * the result array.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
	    for (b = 0; b < 0xD; b++)
		if (workspace[(2*y+1)*W+(2*x+1)] == (1<<b)) {
		    result[y*w+x] = b;
		    break;
		}
	    assert(b < 0xD);	       /* we should have had a break by now */
	}

    cleanup:
    sfree(dsfsize);
    sfree(dsf);
    sfree(workspace);
    assert(ret >= 0);
    return ret;
}

/* ----------------------------------------------------------------------
 * Loop generator.
 */

void pearl_loopgen(int w, int h, char *grid, random_state *rs)
{
    int *options, *mindist, *maxdist, *list;
    int x, y, d, total, n, area, limit;

    /*
     * We're eventually going to have to return a w-by-h array
     * containing line segment data. However, it's more convenient
     * while actually generating the loop to consider the problem
     * as a (w-1) by (h-1) array in which some squares are `inside'
     * and some `outside'.
     * 
     * I'm going to use the top left corner of my return array in
     * the latter manner until the end of the function.
     */

    /*
     * To begin with, all squares are outside (0), except for one
     * randomly selected one which is inside (1).
     */
    memset(grid, 0, w*h);
    x = random_upto(rs, w-1);
    y = random_upto(rs, h-1);
    grid[y*w+x] = 1;

    /*
     * I'm also going to need an array to store the possible
     * options for the next extension of the grid.
     */
    options = snewn(w*h, int);
    for (x = 0; x < w*h; x++)
	options[x] = 0;

    /*
     * And some arrays and a list for breadth-first searching.
     */
    mindist = snewn(w*h, int);
    maxdist = snewn(w*h, int);
    list = snewn(w*h, int);

    /*
     * Now we repeatedly scan the grid for feasible squares into
     * which we can extend our loop, pick one, and do it.
     */
    area = 1;

    while (1) {
#ifdef LOOPGEN_DIAGNOSTICS
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++)
		printf("%d", grid[y*w+x]);
	    printf("\n");
	}
	printf("\n");
#endif

	/*
	 * Our primary aim in growing this loop is to make it
	 * reasonably _dense_ in the target rectangle. That is, we
	 * want the maximum over all squares of the minimum
	 * distance from that square to the loop to be small.
	 * 
	 * Therefore, we start with a breadth-first search of the
	 * grid to find those minimum distances.
	 */
	{
	    int head = 0, tail = 0;
	    int i;

	    for (i = 0; i < w*h; i++) {
		mindist[i] = -1;
		if (grid[i]) {
		    mindist[i] = 0;
		    list[tail++] = i;
		}
	    }

	    while (head < tail) {
		i = list[head++];
		y = i / w;
		x = i % w;
		for (d = 1; d <= 8; d += d) {
		    int xx = x + DX(d), yy = y + DY(d);
		    if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
			mindist[yy*w+xx] < 0) {
			mindist[yy*w+xx] = mindist[i] + 1;
			list[tail++] = yy*w+xx;
		    }
		}
	    }

	    /*
	     * Having done the BFS, we now backtrack along its path
	     * to determine the most distant square that each
	     * square is on the shortest path to. This tells us
	     * which of the loop extension candidates (all of which
	     * are squares marked 1) is most desirable to extend
	     * into in terms of minimising the maximum distance
	     * from any empty square to the nearest loop square.
	     */
	    for (head = tail; head-- > 0 ;) {
		int max;

		i = list[head];
		y = i / w;
		x = i % w;

		max = mindist[i];

		for (d = 1; d <= 8; d += d) {
		    int xx = x + DX(d), yy = y + DY(d);
		    if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
			mindist[yy*w+xx] > mindist[i] &&
			maxdist[yy*w+xx] > max) {
			max = maxdist[yy*w+xx];
		    }
		}

		maxdist[i] = max;
	    }
	}

	/*
	 * A square is a viable candidate for extension of our loop
	 * if and only if the following conditions are all met:
	 *  - It is currently labelled 0.
	 *  - At least one of its four orthogonal neighbours is
	 *    labelled 1.
	 *  - If you consider its eight orthogonal and diagonal
	 *    neighbours to form a ring, that ring contains at most
	 *    one contiguous run of 1s. (It must also contain at
	 *    _least_ one, of course, but that's already guaranteed
	 *    by the previous condition so there's no need to test
	 *    it separately.)
	 */
	total = 0;
	for (y = 0; y < h-1; y++)
	    for (x = 0; x < w-1; x++) {
		int ring[8];
		int rx, neighbours, runs, dist;

		dist = maxdist[y*w+x];
		options[y*w+x] = 0;

		if (grid[y*w+x])
		    continue;	       /* it isn't labelled 0 */

		neighbours = 0;
		for (rx = 0, d = 1; d <= 8; rx += 2, d += d) {
		    int x2 = x + DX(d), y2 = y + DY(d);
		    int x3 = x2 + DX(A(d)), y3 = y2 + DY(A(d));
		    int g2 = (x2 >= 0 && x2 < w && y2 >= 0 && y2 < h ?
			      grid[y2*w+x2] : 0);
		    int g3 = (x3 >= 0 && x3 < w && y3 >= 0 && y3 < h ?
			      grid[y3*w+x3] : 0);
		    ring[rx] = g2;
		    ring[rx+1] = g3;
		    if (g2)
			neighbours++;
		}

		if (!neighbours)
		    continue;	       /* it doesn't have a 1 neighbour */

		runs = 0;
		for (rx = 0; rx < 8; rx++)
		    if (ring[rx] && !ring[(rx+1) & 7])
			runs++;

		if (runs > 1)
		    continue;	       /* too many runs of 1s */

		/*
		 * Now we know this square is a viable extension
		 * candidate. Mark it.
		 * 
		 * FIXME: probabilistic prioritisation based on
		 * perimeter perturbation? (Wow, must keep that
		 * phrase.)
		 */
		options[y*w+x] = dist * (4-neighbours) * (4-neighbours);
		total += options[y*w+x];
	    }

	if (!total)
	    break;		       /* nowhere to go! */

	/*
	 * Now pick a random one of the viable extension squares,
	 * and extend into it.
	 */
	n = random_upto(rs, total);
	for (y = 0; y < h-1; y++)
	    for (x = 0; x < w-1; x++) {
		assert(n >= 0);
		if (options[y*w+x] > n)
		    goto found;	       /* two-level break */
		n -= options[y*w+x];
	    }
	assert(!"We shouldn't ever get here");
	found:
	grid[y*w+x] = 1;
	area++;

	/*
	 * We terminate the loop when around 7/12 of the grid area
	 * is full, but we also require that the loop has reached
	 * all four edges.
	 */
	limit = random_upto(rs, (w-1)*(h-1)) + 13*(w-1)*(h-1);
	if (24 * area > limit) {
	    int l = FALSE, r = FALSE, u = FALSE, d = FALSE;
	    for (x = 0; x < w; x++) {
		if (grid[0*w+x])
		    u = TRUE;
		if (grid[(h-2)*w+x])
		    d = TRUE;
	    }
	    for (y = 0; y < h; y++) {
		if (grid[y*w+0])
		    l = TRUE;
		if (grid[y*w+(w-2)])
		    r = TRUE;
	    }
	    if (l && r && u && d)
		break;
	}
    }

    sfree(list);
    sfree(maxdist);
    sfree(mindist);
    sfree(options);

#ifdef LOOPGEN_DIAGNOSTICS
    printf("final loop:\n");
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++)
	    printf("%d", grid[y*w+x]);
	printf("\n");
    }
    printf("\n");
#endif

    /*
     * Now convert this array of 0s and 1s into an array of path
     * components.
     */
    for (y = h; y-- > 0 ;) {
	for (x = w; x-- > 0 ;) {
	    /*
	     * Examine the four grid squares of which (x,y) are in
	     * the bottom right, to determine the output for this
	     * square.
	     */
	    int ul = (x > 0 && y > 0 ? grid[(y-1)*w+(x-1)] : 0);
	    int ur = (y > 0 ? grid[(y-1)*w+x] : 0);
	    int dl = (x > 0 ? grid[y*w+(x-1)] : 0);
	    int dr = grid[y*w+x];
	    int type = 0;

	    if (ul != ur) type |= U;
	    if (dl != dr) type |= D;
	    if (ul != dl) type |= L;
	    if (ur != dr) type |= R;

	    assert((bLR|bUD|bLU|bLD|bRU|bRD|bBLANK) & (1 << type));

	    grid[y*w+x] = type;

	}
    }

#if defined LOOPGEN_DIAGNOSTICS && !defined GENERATION_DIAGNOSTICS
    printf("as returned:\n");
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
	    int type = grid[y*w+x];
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

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    char *grid, *clues;
    int *clueorder;
    int w = 10, h = 10;
    int x, y, d, ret, i;

#if 0
    clues = snewn(7*7, char);
    memcpy(clues,
	   "\0\1\0\0\2\0\0"
	   "\0\0\0\2\0\0\0"
	   "\0\0\0\2\0\0\1"
	   "\2\0\0\2\0\0\0"
	   "\2\0\0\0\0\0\1"
	   "\0\0\1\0\0\2\0"
	   "\0\0\2\0\0\0\0", 7*7);
    grid = snewn(7*7, char);
    printf("%d\n", pearl_solve(7, 7, clues, grid));
#elif 0
    clues = snewn(10*10, char);
    memcpy(clues,
	   "\0\0\2\0\2\0\0\0\0\0"
	   "\0\0\0\0\2\0\0\0\1\0"
	   "\0\0\1\0\1\0\2\0\0\0"
	   "\0\0\0\2\0\0\2\0\0\0"
	   "\1\0\0\0\0\2\0\0\0\2"
	   "\0\0\2\0\0\0\0\2\0\0"
	   "\0\0\1\0\0\0\2\0\0\0"
	   "\2\0\0\0\1\0\0\0\0\2"
	   "\0\0\0\0\0\0\2\2\0\0"
	   "\0\0\1\0\0\0\0\0\0\1", 10*10);
    grid = snewn(10*10, char);
    printf("%d\n", pearl_solve(10, 10, clues, grid));
#elif 0
    clues = snewn(10*10, char);
    memcpy(clues,
	   "\0\0\0\0\0\0\1\0\0\0"
	   "\0\1\0\1\2\0\0\0\0\2"
	   "\0\0\0\0\0\0\0\0\0\1"
	   "\2\0\0\1\2\2\1\0\0\0"
	   "\1\0\0\0\0\0\0\1\0\0"
	   "\0\0\2\0\0\0\0\0\0\2"
	   "\0\0\0\2\1\2\1\0\0\2"
	   "\2\0\0\0\0\0\0\0\0\0"
	   "\2\0\0\0\0\1\1\0\2\0"
	   "\0\0\0\2\0\0\0\0\0\0", 10*10);
    grid = snewn(10*10, char);
    printf("%d\n", pearl_solve(10, 10, clues, grid));
#endif

    grid = snewn(w*h, char);
    clues = snewn(w*h, char);
    clueorder = snewn(w*h, int);

    while (1) {
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

	/*
	 * See if we can solve the puzzle just like this.
	 */
	ret = pearl_solve(w, h, clues, grid);
	assert(ret > 0);	       /* shouldn't be inconsistent! */
	if (ret != 1)
	    continue;		       /* go round and try again */

	/*
	 * Now shuffle the grid points and gradually remove the
	 * clues to find a minimal set which still leaves the
	 * puzzle soluble.
	 */
	for (i = 0; i < w*h; i++)
	    clueorder[i] = i;
	shuffle(clueorder, w*h, sizeof(*clueorder), rs);
	for (i = 0; i < w*h; i++) {
	    int clue;

	    y = clueorder[i] / w;
	    x = clueorder[i] % w;

	    if (clues[y*w+x] == 0)
		continue;

	    clue = clues[y*w+x];
	    clues[y*w+x] = 0;	       /* try removing this clue */

	    ret = pearl_solve(w, h, clues, grid);
	    assert(ret > 0);
	    if (ret != 1)
		clues[y*w+x] = clue;   /* oops, put it back again */
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

    sfree(grid);
    sfree(clues);
    sfree(clueorder);

    return dupstr("FIXME");
}

static char *validate_desc(game_params *params, char *desc)
{
    return NULL;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    game_state *state = snew(game_state);

    state->FIXME = 0;

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->FIXME = state->FIXME;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    return NULL;
}

static int game_can_format_as_text_now(game_params *params)
{
    return TRUE;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

struct game_drawstate {
    int tilesize;
    int FIXME;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    return NULL;
}

static game_state *execute_move(game_state *state, char *move)
{
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    *x = *y = 10 * tilesize;	       /* FIXME */
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->FIXME = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
    draw_rect(dr, 0, 0, 10*ds->tilesize, 10*ds->tilesize, COL_BACKGROUND);
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    return 0.0F;
}

static int game_status(game_state *state)
{
    return 0;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame pearl
#endif

const struct game thegame = {
    "Pearl", NULL, NULL,
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    FALSE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    FALSE, solve_game,
    FALSE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    20 /* FIXME */, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    FALSE, FALSE, game_print_size, game_print,
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};
