/*
 * Test code for the 2-3-4 tree. This code maintains an alternative
 * representation of the data in the tree, in an array (using the
 * obvious and slow insert and delete functions). After each tree
 * operation, the verify() function is called, which ensures all
 * the tree properties are preserved:
 *  - node->child->parent always equals node
 *  - tree->root->parent always equals NULL
 *  - number of kids == 0 or number of elements + 1;
 *  - tree has the same depth everywhere
 *  - every node has at least one element
 *  - subtree element counts are accurate
 *  - any NULL kid pointer is accompanied by a zero count
 *  - in a sorted tree: ordering property between elements of a
 *    node and elements of its children is preserved
 * and also ensures the list represented by the tree is the same
 * list it should be. (This last check also doubly verifies the
 * ordering properties, because the `same list it should be' is by
 * definition correctly ordered. It also ensures all nodes are
 * distinct, because the enum functions would get caught in a loop
 * if not.)
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#define TREE234_INTERNALS
#include "tree234.h"

/*
 * Error reporting function.
 */
static void error(const char *fmt, ...) {
    va_list ap;
    printf("ERROR: ");
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    printf("\n");
}

/* The array representation of the data. */
static void **array;
static int arraylen, arraysize;
static cmpfn234 cmp;

/* The tree representation of the same data. */
static tree234 *tree;

/*
 * Routines to provide a diagnostic printout of a tree. Currently
 * relies on every element in the tree being a one-character string
 * :-)
 */
typedef struct {
    char **levels;
} dispctx;

static int dispnode(node234 *n, int level, dispctx *ctx) {
    if (level == 0) {
	int xpos = strlen(ctx->levels[0]);
	int len;

	if (n->elems[2])
	    len = sprintf(ctx->levels[0]+xpos, " %s%s%s",
			  (char *)n->elems[0], (char *)n->elems[1],
                          (char *)n->elems[2]);
	else if (n->elems[1])
	    len = sprintf(ctx->levels[0]+xpos, " %s%s",
			  (char *)n->elems[0], (char *)n->elems[1]);
	else
	    len = sprintf(ctx->levels[0]+xpos, " %s",
			  (char *)n->elems[0]);
	return xpos + 1 + (len-1) / 2;
    } else {
	int xpos[4], nkids;
	int nodelen, mypos, myleft, x, i;

	xpos[0] = dispnode(n->kids[0], level-3, ctx);
	xpos[1] = dispnode(n->kids[1], level-3, ctx);
	nkids = 2;
	if (n->kids[2]) {
	    xpos[2] = dispnode(n->kids[2], level-3, ctx);
	    nkids = 3;
	}
	if (n->kids[3]) {
	    xpos[3] = dispnode(n->kids[3], level-3, ctx);
	    nkids = 4;
	}

	if (nkids == 4)
	    mypos = (xpos[1] + xpos[2]) / 2;
	else if (nkids == 3)
	    mypos = xpos[1];
	else
	    mypos = (xpos[0] + xpos[1]) / 2;
	nodelen = nkids * 2 - 1;
	myleft = mypos - ((nodelen-1)/2);
	assert(myleft >= xpos[0]);
	assert(myleft + nodelen-1 <= xpos[nkids-1]);

	x = strlen(ctx->levels[level]);
	while (x <= xpos[0] && x < myleft)
	    ctx->levels[level][x++] = ' ';
	while (x < myleft)
	    ctx->levels[level][x++] = '_';
	if (nkids==4)
	    x += sprintf(ctx->levels[level]+x, ".%s.%s.%s.",
			 (char *)n->elems[0], (char *)n->elems[1],
                         (char *)n->elems[2]);
	else if (nkids==3)
	    x += sprintf(ctx->levels[level]+x, ".%s.%s.",
			 (char *)n->elems[0], (char *)n->elems[1]);
	else
	    x += sprintf(ctx->levels[level]+x, ".%s.",
			 (char *)n->elems[0]);
	while (x < xpos[nkids-1])
	    ctx->levels[level][x++] = '_';
	ctx->levels[level][x] = '\0';

	x = strlen(ctx->levels[level-1]);
	for (i = 0; i < nkids; i++) {
	    int rpos, pos;
	    rpos = xpos[i];
	    if (i > 0 && i < nkids-1)
		pos = myleft + 2*i;
	    else
		pos = rpos;
	    if (rpos < pos)
		rpos++;
	    while (x < pos && x < rpos)
		ctx->levels[level-1][x++] = ' ';
	    if (x == pos)
		ctx->levels[level-1][x++] = '|';
	    while (x < pos || x < rpos)
		ctx->levels[level-1][x++] = '_';
	    if (x == pos)
		ctx->levels[level-1][x++] = '|';
	}
	ctx->levels[level-1][x] = '\0';

	x = strlen(ctx->levels[level-2]);
	for (i = 0; i < nkids; i++) {
	    int rpos = xpos[i];

	    while (x < rpos)
		ctx->levels[level-2][x++] = ' ';
	    ctx->levels[level-2][x++] = '|';
	}
	ctx->levels[level-2][x] = '\0';

	return mypos;
    }
}

static void disptree(tree234 *t) {
    dispctx ctx;
    char *leveldata;
    int width = count234(t);
    int ht = height234(t) * 3 - 2;
    int i;

    if (!t->root) {
	printf("[empty tree]\n");
    }

    leveldata = smalloc(ht * (width+2));
    ctx.levels = smalloc(ht * sizeof(char *));
    for (i = 0; i < ht; i++) {
	ctx.levels[i] = leveldata + i * (width+2);
	ctx.levels[i][0] = '\0';
    }

    (void) dispnode(t->root, ht-1, &ctx);

    for (i = ht; i-- ;)
	printf("%s\n", ctx.levels[i]);

    sfree(ctx.levels);
    sfree(leveldata);
}

typedef struct {
    int treedepth;
    int elemcount;
} chkctx;

static int chknode(chkctx *ctx, int level, node234 *node,
                   void *lowbound, void *highbound) {
    int nkids, nelems;
    int i;
    int count;

    /* Count the non-NULL kids. */
    for (nkids = 0; nkids < 4 && node->kids[nkids]; nkids++);
    /* Ensure no kids beyond the first NULL are non-NULL. */
    for (i = nkids; i < 4; i++)
        if (node->kids[i]) {
            error("node %p: nkids=%d but kids[%d] non-NULL",
                   node, nkids, i);
        } else if (node->counts[i]) {
            error("node %p: kids[%d] NULL but count[%d]=%d nonzero",
                   node, i, i, node->counts[i]);
	}

    /* Count the non-NULL elements. */
    for (nelems = 0; nelems < 3 && node->elems[nelems]; nelems++);
    /* Ensure no elements beyond the first NULL are non-NULL. */
    for (i = nelems; i < 3; i++)
        if (node->elems[i]) {
            error("node %p: nelems=%d but elems[%d] non-NULL",
                   node, nelems, i);
        }

    if (nkids == 0) {
        /*
         * If nkids==0, this is a leaf node; verify that the tree
         * depth is the same everywhere.
         */
        if (ctx->treedepth < 0)
            ctx->treedepth = level;    /* we didn't know the depth yet */
        else if (ctx->treedepth != level)
            error("node %p: leaf at depth %d, previously seen depth %d",
                   node, level, ctx->treedepth);
    } else {
        /*
         * If nkids != 0, then it should be nelems+1, unless nelems
         * is 0 in which case nkids should also be 0 (and so we
         * shouldn't be in this condition at all).
         */
        int shouldkids = (nelems ? nelems+1 : 0);
        if (nkids != shouldkids) {
            error("node %p: %d elems should mean %d kids but has %d",
                   node, nelems, shouldkids, nkids);
        }
    }

    /*
     * nelems should be at least 1.
     */
    if (nelems == 0) {
        error("node %p: no elems", node, nkids);
    }

    /*
     * Add nelems to the running element count of the whole tree.
     */
    ctx->elemcount += nelems;

    /*
     * Check ordering property: all elements should be strictly >
     * lowbound, strictly < highbound, and strictly < each other in
     * sequence. (lowbound and highbound are NULL at edges of tree
     * - both NULL at root node - and NULL is considered to be <
     * everything and > everything. IYSWIM.)
     */
    if (cmp) {
	for (i = -1; i < nelems; i++) {
	    void *lower = (i == -1 ? lowbound : node->elems[i]);
	    void *higher = (i+1 == nelems ? highbound : node->elems[i+1]);
	    if (lower && higher && cmp(lower, higher) >= 0) {
		error("node %p: kid comparison [%d=%s,%d=%s] failed",
		      node, i, lower, i+1, higher);
	    }
	}
    }

    /*
     * Check parent pointers: all non-NULL kids should have a
     * parent pointer coming back to this node.
     */
    for (i = 0; i < nkids; i++)
        if (node->kids[i]->parent != node) {
            error("node %p kid %d: parent ptr is %p not %p",
                   node, i, node->kids[i]->parent, node);
        }


    /*
     * Now (finally!) recurse into subtrees.
     */
    count = nelems;

    for (i = 0; i < nkids; i++) {
        void *lower = (i == 0 ? lowbound : node->elems[i-1]);
        void *higher = (i >= nelems ? highbound : node->elems[i]);
	int subcount = chknode(ctx, level+1, node->kids[i], lower, higher);
	if (node->counts[i] != subcount) {
	    error("node %p kid %d: count says %d, subtree really has %d",
		  node, i, node->counts[i], subcount);
	}
        count += subcount;
    }

    return count;
}

static void verifytree(tree234 *tree, void **array, int arraylen) {
    chkctx ctx;
    int i;
    void *p;

    ctx.treedepth = -1;                /* depth unknown yet */
    ctx.elemcount = 0;                 /* no elements seen yet */
    /*
     * Verify validity of tree properties.
     */
    if (tree->root) {
	if (tree->root->parent != NULL)
	    error("root->parent is %p should be null", tree->root->parent);
        chknode(&ctx, 0, tree->root, NULL, NULL);
    }
    printf("tree depth: %d\n", ctx.treedepth);
    /*
     * Enumerate the tree and ensure it matches up to the array.
     */
    for (i = 0; NULL != (p = index234(tree, i)); i++) {
        if (i >= arraylen)
            error("tree contains more than %d elements", arraylen);
        if (array[i] != p)
            error("enum at position %d: array says %s, tree says %s",
                   i, array[i], p);
    }
    if (ctx.elemcount != i) {
        error("tree really contains %d elements, enum gave %d",
               ctx.elemcount, i);
    }
    if (i < arraylen) {
        error("enum gave only %d elements, array has %d", i, arraylen);
    }
    i = count234(tree);
    if (ctx.elemcount != i) {
        error("tree really contains %d elements, count234 gave %d",
	      ctx.elemcount, i);
    }
}
static void verify(void) { verifytree(tree, array, arraylen); }

static void internal_addtest(void *elem, int index, void *realret) {
    int i, j;
    void *retval;

    if (arraysize < arraylen+1) {
        arraysize = arraylen+1+256;
        array = (array == NULL ? smalloc(arraysize*sizeof(*array)) :
                 srealloc(array, arraysize*sizeof(*array)));
    }

    i = index;
    /* now i points to the first element >= elem */
    retval = elem;                  /* expect elem returned (success) */
    for (j = arraylen; j > i; j--)
	array[j] = array[j-1];
    array[i] = elem;                /* add elem to array */
    arraylen++;

    if (realret != retval) {
        error("add: retval was %p expected %p", realret, retval);
    }

    verify();
}

static void addtest(void *elem) {
    int i;
    void *realret;

    realret = add234(tree, elem);

    i = 0;
    while (i < arraylen && cmp(elem, array[i]) > 0)
        i++;
    if (i < arraylen && !cmp(elem, array[i])) {
        void *retval = array[i];       /* expect that returned not elem */
	if (realret != retval) {
	    error("add: retval was %p expected %p", realret, retval);
	}
    } else
	internal_addtest(elem, i, realret);
}

static void addpostest(void *elem, int i) {
    void *realret;

    realret = addpos234(tree, elem, i);

    internal_addtest(elem, i, realret);
}

static void delpostest(int i) {
    int index = i;
    void *elem = array[i], *ret;

    /* i points to the right element */
    while (i < arraylen-1) {
	array[i] = array[i+1];
	i++;
    }
    arraylen--;			       /* delete elem from array */

    if (tree->cmp)
	ret = del234(tree, elem);
    else
	ret = delpos234(tree, index);

    if (ret != elem) {
	error("del returned %p, expected %p", ret, elem);
    }

    verify();
}

static void deltest(void *elem) {
    int i;

    i = 0;
    while (i < arraylen && cmp(elem, array[i]) > 0)
        i++;
    if (i >= arraylen || cmp(elem, array[i]) != 0)
        return;                        /* don't do it! */
    delpostest(i);
}

/* A sample data set and test utility. Designed for pseudo-randomness,
 * and yet repeatability. */

/*
 * This random number generator uses the `portable implementation'
 * given in ANSI C99 draft N869. It assumes `unsigned' is 32 bits;
 * change it if not.
 */
static int randomnumber(unsigned *seed) {
    *seed *= 1103515245;
    *seed += 12345;
    return ((*seed) / 65536) % 32768;
}

static int mycmp(void *av, void *bv) {
    char const *a = (char const *)av;
    char const *b = (char const *)bv;
    return strcmp(a, b);
}

static const char *const strings_init[] = {
    "0", "2", "3", "I", "K", "d", "H", "J", "Q", "N", "n", "q", "j", "i",
    "7", "G", "F", "D", "b", "x", "g", "B", "e", "v", "V", "T", "f", "E",
    "S", "8", "A", "k", "X", "p", "C", "R", "a", "o", "r", "O", "Z", "u",
    "6", "1", "w", "L", "P", "M", "c", "U", "h", "9", "t", "5", "W", "Y",
    "m", "s", "l", "4",
#if 0
    "a", "ab", "absque", "coram", "de",
    "palam", "clam", "cum", "ex", "e",
    "sine", "tenus", "pro", "prae",
    "banana", "carrot", "cabbage", "broccoli", "onion", "zebra",
    "penguin", "blancmange", "pangolin", "whale", "hedgehog",
    "giraffe", "peanut", "bungee", "foo", "bar", "baz", "quux",
    "murfl", "spoo", "breen", "flarn", "octothorpe",
    "snail", "tiger", "elephant", "octopus", "warthog", "armadillo",
    "aardvark", "wyvern", "dragon", "elf", "dwarf", "orc", "goblin",
    "pixie", "basilisk", "warg", "ape", "lizard", "newt", "shopkeeper",
    "wand", "ring", "amulet"
#endif
};

#define NSTR lenof(strings_init)
static char *strings[NSTR];

static void findtest(void) {
    static const int rels[] = {
	REL234_EQ, REL234_GE, REL234_LE, REL234_LT, REL234_GT
    };
    static const char *const relnames[] = {
	"EQ", "GE", "LE", "LT", "GT"
    };
    int i, j, rel, index;
    char *p, *ret, *realret, *realret2;
    int lo, hi, mid, c;

    for (i = 0; i < (int)NSTR; i++) {
	p = strings[i];
	for (j = 0; j < (int)(sizeof(rels)/sizeof(*rels)); j++) {
	    rel = rels[j];

	    lo = 0; hi = arraylen-1;
	    while (lo <= hi) {
		mid = (lo + hi) / 2;
		c = strcmp(p, array[mid]);
		if (c < 0)
		    hi = mid-1;
		else if (c > 0)
		    lo = mid+1;
		else
		    break;
	    }

	    if (c == 0) {
		if (rel == REL234_LT)
		    ret = (mid > 0 ? array[--mid] : NULL);
		else if (rel == REL234_GT)
		    ret = (mid < arraylen-1 ? array[++mid] : NULL);
		else
		    ret = array[mid];
	    } else {
		assert(lo == hi+1);
		if (rel == REL234_LT || rel == REL234_LE) {
		    mid = hi;
		    ret = (hi >= 0 ? array[hi] : NULL);
		} else if (rel == REL234_GT || rel == REL234_GE) {
		    mid = lo;
		    ret = (lo < arraylen ? array[lo] : NULL);
		} else
		    ret = NULL;
	    }

	    realret = findrelpos234(tree, p, NULL, rel, &index);
	    if (realret != ret) {
		error("find(\"%s\",%s) gave %s should be %s",
		      p, relnames[j], realret, ret);
	    }
	    if (realret && index != mid) {
		error("find(\"%s\",%s) gave %d should be %d",
		      p, relnames[j], index, mid);
	    }
	    if (realret && rel == REL234_EQ) {
		realret2 = index234(tree, index);
		if (realret2 != realret) {
		    error("find(\"%s\",%s) gave %s(%d) but %d -> %s",
			  p, relnames[j], realret, index, index, realret2);
		}
	    }
#if 0
	    printf("find(\"%s\",%s) gave %s(%d)\n", p, relnames[j],
		   realret, index);
#endif
	}
    }

    realret = findrelpos234(tree, NULL, NULL, REL234_GT, &index);
    if (arraylen && (realret != array[0] || index != 0)) {
	error("find(NULL,GT) gave %s(%d) should be %s(0)",
	      realret, index, array[0]);
    } else if (!arraylen && (realret != NULL)) {
	error("find(NULL,GT) gave %s(%d) should be NULL",
	      realret, index);
    }

    realret = findrelpos234(tree, NULL, NULL, REL234_LT, &index);
    if (arraylen && (realret != array[arraylen-1] || index != arraylen-1)) {
	error("find(NULL,LT) gave %s(%d) should be %s(0)",
	      realret, index, array[arraylen-1]);
    } else if (!arraylen && (realret != NULL)) {
	error("find(NULL,LT) gave %s(%d) should be NULL",
	      realret, index);
    }
}

static void splittest(tree234 *tree, void **array, int arraylen) {
    int i;
    tree234 *tree3, *tree4;
    for (i = 0; i <= arraylen; i++) {
	tree3 = copytree234(tree, NULL, NULL);
	tree4 = splitpos234(tree3, i, false);
	verifytree(tree3, array, i);
	verifytree(tree4, array+i, arraylen-i);
	join234(tree3, tree4);
	freetree234(tree4);	       /* left empty by join */
	verifytree(tree3, array, arraylen);
	freetree234(tree3);
    }
}

int main(void) {
    int in[NSTR];
    int i, j, k;
    int tworoot, tmplen;
    unsigned seed = 0;
    tree234 *tree2, *tree3, *tree4;

    setvbuf(stdout, NULL, _IOLBF, 0);

    for (i = 0; i < (int)NSTR; i++)
        strings[i] = dupstr(strings_init[i]);

    for (i = 0; i < (int)NSTR; i++) in[i] = 0;
    array = NULL;
    arraylen = arraysize = 0;
    tree = newtree234(mycmp);
    cmp = mycmp;

    verify();
    for (i = 0; i < 10000; i++) {
        j = randomnumber(&seed);
        j %= NSTR;
        printf("trial: %d\n", i);
        if (in[j]) {
            printf("deleting %s (%d)\n", strings[j], j);
            deltest(strings[j]);
            in[j] = 0;
        } else {
            printf("adding %s (%d)\n", strings[j], j);
            addtest(strings[j]);
            in[j] = 1;
        }
	disptree(tree);
	findtest();
    }

    while (arraylen > 0) {
        j = randomnumber(&seed);
        j %= arraylen;
        deltest(array[j]);
    }

    freetree234(tree);

    /*
     * Now try an unsorted tree. We don't really need to test
     * delpos234 because we know del234 is based on it, so it's
     * already been tested in the above sorted-tree code; but for
     * completeness we'll use it to tear down our unsorted tree
     * once we've built it.
     */
    tree = newtree234(NULL);
    cmp = NULL;
    verify();
    for (i = 0; i < 1000; i++) {
	printf("trial: %d\n", i);
	j = randomnumber(&seed);
	j %= NSTR;
	k = randomnumber(&seed);
	k %= count234(tree)+1;
	printf("adding string %s at index %d\n", strings[j], k);
	addpostest(strings[j], k);
    }

    /*
     * While we have this tree in its full form, we'll take a copy
     * of it to use in split and join testing.
     */
    tree2 = copytree234(tree, NULL, NULL);
    verifytree(tree2, array, arraylen);/* check the copy is accurate */
    /*
     * Split tests. Split the tree at every possible point and
     * check the resulting subtrees.
     */
    tworoot = (!tree2->root->elems[1]);/* see if it has a 2-root */
    splittest(tree2, array, arraylen);
    /*
     * Now do the split test again, but on a tree that has a 2-root
     * (if the previous one didn't) or doesn't (if the previous one
     * did).
     */
    tmplen = arraylen;
    while ((!tree2->root->elems[1]) == tworoot) {
	delpos234(tree2, --tmplen);
    }
    printf("now trying splits on second tree\n");
    splittest(tree2, array, tmplen);
    freetree234(tree2);

    /*
     * Back to the main testing of uncounted trees.
     */
    while (count234(tree) > 0) {
	printf("cleanup: tree size %d\n", count234(tree));
	j = randomnumber(&seed);
	j %= count234(tree);
	printf("deleting string %s from index %d\n", (char *)array[j], j);
	delpostest(j);
    }
    freetree234(tree);

    /*
     * Finally, do some testing on split/join on _sorted_ trees. At
     * the same time, we'll be testing split on very small trees.
     */
    tree = newtree234(mycmp);
    cmp = mycmp;
    arraylen = 0;
    for (i = 0; i < 17; i++) {
	tree2 = copytree234(tree, NULL, NULL);
	splittest(tree2, array, arraylen);
	freetree234(tree2);
	if (i < 16)
	    addtest(strings[i]);
    }
    freetree234(tree);

    /*
     * Test silly cases of join: join(emptytree, emptytree), and
     * also ensure join correctly spots when sorted trees fail the
     * ordering constraint.
     */
    tree = newtree234(mycmp);
    tree2 = newtree234(mycmp);
    tree3 = newtree234(mycmp);
    tree4 = newtree234(mycmp);
    assert(mycmp(strings[0], strings[1]) < 0);   /* just in case :-) */
    add234(tree2, strings[1]);
    add234(tree4, strings[0]);
    array[0] = strings[0];
    array[1] = strings[1];
    verifytree(tree, array, 0);
    verifytree(tree2, array+1, 1);
    verifytree(tree3, array, 0);
    verifytree(tree4, array, 1);

    /*
     * So:
     *  - join(tree,tree3) should leave both tree and tree3 unchanged.
     *  - joinr(tree,tree2) should leave both tree and tree2 unchanged.
     *  - join(tree4,tree3) should leave both tree3 and tree4 unchanged.
     *  - join(tree, tree2) should move the element from tree2 to tree.
     *  - joinr(tree4, tree3) should move the element from tree4 to tree3.
     *  - join(tree,tree3) should return NULL and leave both unchanged.
     *  - join(tree3,tree) should work and create a bigger tree in tree3.
     */
    assert(tree == join234(tree, tree3));
    verifytree(tree, array, 0);
    verifytree(tree3, array, 0);
    assert(tree2 == join234r(tree, tree2));
    verifytree(tree, array, 0);
    verifytree(tree2, array+1, 1);
    assert(tree4 == join234(tree4, tree3));
    verifytree(tree3, array, 0);
    verifytree(tree4, array, 1);
    assert(tree == join234(tree, tree2));
    verifytree(tree, array+1, 1);
    verifytree(tree2, array, 0);
    assert(tree3 == join234r(tree4, tree3));
    verifytree(tree3, array, 1);
    verifytree(tree4, array, 0);
    assert(NULL == join234(tree, tree3));
    verifytree(tree, array+1, 1);
    verifytree(tree3, array, 1);
    assert(tree3 == join234(tree3, tree));
    verifytree(tree3, array, 2);
    verifytree(tree, array, 0);

    return 0;
}
