/*
 * Implementation of matching.h.
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "puzzles.h"
#include "matching.h"

struct scratch {
    /*
     * Current contents of the in-progress matching. LtoR is an array
     * of nl integers, each of which holds a value in {0,1,...,nr-1},
     * or -1 for no current assignment. RtoL is exactly the reverse.
     *
     * Invariant: LtoR[i] is non-empty and equal to j if and only if
     * RtoL[j] is non-empty and equal to i.
     */
    int *LtoR, *RtoL;

    /*
     * Arrays of nl and nr integer respectively, giving the layer
     * assigned to each integer in the breadth-first search step of
     * the algorithm.
     */
    int *Llayer, *Rlayer;

    /*
     * Arrays of nl and nr integers respectively, used to hold the
     * to-do queues in the breadth-first search.
     */
    int *Lqueue, *Rqueue;

    /*
     * An augmenting path of vertices, alternating between L vertices
     * (in the even-numbered positions, starting at 0) and R (in the
     * odd positions). Must be long enough to hold any such path that
     * never repeats a vertex, i.e. must be at least 2*min(nl,nr) in
     * size.
     */
    int *augpath;

    /*
     * Track the progress of the depth-first search at each
     * even-numbered layer. Has one element for each even-numbered
     * position in augpath.
     */
    int *dfsstate;

    /*
     * Store a random permutation of the L vertex indices, if we're
     * randomising the dfs phase.
     */
    int *Lorder;
};

size_t matching_scratch_size(int nl, int nr)
{
    size_t n;
    int nmin = (nl < nr ? nl : nr);

    n = (sizeof(struct scratch) + sizeof(int)-1)/sizeof(int);
    n += nl;                           /* LtoR */
    n += nr;                           /* RtoL */
    n += nl;                           /* Llayer */
    n += nr;                           /* Rlayer */
    n += nl;                           /* Lqueue */
    n += nr;                           /* Rqueue */
    n += 2*nmin;                       /* augpath */
    n += nmin;                         /* dfsstate */
    n += nl;                           /* Lorder */
    return n * sizeof(int);
}

int matching_with_scratch(void *scratchv,
                          int nl, int nr, int **adjlists, int *adjsizes,
                          random_state *rs, int *outl, int *outr)
{
    struct scratch *s = (struct scratch *)scratchv;
    int L, R, i, j;

    /*
     * Set up the various array pointers in the scratch space.
     */
    {
        int *p = scratchv;
        int nmin = (nl < nr ? nl : nr);

        p += (sizeof(struct scratch) + sizeof(int)-1)/sizeof(int);
        s->LtoR = p; p += nl;
        s->RtoL = p; p += nr;
        s->Llayer = p; p += nl;
        s->Rlayer = p; p += nr;
        s->Lqueue = p; p += nl;
        s->Rqueue = p; p += nr;
        s->augpath = p; p += 2*nmin;
        s->dfsstate = p; p += nmin;
        s->Lorder = p; p += nl;
    }

    /*
     * Set up the initial matching, which is empty.
     */
    for (L = 0; L < nl; L++)
        s->LtoR[L] = -1;
    for (R = 0; R < nr; R++)
        s->RtoL[R] = -1;

    while (1) {
        /*
         * Breadth-first search starting from the unassigned left
         * vertices, traversing edges from left to right only if they
         * are _not_ part of the matching, and from right to left only
         * if they _are_. We assign a 'layer number' to all vertices
         * visited by this search, with the starting vertices being
         * layer 0 and every successor of a layer-n node being layer
         * n+1.
         */
        int Lqs, Rqs, layer, target_layer;

        for (L = 0; L < nl; L++)
            s->Llayer[L] = -1;
        for (R = 0; R < nr; R++)
            s->Rlayer[R] = -1;

        Lqs = 0;
        for (L = 0; L < nl; L++) {
            if (s->LtoR[L] == -1) {
                s->Llayer[L] = 0;
                s->Lqueue[Lqs++] = L;
            }
        }

        layer = 0;
        while (1) {
            bool found_free_R_vertex = false;

            Rqs = 0;
            for (i = 0; i < Lqs; i++) {
                L = s->Lqueue[i];
                assert(s->Llayer[L] == layer);

                for (j = 0; j < adjsizes[L]; j++) {
                    R = adjlists[L][j];
                    if (R != s->LtoR[L] && s->Rlayer[R] == -1) {
                        s->Rlayer[R] = layer+1;
                        s->Rqueue[Rqs++] = R;
                        if (s->RtoL[R] == -1)
                            found_free_R_vertex = true;
                    }
                }
            }
            layer++;

            if (found_free_R_vertex)
                break;

            if (Rqs == 0)
                goto done;

            Lqs = 0;
            for (j = 0; j < Rqs; j++) {
                R = s->Rqueue[j];
                assert(s->Rlayer[R] == layer);
                if ((L = s->RtoL[R]) != -1 && s->Llayer[L] == -1) {
                    s->Llayer[L] = layer+1;
                    s->Lqueue[Lqs++] = L;
                }
            }
            layer++;

            if (Lqs == 0)
                goto done;
        }

        target_layer = layer;

        /*
         * Vertices in the target layer are only interesting if
         * they're actually unassigned. Blanking out the others here
         * will save us a special case in the dfs loop below.
         */
        for (R = 0; R < nr; R++)
            if (s->Rlayer[R] == target_layer && s->RtoL[R] != -1)
                s->Rlayer[R] = -1;

        /*
         * Choose an ordering in which to try the L vertices at the
         * start of the next pass.
         */
        for (L = 0; L < nl; L++)
            s->Lorder[L] = L;
        if (rs)
            shuffle(s->Lorder, nl, sizeof(*s->Lorder), rs);

        /*
         * Now depth-first search through that layered set of vertices
         * to find as many (vertex-)disjoint augmenting paths as we
         * can, and for each one we find, augment the matching.
         */
        s->dfsstate[0] = 0;
        i = 0;
        while (1) {
            /*
             * Find the next vertex to go on the end of augpath.
             */
            if (i == 0) {
                /* In this special case, we're just looking for L
                 * vertices that are not yet assigned. */
                if (s->dfsstate[i] == nl)
                    break;             /* entire DFS has finished */

                L = s->Lorder[s->dfsstate[i]++];

                if (s->Llayer[L] != 2*i)
                    continue;          /* skip this vertex */
            } else {
                /* In the more usual case, we're going through the
                 * adjacency list for the previous L vertex. */
                L = s->augpath[2*i-2];
                j = s->dfsstate[i]++;
                if (j == adjsizes[L]) {
                    /* Run out of neighbours of the previous vertex. */
                    i--;
                    continue;
                }
                if (rs && adjsizes[L] - j > 1) {
                    int which = j + random_upto(rs, adjsizes[L] - j);
                    int tmp = adjlists[L][which];
                    adjlists[L][which] = adjlists[L][j];
                    adjlists[L][j] = tmp;
                }
                R = adjlists[L][j];

                if (s->Rlayer[R] != 2*i-1)
                    continue;          /* skip this vertex */

                s->augpath[2*i-1] = R;
                s->Rlayer[R] = -1;     /* mark vertex as visited */

                if (2*i-1 == target_layer) {
                    /*
                     * We've found an augmenting path, in the form of
                     * an even-sized list of vertices alternating
                     * L,R,...,L,R, with the initial L and final R
                     * vertex free and otherwise each R currently
                     * connected to the next L. Adjust so that each L
                     * connects to the next R, increasing the edge
                     * count in the matching by 1.
                     */
                    for (j = 0; j < 2*i; j += 2) {
                        s->LtoR[s->augpath[j]] = s->augpath[j+1];
                        s->RtoL[s->augpath[j+1]] = s->augpath[j];
                    }

                    /*
                     * Having dealt with that path, and already marked
                     * all its vertices as visited, rewind right to
                     * the start and resume our DFS from a new
                     * starting L-vertex.
                     */
                    i = 0;
                    continue;
                }

                L = s->RtoL[R];
                if (s->Llayer[L] != 2*i)
                    continue;          /* skip this vertex */
            }

            s->augpath[2*i] = L;
            s->Llayer[L] = -1;         /* mark vertex as visited */
            i++;
            s->dfsstate[i] = 0;
        }
    }

  done:
    /*
     * Fill in the output arrays.
     */
    if (outl) {
        for (i = 0; i < nl; i++)
            outl[i] = s->LtoR[i];
    }
    if (outr) {
        for (j = 0; j < nr; j++)
            outr[j] = s->RtoL[j];
    }

    /*
     * Return the number of matching edges.
     */
    for (i = j = 0; i < nl; i++)
        if (s->LtoR[i] != -1)
            j++;
    return j;
}

int matching(int nl, int nr, int **adjlists, int *adjsizes,
             random_state *rs, int *outl, int *outr)
{
    void *scratch;
    int size;
    int ret;

    size = matching_scratch_size(nl, nr);
    scratch = malloc(size);
    if (!scratch)
	return -1;

    ret = matching_with_scratch(scratch, nl, nr, adjlists, adjsizes,
                                rs, outl, outr);

    free(scratch);

    return ret;
}

void matching_witness(void *scratchv, int nl, int nr, int *witness)
{
    struct scratch *s = (struct scratch *)scratchv;
    int i, j;

    for (i = 0; i < nl; i++)
        witness[i] = s->Llayer[i] == -1;
    for (j = 0; j < nr; j++)
        witness[nl + j] = s->Rlayer[j] == -1;
}
