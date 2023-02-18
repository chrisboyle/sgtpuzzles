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
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

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

struct ancestor {
    struct set *prev;		       /* index of ancestor set in set list */
    unsigned char pa, pb, po, pr;      /* operation that got here from prev */
};

struct set {
    int *numbers;		       /* rationals stored as n,d pairs */
    short nnumbers;		       /* # of rationals, so half # of ints */
    short flags;		       /* SETFLAG_CONCAT only, at present */
    int npaths;			       /* number of ways to reach this set */
    struct ancestor a;		       /* primary ancestor */
    struct ancestor *as;	       /* further ancestors, if we care */
    int nas, assize;
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
#define OPFLAG_UNARY        4
#define OPFLAG_UNARYPREFIX  8
#define OPFLAG_FN           16

struct operation {
    /*
     * Most operations should be shown in the output working, but
     * concatenation should not; we just take the result of the
     * concatenation and assume that it's obvious how it was
     * derived.
     */
    int display;

    /*
     * Text display of the operator, in expressions and for
     * debugging respectively.
     */
    const char *text, *dbgtext;

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
     * Function which implements the operator. Returns true on
     * success, false on failure. Takes two rationals and writes
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
    if ((b) && (a) && (r) / (b) != (a)) return false; \
} while (0)

#define ADD(r, a, b) do { \
    (r) = (a) + (b); \
    if ((a) > 0 && (b) > 0 && (r) < 0) return false; \
    if ((a) < 0 && (b) < 0 && (r) > 0) return false; \
} while (0)

#define OUT(output, n, d) do { \
    int g = gcd((n),(d)); \
    if (g < 0) g = -g; \
    if ((d) < 0) g = -g; \
    if (g == -1 && (n) < -INT_MAX) return false; \
    if (g == -1 && (d) < -INT_MAX) return false; \
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
    return true;
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
    return true;
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
    return true;
}

static int perform_div(int *a, int *b, int *output)
{
    int tn, bn;

    /*
     * Division by zero is outlawed.
     */
    if (b[0] == 0)
	return false;

    /*
     * a0/a1 / b0/b1 = (a0*b1) / (a1*b0)
     */
    MUL(tn, a[0], b[1]);
    MUL(bn, a[1], b[0]);
    OUT(output, tn, bn);
    return true;
}

static int perform_exact_div(int *a, int *b, int *output)
{
    int tn, bn;

    /*
     * Division by zero is outlawed.
     */
    if (b[0] == 0)
	return false;

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

static int max_p10(int n, int *p10_r)
{
    /*
     * Find the smallest power of ten strictly greater than n.
     *
     * Special case: we must return at least 10, even if n is
     * zero. (This is because this function is used for finding
     * the power of ten by which to multiply a number being
     * concatenated to the front of n, and concatenating 1 to 0
     * should yield 10 and not 1.)
     */
    int p10 = 10;
    while (p10 <= (INT_MAX/10) && p10 <= n)
	p10 *= 10;
    if (p10 > INT_MAX/10)
	return false;		       /* integer overflow */
    *p10_r = p10;
    return true;
}

static int perform_concat(int *a, int *b, int *output)
{
    int t1, t2, p10;

    /*
     * We can't concatenate anything which isn't a non-negative
     * integer.
     */
    if (a[1] != 1 || b[1] != 1 || a[0] < 0 || b[0] < 0)
	return false;

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
	return false;

    if (!max_p10(b[0], &p10)) return false;

    MUL(t1, p10, a[0]);
    ADD(t2, t1, b[0]);
    OUT(output, t2, 1);
    return true;
}

#define IPOW(ret, x, y) do { \
    int ipow_limit = (y); \
    if ((x) == 1 || (x) == 0) ipow_limit = 1; \
    else if ((x) == -1) ipow_limit &= 1; \
    (ret) = 1; \
    while (ipow_limit-- > 0) { \
	int tmp; \
	MUL(tmp, ret, x); \
	ret = tmp; \
    } \
} while (0)

static int perform_exp(int *a, int *b, int *output)
{
    int an, ad, xn, xd;

    /*
     * Exponentiation is permitted if the result is rational. This
     * means that:
     * 
     * 	- first we see whether we can take the (denominator-of-b)th
     * 	  root of a and get a rational; if not, we give up.
     * 
     *  - then we do take that root of a
     * 
     *  - then we multiply by itself (numerator-of-b) times.
     */
    if (b[1] > 1) {
	an = (int)(0.5 + pow(a[0], 1.0/b[1]));
	ad = (int)(0.5 + pow(a[1], 1.0/b[1]));
	IPOW(xn, an, b[1]);
	IPOW(xd, ad, b[1]);
	if (xn != a[0] || xd != a[1])
	    return false;
    } else {
	an = a[0];
	ad = a[1];
    }
    if (b[0] >= 0) {
	IPOW(xn, an, b[0]);
	IPOW(xd, ad, b[0]);
    } else {
	IPOW(xd, an, -b[0]);
	IPOW(xn, ad, -b[0]);
    }
    if (xd == 0)
	return false;

    OUT(output, xn, xd);
    return true;
}

static int perform_factorial(int *a, int *b, int *output)
{
    int ret, t, i;

    /*
     * Factorials of non-negative integers are permitted.
     */
    if (a[1] != 1 || a[0] < 0)
	return false;

    /*
     * However, a special case: we don't take a factorial of
     * anything which would thereby remain the same.
     */
    if (a[0] == 1 || a[0] == 2)
	return false;

    ret = 1;
    for (i = 1; i <= a[0]; i++) {
	MUL(t, ret, i);
	ret = t;
    }

    OUT(output, ret, 1);
    return true;
}

static int perform_decimal(int *a, int *b, int *output)
{
    int p10;

    /*
     * Add a decimal digit to the front of a number;
     * fail if it's not an integer.
     * So, 1 --> 0.1, 15 --> 0.15,
     * or, rather, 1 --> 1/10, 15 --> 15/100,
     * x --> x / (smallest power of 10 > than x)
     *
     */
    if (a[1] != 1) return false;

    if (!max_p10(a[0], &p10)) return false;

    OUT(output, a[0], p10);
    return true;
}

static int perform_recur(int *a, int *b, int *output)
{
    int p10, tn, bn;

    /*
     * This converts a number like .4 to .44444..., or .45 to .45454...
     * The input number must be -1 < a < 1.
     *
     * Calculate the smallest power of 10 that divides the denominator exactly,
     * returning if no such power of 10 exists. Then multiply the numerator
     * up accordingly, and the new denominator becomes that power of 10 - 1.
     */
    if (abs(a[0]) >= abs(a[1])) return false; /* -1 < a < 1 */

    p10 = 10;
    while (p10 <= (INT_MAX/10)) {
        if ((a[1] <= p10) && (p10 % a[1]) == 0) goto found;
        p10 *= 10;
    }
    return false;
found:
    tn = a[0] * (p10 / a[1]);
    bn = p10 - 1;

    OUT(output, tn, bn);
    return true;
}

static int perform_root(int *a, int *b, int *output)
{
    /*
     * A root B is: 1           iff a == 0
     *              B ^ (1/A)   otherwise
     */
    int ainv[2], res;

    if (a[0] == 0) {
        OUT(output, 1, 1);
        return true;
    }

    OUT(ainv, a[1], a[0]);
    res = perform_exp(b, ainv, output);
    return res;
}

static int perform_perc(int *a, int *b, int *output)
{
    if (a[0] == 0) return false; /* 0% = 0, uninteresting. */
    if (a[1] > (INT_MAX/100)) return false;

    OUT(output, a[0], a[1]*100);
    return true;
}

static int perform_gamma(int *a, int *b, int *output)
{
    int asub1[2];

    /*
     * gamma(a) = (a-1)!
     *
     * special case not caught by perform_fact: gamma(1) is 1 so
     * don't bother.
     */
    if (a[0] == 1 && a[1] == 1) return false;

    OUT(asub1, a[0]-a[1], a[1]);
    return perform_factorial(asub1, b, output);
}

static int perform_sqrt(int *a, int *b, int *output)
{
    int half[2] = { 1, 2 };

    /*
     * sqrt(0) == 0, sqrt(1) == 1: don't perform unary noops.
     */
    if (a[0] == 0 || (a[0] == 1 && a[1] == 1)) return false;

    return perform_exp(a, half, output);
}

static const struct operation op_add = {
    true, "+", "+", 0, 10, 0, true, perform_add
};
static const struct operation op_sub = {
    true, "-", "-", 0, 10, 2, false, perform_sub
};
static const struct operation op_mul = {
    true, "*", "*", 0, 20, 0, true, perform_mul
};
static const struct operation op_div = {
    true, "/", "/", 0, 20, 2, false, perform_div
};
static const struct operation op_xdiv = {
    true, "/", "/", 0, 20, 2, false, perform_exact_div
};
static const struct operation op_concat = {
    false, "", "concat", OPFLAG_NEEDS_CONCAT | OPFLAG_KEEPS_CONCAT,
	1000, 0, false, perform_concat
};
static const struct operation op_exp = {
    true, "^", "^", 0, 30, 1, false, perform_exp
};
static const struct operation op_factorial = {
    true, "!", "!", OPFLAG_UNARY, 40, 0, false, perform_factorial
};
static const struct operation op_decimal = {
    true, ".", ".", OPFLAG_UNARY | OPFLAG_UNARYPREFIX | OPFLAG_NEEDS_CONCAT | OPFLAG_KEEPS_CONCAT, 50, 0, false, perform_decimal
};
static const struct operation op_recur = {
    true, "...", "recur", OPFLAG_UNARY | OPFLAG_NEEDS_CONCAT, 45, 2, false, perform_recur
};
static const struct operation op_root = {
    true, "v~", "root", 0, 30, 1, false, perform_root
};
static const struct operation op_perc = {
    true, "%", "%", OPFLAG_UNARY | OPFLAG_NEEDS_CONCAT, 45, 1, false, perform_perc
};
static const struct operation op_gamma = {
    true, "gamma", "gamma", OPFLAG_UNARY | OPFLAG_UNARYPREFIX | OPFLAG_FN, 1, 3, false, perform_gamma
};
static const struct operation op_sqrt = {
    true, "v~", "sqrt", OPFLAG_UNARY | OPFLAG_UNARYPREFIX, 30, 1, false, perform_sqrt
};

/*
 * In Countdown, divisions resulting in fractions are disallowed.
 * http://www.askoxford.com/wordgames/countdown/rules/
 */
static const struct operation *const ops_countdown[] = {
    &op_add, &op_mul, &op_sub, &op_xdiv, NULL
};
static const struct rules rules_countdown = {
    ops_countdown, false
};

/*
 * A slightly different rule set which handles the reasonably well
 * known puzzle of making 24 using two 3s and two 8s. For this we
 * need rational rather than integer division.
 */
static const struct operation *const ops_3388[] = {
    &op_add, &op_mul, &op_sub, &op_div, NULL
};
static const struct rules rules_3388 = {
    ops_3388, true
};

/*
 * A still more permissive rule set usable for the four-4s problem
 * and similar things. Permits concatenation.
 */
static const struct operation *const ops_four4s[] = {
    &op_add, &op_mul, &op_sub, &op_div, &op_concat, NULL
};
static const struct rules rules_four4s = {
    ops_four4s, true
};

/*
 * The most permissive ruleset I can think of. Permits
 * exponentiation, and also silly unary operators like factorials.
 */
static const struct operation *const ops_anythinggoes[] = {
    &op_add, &op_mul, &op_sub, &op_div, &op_concat, &op_exp, &op_factorial, 
    &op_decimal, &op_recur, &op_root, &op_perc, &op_gamma, &op_sqrt, NULL
};
static const struct rules rules_anythinggoes = {
    ops_anythinggoes, true
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

static void addset(struct sets *s, struct set *set, int multiple,
		   struct set *prev, int pa, int po, int pb, int pr)
{
    struct set *s2;
    int npaths = (prev ? prev->npaths : 1);

    assert(set == s->setlists[s->nsets / SETLISTLEN] + s->nsets % SETLISTLEN);
    s2 = add234(s->settree, set);
    if (s2 == set) {
	/*
	 * New set added to the tree.
	 */
	set->a.prev = prev;
	set->a.pa = pa;
	set->a.po = po;
	set->a.pb = pb;
	set->a.pr = pr;
	set->npaths = npaths;
	s->nsets++;
	s->nnumbers += 2 * set->nnumbers;
	set->as = NULL;
	set->nas = set->assize = 0;
    } else {
	/*
	 * Rediscovered an existing set. Update its npaths.
	 */
	s2->npaths += npaths;
	/*
	 * And optionally enter it as an additional ancestor.
	 */
	if (multiple) {
	    if (s2->nas >= s2->assize) {
		s2->assize = s2->nas * 3 / 2 + 4;
		s2->as = sresize(s2->as, s2->assize, struct ancestor);
	    }
	    s2->as[s2->nas].prev = prev;
	    s2->as[s2->nas].pa = pa;
	    s2->as[s2->nas].po = po;
	    s2->as[s2->nas].pb = pb;
	    s2->as[s2->nas].pr = pr;
	    s2->nas++;
	}
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
	return false;

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
    return true;
}

static struct sets *do_search(int ninputs, int *inputs,
			      const struct rules *rules, int *target,
			      int debug, int multiple)
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
    addset(s, sn, multiple, NULL, 0, 0, 0, 0);

    /*
     * Now perform the breadth-first search: keep looping over sets
     * until we run out of steam.
     */
    qpos = 0;
    while (qpos < s->nsets) {
	struct set *ss = s->setlists[qpos / SETLISTLEN] + qpos % SETLISTLEN;
	struct set *sn;
	int i, j, k, m;

	if (debug) {
	    int i;
	    printf("processing set:");
	    for (i = 0; i < ss->nnumbers; i++) {
		printf(" %d", ss->numbers[2*i]);
		if (ss->numbers[2*i+1] != 1)
		    printf("/%d", ss->numbers[2*i+1]);
	    }
	    printf("\n");
	}

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
		int jlimit = (ops[k]->flags & OPFLAG_UNARY ? 1 : ss->nnumbers);
		for (j = 0; j < jlimit; j++) {
		    int n[2], newnn = ss->nnumbers;
		    int pa, po, pb, pr;

		    if (!(ops[k]->flags & OPFLAG_UNARY)) {
			if (i == j)
			    continue;  /* can't combine a number with itself */
			if (i > j && ops[k]->commutes)
			    continue;  /* no need to do this both ways round */
                        newnn--;
		    }
		    if (!ops[k]->perform(ss->numbers+2*i, ss->numbers+2*j, n))
			continue;      /* operation failed */

		    sn = newset(s, newnn, ss->flags);

		    if (!(ops[k]->flags & OPFLAG_KEEPS_CONCAT))
			sn->flags &= ~SETFLAG_CONCAT;

		    for (m = 0; m < ss->nnumbers; m++) {
			if (m == i || (!(ops[k]->flags & OPFLAG_UNARY) &&
				       m == j))
			    continue;
			sn->numbers[2*sn->nnumbers] = ss->numbers[2*m];
			sn->numbers[2*sn->nnumbers + 1] = ss->numbers[2*m + 1];
			sn->nnumbers++;
		    }
		    pa = i;
		    if (ops[k]->flags & OPFLAG_UNARY)
			pb = sn->nnumbers+10;
		    else
			pb = j;
		    po = k;
		    pr = addtoset(sn, n);
		    addset(s, sn, multiple, ss, pa, po, pb, pr);
		    if (debug) {
			int i;
                        if (ops[k]->flags & OPFLAG_UNARYPREFIX)
                            printf("  %s %d ->", ops[po]->dbgtext, pa);
                        else if (ops[k]->flags & OPFLAG_UNARY)
                            printf("  %d %s ->", pa, ops[po]->dbgtext);
                        else
			    printf("  %d %s %d ->", pa, ops[po]->dbgtext, pb);
			for (i = 0; i < sn->nnumbers; i++) {
			    printf(" %d", sn->numbers[2*i]);
			    if (sn->numbers[2*i+1] != 1)
				printf("/%d", sn->numbers[2*i+1]);
			}
			printf("\n");
		    }
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
 * Print a text formula for producing a given output.
 */
static void print_recurse(struct sets *s, struct set *ss, int pathindex,
                          int index, int priority, int assoc, int child);
static void print_recurse_inner(struct sets *s, struct set *ss,
                                struct ancestor *a, int pathindex, int index,
                                int priority, int assoc, int child)
{
    if (a->prev && index != a->pr) {
	int pi;

	/*
	 * This number was passed straight down from this set's
	 * predecessor. Find its index in the previous set and
	 * recurse to there.
	 */
	pi = index;
	assert(pi != a->pr);
	if (pi > a->pr)
	    pi--;
	if (pi >= min(a->pa, a->pb)) {
	    pi++;
	    if (pi >= max(a->pa, a->pb))
		pi++;
	}
	print_recurse(s, a->prev, pathindex, pi, priority, assoc, child);
    } else if (a->prev && index == a->pr &&
	       s->ops[a->po]->display) {
	/*
	 * This number was created by a displayed operator in the
	 * transition from this set to its predecessor. Hence we
	 * write an open paren, then recurse into the first
	 * operand, then write the operator, then the second
	 * operand, and finally close the paren.
	 */
	const char *op;
	int parens, thispri, thisassoc;

	/*
	 * Determine whether we need parentheses.
	 */
	thispri = s->ops[a->po]->priority;
	thisassoc = s->ops[a->po]->assoc;
	parens = (thispri < priority ||
		  (thispri == priority && (assoc & child)));

	if (parens)
	    putchar('(');

	if (s->ops[a->po]->flags & OPFLAG_UNARYPREFIX)
	    for (op = s->ops[a->po]->text; *op; op++)
		putchar(*op);

        if (s->ops[a->po]->flags & OPFLAG_FN)
            putchar('(');

	print_recurse(s, a->prev, pathindex, a->pa, thispri, thisassoc, 1);

        if (s->ops[a->po]->flags & OPFLAG_FN)
            putchar(')');

	if (!(s->ops[a->po]->flags & OPFLAG_UNARYPREFIX))
	    for (op = s->ops[a->po]->text; *op; op++)
		putchar(*op);

	if (!(s->ops[a->po]->flags & OPFLAG_UNARY))
	    print_recurse(s, a->prev, pathindex, a->pb, thispri, thisassoc, 2);

	if (parens)
	    putchar(')');
    } else {
	/*
	 * This number is either an original, or something formed
	 * by a non-displayed operator (concatenation). Either way,
	 * we display it as is.
	 */
	printf("%d", ss->numbers[2*index]);
	if (ss->numbers[2*index+1] != 1)
	    printf("/%d", ss->numbers[2*index+1]);
    }
}
static void print_recurse(struct sets *s, struct set *ss, int pathindex,
                          int index, int priority, int assoc, int child)
{
    if (!ss->a.prev || pathindex < ss->a.prev->npaths) {
	print_recurse_inner(s, ss, &ss->a, pathindex,
			    index, priority, assoc, child);
    } else {
	int i;
	pathindex -= ss->a.prev->npaths;
	for (i = 0; i < ss->nas; i++) {
	    if (pathindex < ss->as[i].prev->npaths) {
		print_recurse_inner(s, ss, &ss->as[i], pathindex,
				    index, priority, assoc, child);
		break;
	    }
	    pathindex -= ss->as[i].prev->npaths;
	}
    }
}
static void print(int pathindex, struct sets *s, struct output *o)
{
    print_recurse(s, o->set, pathindex, o->index, 0, 0, 0);
}

/*
 * gcc -g -O0 -o numgame numgame.c -I.. ../{malloc,tree234,nullfe}.c -lm
 */
int main(int argc, char **argv)
{
    int doing_opts = true;
    const struct rules *rules = NULL;
    char *pname = argv[0];
    int got_target = false, target = 0;
    int numbers[10], nnumbers = 0;
    int verbose = false;
    int pathcounts = false;
    int multiple = false;
    int debug_bfs = false;
    int got_range = false, rangemin = 0, rangemax = 0;

    struct output *o;
    struct sets *s;
    int i, start, limit;

    while (--argc) {
	char *p = *++argv;
	int c;

	if (doing_opts && *p == '-') {
	    p++;

	    if (!strcmp(p, "-")) {
		doing_opts = false;
		continue;
	    } else if (*p == '-') {
		p++;
		if (!strcmp(p, "debug-bfs")) {
		    debug_bfs = true;
		} else {
		    fprintf(stderr, "%s: option '--%s' not recognised\n",
			    pname, p);
		}
	    } else while (p && *p) switch (c = *p++) {
	      case 'C':
		rules = &rules_countdown;
		break;
	      case 'B':
		rules = &rules_3388;
		break;
	      case 'D':
		rules = &rules_four4s;
		break;
	      case 'A':
		rules = &rules_anythinggoes;
		break;
	      case 'v':
		verbose = true;
		break;
	      case 'p':
		pathcounts = true;
		break;
	      case 'm':
		multiple = true;
		break;
	      case 't':
              case 'r':
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
			got_target = true;
			target = atoi(v);
			break;
                      case 'r':
                        {
                             char *sep = strchr(v, '-');
                             got_range = true;
                             if (sep) {
                                 rangemin = atoi(v);
                                 rangemax = atoi(sep+1);
                             } else {
                                 rangemin = 0;
                                 rangemax = atoi(v);
                             }
                        }
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
			pname, (int)lenof(numbers));
		return 1;
	    } else {
		numbers[nnumbers++] = atoi(p);
	    }
	}
    }

    if (!rules) {
	fprintf(stderr, "%s: no rule set specified; use -C,-B,-D,-A\n", pname);
	return 1;
    }

    if (!nnumbers) {
	fprintf(stderr, "%s: no input numbers specified\n", pname);
	return 1;
    }

    if (got_range) {
        if (got_target) {
            fprintf(stderr, "%s: only one of -t and -r may be specified\n", pname);
            return 1;
        }
        if (rangemin >= rangemax) {
            fprintf(stderr, "%s: range not sensible (%d - %d)\n", pname, rangemin, rangemax);
            return 1;
        }
    }

    s = do_search(nnumbers, numbers, rules, (got_target ? &target : NULL),
		  debug_bfs, multiple);

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
    } else if (got_range) {
        if (!findrelpos234(s->outputtree, &rangemin, outputfindcmp,
                           REL234_GE, &start) ||
            !findrelpos234(s->outputtree, &rangemax, outputfindcmp,
                           REL234_LE, &limit)) {
            printf("No solutions available in specified range %d-%d\n", rangemin, rangemax);
            return 1;
        }
        limit++;
    } else {
	start = 0;
	limit = count234(s->outputtree);
    }

    for (i = start; i < limit; i++) {
	char buf[256];

	o = index234(s->outputtree, i);

	sprintf(buf, "%d", o->number);

	if (pathcounts)
	    sprintf(buf + strlen(buf), " [%d]", o->npaths);

	if (got_target || verbose) {
	    int j, npaths;

	    if (multiple)
		npaths = o->npaths;
	    else
		npaths = 1;

	    for (j = 0; j < npaths; j++) {
		printf("%s = ", buf);
		print(j, s, o);
		putchar('\n');
	    }
	} else {
	    printf("%s\n", buf);
	}
    }

    free_sets(s);

    return 0;
}

/* vim: set shiftwidth=4 tabstop=8: */
