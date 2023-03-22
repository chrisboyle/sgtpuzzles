/*
 * towers.c: the puzzle also known as 'Skyscrapers'.
 *
 * Possible future work:
 *
 *  - Relax the upper bound on grid size at 9?
 *     + I'd need TOCHAR and FROMCHAR macros a bit like group's, to
 * 	 be used wherever this code has +'0' or -'0'
 *     + the pencil marks in the drawstate would need a separate
 * 	 word to live in
 *     + the clues outside the grid would have to cope with being
 * 	 multi-digit, meaning in particular that the text formatting
 * 	 would become more unpleasant
 *     + most importantly, though, the solver just isn't fast
 * 	 enough. Even at size 9 it can't really do the solver_hard
 * 	 factorial-time enumeration at a sensible rate. Easy puzzles
 * 	 higher than that would be possible, but more latin-squarey
 * 	 than skyscrapery, as it were.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(EASY,Easy,solver_easy,e) \
    A(HARD,Hard,solver_hard,h) \
    A(EXTREME,Extreme,NULL,x) \
    A(UNREASONABLE,Unreasonable,NULL,u)
#define ENUM(upper,title,func,lower) DIFF_ ## upper,
#define TITLE(upper,title,func,lower) #title,
#define ENCODE(upper,title,func,lower) #lower
#define CONFIG(upper,title,func,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const towers_diffnames[] = { DIFFLIST(TITLE) };
static char const towers_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    COL_DONE,
    NCOLOURS
};

struct game_params {
    int w, diff;
};

struct clues {
    int refcount;
    int w;
    /*
     * An array of 4w integers, of which:
     *  - the first w run across the top
     *  - the next w across the bottom
     *  - the third w down the left
     *  - the last w down the right.
     */
    int *clues;

    /*
     * An array of w*w digits.
     */
    digit *immutable;
};

/*
 * Macros to compute clue indices and coordinates.
 */
#define STARTSTEP(start, step, index, w) do { \
    if (index < w) \
	start = index, step = w; \
    else if (index < 2*w) \
	start = (w-1)*w+(index-w), step = -w; \
    else if (index < 3*w) \
	start = w*(index-2*w), step = 1; \
    else \
	start = w*(index-3*w)+(w-1), step = -1; \
} while (0)
#define CSTARTSTEP(start, step, index, w) \
    STARTSTEP(start, step, (((index)+2*w)%(4*w)), w)
#define CLUEPOS(x, y, index, w) do { \
    if (index < w) \
	x = index, y = -1; \
    else if (index < 2*w) \
	x = index-w, y = w; \
    else if (index < 3*w) \
	x = -1, y = index-2*w; \
    else \
	x = w, y = index-3*w; \
} while (0)

#ifdef STANDALONE_SOLVER
static const char *const cluepos[] = {
    "above column", "below column", "left of row", "right of row"
};
#endif

struct game_state {
    game_params par;
    struct clues *clues;
    bool *clues_done;
    digit *grid;
    int *pencil;		       /* bitmaps using bits 1<<1..1<<n */
    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 5;
    ret->diff = DIFF_EASY;

    return ret;
}

static const struct game_params towers_presets[] = {
    {  4, DIFF_EASY         },
    {  5, DIFF_EASY         },
    {  5, DIFF_HARD         },
    {  6, DIFF_EASY         },
    {  6, DIFF_HARD         },
    {  6, DIFF_EXTREME      },
    {  6, DIFF_UNREASONABLE },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(towers_presets))
        return false;

    ret = snew(game_params);
    *ret = towers_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s", ret->w, ret->w, towers_diffnames[ret->diff]);

    *name = dupstr(buf);
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

    if (*p == 'd') {
        int i;
        p++;
        params->diff = DIFFCOUNT+1; /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == towers_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[80];

    sprintf(ret, "%d", params->w);
    if (full)
        sprintf(ret + strlen(ret), "d%c", towers_diffchars[params->diff]);

    return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Grid size";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].u.choices.choicenames = DIFFCONFIG;
    ret[1].u.choices.selected = params->diff;

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->diff = cfg[1].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 3 || params->w > 9)
        return "Grid size must be between 3 and 9";
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

struct solver_ctx {
    int w, diff;
    bool started;
    int *clues;
    long *iscratch;
    int *dscratch;
};

static int solver_easy(struct latin_solver *solver, void *vctx)
{
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    int w = ctx->w;
    int c, i, j, n, m, furthest;
    int start, step, cstart, cstep, clue, pos, cpos;
    int ret = 0;
#ifdef STANDALONE_SOLVER
    char prefix[256];
#endif

    if (!ctx->started) {
	ctx->started = true;
	/*
	 * One-off loop to help get started: when a pair of facing
	 * clues sum to w+1, it must mean that the row consists of
	 * two increasing sequences back to back, so we can
	 * immediately place the highest digit by knowing the
	 * lengths of those two sequences.
	 */
	for (c = 0; c < 3*w; c = (c == w-1 ? 2*w : c+1)) {
	    int c2 = c + w;

	    if (ctx->clues[c] && ctx->clues[c2] &&
		ctx->clues[c] + ctx->clues[c2] == w+1) {
		STARTSTEP(start, step, c, w);
		CSTARTSTEP(cstart, cstep, c, w);
		pos = start + (ctx->clues[c]-1)*step;
		cpos = cstart + (ctx->clues[c]-1)*cstep;
		if (solver->cube[cpos*w+w-1]) {
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*sfacing clues on %s %d are maximal:\n",
			       solver_recurse_depth*4, "",
			       c>=2*w ? "row" : "column", c % w + 1);
			printf("%*s  placing %d at (%d,%d)\n",
			       solver_recurse_depth*4, "",
			       w, pos%w+1, pos/w+1);
		    }
#endif
		    latin_solver_place(solver, pos%w, pos/w, w);
		    ret = 1;
		} else {
		    ret = -1;
		}
	    }
	}

	if (ret)
	    return ret;
    }

    /*
     * Go over every clue doing reasonably simple heuristic
     * deductions.
     */
    for (c = 0; c < 4*w; c++) {
	clue = ctx->clues[c];
	if (!clue)
	    continue;
	STARTSTEP(start, step, c, w);
	CSTARTSTEP(cstart, cstep, c, w);

	/* Find the location of each number in the row. */
	for (i = 0; i < w; i++)
	    ctx->dscratch[i] = w;
	for (i = 0; i < w; i++)
	    if (solver->grid[start+i*step])
		ctx->dscratch[solver->grid[start+i*step]-1] = i;

	n = m = 0;
	furthest = w;
	for (i = w; i >= 1; i--) {
	    if (ctx->dscratch[i-1] == w) {
		break;
	    } else if (ctx->dscratch[i-1] < furthest) {
		furthest = ctx->dscratch[i-1];
		m = i;
		n++;
	    }
	}
	if (clue == n+1 && furthest > 1) {
#ifdef STANDALONE_SOLVER
	    if (solver_show_working)
		sprintf(prefix, "%*sclue %s %d is nearly filled:\n",
			solver_recurse_depth*4, "",
			cluepos[c/w], c%w+1);
	    else
		prefix[0] = '\0';	       /* placate optimiser */
#endif
	    /*
	     * We can already see an increasing sequence of the very
	     * highest numbers, of length one less than that
	     * specified in the clue. All of those numbers _must_ be
	     * part of the clue sequence, so the number right next
	     * to the clue must be the final one - i.e. it must be
	     * bigger than any of the numbers between it and m. This
	     * allows us to rule out small numbers in that square.
	     *
	     * (This is a generalisation of the obvious deduction
	     * that when you see a clue saying 1, it must be right
	     * next to the largest possible number; and similarly,
	     * when you see a clue saying 2 opposite that, it must
	     * be right next to the second-largest.)
	     */
	    j = furthest-1;  /* number of small numbers we can rule out */
	    for (i = 1; i <= w && j > 0; i++) {
		if (ctx->dscratch[i-1] < w && ctx->dscratch[i-1] >= furthest)
		    continue;	       /* skip this number, it's elsewhere */
		j--;
		if (solver->cube[cstart*w+i-1]) {
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%s%*s  ruling out %d at (%d,%d)\n",
			       prefix, solver_recurse_depth*4, "",
			       i, start%w+1, start/w+1);
			prefix[0] = '\0';
		    }
#endif
		    solver->cube[cstart*w+i-1] = 0;
		    ret = 1;
		}
	    }
	}

	if (ret)
	    return ret;

#ifdef STANDALONE_SOLVER
	if (solver_show_working)
	    sprintf(prefix, "%*slower bounds for clue %s %d:\n",
		    solver_recurse_depth*4, "",
		    cluepos[c/w], c%w+1);
	else
	    prefix[0] = '\0';	       /* placate optimiser */
#endif

	i = 0;
	for (n = w; n > 0; n--) {
	    /*
	     * The largest number cannot occur in the first (clue-1)
	     * squares of the row, or else there wouldn't be space
	     * for a sufficiently long increasing sequence which it
	     * terminated. The second-largest number (not counting
	     * any that are known to be on the far side of a larger
	     * number and hence excluded from this sequence) cannot
	     * occur in the first (clue-2) squares, similarly, and
	     * so on.
	     */

	    if (ctx->dscratch[n-1] < w) {
		for (m = n+1; m < w; m++)
		    if (ctx->dscratch[m] < ctx->dscratch[n-1])
			break;
		if (m < w)
		    continue;	       /* this number doesn't count */
	    }

	    for (j = 0; j < clue - i - 1; j++)
		if (solver->cube[(cstart + j*cstep)*w+n-1]) {
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			int pos = start+j*step;
			printf("%s%*s  ruling out %d at (%d,%d)\n",
			       prefix, solver_recurse_depth*4, "",
			       n, pos%w+1, pos/w+1);
			prefix[0] = '\0';
		    }
#endif
		    solver->cube[(cstart + j*cstep)*w+n-1] = 0;
		    ret = 1;
		}
	    i++;
	}
    }

    if (ret)
	return ret;

    return 0;
}

static int solver_hard(struct latin_solver *solver, void *vctx)
{
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    int w = ctx->w;
    int c, i, j, n, best, clue, start, step, ret;
    long bitmap;
#ifdef STANDALONE_SOLVER
    char prefix[256];
#endif

    /*
     * Go over every clue analysing all possibilities.
     */
    for (c = 0; c < 4*w; c++) {
	clue = ctx->clues[c];
	if (!clue)
	    continue;
	CSTARTSTEP(start, step, c, w);

	for (i = 0; i < w; i++)
	    ctx->iscratch[i] = 0;

	/*
	 * Instead of a tedious physical recursion, I iterate in the
	 * scratch array through all possibilities. At any given
	 * moment, i indexes the element of the box that will next
	 * be incremented.
	 */
	i = 0;
	ctx->dscratch[i] = 0;
	best = n = 0;
	bitmap = 0;

	while (1) {
	    if (i < w) {
		/*
		 * Find the next valid value for cell i.
		 */
		int limit = (n == clue ? best : w);
		int pos = start + step * i;
		for (j = ctx->dscratch[i] + 1; j <= limit; j++) {
		    if (bitmap & (1L << j))
			continue;      /* used this one already */
		    if (!solver->cube[pos*w+j-1])
			continue;      /* ruled out already */

		    /* Found one. */
		    break;
		}

		if (j > limit) {
		    /* No valid values left; drop back. */
		    i--;
		    if (i < 0)
			break;	       /* overall iteration is finished */
		    bitmap &= ~(1L << ctx->dscratch[i]);
		    if (ctx->dscratch[i] == best) {
			n--;
			best = 0;
			for (j = 0; j < i; j++)
			    if (best < ctx->dscratch[j])
				best = ctx->dscratch[j];
		    }
		} else {
		    /* Got a valid value; store it and move on. */
		    bitmap |= 1L << j;
		    ctx->dscratch[i++] = j;
		    if (j > best) {
			best = j;
			n++;
		    }
		    ctx->dscratch[i] = 0;
		}
	    } else {
		if (n == clue) {
		    for (j = 0; j < w; j++)
			ctx->iscratch[j] |= 1L << ctx->dscratch[j];
		}
		i--;
		bitmap &= ~(1L << ctx->dscratch[i]);
		if (ctx->dscratch[i] == best) {
		    n--;
		    best = 0;
		    for (j = 0; j < i; j++)
			if (best < ctx->dscratch[j])
			    best = ctx->dscratch[j];
		}
	    }
	}

#ifdef STANDALONE_SOLVER
	if (solver_show_working)
	    sprintf(prefix, "%*sexhaustive analysis of clue %s %d:\n",
		    solver_recurse_depth*4, "",
		    cluepos[c/w], c%w+1);
	else
	    prefix[0] = '\0';	       /* placate optimiser */
#endif

	ret = 0;

	for (i = 0; i < w; i++) {
	    int pos = start + step * i;
	    for (j = 1; j <= w; j++) {
		if (solver->cube[pos*w+j-1] &&
		    !(ctx->iscratch[i] & (1L << j))) {
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%s%*s  ruling out %d at (%d,%d)\n",
			       prefix, solver_recurse_depth*4, "",
			       j, pos/w+1, pos%w+1);
			prefix[0] = '\0';
		    }
#endif
		    solver->cube[pos*w+j-1] = 0;
		    ret = 1;
		}
	    }

	    /*
	     * Once we find one clue we can do something with in
	     * this way, revert to trying easier deductions, so as
	     * not to generate solver diagnostics that make the
	     * problem look harder than it is.
	     */
	    if (ret)
		return ret;
	}
    }

    return 0;
}

#define SOLVER(upper,title,func,lower) func,
static usersolver_t const towers_solvers[] = { DIFFLIST(SOLVER) };

static bool towers_valid(struct latin_solver *solver, void *vctx)
{
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    int w = ctx->w;
    int c, i, n, best, clue, start, step;
    for (c = 0; c < 4*w; c++) {
	clue = ctx->clues[c];
	if (!clue)
	    continue;

        STARTSTEP(start, step, c, w);
        n = best = 0;
        for (i = 0; i < w; i++) {
            if (solver->grid[start+i*step] > best) {
                best = solver->grid[start+i*step];
                n++;
            }
        }

        if (n != clue) {
#ifdef STANDALONE_SOLVER
            if (solver_show_working)
		printf("%*sclue %s %d is violated\n",
			solver_recurse_depth*4, "",
			cluepos[c/w], c%w+1);
#endif
            return false;
        }
    }
    return true;
}

static int solver(int w, int *clues, digit *soln, int maxdiff)
{
    int ret;
    struct solver_ctx ctx;

    ctx.w = w;
    ctx.diff = maxdiff;
    ctx.clues = clues;
    ctx.started = false;
    ctx.iscratch = snewn(w, long);
    ctx.dscratch = snewn(w+1, int);

    ret = latin_solver(soln, w, maxdiff,
		       DIFF_EASY, DIFF_HARD, DIFF_EXTREME,
		       DIFF_EXTREME, DIFF_UNREASONABLE,
		       towers_solvers, towers_valid, &ctx, NULL, NULL);

    sfree(ctx.iscratch);
    sfree(ctx.dscratch);

    return ret;
}

/* ----------------------------------------------------------------------
 * Grid generation.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, a = w*w;
    digit *grid, *soln, *soln2;
    int *clues, *order;
    int i, ret;
    int diff = params->diff;
    char *desc, *p;

    /*
     * Difficulty exceptions: some combinations of size and
     * difficulty cannot be satisfied, because all puzzles of at
     * most that difficulty are actually even easier.
     *
     * Remember to re-test this whenever a change is made to the
     * solver logic!
     *
     * I tested it using the following shell command:

for d in e h x u; do
  for i in {3..9}; do
    echo -n "./towers --generate 1 ${i}d${d}: "
    perl -e 'alarm 30; exec @ARGV' ./towers --generate 1 ${i}d${d} >/dev/null \
      && echo ok
  done
done

     * Of course, it's better to do that after taking the exceptions
     * _out_, so as to detect exceptions that should be removed as
     * well as those which should be added.
     */
    if (diff > DIFF_HARD && w <= 3)
	diff = DIFF_HARD;

    grid = NULL;
    clues = snewn(4*w, int);
    soln = snewn(a, digit);
    soln2 = snewn(a, digit);
    order = snewn(max(4*w,a), int);

    while (1) {
	/*
	 * Construct a latin square to be the solution.
	 */
	sfree(grid);
	grid = latin_generate(w, rs);

	/*
	 * Fill in the clues.
	 */
	for (i = 0; i < 4*w; i++) {
	    int start, step, j, k, best;
	    STARTSTEP(start, step, i, w);
	    k = best = 0;
	    for (j = 0; j < w; j++) {
		if (grid[start+j*step] > best) {
		    best = grid[start+j*step];
		    k++;
		}
	    }
	    clues[i] = k;
	}

	/*
	 * Remove the grid numbers and then the clues, one by one,
	 * for as long as the game remains soluble at the given
	 * difficulty.
	 */
	memcpy(soln, grid, a);

	if (diff == DIFF_EASY && w <= 5) {
	    /*
	     * Special case: for Easy-mode grids that are small
	     * enough, it's nice to be able to find completely empty
	     * grids.
	     */
	    memset(soln2, 0, a);
	    ret = solver(w, clues, soln2, diff);
	    if (ret > diff)
		continue;
	}

	for (i = 0; i < a; i++)
	    order[i] = i;
	shuffle(order, a, sizeof(*order), rs);
	for (i = 0; i < a; i++) {
	    int j = order[i];

	    memcpy(soln2, grid, a);
	    soln2[j] = 0;
	    ret = solver(w, clues, soln2, diff);
	    if (ret <= diff)
		grid[j] = 0;
	}

	if (diff > DIFF_EASY) {	       /* leave all clues on Easy mode */
	    for (i = 0; i < 4*w; i++)
		order[i] = i;
	    shuffle(order, 4*w, sizeof(*order), rs);
	    for (i = 0; i < 4*w; i++) {
		int j = order[i];
		int clue = clues[j];

		memcpy(soln2, grid, a);
		clues[j] = 0;
		ret = solver(w, clues, soln2, diff);
		if (ret > diff)
		    clues[j] = clue;
	    }
	}

	/*
	 * See if the game can be solved at the specified difficulty
	 * level, but not at the one below.
	 */
	memcpy(soln2, grid, a);
	ret = solver(w, clues, soln2, diff);
	if (ret != diff)
	    continue;		       /* go round again */

	/*
	 * We've got a usable puzzle!
	 */
	break;
    }

    /*
     * Encode the puzzle description.
     */
    desc = snewn(40*a, char);
    p = desc;
    for (i = 0; i < 4*w; i++) {
        if (i)
            *p++ = '/';
        if (clues[i])
            p += sprintf(p, "%d", clues[i]);
    }
    for (i = 0; i < a; i++)
	if (grid[i])
	    break;
    if (i < a) {
	int run = 0;

	*p++ = ',';

	for (i = 0; i <= a; i++) {
	    int n = (i < a ? grid[i] : -1);

	    if (!n)
		run++;
	    else {
		if (run) {
		    while (run > 0) {
			int thisrun = min(run, 26);
			*p++ = thisrun - 1 + 'a';
			run -= thisrun;
		    }
		} else {
		    /*
		     * If there's a number in the very top left or
		     * bottom right, there's no point putting an
		     * unnecessary _ before or after it.
		     */
		    if (i > 0 && n > 0)
			*p++ = '_';
		}
		if (n > 0)
		    p += sprintf(p, "%d", n);
		run = 0;
	    }
	}
    }
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    /*
     * Encode the solution.
     */
    *aux = snewn(a+2, char);
    (*aux)[0] = 'S';
    for (i = 0; i < a; i++)
	(*aux)[i+1] = '0' + soln[i];
    (*aux)[a+1] = '\0';

    sfree(grid);
    sfree(clues);
    sfree(soln);
    sfree(soln2);
    sfree(order);

    return desc;
}

/* ----------------------------------------------------------------------
 * Gameplay.
 */

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, a = w*w;
    const char *p = desc;
    int i, clue;

    /*
     * Verify that the right number of clues are given, and that
     * they're in range.
     */
    for (i = 0; i < 4*w; i++) {
	if (!*p)
	    return "Too few clues for grid size";

	if (i > 0) {
	    if (*p != '/')
		return "Expected commas between clues";
	    p++;
	}

	if (isdigit((unsigned char)*p)) {
	    clue = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;

	    if (clue <= 0 || clue > w)
		return "Clue number out of range";
	}
    }
    if (*p == '/')
	return "Too many clues for grid size";

    if (*p == ',') {
	/*
	 * Verify that the right amount of grid data is given, and
	 * that any grid elements provided are in range.
	 */
	int squares = 0;

	p++;
	while (*p) {
	    int c = *p++;
	    if (c >= 'a' && c <= 'z') {
		squares += c - 'a' + 1;
	    } else if (c == '_') {
		/* do nothing */;
	    } else if (c > '0' && c <= '9') {
		int val = atoi(p-1);
		if (val < 1 || val > w)
		    return "Out-of-range number in grid description";
		squares++;
		while (*p && isdigit((unsigned char)*p)) p++;
	    } else
		return "Invalid character in game description";
	}

	if (squares < a)
	    return "Not enough data to fill grid";

	if (squares > a)
	    return "Too much data to fit in grid";
    }

    if (*p) return "Rubbish at end of game description";
    return NULL;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
    int i;
    int w = params->w;
    key_label *keys = snewn(w+1, key_label);
    *nkeys = w + 1;

    for (i = 0; i < w; i++) {
	if (i<9) keys[i].button = '1' + i;
	else keys[i].button = 'a' + i - 9;

        keys[i].label = NULL;
    }
    keys[w].button = '\b';
    keys[w].label = NULL;

    return keys;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, a = w*w;
    game_state *state = snew(game_state);
    const char *p = desc;
    int i;

    state->par = *params;	       /* structure copy */
    state->clues = snew(struct clues);
    state->clues->refcount = 1;
    state->clues->w = w;
    state->clues->clues = snewn(4*w, int);
    state->clues->immutable = snewn(a, digit);
    state->grid = snewn(a, digit);
    state->clues_done = snewn(4*w, bool);
    state->pencil = snewn(a, int);

    for (i = 0; i < a; i++) {
	state->grid[i] = 0;
	state->pencil[i] = 0;
    }

    memset(state->clues->immutable, 0, a);
    memset(state->clues_done, 0, 4*w*sizeof(bool));

    for (i = 0; i < 4*w; i++) {
	if (i > 0) {
	    assert(*p == '/');
	    p++;
	}
	if (*p && isdigit((unsigned char)*p)) {
	    state->clues->clues[i] = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	} else
	    state->clues->clues[i] = 0;
    }

    if (*p == ',') {
	int pos = 0;
	p++;
	while (*p) {
	    int c = *p++;
	    if (c >= 'a' && c <= 'z') {
		pos += c - 'a' + 1;
	    } else if (c == '_') {
		/* do nothing */;
	    } else if (c > '0' && c <= '9') {
		int val = atoi(p-1);
		assert(val >= 1 && val <= w);
		assert(pos < a);
		state->grid[pos] = state->clues->immutable[pos] = val;
		pos++;
		while (*p && isdigit((unsigned char)*p)) p++;
	    } else
		assert(!"Corrupt game description");
	}
	assert(pos == a);
    }
    assert(!*p);

    state->completed = false;
    state->cheated = false;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int w = state->par.w, a = w*w;
    game_state *ret = snew(game_state);

    ret->par = state->par;	       /* structure copy */

    ret->clues = state->clues;
    ret->clues->refcount++;

    ret->grid = snewn(a, digit);
    ret->pencil = snewn(a, int);
    ret->clues_done = snewn(4*w, bool);
    memcpy(ret->grid, state->grid, a*sizeof(digit));
    memcpy(ret->pencil, state->pencil, a*sizeof(int));
    memcpy(ret->clues_done, state->clues_done, 4*w*sizeof(bool));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->pencil);
    sfree(state->clues_done);
    if (--state->clues->refcount <= 0) {
	sfree(state->clues->immutable);
	sfree(state->clues->clues);
	sfree(state->clues);
    }
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int w = state->par.w, a = w*w;
    int i, ret;
    digit *soln;
    char *out;

    if (aux)
	return dupstr(aux);

    soln = snewn(a, digit);
    memcpy(soln, state->clues->immutable, a);

    ret = solver(w, state->clues->clues, soln, DIFFCOUNT-1);

    if (ret == diff_impossible) {
	*error = "No solution exists for this puzzle";
	out = NULL;
    } else if (ret == diff_ambiguous) {
	*error = "Multiple solutions exist for this puzzle";
	out = NULL;
    } else {
	out = snewn(a+2, char);
	out[0] = 'S';
	for (i = 0; i < a; i++)
	    out[i+1] = '0' + soln[i];
	out[a+1] = '\0';
    }

    sfree(soln);
    return out;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->par.w /* , a = w*w */;
    char *ret;
    char *p;
    int x, y;
    int total;

    /*
     * We have:
     * 	- a top clue row, consisting of three spaces, then w clue
     * 	  digits with spaces between (total 2*w+3 chars including
     * 	  newline)
     *  - a blank line (one newline)
     * 	- w main rows, consisting of a left clue digit, two spaces,
     * 	  w grid digits with spaces between, two spaces and a right
     * 	  clue digit (total 2*w+6 chars each including newline)
     *  - a blank line (one newline)
     *  - a bottom clue row (same as top clue row)
     *  - terminating NUL.
     *
     * Total size is therefore 2*(2*w+3) + 2 + w*(2*w+6) + 1
     * = 2w^2+10w+9.
     */
    total = 2*w*w + 10*w + 9;
    ret = snewn(total, char);
    p = ret;

    /* Top clue row. */
    *p++ = ' '; *p++ = ' ';
    for (x = 0; x < w; x++) {
	*p++ = ' ';
	*p++ = (state->clues->clues[x] ? '0' + state->clues->clues[x] : ' ');
    }
    *p++ = '\n';

    /* Blank line. */
    *p++ = '\n';

    /* Main grid. */
    for (y = 0; y < w; y++) {
	*p++ = (state->clues->clues[y+2*w] ? '0' + state->clues->clues[y+2*w] :
		' ');
	*p++ = ' ';
	for (x = 0; x < w; x++) {
	    *p++ = ' ';
	    *p++ = (state->grid[y*w+x] ? '0' + state->grid[y*w+x] : ' ');
	}
	*p++ = ' '; *p++ = ' ';
	*p++ = (state->clues->clues[y+3*w] ? '0' + state->clues->clues[y+3*w] :
		' ');
	*p++ = '\n';
    }

    /* Blank line. */
    *p++ = '\n';

    /* Bottom clue row. */
    *p++ = ' '; *p++ = ' ';
    for (x = 0; x < w; x++) {
	*p++ = ' ';
	*p++ = (state->clues->clues[x+w] ? '0' + state->clues->clues[x+w] :
		' ');
    }
    *p++ = '\n';

    *p++ = '\0';
    assert(p == ret + total);

    return ret;
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, if hshow = 1.
     */
    int hx, hy;
    /*
     * This indicates whether the current highlight is a
     * pencil-mark one or a real one.
     */
    bool hpencil;
    /*
     * This indicates whether or not we're showing the highlight
     * (used to be hx = hy = -1); important so that when we're
     * using the cursor keys it doesn't keep coming back at a
     * fixed position. When hshow = 1, pressing a valid number
     * or letter key or Space will enter that number or letter in the grid.
     */
    bool hshow;
    /*
     * This indicates whether we're using the highlight as a cursor;
     * it means that it doesn't vanish on a keypress, and that it is
     * allowed on immutable squares.
     */
    bool hcursor;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = 0;
    ui->hpencil = false;
    ui->hshow = ui->hcursor = getenv_bool("PUZZLES_SHOW_CURSOR", false);

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
    int w = newstate->par.w;
    /*
     * We prevent pencil-mode highlighting of a filled square, unless
     * we're using the cursor keys. So if the user has just filled in
     * a square which we had a pencil-mode highlight in (by Undo, or
     * by Redo, or by Solve), then we cancel the highlight.
     */
    if (ui->hshow && ui->hpencil && !ui->hcursor &&
        newstate->grid[ui->hy * w + ui->hx] != 0) {
        ui->hshow = false;
    }
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->hshow && (button == CURSOR_SELECT))
        return ui->hpencil ? "Ink" : "Pencil";
    return "";
}

#define PREFERRED_TILESIZE 48
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE * 9 / 8)
#define COORD(x) ((x)*TILESIZE + BORDER)
#define FROMCOORD(x) (((x)+(TILESIZE-BORDER)) / TILESIZE - 1)

/* These always return positive values, though y offsets are actually -ve */
#define X_3D_DISP(height, w) ((height) * TILESIZE / (8 * (w)))
#define Y_3D_DISP(height, w) ((height) * TILESIZE / (4 * (w)))

#define FLASH_TIME 0.4F

#define DF_PENCIL_SHIFT 16
#define DF_CLUE_DONE 0x10000
#define DF_ERROR 0x8000
#define DF_HIGHLIGHT 0x4000
#define DF_HIGHLIGHT_PENCIL 0x2000
#define DF_IMMUTABLE 0x1000
#define DF_PLAYAREA 0x0800
#define DF_DIGIT_MASK 0x00FF

struct game_drawstate {
    int tilesize;
    bool three_d;       /* default 3D graphics are user-disableable */
    long *tiles;		       /* (w+2)*(w+2) temp space */
    long *drawn;		       /* (w+2)*(w+2)*4: current drawn data */
    bool *errtmp;
};

static bool check_errors(const game_state *state, bool *errors)
{
    int w = state->par.w /*, a = w*w */;
    int W = w+2, A = W*W;	       /* the errors array is (w+2) square */
    int *clues = state->clues->clues;
    digit *grid = state->grid;
    int i, x, y;
    bool errs = false;
    int tmp[32];

    assert(w < lenof(tmp));

    if (errors)
	for (i = 0; i < A; i++)
	    errors[i] = false;

    for (y = 0; y < w; y++) {
	unsigned long mask = 0, errmask = 0;
	for (x = 0; x < w; x++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1L << (w+1)) - (1L << 1)) {
	    errs = true;
	    errmask &= ~1UL;
	    if (errors) {
		for (x = 0; x < w; x++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[(y+1)*W+(x+1)] = true;
	    }
	}
    }

    for (x = 0; x < w; x++) {
	unsigned long mask = 0, errmask = 0;
	for (y = 0; y < w; y++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = true;
	    errmask &= ~1UL;
	    if (errors) {
		for (y = 0; y < w; y++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[(y+1)*W+(x+1)] = true;
	    }
	}
    }

    for (i = 0; i < 4*w; i++) {
	int start, step, j, n, best;
	STARTSTEP(start, step, i, w);

	if (!clues[i])
	    continue;

	best = n = 0;
	for (j = 0; j < w; j++) {
	    int number = grid[start+j*step];
	    if (!number)
		break;		       /* can't tell what happens next */
	    if (number > best) {
		best = number;
		n++;
	    }
	}

	if (n > clues[i] || (best == w && n < clues[i]) ||
	    (best < w && n == clues[i])) {
	    if (errors) {
		int x, y;
		CLUEPOS(x, y, i, w);
		errors[(y+1)*W+(x+1)] = true;
	    }
	    errs = true;
	}
    }

    return errs;
}

static int clue_index(const game_state *state, int x, int y)
{
    int w = state->par.w;

    if (x == -1 || x == w)
        return w * (x == -1 ? 2 : 3) + y;
    else if (y == -1 || y == w)
        return (y == -1 ? 0 : w) + x;

    return -1;
}

static bool is_clue(const game_state *state, int x, int y)
{
    int w = state->par.w;

    if (((x == -1 || x == w) && y >= 0 && y < w) ||
        ((y == -1 || y == w) && x >= 0 && x < w))
    {
        if (state->clues->clues[clue_index(state, x, y)] & DF_DIGIT_MASK)
            return true;
    }

    return false;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->par.w;
    bool shift_or_control = button & (MOD_SHFT | MOD_CTRL);
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = FROMCOORD(x);
    ty = FROMCOORD(y);

    if (ds->three_d) {
	/*
	 * In 3D mode, just locating the mouse click in the natural
	 * square grid may not be sufficient to tell which tower the
	 * user clicked on. Investigate the _tops_ of the nearby
	 * towers to see if a click on one grid square was actually
	 * a click on a tower protruding into that region from
	 * another.
	 */
	int dx, dy;
	for (dy = 0; dy <= 1; dy++)
	    for (dx = 0; dx >= -1; dx--) {
		int cx = tx + dx, cy = ty + dy;
		if (cx >= 0 && cx < w && cy >= 0 && cy < w) {
		    int height = state->grid[cy*w+cx];
		    int bx = COORD(cx), by = COORD(cy);
		    int ox = bx + X_3D_DISP(height, w);
		    int oy = by - Y_3D_DISP(height, w);
		    if (/* on top face? */
			(x - ox >= 0 && x - ox < TILESIZE &&
			 y - oy >= 0 && y - oy < TILESIZE) ||
			/* in triangle between top-left corners? */
			(ox > bx && x >= bx && x <= ox && y <= by &&
			 (by-y) * (ox-bx) <= (by-oy) * (x-bx)) ||
			/* in triangle between bottom-right corners? */
			(ox > bx && x >= bx+TILESIZE && x <= ox+TILESIZE &&
			 y >= oy+TILESIZE &&
			 (by-y+TILESIZE)*(ox-bx) >= (by-oy)*(x-bx-TILESIZE))) {
			tx = cx;
			ty = cy;
		    }
		}
	    }
    }

    if (tx >= 0 && tx < w && ty >= 0 && ty < w) {
        if (button == LEFT_BUTTON) {
	    if (tx == ui->hx && ty == ui->hy &&
		ui->hshow && !ui->hpencil) {
                ui->hshow = false;
            } else {
                ui->hx = tx;
                ui->hy = ty;
		ui->hshow = !state->clues->immutable[ty*w+tx];
                ui->hpencil = false;
            }
            ui->hcursor = false;
            return UI_UPDATE;
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*w+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy &&
                    ui->hshow && ui->hpencil) {
                    ui->hshow = false;
                } else {
                    ui->hpencil = true;
                    ui->hx = tx;
                    ui->hy = ty;
                    ui->hshow = true;
                }
            } else {
                ui->hshow = false;
            }
            ui->hcursor = false;
            return UI_UPDATE;
        }
    } else if (button == LEFT_BUTTON) {
        if (is_clue(state, tx, ty)) {
            sprintf(buf, "%c%d,%d", 'D', tx, ty);
            return dupstr(buf);
        }
    }
    if (IS_CURSOR_MOVE(button)) {
        if (shift_or_control) {
            int x = ui->hx, y = ui->hy;
            switch (button) {
            case CURSOR_LEFT:   x = -1; break;
            case CURSOR_RIGHT:  x =  w; break;
            case CURSOR_UP:     y = -1; break;
            case CURSOR_DOWN:   y =  w; break;
            }
            if (is_clue(state, x, y)) {
                sprintf(buf, "%c%d,%d", 'D', x, y);
                return dupstr(buf);
            }
            return NULL;
        }
        move_cursor(button, &ui->hx, &ui->hy, w, w, false);
        ui->hshow = true;
        ui->hcursor = true;
        return UI_UPDATE;
    }
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil = !ui->hpencil;
        ui->hcursor = true;
        return UI_UPDATE;
    }

    if (ui->hshow &&
	((button >= '0' && button <= '9' && button - '0' <= w) ||
	 button == CURSOR_SELECT2 || button == '\b')) {
	int n = button - '0';
	if (button == CURSOR_SELECT2 || button == '\b')
	    n = 0;

        /*
         * Can't make pencil marks in a filled square. This can only
         * become highlighted if we're using cursor keys.
         */
        if (ui->hpencil && state->grid[ui->hy*w+ui->hx])
            return NULL;

	/*
	 * Can't do anything to an immutable square.
	 */
        if (state->clues->immutable[ui->hy*w+ui->hx])
            return NULL;

        /*
         * If you ask to fill a square with what it already contains,
         * or blank it when it's already empty, that has no effect...
         */
        if ((!ui->hpencil || n == 0) && state->grid[ui->hy*w+ui->hx] == n &&
            state->pencil[ui->hy*w+ui->hx] == 0) {
            /* ... expect to remove the cursor in mouse mode. */
            if (!ui->hcursor) {
                ui->hshow = false;
                return UI_UPDATE;
            }
            return NULL;
        }

	sprintf(buf, "%c%d,%d,%d",
		(char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        if (!ui->hcursor) ui->hshow = false;

	return dupstr(buf);
    }

    if (button == 'M' || button == 'm')
        return dupstr("M");

    return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int w = from->par.w, a = w*w;
    game_state *ret = dup_game(from);
    int x, y, i, n;

    if (move[0] == 'S') {
	ret->completed = ret->cheated = true;

	for (i = 0; i < a; i++) {
            if (move[i+1] < '1' || move[i+1] > '0'+w)
                goto badmove;
	    ret->grid[i] = move[i+1] - '0';
	    ret->pencil[i] = 0;
	}

        if (move[a+1] != '\0')
            goto badmove;

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < w && y >= 0 && y < w && n >= 0 && n <= w) {
	if (from->clues->immutable[y*w+x])
            goto badmove;

        if (move[0] == 'P' && n > 0) {
            ret->pencil[y*w+x] ^= 1L << n;
        } else {
            ret->grid[y*w+x] = n;
            ret->pencil[y*w+x] = 0;

            if (!ret->completed && !check_errors(ret, NULL))
                ret->completed = true;
        }
	return ret;
    } else if (move[0] == 'M') {
	/*
	 * Fill in absolutely all pencil marks everywhere. (I
	 * wouldn't use this for actual play, but it's a handy
	 * starting point when following through a set of
	 * diagnostics output by the standalone solver.)
	 */
	for (i = 0; i < a; i++) {
	    if (!ret->grid[i])
		ret->pencil[i] = (1L << (w+1)) - (1L << 1);
	}
	return ret;
    } else if (move[0] == 'D' && sscanf(move+1, "%d,%d", &x, &y) == 2 &&
               is_clue(from, x, y)) {
        int index = clue_index(from, x, y);
        ret->clues_done[index] = !ret->clues_done[index];
        return ret;
    }

  badmove:
    /* couldn't parse move string */
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define SIZE(w) ((w) * TILESIZE + 2*BORDER)

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = *y = SIZE(params->w);
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

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    ret[COL_DONE * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] / 1.5F;
    ret[COL_DONE * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] / 1.5F;
    ret[COL_DONE * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] / 1.5F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->par.w /*, a = w*w */;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->three_d = !getenv_bool("TOWERS_2D", false);
    ds->tiles = snewn((w+2)*(w+2), long);
    ds->drawn = snewn((w+2)*(w+2)*4, long);
    for (i = 0; i < (w+2)*(w+2)*4; i++)
	ds->drawn[i] = -1;
    ds->errtmp = snewn((w+2)*(w+2), bool);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->errtmp);
    sfree(ds->tiles);
    sfree(ds->drawn);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, struct clues *clues,
		      int x, int y, long tile)
{
    int w = clues->w /* , a = w*w */;
    int tx, ty, bg;
    char str[64];

    tx = COORD(x);
    ty = COORD(y);

    bg = (tile & DF_HIGHLIGHT) ? COL_HIGHLIGHT : COL_BACKGROUND;

    /* draw tower */
    if (ds->three_d && (tile & DF_PLAYAREA) && (tile & DF_DIGIT_MASK)) {
	int coords[8];
	int xoff = X_3D_DISP(tile & DF_DIGIT_MASK, w);
	int yoff = Y_3D_DISP(tile & DF_DIGIT_MASK, w);

	/* left face of tower */
	coords[0] = tx;
	coords[1] = ty - 1;
	coords[2] = tx;
	coords[3] = ty + TILESIZE - 1;
	coords[4] = coords[2] + xoff;
	coords[5] = coords[3] - yoff;
	coords[6] = coords[0] + xoff;
	coords[7] = coords[1] - yoff;
	draw_polygon(dr, coords, 4, bg, COL_GRID);

	/* bottom face of tower */
	coords[0] = tx + TILESIZE;
	coords[1] = ty + TILESIZE - 1;
	coords[2] = tx;
	coords[3] = ty + TILESIZE - 1;
	coords[4] = coords[2] + xoff;
	coords[5] = coords[3] - yoff;
	coords[6] = coords[0] + xoff;
	coords[7] = coords[1] - yoff;
	draw_polygon(dr, coords, 4, bg, COL_GRID);

	/* now offset all subsequent drawing to the top of the tower */
	tx += xoff;
	ty -= yoff;
    }

    /* erase background */
    draw_rect(dr, tx, ty, TILESIZE, TILESIZE, bg);

    /* pencil-mode highlight */
    if (tile & DF_HIGHLIGHT_PENCIL) {
        int coords[6];
        coords[0] = tx;
        coords[1] = ty;
        coords[2] = tx+TILESIZE/2;
        coords[3] = ty;
        coords[4] = tx;
        coords[5] = ty+TILESIZE/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /* draw box outline */
    if (tile & DF_PLAYAREA) {
        int coords[8];
        coords[0] = tx;
        coords[1] = ty - 1;
        coords[2] = tx + TILESIZE;
        coords[3] = ty - 1;
        coords[4] = tx + TILESIZE;
        coords[5] = ty + TILESIZE - 1;
        coords[6] = tx;
        coords[7] = ty + TILESIZE - 1;
        draw_polygon(dr, coords, 4, -1, COL_GRID);
    }

    /* new number needs drawing? */
    if (tile & DF_DIGIT_MASK) {
        int color;

	str[1] = '\0';
	str[0] = (tile & DF_DIGIT_MASK) + '0';

        if (tile & DF_ERROR)
            color = COL_ERROR;
        else if (tile & DF_CLUE_DONE)
            color = COL_DONE;
        else if (x < 0 || y < 0 || x >= w || y >= w)
            color = COL_GRID;
        else if (tile & DF_IMMUTABLE)
            color = COL_GRID;
        else
            color = COL_USER;

	draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2, FONT_VARIABLE,
		  (tile & DF_PLAYAREA ? TILESIZE/2 : TILESIZE*2/5),
                  ALIGN_VCENTRE | ALIGN_HCENTRE, color, str);
    } else {
        int i, j, npencil;
	int pl, pr, pt, pb;
	float bestsize;
	int pw, ph, minph, pbest, fontsize;

        /* Count the pencil marks required. */
        for (i = 1, npencil = 0; i <= w; i++)
            if (tile & (1L << (i + DF_PENCIL_SHIFT)))
		npencil++;
	if (npencil) {

	    minph = 2;

	    /*
	     * Determine the bounding rectangle within which we're going
	     * to put the pencil marks.
	     */
	    /* Start with the whole square, minus space for impinging towers */
	    pl = tx + (ds->three_d ? X_3D_DISP(w,w) : 0);
	    pr = tx + TILESIZE;
	    pt = ty;
	    pb = ty + TILESIZE - (ds->three_d ? Y_3D_DISP(w,w) : 0);

	    /*
	     * We arrange our pencil marks in a grid layout, with
	     * the number of rows and columns adjusted to allow the
	     * maximum font size.
	     *
	     * So now we work out what the grid size ought to be.
	     */
	    bestsize = 0.0;
	    pbest = 0;
	    /* Minimum */
	    for (pw = 3; pw < max(npencil,4); pw++) {
		float fw, fh, fs;

		ph = (npencil + pw - 1) / pw;
		ph = max(ph, minph);
		fw = (pr - pl) / (float)pw;
		fh = (pb - pt) / (float)ph;
		fs = min(fw, fh);
		if (fs > bestsize) {
		    bestsize = fs;
		    pbest = pw;
		}
	    }
	    assert(pbest > 0);
	    pw = pbest;
	    ph = (npencil + pw - 1) / pw;
	    ph = max(ph, minph);

	    /*
	     * Now we've got our grid dimensions, work out the pixel
	     * size of a grid element, and round it to the nearest
	     * pixel. (We don't want rounding errors to make the
	     * grid look uneven at low pixel sizes.)
	     */
	    fontsize = min((pr - pl) / pw, (pb - pt) / ph);

	    /*
	     * Centre the resulting figure in the square.
	     */
	    pl = pl + (pr - pl - fontsize * pw) / 2;
	    pt = pt + (pb - pt - fontsize * ph) / 2;

	    /*
	     * Now actually draw the pencil marks.
	     */
	    for (i = 1, j = 0; i <= w; i++)
		if (tile & (1L << (i + DF_PENCIL_SHIFT))) {
		    int dx = j % pw, dy = j / pw;

		    str[1] = '\0';
		    str[0] = i + '0';
		    draw_text(dr, pl + fontsize * (2*dx+1) / 2,
			      pt + fontsize * (2*dy+1) / 2,
			      FONT_VARIABLE, fontsize,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
		    j++;
		}
	}
    }
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->par.w /*, a = w*w */;
    int i, x, y;

    check_errors(state, ds->errtmp);

    /*
     * Work out what data each tile should contain.
     */
    for (i = 0; i < (w+2)*(w+2); i++)
	ds->tiles[i] = 0;	       /* completely blank square */
    /* The clue squares... */
    for (i = 0; i < 4*w; i++) {
	long tile = state->clues->clues[i];

	CLUEPOS(x, y, i, w);

	if (ds->errtmp[(y+1)*(w+2)+(x+1)])
	    tile |= DF_ERROR;
        else if (state->clues_done[i])
            tile |= DF_CLUE_DONE;

	ds->tiles[(y+1)*(w+2)+(x+1)] = tile;
    }
    /* ... and the main grid. */
    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    long tile = DF_PLAYAREA;

	    if (state->grid[y*w+x])
		tile |= state->grid[y*w+x];
	    else
		tile |= (long)state->pencil[y*w+x] << DF_PENCIL_SHIFT;

	    if (ui->hshow && ui->hx == x && ui->hy == y)
		tile |= (ui->hpencil ? DF_HIGHLIGHT_PENCIL : DF_HIGHLIGHT);

	    if (state->clues->immutable[y*w+x])
		tile |= DF_IMMUTABLE;

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                tile |= DF_HIGHLIGHT;  /* completion flash */

	    if (ds->errtmp[(y+1)*(w+2)+(x+1)])
		tile |= DF_ERROR;

	    ds->tiles[(y+1)*(w+2)+(x+1)] = tile;
	}
    }

    /*
     * Now actually draw anything that needs to be changed.
     */
    for (y = 0; y < w+2; y++) {
	for (x = 0; x < w+2; x++) {
	    long tl, tr, bl, br;
	    int i = y*(w+2)+x;

	    tr = ds->tiles[y*(w+2)+x];
	    tl = (x == 0 ? 0 : ds->tiles[y*(w+2)+(x-1)]);
	    br = (y == w+1 ? 0 : ds->tiles[(y+1)*(w+2)+x]);
	    bl = (x == 0 || y == w+1 ? 0 : ds->tiles[(y+1)*(w+2)+(x-1)]);

	    if (ds->drawn[i*4] != tl || ds->drawn[i*4+1] != tr ||
		ds->drawn[i*4+2] != bl || ds->drawn[i*4+3] != br) {
		clip(dr, COORD(x-1), COORD(y-1), TILESIZE, TILESIZE);

		draw_tile(dr, ds, state->clues, x-1, y-1, tr);
		if (x > 0)
		    draw_tile(dr, ds, state->clues, x-2, y-1, tl);
		if (y <= w)
		    draw_tile(dr, ds, state->clues, x-1, y, br);
		if (x > 0 && y <= w)
		    draw_tile(dr, ds, state->clues, x-2, y, bl);

		unclip(dr);
		draw_update(dr, COORD(x-1), COORD(y-1), TILESIZE, TILESIZE);

		ds->drawn[i*4] = tl;
		ds->drawn[i*4+1] = tr;
		ds->drawn[i*4+2] = bl;
		ds->drawn[i*4+3] = br;
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
    if(ui->hshow) {
        *x = COORD(ui->hx);
        *y = COORD(ui->hy);
        *w = *h = TILESIZE;
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
     * We use 9mm squares by default, like Solo.
     */
    game_compute_size(params, 900, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->par.w;
    int ink = print_mono_colour(dr, 0);
    int i, x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, 3 * TILESIZE / 40);
    draw_rect_outline(dr, BORDER, BORDER, w*TILESIZE, w*TILESIZE, ink);

    /*
     * Main grid.
     */
    for (x = 1; x < w; x++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER+x*TILESIZE, BORDER,
		  BORDER+x*TILESIZE, BORDER+w*TILESIZE, ink);
    }
    for (y = 1; y < w; y++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER, BORDER+y*TILESIZE,
		  BORDER+w*TILESIZE, BORDER+y*TILESIZE, ink);
    }

    /*
     * Clues.
     */
    for (i = 0; i < 4*w; i++) {
	char str[128];

	if (!state->clues->clues[i])
	    continue;

	CLUEPOS(x, y, i, w);

	sprintf (str, "%d", state->clues->clues[i]);

	draw_text(dr, BORDER + x*TILESIZE + TILESIZE/2,
		  BORDER + y*TILESIZE + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2,
		  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
    }

    /*
     * Numbers for the solution, if any.
     */
    for (y = 0; y < w; y++)
	for (x = 0; x < w; x++)
	    if (state->grid[y*w+x]) {
		char str[2];
		str[1] = '\0';
		str[0] = state->grid[y*w+x] + '0';
		draw_text(dr, BORDER + x*TILESIZE + TILESIZE/2,
			  BORDER + y*TILESIZE + TILESIZE/2,
			  FONT_VARIABLE, TILESIZE/2,
			  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
	    }
}

#ifdef COMBINED
#define thegame towers
#endif

const struct game thegame = {
    "Towers", "games.towers", "towers",
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
    game_request_keys,
    game_changed_state,
    current_key_label,
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
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    REQUIRE_RBUTTON | REQUIRE_NUMPAD,  /* flags */
};

#ifdef STANDALONE_SOLVER

#include <stdarg.h>

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc;
    const char *err;
    bool grade = false;
    int ret, diff;
    bool really_show_working = false;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            really_show_working = true;
        } else if (!strcmp(p, "-g")) {
            grade = true;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
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

    /*
     * When solving an Easy puzzle, we don't want to bother the
     * user with Hard-level deductions. For this reason, we grade
     * the puzzle internally before doing anything else.
     */
    ret = -1;			       /* placate optimiser */
    solver_show_working = 0;
    for (diff = 0; diff < DIFFCOUNT; diff++) {
	memcpy(s->grid, s->clues->immutable, p->w * p->w);
	ret = solver(p->w, s->clues->clues, s->grid, diff);
	if (ret <= diff)
	    break;
    }

    if (really_show_working) {
        /*
         * Now run the solver again at the last difficulty level we
         * tried, but this time with diagnostics enabled.
         */
        solver_show_working = really_show_working;
        memcpy(s->grid, s->clues->immutable, p->w * p->w);
        ret = solver(p->w, s->clues->clues, s->grid,
                     diff < DIFFCOUNT ? diff : DIFFCOUNT-1);
    }

    if (diff == DIFFCOUNT) {
	if (grade)
	    printf("Difficulty rating: ambiguous\n");
	else
	    printf("Unable to find a unique solution\n");
    } else {
	if (grade) {
	    if (ret == diff_impossible)
		printf("Difficulty rating: impossible (no solution exists)\n");
	    else
		printf("Difficulty rating: %s\n", towers_diffnames[ret]);
	} else {
	    if (ret != diff)
		printf("Puzzle is inconsistent\n");
	    else
		fputs(game_text_format(s), stdout);
	}
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
