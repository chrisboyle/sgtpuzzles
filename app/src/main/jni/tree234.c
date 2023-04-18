/*
 * tree234.c: reasonably generic counted 2-3-4 tree routines.
 * 
 * This file is copyright 1999-2001 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define TREE234_INTERNALS
#include "tree234.h"

#include "puzzles.h"		       /* for smalloc/sfree */

#ifdef DEBUG_TREE234
#include <stdarg.h>
static void logprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#define LOG(x) (logprintf x)
#else
#define LOG(x)
#endif

/*
 * Create a 2-3-4 tree.
 */
tree234 *newtree234(cmpfn234 cmp) {
    tree234 *ret = snew(tree234);
    LOG(("created tree %p\n", ret));
    ret->root = NULL;
    ret->cmp = cmp;
    return ret;
}

/*
 * Free a 2-3-4 tree (not including freeing the elements).
 */
static void freenode234(node234 *n) {
    if (!n)
	return;
    freenode234(n->kids[0]);
    freenode234(n->kids[1]);
    freenode234(n->kids[2]);
    freenode234(n->kids[3]);
    sfree(n);
}
void freetree234(tree234 *t) {
    freenode234(t->root);
    sfree(t);
}

/*
 * Internal function to count a node.
 */
static int countnode234(node234 *n) {
    int count = 0;
    int i;
    if (!n)
	return 0;
    for (i = 0; i < 4; i++)
	count += n->counts[i];
    for (i = 0; i < 3; i++)
	if (n->elems[i])
	    count++;
    return count;
}

/*
 * Count the elements in a tree.
 */
int count234(tree234 *t) {
    if (t->root)
	return countnode234(t->root);
    else
	return 0;
}

/*
 * Propagate a node overflow up a tree until it stops. Returns 0 or
 * 1, depending on whether the root had to be split or not.
 */
static int add234_insert(node234 *left, void *e, node234 *right,
			 node234 **root, node234 *n, int ki) {
    int lcount, rcount;
    /*
     * We need to insert the new left/element/right set in n at
     * child position ki.
     */
    lcount = countnode234(left);
    rcount = countnode234(right);
    while (n) {
	LOG(("  at %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	     n,
	     n->kids[0], n->counts[0], n->elems[0],
	     n->kids[1], n->counts[1], n->elems[1],
	     n->kids[2], n->counts[2], n->elems[2],
	     n->kids[3], n->counts[3]));
	LOG(("  need to insert %p/%d \"%s\" %p/%d at position %d\n",
	     left, lcount, e, right, rcount, ki));
	if (n->elems[1] == NULL) {
	    /*
	     * Insert in a 2-node; simple.
	     */
	    if (ki == 0) {
		LOG(("  inserting on left of 2-node\n"));
		n->kids[2] = n->kids[1];     n->counts[2] = n->counts[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = right;          n->counts[1] = rcount;
		n->elems[0] = e;
		n->kids[0] = left;           n->counts[0] = lcount;
	    } else { /* ki == 1 */
		LOG(("  inserting on right of 2-node\n"));
		n->kids[2] = right;          n->counts[2] = rcount;
		n->elems[1] = e;
		n->kids[1] = left;           n->counts[1] = lcount;
	    }
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    if (n->kids[2]) n->kids[2]->parent = n;
	    LOG(("  done\n"));
	    break;
	} else if (n->elems[2] == NULL) {
	    /*
	     * Insert in a 3-node; simple.
	     */
	    if (ki == 0) {
		LOG(("  inserting on left of 3-node\n"));
		n->kids[3] = n->kids[2];    n->counts[3] = n->counts[2];
		n->elems[2] = n->elems[1];
		n->kids[2] = n->kids[1];    n->counts[2] = n->counts[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = right;         n->counts[1] = rcount;
		n->elems[0] = e;
		n->kids[0] = left;          n->counts[0] = lcount;
	    } else if (ki == 1) {
		LOG(("  inserting in middle of 3-node\n"));
		n->kids[3] = n->kids[2];    n->counts[3] = n->counts[2];
		n->elems[2] = n->elems[1];
		n->kids[2] = right;         n->counts[2] = rcount;
		n->elems[1] = e;
		n->kids[1] = left;          n->counts[1] = lcount;
	    } else { /* ki == 2 */
		LOG(("  inserting on right of 3-node\n"));
		n->kids[3] = right;         n->counts[3] = rcount;
		n->elems[2] = e;
		n->kids[2] = left;          n->counts[2] = lcount;
	    }
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    if (n->kids[2]) n->kids[2]->parent = n;
	    if (n->kids[3]) n->kids[3]->parent = n;
	    LOG(("  done\n"));
	    break;
	} else {
	    node234 *m = snew(node234);
	    m->parent = n->parent;
	    LOG(("  splitting a 4-node; created new node %p\n", m));
	    /*
	     * Insert in a 4-node; split into a 2-node and a
	     * 3-node, and move focus up a level.
	     * 
	     * I don't think it matters which way round we put the
	     * 2 and the 3. For simplicity, we'll put the 3 first
	     * always.
	     */
	    if (ki == 0) {
		m->kids[0] = left;          m->counts[0] = lcount;
		m->elems[0] = e;
		m->kids[1] = right;         m->counts[1] = rcount;
		m->elems[1] = n->elems[0];
		m->kids[2] = n->kids[1];    m->counts[2] = n->counts[1];
		e = n->elems[1];
		n->kids[0] = n->kids[2];    n->counts[0] = n->counts[2];
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];    n->counts[1] = n->counts[3];
	    } else if (ki == 1) {
		m->kids[0] = n->kids[0];    m->counts[0] = n->counts[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = left;          m->counts[1] = lcount;
		m->elems[1] = e;
		m->kids[2] = right;         m->counts[2] = rcount;
		e = n->elems[1];
		n->kids[0] = n->kids[2];    n->counts[0] = n->counts[2];
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];    n->counts[1] = n->counts[3];
	    } else if (ki == 2) {
		m->kids[0] = n->kids[0];    m->counts[0] = n->counts[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = n->kids[1];    m->counts[1] = n->counts[1];
		m->elems[1] = n->elems[1];
		m->kids[2] = left;          m->counts[2] = lcount;
		/* e = e; */
		n->kids[0] = right;         n->counts[0] = rcount;
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];    n->counts[1] = n->counts[3];
	    } else { /* ki == 3 */
		m->kids[0] = n->kids[0];    m->counts[0] = n->counts[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = n->kids[1];    m->counts[1] = n->counts[1];
		m->elems[1] = n->elems[1];
		m->kids[2] = n->kids[2];    m->counts[2] = n->counts[2];
		n->kids[0] = left;          n->counts[0] = lcount;
		n->elems[0] = e;
		n->kids[1] = right;         n->counts[1] = rcount;
		e = n->elems[2];
	    }
	    m->kids[3] = n->kids[3] = n->kids[2] = NULL;
	    m->counts[3] = n->counts[3] = n->counts[2] = 0;
	    m->elems[2] = n->elems[2] = n->elems[1] = NULL;
	    if (m->kids[0]) m->kids[0]->parent = m;
	    if (m->kids[1]) m->kids[1]->parent = m;
	    if (m->kids[2]) m->kids[2]->parent = m;
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    LOG(("  left (%p): %p/%d \"%s\" %p/%d \"%s\" %p/%d\n", m,
		 m->kids[0], m->counts[0], m->elems[0],
		 m->kids[1], m->counts[1], m->elems[1],
		 m->kids[2], m->counts[2]));
	    LOG(("  right (%p): %p/%d \"%s\" %p/%d\n", n,
		 n->kids[0], n->counts[0], n->elems[0],
		 n->kids[1], n->counts[1]));
	    left = m;  lcount = countnode234(left);
	    right = n; rcount = countnode234(right);
	}
	if (n->parent)
	    ki = (n->parent->kids[0] == n ? 0 :
		  n->parent->kids[1] == n ? 1 :
		  n->parent->kids[2] == n ? 2 : 3);
	n = n->parent;
    }

    /*
     * If we've come out of here by `break', n will still be
     * non-NULL and all we need to do is go back up the tree
     * updating counts. If we've come here because n is NULL, we
     * need to create a new root for the tree because the old one
     * has just split into two. */
    if (n) {
	while (n->parent) {
	    int count = countnode234(n);
	    int childnum;
	    childnum = (n->parent->kids[0] == n ? 0 :
			n->parent->kids[1] == n ? 1 :
			n->parent->kids[2] == n ? 2 : 3);
	    n->parent->counts[childnum] = count;
	    n = n->parent;
	}
	return 0;		       /* root unchanged */
    } else {
	LOG(("  root is overloaded, split into two\n"));
	(*root) = snew(node234);
	(*root)->kids[0] = left;     (*root)->counts[0] = lcount;
	(*root)->elems[0] = e;
	(*root)->kids[1] = right;    (*root)->counts[1] = rcount;
	(*root)->elems[1] = NULL;
	(*root)->kids[2] = NULL;     (*root)->counts[2] = 0;
	(*root)->elems[2] = NULL;
	(*root)->kids[3] = NULL;     (*root)->counts[3] = 0;
	(*root)->parent = NULL;
	if ((*root)->kids[0]) (*root)->kids[0]->parent = (*root);
	if ((*root)->kids[1]) (*root)->kids[1]->parent = (*root);
	LOG(("  new root is %p/%d \"%s\" %p/%d\n",
	     (*root)->kids[0], (*root)->counts[0],
	     (*root)->elems[0],
	     (*root)->kids[1], (*root)->counts[1]));
	return 1;		       /* root moved */
    }
}

/*
 * Add an element e to a 2-3-4 tree t. Returns e on success, or if
 * an existing element compares equal, returns that.
 */
static void *add234_internal(tree234 *t, void *e, int index) {
    node234 *n;
    int ki;
    void *orig_e = e;
    int c;

    LOG(("adding element \"%s\" to tree %p\n", e, t));
    if (t->root == NULL) {
	t->root = snew(node234);
	t->root->elems[1] = t->root->elems[2] = NULL;
	t->root->kids[0] = t->root->kids[1] = NULL;
	t->root->kids[2] = t->root->kids[3] = NULL;
	t->root->counts[0] = t->root->counts[1] = 0;
	t->root->counts[2] = t->root->counts[3] = 0;
	t->root->parent = NULL;
	t->root->elems[0] = e;
	LOG(("  created root %p\n", t->root));
	return orig_e;
    }

    n = t->root;
    do {
	LOG(("  node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	     n,
	     n->kids[0], n->counts[0], n->elems[0],
	     n->kids[1], n->counts[1], n->elems[1],
	     n->kids[2], n->counts[2], n->elems[2],
	     n->kids[3], n->counts[3]));
	if (index >= 0) {
	    if (!n->kids[0]) {
		/*
		 * Leaf node. We want to insert at kid position
		 * equal to the index:
		 * 
		 *   0 A 1 B 2 C 3
		 */
		ki = index;
	    } else {
		/*
		 * Internal node. We always descend through it (add
		 * always starts at the bottom, never in the
		 * middle).
		 */
		if (index <= n->counts[0]) {
		    ki = 0;
		} else if (index -= n->counts[0] + 1, index <= n->counts[1]) {
		    ki = 1;
		} else if (index -= n->counts[1] + 1, index <= n->counts[2]) {
		    ki = 2;
		} else if (index -= n->counts[2] + 1, index <= n->counts[3]) {
		    ki = 3;
		} else
		    return NULL;       /* error: index out of range */
	    }
	} else {
	    if ((c = t->cmp(e, n->elems[0])) < 0)
		ki = 0;
	    else if (c == 0)
		return n->elems[0];	       /* already exists */
	    else if (n->elems[1] == NULL || (c = t->cmp(e, n->elems[1])) < 0)
		ki = 1;
	    else if (c == 0)
		return n->elems[1];	       /* already exists */
	    else if (n->elems[2] == NULL || (c = t->cmp(e, n->elems[2])) < 0)
		ki = 2;
	    else if (c == 0)
		return n->elems[2];	       /* already exists */
	    else
		ki = 3;
	}
	LOG(("  moving to child %d (%p)\n", ki, n->kids[ki]));
	if (!n->kids[ki])
	    break;
	n = n->kids[ki];
    } while (n);

    add234_insert(NULL, e, NULL, &t->root, n, ki);

    return orig_e;
}

void *add234(tree234 *t, void *e) {
    if (!t->cmp)		       /* tree is unsorted */
	return NULL;

    return add234_internal(t, e, -1);
}
void *addpos234(tree234 *t, void *e, int index) {
    if (index < 0 ||		       /* index out of range */
	t->cmp)			       /* tree is sorted */
	return NULL;		       /* return failure */

    return add234_internal(t, e, index);  /* this checks the upper bound */
}

/*
 * Look up the element at a given numeric index in a 2-3-4 tree.
 * Returns NULL if the index is out of range.
 */
void *index234(tree234 *t, int index) {
    node234 *n;

    if (!t->root)
	return NULL;		       /* tree is empty */

    if (index < 0 || index >= countnode234(t->root))
	return NULL;		       /* out of range */

    n = t->root;
    
    while (n) {
	if (index < n->counts[0])
	    n = n->kids[0];
	else if (index -= n->counts[0] + 1, index < 0)
	    return n->elems[0];
	else if (index < n->counts[1])
	    n = n->kids[1];
	else if (index -= n->counts[1] + 1, index < 0)
	    return n->elems[1];
	else if (index < n->counts[2])
	    n = n->kids[2];
	else if (index -= n->counts[2] + 1, index < 0)
	    return n->elems[2];
	else
	    n = n->kids[3];
    }

    /* We shouldn't ever get here. I wonder how we did. */
    return NULL;
}

/*
 * Find an element e in a sorted 2-3-4 tree t. Returns NULL if not
 * found. e is always passed as the first argument to cmp, so cmp
 * can be an asymmetric function if desired. cmp can also be passed
 * as NULL, in which case the compare function from the tree proper
 * will be used.
 */
void *findrelpos234(tree234 *t, void *e, cmpfn234 cmp,
		    int relation, int *index) {
    node234 *n;
    void *ret;
    int c;
    int idx, ecount, kcount, cmpret;

    if (t->root == NULL)
	return NULL;

    if (cmp == NULL)
	cmp = t->cmp;

    n = t->root;
    /*
     * Attempt to find the element itself.
     */
    idx = 0;
    ecount = -1;
    /*
     * Prepare a fake `cmp' result if e is NULL.
     */
    cmpret = 0;
    if (e == NULL) {
	assert(relation == REL234_LT || relation == REL234_GT);
	if (relation == REL234_LT)
	    cmpret = +1;	       /* e is a max: always greater */
	else if (relation == REL234_GT)
	    cmpret = -1;	       /* e is a min: always smaller */
    }
    while (1) {
	for (kcount = 0; kcount < 4; kcount++) {
	    if (kcount >= 3 || n->elems[kcount] == NULL ||
		(c = cmpret ? cmpret : cmp(e, n->elems[kcount])) < 0) {
		break;
	    }
	    if (n->kids[kcount]) idx += n->counts[kcount];
	    if (c == 0) {
		ecount = kcount;
		break;
	    }
	    idx++;
	}
	if (ecount >= 0)
	    break;
	if (n->kids[kcount])
	    n = n->kids[kcount];
	else
	    break;
    }

    if (ecount >= 0) {
	/*
	 * We have found the element we're looking for. It's
	 * n->elems[ecount], at tree index idx. If our search
	 * relation is EQ, LE or GE we can now go home.
	 */
	if (relation != REL234_LT && relation != REL234_GT) {
	    if (index) *index = idx;
	    return n->elems[ecount];
	}

	/*
	 * Otherwise, we'll do an indexed lookup for the previous
	 * or next element. (It would be perfectly possible to
	 * implement these search types in a non-counted tree by
	 * going back up from where we are, but far more fiddly.)
	 */
	if (relation == REL234_LT)
	    idx--;
	else
	    idx++;
    } else {
	/*
	 * We've found our way to the bottom of the tree and we
	 * know where we would insert this node if we wanted to:
	 * we'd put it in in place of the (empty) subtree
	 * n->kids[kcount], and it would have index idx
	 * 
	 * But the actual element isn't there. So if our search
	 * relation is EQ, we're doomed.
	 */
	if (relation == REL234_EQ)
	    return NULL;

	/*
	 * Otherwise, we must do an index lookup for index idx-1
	 * (if we're going left - LE or LT) or index idx (if we're
	 * going right - GE or GT).
	 */
	if (relation == REL234_LT || relation == REL234_LE) {
	    idx--;
	}
    }

    /*
     * We know the index of the element we want; just call index234
     * to do the rest. This will return NULL if the index is out of
     * bounds, which is exactly what we want.
     */
    ret = index234(t, idx);
    if (ret && index) *index = idx;
    return ret;
}
void *find234(tree234 *t, void *e, cmpfn234 cmp) {
    return findrelpos234(t, e, cmp, REL234_EQ, NULL);
}
void *findrel234(tree234 *t, void *e, cmpfn234 cmp, int relation) {
    return findrelpos234(t, e, cmp, relation, NULL);
}
void *findpos234(tree234 *t, void *e, cmpfn234 cmp, int *index) {
    return findrelpos234(t, e, cmp, REL234_EQ, index);
}

/*
 * Tree transformation used in delete and split: move a subtree
 * right, from child ki of a node to the next child. Update k and
 * index so that they still point to the same place in the
 * transformed tree. Assumes the destination child is not full, and
 * that the source child does have a subtree to spare. Can cope if
 * the destination child is undersized.
 * 
 *                . C .                     . B .
 *               /     \     ->            /     \
 * [more] a A b B c   d D e      [more] a A b   c C d D e
 * 
 *                 . C .                     . B .
 *                /     \    ->             /     \
 *  [more] a A b B c     d        [more] a A b   c C d
 */
static void trans234_subtree_right(node234 *n, int ki, int *k, int *index) {
    node234 *src, *dest;
    int i, srclen, adjust;

    src = n->kids[ki];
    dest = n->kids[ki+1];

    LOG(("  trans234_subtree_right(%p, %d):\n", n, ki));
    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    src %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 src,
	 src->kids[0], src->counts[0], src->elems[0],
	 src->kids[1], src->counts[1], src->elems[1],
	 src->kids[2], src->counts[2], src->elems[2],
	 src->kids[3], src->counts[3]));
    LOG(("    dest %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 dest,
	 dest->kids[0], dest->counts[0], dest->elems[0],
	 dest->kids[1], dest->counts[1], dest->elems[1],
	 dest->kids[2], dest->counts[2], dest->elems[2],
	 dest->kids[3], dest->counts[3]));
    /*
     * Move over the rest of the destination node to make space.
     */
    dest->kids[3] = dest->kids[2];    dest->counts[3] = dest->counts[2];
    dest->elems[2] = dest->elems[1];
    dest->kids[2] = dest->kids[1];    dest->counts[2] = dest->counts[1];
    dest->elems[1] = dest->elems[0];
    dest->kids[1] = dest->kids[0];    dest->counts[1] = dest->counts[0];

    /* which element to move over */
    i = (src->elems[2] ? 2 : src->elems[1] ? 1 : 0);

    dest->elems[0] = n->elems[ki];
    n->elems[ki] = src->elems[i];
    src->elems[i] = NULL;

    dest->kids[0] = src->kids[i+1];   dest->counts[0] = src->counts[i+1];
    src->kids[i+1] = NULL;            src->counts[i+1] = 0;

    if (dest->kids[0]) dest->kids[0]->parent = dest;

    adjust = dest->counts[0] + 1;

    n->counts[ki] -= adjust;
    n->counts[ki+1] += adjust;

    srclen = n->counts[ki];

    if (k) {
	LOG(("    before: k,index = %d,%d\n", (*k), (*index)));
	if ((*k) == ki && (*index) > srclen) {
	    (*index) -= srclen + 1;
	    (*k)++;
	} else if ((*k) == ki+1) {
	    (*index) += adjust;
	}
	LOG(("    after: k,index = %d,%d\n", (*k), (*index)));
    }

    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    src %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 src,
	 src->kids[0], src->counts[0], src->elems[0],
	 src->kids[1], src->counts[1], src->elems[1],
	 src->kids[2], src->counts[2], src->elems[2],
	 src->kids[3], src->counts[3]));
    LOG(("    dest %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 dest,
	 dest->kids[0], dest->counts[0], dest->elems[0],
	 dest->kids[1], dest->counts[1], dest->elems[1],
	 dest->kids[2], dest->counts[2], dest->elems[2],
	 dest->kids[3], dest->counts[3]));
}

/*
 * Tree transformation used in delete and split: move a subtree
 * left, from child ki of a node to the previous child. Update k
 * and index so that they still point to the same place in the
 * transformed tree. Assumes the destination child is not full, and
 * that the source child does have a subtree to spare. Can cope if
 * the destination child is undersized. 
 *
 *      . B .                             . C .
 *     /     \                ->         /     \
 *  a A b   c C d D e [more]      a A b B c   d D e [more]
 *
 *     . A .                             . B .
 *    /     \                 ->        /     \
 *   a   b B c C d [more]            a A b   c C d [more]
 */
static void trans234_subtree_left(node234 *n, int ki, int *k, int *index) {
    node234 *src, *dest;
    int i, adjust;

    src = n->kids[ki];
    dest = n->kids[ki-1];

    LOG(("  trans234_subtree_left(%p, %d):\n", n, ki));
    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    dest %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 dest,
	 dest->kids[0], dest->counts[0], dest->elems[0],
	 dest->kids[1], dest->counts[1], dest->elems[1],
	 dest->kids[2], dest->counts[2], dest->elems[2],
	 dest->kids[3], dest->counts[3]));
    LOG(("    src %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 src,
	 src->kids[0], src->counts[0], src->elems[0],
	 src->kids[1], src->counts[1], src->elems[1],
	 src->kids[2], src->counts[2], src->elems[2],
	 src->kids[3], src->counts[3]));

    /* where in dest to put it */
    i = (dest->elems[1] ? 2 : dest->elems[0] ? 1 : 0);
    dest->elems[i] = n->elems[ki-1];
    n->elems[ki-1] = src->elems[0];

    dest->kids[i+1] = src->kids[0];   dest->counts[i+1] = src->counts[0];

    if (dest->kids[i+1]) dest->kids[i+1]->parent = dest;

    /*
     * Move over the rest of the source node.
     */
    src->kids[0] = src->kids[1];      src->counts[0] = src->counts[1];
    src->elems[0] = src->elems[1];
    src->kids[1] = src->kids[2];      src->counts[1] = src->counts[2];
    src->elems[1] = src->elems[2];
    src->kids[2] = src->kids[3];      src->counts[2] = src->counts[3];
    src->elems[2] = NULL;
    src->kids[3] = NULL;              src->counts[3] = 0;

    adjust = dest->counts[i+1] + 1;

    n->counts[ki] -= adjust;
    n->counts[ki-1] += adjust;

    if (k) {
	LOG(("    before: k,index = %d,%d\n", (*k), (*index)));
	if ((*k) == ki) {
	    (*index) -= adjust;
	    if ((*index) < 0) {
		(*index) += n->counts[ki-1] + 1;
		(*k)--;
	    }
	}
	LOG(("    after: k,index = %d,%d\n", (*k), (*index)));
    }

    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    dest %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 dest,
	 dest->kids[0], dest->counts[0], dest->elems[0],
	 dest->kids[1], dest->counts[1], dest->elems[1],
	 dest->kids[2], dest->counts[2], dest->elems[2],
	 dest->kids[3], dest->counts[3]));
    LOG(("    src %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 src,
	 src->kids[0], src->counts[0], src->elems[0],
	 src->kids[1], src->counts[1], src->elems[1],
	 src->kids[2], src->counts[2], src->elems[2],
	 src->kids[3], src->counts[3]));
}

/*
 * Tree transformation used in delete and split: merge child nodes
 * ki and ki+1 of a node. Update k and index so that they still
 * point to the same place in the transformed tree. Assumes both
 * children _are_ sufficiently small.
 *
 *      . B .                .
 *     /     \     ->        |
 *  a A b   c C d      a A b B c C d
 * 
 * This routine can also cope with either child being undersized:
 * 
 *     . A .                 .
 *    /     \      ->        |
 *   a     b B c         a A b B c
 *
 *    . A .                  .
 *   /     \       ->        |
 *  a   b B c C d      a A b B c C d
 */
static void trans234_subtree_merge(node234 *n, int ki, int *k, int *index) {
    node234 *left, *right;
    int i, leftlen, rightlen, lsize, rsize;

    left = n->kids[ki];               leftlen = n->counts[ki];
    right = n->kids[ki+1];            rightlen = n->counts[ki+1];

    LOG(("  trans234_subtree_merge(%p, %d):\n", n, ki));
    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    left %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 left,
	 left->kids[0], left->counts[0], left->elems[0],
	 left->kids[1], left->counts[1], left->elems[1],
	 left->kids[2], left->counts[2], left->elems[2],
	 left->kids[3], left->counts[3]));
    LOG(("    right %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 right,
	 right->kids[0], right->counts[0], right->elems[0],
	 right->kids[1], right->counts[1], right->elems[1],
	 right->kids[2], right->counts[2], right->elems[2],
	 right->kids[3], right->counts[3]));

    assert(!left->elems[2] && !right->elems[2]);   /* neither is large! */
    lsize = (left->elems[1] ? 2 : left->elems[0] ? 1 : 0);
    rsize = (right->elems[1] ? 2 : right->elems[0] ? 1 : 0);

    left->elems[lsize] = n->elems[ki];

    for (i = 0; i < rsize+1; i++) {
	left->kids[lsize+1+i] = right->kids[i];
	left->counts[lsize+1+i] = right->counts[i];
	if (left->kids[lsize+1+i])
	    left->kids[lsize+1+i]->parent = left;
	if (i < rsize)
	    left->elems[lsize+1+i] = right->elems[i];
    }

    n->counts[ki] += rightlen + 1;

    sfree(right);

    /*
     * Move the rest of n up by one.
     */
    for (i = ki+1; i < 3; i++) {
	n->kids[i] = n->kids[i+1];
	n->counts[i] = n->counts[i+1];
    }
    for (i = ki; i < 2; i++) {
	n->elems[i] = n->elems[i+1];
    }
    n->kids[3] = NULL;
    n->counts[3] = 0;
    n->elems[2] = NULL;

    if (k) {
	LOG(("    before: k,index = %d,%d\n", (*k), (*index)));
	if ((*k) == ki+1) {
	    (*k)--;
	    (*index) += leftlen + 1;
	} else if ((*k) > ki+1) {
	    (*k)--;
	}
	LOG(("    after: k,index = %d,%d\n", (*k), (*index)));
    }

    LOG(("    parent %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 n,
	 n->kids[0], n->counts[0], n->elems[0],
	 n->kids[1], n->counts[1], n->elems[1],
	 n->kids[2], n->counts[2], n->elems[2],
	 n->kids[3], n->counts[3]));
    LOG(("    merged %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	 left,
	 left->kids[0], left->counts[0], left->elems[0],
	 left->kids[1], left->counts[1], left->elems[1],
	 left->kids[2], left->counts[2], left->elems[2],
	 left->kids[3], left->counts[3]));

}
    
/*
 * Delete an element e in a 2-3-4 tree. Does not free the element,
 * merely removes all links to it from the tree nodes.
 */
static void *delpos234_internal(tree234 *t, int index) {
    node234 *n;
    void *retval;
    int ki, i;

    retval = NULL;

    n = t->root;		       /* by assumption this is non-NULL */
    LOG(("deleting item %d from tree %p\n", index, t));
    while (1) {
	node234 *sub;

	LOG(("  node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d index=%d\n",
	     n,
	     n->kids[0], n->counts[0], n->elems[0],
	     n->kids[1], n->counts[1], n->elems[1],
	     n->kids[2], n->counts[2], n->elems[2],
	     n->kids[3], n->counts[3],
	     index));
	if (index <= n->counts[0]) {
	    ki = 0;
	} else if (index -= n->counts[0]+1, index <= n->counts[1]) {
	    ki = 1;
	} else if (index -= n->counts[1]+1, index <= n->counts[2]) {
	    ki = 2;
	} else if (index -= n->counts[2]+1, index <= n->counts[3]) {
	    ki = 3;
	} else {
	    assert(0);		       /* can't happen */
	}

	if (!n->kids[0])
	    break;		       /* n is a leaf node; we're here! */

	/*
	 * Check to see if we've found our target element. If so,
	 * we must choose a new target (we'll use the old target's
	 * successor, which will be in a leaf), move it into the
	 * place of the old one, continue down to the leaf and
	 * delete the old copy of the new target.
	 */
	if (index == n->counts[ki]) {
	    node234 *m;
	    LOG(("  found element in internal node, index %d\n", ki));
	    assert(n->elems[ki]);      /* must be a kid _before_ an element */
	    ki++; index = 0;
	    for (m = n->kids[ki]; m->kids[0]; m = m->kids[0])
		continue;
	    LOG(("  replacing with element \"%s\" from leaf node %p\n",
		 m->elems[0], m));
	    retval = n->elems[ki-1];
	    n->elems[ki-1] = m->elems[0];
	}

	/*
	 * Recurse down to subtree ki. If it has only one element,
	 * we have to do some transformation to start with.
	 */
	LOG(("  moving to subtree %d\n", ki));
	sub = n->kids[ki];
	if (!sub->elems[1]) {
	    LOG(("  subtree has only one element!\n"));
	    if (ki > 0 && n->kids[ki-1]->elems[1]) {
		/*
		 * Child ki has only one element, but child
		 * ki-1 has two or more. So we need to move a
		 * subtree from ki-1 to ki.
		 */
		trans234_subtree_right(n, ki-1, &ki, &index);
	    } else if (ki < 3 && n->kids[ki+1] &&
		       n->kids[ki+1]->elems[1]) {
		/*
		 * Child ki has only one element, but ki+1 has
		 * two or more. Move a subtree from ki+1 to ki.
		 */
		trans234_subtree_left(n, ki+1, &ki, &index);
	    } else {
		/*
		 * ki is small with only small neighbours. Pick a
		 * neighbour and merge with it.
		 */
		trans234_subtree_merge(n, ki>0 ? ki-1 : ki, &ki, &index);
		sub = n->kids[ki];

		if (!n->elems[0]) {
		    /*
		     * The root is empty and needs to be
		     * removed.
		     */
		    LOG(("  shifting root!\n"));
		    t->root = sub;
		    sub->parent = NULL;
		    sfree(n);
		    n = NULL;
		}
	    }
	}

	if (n)
	    n->counts[ki]--;
	n = sub;
    }

    /*
     * Now n is a leaf node, and ki marks the element number we
     * want to delete. We've already arranged for the leaf to be
     * bigger than minimum size, so let's just go to it.
     */
    assert(!n->kids[0]);
    if (!retval)
	retval = n->elems[ki];

    for (i = ki; i < 2 && n->elems[i+1]; i++)
	n->elems[i] = n->elems[i+1];
    n->elems[i] = NULL;

    /*
     * It's just possible that we have reduced the leaf to zero
     * size. This can only happen if it was the root - so destroy
     * it and make the tree empty.
     */
    if (!n->elems[0]) {
	LOG(("  removed last element in tree, destroying empty root\n"));
	assert(n == t->root);
	sfree(n);
	t->root = NULL;
    }

    return retval;		       /* finished! */
}
void *delpos234(tree234 *t, int index) {
    if (index < 0 || index >= countnode234(t->root))
	return NULL;
    return delpos234_internal(t, index);
}
void *del234(tree234 *t, void *e) {
    int index;
    if (!findrelpos234(t, e, NULL, REL234_EQ, &index))
	return NULL;		       /* it wasn't in there anyway */
    return delpos234_internal(t, index); /* it's there; delete it. */
}

/*
 * Join two subtrees together with a separator element between
 * them, given their relative height.
 * 
 * (Height<0 means the left tree is shorter, >0 means the right
 * tree is shorter, =0 means (duh) they're equal.)
 * 
 * It is assumed that any checks needed on the ordering criterion
 * have _already_ been done.
 * 
 * The value returned in `height' is 0 or 1 depending on whether the
 * resulting tree is the same height as the original larger one, or
 * one higher.
 */
static node234 *join234_internal(node234 *left, void *sep,
				 node234 *right, int *height) {
    node234 *root, *node;
    int relht = *height;
    int ki;

    LOG(("  join: joining %p \"%s\" %p, relative height is %d\n",
	 left, sep, right, relht));
    if (relht == 0) {
	/*
	 * The trees are the same height. Create a new one-element
	 * root containing the separator and pointers to the two
	 * nodes.
	 */
	node234 *newroot;
	newroot = snew(node234);
	newroot->kids[0] = left;     newroot->counts[0] = countnode234(left);
	newroot->elems[0] = sep;
	newroot->kids[1] = right;    newroot->counts[1] = countnode234(right);
	newroot->elems[1] = NULL;
	newroot->kids[2] = NULL;     newroot->counts[2] = 0;
	newroot->elems[2] = NULL;
	newroot->kids[3] = NULL;     newroot->counts[3] = 0;
	newroot->parent = NULL;
	if (left) left->parent = newroot;
	if (right) right->parent = newroot;
	*height = 1;
	LOG(("  join: same height, brand new root\n"));
	return newroot;
    }

    /*
     * This now works like the addition algorithm on the larger
     * tree. We're replacing a single kid pointer with two kid
     * pointers separated by an element; if that causes the node to
     * overload, we split it in two, move a separator element up to
     * the next node, and repeat.
     */
    if (relht < 0) {
	/*
	 * Left tree is shorter. Search down the right tree to find
	 * the pointer we're inserting at.
	 */
	node = root = right;
	while (++relht < 0) {
	    node = node->kids[0];
	}
	ki = 0;
	right = node->kids[ki];
    } else {
	/*
	 * Right tree is shorter; search down the left to find the
	 * pointer we're inserting at.
	 */
	node = root = left;
	while (--relht > 0) {
	    if (node->elems[2])
		node = node->kids[3];
	    else if (node->elems[1])
		node = node->kids[2];
	    else
		node = node->kids[1];
	}
	if (node->elems[2])
	    ki = 3;
	else if (node->elems[1])
	    ki = 2;
	else
	    ki = 1;
	left = node->kids[ki];
    }

    /*
     * Now proceed as for addition.
     */
    *height = add234_insert(left, sep, right, &root, node, ki);

    return root;
}
int height234(tree234 *t) {
    int level = 0;
    node234 *n = t->root;
    while (n) {
	level++;
	n = n->kids[0];
    }
    return level;
}
tree234 *join234(tree234 *t1, tree234 *t2) {
    int size2 = countnode234(t2->root);
    if (size2 > 0) {
	void *element;
	int relht;

	if (t1->cmp) {
	    element = index234(t2, 0);
	    element = findrelpos234(t1, element, NULL, REL234_GE, NULL);
	    if (element)
		return NULL;
	}

	element = delpos234(t2, 0);
	relht = height234(t1) - height234(t2);
	t1->root = join234_internal(t1->root, element, t2->root, &relht);
	t2->root = NULL;
    }
    return t1;
}
tree234 *join234r(tree234 *t1, tree234 *t2) {
    int size1 = countnode234(t1->root);
    if (size1 > 0) {
	void *element;
	int relht;

	if (t2->cmp) {
	    element = index234(t1, size1-1);
	    element = findrelpos234(t2, element, NULL, REL234_LE, NULL);
	    if (element)
		return NULL;
	}

	element = delpos234(t1, size1-1);
	relht = height234(t1) - height234(t2);
	t2->root = join234_internal(t1->root, element, t2->root, &relht);
	t1->root = NULL;
    }
    return t2;
}

/*
 * Split out the first <index> elements in a tree and return a
 * pointer to the root node. Leave the root node of the remainder
 * in t.
 */
static node234 *split234_internal(tree234 *t, int index) {
    node234 *halves[2] = { NULL, NULL }, *n, *sib, *sub;
    node234 *lparent, *rparent;
    int ki, pki, i, half, lcount, rcount;

    n = t->root;
    LOG(("splitting tree %p at point %d\n", t, index));

    /*
     * Easy special cases. After this we have also dealt completely
     * with the empty-tree case and we can assume the root exists.
     */
    if (index == 0)		       /* return nothing */
	return NULL;
    if (index == countnode234(t->root)) {   /* return the whole tree */
	node234 *ret = t->root;
	t->root = NULL;
	return ret;
    }

    /*
     * Search down the tree to find the split point.
     */
    halves[0] = halves[1] = NULL;
    lparent = rparent = NULL;
    pki = -1;
    while (n) {
	LOG(("  node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d index=%d\n",
	     n,
	     n->kids[0], n->counts[0], n->elems[0],
	     n->kids[1], n->counts[1], n->elems[1],
	     n->kids[2], n->counts[2], n->elems[2],
	     n->kids[3], n->counts[3],
	     index));
	lcount = index;
	rcount = countnode234(n) - lcount;
	if (index <= n->counts[0]) {
	    ki = 0;
	} else if (index -= n->counts[0]+1, index <= n->counts[1]) {
	    ki = 1;
	} else if (index -= n->counts[1]+1, index <= n->counts[2]) {
	    ki = 2;
	} else {
	    index -= n->counts[2]+1;
	    ki = 3;
	}

	LOG(("  splitting at subtree %d\n", ki));
	sub = n->kids[ki];

	LOG(("  splitting at child index %d\n", ki));

	/*
	 * Split the node, put halves[0] on the right of the left
	 * one and halves[1] on the left of the right one, put the
	 * new node pointers in halves[0] and halves[1], and go up
	 * a level.
	 */
	sib = snew(node234);
	for (i = 0; i < 3; i++) {
	    if (i+ki < 3 && n->elems[i+ki]) {
		sib->elems[i] = n->elems[i+ki];
		sib->kids[i+1] = n->kids[i+ki+1];
		if (sib->kids[i+1]) sib->kids[i+1]->parent = sib;
		sib->counts[i+1] = n->counts[i+ki+1];
		n->elems[i+ki] = NULL;
		n->kids[i+ki+1] = NULL;
		n->counts[i+ki+1] = 0;
	    } else {
		sib->elems[i] = NULL;
		sib->kids[i+1] = NULL;
		sib->counts[i+1] = 0;
	    }
	}
	if (lparent) {
	    lparent->kids[pki] = n;
	    lparent->counts[pki] = lcount;
	    n->parent = lparent;
	    rparent->kids[0] = sib;
	    rparent->counts[0] = rcount;
	    sib->parent = rparent;
	} else {
	    halves[0] = n;
	    n->parent = NULL;
	    halves[1] = sib;
	    sib->parent = NULL;
	}
	lparent = n;
	rparent = sib;
	pki = ki;
	LOG(("  left node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	     n,
	     n->kids[0], n->counts[0], n->elems[0],
	     n->kids[1], n->counts[1], n->elems[1],
	     n->kids[2], n->counts[2], n->elems[2],
	     n->kids[3], n->counts[3]));
	LOG(("  right node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
	     sib,
	     sib->kids[0], sib->counts[0], sib->elems[0],
	     sib->kids[1], sib->counts[1], sib->elems[1],
	     sib->kids[2], sib->counts[2], sib->elems[2],
	     sib->kids[3], sib->counts[3]));

	n = sub;
    }

    /*
     * We've come off the bottom here, so we've successfully split
     * the tree into two equally high subtrees. The only problem is
     * that some of the nodes down the fault line will be smaller
     * than the minimum permitted size. (Since this is a 2-3-4
     * tree, that means they'll be zero-element one-child nodes.)
     */
    LOG(("  fell off bottom, lroot is %p, rroot is %p\n",
	 halves[0], halves[1]));
    assert(halves[0] != NULL);
    assert(halves[1] != NULL);
    lparent->counts[pki] = rparent->counts[0] = 0;
    lparent->kids[pki] = rparent->kids[0] = NULL;

    /*
     * So now we go back down the tree from each of the two roots,
     * fixing up undersize nodes.
     */
    for (half = 0; half < 2; half++) {
	/*
	 * Remove the root if it's undersize (it will contain only
	 * one child pointer, so just throw it away and replace it
	 * with its child). This might happen several times.
	 */
	while (halves[half] && !halves[half]->elems[0]) {
	    LOG(("  root %p is undersize, throwing away\n", halves[half]));
	    halves[half] = halves[half]->kids[0];
	    sfree(halves[half]->parent);
	    halves[half]->parent = NULL;
	    LOG(("  new root is %p\n", halves[half]));
	}

	n = halves[half];
	while (n) {
	    void (*toward)(node234 *n, int ki, int *k, int *index);
	    int ni, merge;

	    /*
	     * Now we have a potentially undersize node on the
	     * right (if half==0) or left (if half==1). Sort it
	     * out, by merging with a neighbour or by transferring
	     * subtrees over. At this time we must also ensure that
	     * nodes are bigger than minimum, in case we need an
	     * element to merge two nodes below.
	     */
	    LOG(("  node %p: %p/%d \"%s\" %p/%d \"%s\" %p/%d \"%s\" %p/%d\n",
		 n,
		 n->kids[0], n->counts[0], n->elems[0],
		 n->kids[1], n->counts[1], n->elems[1],
		 n->kids[2], n->counts[2], n->elems[2],
		 n->kids[3], n->counts[3]));
	    if (half == 1) {
		ki = 0;		       /* the kid we're interested in */
		ni = 1;		       /* the neighbour */
		merge = 0;	       /* for merge: leftmost of the two */
		toward = trans234_subtree_left;
	    } else {
		ki = (n->kids[3] ? 3 : n->kids[2] ? 2 : 1);
		ni = ki-1;
		merge = ni;
		toward = trans234_subtree_right;
	    }

	    sub = n->kids[ki];
	    if (sub && !sub->elems[1]) {
		/*
		 * This node is undersized or minimum-size. If we
		 * can merge it with its neighbour, we do so;
		 * otherwise we must be able to transfer subtrees
		 * over to it until it is greater than minimum
		 * size.
		 */
		bool undersized = (!sub->elems[0]);
		LOG(("  child %d is %ssize\n", ki,
		     undersized ? "under" : "minimum-"));
		LOG(("  neighbour is %s\n",
		     n->kids[ni]->elems[2] ? "large" :
		     n->kids[ni]->elems[1] ? "medium" : "small"));
		if (!n->kids[ni]->elems[1] ||
		    (undersized && !n->kids[ni]->elems[2])) {
		    /*
		     * Neighbour is small, or possibly neighbour is
		     * medium and we are undersize.
		     */
		    trans234_subtree_merge(n, merge, NULL, NULL);
		    sub = n->kids[merge];
		    if (!n->elems[0]) {
			/*
			 * n is empty, and hence must have been the
			 * root and needs to be removed.
			 */
			assert(!n->parent);
			LOG(("  shifting root!\n"));
			halves[half] = sub;
			halves[half]->parent = NULL;
			sfree(n);
		    }
		} else {
		    /* Neighbour is big enough to move trees over. */
		    toward(n, ni, NULL, NULL);
		    if (undersized)
			toward(n, ni, NULL, NULL);
		}
	    }
	    n = sub;
	}
    }

    t->root = halves[1];
    return halves[0];
}
tree234 *splitpos234(tree234 *t, int index, bool before) {
    tree234 *ret;
    node234 *n;
    int count;

    count = countnode234(t->root);
    if (index < 0 || index > count)
	return NULL;		       /* error */
    ret = newtree234(t->cmp);
    n = split234_internal(t, index);
    if (before) {
	/* We want to return the ones before the index. */
	ret->root = n;
    } else {
	/*
	 * We want to keep the ones before the index and return the
	 * ones after.
	 */
	ret->root = t->root;
	t->root = n;
    }
    return ret;
}
tree234 *split234(tree234 *t, void *e, cmpfn234 cmp, int rel) {
    bool before;
    int index;

    assert(rel != REL234_EQ);

    if (rel == REL234_GT || rel == REL234_GE) {
	before = true;
	rel = (rel == REL234_GT ? REL234_LE : REL234_LT);
    } else {
	before = false;
    }
    if (!findrelpos234(t, e, cmp, rel, &index))
	index = 0;

    return splitpos234(t, index+1, before);
}

static node234 *copynode234(node234 *n, copyfn234 copyfn, void *copyfnstate) {
    int i;
    node234 *n2 = snew(node234);

    for (i = 0; i < 3; i++) {
	if (n->elems[i] && copyfn)
	    n2->elems[i] = copyfn(copyfnstate, n->elems[i]);
	else
	    n2->elems[i] = n->elems[i];
    }

    for (i = 0; i < 4; i++) {
	if (n->kids[i]) {
	    n2->kids[i] = copynode234(n->kids[i], copyfn, copyfnstate);
	    n2->kids[i]->parent = n2;
	} else {
	    n2->kids[i] = NULL;
	}
	n2->counts[i] = n->counts[i];
    }

    return n2;
}
tree234 *copytree234(tree234 *t, copyfn234 copyfn, void *copyfnstate) {
    tree234 *t2;

    t2 = newtree234(t->cmp);
    if (t->root) {
	t2->root = copynode234(t->root, copyfn, copyfnstate);
	t2->root->parent = NULL;
    } else
	t2->root = NULL;

    return t2;
}
