/*
 * Edmonds-Karp algorithm for finding a maximum flow and minimum
 * cut in a network. Almost identical to the Ford-Fulkerson
 * algorithm, but apparently using breadth-first search to find the
 * _shortest_ augmenting path is a good way to guarantee
 * termination and ensure the time complexity is not dependent on
 * the actual value of the maximum flow. I don't understand why
 * that should be, but it's claimed on the Internet that it's been
 * proved, and that's good enough for me. I prefer BFS to DFS
 * anyway :-)
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "maxflow.h"

#include "puzzles.h"		       /* for snewn/sfree */

int maxflow_with_scratch(void *scratch, int nv, int source, int sink,
			 int ne, const int *edges, const int *backedges,
			 const int *capacity, int *flow, int *cut)
{
    int *todo = (int *)scratch;
    int *prev = todo + nv;
    int *firstedge = todo + 2*nv;
    int *firstbackedge = todo + 3*nv;
    int i, j, head, tail, from, to;
    int totalflow;

    /*
     * Scan the edges array to find the index of the first edge
     * from each node.
     */
    j = 0;
    for (i = 0; i < ne; i++)
	while (j <= edges[2*i])
	    firstedge[j++] = i;
    while (j < nv)
	firstedge[j++] = ne;
    assert(j == nv);

    /*
     * Scan the backedges array to find the index of the first edge
     * _to_ each node.
     */
    j = 0;
    for (i = 0; i < ne; i++)
	while (j <= edges[2*backedges[i]+1])
	    firstbackedge[j++] = i;
    while (j < nv)
	firstbackedge[j++] = ne;
    assert(j == nv);

    /*
     * Start the flow off at zero on every edge.
     */
    for (i = 0; i < ne; i++)
	flow[i] = 0;
    totalflow = 0;

    /*
     * Repeatedly look for an augmenting path, and follow it.
     */
    while (1) {

	/*
	 * Set up the prev array.
	 */
	for (i = 0; i < nv; i++)
	    prev[i] = -1;

	/*
	 * Initialise the to-do list for BFS.
	 */
	head = tail = 0;
	todo[tail++] = source;

	/*
	 * Now do the BFS loop.
	 */
	while (head < tail && prev[sink] <= 0) {
	    from = todo[head++];

	    /*
	     * Try all the forward edges out of node `from'. For a
	     * forward edge to be valid, it must have flow
	     * currently less than its capacity.
	     */
	    for (i = firstedge[from]; i < ne && edges[2*i] == from; i++) {
		to = edges[2*i+1];
		if (to == source || prev[to] >= 0)
		    continue;
		if (capacity[i] >= 0 && flow[i] >= capacity[i])
		    continue;
		/*
		 * This is a valid augmenting edge. Visit node `to'.
		 */
		prev[to] = 2*i;
		todo[tail++] = to;
	    }

	    /*
	     * Try all the backward edges into node `from'. For a
	     * backward edge to be valid, it must have flow
	     * currently greater than zero.
	     */
	    for (i = firstbackedge[from];
		 j = backedges[i], i < ne && edges[2*j+1]==from; i++) {
		to = edges[2*j];
		if (to == source || prev[to] >= 0)
		    continue;
		if (flow[j] <= 0)
		    continue;
		/*
		 * This is a valid augmenting edge. Visit node `to'.
		 */
		prev[to] = 2*j+1;
		todo[tail++] = to;
	    }
	}

	/*
	 * If prev[sink] is non-null, we have found an augmenting
	 * path.
	 */
	if (prev[sink] >= 0) {
	    int max;

	    /*
	     * Work backwards along the path figuring out the
	     * maximum flow we can add.
	     */
	    to = sink;
	    max = -1;
	    while (to != source) {
		int spare;

		/*
		 * Find the edge we're currently moving along.
		 */
		i = prev[to];
		from = edges[i];
		assert(from != to);

		/*
		 * Determine the spare capacity of this edge.
		 */
		if (i & 1)
		    spare = flow[i / 2];   /* backward edge */
		else if (capacity[i / 2] >= 0)
		    spare = capacity[i / 2] - flow[i / 2];   /* forward edge */
		else
		    spare = -1;	       /* unlimited forward edge */

		assert(spare != 0);

		if (max < 0 || (spare >= 0 && spare < max))
		    max = spare;

		to = from;
	    }
	    /*
	     * Fail an assertion if max is still < 0, i.e. there is
	     * an entirely unlimited path from source to sink. Also
	     * max should not _be_ zero, because by construction
	     * this _should_ be an augmenting path.
	     */
	    assert(max > 0);

	    /*
	     * Now work backwards along the path again, this time
	     * actually adjusting the flow.
	     */
	    to = sink;
	    while (to != source) {
		/*
		 * Find the edge we're currently moving along.
		 */
		i = prev[to];
		from = edges[i];
		assert(from != to);

		/*
		 * Adjust the edge.
		 */
		if (i & 1)
		    flow[i / 2] -= max;  /* backward edge */
		else
		    flow[i / 2] += max;  /* forward edge */

		to = from;
	    }

	    /*
	     * And adjust the overall flow counter.
	     */
	    totalflow += max;

	    continue;
	}

	/*
	 * If we reach here, we have failed to find an augmenting
	 * path, which means we're done. Output the `cut' array if
	 * required, and leave.
	 */
	if (cut) {
	    for (i = 0; i < nv; i++) {
		if (i == source || prev[i] >= 0)
		    cut[i] = 0;
		else
		    cut[i] = 1;
	    }
	}
	return totalflow;
    }
}

int maxflow_scratch_size(int nv)
{
    return (nv * 4) * sizeof(int);
}

void maxflow_setup_backedges(int ne, const int *edges, int *backedges)
{
    int i, n;

    for (i = 0; i < ne; i++)
	backedges[i] = i;

    /*
     * We actually can't use the C qsort() function, because we'd
     * need to pass `edges' as a context parameter to its
     * comparator function. So instead I'm forced to implement my
     * own sorting algorithm internally, which is a pest. I'll use
     * heapsort, because I like it.
     */

#define LESS(i,j) ( (edges[2*(i)+1] < edges[2*(j)+1]) || \
		    (edges[2*(i)+1] == edges[2*(j)+1] && \
		     edges[2*(i)] < edges[2*(j)]) )
#define PARENT(n) ( ((n)-1)/2 )
#define LCHILD(n) ( 2*(n)+1 )
#define RCHILD(n) ( 2*(n)+2 )
#define SWAP(i,j) do { int swaptmp = (i); (i) = (j); (j) = swaptmp; } while (0)

    /*
     * Phase 1: build the heap. We want the _largest_ element at
     * the top.
     */
    n = 0;
    while (n < ne) {
	n++;

	/*
	 * Swap element n with its parent repeatedly to preserve
	 * the heap property.
	 */
	i = n-1;

	while (i > 0) {
	    int p = PARENT(i);

	    if (LESS(backedges[p], backedges[i])) {
		SWAP(backedges[p], backedges[i]);
		i = p;
	    } else
		break;
	}
    }

    /*
     * Phase 2: repeatedly remove the largest element and stick it
     * at the top of the array.
     */
    while (n > 0) {
	/*
	 * The largest element is at position 0. Put it at the top,
	 * and swap the arbitrary element from that position into
	 * position 0.
	 */
	n--;
	SWAP(backedges[0], backedges[n]);

	/*
	 * Now repeatedly move that arbitrary element down the heap
	 * by swapping it with the more suitable of its children.
	 */
	i = 0;
	while (1) {
	    int lc, rc;

	    lc = LCHILD(i);
	    rc = RCHILD(i);

	    if (lc >= n)
		break;		       /* we've hit bottom */

	    if (rc >= n) {
		/*
		 * Special case: there is only one child to check.
		 */
		if (LESS(backedges[i], backedges[lc]))
		    SWAP(backedges[i], backedges[lc]);

		/* _Now_ we've hit bottom. */
		break;
	    } else {
		/*
		 * The common case: there are two children and we
		 * must check them both.
		 */
		if (LESS(backedges[i], backedges[lc]) ||
		    LESS(backedges[i], backedges[rc])) {
		    /*
		     * Pick the more appropriate child to swap with
		     * (i.e. the one which would want to be the
		     * parent if one were above the other - as one
		     * is about to be).
		     */
		    if (LESS(backedges[lc], backedges[rc])) {
			SWAP(backedges[i], backedges[rc]);
			i = rc;
		    } else {
			SWAP(backedges[i], backedges[lc]);
			i = lc;
		    }
		} else {
		    /* This element is in the right place; we're done. */
		    break;
		}
	    }
	}
    }

#undef LESS
#undef PARENT
#undef LCHILD
#undef RCHILD
#undef SWAP

}

int maxflow(int nv, int source, int sink,
	    int ne, const int *edges, const int *capacity,
	    int *flow, int *cut)
{
    void *scratch;
    int *backedges;
    int size;
    int ret;

    /*
     * Allocate the space.
     */
    size = ne * sizeof(int) + maxflow_scratch_size(nv);
    backedges = smalloc(size);
    if (!backedges)
	return -1;
    scratch = backedges + ne;

    /*
     * Set up the backedges array.
     */
    maxflow_setup_backedges(ne, edges, backedges);

    /*
     * Call the main function.
     */
    ret = maxflow_with_scratch(scratch, nv, source, sink, ne, edges,
			       backedges, capacity, flow, cut);

    /*
     * Free the scratch space.
     */
    sfree(backedges);

    /*
     * And we're done.
     */
    return ret;
}

#ifdef TESTMODE

#define MAXEDGES 256
#define MAXVERTICES 128
#define ADDEDGE(i,j) do{edges[ne*2] = (i); edges[ne*2+1] = (j); ne++;}while(0)

int compare_edge(const void *av, const void *bv)
{
    const int *a = (const int *)av;
    const int *b = (const int *)bv;

    if (a[0] < b[0])
	return -1;
    else if (a[0] > b[0])
	return +1;
    else if (a[1] < b[1])
	return -1;
    else if (a[1] > b[1])
	return +1;
    else
	return 0;
}

int main(void)
{
    int edges[MAXEDGES*2], ne, nv;
    int capacity[MAXEDGES], flow[MAXEDGES], cut[MAXVERTICES];
    int source, sink, p, q, i, j, ret;

    /*
     * Use this algorithm to find a maximal complete matching in a
     * bipartite graph.
     */
    ne = 0;
    nv = 0;
    source = nv++;
    p = nv;
    nv += 5;
    q = nv;
    nv += 5;
    sink = nv++;
    for (i = 0; i < 5; i++) {
	capacity[ne] = 1;
	ADDEDGE(source, p+i);
    }
    for (i = 0; i < 5; i++) {
	capacity[ne] = 1;
	ADDEDGE(q+i, sink);
    }
    j = ne;
    capacity[ne] = 1; ADDEDGE(p+0,q+0);
    capacity[ne] = 1; ADDEDGE(p+1,q+0);
    capacity[ne] = 1; ADDEDGE(p+1,q+1);
    capacity[ne] = 1; ADDEDGE(p+2,q+1);
    capacity[ne] = 1; ADDEDGE(p+2,q+2);
    capacity[ne] = 1; ADDEDGE(p+3,q+2);
    capacity[ne] = 1; ADDEDGE(p+3,q+3);
    capacity[ne] = 1; ADDEDGE(p+4,q+3);
    /* capacity[ne] = 1; ADDEDGE(p+2,q+4); */
    qsort(edges, ne, 2*sizeof(int), compare_edge);

    ret = maxflow(nv, source, sink, ne, edges, capacity, flow, cut);

    printf("ret = %d\n", ret);

    for (i = 0; i < ne; i++)
	printf("flow %d: %d -> %d\n", flow[i], edges[2*i], edges[2*i+1]);

    for (i = 0; i < nv; i++)
	if (cut[i] == 0)
	    printf("difficult set includes %d\n", i);

    return 0;
}

#endif
