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

/*
 * Appendix: the long and painful history of loop detection in these puzzles
 * =========================================================================
 *
 * For interest, I thought I'd write up the five loop-finding methods
 * I've gone through before getting to this algorithm. It's a case
 * study in all the ways you can solve this particular problem
 * wrongly, and also how much effort you can waste by not managing to
 * find the existing solution in the literature :-(
 *
 * Vertex dsf
 * ----------
 *
 * Initially, in puzzles where you need to not have any loops in the
 * solution graph, I detected them by using a dsf to track connected
 * components of vertices. Iterate over each edge unifying the two
 * vertices it connects; but before that, check if the two vertices
 * are _already_ known to be connected. If so, then the new edge is
 * providing a second path between them, i.e. a loop exists.
 *
 * That's adequate for automated solvers, where you just need to know
 * _whether_ a loop exists, so as to rule out that move and do
 * something else. But during play, you want to do better than that:
 * you want to _point out_ the loops with error highlighting.
 *
 * Graph pruning
 * -------------
 *
 * So my second attempt worked by iteratively pruning the graph. Find
 * a vertex with degree 1; remove that edge; repeat until you can't
 * find such a vertex any more. This procedure will remove *every*
 * edge of the graph if and only if there were no loops; so if there
 * are any edges remaining, highlight them.
 *
 * This successfully highlights loops, but not _only_ loops. If the
 * graph contains a 'dumb-bell' shaped subgraph consisting of two
 * loops connected by a path, then we'll end up highlighting the
 * connecting path as well as the loops. That's not what we wanted.
 *
 * Vertex dsf with ad-hoc loop tracing
 * -----------------------------------
 *
 * So my third attempt was to go back to the dsf strategy, only this
 * time, when you detect that a particular edge connects two
 * already-connected vertices (and hence is part of a loop), you try
 * to trace round that loop to highlight it - before adding the new
 * edge, search for a path between its endpoints among the edges the
 * algorithm has already visited, and when you find one (which you
 * must), highlight the loop consisting of that path plus the new
 * edge.
 *
 * This solves the dumb-bell problem - we definitely now cannot
 * accidentally highlight any edge that is *not* part of a loop. But
 * it's far from clear that we'll highlight *every* edge that *is*
 * part of a loop - what if there were multiple paths between the two
 * vertices? It would be difficult to guarantee that we'd always catch
 * every single one.
 *
 * On the other hand, it is at least guaranteed that we'll highlight
 * _something_ if any loop exists, and in other error highlighting
 * situations (see in particular the Tents connected component
 * analysis) I've been known to consider that sufficient. So this
 * version hung around for quite a while, until I had a better idea.
 *
 * Face dsf
 * --------
 *
 * Round about the time Loopy was being revamped to include non-square
 * grids, I had a much cuter idea, making use of the fact that the
 * graph is planar, and hence has a concept of faces.
 *
 * In Loopy, there are really two graphs: the 'grid', consisting of
 * all the edges that the player *might* fill in, and the solution
 * graph of the edges the player actually *has* filled in. The
 * algorithm is: set up a dsf on the *faces* of the grid. Iterate over
 * each edge of the grid which is _not_ marked by the player as an
 * edge of the solution graph, unifying the faces on either side of
 * that edge. This groups the faces into connected components. Now,
 * there is more than one connected component iff a loop exists, and
 * moreover, an edge of the solution graph is part of a loop iff the
 * faces on either side of it are in different connected components!
 *
 * This is the first algorithm I came up with that I was confident
 * would successfully highlight exactly the correct set of edges in
 * all cases. It's also conceptually elegant, and very easy to
 * implement and to be confident you've got it right (since it just
 * consists of two very simple loops over the edge set, one building
 * the dsf and one reading it off). I was very pleased with it.
 *
 * Doing the same thing in Slant is slightly more difficult because
 * the set of edges the user can fill in do not form a planar graph
 * (the two potential edges in each square cross in the middle). But
 * you can still apply the same principle by considering the 'faces'
 * to be diamond-shaped regions of space around each horizontal or
 * vertical grid line. Equivalently, pretend each edge added by the
 * player is really divided into two edges, each from a square-centre
 * to one of the square's corners, and now the grid graph is planar
 * again.
 *
 * However, it fell down when - much later - I tried to implement the
 * same algorithm in Net.
 *
 * Net doesn't *absolutely need* loop detection, because of its system
 * of highlighting squares connected to the source square: an argument
 * involving counting vertex degrees shows that if any loop exists,
 * then it must be counterbalanced by some disconnected square, so
 * there will be _some_ error highlight in any invalid grid even
 * without loop detection. However, in large complicated cases, it's
 * still nice to highlight the loop itself, so that once the player is
 * clued in to its existence by a disconnected square elsewhere, they
 * don't have to spend forever trying to find it.
 *
 * The new wrinkle in Net, compared to other loop-disallowing puzzles,
 * is that it can be played with wrapping walls, or - topologically
 * speaking - on a torus. And a torus has a property that algebraic
 * topologists would know of as a 'non-trivial H_1 homology group',
 * which essentially means that there can exist a loop on a torus
 * which *doesn't* separate the surface into two regions disconnected
 * from each other.
 *
 * In other words, using this algorithm in Net will do fine at finding
 * _small_ localised loops, but a large-scale loop that goes (say) off
 * the top of the grid, back on at the bottom, and meets up in the
 * middle again will not be detected.
 *
 * Footpath dsf
 * ------------
 *
 * To solve this homology problem in Net, I hastily thought up another
 * dsf-based algorithm.
 *
 * This time, let's consider each edge of the graph to be a road, with
 * a separate pedestrian footpath down each side. We'll form a dsf on
 * those imaginary segments of footpath.
 *
 * At each vertex of the graph, we go round the edges leaving that
 * vertex, in order around the vertex. For each pair of edges adjacent
 * in this order, we unify their facing pair of footpaths (e.g. if
 * edge E appears anticlockwise of F, then we unify the anticlockwise
 * footpath of F with the clockwise one of E) . In particular, if a
 * vertex has degree 1, then the two footpaths on either side of its
 * single edge are unified.
 *
 * Then, an edge is part of a loop iff its two footpaths are not
 * reachable from one another.
 *
 * This algorithm is almost as simple to implement as the face dsf,
 * and it works on a wider class of graphs embedded in plane-like
 * surfaces; in particular, it fixes the torus bug in the face-dsf
 * approach. However, it still depends on the graph having _some_ sort
 * of embedding in a 2-manifold, because it relies on there being a
 * meaningful notion of 'order of edges around a vertex' in the first
 * place, so you couldn't use it on a wildly nonplanar graph like the
 * diamond lattice. Also, more subtly, it depends on the graph being
 * embedded in an _orientable_ surface - and that's a thing that might
 * much more plausibly change in future puzzles, because it's not at
 * all unlikely that at some point I might feel moved to implement a
 * puzzle that can be played on the surface of a Mobius strip or a
 * Klein bottle. And then even this algorithm won't work.
 *
 * Tarjan's bridge-finding algorithm
 * ---------------------------------
 *
 * And so, finally, we come to the algorithm above. This one is pure
 * graph theory: it doesn't depend on any concept of 'faces', or 'edge
 * ordering around a vertex', or any other trapping of a planar or
 * quasi-planar graph embedding. It should work on any graph
 * whatsoever, and reliably identify precisely the set of edges that
 * form part of some loop. So *hopefully* this long string of failures
 * has finally come to an end...
 */
