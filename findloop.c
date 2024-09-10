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
 */

#include "puzzles.h"

struct findloopstate {
    int parent, child, sibling, component_root;
    bool visited;
    int index, minindex, maxindex;
    int minreachable, maxreachable;
    int bridge;
};

struct findloopstate *findloop_new_state(int nvertices)
{
    /*
     * Allocate a findloopstate structure for each vertex, and one
     * extra one at the end which will be the overall root of a
     * 'super-tree', which links the whole graph together to make it
     * as easy as possible to iterate over all the connected
     * components.
     */
    return snewn(nvertices + 1, struct findloopstate);
}

void findloop_free_state(struct findloopstate *state)
{
    sfree(state);
}

bool findloop_is_loop_edge(struct findloopstate *pv, int u, int v)
{
    /*
     * Since the algorithm is intended for finding bridges, and a
     * bridge must be part of any spanning tree, it follows that there
     * is at most one bridge per vertex.
     *
     * Furthermore, by finding a _rooted_ spanning tree (so that each
     * bridge is a parent->child link), you can find an injection from
     * bridges to vertices (namely, map each bridge to the vertex at
     * its child end).
     *
     * So if the u-v edge is a bridge, then either v was u's parent
     * when the algorithm ran and we set pv[u].bridge = v, or vice
     * versa.
     */
    return !(pv[u].bridge == v || pv[v].bridge == u);
}

static bool findloop_is_bridge_oneway(
    struct findloopstate *pv, int u, int v, int *u_vertices, int *v_vertices)
{
    int r, total, below;

    if (pv[u].bridge != v)
        return false;

    r = pv[u].component_root;
    total = pv[r].maxindex - pv[r].minindex + 1;
    below = pv[u].maxindex - pv[u].minindex + 1;

    if (u_vertices)
        *u_vertices = below;
    if (v_vertices)
        *v_vertices = total - below;

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
    int u, v, w, root, index;
    int nbridges, nedges;

    root = nvertices;

    /*
     * First pass: organise the graph into a rooted spanning forest.
     * That is, a tree structure with a clear up/down orientation -
     * every node has exactly one parent (which may be 'root') and
     * zero or more children, and every parent-child link corresponds
     * to a graph edge.
     *
     * (A side effect of this is to find all the connected components,
     * which of course we could do less confusingly with a dsf - but
     * then we'd have to do that *and* build the tree, so it's less
     * effort to do it all at once.)
     */
    for (v = 0; v <= nvertices; v++) {
        pv[v].parent = root;
        pv[v].child = -2;
        pv[v].sibling = -1;
        pv[v].visited = false;
    }
    pv[root].child = -1;
    nedges = 0;
    debug(("------------- new find_loops, nvertices=%d\n", nvertices));
    for (v = 0; v < nvertices; v++) {
        if (pv[v].parent == root) {
            /*
             * Found a new connected component. Enumerate and treeify
             * it.
             */
            pv[v].sibling = pv[root].child;
            pv[root].child = v;
            pv[v].component_root = v;
            debug(("%d is new child of root\n", v));

            u = v;
            while (1) {
                if (!pv[u].visited) {
                    pv[u].visited = true;

                    /*
                     * Enumerate the neighbours of u, and any that are
                     * as yet not in the tree structure (indicated by
                     * child==-2, and distinct from the 'visited'
                     * flag) become children of u.
                     */
                    debug(("  component pass: processing %d\n", u));
                    for (w = neighbour(u, ctx); w >= 0;
                         w = neighbour(-1, ctx)) {
                        debug(("    edge %d-%d\n", u, w));
                        if (pv[w].child == -2) {
                            debug(("      -> new child\n"));
                            pv[w].child = -1;
                            pv[w].sibling = pv[u].child;
                            pv[w].parent = u;
                            pv[w].component_root = pv[u].component_root;
                            pv[u].child = w;
                        }

                        /* While we're here, count the edges in the whole
                         * graph, so that we can easily check at the end
                         * whether all of them are bridges, i.e. whether
                         * no loop exists at all. */
                        if (w > u) /* count each edge only in one direction */
                            nedges++;
                    }

                    /*
                     * Now descend in depth-first search.
                     */
                    if (pv[u].child >= 0) {
                        u = pv[u].child;
                        debug(("    descending to %d\n", u));
                        continue;
                    }
                }

                if (u == v) {
                    debug(("      back at %d, done this component\n", u));
                    break;
                } else if (pv[u].sibling >= 0) {
                    u = pv[u].sibling;
                    debug(("    sideways to %d\n", u));
                } else {
                    u = pv[u].parent;
                    debug(("    ascending to %d\n", u));
                }
            }
        }
    }

    /*
     * Second pass: index all the vertices in such a way that every
     * subtree has a contiguous range of indices. (Easily enough done,
     * by iterating through the tree structure we just built and
     * numbering its elements as if they were those of a sorted list.)
     *
     * For each vertex, we compute the min and max index of the
     * subtree starting there.
     *
     * (We index the vertices in preorder, per Tarjan's original
     * description, so that each vertex's min subtree index is its own
     * index; but that doesn't actually matter; either way round would
     * do. The important thing is that we have a simple arithmetic
     * criterion that tells us whether a vertex is in a given subtree
     * or not.)
     */
    debug(("--- begin indexing pass\n"));
    index = 0;
    for (v = 0; v < nvertices; v++)
        pv[v].visited = false;
    pv[root].visited = true;
    u = pv[root].child;
    while (1) {
        if (!pv[u].visited) {
            pv[u].visited = true;

            /*
             * Index this node.
             */
            pv[u].minindex = pv[u].index = index;
            debug(("  vertex %d <- index %d\n", u, index));
            index++;

            /*
             * Now descend in depth-first search.
             */
            if (pv[u].child >= 0) {
                u = pv[u].child;
                debug(("    descending to %d\n", u));
                continue;
            }
        }

        if (u == root) {
            debug(("      back at %d, done indexing\n", u));
            break;
        }

        /*
         * As we re-ascend to here from its children (or find that we
         * had no children to descend to in the first place), fill in
         * its maxindex field.
         */
        pv[u].maxindex = index-1;
        debug(("  vertex %d <- maxindex %d\n", u, pv[u].maxindex));

        if (pv[u].sibling >= 0) {
            u = pv[u].sibling;
            debug(("    sideways to %d\n", u));
        } else {
            u = pv[u].parent;
            debug(("    ascending to %d\n", u));
        }
    }

    /*
     * We're ready to generate output now, so initialise the output
     * fields.
     */
    for (v = 0; v < nvertices; v++)
        pv[v].bridge = -1;

    /*
     * Final pass: determine the min and max index of the vertices
     * reachable from every subtree, not counting the link back to
     * each vertex's parent. Then our criterion is: given a vertex u,
     * defining a subtree consisting of u and all its descendants, we
     * compare the range of vertex indices _in_ that subtree (which is
     * just the minindex and maxindex of u) with the range of vertex
     * indices in the _neighbourhood_ of the subtree (computed in this
     * final pass, and not counting u's own edge to its parent), and
     * if the latter includes anything outside the former, then there
     * must be some path from u to outside its subtree which does not
     * go through the parent edge - i.e. the edge from u to its parent
     * is part of a loop.
     */
    debug(("--- begin min-max pass\n"));
    nbridges = 0;
    for (v = 0; v < nvertices; v++)
        pv[v].visited = false;
    u = pv[root].child;
    pv[root].visited = true;
    while (1) {
        if (!pv[u].visited) {
            pv[u].visited = true;

            /*
             * Look for vertices reachable directly from u, including
             * u itself.
             */
            debug(("  processing vertex %d\n", u));
            pv[u].minreachable = pv[u].maxreachable = pv[u].minindex;
            for (w = neighbour(u, ctx); w >= 0; w = neighbour(-1, ctx)) {
                debug(("    edge %d-%d\n", u, w));
                if (w != pv[u].parent) {
                    int i = pv[w].index;
                    if (pv[u].minreachable > i)
                        pv[u].minreachable = i;
                    if (pv[u].maxreachable < i)
                        pv[u].maxreachable = i;
                }
            }
            debug(("    initial min=%d max=%d\n",
                   pv[u].minreachable, pv[u].maxreachable));

            /*
             * Now descend in depth-first search.
             */
            if (pv[u].child >= 0) {
                u = pv[u].child;
                debug(("    descending to %d\n", u));
                continue;
            }
        }

        if (u == root) {
            debug(("      back at %d, done min-maxing\n", u));
            break;
        }

        /*
         * As we re-ascend to this vertex, go back through its
         * immediate children and do a post-update of its min/max.
         */
        for (v = pv[u].child; v >= 0; v = pv[v].sibling) {
            if (pv[u].minreachable > pv[v].minreachable)
                pv[u].minreachable = pv[v].minreachable;
            if (pv[u].maxreachable < pv[v].maxreachable)
                pv[u].maxreachable = pv[v].maxreachable;
        }

        debug(("  postorder update of %d: min=%d max=%d (indices %d-%d)\n", u,
               pv[u].minreachable, pv[u].maxreachable,
               pv[u].minindex, pv[u].maxindex));

        /*
         * And now we know whether each to our own parent is a bridge.
         */
        if ((v = pv[u].parent) != root) {
            if (pv[u].minreachable >= pv[u].minindex &&
                pv[u].maxreachable <= pv[u].maxindex) {
                /* Yes, it's a bridge. */
                pv[u].bridge = v;
                nbridges++;
                debug(("  %d-%d is a bridge\n", v, u));
            } else {
                debug(("  %d-%d is not a bridge\n", v, u));
            }
        }

        if (pv[u].sibling >= 0) {
            u = pv[u].sibling;
            debug(("    sideways to %d\n", u));
        } else {
            u = pv[u].parent;
            debug(("    ascending to %d\n", u));
        }
    }

    debug(("finished, nedges=%d nbridges=%d\n", nedges, nbridges));

    /*
     * Done.
     */
    return nbridges < nedges;
}
