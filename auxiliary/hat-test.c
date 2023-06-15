/*
 * Standalone test program for hat.c, which generates patches of hat
 * tiling in multiple output formats without also generating a Loopy
 * puzzle around them.
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

#include "hat-internal.h"

static HatCoords *hat_coords_construct_v(TileType type, va_list ap)
{
    HatCoords *hc = hat_coords_new();
    while (true) {
        int index = va_arg(ap, int);

        hat_coords_make_space(hc, hc->nc + 1);
        hc->c[hc->nc].type = type;
        hc->c[hc->nc].index = index;
        hc->nc++;

        if (index < 0)
            return hc;

        type = va_arg(ap, TileType);
    }
}

static HatCoords *hat_coords_construct(TileType type, ...)
{
    HatCoords *hc;
    va_list ap;

    va_start(ap, type);
    hc = hat_coords_construct_v(type, ap);
    va_end(ap);

    return hc;
}

static bool hat_coords_equal(HatCoords *hc1, HatCoords *hc2)
{
    size_t i;

    if (hc1->nc != hc2->nc)
        return false;

    for (i = 0; i < hc1->nc; i++) {
        if (hc1->c[i].type != hc2->c[i].type ||
            hc1->c[i].index != hc2->c[i].index)
            return false;
    }

    return true;
}

static bool hat_coords_expect(const char *file, int line, HatCoords *hc,
                              TileType type, ...)
{
    bool equal;
    va_list ap;
    HatCoords *hce;

    va_start(ap, type);
    hce = hat_coords_construct_v(type, ap);
    va_end(ap);

    equal = hat_coords_equal(hc, hce);

    if (!equal) {
        fprintf(stderr, "%s:%d: coordinate mismatch\n", file, line);
        hat_coords_debug("  expected: ", hce, "\n");
        hat_coords_debug("  actual:   ", hc, "\n");
    }

    hat_coords_free(hce);
    return equal;
}

#define EXPECT(hc, ...) do {                                            \
        if (!hat_coords_expect(__FILE__, __LINE__, hc, __VA_ARGS__))    \
            fails++;                                                    \
    } while (0)

/*
 * For four-colouring the tiling: these tables give a colouring of
 * each kitemap, with colour 3 assigned to the reflected tiles in the
 * middle of the H, and 0,1,2 chosen arbitrarily.
 */

static const int fourcolours_H[] = {
    /*  0 */ 0,  2,  1,  3,
    /*  1 */ 1,  0,  2,  3,
    /*  2 */ 0,  2,  1,  3,
    /*  3 */ 1, -1, -1, -1,
    /*  4 */ 1,  2, -1, -1,
    /*  5 */ 1,  2, -1, -1,
    /*  6 */ 2,  1, -1, -1,
    /*  7 */ 0,  1, -1, -1,
    /*  8 */ 2,  0, -1, -1,
    /*  9 */ 2,  0, -1, -1,
    /* 10 */ 0,  1, -1, -1,
    /* 11 */ 0,  1, -1, -1,
    /* 12 */ 2,  0, -1, -1,
};
static const int fourcolours_T[] = {
    /*  0 */ 1,  2,  0,  3,
    /*  1 */ 2,  1, -1, -1,
    /*  2 */ 0,  1, -1, -1,
    /*  3 */ 0,  2, -1, -1,
    /*  4 */ 2,  0, -1, -1,
    /*  5 */ 0,  1, -1, -1,
    /*  6 */ 1,  2, -1, -1,
};
static const int fourcolours_P[] = {
    /*  0 */ 2,  1,  0,  3,
    /*  1 */ 1,  2,  0,  3,
    /*  2 */ 2,  1, -1, -1,
    /*  3 */ 0,  2, -1, -1,
    /*  4 */ 0,  1, -1, -1,
    /*  5 */ 1,  2, -1, -1,
    /*  6 */ 2,  0, -1, -1,
    /*  7 */ 0,  1, -1, -1,
    /*  8 */ 1,  0, -1, -1,
    /*  9 */ 2,  1, -1, -1,
    /* 10 */ 0,  2, -1, -1,
};
static const int fourcolours_F[] = {
    /*  0 */ 2,  0,  1,  3,
    /*  1 */ 0,  2,  1,  3,
    /*  2 */ 1,  2, -1, -1,
    /*  3 */ 1,  0, -1, -1,
    /*  4 */ 0,  2, -1, -1,
    /*  5 */ 2,  1, -1, -1,
    /*  6 */ 2,  0, -1, -1,
    /*  7 */ 0,  1, -1, -1,
    /*  8 */ 0,  1, -1, -1,
    /*  9 */ 2,  0, -1, -1,
    /* 10 */ 1,  2, -1, -1,
};
static const int *const fourcolours[] = {
    fourcolours_H, fourcolours_T, fourcolours_P, fourcolours_F,
};

static bool unit_tests(void)
{
    int fails = 0;
    HatContext ctx[1];
    HatCoords *hc_in, *hc_out;

    ctx->rs = NULL;
    ctx->prototype = hat_coords_construct(TT_KITE, 0, TT_HAT, 0, TT_H, -1);

    /* Simple steps within a hat */

    hc_in = hat_coords_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_LEFT);
    EXPECT(hc_out, TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    hc_in = hat_coords_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_RIGHT);
    EXPECT(hc_out, TT_KITE, 7, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    hc_in = hat_coords_construct(TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_F_LEFT);
    EXPECT(hc_out, TT_KITE, 2, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    hc_in = hat_coords_construct(TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_F_RIGHT);
    EXPECT(hc_out, TT_KITE, 1, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    /* Step between hats in the same kitemap, which can change the
     * metatile type at layer 2 */

    hc_in = hat_coords_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_F_LEFT);
    EXPECT(hc_out, TT_KITE, 3, TT_HAT, 0, TT_H, 0, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    hc_in = hat_coords_construct(TT_KITE, 7, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_F_RIGHT);
    EXPECT(hc_out, TT_KITE, 4, TT_HAT, 0, TT_T, 3, TT_H, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    /* Step off the edge of one kitemap, necessitating a metamap
     * rewrite of layers 2,3 to get into a different kitemap where
     * that step can be made */

    hc_in = hat_coords_construct(TT_KITE, 6, TT_HAT, 0, TT_P, 2, TT_P, 3,
                                 TT_P, -1);
    hc_out = hatctx_step(ctx, hc_in, KS_F_RIGHT);
    /* Working:
     *     kite 6 . hat 0 . P 2 . P 3 . P ?
     *  -> kite 6 . hat 0 . P 6 . H 0 . P ?   (P metamap says 2.3 = 6.0)
     */
    EXPECT(hc_out, TT_KITE, 7, TT_HAT, 1, TT_H, 1, TT_H, 0, TT_P, -1);
    hat_coords_free(hc_in);
    hat_coords_free(hc_out);

    hatctx_cleanup(ctx);
    return fails == 0;
}

/*
 * Structure that describes how the colours in the above maps are
 * translated to output colours. This will vary with each kitemap our
 * coordinates pass through, in order to maintain consistency.
 */
typedef struct FourColourMap {
    unsigned char map[4];
} FourColourMap;

/*
 * Make an initial FourColourMap by choosing the initial permutation
 * of the three 'normal' hat colours randomly.
 */
static inline FourColourMap fourcolourmap_initial(random_state *rs)
{
    FourColourMap f;
    unsigned i;

    /* Start with the identity mapping */
    for (i = 0; i < 4; i++)
        f.map[i] = i;

    /* Randomly permute colours 0,1,2, leaving 3 as the distinguished
     * colour for reflected hats */
    shuffle(f.map, 3, sizeof(f.map[0]), rs);

    return f;
}

static inline FourColourMap fourcolourmap_update(
    FourColourMap prevm, HatCoords *prevc, HatCoords *currc, KiteStep step,
    HatContext *ctx)
{
    size_t i, m1, m2;
    const int *f1, *f2;
    unsigned sum;
    int missing;
    FourColourMap newm;
    HatCoords *prev2c;

    /*
     * If prevc and currc are in the same kitemap anyway, that's the
     * easy case: the colour map for the new kitemap is the same as
     * for the old one, because they're the same kitemap.
     */
    hatctx_extend_coords(ctx, prevc, currc->nc);
    hatctx_extend_coords(ctx, currc, prevc->nc);
    for (i = 3; i < prevc->nc; i++)
        if (currc->c[i].index != prevc->c[i].index)
            goto mismatch;
    return prevm;
  mismatch:

    /*
     * The hatctx_step algorithm guarantees that the _new_ coordinate
     * currc is expected to be in a kitemap containing both this kite
     * and the previous one (because it first transformed the previous
     * coordinate until it _could_ take a step within the same
     * kitemap, and then did).
     *
     * So if we reverse the last step we took, we should get a second
     * HatCoords describing the same kite as prevc but showing its
     * position in the _new_ kitemap. This lets us figure out a pair
     * of corresponding metatile indices within the old and new
     * kitemaps (by looking at which metatile prevc and prev2c claim
     * to be in).
     *
     * That metatile will also always be a P or an F (because all
     * metatiles overlapping the next kitemap are of those types),
     * which means it will have two hats in it. And those hats will be
     * adjacent, so differently coloured. Hence, we have enough
     * information to decide how two of the new kitemap's three normal
     * colours map to the colours we were using in the old kitemap -
     * and then the third is determined by process of elimination.
     */
    prev2c = hatctx_step(
        ctx, currc, (step == KS_LEFT ? KS_RIGHT :
                     step == KS_RIGHT ? KS_LEFT :
                     step == KS_F_LEFT ? KS_F_RIGHT : KS_F_LEFT));

    /* Metatile indices within the old and new kitemaps */
    m1 = prevc->c[2].index;
    m2 = prev2c->c[2].index;

    /* The colourings of those metatiles' hats in our fixed fourcolours[] */
    f1 = fourcolours[prevc->c[3].type] + 4*m1;
    f2 = fourcolours[prev2c->c[3].type] + 4*m2;

    hat_coords_free(prev2c);

    /*
     * Start making our new output map, filling in all three normal
     * colours to 255 = "don't know yet".
     */
    newm.map[3] = 3;
    newm.map[0] = newm.map[1] = newm.map[2] = 255;

    /*
     * Iterate over the tile colourings in fourcolours[] for these
     * metatiles, matching up our mappings.
     */
    for (i = 0; i < 4; i++) {
        /* They should be the same metatile, so have same number of hats! */
        if (f1[i] == -1 && f2[i] == -1)
            continue;
        assert(f1[i] != -1 && f2[i] != -1);

        if (f1[i] != 255)
            newm.map[f2[i]] = prevm.map[f1[i]];
    }

    /*
     * We expect to have filled in exactly two of the three normal
     * colours. Find the missing index, and fill in its colour by
     * arithmetic (using the fact that the three colours add up to 3).
     */
    sum = 0;
    missing = -1;
    for (i = 0; i < 3; i++) {
        if (newm.map[i] == 255) {
            assert(missing == -1); /* shouldn't have two missing colours */
            missing = i;
        } else {
            sum += newm.map[i];
        }
    }
    assert(missing != -1);
    assert(0 < sum && sum <= 3);
    newm.map[missing] = 3 - sum;

    return newm;
}

typedef struct pspoint {
    float x, y;
} pspoint;

typedef struct psbbox {
    bool started;
    pspoint bl, tr;
} psbbox;

static inline void psbbox_add(psbbox *bbox, pspoint p)
{
    if (!bbox->started || bbox->bl.x > p.x)
        bbox->bl.x = p.x;
    if (!bbox->started || bbox->tr.x < p.x)
        bbox->tr.x = p.x;
    if (!bbox->started || bbox->bl.y > p.y)
        bbox->bl.y = p.y;
    if (!bbox->started || bbox->tr.y < p.y)
        bbox->tr.y = p.y;
    bbox->started = true;
}

typedef enum OutFmt { OF_POSTSCRIPT, OF_SVG, OF_PYTHON } OutFmt;
typedef enum ColourMode { CM_SEMANTIC, CM_FOURCOLOUR } ColourMode;

typedef struct drawctx {
    OutFmt outfmt;
    ColourMode colourmode;
    psbbox *bbox;
    KiteEnum *kiteenum;
    FourColourMap fourcolourmap[KE_NKEEP];
    bool natural_scale, clip;

    float xoff, xscale, yoff, yscale;  /* used for SVG only */
} drawctx;

static void bbox_add_hat(void *vctx, Kite kite0, HatCoords *hc, int *coords)
{
    drawctx *ctx = (drawctx *)vctx;
    pspoint p;
    size_t i;

    for (i = 0; i < 14; i++) {
        p.x = coords[2*i] * 1.5;
        p.y = coords[2*i+1] * sqrt(0.75);
        psbbox_add(ctx->bbox, p);
    }
}

static void header(drawctx *ctx)
{
    float scale, ox, oy;

    /* Optionally clip to an inner rectangle that guarantees
     * the whole visible area is covered in hats. */
    if (ctx->clip) {
        ctx->bbox->bl.x += 9;
        ctx->bbox->tr.x -= 9;
        ctx->bbox->bl.y += 12 * sqrt(0.75);
        ctx->bbox->tr.y -= 12 * sqrt(0.75);
    }

    if (!ctx->natural_scale) {
        /* Scale the output to fit on an A4 page, for test prints. */
        float w = 595, h = 842, margin = 12;
        float xext = ctx->bbox->tr.x - ctx->bbox->bl.x;
        float yext = ctx->bbox->tr.y - ctx->bbox->bl.y;
        float xscale = (w - 2*margin) / xext;
        float yscale = (h - 2*margin) / yext;
        scale = xscale < yscale ? xscale : yscale;
        ox = (w - scale * (ctx->bbox->bl.x + ctx->bbox->tr.x)) / 2;
        oy = (h - scale * (ctx->bbox->bl.y + ctx->bbox->tr.y)) / 2;
    } else {
        /* Leave the patch at its natural scale. */
        scale = 1.0;

        /* And translate the lower left corner of the bounding box to 0. */
        ox = -ctx->bbox->bl.x;
        oy = -ctx->bbox->bl.y;
    }

    switch (ctx->outfmt) {
      case OF_POSTSCRIPT: {
        printf("%%!PS-Adobe-2.0\n%%%%Creator: hat-test from Simon Tatham's "
               "Portable Puzzle Collection\n%%%%Pages: 1\n"
               "%%%%BoundingBox: %f %f %f %f\n"
               "%%%%EndComments\n%%%%Page: 1 1\n",
               ox + scale * ctx->bbox->bl.x,
               oy + scale * ctx->bbox->bl.y,
               ox + scale * ctx->bbox->tr.x,
               oy + scale * ctx->bbox->tr.y);

        if (ctx->clip) {
            printf("%f %f moveto %f %f lineto %f %f lineto %f %f lineto "
                   "closepath clip\n",
                   ox + scale * ctx->bbox->bl.x,
                   oy + scale * ctx->bbox->bl.y,
                   ox + scale * ctx->bbox->bl.x,
                   oy + scale * ctx->bbox->tr.y,
                   ox + scale * ctx->bbox->tr.x,
                   oy + scale * ctx->bbox->tr.y,
                   ox + scale * ctx->bbox->tr.x,
                   oy + scale * ctx->bbox->bl.y);
        }
        printf("%f %f translate %f dup scale\n", ox, oy, scale);
        printf("%f setlinewidth\n", 0.06);
        printf("0 setgray 1 setlinejoin 1 setlinecap\n");
        break;
      }
      case OF_SVG: {
        printf("<?xml version=\"1.0\" encoding=\"UTF-8\" "
                "standalone=\"no\"?>\n");
        printf("<svg xmlns=\"http://www.w3.org/2000/svg\" "
               "version=\"1.1\" width=\"%f\" height=\"%f\">\n",
               scale * (ctx->bbox->tr.x - ctx->bbox->bl.x),
               scale * (ctx->bbox->tr.y - ctx->bbox->bl.y));
        printf("<style type=\"text/css\">\n");
        printf("path { fill: none; stroke: black; stroke-width: %f; "
               "stroke-linejoin: round; stroke-linecap: round; }\n",
               0.06 * scale);
        switch (ctx->colourmode) {
          case CM_SEMANTIC:
            printf(".H     { fill: rgb(153, 204, 255); }\n");
            printf(".H3    { fill: rgb(  0, 128, 204); }\n");
            printf(".T, .P { fill: rgb(255, 255, 255); }\n");
            printf(".F     { fill: rgb(178, 178, 178); }\n");
            break;

          default /* case CM_FOURCOLOUR */:
            printf(".c0 { fill: rgb(255, 178, 178); }\n");
            printf(".c1 { fill: rgb(255, 255, 178); }\n");
            printf(".c2 { fill: rgb(178, 255, 178); }\n");
            printf(".c3 { fill: rgb(153, 153, 255); }\n");
            break;
        }
        printf("</style>\n");

        ctx->xoff = -ctx->bbox->bl.x * scale;
        ctx->xscale = scale;
        ctx->yoff = ctx->bbox->tr.y * scale;
        ctx->yscale = -scale;
        break;
      }
      default:
        break;
    }
}

static void draw_hat(void *vctx, Kite kite0, HatCoords *hc, int *coords)
{
    drawctx *ctx = (drawctx *)vctx;
    pspoint p;
    size_t i;
    int orientation;

    /*
     * Determine an index for the hat's orientation, based on the axis
     * of symmetry of its kite #0.
     */
    {
        int dx = kite0.outer.x - kite0.centre.x;
        int dy = kite0.outer.y - kite0.centre.y;
        orientation = 0;
        while (dx < 0 || dy < 0) {
            int newdx = dx + dy;
            int newdy = -dx;
            dx = newdx;
            dy = newdy;
            orientation++;
            assert(orientation < 6);
        }
    }

    switch (ctx->outfmt) {
      case OF_POSTSCRIPT: {
        const char *colour;

        printf("newpath");
        for (i = 0; i < 14; i++) {
            p.x = coords[2*i] * 1.5;
            p.y = coords[2*i+1] * sqrt(0.75);
            printf(" %f %f %s", p.x, p.y, i ? "lineto" : "moveto");
        }
        printf(" closepath gsave");

        switch (ctx->colourmode) {
          case CM_SEMANTIC:
            if (hc->c[2].type == TT_H) {
                colour = (hc->c[1].index == 3 ? "0 0.5 0.8 setrgbcolor" :
                          "0.6 0.8 1 setrgbcolor");
            } else if (hc->c[2].type == TT_F) {
                colour = "0.7 setgray";
            } else {
                colour = "1 setgray";
            }
            break;

          default /* case CM_FOURCOLOUR */: {
            /*
             * Determine the colour of this tile by translating the
             * fixed colour from fourcolours[] through our current
             * FourColourMap.
             */
            FourColourMap f = ctx->fourcolourmap[ctx->kiteenum->curr_index];
            const int *m = fourcolours[hc->c[3].type];
            static const char *const colours[] = {
                "1 0.7 0.7 setrgbcolor",
                "1 1 0.7 setrgbcolor",
                "0.7 1 0.7 setrgbcolor",
                "0.6 0.6 1 setrgbcolor",
            };
            colour = colours[f.map[m[hc->c[2].index * 4 + hc->c[1].index]]];
            break;
          }
        }
        printf(" %s fill grestore", colour);
        printf(" stroke\n");
        break;
      }
      case OF_SVG: {
        const char *class;

        switch (ctx->colourmode) {
          case CM_SEMANTIC: {
            static const char *const classes[] = {"H", "T", "P", "F"};

            if (hc->c[2].type == TT_H && hc->c[1].index == 3)
                class = "H3";
            else
                class = classes[hc->c[2].type];
            break;
          }

          default /* case CM_FOURCOLOUR */: {
            static const char *const classes[] = {"c0", "c1", "c2", "c3"};
            FourColourMap f = ctx->fourcolourmap[ctx->kiteenum->curr_index];
            const int *m = fourcolours[hc->c[3].type];
            class = classes[f.map[m[hc->c[2].index * 4 + hc->c[1].index]]];
            break;
          }
        }

        printf("<path class=\"%s\" d=\"", class);

        for (i = 0; i < 14; i++) {
            p.x = coords[2*i] * 1.5;
            p.y = coords[2*i+1] * sqrt(0.75);
            printf("%s %f %f", i == 0 ? "M" : " L",
                   ctx->xoff + ctx->xscale * p.x,
                   ctx->yoff + ctx->yscale * p.y);
        }
        printf(" z\"/>\n");
        break;
      }
      case OF_PYTHON: {
        printf("hat('%c', %d, %d, [", "HTPF"[hc->c[2].type], hc->c[1].index,
               orientation);
        for (i = 0; i < 14; i++)
            printf("%s(%d,%d)", i ? ", " : "", coords[2*i], coords[2*i+1]);
        printf("])\n");
        break;
      }
    }
}

static void trailer(drawctx *dctx)
{
    switch (dctx->outfmt) {
      case OF_POSTSCRIPT: {
        printf("showpage\n");
        printf("%%%%Trailer\n");
        printf("%%%%EOF\n");
        break;
      }
      case OF_SVG: {
        printf("</svg>\n");
        break;
      }
      default:
        break;
    }
}

int main(int argc, char **argv)
{
    psbbox bbox[1];
    KiteEnum s[1];
    HatContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    random_state *rs;
    const char *random_seed = "12345";
    int w = 10, h = 10;
    int argpos = 0;
    size_t i;
    drawctx dctx[1];

    dctx->outfmt = OF_POSTSCRIPT;
    dctx->colourmode = CM_SEMANTIC;
    dctx->natural_scale = false;
    dctx->clip = false;
    dctx->kiteenum = s;

    while (--argc > 0) {
        const char *arg = *++argv;
        if (!strcmp(arg, "--help")) {
            printf("  usage: hat-test [options] [<width>] [<height>]\n"
                   "options: --python   write a Python function call per hat\n"
                   "         --seed=STR vary the starting random seed\n"
                   "   also: hat-test --test\n");
            return 0;
        } else if (!strcmp(arg, "--test")) {
            return unit_tests() ? 0 : 1;
        } else if (!strcmp(arg, "--svg")) {
            dctx->outfmt = OF_SVG;
        } else if (!strcmp(arg, "--python")) {
            dctx->outfmt = OF_PYTHON;
        } else if (!strcmp(arg, "--fourcolour")) {
            dctx->colourmode = CM_FOURCOLOUR;
        } else if (!strcmp(arg, "--unscaled")) {
            dctx->natural_scale = true;
        } else if (!strcmp(arg, "--clip")) {
            dctx->clip = true;
        } else if (!strncmp(arg, "--seed=", 7)) {
            random_seed = arg+7;
        } else if (arg[0] == '-') {
            fprintf(stderr, "unrecognised option '%s'\n", arg);
            return 1;
        } else {
            switch (argpos++) {
              case 0:
                w = atoi(arg);
                break;
              case 1:
                h = atoi(arg);
                break;
              default:
                fprintf(stderr, "unexpected extra argument '%s'\n", arg);
                return 1;
            }
        }
    }

    for (i = 0; i < lenof(coords); i++)
        coords[i] = NULL;

    rs = random_new(random_seed, strlen(random_seed));
    hatctx_init_random(ctx, rs);

    bbox->started = false;
    dctx->bbox = bbox;

    hat_kiteenum_first(s, w, h);
    coords[s->curr_index] = hatctx_initial_coords(ctx);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     bbox_add_hat, dctx);
    while (hat_kiteenum_next(s)) {
        hat_coords_free(coords[s->curr_index]);
        coords[s->curr_index] = hatctx_step(
            ctx, coords[s->last_index], s->last_step);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         bbox_add_hat, dctx);
    }
    for (i = 0; i < lenof(coords); i++) {
        hat_coords_free(coords[i]);
        coords[i] = NULL;
    }

    header(dctx);

    hat_kiteenum_first(s, w, h);
    coords[s->curr_index] = hatctx_initial_coords(ctx);
    dctx->fourcolourmap[s->curr_index] = fourcolourmap_initial(rs);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     draw_hat, dctx);
    while (hat_kiteenum_next(s)) {
        hat_coords_free(coords[s->curr_index]);
        coords[s->curr_index] = hatctx_step(
            ctx, coords[s->last_index], s->last_step);
        dctx->fourcolourmap[s->curr_index] = fourcolourmap_update(
            dctx->fourcolourmap[s->last_index], coords[s->last_index],
            coords[s->curr_index], s->last_step, ctx);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         draw_hat, dctx);
    }
    for (i = 0; i < lenof(coords); i++) {
        hat_coords_free(coords[i]);
        coords[i] = NULL;
    }

    trailer(dctx);

    hatctx_cleanup(ctx);
    random_free(rs);

    return 0;
}
