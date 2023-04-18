/*
 * Standalone tool to run the matching algorithm.
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "puzzles.h"
#include "matching.h"
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
