/*
 * Routine for finding loops in graphs, reusable across multiple
 * puzzles.
 *
 * The strategy is Tarjan's bridge-finding algorithm, which is
 * designed to list all edges whose removal would disconnect a
 * previously connected component of the graph. We're interested in
 * exactly the reverse - edges that are part of a loop in the graph
 * are precisely those which _wouldn't_ disconnect anything if removed
 * (individually) - but of course flipping the sense of the output is
 * easy.
 *
 * For some fun background reading about all the _wrong_ ways the
 * Puzzles code base has tried to solve this problem in the past:
 * https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/findloop/
 *
 * The specific variant of Tarjan's algorithm we use is the one from
 * https://mathstodon.xyz/@abacabadabacaba@infosec.exchange/113113280480134188
 */

#include "puzzles.h"

struct findloopstate {
    int depth, shallowest_reachable, subtree_size;
    int parent, component_root;
    int prev, next;
};

struct findloopstate *findloop_new_state(int nvertices)
{
    return snewn(nvertices, struct findloopstate);
}

void findloop_free_state(struct findloopstate *state)
{
    sfree(state);
}

bool findloop_is_loop_edge(struct findloopstate *pv, int u, int v)
{
    /*
     * In the DFS-built forest, all edges are either are from parent
     * to child or from child to ancestor.
     *
     * Back-edges to ancestors must be parts of loops. In order to
     * detect whether a parent-to-child edge is part of a loop, we
     * check if any ancestor is reachable from that child's subtree.
     */
    if (pv[u].parent == v && pv[u].shallowest_reachable >= pv[u].depth)
	return false;
    if (pv[v].parent == u && pv[v].shallowest_reachable >= pv[v].depth)
	return false;
    return true;
}

static bool findloop_is_bridge_oneway(
    struct findloopstate *pv, int u, int v, int *u_vertices, int *v_vertices)
{
    if (pv[u].parent != v)
	return false;
    if (pv[u].shallowest_reachable < pv[u].depth)
	return false;

    if (u_vertices)
	*u_vertices = pv[u].subtree_size;
    if (v_vertices) {
	int r = pv[u].component_root;
	*v_vertices = pv[r].subtree_size - pv[u].subtree_size;
    }

    return true;
}

bool findloop_is_bridge(
    struct findloopstate *pv, int u, int v, int *u_vertices, int *v_vertices)
{
    return (findloop_is_bridge_oneway(pv, u, v, u_vertices, v_vertices) ||
	    findloop_is_bridge_oneway(pv, v, u, v_vertices, u_vertices));
}

bool findloop_run(struct findloopstate *pv, int nvertices,
		  neighbour_fn_t neighbour, void *ctx)
{
    int u, v, w;
    bool any_loop = false;

    /*
     * We run a DFS to split the graph into disjoint spanning trees.
     * This construction guarantees that every edge either descends
     * to a new child never visited before or goes to an ancestor.
     * Tracking which ancestors are linked from which subtrees lets
     * us detect all loops efficiently.
     *
     * Here we don't use recursion, instead holding the entire DSF
     * state in the findloopstate struct. The loop below visits each
     * node exactly twice: before and after visiting its subtree.
     *
     * The first time we visit a node, we take care of marking its
     * children with their position in the tree and when they're
     * scheduled to be visited. The second time we update the parent
     * with statistics about the subtree.
     *
     * The order of nodes is managed using a doubly-linked list.
     * The first time a node is visited, we add its children before it
     * in the list and set the pointer to go through them first. The
     * second time we move to the next node in the list, which is a
     * sibling or a parent, or if we're at the root of the connected
     * component will be a node in the next component. (All the nodes
     * of the graph always appear in the list)
     * A linked list is used to handle the case where the same child
     * appears in two levels, to allow us to efficiently remove it from
     * its previous position.
     *
     * The algorithm tracks whether we're at the first or the second
     * visit at a node using the depth property. It's set to a negative
     * value on initialization, and to the depth in the connected
     * component's tree on the first visit.
     * It detects a new connected component using the parent pointer,
     * it's always set to a real node in the search, and is negative for
     * new trees.
     *
     * In the first visit, we go over the node's children, moving them
     * in the list and setting their parent pointer. Edges going to
     * ancestors are noted in the shallowest_reachable field.
     * In the second visit, we adjust the subtree_size and
     * shallowest_reachable fields of the parent.
     *
     * Variables:
     *   u = the current node under examination
     *   v = the node to go to in the next iteration
     *   w = neighbour iterator
     */

    for (u = 0; u < nvertices; u++) {
	pv[u].depth = -1;
	pv[u].shallowest_reachable = nvertices;
	pv[u].subtree_size = 1;
	pv[u].parent = -1;
	pv[u].component_root = u;
	pv[u].prev = u - 1;
	pv[u].next = (u == nvertices - 1) ? -1 : u + 1;
    }

    debug(("------------- new find_loops, nvertices=%d\n", nvertices));

    v = 0;
    while (v != -1) {
	u = v;
	if (pv[u].depth < 0) {
	    /* Our first visit to the node (on the way down the search) */
	    if (pv[u].parent < 0) {
		debug(("    new component: processing %d\n", u));
		pv[u].depth = 0;
		pv[u].component_root = u;
	    } else {
		debug(("    processing %d\n", u));
		pv[u].depth = pv[pv[u].parent].depth + 1;
		pv[u].component_root = pv[pv[u].parent].component_root;
	    }

	    /* Schedule visits to the neighbors, and then back here */
	    v = u;
	    for (w = neighbour(u, ctx); w >= 0; w = neighbour(-1, ctx)) {
		if (w == pv[u].parent)
		    continue;
		if (pv[w].depth < 0) {
		    debug(("    adding edge %d-%d to tree\n", u, w));
		    pv[w].parent = u;
		    /* Remove the neighbour from the linked list */
		    if (pv[w].prev >= 0)
			pv[pv[w].prev].next = pv[w].next;
		    if (pv[w].next >= 0)
			pv[pv[w].next].prev = pv[w].prev;
		    /* Add it to the start of the list */
		    pv[w].prev = pv[v].prev;
		    pv[w].next = v;
		    if (pv[v].prev >= 0)
			pv[pv[v].prev].next = w;
		    pv[v].prev = w;
		    /* Mark this as the next node to visit */
		    v = w;
		} else {
		    debug(("    found back-edge %d-%d\n", u, w));
		    pv[u].shallowest_reachable =
			min(pv[u].shallowest_reachable, pv[w].depth);
		    any_loop = true;
		}
	    }
	} else {
	    debug(("    wrapping up %d. |subtree| = %d, min(reachable) = %d\n",
		   u, pv[u].subtree_size, pv[u].shallowest_reachable));
	    if (pv[u].parent >= 0) {
		if (pv[u].shallowest_reachable >= pv[u].depth) {
		    debug(("    bridge: %d-%d\n", u, pv[u].parent));
		}
		pv[pv[u].parent].subtree_size += pv[u].subtree_size;
		pv[pv[u].parent].shallowest_reachable =
		    min(pv[pv[u].parent].shallowest_reachable,
			pv[u].shallowest_reachable);
	    }
	    v = pv[u].next;
	}
    }

    return any_loop;
}
