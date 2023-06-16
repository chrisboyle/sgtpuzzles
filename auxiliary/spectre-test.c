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
    struct genctx *gctx, int width, int height, double scale,
    int *xmin, int *xmax, int *ymin, int *ymax)
{
    *xmax = ceil(width/(2*scale));
    *xmin = -*xmax;
    *ymax = ceil(height/(2*scale));
    *ymin = -*ymax;

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

static void generate(struct genctx *gctx)
{
    SpectreContext ctx[1];
    
    spectrectx_init_random(ctx, gctx->rs);
    ctx->prototype->hex_colour = random_upto(gctx->rs, 3);
    ctx->prototype->prev_hex_colour = (ctx->prototype->hex_colour + 1 +
                                       random_upto(gctx->rs, 2)) % 3;
    ctx->prototype->incoming_hex_edge = random_upto(gctx->rs, 2);

    spectrectx_generate(ctx, callback, gctx);

    spectrectx_cleanup(ctx);
}

static inline Point reflected(Point p)
{
    /*
     * This reflection operation is used as a conjugation, so it
     * doesn't matter _what_ reflection it is, only that it reverses
     * sense.
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
    enum { TESTS, TILING, CHEAT, HEXES } mode = TILING;
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

      case TILING:
      case CHEAT: {
        struct genctx gctx[1];
        bool close_output = false;
        int xmin, xmax, ymin, ymax;

        gctx_set_size(gctx, width, height, scale, &xmin, &xmax, &ymin, &ymax);

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
          case TILING:
            generate(gctx);
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

        gctx_set_size(gctx, width, height, scale, &xmin, &xmax, &ymin, &ymax);

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
