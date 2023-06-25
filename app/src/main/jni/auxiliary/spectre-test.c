/*
 * Standalone test program for spectre.c.
 */

#include <assert.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "puzzles.h"
#include "spectre-internal.h"
#include "spectre-tables-manual.h"
#include "spectre-tables-auto.h"
#include "spectre-help.h"

static void step_tests(void)
{
    SpectreContext ctx[1];
    random_state *rs;
    SpectreCoords *sc;
    unsigned outedge;

    rs = random_new("12345", 5);
    spectrectx_init_random(ctx, rs);

    /* Simplest possible transition: between the two Spectres making
     * up a G hex. */
    sc = spectre_coords_new();
    spectre_coords_make_space(sc, 1);
    sc->index = 0;
    sc->nc = 1;
    sc->c[0].type = HEX_G;
    sc->c[0].index = -1;
    spectrectx_step(ctx, sc, 12, &outedge);
    assert(outedge == 5);
    assert(sc->index == 1);
    assert(sc->nc == 1);
    assert(sc->c[0].type == HEX_G);
    assert(sc->c[0].index == -1);
    spectre_coords_free(sc);

    /* Test the double Spectre transition. Here, within a F superhex,
     * we attempt to step from the G subhex to the S one, in such a
     * way that the place where we enter the Spectre corresponding to
     * the S hex is on its spur of detached edge, causing us to
     * immediately transition back out of the other side of that spur
     * and end up in the D subhex instead. */
    sc = spectre_coords_new();
    spectre_coords_make_space(sc, 2);
    sc->index = 1;
    sc->nc = 2;
    sc->c[0].type = HEX_G;
    sc->c[0].index = 2;
    sc->c[1].type = HEX_F;
    sc->c[1].index = -1;
    spectrectx_step(ctx, sc, 1, &outedge);
    assert(outedge == 6);
    assert(sc->index == 0);
    assert(sc->nc == 2);
    assert(sc->c[0].type == HEX_D);
    assert(sc->c[0].index == 5);
    assert(sc->c[1].type == HEX_F);
    assert(sc->c[1].index == -1);
    spectre_coords_free(sc);

    /* However, _this_ transition leaves the same G subhex by the same
     * edge of the hexagon, but further along it, so that we land in
     * the S Spectre and stay there, without needing a double
     * transition. */
    sc = spectre_coords_new();
    spectre_coords_make_space(sc, 2);
    sc->index = 1;
    sc->nc = 2;
    sc->c[0].type = HEX_G;
    sc->c[0].index = 2;
    sc->c[1].type = HEX_F;
    sc->c[1].index = -1;
    spectrectx_step(ctx, sc, 13, &outedge);
    assert(outedge == 4);
    assert(sc->index == 0);
    assert(sc->nc == 2);
    assert(sc->c[0].type == HEX_S);
    assert(sc->c[0].index == 3);
    assert(sc->c[1].type == HEX_F);
    assert(sc->c[1].index == -1);
    spectre_coords_free(sc);

    /* A couple of randomly generated transition tests that go a long
     * way up the stack. */
    sc = spectre_coords_new();
    spectre_coords_make_space(sc, 7);
    sc->index = 0;
    sc->nc = 7;
    sc->c[0].type = HEX_S;
    sc->c[0].index = 3;
    sc->c[1].type = HEX_Y;
    sc->c[1].index = 7;
    sc->c[2].type = HEX_Y;
    sc->c[2].index = 4;
    sc->c[3].type = HEX_Y;
    sc->c[3].index = 4;
    sc->c[4].type = HEX_F;
    sc->c[4].index = 0;
    sc->c[5].type = HEX_X;
    sc->c[5].index = 1;
    sc->c[6].type = HEX_G;
    sc->c[6].index = -1;
    spectrectx_step(ctx, sc, 13, &outedge);
    assert(outedge == 12);
    assert(sc->index == 0);
    assert(sc->nc == 7);
    assert(sc->c[0].type == HEX_Y);
    assert(sc->c[0].index == 1);
    assert(sc->c[1].type == HEX_P);
    assert(sc->c[1].index == 1);
    assert(sc->c[2].type == HEX_D);
    assert(sc->c[2].index == 5);
    assert(sc->c[3].type == HEX_Y);
    assert(sc->c[3].index == 4);
    assert(sc->c[4].type == HEX_X);
    assert(sc->c[4].index == 7);
    assert(sc->c[5].type == HEX_S);
    assert(sc->c[5].index == 3);
    assert(sc->c[6].type == HEX_G);
    assert(sc->c[6].index == -1);
    spectre_coords_free(sc);

    sc = spectre_coords_new();
    spectre_coords_make_space(sc, 7);
    sc->index = 0;
    sc->nc = 7;
    sc->c[0].type = HEX_Y;
    sc->c[0].index = 7;
    sc->c[1].type = HEX_F;
    sc->c[1].index = 6;
    sc->c[2].type = HEX_Y;
    sc->c[2].index = 4;
    sc->c[3].type = HEX_X;
    sc->c[3].index = 7;
    sc->c[4].type = HEX_L;
    sc->c[4].index = 0;
    sc->c[5].type = HEX_S;
    sc->c[5].index = 3;
    sc->c[6].type = HEX_F;
    sc->c[6].index = -1;
    spectrectx_step(ctx, sc, 0, &outedge);
    assert(outedge == 1);
    assert(sc->index == 0);
    assert(sc->nc == 7);
    assert(sc->c[0].type == HEX_P);
    assert(sc->c[0].index == 1);
    assert(sc->c[1].type == HEX_F);
    assert(sc->c[1].index == 0);
    assert(sc->c[2].type == HEX_Y);
    assert(sc->c[2].index == 7);
    assert(sc->c[3].type == HEX_F);
    assert(sc->c[3].index == 0);
    assert(sc->c[4].type == HEX_G);
    assert(sc->c[4].index == 2);
    assert(sc->c[5].type == HEX_D);
    assert(sc->c[5].index == 5);
    assert(sc->c[6].type == HEX_F);
    assert(sc->c[6].index == -1);
    spectre_coords_free(sc);

    spectrectx_cleanup(ctx);
    random_free(rs);
}

struct genctx {
    Graphics *gr;
    FILE *fp; /* for non-graphical output modes */
    random_state *rs;
    Coord xmin, xmax, ymin, ymax;
};

static void gctx_set_size(
    struct genctx *gctx, int width, int height, double scale, bool centre,
    int *xmin, int *xmax, int *ymin, int *ymax)
{
    if (centre) {
        *xmax = ceil(width/(2*scale));
        *xmin = -*xmax;
        *ymax = ceil(height/(2*scale));
        *ymin = -*ymax;
    } else {
        *xmin = *ymin = 0;
        *xmax = ceil(width/scale);
        *ymax = ceil(height/scale);
    }

    /* point_x() and point_y() double their output to avoid having
     * to use fractions, so double the bounds we'll compare their
     * results against */
    gctx->xmin.c1 = *xmin * 2; gctx->xmin.cr3 = 0;
    gctx->xmax.c1 = *xmax * 2; gctx->xmax.cr3 = 0;
    gctx->ymin.c1 = *ymin * 2; gctx->ymin.cr3 = 0;
    gctx->ymax.c1 = *ymax * 2; gctx->ymax.cr3 = 0;
}

static bool callback(void *vctx, const Spectre *spec)
{
    struct genctx *gctx = (struct genctx *)vctx;
    size_t i;

    for (i = 0; i < 14; i++) {
        Point p = spec->vertices[i];
        Coord x = point_x(p), y = point_y(p);
        if (coord_cmp(x, gctx->xmin) >= 0 && coord_cmp(x, gctx->xmax) <= 0 &&
            coord_cmp(y, gctx->ymin) >= 0 && coord_cmp(y, gctx->ymax) <= 0)
            goto ok;
    }
    return false;

  ok:
    gr_draw_spectre_from_coords(gctx->gr, spec->sc, spec->vertices);
    if (gctx->fp) {
        /*
         * Emit calls to a made-up Python 'spectre()' function which
         * takes the following parameters:
         *
         *  - lowest-level hexagon type (one-character string)
         *  - index of Spectre within hexagon (0 or rarely 1)
         *  - array of 14 point coordinates. Each is a 2-tuple
         *    containing x and y. Each of those in turn is a 2-tuple
         *    containing coordinates of 1 and sqrt(3).
         */
        fprintf(gctx->fp, "spectre('%s', %d, [",
                hex_names[spec->sc->c[0].type], spec->sc->index);
        for (i = 0; i < 14; i++) {
            Point p = spec->vertices[i];
            Coord x = point_x(p), y = point_y(p);
            fprintf(gctx->fp, "%s((%d,%d),(%d,%d))", i ? ", " : "",
                    x.c1, x.cr3, y.c1, y.cr3);
        }
        fprintf(gctx->fp, "])\n");
    }
    return true;
}

static void spectrectx_init_random_with_four_colouring(
    SpectreContext *ctx, random_state *rs)
{
    spectrectx_init_random(ctx, rs);
    ctx->prototype->hex_colour = random_upto(rs, 3);
    ctx->prototype->prev_hex_colour = (ctx->prototype->hex_colour + 1 +
                                       random_upto(rs, 2)) % 3;
    ctx->prototype->incoming_hex_edge = random_upto(rs, 2);
}

static void generate_bfs(struct genctx *gctx)
{
    SpectreContext ctx[1];
    
    spectrectx_init_random_with_four_colouring(ctx, gctx->rs);
    spectrectx_generate(ctx, callback, gctx);
    spectrectx_cleanup(ctx);
}

static inline Point reflected(Point p)
{
    /*
     * This reflection operation is used as a conjugation by
     * periodic_cheat(). For that purpose, it doesn't matter _what_
     * reflection it is, only that it reverses sense.
     *
     * generate_raster() also uses it to conjugate between the 'find
     * edges intersecting a horizontal line' and 'ditto vertical'
     * operations, so for that purpose, it wants to be the specific
     * reflection about the 45-degree line that swaps the positive x-
     * and y-axes.
     */
    Point r;
    size_t i;
    for (i = 0; i < 4; i++)
        r.coeffs[i] = p.coeffs[3-i];
    return r;
}
static void reflect_spectre(Spectre *spec)
{
    size_t i;
    for (i = 0; i < 14; i++)
        spec->vertices[i] = reflected(spec->vertices[i]);
}

static void periodic_cheat(struct genctx *gctx)
{
    Spectre start, sh, sv;
    size_t i;

    start.sc = NULL;
    {
        Point u = {{ 0, 0, 0, 0 }};
        Point v = {{ 1, 0, 0, 1 }};
        v = point_mul(v, point_rot(1));
        spectre_place(&start, u, v, 0);
    }

    sh = start;
    while (callback(gctx, &sh)) {
        sv = sh;
        i = 0;
        do {
            if (i) {
                spectre_place(&sv, sv.vertices[6], sv.vertices[7], 0);
            } else {
                spectre_place(&sv, reflected(sv.vertices[6]),
                              reflected(sv.vertices[7]), 0);
                reflect_spectre(&sv);
            }
            i ^= 1;
        } while (callback(gctx, &sv));

        sv = sh;
        i = 0;
        do {
            if (i) {
                spectre_place(&sv, sv.vertices[0], sv.vertices[1], 6);
            } else {
                spectre_place(&sv, reflected(sv.vertices[0]),
                              reflected(sv.vertices[1]), 6);
                reflect_spectre(&sv);
            }
            i ^= 1;
        } while (callback(gctx, &sv));

        spectre_place(&sh, sh.vertices[12], sh.vertices[11], 4);
    }

    sh = start;
    do {
        spectre_place(&sh, sh.vertices[5], sh.vertices[4], 11);

        sv = sh;
        i = 0;
        do {
            if (i) {
                spectre_place(&sv, sv.vertices[6], sv.vertices[7], 0);
            } else {
                spectre_place(&sv, reflected(sv.vertices[6]),
                              reflected(sv.vertices[7]), 0);
                reflect_spectre(&sv);
            }
            i ^= 1;
        } while (callback(gctx, &sv));

        sv = sh;
        i = 0;
        do {
            if (i) {
                spectre_place(&sv, sv.vertices[0], sv.vertices[1], 6);
            } else {
                spectre_place(&sv, reflected(sv.vertices[0]),
                              reflected(sv.vertices[1]), 6);
                reflect_spectre(&sv);
            }
            i ^= 1;
        } while (callback(gctx, &sv));
    } while (callback(gctx, &sh));
}

static Spectre *spectre_copy(const Spectre *orig)
{
    Spectre *copy = snew(Spectre);
    memcpy(copy->vertices, orig->vertices, sizeof(copy->vertices));
    copy->sc = spectre_coords_copy(orig->sc);
    copy->next = NULL;                 /* not used in this tool */
    return copy;
}

static size_t find_crossings(struct genctx *gctx, const Spectre *spec, Coord y,
                             size_t direction, unsigned *edges_out)
{
    /*
     * Find edges of this Spectre which cross the horizontal line
     * specified by the coordinate y.
     *
     * For tie-breaking purposes, we're treating the line as actually
     * being at y + epsilon, so that a line with one endpoint _on_
     * that coordinate is counted as crossing it if it goes upwards,
     * and not downwards. Put another way, we seek edges one of whose
     * vertices is < y and the other >= y.
     *
     * Also, we're only interested in crossings in a particular
     * direction, specified by 'direction' being 0 or 1.
     */
    size_t i, j;
    struct Edge {
        unsigned edge;
        /* Location of the crossing point, as the ratio of two Coord */
        Coord n, d;
    } edges[14];
    size_t nedges = 0;

    for (i = 0; i < 14; i++) {
        Coord yc[2], d[2];

        yc[0] = point_y(spec->vertices[i]);
        yc[1] = point_y(spec->vertices[(i+1) % 14]);
        for (j = 0; j < 2; j++)
            d[j] = coord_sub(yc[j], y);
        if (coord_sign(d[1-direction]) >= 0 && coord_sign(d[direction]) < 0) {
            Coord a0 = coord_abs(d[0]), a1 = coord_abs(d[1]);
            Coord x0 = point_x(spec->vertices[i]);
            Coord x1 = point_x(spec->vertices[(i+1) % 14]);

            edges[nedges].d = coord_add(a0, a1);
            edges[nedges].n = coord_add(coord_mul(a1, x0), coord_mul(a0, x1));
            edges[nedges].edge = i;

            nedges++;

            /*
             * Insertion sort: swap this edge backwards in the array
             * until it's in the right order.
             */
            {
                size_t j = nedges - 1;
                while (j > 0 && coord_cmp(
                           coord_mul(edges[j-1].n, edges[j].d),
                           coord_mul(edges[j].n, edges[j-1].d)) > 0) {
                    struct Edge tmp = edges[j-1];
                    edges[j-1] = edges[j];
                    edges[j] = tmp;
                    j--;
                }
            }
        }
    }

    for (i = 0; i < nedges; i++)
        edges_out[i] = edges[i].edge;
    return nedges;
}

static void raster_emit(struct genctx *gctx, const Spectre *spec,
                        Coord y, unsigned edge)
{
    unsigned edges[14];
    size_t nedges;

    Coord yprev = coord_sub(y, coord_construct(2, 4));
    if (find_crossings(gctx, spec, yprev, true, edges))
        return;      /* we've seen this on a previous raster_x pass */

    if (edge != (unsigned)-1) {
        nedges = find_crossings(gctx, spec, y, false, edges);
        assert(nedges > 0);
        if (edge != edges[0])
            return; /* we've seen this before within the same raster_x pass */
    }

    callback(gctx, spec);
}

static void raster_x(struct genctx *gctx, SpectreContext *ctx,
                     const Spectre *start, Coord *yptr, Coord xlimit)
{
    Spectre *curr, *new;
    Coord y;
    size_t i;
    unsigned incoming_edge;

    /*
     * Find out if this Spectre intersects our current
     * y-coordinate.
     */
    for (i = 0; i < 14; i++)
        if (coord_cmp(point_y(start->vertices[i]), *yptr) > 0)
            break;
    if (i == 14) {
        /*
         * No, this Spectre is still below the start line.
         */
        return;
    }

    /*
     * It does! Start an x iteration here, and increment y by 2 + 4
     * sqrt(3), which is the smallest possible y-extent of any
     * rotation of our starting Spectre.
     */
    y = *yptr;
    *yptr = coord_add(*yptr, coord_construct(2, 4));

    curr = spectre_copy(start);
    incoming_edge = -1;
    while (true) {
        unsigned edges[14];
        size_t nedges;

        raster_emit(gctx, curr, y, incoming_edge);

        nedges = find_crossings(gctx, curr, y, true, edges);

        assert(nedges > 0);

        for (i = 0; i+1 < nedges; i++) {
            new = spectre_adjacent(ctx, curr, edges[i], &incoming_edge);
            raster_emit(gctx, new, y, incoming_edge);
            spectre_free(new);
        }

        new = spectre_adjacent(ctx, curr, edges[nedges-1], &incoming_edge);
        spectre_free(curr);
        curr = new;

        /*
         * Find out whether this Spectre is entirely beyond the
         * x-limit.
         */
        for (i = 0; i < 14; i++)
            if (coord_cmp(point_x(curr->vertices[i]), xlimit) < 0)
                break;
        if (i == 14)                   /* no vertex broke that loop */
            break;
    }
    spectre_free(curr);
}
static void raster_y(struct genctx *gctx, SpectreContext *ctx,
                     const Spectre *start, Coord x, Coord ylimit,
                     Coord *yptr, Coord xlimit)
{
    Spectre *curr, *new;

    curr = spectre_copy(start);

    while (true) {
        unsigned edges[14];
        size_t i, nedges;

        raster_x(gctx, ctx, curr, yptr, xlimit);

        reflect_spectre(curr);
        nedges = find_crossings(gctx, curr, x, false, edges);
        reflect_spectre(curr);

        assert(nedges > 0);

        for (i = 0; i+1 < nedges; i++) {
            new = spectre_adjacent(ctx, curr, edges[i], NULL);
            raster_x(gctx, ctx, new, yptr, xlimit);
            spectre_free(new);
        }

        new = spectre_adjacent(ctx, curr, edges[nedges-1], NULL);
        spectre_free(curr);
        curr = new;

        /*
         * Find out whether this Spectre is entirely beyond the
         * y-limit.
         */
        for (i = 0; i < 14; i++)
            if (coord_cmp(point_y(curr->vertices[i]), ylimit) < 0)
                break;
        if (i == 14)                   /* no vertex broke that loop */
            break;
    }
    spectre_free(curr);
}

static void generate_raster(struct genctx *gctx)
{
    SpectreContext ctx[1];
    Spectre *start;
    Coord y = coord_integer(-10);

    spectrectx_init_random_with_four_colouring(ctx, gctx->rs);

    start = spectre_initial(ctx);

    /*
     * Move the starting Spectre down and left a bit, so that edge
     * effects causing a few Spectres to be missed on the initial
     * passes won't affect the overall result.
     */
    {
        Point offset = {{ -5, 0, 0, -5 }};
        size_t i;
        for (i = 0; i < 14; i++)
            start->vertices[i] = point_add(start->vertices[i], offset);
    }

    raster_y(gctx, ctx, start, coord_integer(-10), gctx->ymax, &y, gctx->xmax);
    spectre_free(start);

    spectrectx_cleanup(ctx);
}

static void generate_hexes(struct genctx *gctx)
{
    SpectreContext ctx[1];
    spectrectx_init_random(ctx, gctx->rs);
    SpectreCoords *sc;
    unsigned orient, outedge, inedge;
    bool printed_any = false;
    size_t r = 1, ri = 0, rj = 0;

    Point centre = {{ 0, 0, 0, 0 }};
    const Point six = {{ 6, 0, 0, 0 }};

    sc = spectre_coords_copy(ctx->prototype);
    orient = random_upto(gctx->rs, 6);

    while (true) {
        Point top = {{ -2, 0, 4, 0 }};
        Point vertices[6];
        bool print_this = false;
        size_t i;

        for (i = 0; i < 6; i++) {
            vertices[i] = point_add(centre, point_mul(
                                        top, point_rot(2 * (orient + i))));
            Coord x = point_x(vertices[i]), y = point_y(vertices[i]);
            if (coord_cmp(x, gctx->xmin) >= 0 &&
                coord_cmp(x, gctx->xmax) <= 0 &&
                coord_cmp(y, gctx->ymin) >= 0 &&
                coord_cmp(y, gctx->ymax) <= 0)
                print_this = true;
        }

        if (print_this) {
            printed_any = true;
            gr_draw_hex(gctx->gr, -1, sc->c[0].type, vertices);
        }

        /*
         * Decide which way to step next. We spiral outwards from a
         * central hexagon.
         */
        outedge = (ri == 0 && rj == 0) ? 5 : ri;
        if (++rj >= r) {
            rj = 0;
            if (++ri >= 6) {
                ri = 0;
                if (!printed_any)
                    break;
                printed_any = false;
                ++r;
            }
        }

        spectrectx_step_hex(ctx, sc, 0, (outedge + 6 - orient) % 6, &inedge);
        orient = (outedge + 9 - inedge) % 6;

        centre = point_add(centre, point_mul(six, point_rot(4 + 2 * outedge)));
    }

    spectre_coords_free(sc);
    spectrectx_cleanup(ctx);
}

int main(int argc, char **argv)
{
    const char *random_seed = "12345";
    const char *outfile = "-";
    bool four_colour = false;
    enum {
        TESTS, TILING_BFS, TILING_RASTER, CHEAT, HEXES
    } mode = TILING_RASTER;
    enum { SVG, PYTHON } outmode = SVG;
    double scale = 10, linewidth = 1.5;
    int width = 1024, height = 768;
    bool arcs = false;

    while (--argc > 0) {
        const char *arg = *++argv;
        if (!strcmp(arg, "--help")) {
            printf("  usage: spectre-test [FIXME]\n"
                   "   also: spectre-test --test\n");
            return 0;
        } else if (!strcmp(arg, "--test")) {
            mode = TESTS;
        } else if (!strcmp(arg, "--hex")) {
            mode = HEXES;
        } else if (!strcmp(arg, "--bfs")) {
            mode = TILING_BFS;
        } else if (!strcmp(arg, "--cheat")) {
            mode = CHEAT;
        } else if (!strcmp(arg, "--python")) {
            outmode = PYTHON;
        } else if (!strcmp(arg, "--arcs")) {
            arcs = true;
        } else if (!strncmp(arg, "--seed=", 7)) {
            random_seed = arg+7;
        } else if (!strcmp(arg, "--fourcolour")) {
            four_colour = true;
        } else if (!strncmp(arg, "--scale=", 8)) {
            scale = atof(arg+8);
        } else if (!strncmp(arg, "--width=", 8)) {
            width = atof(arg+8);
        } else if (!strncmp(arg, "--height=", 9)) {
            height = atof(arg+9);
        } else if (!strncmp(arg, "--linewidth=", 12)) {
            linewidth = atof(arg+12);
        } else if (!strcmp(arg, "-o")) {
            if (--argc <= 0) {
                fprintf(stderr, "expected argument to '%s'\n", arg);
                return 1;
            }
            outfile = *++argv;
        } else {
            fprintf(stderr, "unexpected extra argument '%s'\n", arg);
            return 1;
        }
    }

    switch (mode) {
      case TESTS: {
        step_tests();
        break;
      }

      case TILING_BFS:
      case TILING_RASTER:
      case CHEAT: {
        struct genctx gctx[1];
        bool close_output = false;
        int xmin, xmax, ymin, ymax;

        gctx_set_size(gctx, width, height, scale, (mode != TILING_RASTER),
                      &xmin, &xmax, &ymin, &ymax);

        switch (outmode) {
          case SVG:
            gctx->gr = gr_new(outfile, xmin, xmax, ymin, ymax, scale);
            gctx->gr->number_cells = false;
            gctx->gr->four_colour = four_colour;
            gctx->gr->linewidth = linewidth;
            gctx->gr->arcs = arcs;
            gctx->fp = NULL;
            break;
          case PYTHON:
            gctx->gr = NULL;
            if (!strcmp(outfile, "-")) {
                gctx->fp = stdout;
            } else {
                gctx->fp = fopen(outfile, "w");
                close_output = true;
            }
            break;
        }

        gctx->rs = random_new(random_seed, strlen(random_seed));
        switch (mode) {
          case TILING_RASTER:
            generate_raster(gctx);
            break;
          case TILING_BFS:
            generate_bfs(gctx);
            break;
          case CHEAT:
            periodic_cheat(gctx);
            break;
          default: /* shouldn't happen */
            break;
        }
        random_free(gctx->rs);
        gr_free(gctx->gr);
        if (close_output)
            fclose(gctx->fp);
        break;
      }

      case HEXES: {
        struct genctx gctx[1];
        int xmin, xmax, ymin, ymax;

        gctx_set_size(gctx, width, height, scale, true,
                      &xmin, &xmax, &ymin, &ymax);

        gctx->gr = gr_new(outfile, xmin, xmax, ymin, ymax, scale);
        gctx->gr->jigsaw_mode = true;
        gctx->gr->number_edges = false;
        gctx->gr->linewidth = linewidth;
        gctx->rs = random_new(random_seed, strlen(random_seed));
        generate_hexes(gctx);          /* FIXME: bounds */
        random_free(gctx->rs);
        gr_free(gctx->gr);
        break;
      }
    }
}
