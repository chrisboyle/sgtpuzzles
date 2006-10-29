/*
 * This program implements a breadth-first search which
 * exhaustively solves the Countdown numbers game, and related
 * games with slightly different rule sets such as `Flippo'.
 * 
 * Currently it is simply a standalone command-line utility to
 * which you provide a set of numbers and it tells you everything
 * it can make together with how many different ways it can be
 * made. I would like ultimately to turn it into the generator for
 * a Puzzles puzzle, but I haven't even started on writing a
 * Puzzles user interface yet.
 */

/*
 * TODO:
 * 
 *  - start thinking about difficulty ratings
 *     + anything involving associative operations will be flagged
 * 	 as many-paths because of the associative options (e.g.
 * 	 2*3*4 can be (2*3)*4 or 2*(3*4), or indeed (2*4)*3). This
 * 	 is probably a _good_ thing, since those are unusually
 * 	 easy.
 *     + tree-structured calculations ((a*b)/(c+d)) have multiple
 * 	 paths because the independent branches of the tree can be
 * 	 evaluated in either order, whereas straight-line
 * 	 calculations with no branches will be considered easier.
 * 	 Can we do anything about this? It's certainly not clear to
 * 	 me that tree-structure calculations are _easier_, although
 * 	 I'm also not convinced they're harder.
 *     + I think for a realistic difficulty assessment we must also
 * 	 consider the `obviousness' of the arithmetic operations in
 * 	 some heuristic sense, and also (in Countdown) how many
 * 	 numbers ended up being used.
 *  - actually try some generations
 *  - at this point we're probably ready to start on the Puzzles
 *    integration.
 */

#include <stdio.h>
#include <limits.h>
#include <assert.h>

#include "puzzles.h"
#include "tree234.h"

/*
 * To search for numbers we can make, we employ a breadth-first
 * search across the space of sets of input numbers. That is, for
 * example, we start with the set (3,6,25,50,75,100); we apply
 * moves which involve combining two numbers (e.g. adding the 50
 * and the 75 takes us to the set (3,6,25,100,125); and then we see
 * if we ever end up with a set containing (say) 952.
 * 
 * If the rules are changed so that all the numbers must be used,
 * this is easy to adjust to: we simply see if we end up with a set
 * containing _only_ (say) 952.
 * 
 * Obviously, we can vary the rules about permitted arithmetic
 * operations simply by altering the set of valid moves in the bfs.
 * However, there's one common rule in this sort of puzzle which
 * takes a little more thought, and that's _concatenation_. For
 * example, if you are given (say) four 4s and required to make 10,
 * you are permitted to combine two of the 4s into a 44 to begin
 * with, making (44-4)/4 = 10. However, you are generally not
 * allowed to concatenate two numbers that _weren't_ both in the
 * original input set (you couldn't multiply two 4s to get 16 and
 * then concatenate a 4 on to it to make 164), so concatenation is
 * not an operation which is valid in all situations.
 * 
 * We could enforce this restriction by storing a flag alongside
 * each number indicating whether or not it's an original number;
 * the rules being that concatenation of two numbers is only valid
 * if they both have the original flag, and that its output _also_
 * has the original flag (so that you can concatenate three 4s into
 * a 444), but that applying any other arithmetic operation clears
 * the original flag on the output. However, we can get marginally
 * simpler than that by observing that since concatenation has to
 * happen to a number before any other operation, we can simply
 * place all the concatenations at the start of the search. In
 * other words, we have a global flag on an entire number _set_
 * which indicates whether we are still permitted to perform
 * concatenations; if so, we can concatenate any of the numbers in
 * that set. Performing any other operation clears the flag.
 */

#define SETFLAG_CONCAT 1	       /* we can do concatenation */

struct sets;

struct set {
    int *numbers;		       /* rationals stored as n,d pairs */
    short nnumbers;		       /* # of rationals, so half # of ints */
    short flags;		       /* SETFLAG_CONCAT only, at present */
    struct set *prev;		       /* index of ancestor set in set list */
    unsigned char pa, pb, po, pr;      /* operation that got here from prev */
    int npaths;			       /* number of ways to reach this set */
};

struct output {
    int number;
    struct set *set;
    int index;			       /* which number in the set is it? */
    int npaths;			       /* number of ways to reach this */
};

#define SETLISTLEN 1024
#define NUMBERLISTLEN 32768
#define OUTPUTLISTLEN 1024
struct operation;
struct sets {
    struct set **setlists;
    int nsets, nsetlists, setlistsize;
    tree234 *settree;
    int **numberlists;
    int nnumbers, nnumberlists, numberlistsize;
    struct output **outputlists;
    int noutputs, noutputlists, outputlistsize;
    tree234 *outputtree;
    const struct operation *const *ops;
};

#define OPFLAG_NEEDS_CONCAT 1
#define OPFLAG_KEEPS_CONCAT 2

struct operation {
    /*
     * Most operations should be shown in the output working, but
     * concatenation should not; we just take the result of the
     * concatenation and assume that it's obvious how it was
     * derived.
     */
    int display;

    /*
     * Text display of the operator.
     */
    char *text;

    /*
     * Flags dictating when the operator can be applied.
     */
    int flags;

    /*
     * Priority of the operator (for avoiding unnecessary
     * parentheses when formatting it into a string).
     */
    int priority;

    /*
     * Associativity of the operator. Bit 0 means we need parens
     * when the left operand of one of these operators is another
     * instance of it, e.g. (2^3)^4. Bit 1 means we need parens
     * when the right operand is another instance of the same
     * operator, e.g. 2-(3-4). Thus:
     * 
     * 	- this field is 0 for a fully associative operator, since
     * 	  we never need parens.
     *  - it's 1 for a right-associative operator.
     *  - it's 2 for a left-associative operator.
     * 	- it's 3 for a _non_-associative operator (which always
     * 	  uses parens just to be sure).
     */
    int assoc;

    /*
     * Whether the operator is commutative. Saves time in the
     * search if we don't have to try it both ways round.
     */
    int commutes;

    /*
     * Function which implements the operator. Returns TRUE on
     * success, FALSE on failure. Takes two rationals and writes
     * out a third.
     */
    int (*perform)(int *a, int *b, int *output);
};

struct rules {
    const struct operation *const *ops;
    int use_all;
};

#define MUL(r, a, b) do { \
    (r) = (a) * (b); \
    if ((b) && (a) && (r) / (b) != (a)) return FALSE; \
} while (0)

#define ADD(r, a, b) do { \
    (r) = (a) + (b); \
    if ((a) > 0 && (b) > 0 && (r) < 0) return FALSE; \
    if ((a) < 0 && (b) < 0 && (r) > 0) return FALSE; \
} while (0)

#define OUT(output, n, d) do { \
    int g = gcd((n),(d)); \
    if ((d) < 0) g = -g; \
    (output)[0] = (n)/g; \
    (output)[1] = (d)/g; \
    assert((output)[1] > 0); \
} while (0)

static int gcd(int x, int y)
{
    while (x != 0 && y != 0) {
	int t = x;
	x = y;
	y = t % y;
    }

    return abs(x + y);		       /* i.e. whichever one isn't zero */
}

static int perform_add(int *a, int *b, int *output)
{
    int at, bt, tn, bn;
    /*
     * a0/a1 + b0/b1 = (a0*b1 + b0*a1) / (a1*b1)
     */
    MUL(at, a[0], b[1]);
    MUL(bt, b[0], a[1]);
    ADD(tn, at, bt);
    MUL(bn, a[1], b[1]);
    OUT(output, tn, bn);
    return TRUE;
}

static int perform_sub(int *a, int *b, int *output)
{
    int at, bt, tn, bn;
    /*
     * a0/a1 - b0/b1 = (a0*b1 - b0*a1) / (a1*b1)
     */
    MUL(at, a[0], b[1]);
    MUL(bt, b[0], a[1]);
    ADD(tn, at, -bt);
    MUL(bn, a[1], b[1]);
    OUT(output, tn, bn);
    return TRUE;
}

static int perform_mul(int *a, int *b, int *output)
{
    int tn, bn;
    /*
     * a0/a1 * b0/b1 = (a0*b0) / (a1*b1)
     */
    MUL(tn, a[0], b[0]);
    MUL(bn, a[1], b[1]);
    OUT(output, tn, bn);
    return TRUE;
}

static int perform_div(int *a, int *b, int *output)
{
    int tn, bn;

    /*
     * Division by zero is outlawed.
     */
    if (b[0] == 0)
	return FALSE;

    /*
     * a0/a1 / b0/b1 = (a0*b1) / (a1*b0)
     */
    MUL(tn, a[0], b[1]);
    MUL(bn, a[1], b[0]);
    OUT(output, tn, bn);
    return TRUE;
}

static int perform_exact_div(int *a, int *b, int *output)
{
    int tn, bn;

    /*
     * Division by zero is outlawed.
     */
    if (b[0] == 0)
	return FALSE;

    /*
     * a0/a1 / b0/b1 = (a0*b1) / (a1*b0)
     */
    MUL(tn, a[0], b[1]);
    MUL(bn, a[1], b[0]);
    OUT(output, tn, bn);

    /*
     * Exact division means we require the result to be an integer.
     */
    return (output[1] == 1);
}

static int perform_concat(int *a, int *b, int *output)
{
    int t1, t2, p10;

    /*
     * We can't concatenate anything which isn't an integer.
     */
    if (a[1] != 1 || b[1] != 1)
	return FALSE;

    /*
     * For concatenation, we can safely assume leading zeroes
     * aren't an issue. It isn't clear whether they `should' be
     * allowed, but it turns out not to matter: concatenating a
     * leading zero on to a number in order to harmlessly get rid
     * of the zero is never necessary because unwanted zeroes can
     * be disposed of by adding them to something instead. So we
     * disallow them always.
     *
     * The only other possibility is that you might want to
     * concatenate a leading zero on to something and then
     * concatenate another non-zero digit on to _that_ (to make,
     * for example, 106); but that's also unnecessary, because you
     * can make 106 just as easily by concatenating the 0 on to the
     * _end_ of the 1 first.
     */
    if (a[0] == 0)
	return FALSE;

    /*
     * Find the smallest power of ten strictly greater than b. This
     * is the power of ten by which we'll multiply a.
     * 
     * Special case: we must multiply a by at least 10, even if b
     * is zero.
     */
    p10 = 10;
    while (p10 <= (INT_MAX/10) && p10 <= b[0])
	p10 *= 10;
    if (p10 > INT_MAX/10)
	return FALSE;		       /* integer overflow */
    MUL(t1, p10, a[0]);
    ADD(t2, t1, b[0]);
    OUT(output, t2, 1);
    return TRUE;
}

const static struct operation op_add = {
    TRUE, "+", 0, 10, 0, TRUE, perform_add
};
const static struct operation op_sub = {
    TRUE, "-", 0, 10, 2, FALSE, perform_sub
};
const static struct operation op_mul = {
    TRUE, "*", 0, 20, 0, TRUE, perform_mul
};
const static struct operation op_div = {
    TRUE, "/", 0, 20, 2, FALSE, perform_div
};
const static struct operation op_xdiv = {
    TRUE, "/", 0, 20, 2, FALSE, perform_exact_div
};
const static struct operation op_concat = {
    FALSE, "", OPFLAG_NEEDS_CONCAT | OPFLAG_KEEPS_CONCAT,
	1000, 0, FALSE, perform_concat
};

/*
 * In Countdown, divisions resulting in fractions are disallowed.
 * http://www.askoxford.com/wordgames/countdown/rules/
 */
const static struct operation *const ops_countdown[] = {
    &op_add, &op_mul, &op_sub, &op_xdiv, NULL
};
const static struct rules rules_countdown = {
    ops_countdown, FALSE
};

/*
 * A slightly different rule set which handles the reasonably well
 * known puzzle of making 24 using two 3s and two 8s. For this we
 * need rational rather than integer division.
 */
const static struct operation *const ops_3388[] = {
    &op_add, &op_mul, &op_sub, &op_div, NULL
};
const static struct rules rules_3388 = {
    ops_3388, TRUE
};

/*
 * A still more permissive rule set usable for the four-4s problem
 * and similar things. Permits concatenation.
 */
const static struct operation *const ops_four4s[] = {
    &op_add, &op_mul, &op_sub, &op_div, &op_concat, NULL
};
const static struct rules rules_four4s = {
    ops_four4s, TRUE
};

#define ratcmp(a,op,b) ( (long long)(a)[0] * (b)[1] op \
			 (long long)(b)[0] * (a)[1] )

static int addtoset(struct set *set, int newnumber[2])
{
    int i, j;

    /* Find where we want to insert the new number */
    for (i = 0; i < set->nnumbers &&
	 ratcmp(set->numbers+2*i, <, newnumber); i++);

    /* Move everything else up */
    for (j = set->nnumbers; j > i; j--) {
	set->numbers[2*j] = set->numbers[2*j-2];
	set->numbers[2*j+1] = set->numbers[2*j-1];
    }

    /* Insert the new number */
    set->numbers[2*i] = newnumber[0];
    set->numbers[2*i+1] = newnumber[1];

    set->nnumbers++;

    return i;
}

#define ensure(array, size, newlen, type) do { \
    if ((newlen) > (size)) { \
	(size) = (newlen) + 512; \
	(array) = sresize((array), (size), type); \
    } \
} while (0)

static int setcmp(void *av, void *bv)
{
    struct set *a = (struct set *)av;
    struct set *b = (struct set *)bv;
    int i;

    if (a->nnumbers < b->nnumbers)
	return -1;
    else if (a->nnumbers > b->nnumbers)
	return +1;

    if (a->flags < b->flags)
	return -1;
    else if (a->flags > b->flags)
	return +1;

    for (i = 0; i < a->nnumbers; i++) {
	if (ratcmp(a->numbers+2*i, <, b->numbers+2*i))
	    return -1;
	else if (ratcmp(a->numbers+2*i, >, b->numbers+2*i))
	    return +1;
    }

    return 0;
}

static int outputcmp(void *av, void *bv)
{
    struct output *a = (struct output *)av;
    struct output *b = (struct output *)bv;

    if (a->number < b->number)
	return -1;
    else if (a->number > b->number)
	return +1;

    return 0;
}

static int outputfindcmp(void *av, void *bv)
{
    int *a = (int *)av;
    struct output *b = (struct output *)bv;

    if (*a < b->number)
	return -1;
    else if (*a > b->number)
	return +1;

    return 0;
}

static void addset(struct sets *s, struct set *set, struct set *prev)
{
    struct set *s2;
    int npaths = (prev ? prev->npaths : 1);

    assert(set == s->setlists[s->nsets / SETLISTLEN] + s->nsets % SETLISTLEN);
    s2 = add234(s->settree, set);
    if (s2 == set) {
	/*
	 * New set added to the tree.
	 */
	set->prev = prev;
	set->npaths = npaths;
	s->nsets++;
	s->nnumbers += 2 * set->nnumbers;
    } else {
	/*
	 * Rediscovered an existing set. Update its npaths only.
	 */
	s2->npaths += npaths;
    }
}

static struct set *newset(struct sets *s, int nnumbers, int flags)
{
    struct set *sn;

    ensure(s->setlists, s->setlistsize, s->nsets/SETLISTLEN+1, struct set *);
    while (s->nsetlists <= s->nsets / SETLISTLEN)
	s->setlists[s->nsetlists++] = snewn(SETLISTLEN, struct set);
    sn = s->setlists[s->nsets / SETLISTLEN] + s->nsets % SETLISTLEN;

    if (s->nnumbers + nnumbers * 2 > s->nnumberlists * NUMBERLISTLEN)
	s->nnumbers = s->nnumberlists * NUMBERLISTLEN;
    ensure(s->numberlists, s->numberlistsize,
	   s->nnumbers/NUMBERLISTLEN+1, int *);
    while (s->nnumberlists <= s->nnumbers / NUMBERLISTLEN)
	s->numberlists[s->nnumberlists++] = snewn(NUMBERLISTLEN, int);
    sn->numbers = s->numberlists[s->nnumbers / NUMBERLISTLEN] +
	s->nnumbers % NUMBERLISTLEN;

    /*
     * Start the set off empty.
     */
    sn->nnumbers = 0;

    sn->flags = flags;

    return sn;
}

static int addoutput(struct sets *s, struct set *ss, int index, int *n)
{
    struct output *o, *o2;

    /*
     * Target numbers are always integers.
     */
    if (ss->numbers[2*index+1] != 1)
	return FALSE;

    ensure(s->outputlists, s->outputlistsize, s->noutputs/OUTPUTLISTLEN+1,
	   struct output *);
    while (s->noutputlists <= s->noutputs / OUTPUTLISTLEN)
	s->outputlists[s->noutputlists++] = snewn(OUTPUTLISTLEN,
						  struct output);
    o = s->outputlists[s->noutputs / OUTPUTLISTLEN] +
	s->noutputs % OUTPUTLISTLEN;

    o->number = ss->numbers[2*index];
    o->set = ss;
    o->index = index;
    o->npaths = ss->npaths;
    o2 = add234(s->outputtree, o);
    if (o2 != o) {
	o2->npaths += o->npaths;
    } else {
	s->noutputs++;
    }
    *n = o->number;
    return TRUE;
}

static struct sets *do_search(int ninputs, int *inputs,
			      const struct rules *rules, int *target)
{
    struct sets *s;
    struct set *sn;
    int qpos, i;
    const struct operation *const *ops = rules->ops;

    s = snew(struct sets);
    s->setlists = NULL;
    s->nsets = s->nsetlists = s->setlistsize = 0;
    s->numberlists = NULL;
    s->nnumbers = s->nnumberlists = s->numberlistsize = 0;
    s->outputlists = NULL;
    s->noutputs = s->noutputlists = s->outputlistsize = 0;
    s->settree = newtree234(setcmp);
    s->outputtree = newtree234(outputcmp);
    s->ops = ops;

    /*
     * Start with the input set.
     */
    sn = newset(s, ninputs, SETFLAG_CONCAT);
    for (i = 0; i < ninputs; i++) {
	int newnumber[2];
	newnumber[0] = inputs[i];
	newnumber[1] = 1;
	addtoset(sn, newnumber);
    }
    addset(s, sn, NULL);

    /*
     * Now perform the breadth-first search: keep looping over sets
     * until we run out of steam.
     */
    qpos = 0;
    while (qpos < s->nsets) {
	struct set *ss = s->setlists[qpos / SETLISTLEN] + qpos % SETLISTLEN;
	struct set *sn;
	int i, j, k, m;

	/*
	 * Record all the valid output numbers in this state. We
	 * can always do this if there's only one number in the
	 * state; otherwise, we can only do it if we aren't
	 * required to use all the numbers in coming to our answer.
	 */
	if (ss->nnumbers == 1 || !rules->use_all) {
	    for (i = 0; i < ss->nnumbers; i++) {
		int n;

		if (addoutput(s, ss, i, &n) && target && n == *target)
		    return s;
	    }
	}

	/*
	 * Try every possible operation from this state.
	 */
	for (k = 0; ops[k] && ops[k]->perform; k++) {
	    if ((ops[k]->flags & OPFLAG_NEEDS_CONCAT) &&
		!(ss->flags & SETFLAG_CONCAT))
		continue;	       /* can't use this operation here */
	    for (i = 0; i < ss->nnumbers; i++) {
		for (j = 0; j < ss->nnumbers; j++) {
		    int n[2];

		    if (i == j)
			continue;      /* can't combine a number with itself */
		    if (i > j && ops[k]->commutes)
			continue;      /* no need to do this both ways round */
		    if (!ops[k]->perform(ss->numbers+2*i, ss->numbers+2*j, n))
			continue;      /* operation failed */

		    sn = newset(s, ss->nnumbers-1, ss->flags);

		    if (!(ops[k]->flags & OPFLAG_KEEPS_CONCAT))
			sn->flags &= ~SETFLAG_CONCAT;

		    for (m = 0; m < ss->nnumbers; m++) {
			if (m == i || m == j)
			    continue;
			sn->numbers[2*sn->nnumbers] = ss->numbers[2*m];
			sn->numbers[2*sn->nnumbers + 1] = ss->numbers[2*m + 1];
			sn->nnumbers++;
		    }
		    sn->pa = i;
		    sn->pb = j;
		    sn->po = k;
		    sn->pr = addtoset(sn, n);
		    addset(s, sn, ss);
		}
	    }
	}

	qpos++;
    }

    return s;
}

static void free_sets(struct sets *s)
{
    int i;

    freetree234(s->settree);
    freetree234(s->outputtree);
    for (i = 0; i < s->nsetlists; i++)
	sfree(s->setlists[i]);
    sfree(s->setlists);
    for (i = 0; i < s->nnumberlists; i++)
	sfree(s->numberlists[i]);
    sfree(s->numberlists);
    for (i = 0; i < s->noutputlists; i++)
	sfree(s->outputlists[i]);
    sfree(s->outputlists);
    sfree(s);
}

/*
 * Construct a text formula for producing a given output.
 */
void mkstring_recurse(char **str, int *len,
		      struct sets *s, struct set *ss, int index,
		      int priority, int assoc, int child)
{
    if (ss->prev && index != ss->pr) {
	int pi;

	/*
	 * This number was passed straight down from this set's
	 * predecessor. Find its index in the previous set and
	 * recurse to there.
	 */
	pi = index;
	assert(pi != ss->pr);
	if (pi > ss->pr)
	    pi--;
	if (pi >= min(ss->pa, ss->pb)) {
	    pi++;
	    if (pi >= max(ss->pa, ss->pb))
		pi++;
	}
	mkstring_recurse(str, len, s, ss->prev, pi, priority, assoc, child);
    } else if (ss->prev && index == ss->pr &&
	       s->ops[ss->po]->display) {
	/*
	 * This number was created by a displayed operator in the
	 * transition from this set to its predecessor. Hence we
	 * write an open paren, then recurse into the first
	 * operand, then write the operator, then the second
	 * operand, and finally close the paren.
	 */
	char *op;
	int parens, thispri, thisassoc;

	/*
	 * Determine whether we need parentheses.
	 */
	thispri = s->ops[ss->po]->priority;
	thisassoc = s->ops[ss->po]->assoc;
	parens = (thispri < priority ||
		  (thispri == priority && (assoc & child)));

	if (parens) {
	    if (str)
		*(*str)++ = '(';
	    if (len)
		(*len)++;
	}
	mkstring_recurse(str, len, s, ss->prev, ss->pa, thispri, thisassoc, 1);
	for (op = s->ops[ss->po]->text; *op; op++) {
	    if (str)
		*(*str)++ = *op;
	    if (len)
		(*len)++;
	}
	mkstring_recurse(str, len, s, ss->prev, ss->pb, thispri, thisassoc, 2);
	if (parens) {
	    if (str)
		*(*str)++ = ')';
	    if (len)
		(*len)++;
	}
    } else {
	/*
	 * This number is either an original, or something formed
	 * by a non-displayed operator (concatenation). Either way,
	 * we display it as is.
	 */
	char buf[80], *p;
	int blen;
	blen = sprintf(buf, "%d", ss->numbers[2*index]);
	if (ss->numbers[2*index+1] != 1)
	    blen += sprintf(buf+blen, "/%d", ss->numbers[2*index+1]);
	assert(blen < lenof(buf));
	for (p = buf; *p; p++) {
	    if (str)
		*(*str)++ = *p;
	    if (len)
		(*len)++;
	}
    }
}
char *mkstring(struct sets *s, struct output *o)
{
    int len;
    char *str, *p;

    len = 0;
    mkstring_recurse(NULL, &len, s, o->set, o->index, 0, 0, 0);
    str = snewn(len+1, char);
    p = str;
    mkstring_recurse(&p, NULL, s, o->set, o->index, 0, 0, 0);
    assert(p - str <= len);
    *p = '\0';
    return str;
}

int main(int argc, char **argv)
{
    int doing_opts = TRUE;
    const struct rules *rules = NULL;
    char *pname = argv[0];
    int got_target = FALSE, target = 0;
    int numbers[10], nnumbers = 0;
    int verbose = FALSE;
    int pathcounts = FALSE;

    struct output *o;
    struct sets *s;
    int i, start, limit;

    while (--argc) {
	char *p = *++argv;
	int c;

	if (doing_opts && *p == '-') {
	    p++;

	    if (!strcmp(p, "-")) {
		doing_opts = FALSE;
		continue;
	    } else while (*p) switch (c = *p++) {
	      case 'C':
		rules = &rules_countdown;
		break;
	      case 'B':
		rules = &rules_3388;
		break;
	      case 'D':
		rules = &rules_four4s;
		break;
	      case 'v':
		verbose = TRUE;
		break;
	      case 'p':
		pathcounts = TRUE;
		break;
	      case 't':
		{
		    char *v;
		    if (*p) {
			v = p;
			p = NULL;
		    } else if (--argc) {
			v = *++argv;
		    } else {
			fprintf(stderr, "%s: option '-%c' expects an"
				" argument\n", pname, c);
			return 1;
		    }
		    switch (c) {
		      case 't':
			got_target = TRUE;
			target = atoi(v);
			break;
		    }
		}
		break;
	      default:
		fprintf(stderr, "%s: option '-%c' not"
			" recognised\n", pname, c);
		return 1;
	    }
	} else {
	    if (nnumbers >= lenof(numbers)) {
		fprintf(stderr, "%s: internal limit of %d numbers exceeded\n",
			pname, lenof(numbers));
		return 1;
	    } else {
		numbers[nnumbers++] = atoi(p);
	    }
	}
    }

    if (!rules) {
	fprintf(stderr, "%s: no rule set specified; use -C,-B,-D\n", pname);
	return 1;
    }

    if (!nnumbers) {
	fprintf(stderr, "%s: no input numbers specified\n", pname);
	return 1;
    }

    s = do_search(nnumbers, numbers, rules, (got_target ? &target : NULL));

    if (got_target) {
	o = findrelpos234(s->outputtree, &target, outputfindcmp,
			  REL234_LE, &start);
	if (!o)
	    start = -1;
	o = findrelpos234(s->outputtree, &target, outputfindcmp,
			  REL234_GE, &limit);
	if (!o)
	    limit = -1;
	assert(start != -1 || limit != -1);
	if (start == -1)
	    start = limit;
	else if (limit == -1)
	    limit = start;
	limit++;
    } else {
	start = 0;
	limit = count234(s->outputtree);
    }

    for (i = start; i < limit; i++) {
	o = index234(s->outputtree, i);

	printf("%d", o->number);

	if (pathcounts)
	    printf(" [%d]", o->npaths);

	if (got_target || verbose) {
	    char *p = mkstring(s, o);
	    printf(" = %s", p);
	    sfree(p);
	}

	printf("\n");
    }

    free_sets(s);

    return 0;
}
