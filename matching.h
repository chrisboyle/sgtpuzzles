/*
 * Hopcroft-Karp algorithm for finding a maximal matching in a
 * bipartite graph.
 */

#ifndef MATCHING_MATCHING_H
#define MATCHING_MATCHING_H

/*
 * The actual algorithm.
 * 
 * Inputs:
 * 
 *  - 'scratch' is previously allocated scratch space of a size
 *    previously determined by calling 'matching_scratch_size'.
 * 
 *  - 'nl' is the number of vertices on the left side of the graph.
 *    Left vertices are numbered from 0 to nl-1.
 * 
 *  - 'nr' is the number of vertices on the left side of the graph.
 *    Right vertices are numbered from 0 to nr-1.
 * 
 *  - 'adjlists' and 'adjsizes' represents the graph in adjacency-list
 *    form. For each left vertex L, adjlists[L] points to an array of
 *    adjsizes[L] integers giving the list of right vertices adjacent
 *    to L.
 *
 *  - 'rs', if not NULL, is a random_state used to perturb the
 *    progress of the algorithm so as to choose randomly from the
 *    possible matchings if there's more than one. (The exact
 *    probability distribution can't be guaranteed, but at the very
 *    least, any matching that exists should be a _possible_ output.)
 *
 * If 'rs' is not NULL, then each list in adjlists[] will be permuted
 * during the course of the algorithm as a side effect. (That's why
 * it's not an array of _const_ int pointers.)
 * 
 * Output:
 * 
 *  - 'outl' may be NULL. If non-NULL, it is an array of 'nl'
 *    integers, and outl[L] will be assigned the index of the right
 *    vertex that the output matching paired with the left vertex L,
 *    or -1 if L is unpaired.
 * 
 *  - 'outr' may be NULL. If non-NULL, it is an array of 'nr'
 *    integers, and outr[R] will be assigned the index of the left
 *    vertex that the output matching paired with the right vertex R,
 *    or -1 if R is unpaired.
 * 
 *  - the returned value from the function is the total number of
 *    edges in the matching.
 */
int matching_with_scratch(void *scratch,
                          int nl, int nr, int **adjlists, int *adjsizes,
                          random_state *rs, int *outl, int *outr);

/*
 * The above function expects its 'scratch' parameter to have already
 * been set up. This function tells you how much space is needed for a
 * given size of graph, so that you can allocate a single instance of
 * scratch space and run the algorithm multiple times without the
 * overhead of an alloc and free every time.
 */
size_t matching_scratch_size(int nl, int nr);

/*
 * Simplified version of the above function. All parameters are the
 * same, except that 'scratch' is constructed internally and freed on
 * exit. This is the simplest way to call the algorithm as a one-off;
 * however, if you need to call it multiple times on the same size of
 * graph, it is probably better to call the above version directly so
 * that you only construct 'scratch' once.
 *
 * Additional return value is now -1, meaning that scratch space
 * could not be allocated.
 */
int matching(int nl, int nr, int **adjlists, int *adjsizes,
             random_state *rs, int *outl, int *outr);

/*
 * Diagnostic routine used in testing this algorithm. It is passed a
 * pointer to a piece of scratch space that's just been used by
 * matching_with_scratch, and extracts from it a labelling of the
 * input graph that acts as a 'witness' to the maximality of the
 * returned matching.
 *
 * The output parameter 'witness' should be an array of (nl+nr)
 * integers, indexed such that witness[L] corresponds to an L-vertex (for
 * L=0,1,...,nl-1) and witness[nl+R] corresponds to an R-vertex (for
 * R=0,1,...,nr-1). On return, this array will assign each vertex a
 * label which is either 0 or 1, and the following properties will
 * hold:
 *
 *  + all vertices not paired up by the matching are type L0 or R1
 *  + every L0->R1 edge is used by the matching
 *  + no L1->R0 edge is used by the matching.
 *
 * The mere existence of such a labelling is enough to prove the
 * maximality of the matching, because if there is any larger matching
 * then its symmetric difference with this one must include at least
 * one 'augmenting path', which starts at a free L-vertex and ends at
 * a free R-vertex, traversing only unused L->R edges and only used
 * R->L edges. But that would mean it starts at an L0, ends at an R1,
 * and never follows an edge that can get from an 0 to a 1.
 */
void matching_witness(void *scratch, int nl, int nr, int *witness);

#endif /* MATCHING_MATCHING_H */
