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

#ifdef STANDALONE_MATCHING_TEST

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
static void matching_witness(void *scratchv, int nl, int nr, int *witness)
{
    struct scratch *s = (struct scratch *)scratchv;
    int i, j;

    for (i = 0; i < nl; i++)
        witness[i] = s->Llayer[i] == -1;
    for (j = 0; j < nr; j++)
        witness[nl + j] = s->Rlayer[j] == -1;
}

/*
 * Standalone tool to run the matching algorithm.
 */

#include <string.h>
#include <ctype.h>
#include <time.h>

#include "tree234.h"

static int nl, nr, count;
static int **adjlists, *adjsizes;
static int *adjdata, *outl, *outr, *witness;
static void *scratch;
static random_state *rs;

static void allocate(int nl_, int nr_, int maxedges)
{
    nl = nl_;
    nr = nr_;
    adjdata = snewn(maxedges, int);
    adjlists = snewn(nl, int *);
    adjsizes = snewn(nl, int);
    outl = snewn(nl, int);
    outr = snewn(nr, int);
    witness = snewn(nl+nr, int);
    scratch = smalloc(matching_scratch_size(nl, nr));
}

static void deallocate(void)
{
    sfree(adjlists);
    sfree(adjsizes);
    sfree(adjdata);
    sfree(outl);
    sfree(outr);
    sfree(witness);
    sfree(scratch);
}

static void find_and_check_matching(void)
{
    int i, j, k;

    count = matching_with_scratch(scratch, nl, nr, adjlists, adjsizes,
                                  rs, outl, outr);
    matching_witness(scratch, nl, nr, witness);

    for (i = j = 0; i < nl; i++) {
        if (outl[i] != -1) {
            assert(0 <= outl[i] && outl[i] < nr);
            assert(outr[outl[i]] == i);
            j++;

            for (k = 0; k < adjsizes[i]; k++)
                if (adjlists[i][k] == outl[i])
                    break;
            assert(k < adjsizes[i]);
        }
    }
    assert(j == count);

    for (i = j = 0; i < nr; i++) {
        if (outr[i] != -1) {
            assert(0 <= outr[i] && outr[i] < nl);
            assert(outl[outr[i]] == i);
            j++;
        }
    }
    assert(j == count);

    for (i = 0; i < nl; i++) {
        if (outl[i] == -1)
            assert(witness[i] == 0);
    }
    for (i = 0; i < nr; i++) {
        if (outr[i] == -1)
            assert(witness[nl+i] == 1);
    }
    for (i = 0; i < nl; i++) {
        for (j = 0; j < adjsizes[i]; j++) {
            k = adjlists[i][j];

            if (outl[i] == k)
                assert(!(witness[i] == 1 && witness[nl+k] == 0));
            else
                assert(!(witness[i] == 0 && witness[nl+k] == 1));
        }
    }
}

struct nodename {
    const char *name;
    int index;
};

static int compare_nodes(void *av, void *bv)
{
    const struct nodename *a = (const struct nodename *)av;
    const struct nodename *b = (const struct nodename *)bv;
    return strcmp(a->name, b->name);
}

static int node_index(tree234 *n2i, tree234 *i2n, const char *name)
{
    struct nodename *nn, *nn_prev;
    char *namedup = dupstr(name);

    nn = snew(struct nodename);
    nn->name = namedup;
    nn->index = count234(n2i);

    nn_prev = add234(n2i, nn);
    if (nn_prev != nn) {
        sfree(nn);
        sfree(namedup);
    } else {
        addpos234(i2n, nn, nn->index);
    }

    return nn_prev->index;
}

struct edge {
    int L, R;
};

static int compare_edges(void *av, void *bv)
{
    const struct edge *a = (const struct edge *)av;
    const struct edge *b = (const struct edge *)bv;
    if (a->L < b->L) return -1;
    if (a->L > b->L) return +1;
    if (a->R < b->R) return -1;
    if (a->R > b->R) return +1;
    return 0;
}

static void matching_from_user_input(FILE *fp, const char *filename)
{
    tree234 *Ln2i, *Li2n, *Rn2i, *Ri2n, *edges;
    char *line = NULL;
    struct edge *e;
    int i, lineno = 0;
    int *adjptr;

    Ln2i = newtree234(compare_nodes);
    Rn2i = newtree234(compare_nodes);
    Li2n = newtree234(NULL);
    Ri2n = newtree234(NULL);
    edges = newtree234(compare_edges);

    while (sfree(line), lineno++, (line = fgetline(fp)) != NULL) {
        char *p, *Lname, *Rname;

        p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p)
            continue;

        Lname = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p)
            *p++ = '\0';
        while (*p && isspace((unsigned char)*p)) p++;

        if (!*p) {
            fprintf(stderr, "%s:%d: expected 2 words, found 1\n",
                    filename, lineno);
            exit(1);
        }

        Rname = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p)
            *p++ = '\0';
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p) {
            fprintf(stderr, "%s:%d: expected 2 words, found more\n",
                    filename, lineno);
            exit(1);
        }

        e = snew(struct edge);
        e->L = node_index(Ln2i, Li2n, Lname);
        e->R = node_index(Rn2i, Ri2n, Rname);
        if (add234(edges, e) != e) {
            fprintf(stderr, "%s:%d: duplicate edge\n",
                    filename, lineno);
            exit(1);
        }
    }

    allocate(count234(Ln2i), count234(Rn2i), count234(edges));

    adjptr = adjdata;
    for (i = 0; i < nl; i++)
        adjlists[i] = NULL;
    for (i = 0; (e = index234(edges, i)) != NULL; i++) {
        if (!adjlists[e->L])
            adjlists[e->L] = adjptr;
        *adjptr++ = e->R;
        adjsizes[e->L] = adjptr - adjlists[e->L];
    }

    find_and_check_matching();

    for (i = 0; i < nl; i++) {
        if (outl[i] != -1) {
            struct nodename *Lnn = index234(Li2n, i);
            struct nodename *Rnn = index234(Ri2n, outl[i]);
            printf("%s %s\n", Lnn->name, Rnn->name);
        }
    }
    deallocate();
}

static void test_subsets(void)
{
    int b = 8;
    int n = 1 << b;
    int i, j, nruns, expected_size;
    int *adjptr;
    int *edgecounts;
    struct stats {
        int min, max;
        double n, sx, sxx;
    } *stats;
    static const char seed[] = "fixed random seed for repeatability";

    /*
     * Generate a graph in which every subset of [b] = {1,...,b}
     * (represented as a b-bit integer 0 <= i < n) has an edge going
     * to every subset obtained by removing exactly one element.
     *
     * This graph is the disjoint union of the corresponding graph for
     * each layer (collection of same-sized subset) of the power set
     * of [b]. Each of those graphs has a matching of size equal to
     * the smaller of its vertex sets. So we expect the overall size
     * of the output matching to be less than n by the size of the
     * largest layer, that is, to be n - binomial(n, floor(n/2)).
     *
     * We run the generation repeatedly, randomising it every time,
     * and we expect to see every possible edge appear sooner or
     * later.
     */

    rs = random_new(seed, strlen(seed));

    allocate(n, n, n*b);
    adjptr = adjdata;
    expected_size = 0;
    for (i = 0; i < n; i++) {
        adjlists[i] = adjptr;
        for (j = 0; j < b; j++) {
            if (i & (1 << j))
                *adjptr++ = i & ~(1 << j);
        }
        adjsizes[i] = adjptr - adjlists[i];
        if (adjsizes[i] != b/2)
            expected_size++;
    }

    edgecounts = snewn(n*b, int);
    for (i = 0; i < n*b; i++)
        edgecounts[i] = 0;

    stats = snewn(b, struct stats);

    nruns = 0;
    while (nruns < 10000) {
        nruns++;
        find_and_check_matching();
        assert(count == expected_size);

        for (i = 0; i < n; i++)
            for (j = 0; j < b; j++)
                if ((i ^ outl[i]) == (1 << j))
                    edgecounts[b*i+j]++;

        if (nruns % 1000 == 0) {
            for (i = 0; i < b; i++) {
                struct stats *st = &stats[i];
                st->min = st->max = -1;
                st->n = st->sx = st->sxx = 0;
            }

            for (i = 0; i < n; i++) {
                int pop = 0;
                for (j = 0; j < b; j++)
                    if (i & (1 << j))
                        pop++;
                pop--;

                for (j = 0; j < b; j++) {
                    if (i & (1 << j)) {
                        struct stats *st = &stats[pop];
                        int x = edgecounts[b*i+j];
                        if (st->max == -1 || st->max < x)
                            st->max = x;
                        if (st->min == -1 || st->min > x)
                            st->min = x;
                        st->n++;
                        st->sx += x;
                        st->sxx += (double)x*x;
                    } else {
                        assert(edgecounts[b*i+j] == 0);
                    }
                }
            }
        }
    }

    printf("after %d runs:\n", nruns);
    for (j = 0; j < b; j++) {
        struct stats *st = &stats[j];
        printf("edges between layers %d,%d:"
               " min=%d max=%d mean=%f variance=%f\n",
               j, j+1, st->min, st->max, st->sx/st->n,
               (st->sxx - st->sx*st->sx/st->n) / st->n);
    }
    sfree(edgecounts);
    deallocate();
}

int main(int argc, char **argv)
{
    static const char stdin_identifier[] = "<standard input>";
    const char *infile = NULL;
    bool doing_opts = true;
    enum { USER_INPUT, AUTOTEST } mode = USER_INPUT;

    while (--argc > 0) {
        const char *arg = *++argv;

        if (doing_opts && arg[0] == '-' && arg[1]) {
            if (!strcmp(arg, "--")) {
                doing_opts = false;
            } else if (!strcmp(arg, "--random")) {
                char buf[64];
                int len = sprintf(buf, "%lu", (unsigned long)time(NULL));
                rs = random_new(buf, len);
            } else if (!strcmp(arg, "--autotest")) {
                mode = AUTOTEST;
            } else {
                fprintf(stderr, "matching: unrecognised option '%s'\n", arg);
                return 1;
            }
        } else {
            if (!infile) {
                infile = (!strcmp(arg, "-") ? stdin_identifier : arg);
            } else {
                fprintf(stderr, "matching: too many arguments\n");
                return 1;
            }
        }
    }

    if (mode == USER_INPUT) {
        FILE *fp;

        if (!infile)
            infile = stdin_identifier;

        if (infile != stdin_identifier) {
            fp = fopen(infile, "r");
            if (!fp) {
                fprintf(stderr, "matching: could not open input file '%s'\n",
                        infile);
                return 1;
            }
        } else {
            fp = stdin;
        }

        matching_from_user_input(fp, infile);

        if (infile != stdin_identifier)
            fclose(fp);
    }

    if (mode == AUTOTEST) {
        if (infile) {
            fprintf(stderr, "matching: expected no filename argument "
                    "with --autotest\n");
            return 1;
        }

        test_subsets();
    }

    return 0;
}

#endif /* STANDALONE_MATCHING_TEST */
