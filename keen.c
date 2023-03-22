/*
 * keen.c: an implementation of the Times's 'KenKen' puzzle, and
 * also of Nikoli's very similar 'Inshi No Heya' puzzle.
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
    A(NORMAL,Normal,solver_normal,n) \
    A(HARD,Hard,solver_hard,h) \
    A(EXTREME,Extreme,NULL,x) \
    A(UNREASONABLE,Unreasonable,NULL,u)
#define ENUM(upper,title,func,lower) DIFF_ ## upper,
#define TITLE(upper,title,func,lower) #title,
#define ENCODE(upper,title,func,lower) #lower
#define CONFIG(upper,title,func,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const keen_diffnames[] = { DIFFLIST(TITLE) };
static char const keen_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

/*
 * Clue notation. Important here that ADD and MUL come before SUB
 * and DIV, and that DIV comes last. 
 */
#define C_ADD 0x00000000L
#define C_MUL 0x20000000L
#define C_SUB 0x40000000L
#define C_DIV 0x60000000L
#define CMASK 0x60000000L
#define CUNIT 0x20000000L

/*
 * Maximum size of any clue block. Very large ones are annoying in UI
 * terms (if they're multiplicative you end up with too many digits to
 * fit in the square) and also in solver terms (too many possibilities
 * to iterate over).
 */
#define MAXBLK 6

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    NCOLOURS
};

struct game_params {
    int w, diff;
    bool multiplication_only;
};

struct clues {
    int refcount;
    int w;
    int *dsf;
    long *clues;
};

struct game_state {
    game_params par;
    struct clues *clues;
    digit *grid;
    int *pencil;		       /* bitmaps using bits 1<<1..1<<n */
    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 6;
    ret->diff = DIFF_NORMAL;
    ret->multiplication_only = false;

    return ret;
}

static const struct game_params keen_presets[] = {
    {  4, DIFF_EASY,         false },
    {  5, DIFF_EASY,         false },
    {  5, DIFF_EASY,         true  },
    {  6, DIFF_EASY,         false },
    {  6, DIFF_NORMAL,       false },
    {  6, DIFF_NORMAL,       true  },
    {  6, DIFF_HARD,         false },
    {  6, DIFF_EXTREME,      false },
    {  6, DIFF_UNREASONABLE, false },
    {  9, DIFF_NORMAL,       false },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(keen_presets))
        return false;

    ret = snew(game_params);
    *ret = keen_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s%s", ret->w, ret->w, keen_diffnames[ret->diff],
	    ret->multiplication_only ? ", multiplication only" : "");

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
                if (*p == keen_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }

    if (*p == 'm') {
	p++;
	params->multiplication_only = true;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char ret[80];

    sprintf(ret, "%d", params->w);
    if (full)
        sprintf(ret + strlen(ret), "d%c%s", keen_diffchars[params->diff],
		params->multiplication_only ? "m" : "");

    return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Grid size";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].u.choices.choicenames = DIFFCONFIG;
    ret[1].u.choices.selected = params->diff;

    ret[2].name = "Multiplication only";
    ret[2].type = C_BOOLEAN;
    ret[2].u.boolean.bval = params->multiplication_only;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->diff = cfg[1].u.choices.selected;
    ret->multiplication_only = cfg[2].u.boolean.bval;

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
    int nboxes;
    int *boxes, *boxlist, *whichbox;
    long *clues;
    digit *soln;
    digit *dscratch;
    int *iscratch;
};

static void solver_clue_candidate(struct solver_ctx *ctx, int diff, int box)
{
    int w = ctx->w;
    int n = ctx->boxes[box+1] - ctx->boxes[box];
    int j;

    /*
     * This function is called from the main clue-based solver
     * routine when we discover a candidate layout for a given clue
     * box consistent with everything we currently know about the
     * digit constraints in that box. We expect to find the digits
     * of the candidate layout in ctx->dscratch, and we update
     * ctx->iscratch as appropriate.
     *
     * The contents of ctx->iscratch are completely different
     * depending on whether diff == DIFF_HARD or not. This function
     * uses iscratch completely differently between the two cases, and
     * the code in solver_common() which consumes the result must
     * likewise have an if statement with completely different
     * branches for the two cases.
     *
     * In DIFF_EASY and DIFF_NORMAL modes, the valid entries in
     * ctx->iscratch are 0,...,n-1, and each of those entries
     * ctx->iscratch[i] gives a bitmap of the possible digits in the
     * ith square of the clue box currently under consideration. So
     * each entry of iscratch starts off as an empty bitmap, and we
     * set bits in it as possible layouts for the clue box are
     * considered (and the difference between DIFF_EASY and
     * DIFF_NORMAL is just that in DIFF_EASY mode we deliberately set
     * more bits than absolutely necessary, hence restricting our own
     * knowledge).
     *
     * But in DIFF_HARD mode, the valid entries are 0,...,2*w-1 (at
     * least outside *this* function - inside this function, we also
     * use 2*w,...,4*w-1 as scratch space in the loop below); the
     * first w of those give the possible digits in the intersection
     * of the current clue box with each column of the puzzle, and the
     * next w do the same for each row. In this mode, each iscratch
     * entry starts off as a _full_ bitmap, and in this function we
     * _clear_ bits for digits that are absent from a given row or
     * column in each candidate layout, so that the only bits which
     * remain set are those for digits which have to appear in a given
     * row/column no matter how the clue box is laid out.
     */
    if (diff == DIFF_EASY) {
	unsigned mask = 0;
	/*
	 * Easy-mode clue deductions: we do not record information
	 * about which squares take which values, so we amalgamate
	 * all the values in dscratch and OR them all into
	 * everywhere.
	 */
	for (j = 0; j < n; j++)
	    mask |= 1 << ctx->dscratch[j];
	for (j = 0; j < n; j++)
	    ctx->iscratch[j] |= mask;
    } else if (diff == DIFF_NORMAL) {
	/*
	 * Normal-mode deductions: we process the information in
	 * dscratch in the obvious way.
	 */
	for (j = 0; j < n; j++)
	    ctx->iscratch[j] |= 1 << ctx->dscratch[j];
    } else if (diff == DIFF_HARD) {
	/*
	 * Hard-mode deductions: instead of ruling things out
	 * _inside_ the clue box, we look for numbers which occur in
	 * a given row or column in all candidate layouts, and rule
	 * them out of all squares in that row or column that
	 * _aren't_ part of this clue box.
	 */
	int *sq = ctx->boxlist + ctx->boxes[box];

	for (j = 0; j < 2*w; j++)
	    ctx->iscratch[2*w+j] = 0;
	for (j = 0; j < n; j++) {
	    int x = sq[j] / w, y = sq[j] % w;
	    ctx->iscratch[2*w+x] |= 1 << ctx->dscratch[j];
	    ctx->iscratch[3*w+y] |= 1 << ctx->dscratch[j];
	}
	for (j = 0; j < 2*w; j++)
	    ctx->iscratch[j] &= ctx->iscratch[2*w+j];
    }
}

static int solver_common(struct latin_solver *solver, void *vctx, int diff)
{
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    int w = ctx->w;
    int box, i, j, k;
    int ret = 0, total;

    /*
     * Iterate over each clue box and deduce what we can.
     */
    for (box = 0; box < ctx->nboxes; box++) {
	int *sq = ctx->boxlist + ctx->boxes[box];
	int n = ctx->boxes[box+1] - ctx->boxes[box];
	long value = ctx->clues[box] & ~CMASK;
	long op = ctx->clues[box] & CMASK;

        /*
         * Initialise ctx->iscratch for this clue box. At different
         * difficulty levels we must initialise a different amount of
         * it to different things; see the comments in
         * solver_clue_candidate explaining what each version does.
         */
	if (diff == DIFF_HARD) {
	    for (i = 0; i < 2*w; i++)
		ctx->iscratch[i] = (1 << (w+1)) - (1 << 1);
	} else {
	    for (i = 0; i < n; i++)
		ctx->iscratch[i] = 0;
	}

	switch (op) {
	  case C_SUB:
	  case C_DIV:
	    /*
	     * These two clue types must always apply to a box of
	     * area 2. Also, the two digits in these boxes can never
	     * be the same (because any domino must have its two
	     * squares in either the same row or the same column).
	     * So we simply iterate over all possibilities for the
	     * two squares (both ways round), rule out any which are
	     * inconsistent with the digit constraints we already
	     * have, and update the digit constraints with any new
	     * information thus garnered.
	     */
	    assert(n == 2);

	    for (i = 1; i <= w; i++) {
		j = (op == C_SUB ? i + value : i * value);
		if (j > w) break;

		/* (i,j) is a valid digit pair. Try it both ways round. */

		if (solver->cube[sq[0]*w+i-1] &&
		    solver->cube[sq[1]*w+j-1]) {
		    ctx->dscratch[0] = i;
		    ctx->dscratch[1] = j;
		    solver_clue_candidate(ctx, diff, box);
		}

		if (solver->cube[sq[0]*w+j-1] &&
		    solver->cube[sq[1]*w+i-1]) {
		    ctx->dscratch[0] = j;
		    ctx->dscratch[1] = i;
		    solver_clue_candidate(ctx, diff, box);
		}
	    }

	    break;

	  case C_ADD:
	  case C_MUL:
	    /*
	     * For these clue types, I have no alternative but to go
	     * through all possible number combinations.
	     *
	     * Instead of a tedious physical recursion, I iterate in
	     * the scratch array through all possibilities. At any
	     * given moment, i indexes the element of the box that
	     * will next be incremented.
	     */
	    i = 0;
	    ctx->dscratch[i] = 0;
	    total = value;	       /* start with the identity */
	    while (1) {
		if (i < n) {
		    /*
		     * Find the next valid value for cell i.
		     */
		    for (j = ctx->dscratch[i] + 1; j <= w; j++) {
			if (op == C_ADD ? (total < j) : (total % j != 0))
			    continue;  /* this one won't fit */
			if (!solver->cube[sq[i]*w+j-1])
			    continue;  /* this one is ruled out already */
			for (k = 0; k < i; k++)
			    if (ctx->dscratch[k] == j &&
				(sq[k] % w == sq[i] % w ||
				 sq[k] / w == sq[i] / w))
				break; /* clashes with another row/col */
			if (k < i)
			    continue;

			/* Found one. */
			break;
		    }

		    if (j > w) {
			/* No valid values left; drop back. */
			i--;
			if (i < 0)
			    break;     /* overall iteration is finished */
			if (op == C_ADD)
			    total += ctx->dscratch[i];
			else
			    total *= ctx->dscratch[i];
		    } else {
			/* Got a valid value; store it and move on. */
			ctx->dscratch[i++] = j;
			if (op == C_ADD)
			    total -= j;
			else
			    total /= j;
			ctx->dscratch[i] = 0;
		    }
		} else {
		    if (total == (op == C_ADD ? 0 : 1))
			solver_clue_candidate(ctx, diff, box);
		    i--;
		    if (op == C_ADD)
			total += ctx->dscratch[i];
		    else
			total *= ctx->dscratch[i];
		}
	    }

	    break;
	}

        /*
         * Do deductions based on the information we've now
         * accumulated in ctx->iscratch. See the comments above in
         * solver_clue_candidate explaining what data is left in here,
         * and how it differs between DIFF_HARD and lower difficulty
         * levels (hence the big if statement here).
         */
	if (diff < DIFF_HARD) {
#ifdef STANDALONE_SOLVER
	    char prefix[256];

	    if (solver_show_working)
		sprintf(prefix, "%*susing clue at (%d,%d):\n",
			solver_recurse_depth*4, "",
			sq[0]/w+1, sq[0]%w+1);
	    else
		prefix[0] = '\0';	       /* placate optimiser */
#endif

	    for (i = 0; i < n; i++)
		for (j = 1; j <= w; j++) {
		    if (solver->cube[sq[i]*w+j-1] &&
			!(ctx->iscratch[i] & (1 << j))) {
#ifdef STANDALONE_SOLVER
			if (solver_show_working) {
			    printf("%s%*s  ruling out %d at (%d,%d)\n",
				   prefix, solver_recurse_depth*4, "",
				   j, sq[i]/w+1, sq[i]%w+1);
			    prefix[0] = '\0';
			}
#endif
			solver->cube[sq[i]*w+j-1] = 0;
			ret = 1;
		    }
		}
	} else {
#ifdef STANDALONE_SOLVER
	    char prefix[256];

	    if (solver_show_working)
		sprintf(prefix, "%*susing clue at (%d,%d):\n",
			solver_recurse_depth*4, "",
			sq[0]/w+1, sq[0]%w+1);
	    else
		prefix[0] = '\0';	       /* placate optimiser */
#endif

	    for (i = 0; i < 2*w; i++) {
		int start = (i < w ? i*w : i-w);
		int step = (i < w ? 1 : w);
		for (j = 1; j <= w; j++) if (ctx->iscratch[i] & (1 << j)) {
#ifdef STANDALONE_SOLVER
		    char prefix2[256];

		    if (solver_show_working)
			sprintf(prefix2, "%*s  this clue requires %d in"
				" %s %d:\n", solver_recurse_depth*4, "",
				j, i < w ? "column" : "row", i%w+1);
		    else
			prefix2[0] = '\0';   /* placate optimiser */
#endif

		    for (k = 0; k < w; k++) {
			int pos = start + k*step;
			if (ctx->whichbox[pos] != box &&
			    solver->cube[pos*w+j-1]) {
#ifdef STANDALONE_SOLVER
			    if (solver_show_working) {
				printf("%s%s%*s   ruling out %d at (%d,%d)\n",
				       prefix, prefix2,
				       solver_recurse_depth*4, "",
				       j, pos/w+1, pos%w+1);
				prefix[0] = prefix2[0] = '\0';
			    }
#endif
			    solver->cube[pos*w+j-1] = 0;
			    ret = 1;
			}
		    }
		}
	    }

	    /*
	     * Once we find one block we can do something with in
	     * this way, revert to trying easier deductions, so as
	     * not to generate solver diagnostics that make the
	     * problem look harder than it is. (We have to do this
	     * for the Hard deductions but not the Easy/Normal ones,
	     * because only the Hard deductions are cross-box.)
	     */
	    if (ret)
		return ret;
	}
    }

    return ret;
}

static int solver_easy(struct latin_solver *solver, void *vctx)
{
    /*
     * Omit the EASY deductions when solving at NORMAL level, since
     * the NORMAL deductions are a superset of them anyway and it
     * saves on time and confusing solver diagnostics.
     *
     * Note that this breaks the natural semantics of the return
     * value of latin_solver. Without this hack, you could determine
     * a puzzle's difficulty in one go by trying to solve it at
     * maximum difficulty and seeing what difficulty value was
     * returned; but with this hack, solving an Easy puzzle on
     * Normal difficulty will typically return Normal. Hence the
     * uses of the solver to determine difficulty are all arranged
     * so as to double-check by re-solving at the next difficulty
     * level down and making sure it failed.
     */
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    if (ctx->diff > DIFF_EASY)
	return 0;
    return solver_common(solver, vctx, DIFF_EASY);
}

static int solver_normal(struct latin_solver *solver, void *vctx)
{
    return solver_common(solver, vctx, DIFF_NORMAL);
}

static int solver_hard(struct latin_solver *solver, void *vctx)
{
    return solver_common(solver, vctx, DIFF_HARD);
}

#define SOLVER(upper,title,func,lower) func,
static usersolver_t const keen_solvers[] = { DIFFLIST(SOLVER) };

static int transpose(int index, int w)
{
    return (index % w) * w + (index / w);
}

static bool keen_valid(struct latin_solver *solver, void *vctx)
{
    struct solver_ctx *ctx = (struct solver_ctx *)vctx;
    int w = ctx->w;
    int box, i;

    /*
     * Iterate over each clue box and check it's satisfied.
     */
    for (box = 0; box < ctx->nboxes; box++) {
	int *sq = ctx->boxlist + ctx->boxes[box];
	int n = ctx->boxes[box+1] - ctx->boxes[box];
	long value = ctx->clues[box] & ~CMASK;
	long op = ctx->clues[box] & CMASK;
        bool fail = false;

        switch (op) {
          case C_ADD: {
            long sum = 0;
            for (i = 0; i < n; i++)
                sum += solver->grid[transpose(sq[i], w)];
            fail = (sum != value);
            break;
          }

          case C_MUL: {
            long remaining = value;
            for (i = 0; i < n; i++) {
                if (remaining % solver->grid[transpose(sq[i], w)]) {
                    fail = true;
                    break;
                }
                remaining /= solver->grid[transpose(sq[i], w)];
            }
            if (remaining != 1)
                fail = true;
            break;
          }

          case C_SUB:
            assert(n == 2);
            if (value != labs(solver->grid[transpose(sq[0], w)] -
                              solver->grid[transpose(sq[1], w)]))
                fail = true;
            break;

          case C_DIV: {
            int num, den;
            assert(n == 2);
            num = max(solver->grid[transpose(sq[0], w)],
                      solver->grid[transpose(sq[1], w)]);
            den = min(solver->grid[transpose(sq[0], w)],
                      solver->grid[transpose(sq[1], w)]);
            if (den * value != num)
                fail = true;
            break;
          }
        }

        if (fail) {
#ifdef STANDALONE_SOLVER
	    if (solver_show_working) {
		printf("%*sclue at (%d,%d) is violated\n",
                       solver_recurse_depth*4, "",
                       sq[0]/w+1, sq[0]%w+1);
		printf("%*s  (%s clue with target %ld containing [",
                       solver_recurse_depth*4, "",
                       (op == C_ADD ? "addition" : op == C_SUB ? "subtraction":
                        op == C_MUL ? "multiplication" : "division"), value);
                for (i = 0; i < n; i++)
                    printf(" %d", (int)solver->grid[transpose(sq[i], w)]);
                printf(" ]\n");
            }
#endif
            return false;
        }
    }

    return true;
}

static int solver(int w, int *dsf, long *clues, digit *soln, int maxdiff)
{
    int a = w*w;
    struct solver_ctx ctx;
    int ret;
    int i, j, n, m;
    
    ctx.w = w;
    ctx.soln = soln;
    ctx.diff = maxdiff;

    /*
     * Transform the dsf-formatted clue list into one over which we
     * can iterate more easily.
     *
     * Also transpose the x- and y-coordinates at this point,
     * because the 'cube' array in the general Latin square solver
     * puts x first (oops).
     */
    for (ctx.nboxes = i = 0; i < a; i++)
	if (dsf_canonify(dsf, i) == i)
	    ctx.nboxes++;
    ctx.boxlist = snewn(a, int);
    ctx.boxes = snewn(ctx.nboxes+1, int);
    ctx.clues = snewn(ctx.nboxes, long);
    ctx.whichbox = snewn(a, int);
    for (n = m = i = 0; i < a; i++)
	if (dsf_canonify(dsf, i) == i) {
	    ctx.clues[n] = clues[i];
	    ctx.boxes[n] = m;
	    for (j = 0; j < a; j++)
		if (dsf_canonify(dsf, j) == i) {
		    ctx.boxlist[m++] = (j % w) * w + (j / w);   /* transpose */
		    ctx.whichbox[ctx.boxlist[m-1]] = n;
		}
	    n++;
	}
    assert(n == ctx.nboxes);
    assert(m == a);
    ctx.boxes[n] = m;

    ctx.dscratch = snewn(a+1, digit);
    ctx.iscratch = snewn(max(a+1, 4*w), int);

    ret = latin_solver(soln, w, maxdiff,
		       DIFF_EASY, DIFF_HARD, DIFF_EXTREME,
		       DIFF_EXTREME, DIFF_UNREASONABLE,
		       keen_solvers, keen_valid, &ctx, NULL, NULL);

    sfree(ctx.dscratch);
    sfree(ctx.iscratch);
    sfree(ctx.whichbox);
    sfree(ctx.boxlist);
    sfree(ctx.boxes);
    sfree(ctx.clues);

    return ret;
}

/* ----------------------------------------------------------------------
 * Grid generation.
 */

static char *encode_block_structure(char *p, int w, int *dsf)
{
    int i, currrun = 0;
    char *orig, *q, *r, c;

    orig = p;

    /*
     * Encode the block structure. We do this by encoding the
     * pattern of dividing lines: first we iterate over the w*(w-1)
     * internal vertical grid lines in ordinary reading order, then
     * over the w*(w-1) internal horizontal ones in transposed
     * reading order.
     *
     * We encode the number of non-lines between the lines; _ means
     * zero (two adjacent divisions), a means 1, ..., y means 25,
     * and z means 25 non-lines _and no following line_ (so that za
     * means 26, zb 27 etc).
     */
    for (i = 0; i <= 2*w*(w-1); i++) {
	int x, y, p0, p1;
        bool edge;

	if (i == 2*w*(w-1)) {
	    edge = true;       /* terminating virtual edge */
	} else {
	    if (i < w*(w-1)) {
		y = i/(w-1);
		x = i%(w-1);
		p0 = y*w+x;
		p1 = y*w+x+1;
	    } else {
		x = i/(w-1) - w;
		y = i%(w-1);
		p0 = y*w+x;
		p1 = (y+1)*w+x;
	    }
	    edge = (dsf_canonify(dsf, p0) != dsf_canonify(dsf, p1));
	}

	if (edge) {
	    while (currrun > 25)
		*p++ = 'z', currrun -= 25;
	    if (currrun)
		*p++ = 'a'-1 + currrun;
	    else
		*p++ = '_';
	    currrun = 0;
	} else
	    currrun++;
    }

    /*
     * Now go through and compress the string by replacing runs of
     * the same letter with a single copy of that letter followed by
     * a repeat count, where that makes it shorter. (This puzzle
     * seems to generate enough long strings of _ to make this a
     * worthwhile step.)
     */
    for (q = r = orig; r < p ;) {
	*q++ = c = *r;

	for (i = 0; r+i < p && r[i] == c; i++);
	r += i;

	if (i == 2) {
	    *q++ = c;
	} else if (i > 2) {
	    q += sprintf(q, "%d", i);
	}
    }
    
    return q;
}

static const char *parse_block_structure(const char **p, int w, int *dsf)
{
    int a = w*w;
    int pos = 0;
    int repc = 0, repn = 0;

    dsf_init(dsf, a);

    while (**p && (repn > 0 || **p != ',')) {
	int c;
        bool adv;

	if (repn > 0) {
	    repn--;
	    c = repc;
	} else if (**p == '_' || (**p >= 'a' && **p <= 'z')) {
	    c = (**p == '_' ? 0 : **p - 'a' + 1);
	    (*p)++;
	    if (**p && isdigit((unsigned char)**p)) {
		repc = c;
		repn = atoi(*p)-1;
		while (**p && isdigit((unsigned char)**p)) (*p)++;
	    }
	} else
	    return "Invalid character in game description";

	adv = (c != 25);	       /* 'z' is a special case */

	while (c-- > 0) {
	    int p0, p1;

	    /*
	     * Non-edge; merge the two dsf classes on either
	     * side of it.
	     */
	    if (pos >= 2*w*(w-1))
		return "Too much data in block structure specification";
	    if (pos < w*(w-1)) {
		int y = pos/(w-1);
		int x = pos%(w-1);
		p0 = y*w+x;
		p1 = y*w+x+1;
	    } else {
		int x = pos/(w-1) - w;
		int y = pos%(w-1);
		p0 = y*w+x;
		p1 = (y+1)*w+x;
	    }
	    dsf_merge(dsf, p0, p1);

	    pos++;
	}
	if (adv) {
	    pos++;
	    if (pos > 2*w*(w-1)+1)
		return "Too much data in block structure specification";
	}
    }

    /*
     * When desc is exhausted, we expect to have gone exactly
     * one space _past_ the end of the grid, due to the dummy
     * edge at the end.
     */
    if (pos != 2*w*(w-1)+1)
	return "Not enough data in block structure specification";

    return NULL;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, a = w*w;
    digit *grid, *soln;
    int *order, *revorder, *singletons, *dsf;
    long *clues, *cluevals;
    int i, j, k, n, x, y, ret;
    int diff = params->diff;
    char *desc, *p;

    /*
     * Difficulty exceptions: 3x3 puzzles at difficulty Hard or
     * higher are currently not generable - the generator will spin
     * forever looking for puzzles of the appropriate difficulty. We
     * dial each of these down to the next lower difficulty.
     *
     * Remember to re-test this whenever a change is made to the
     * solver logic!
     *
     * I tested it using the following shell command:

for d in e n h x u; do
  for i in {3..9}; do
    echo ./keen --generate 1 ${i}d${d}
    perl -e 'alarm 30; exec @ARGV' ./keen --generate 5 ${i}d${d} >/dev/null \
      || echo broken
  done
done

     * Of course, it's better to do that after taking the exceptions
     * _out_, so as to detect exceptions that should be removed as
     * well as those which should be added.
     */
    if (w == 3 && diff > DIFF_NORMAL)
	diff = DIFF_NORMAL;

    grid = NULL;

    order = snewn(a, int);
    revorder = snewn(a, int);
    singletons = snewn(a, int);
    dsf = snew_dsf(a);
    clues = snewn(a, long);
    cluevals = snewn(a, long);
    soln = snewn(a, digit);

    while (1) {
	/*
	 * First construct a latin square to be the solution.
	 */
	sfree(grid);
	grid = latin_generate(w, rs);

	/*
	 * Divide the grid into arbitrarily sized blocks, but so as
	 * to arrange plenty of dominoes which can be SUB/DIV clues.
	 * We do this by first placing dominoes at random for a
	 * while, then tying the remaining singletons one by one
	 * into neighbouring blocks.
	 */
	for (i = 0; i < a; i++)
	    order[i] = i;
	shuffle(order, a, sizeof(*order), rs);
	for (i = 0; i < a; i++)
	    revorder[order[i]] = i;

	for (i = 0; i < a; i++)
	    singletons[i] = true;

	dsf_init(dsf, a);

	/* Place dominoes. */
	for (i = 0; i < a; i++) {
	    if (singletons[i]) {
		int best = -1;

		x = i % w;
		y = i / w;

		if (x > 0 && singletons[i-1] &&
		    (best == -1 || revorder[i-1] < revorder[best]))
		    best = i-1;
		if (x+1 < w && singletons[i+1] &&
		    (best == -1 || revorder[i+1] < revorder[best]))
		    best = i+1;
		if (y > 0 && singletons[i-w] &&
		    (best == -1 || revorder[i-w] < revorder[best]))
		    best = i-w;
		if (y+1 < w && singletons[i+w] &&
		    (best == -1 || revorder[i+w] < revorder[best]))
		    best = i+w;

		/*
		 * When we find a potential domino, we place it with
		 * probability 3/4, which seems to strike a decent
		 * balance between plenty of dominoes and leaving
		 * enough singletons to make interesting larger
		 * shapes.
		 */
		if (best >= 0 && random_upto(rs, 4)) {
		    singletons[i] = singletons[best] = false;
		    dsf_merge(dsf, i, best);
		}
	    }
	}

	/* Fold in singletons. */
	for (i = 0; i < a; i++) {
	    if (singletons[i]) {
		int best = -1;

		x = i % w;
		y = i / w;

		if (x > 0 && dsf_size(dsf, i-1) < MAXBLK &&
		    (best == -1 || revorder[i-1] < revorder[best]))
		    best = i-1;
		if (x+1 < w && dsf_size(dsf, i+1) < MAXBLK &&
		    (best == -1 || revorder[i+1] < revorder[best]))
		    best = i+1;
		if (y > 0 && dsf_size(dsf, i-w) < MAXBLK &&
		    (best == -1 || revorder[i-w] < revorder[best]))
		    best = i-w;
		if (y+1 < w && dsf_size(dsf, i+w) < MAXBLK &&
		    (best == -1 || revorder[i+w] < revorder[best]))
		    best = i+w;

		if (best >= 0) {
		    singletons[i] = singletons[best] = false;
		    dsf_merge(dsf, i, best);
		}
	    }
	}

        /* Quit and start again if we have any singletons left over
         * which we weren't able to do anything at all with. */
	for (i = 0; i < a; i++)
	    if (singletons[i])
                break;
        if (i < a)
            continue;

	/*
	 * Decide what would be acceptable clues for each block.
	 *
	 * Blocks larger than 2 have free choice of ADD or MUL;
	 * blocks of size 2 can be anything in principle (except
	 * that they can only be DIV if the two numbers have an
	 * integer quotient, of course), but we rule out (or try to
	 * avoid) some clues because they're of low quality.
	 *
	 * Hence, we iterate once over the grid, stopping at the
	 * canonical element of every >2 block and the _non_-
	 * canonical element of every 2-block; the latter means that
	 * we can make our decision about a 2-block in the knowledge
	 * of both numbers in it.
	 *
	 * We reuse the 'singletons' array (finished with in the
	 * above loop) to hold information about which blocks are
	 * suitable for what.
	 */
#define F_ADD     0x01
#define F_SUB     0x02
#define F_MUL     0x04
#define F_DIV     0x08
#define BAD_SHIFT 4

	for (i = 0; i < a; i++) {
	    singletons[i] = 0;
	    j = dsf_canonify(dsf, i);
	    k = dsf_size(dsf, j);
	    if (params->multiplication_only)
		singletons[j] = F_MUL;
	    else if (j == i && k > 2) {
		singletons[j] |= F_ADD | F_MUL;
	    } else if (j != i && k == 2) {
		/* Fetch the two numbers and sort them into order. */
		int p = grid[j], q = grid[i], v;
		if (p < q) {
		    int t = p; p = q; q = t;
		}

		/*
		 * Addition clues are always allowed, but we try to
		 * avoid sums of 3, 4, (2w-1) and (2w-2) if we can,
		 * because they're too easy - they only leave one
		 * option for the pair of numbers involved.
		 */
		v = p + q;
		if (v > 4 && v < 2*w-2)
		    singletons[j] |= F_ADD;
                else
		    singletons[j] |= F_ADD << BAD_SHIFT;

		/*
		 * Multiplication clues: above Normal difficulty, we
		 * prefer (but don't absolutely insist on) clues of
		 * this type which leave multiple options open.
		 */
		v = p * q;
		n = 0;
		for (k = 1; k <= w; k++)
		    if (v % k == 0 && v / k <= w && v / k != k)
			n++;
		if (n <= 2 && diff > DIFF_NORMAL)
		    singletons[j] |= F_MUL << BAD_SHIFT;
                else
		    singletons[j] |= F_MUL;

		/*
		 * Subtraction: we completely avoid a difference of
		 * w-1.
		 */
		v = p - q;
		if (v < w-1)
		    singletons[j] |= F_SUB;

		/*
		 * Division: for a start, the quotient must be an
		 * integer or the clue type is impossible. Also, we
		 * never use quotients strictly greater than w/2,
		 * because they're not only too easy but also
		 * inelegant.
		 */
		if (p % q == 0 && 2 * (p / q) <= w)
		    singletons[j] |= F_DIV;
	    }
	}

	/*
	 * Actually choose a clue for each block, trying to keep the
	 * numbers of each type even, and starting with the
	 * preferred candidates for each type where possible.
	 *
	 * I'm sure there should be a faster algorithm for doing
	 * this, but I can't be bothered: O(N^2) is good enough when
	 * N is at most the number of dominoes that fits into a 9x9
	 * square.
	 */
	shuffle(order, a, sizeof(*order), rs);
	for (i = 0; i < a; i++)
	    clues[i] = 0;
	while (1) {
	    bool done_something = false;

	    for (k = 0; k < 4; k++) {
		long clue;
		int good, bad;
		switch (k) {
		  case 0:                clue = C_DIV; good = F_DIV; break;
		  case 1:                clue = C_SUB; good = F_SUB; break;
		  case 2:                clue = C_MUL; good = F_MUL; break;
		  default /* case 3 */ : clue = C_ADD; good = F_ADD; break;
		}

		for (i = 0; i < a; i++) {
		    j = order[i];
		    if (singletons[j] & good) {
			clues[j] = clue;
			singletons[j] = 0;
			break;
		    }
		}
		if (i == a) {
		    /* didn't find a nice one, use a nasty one */
                    bad = good << BAD_SHIFT;
		    for (i = 0; i < a; i++) {
			j = order[i];
			if (singletons[j] & bad) {
			    clues[j] = clue;
			    singletons[j] = 0;
			    break;
			}
		    }
		}
		if (i < a)
		    done_something = true;
	    }

	    if (!done_something)
		break;
	}
#undef F_ADD
#undef F_SUB
#undef F_MUL
#undef F_DIV
#undef BAD_SHIFT

	/*
	 * Having chosen the clue types, calculate the clue values.
	 */
	for (i = 0; i < a; i++) {
	    j = dsf_canonify(dsf, i);
	    if (j == i) {
		cluevals[j] = grid[i];
	    } else {
		switch (clues[j]) {
		  case C_ADD:
		    cluevals[j] += grid[i];
		    break;
		  case C_MUL:
		    cluevals[j] *= grid[i];
		    break;
		  case C_SUB:
		    cluevals[j] = labs(cluevals[j] - grid[i]);
		    break;
		  case C_DIV:
		    {
			int d1 = cluevals[j], d2 = grid[i];
			if (d1 == 0 || d2 == 0)
			    cluevals[j] = 0;
			else
			    cluevals[j] = d2/d1 + d1/d2;/* one is 0 :-) */
		    }
		    break;
		}
	    }
	}

	for (i = 0; i < a; i++) {
	    j = dsf_canonify(dsf, i);
	    if (j == i) {
		clues[j] |= cluevals[j];
	    }
	}

	/*
	 * See if the game can be solved at the specified difficulty
	 * level, but not at the one below.
	 */
	if (diff > 0) {
	    memset(soln, 0, a);
	    ret = solver(w, dsf, clues, soln, diff-1);
	    if (ret <= diff-1)
		continue;
	}
	memset(soln, 0, a);
	ret = solver(w, dsf, clues, soln, diff);
	if (ret != diff)
	    continue;		       /* go round again */

	/*
	 * I wondered if at this point it would be worth trying to
	 * merge adjacent blocks together, to make the puzzle
	 * gradually more difficult if it's currently easier than
	 * specced, increasing the chance of a given generation run
	 * being successful.
	 *
	 * It doesn't seem to be critical for the generation speed,
	 * though, so for the moment I'm leaving it out.
	 */

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
    p = encode_block_structure(p, w, dsf);
    *p++ = ',';
    for (i = 0; i < a; i++) {
	j = dsf_canonify(dsf, i);
	if (j == i) {
	    switch (clues[j] & CMASK) {
	      case C_ADD: *p++ = 'a'; break;
	      case C_SUB: *p++ = 's'; break;
	      case C_MUL: *p++ = 'm'; break;
	      case C_DIV: *p++ = 'd'; break;
	    }
	    p += sprintf(p, "%ld", clues[j] & ~CMASK);
	}
    }
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    /*
     * Encode the solution.
     */
    assert(memcmp(soln, grid, a) == 0);
    *aux = snewn(a+2, char);
    (*aux)[0] = 'S';
    for (i = 0; i < a; i++)
	(*aux)[i+1] = '0' + soln[i];
    (*aux)[a+1] = '\0';

    sfree(grid);
    sfree(order);
    sfree(revorder);
    sfree(singletons);
    sfree(dsf);
    sfree(clues);
    sfree(cluevals);
    sfree(soln);

    return desc;
}

/* ----------------------------------------------------------------------
 * Gameplay.
 */

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, a = w*w;
    int *dsf;
    const char *ret;
    const char *p = desc;
    int i;

    /*
     * Verify that the block structure makes sense.
     */
    dsf = snew_dsf(a);
    ret = parse_block_structure(&p, w, dsf);
    if (ret) {
	sfree(dsf);
	return ret;
    }

    if (*p != ',') {
        sfree(dsf);
	return "Expected ',' after block structure description";
    }
    p++;

    /*
     * Verify that the right number of clues are given, and that SUB
     * and DIV clues don't apply to blocks of the wrong size.
     */
    for (i = 0; i < a; i++) {
	if (dsf_canonify(dsf, i) == i) {
	    if (*p == 'a' || *p == 'm') {
		/* these clues need no validation */
	    } else if (*p == 'd' || *p == 's') {
		if (dsf_size(dsf, i) != 2) {
                    sfree(dsf);
		    return "Subtraction and division blocks must have area 2";
                }
	    } else if (!*p) {
                sfree(dsf);
		return "Too few clues for block structure";
	    } else {
                sfree(dsf);
		return "Unrecognised clue type";
	    }
	    p++;
	    while (*p && isdigit((unsigned char)*p)) p++;
	}
    }
    sfree(dsf);
    if (*p)
	return "Too many clues for block structure";

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
    state->clues->dsf = snew_dsf(a);
    parse_block_structure(&p, w, state->clues->dsf);

    assert(*p == ',');
    p++;

    state->clues->clues = snewn(a, long);
    for (i = 0; i < a; i++) {
	if (dsf_canonify(state->clues->dsf, i) == i) {
	    long clue = 0;
	    switch (*p) {
	      case 'a':
		clue = C_ADD;
		break;
	      case 'm':
		clue = C_MUL;
		break;
	      case 's':
		clue = C_SUB;
		assert(dsf_size(state->clues->dsf, i) == 2);
		break;
	      case 'd':
		clue = C_DIV;
		assert(dsf_size(state->clues->dsf, i) == 2);
		break;
	      default:
		assert(!"Bad description in new_game");
	    }
	    p++;
	    clue |= atol(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	    state->clues->clues[i] = clue;
	} else
	    state->clues->clues[i] = 0;
    }

    state->grid = snewn(a, digit);
    state->pencil = snewn(a, int);
    for (i = 0; i < a; i++) {
	state->grid[i] = 0;
	state->pencil[i] = 0;
    }

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
    memcpy(ret->grid, state->grid, a*sizeof(digit));
    memcpy(ret->pencil, state->pencil, a*sizeof(int));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->pencil);
    if (--state->clues->refcount <= 0) {
	sfree(state->clues->dsf);
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
    memset(soln, 0, a);

    ret = solver(w, state->clues->dsf, state->clues->clues,
		 soln, DIFFCOUNT-1);

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

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, if hshow is true.
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
     * fixed position. When true, pressing a valid number or letter
     * key or Space will enter that number or letter in the grid.
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
#define BORDER (TILESIZE / 2)
#define GRIDEXTRA max((TILESIZE / 32),1)
#define COORD(x) ((x)*TILESIZE + BORDER)
#define FROMCOORD(x) (((x)+(TILESIZE-BORDER)) / TILESIZE - 1)

#define FLASH_TIME 0.4F

#define DF_PENCIL_SHIFT 16
#define DF_ERR_LATIN 0x8000
#define DF_ERR_CLUE 0x4000
#define DF_HIGHLIGHT 0x2000
#define DF_HIGHLIGHT_PENCIL 0x1000
#define DF_DIGIT_MASK 0x000F

struct game_drawstate {
    int tilesize;
    bool started;
    long *tiles;
    long *errors;
    char *minus_sign, *times_sign, *divide_sign;
};

static bool check_errors(const game_state *state, long *errors)
{
    int w = state->par.w, a = w*w;
    int i, j, x, y;
    bool errs = false;
    long *cluevals;
    bool *full;

    cluevals = snewn(a, long);
    full = snewn(a, bool);

    if (errors)
	for (i = 0; i < a; i++) {
	    errors[i] = 0;
	    full[i] = true;
	}

    for (i = 0; i < a; i++) {
	long clue;

	j = dsf_canonify(state->clues->dsf, i);
	if (j == i) {
	    cluevals[i] = state->grid[i];
	} else {
	    clue = state->clues->clues[j] & CMASK;

	    switch (clue) {
	      case C_ADD:
		cluevals[j] += state->grid[i];
		break;
	      case C_MUL:
		cluevals[j] *= state->grid[i];
		break;
	      case C_SUB:
		cluevals[j] = labs(cluevals[j] - state->grid[i]);
		break;
	      case C_DIV:
		{
		    int d1 = min(cluevals[j], state->grid[i]);
		    int d2 = max(cluevals[j], state->grid[i]);
		    if (d1 == 0 || d2 % d1 != 0)
			cluevals[j] = 0;
		    else
			cluevals[j] = d2 / d1;
		}
		break;
	    }
	}

	if (!state->grid[i])
	    full[j] = false;
    }

    for (i = 0; i < a; i++) {
	j = dsf_canonify(state->clues->dsf, i);
	if (j == i) {
	    if ((state->clues->clues[j] & ~CMASK) != cluevals[i]) {
		errs = true;
		if (errors && full[j])
		    errors[j] |= DF_ERR_CLUE;
	    }
	}
    }

    sfree(cluevals);
    sfree(full);

    for (y = 0; y < w; y++) {
	int mask = 0, errmask = 0;
	for (x = 0; x < w; x++) {
	    int bit = 1 << state->grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = true;
	    errmask &= ~1;
	    if (errors) {
		for (x = 0; x < w; x++)
		    if (errmask & (1 << state->grid[y*w+x]))
			errors[y*w+x] |= DF_ERR_LATIN;
	    }
	}
    }

    for (x = 0; x < w; x++) {
	int mask = 0, errmask = 0;
	for (y = 0; y < w; y++) {
	    int bit = 1 << state->grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = true;
	    errmask &= ~1;
	    if (errors) {
		for (y = 0; y < w; y++)
		    if (errmask & (1 << state->grid[y*w+x]))
			errors[y*w+x] |= DF_ERR_LATIN;
	    }
	}
    }

    return errs;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->par.w;
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = FROMCOORD(x);
    ty = FROMCOORD(y);

    if (tx >= 0 && tx < w && ty >= 0 && ty < w) {
        if (button == LEFT_BUTTON) {
	    if (tx == ui->hx && ty == ui->hy &&
		ui->hshow && !ui->hpencil) {
                ui->hshow = false;
            } else {
                ui->hx = tx;
                ui->hy = ty;
                ui->hshow = true;
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
    }
    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->hx, &ui->hy, w, w, false);
        ui->hshow = true;
        ui->hcursor = true;
        return UI_UPDATE;
    }
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil ^= 1;
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
    game_state *ret;
    int x, y, i, n;

    if (move[0] == 'S') {
	ret = dup_game(from);
	ret->completed = ret->cheated = true;

	for (i = 0; i < a; i++) {
	    if (move[i+1] < '1' || move[i+1] > '0'+w) {
		free_game(ret);
		return NULL;
	    }
	    ret->grid[i] = move[i+1] - '0';
	    ret->pencil[i] = 0;
	}

	if (move[a+1] != '\0') {
	    free_game(ret);
	    return NULL;
	}

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < w && y >= 0 && y < w && n >= 0 && n <= w) {

	ret = dup_game(from);
        if (move[0] == 'P' && n > 0) {
            ret->pencil[y*w+x] ^= 1 << n;
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
	ret = dup_game(from);
	for (i = 0; i < a; i++) {
	    if (!ret->grid[i])
		ret->pencil[i] = (1 << (w+1)) - (1 << 1);
	}
	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
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

    *ncolours = NCOLOURS;
    return ret;
}

static const char *const minus_signs[] = { "\xE2\x88\x92", "-" };
static const char *const times_signs[] = { "\xC3\x97", "*" };
static const char *const divide_signs[] = { "\xC3\xB7", "/" };

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int w = state->par.w, a = w*w;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->started = false;
    ds->tiles = snewn(a, long);
    for (i = 0; i < a; i++)
	ds->tiles[i] = -1;
    ds->errors = snewn(a, long);
    ds->minus_sign = text_fallback(dr, minus_signs, lenof(minus_signs));
    ds->times_sign = text_fallback(dr, times_signs, lenof(times_signs));
    ds->divide_sign = text_fallback(dr, divide_signs, lenof(divide_signs));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds->errors);
    sfree(ds->minus_sign);
    sfree(ds->times_sign);
    sfree(ds->divide_sign);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, struct clues *clues,
		      int x, int y, long tile, bool only_one_op)
{
    int w = clues->w /* , a = w*w */;
    int tx, ty, tw, th;
    int cx, cy, cw, ch;
    char str[64];

    tx = BORDER + x * TILESIZE + 1 + GRIDEXTRA;
    ty = BORDER + y * TILESIZE + 1 + GRIDEXTRA;

    cx = tx;
    cy = ty;
    cw = tw = TILESIZE-1-2*GRIDEXTRA;
    ch = th = TILESIZE-1-2*GRIDEXTRA;

    if (x > 0 && dsf_canonify(clues->dsf, y*w+x) == dsf_canonify(clues->dsf, y*w+x-1))
	cx -= GRIDEXTRA, cw += GRIDEXTRA;
    if (x+1 < w && dsf_canonify(clues->dsf, y*w+x) == dsf_canonify(clues->dsf, y*w+x+1))
	cw += GRIDEXTRA;
    if (y > 0 && dsf_canonify(clues->dsf, y*w+x) == dsf_canonify(clues->dsf, (y-1)*w+x))
	cy -= GRIDEXTRA, ch += GRIDEXTRA;
    if (y+1 < w && dsf_canonify(clues->dsf, y*w+x) == dsf_canonify(clues->dsf, (y+1)*w+x))
	ch += GRIDEXTRA;

    clip(dr, cx, cy, cw, ch);

    /* background needs erasing */
    draw_rect(dr, cx, cy, cw, ch,
	      (tile & DF_HIGHLIGHT) ? COL_HIGHLIGHT : COL_BACKGROUND);

    /* pencil-mode highlight */
    if (tile & DF_HIGHLIGHT_PENCIL) {
        int coords[6];
        coords[0] = cx;
        coords[1] = cy;
        coords[2] = cx+cw/2;
        coords[3] = cy;
        coords[4] = cx;
        coords[5] = cy+ch/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /*
     * Draw the corners of thick lines in corner-adjacent squares,
     * which jut into this square by one pixel.
     */
    if (x > 0 && y > 0 && dsf_canonify(clues->dsf, y*w+x) != dsf_canonify(clues->dsf, (y-1)*w+x-1))
	draw_rect(dr, tx-GRIDEXTRA, ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x+1 < w && y > 0 && dsf_canonify(clues->dsf, y*w+x) != dsf_canonify(clues->dsf, (y-1)*w+x+1))
	draw_rect(dr, tx+TILESIZE-1-2*GRIDEXTRA, ty-GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x > 0 && y+1 < w && dsf_canonify(clues->dsf, y*w+x) != dsf_canonify(clues->dsf, (y+1)*w+x-1))
	draw_rect(dr, tx-GRIDEXTRA, ty+TILESIZE-1-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);
    if (x+1 < w && y+1 < w && dsf_canonify(clues->dsf, y*w+x) != dsf_canonify(clues->dsf, (y+1)*w+x+1))
	draw_rect(dr, tx+TILESIZE-1-2*GRIDEXTRA, ty+TILESIZE-1-2*GRIDEXTRA, GRIDEXTRA, GRIDEXTRA, COL_GRID);

    /* Draw the box clue. */
    if (dsf_canonify(clues->dsf, y*w+x) == y*w+x) {
	long clue = clues->clues[y*w+x];
	long cluetype = clue & CMASK, clueval = clue & ~CMASK;
	int size = dsf_size(clues->dsf, y*w+x);
	/*
	 * Special case of clue-drawing: a box with only one square
	 * is written as just the number, with no operation, because
	 * it doesn't matter whether the operation is ADD or MUL.
	 * The generation code above should never produce puzzles
	 * containing such a thing - I think they're inelegant - but
	 * it's possible to type in game IDs from elsewhere, so I
	 * want to display them right if so.
	 */
	sprintf (str, "%ld%s", clueval,
		 (size == 1 || only_one_op ? "" :
		  cluetype == C_ADD ? "+" :
		  cluetype == C_SUB ? ds->minus_sign :
		  cluetype == C_MUL ? ds->times_sign :
		  /* cluetype == C_DIV ? */ ds->divide_sign));
	draw_text(dr, tx + GRIDEXTRA * 2, ty + GRIDEXTRA * 2 + TILESIZE/4,
		  FONT_VARIABLE, TILESIZE/4, ALIGN_VNORMAL | ALIGN_HLEFT,
		  (tile & DF_ERR_CLUE ? COL_ERROR : COL_GRID), str);
    }

    /* new number needs drawing? */
    if (tile & DF_DIGIT_MASK) {
	str[1] = '\0';
	str[0] = (tile & DF_DIGIT_MASK) + '0';
	draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  (tile & DF_ERR_LATIN) ? COL_ERROR : COL_USER, str);
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
	    /* Start with the whole square */
	    pl = tx + GRIDEXTRA;
	    pr = pl + TILESIZE - GRIDEXTRA;
	    pt = ty + GRIDEXTRA;
	    pb = pt + TILESIZE - GRIDEXTRA;
	    if (dsf_canonify(clues->dsf, y*w+x) == y*w+x) {
		/*
		 * Make space for the clue text.
		 */
		pt += TILESIZE/4;
		/* minph--; */
	    }

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
	    pl = tx + (TILESIZE - fontsize * pw) / 2;
	    pt = ty + (TILESIZE - fontsize * ph) / 2;

	    /*
	     * And move it down a bit if it's collided with some
	     * clue text.
	     */
	    if (dsf_canonify(clues->dsf, y*w+x) == y*w+x) {
		pt = max(pt, ty + GRIDEXTRA * 3 + TILESIZE/4);
	    }

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

    unclip(dr);

    draw_update(dr, cx, cy, cw, ch);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->par.w /*, a = w*w */;
    int x, y;

    if (!ds->started) {
	/*
	 * Big containing rectangle.
	 */
	draw_rect(dr, COORD(0) - GRIDEXTRA, COORD(0) - GRIDEXTRA,
		  w*TILESIZE+1+GRIDEXTRA*2, w*TILESIZE+1+GRIDEXTRA*2,
		  COL_GRID);

	draw_update(dr, 0, 0, SIZE(w), SIZE(w));

	ds->started = true;
    }

    check_errors(state, ds->errors);

    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    long tile = 0L;

	    if (state->grid[y*w+x])
		tile = state->grid[y*w+x];
	    else
		tile = (long)state->pencil[y*w+x] << DF_PENCIL_SHIFT;

	    if (ui->hshow && ui->hx == x && ui->hy == y)
		tile |= (ui->hpencil ? DF_HIGHLIGHT_PENCIL : DF_HIGHLIGHT);

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                tile |= DF_HIGHLIGHT;  /* completion flash */

	    tile |= ds->errors[y*w+x];

	    if (ds->tiles[y*w+x] != tile) {
		ds->tiles[y*w+x] = tile;
		draw_tile(dr, ds, state->clues, x, y, tile,
			  state->par.multiplication_only);
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
        *x = BORDER + ui->hx * TILESIZE + 1 + GRIDEXTRA;
        *y = BORDER + ui->hy * TILESIZE + 1 + GRIDEXTRA;

        *w = *h = TILESIZE-1-2*GRIDEXTRA;
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

/*
 * Subfunction to draw the thick lines between cells. In order to do
 * this using the line-drawing rather than rectangle-drawing API (so
 * as to get line thicknesses to scale correctly) and yet have
 * correctly mitred joins between lines, we must do this by tracing
 * the boundary of each sub-block and drawing it in one go as a
 * single polygon.
 */
static void outline_block_structure(drawing *dr, game_drawstate *ds,
				    int w, int *dsf, int ink)
{
    int a = w*w;
    int *coords;
    int i, n;
    int x, y, dx, dy, sx, sy, sdx, sdy;

    coords = snewn(4*a, int);

    /*
     * Iterate over all the blocks.
     */
    for (i = 0; i < a; i++) {
	if (dsf_canonify(dsf, i) != i)
	    continue;

	/*
	 * For each block, we need a starting square within it which
	 * has a boundary at the left. Conveniently, we have one
	 * right here, by construction.
	 */
	x = i % w;
	y = i / w;
	dx = -1;
	dy = 0;

	/*
	 * Now begin tracing round the perimeter. At all
	 * times, (x,y) describes some square within the
	 * block, and (x+dx,y+dy) is some adjacent square
	 * outside it; so the edge between those two squares
	 * is always an edge of the block.
	 */
	sx = x, sy = y, sdx = dx, sdy = dy;   /* save starting position */
	n = 0;
	do {
	    int cx, cy, tx, ty, nin;

	    /*
	     * Advance to the next edge, by looking at the two
	     * squares beyond it. If they're both outside the block,
	     * we turn right (by leaving x,y the same and rotating
	     * dx,dy clockwise); if they're both inside, we turn
	     * left (by rotating dx,dy anticlockwise and contriving
	     * to leave x+dx,y+dy unchanged); if one of each, we go
	     * straight on (and may enforce by assertion that
	     * they're one of each the _right_ way round).
	     */
	    nin = 0;
	    tx = x - dy + dx;
	    ty = y + dx + dy;
	    nin += (tx >= 0 && tx < w && ty >= 0 && ty < w &&
		    dsf_canonify(dsf, ty*w+tx) == i);
	    tx = x - dy;
	    ty = y + dx;
	    nin += (tx >= 0 && tx < w && ty >= 0 && ty < w &&
		    dsf_canonify(dsf, ty*w+tx) == i);
	    if (nin == 0) {
		/*
		 * Turn right.
		 */
		int tmp;
		tmp = dx;
		dx = -dy;
		dy = tmp;
	    } else if (nin == 2) {
		/*
		 * Turn left.
		 */
		int tmp;

		x += dx;
		y += dy;

		tmp = dx;
		dx = dy;
		dy = -tmp;

		x -= dx;
		y -= dy;
	    } else {
		/*
		 * Go straight on.
		 */
		x -= dy;
		y += dx;
	    }

	    /*
	     * Now enforce by assertion that we ended up
	     * somewhere sensible.
	     */
	    assert(x >= 0 && x < w && y >= 0 && y < w &&
		   dsf_canonify(dsf, y*w+x) == i);
	    assert(x+dx < 0 || x+dx >= w || y+dy < 0 || y+dy >= w ||
		   dsf_canonify(dsf, (y+dy)*w+(x+dx)) != i);

	    /*
	     * Record the point we just went past at one end of the
	     * edge. To do this, we translate (x,y) down and right
	     * by half a unit (so they're describing a point in the
	     * _centre_ of the square) and then translate back again
	     * in a manner rotated by dy and dx.
	     */
	    assert(n < 2*w+2);
	    cx = ((2*x+1) + dy + dx) / 2;
	    cy = ((2*y+1) - dx + dy) / 2;
	    coords[2*n+0] = BORDER + cx * TILESIZE;
	    coords[2*n+1] = BORDER + cy * TILESIZE;
	    n++;

	} while (x != sx || y != sy || dx != sdx || dy != sdy);

	/*
	 * That's our polygon; now draw it.
	 */
	draw_polygon(dr, coords, n, -1, ink);
    }

    sfree(coords);
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->par.w;
    int ink = print_mono_colour(dr, 0);
    int x, y;
    char *minus_sign, *times_sign, *divide_sign;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    minus_sign = text_fallback(dr, minus_signs, lenof(minus_signs));
    times_sign = text_fallback(dr, times_signs, lenof(times_signs));
    divide_sign = text_fallback(dr, divide_signs, lenof(divide_signs));

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
     * Thick lines between cells.
     */
    print_line_width(dr, 3 * TILESIZE / 40);
    outline_block_structure(dr, ds, w, state->clues->dsf, ink);

    /*
     * Clues.
     */
    for (y = 0; y < w; y++)
	for (x = 0; x < w; x++)
	    if (dsf_canonify(state->clues->dsf, y*w+x) == y*w+x) {
		long clue = state->clues->clues[y*w+x];
		long cluetype = clue & CMASK, clueval = clue & ~CMASK;
		int size = dsf_size(state->clues->dsf, y*w+x);
		char str[64];

		/*
		 * As in the drawing code, we omit the operator for
		 * blocks of area 1.
		 */
		sprintf (str, "%ld%s", clueval,
			 (size == 1 ? "" :
			  cluetype == C_ADD ? "+" :
			  cluetype == C_SUB ? minus_sign :
			  cluetype == C_MUL ? times_sign :
			  /* cluetype == C_DIV ? */ divide_sign));

		draw_text(dr,
			  BORDER+x*TILESIZE + 5*TILESIZE/80,
			  BORDER+y*TILESIZE + 20*TILESIZE/80,
			  FONT_VARIABLE, TILESIZE/4,
			  ALIGN_VNORMAL | ALIGN_HLEFT,
			  ink, str);
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

    sfree(minus_sign);
    sfree(times_sign);
    sfree(divide_sign);
}

#ifdef COMBINED
#define thegame keen
#endif

const struct game thegame = {
    "Keen", "games.keen", "keen",
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
	memset(s->grid, 0, p->w * p->w);
	ret = solver(p->w, s->clues->dsf, s->clues->clues,
		     s->grid, diff);
	if (ret <= diff)
	    break;
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
		printf("Difficulty rating: %s\n", keen_diffnames[ret]);
	} else {
	    solver_show_working = really_show_working ? 1 : 0;
	    memset(s->grid, 0, p->w * p->w);
	    ret = solver(p->w, s->clues->dsf, s->clues->clues,
			 s->grid, diff);
	    if (ret != diff)
		printf("Puzzle is inconsistent\n");
	    else {
		/*
		 * We don't have a game_text_format for this game,
		 * so we have to output the solution manually.
		 */
		int x, y;
		for (y = 0; y < p->w; y++) {
		    for (x = 0; x < p->w; x++) {
			printf("%s%c", x>0?" ":"", '0' + s->grid[y*p->w+x]);
		    }
		    putchar('\n');
		}
	    }
	}
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
