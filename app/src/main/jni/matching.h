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

#endif /* MATCHING_MATCHING_H */
