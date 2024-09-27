#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "puzzles.h"

static char *progname;

struct graph {
    int nvertices;
    bool *adj;
};

struct neighbour_ctx {
    struct graph *graph;
    int vertex;
    int neighbour;
};

static struct graph *graph_random(
    random_state *rs, int minvertices, int maxvertices)
{
    int nvertices = minvertices + random_upto(
        rs, maxvertices + 1 - minvertices);
    int avg_degree = 1 + random_upto(rs, nvertices - 1);
    struct graph *graph = snew(struct graph);
    int u, v;

    graph->nvertices = nvertices;
    graph->adj = snewn(nvertices * nvertices, bool);
    memset(graph->adj, 0, nvertices * nvertices * sizeof(bool));
    for (u = 0; u < nvertices; u++) {
	for (v = 0; v < u; v++) {
	    if (random_upto(rs, nvertices) <= avg_degree) {
		graph->adj[u * nvertices + v] = 1;
		graph->adj[v * nvertices + u] = 1;
	    }
	}
    }

    return graph;
}

static struct graph *graph_from_untangle(const char *p)
{
    struct graph *graph = snew(struct graph);

    graph->nvertices = atoi(p);
    while (p && isdigit((unsigned char)*p))
        p++;

    assert(*p == ':' && "Expected ':' separating vertex count from edges");
    p++;

    graph->adj = snewn(graph->nvertices * graph->nvertices, bool);
    memset(graph->adj, 0, graph->nvertices * graph->nvertices * sizeof(bool));

    while (*p) {
        int u, v;

        u = atoi(p);
        while (p && isdigit((unsigned char)*p))
            p++;

        assert(*p == '-' && "Expected '-' separating two vertex numbers");
        p++;

        v = atoi(p);
        while (p && isdigit((unsigned char)*p))
            p++;

        if (*p) {
            assert(*p == ',' && "Expected ',' separating two edges");
            p++;
        }

        graph->adj[u * graph->nvertices + v] = 1;
        graph->adj[v * graph->nvertices + u] = 1;
    }

    return graph;
}

static void graph_free(struct graph *graph)
{
    sfree(graph->adj);
    sfree(graph);
}

static bool naive_is_bridge(struct graph *graph, int u, int v,
                            int *u_vertices, int *v_vertices)
{
    DSF *dsf = dsf_new(graph->nvertices);
    int i, j;
    bool toret;

    /* Make a dsf out of the input graph, _except_ the u-v edge. This
     * determines the connected components of what you have if you
     * delete that edge. */
    for (i = 0; i < graph->nvertices; i++) {
	for (j = 0; j < i; j++) {
	    if (graph->adj[i * graph->nvertices + j] &&
                !(i == u && j == v) && !(i == v && j == u))
                dsf_merge(dsf, i, j);
	}
    }

    if (dsf_equivalent(dsf, u, v)) {
        /* If u and v are still in the same component, then deleting
         * the u-v edge didn't disconnect them, so it wasn't a
         * bridge. */
        toret = false;
        *u_vertices = *v_vertices = 0;
    } else {
        /* If u and v aren't in the same component, then the deleted
         * edge was a bridge, and moreover, dsf_size() tells us the
         * sizes of the two components. */
        toret = true;
        *u_vertices = dsf_size(dsf, u);
        *v_vertices = dsf_size(dsf, v);
    }

    dsf_free(dsf);
    return toret;
}

static int neighbour_fn(int vertex, void *vctx)
{
    struct neighbour_ctx *ctx = (struct neighbour_ctx *)vctx;
    bool *row;

    if (vertex >= 0) {
	ctx->vertex = vertex;
	ctx->neighbour = -1;
    }

    row = ctx->graph->adj + ctx->vertex * ctx->graph->nvertices;
    while (++ctx->neighbour < ctx->graph->nvertices) {
	if (row[ctx->neighbour]) {
	    return ctx->neighbour;
	}
    }
    return -1;
}

static void test_findloop(struct graph *graph)
{
    struct neighbour_ctx ctx;
    struct findloopstate *fls;
    int u, v;

    fls = findloop_new_state(graph->nvertices);
    ctx.graph = graph;
    findloop_run(fls, graph->nvertices, neighbour_fn, (void*)&ctx);
    for (u = 0; u < graph->nvertices; u++) {
	for (v = u + 1; v < graph->nvertices; v++) {
	    if (graph->adj[u * graph->nvertices + v]) {
		int u_vertices_naive, v_vertices_naive;
		bool naive_res = naive_is_bridge(
                    graph, u, v, &u_vertices_naive, &v_vertices_naive);
		int u_vertices, v_vertices;
		bool is_bridge = findloop_is_bridge(
                    fls, u, v, &u_vertices, &v_vertices);
		bool is_loop_edge = findloop_is_loop_edge(fls, u, v);
		bool bug = false;
		if (is_bridge) {
		    if (is_loop_edge || !naive_res ||
                        u_vertices != u_vertices_naive
                        || v_vertices != v_vertices_naive) {
			bug = true;
		    }
		} else {
		    if (!is_loop_edge || naive_res) {
			bug = true;
		    }
		}
		if (bug) {
		    int i, j;
                    const char *sep = "";
		    printf("\nFound inconsistency!\n");
		    printf("Graph = %d:", graph->nvertices);
		    for (i = 0; i < graph->nvertices; i++) {
			for (j = i + 1; j < graph->nvertices; j++) {
			    if (graph->adj[i * graph->nvertices + j]) {
				printf("%s%d-%d", sep, i, j);
                                sep = ",";
			    }
			}
		    }
		    printf("\n");
		    printf("For edge (%d, %d):\n", u, v);
		    printf("  Naive is_bridge = %s\n",
                           naive_res ? "true" : "false");
		    printf("  findloop_is_bridge = %s\n",
                           is_bridge ? "true" : "false");
		    printf("  findloop_is_loop_edge = %s\n",
                           is_loop_edge ? "true" : "false");
		    printf("  Naive u_vertices = %d\n", u_vertices_naive);
		    printf("  findloop u_vertices = %d\n", u_vertices);
		    printf("  Naive v_vertices = %d\n", v_vertices_naive);
		    printf("  findloop v_vertices = %d\n", v_vertices);
		    exit(1);
		}
	    }
	}
    }
    findloop_free_state(fls);
}

static void error_exit(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void usage(void)
{
    printf("Usage: %s [--help] [--seed seed] [--iterations iterations]",
           progname);
    printf("   verifies the findloop algorithm works as expected, "
           "by comparing to a simple implementation");
}

static const char *const testgraphs[] = {
    "3:", /* empty graph: no edges at all */
    "3:0-1,1-2", /* line graph: everything is a bridge */
    "3:0-1,0-2,1-2", /* cycle graph: nothing is a bridge */
    "6:0-1,0-2,1-2,2-3,3-4,3-5,4-5", /* simplest dumb-bell graph */
    "5:0-1,0-2,0-3,0-4,1-2,1-4,2-3,2-4,3-4", /* complete graph */
    "20:0-1,0-2,1-2,3-4,4-5,6-7,7-8,7-9,8-9,8-10,10-11,11-12,11-13,11-18,12-14,13-14,13-17,14-15,15-16,17-18,17-19,18-19", /* complicated-ish example */
};

int main(int argc, char **argv)
{
    const char *random_seed = "12345";
    int iterations = 10000;
    size_t i;
    random_state *rs;

    progname = argv[0];
    while (--argc > 0) {
	const char *arg = *++argv;
	if (!strcmp(arg, "--help")) {
	    usage();
            exit(0);
	} else if (!strcmp(arg, "--seed")) {
	    if (--argc == 0)
		error_exit("--seed needs an argument");
	    random_seed = *++argv;
	} else if (!strcmp(arg, "--iterations")) {
	    if (--argc == 0)
		error_exit("--iterations needs an argument");
	    iterations = atoi(*++argv);
	} else {
	    error_exit("unrecognized argument");
	}
    }

    printf("Testing %d fixed test cases\n", (int)lenof(testgraphs));

    for (i = 0; i < lenof(testgraphs); i++) {
        struct graph *graph = graph_from_untangle(testgraphs[i]);
        test_findloop(graph);
        graph_free(graph);
    }

    printf("Seeding with \"%s\"\n", random_seed);
    printf("Testing %d random graphs\n", iterations);

    rs = random_new(random_seed, strlen(random_seed));
    while (iterations --> 0) {
        struct graph *graph = graph_random(rs, 2, 100);
        test_findloop(graph);
        graph_free(graph);
    }
    random_free(rs);
    return 0;
}
