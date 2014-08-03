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

#ifndef MAXFLOW_MAXFLOW_H
#define MAXFLOW_MAXFLOW_H

/*
 * The actual algorithm.
 * 
 * Inputs:
 * 
 *  - `scratch' is previously allocated scratch space of a size
 *    previously determined by calling `maxflow_scratch_size'.
 * 
 *  - `nv' is the number of vertices. Vertices are assumed to be
 *    numbered from 0 to nv-1.
 * 
 *  - `source' and `sink' are the distinguished source and sink
 *    vertices.
 * 
 *  - `ne' is the number of edges in the graph.
 * 
 *  - `edges' is an array of 2*ne integers, giving a (source, dest)
 *    pair for each network edge. Edge pairs are expected to be
 *    sorted in lexicographic order.
 * 
 *  - `backedges' is an array of `ne' integers, each a distinct
 *    index into `edges'. The edges in `edges', if permuted as
 *    specified by this array, should end up sorted in the _other_
 *    lexicographic order, i.e. dest taking priority over source.
 * 
 *  - `capacity' is an array of `ne' integers, giving a maximum
 *    flow capacity for each edge. A negative value is taken to
 *    indicate unlimited capacity on that edge, but note that there
 *    may not be any unlimited-capacity _path_ from source to sink
 *    or an assertion will be failed.
 * 
 * Output:
 * 
 *  - `flow' must be non-NULL. It is an array of `ne' integers,
 *    each giving the final flow along each edge.
 * 
 *  - `cut' may be NULL. If non-NULL, it is an array of `nv'
 *    integers, which will be set to zero or one on output, in such
 *    a way that:
 *     + the set of zero vertices includes the source
 *     + the set of one vertices includes the sink
 *     + the maximum flow capacity between the zero and one vertex
 * 	 sets is achieved (i.e. all edges from a zero vertex to a
 * 	 one vertex are at full capacity, while all edges from a
 * 	 one vertex to a zero vertex have no flow at all).
 * 
 *  - the returned value from the function is the total flow
 *    achieved.
 */
int maxflow_with_scratch(void *scratch, int nv, int source, int sink,
			 int ne, const int *edges, const int *backedges,
			 const int *capacity, int *flow, int *cut);

/*
 * The above function expects its `scratch' and `backedges'
 * parameters to have already been set up. This allows you to set
 * them up once and use them in multiple invocates of the
 * algorithm. Now I provide functions to actually do the setting
 * up.
 */
int maxflow_scratch_size(int nv);
void maxflow_setup_backedges(int ne, const int *edges, int *backedges);

/*
 * Simplified version of the above function. All parameters are the
 * same, except that `scratch' and `backedges' are constructed
 * internally. This is the simplest way to call the algorithm as a
 * one-off; however, if you need to call it multiple times on the
 * same network, it is probably better to call the above version
 * directly so that you only construct `scratch' and `backedges'
 * once.
 * 
 * Additional return value is now -1, meaning that scratch space
 * could not be allocated.
 */
int maxflow(int nv, int source, int sink,
	    int ne, const int *edges, const int *capacity,
	    int *flow, int *cut);

#endif /* MAXFLOW_MAXFLOW_H */
