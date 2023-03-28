/*
 * Code to generate patches of the aperiodic 'hat' tiling discovered
 * in 2023.
 *
 * auxiliary/doc/hats.html contains an explanation of the basic ideas
 * of this algorithm, which can't really be put in a source file
 * because it just has too many complicated diagrams. So read that
 * first, because the comments in here will refer to it.
 *
 * Discoverers' website: https://cs.uwaterloo.ca/~csk/hat/
 * Preprint of paper:    https://arxiv.org/abs/2303.10798
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"
#include "hat.h"

/*
 * Coordinate system:
 *
 * The output of this code lives on the tiling known to grid.c as
 * 'Kites', which can be viewed as a tiling of hexagons each of which
 * is subdivided into six kites sharing their pointy vertex, or
 * (equivalently) a tiling of equilateral triangles each subdivided
 * into three kits sharing their blunt vertex.
 *
 * We express coordinates in this system relative to the basis (1, r)
 * where r = (1 + sqrt(3)i) / 2 is a primitive 6th root of unity. This
 * gives us a system in which two integer coordinates can address any
 * grid point, provided we scale up so that the side length of the
 * equilateral triangles in the tiling is 6.
 */

typedef struct Point {
    int x, y;                          /* represents x + yr */
} Point;

static inline Point pointscale(int scale, Point a)
{
    Point r = { scale * a.x, scale * a.y };
    return r;
}

static inline Point pointadd(Point a, Point b)
{
    Point r = { a.x + b.x, a.y + b.y };
    return r;
}

/*
 * We identify a single kite by the coordinates of its four vertices.
 * This allows us to construct the coordinates of an adjacent kite by
 * taking affine transformations of the original kite's vertices.
 *
 * This is a useful way to do it because it means that if you reflect
 * the kite (by swapping its left and right vertices) then these
 * transformations also perform in a reflected way. This will be
 * useful in the code below that outputs the coordinates of each hat,
 * because this way it can work by walking around its 8 kites using a
 * fixed set of steps, and if the hat is reflected, then we just
 * reflect the starting kite before doing that, and everything still
 * works.
 */

typedef struct Kite {
    Point centre, left, right, outer;
} Kite;

static inline Kite kite_left(Kite k)
{
    Kite r;
    r.centre = k.centre;
    r.right = k.left;
    r.outer = pointadd(pointscale(2, k.left), pointscale(-1, k.outer));
    r.left = pointadd(pointadd(k.centre, k.left), pointscale(-1, k.right));
    return r;
}

static inline Kite kite_right(Kite k)
{
    Kite r;
    r.centre = k.centre;
    r.left = k.right;
    r.outer = pointadd(pointscale(2, k.right), pointscale(-1, k.outer));
    r.right = pointadd(pointadd(k.centre, k.right), pointscale(-1, k.left));
    return r;
}

static inline Kite kite_forward_left(Kite k)
{
    Kite r;
    r.outer = k.outer;
    r.right = k.left;
    r.centre = pointadd(pointscale(2, k.left), pointscale(-1, k.centre));
    r.left = pointadd(pointadd(k.right, k.left), pointscale(-1, k.centre));
    return r;
}

static inline Kite kite_forward_right(Kite k)
{
    Kite r;
    r.outer = k.outer;
    r.left = k.right;
    r.centre = pointadd(pointscale(2, k.right), pointscale(-1, k.centre));
    r.right = pointadd(pointadd(k.left, k.right), pointscale(-1, k.centre));
    return r;
}

typedef enum KiteStep { KS_LEFT, KS_RIGHT, KS_F_LEFT, KS_F_RIGHT } KiteStep;

static inline Kite kite_step(Kite k, KiteStep step)
{
    switch (step) {
      case KS_LEFT: return kite_left(k);
      case KS_RIGHT: return kite_right(k);
      case KS_F_LEFT: return kite_forward_left(k);
      default /* case KS_F_RIGHT */: return kite_forward_right(k);
    }
}

/*
 * Function to enumerate the kites in a rectangular region, in a
 * serpentine-raster fashion so that every kite delivered shares an
 * edge with a recent previous one.
 */
#define KE_NKEEP 3
typedef struct KiteEnum {
    /* Fields private to the enumerator */
    int state;
    int x, y, w, h;
    unsigned curr_index;

    /* Fields the client can legitimately read out */
    Kite *curr;
    Kite recent[KE_NKEEP];
    unsigned last_index;
    KiteStep last_step; /* step that got curr from recent[last_index] */
} KiteEnum;

static void first_kite(KiteEnum *s, int w, int h)
{
    Kite start = { {0,0}, {0, 3}, {3, 0}, {2, 2} };
    size_t i;

    for (i = 0; i < KE_NKEEP; i++)
        s->recent[i] = start;          /* initialise to *something* */
    s->curr_index = 0;
    s->curr = &s->recent[s->curr_index];
    s->state = 1;
    s->w = w;
    s->h = h;
    s->x = 0;
    s->y = 0;
}
static bool next_kite(KiteEnum *s)
{
    unsigned lastbut1 = s->last_index;
    s->last_index = s->curr_index;
    s->curr_index = (s->curr_index + 1) % KE_NKEEP;
    s->curr = &s->recent[s->curr_index];

    switch (s->state) {
        /* States 1,2,3 walk rightwards along the upper side of a
         * horizontal grid line with a pointy kite end at the start
         * point */
      case 1:
        s->last_step = KS_F_RIGHT;
        s->state = 2;
        break;

      case 2:
        if (s->x+1 >= s->w) {
            s->last_step = KS_F_RIGHT;
            s->state = 4;
            break;
        }
        s->last_step = KS_RIGHT;
        s->state = 3;
        s->x++;
        break;

      case 3:
        s->last_step = KS_RIGHT;
        s->state = 1;
        break;

        /* State 4 is special: we've just moved up into a row below a
         * grid line, but we can't produce the rightmost tile of that
         * row because it's not adjacent any tile so far emitted. So
         * instead, emit the second-rightmost tile, and next time,
         * we'll emit the rightmost. */
      case 4:
        s->last_step = KS_LEFT;
        s->state = 5;
        break;

        /* And now we have to emit the third-rightmost tile relative
         * to the last but one tile we emitted (the one from state 2,
         * not state 4). */
      case 5:
        s->last_step = KS_RIGHT;
        s->last_index = lastbut1;
        s->state = 6;
        break;

        /* Now states 6-8 handle the general case of walking leftwards
         * along the lower side of a line, starting from a
         * right-angled kite end. */
      case 6:
        if (s->x <= 0) {
            if (s->y+1 >= s->h) {
                s->state = 0;
                return false;
            }
            s->last_step = KS_RIGHT;
            s->state = 9;
            s->y++;
            break;
        }
        s->last_step = KS_F_RIGHT;
        s->state = 7;
        s->x--;
        break;

      case 7:
        s->last_step = KS_RIGHT;
        s->state = 8;
        break;

      case 8:
        s->last_step = KS_RIGHT;
        s->state = 6;
        break;

        /* States 9,10,11 walk rightwards along the upper side of a
         * horizontal grid line with a right-angled kite end at the
         * start point. This time there's no awkward transition from
         * the previous row. */
      case 9:
        s->last_step = KS_RIGHT;
        s->state = 10;
        break;

      case 10:
        s->last_step = KS_RIGHT;
        s->state = 11;
        break;

      case 11:
        if (s->x+1 >= s->w) {
            /* Another awkward transition to the next row, where we
             * have to generate it based on the previous state-9 tile.
             * But this time at least we generate the rightmost tile
             * of the new row, so the next states will be simple. */
            s->last_step = KS_F_RIGHT;
            s->last_index = lastbut1;
            s->state = 12;
            break;
        }
        s->last_step = KS_F_RIGHT;
        s->state = 9;
        s->x++;
        break;

        /* States 12,13,14 walk leftwards along the upper edge of a
         * horizontal grid line with a pointy kite end at the start
         * point */
      case 12:
        s->last_step = KS_F_RIGHT;
        s->state = 13;
        break;

      case 13:
        if (s->x <= 0) {
            if (s->y+1 >= s->h) {
                s->state = 0;
                return false;
            }
            s->last_step = KS_LEFT;
            s->state = 1;
            s->y++;
            break;
        }
        s->last_step = KS_RIGHT;
        s->state = 14;
        s->x--;
        break;

      case 14:
        s->last_step = KS_RIGHT;
        s->state = 12;
        break;

      default:
        return false;
    }

    *s->curr = kite_step(s->recent[s->last_index], s->last_step);
    return true;
}

/*
 * Assorted useful definitions.
 */
typedef enum TileType { TT_H, TT_T, TT_P, TT_F, TT_KITE, TT_HAT } TileType;
static const char tilechars[] = "HTPF";

#define HAT_KITES 8     /* number of kites in a hat */
#define MT_MAXEXPAND 13 /* largest number of metatiles in any expansion */

/*
 * Definitions for the autogenerated hat-tables.h header file that
 * defines all the lookup tables.
 */
typedef struct KitemapEntry {
    int kite, hat, meta;               /* all -1 if impossible */
} KitemapEntry;

typedef struct MetamapEntry {
    int meta, meta2;
} MetamapEntry;

static inline size_t kitemap_index(KiteStep step, unsigned kite,
                                   unsigned hat, unsigned meta)
{
    return step + 4 * (kite + 8 * (hat + 4 * meta));
}

static inline size_t metamap_index(unsigned meta, unsigned meta2)
{
    return meta2 * MT_MAXEXPAND + meta;
}

/*
 * The actual tables.
 */
#include "hat-tables.h"

/*
 * Coordinate system for tracking kites within a randomly selected
 * part of the recursively expanded hat tiling.
 *
 * HatCoords will store an array of HatCoord, in little-endian
 * arrangement. So hc->c[0] will always have type TT_KITE and index a
 * single kite within a hat; hc->c[1] will have type TT_HAT and index
 * a hat within a first-order metatile; hc->c[2] will be the smallest
 * metatile containing this hat, and hc->c[3, 4, 5, ...] will be
 * higher-order metatiles as needed.
 *
 * The last coordinate stored, hc->c[hc->nc-1], will have a tile type
 * but no index (represented by index==-1). This means "we haven't
 * decided yet what this level of metatile needs to be". If we need to
 * refer to this level during the step_coords algorithm, we make it up
 * at random, based on a table of what metatiles each type can
 * possibly be part of, at what index.
 */
typedef struct HatCoord {
    int index; /* index within that tile, or -1 if not yet known */
    TileType type;  /* type of this tile */
} HatCoord;

typedef struct HatCoords {
    HatCoord *c;
    size_t nc, csize;
} HatCoords;

static HatCoords *hc_new(void)
{
    HatCoords *hc = snew(HatCoords);
    hc->nc = hc->csize = 0;
    hc->c = NULL;
    return hc;
}

static void hc_free(HatCoords *hc)
{
    if (hc) {
        sfree(hc->c);
        sfree(hc);
    }
}

static void hc_make_space(HatCoords *hc, size_t size)
{
    if (hc->csize < size) {
        hc->csize = hc->csize * 5 / 4 + 16;
        if (hc->csize < size)
            hc->csize = size;
        hc->c = sresize(hc->c, hc->csize, HatCoord);
    }
}

static HatCoords *hc_copy(HatCoords *hc_in)
{
    HatCoords *hc_out = hc_new();
    hc_make_space(hc_out, hc_in->nc);
    memcpy(hc_out->c, hc_in->c, hc_in->nc * sizeof(*hc_out->c));
    hc_out->nc = hc_in->nc;
    return hc_out;
}

/*
 * HatCoordContext is the shared context of a whole run of the
 * algorithm. Its 'prototype' HatCoords object represents the
 * coordinates of the starting kite, and is extended as necessary; any
 * other HatCoord that needs extending will copy the higher-order
 * values from ctx->prototype as needed, so that once each choice has
 * been made, it remains consistent.
 *
 * When we're inventing a random piece of tiling in the first place,
 * we append to ctx->prototype by choosing a random (but legal)
 * higher-level metatile for the current topmost one to turn out to be
 * part of. When we're replaying a generation whose parameters are
 * already stored, we don't have a random_state, and we make fixed
 * decisions if not enough coordinates were provided.
 *
 * (Of course another approach would be to reject grid descriptions
 * that didn't define enough coordinates! But that would involve a
 * whole extra iteration over the whole grid region just for
 * validation, and that seems like more timewasting than really
 * needed. So we tolerate short descriptions, and do something
 * deterministic with them.)
 */

typedef struct HatCoordContext {
    random_state *rs;
    HatCoords *prototype;
} HatCoordContext;

static void init_coords_random(HatCoordContext *ctx, random_state *rs)
{
    ctx->rs = rs;
    ctx->prototype = hc_new();
    hc_make_space(ctx->prototype, 3);
    ctx->prototype->c[0].type = TT_KITE;
    ctx->prototype->c[1].type = TT_HAT;
    ctx->prototype->c[2].type = random_upto(rs, 4);
    ctx->prototype->c[2].index = -1;
    ctx->prototype->c[1].index = random_upto(
        rs, hats_in_metatile[ctx->prototype->c[2].type]);
    ctx->prototype->c[0].index = random_upto(rs, HAT_KITES);
    ctx->prototype->nc = 3;
}

static inline int metatile_char_to_enum(char metatile)
{
    return (metatile == 'H' ? TT_H :
            metatile == 'T' ? TT_T :
            metatile == 'P' ? TT_P :
            metatile == 'F' ? TT_F : -1);
}

static void init_coords_params(HatCoordContext *ctx,
                               const struct HatPatchParams *hp)
{
    size_t i;

    ctx->rs = NULL;
    ctx->prototype = hc_new();

    assert(hp->ncoords >= 3);

    hc_make_space(ctx->prototype, hp->ncoords + 1);
    ctx->prototype->nc = hp->ncoords + 1;

    for (i = 0; i < hp->ncoords; i++)
        ctx->prototype->c[i].index = hp->coords[i];

    ctx->prototype->c[hp->ncoords].type =
        metatile_char_to_enum(hp->final_metatile);
    ctx->prototype->c[hp->ncoords].index = -1;

    ctx->prototype->c[0].type = TT_KITE;
    ctx->prototype->c[1].type = TT_HAT;

    for (i = hp->ncoords - 1; i > 1; i--) {
        TileType metatile = ctx->prototype->c[i+1].type;
        assert(hp->coords[i] < nchildren[metatile]);
        ctx->prototype->c[i].type = children[metatile][hp->coords[i]];
    }

    assert(hp->coords[0] < 8);
}

static HatCoords *initial_coords(HatCoordContext *ctx)
{
    return hc_copy(ctx->prototype);
}

/*
 * Extend hc until it has at least n coordinates in, by copying from
 * ctx->prototype if needed, and extending ctx->prototype if needed in
 * order to do that.
 */
static void ensure_coords(HatCoordContext *ctx, HatCoords *hc, size_t n)
{
    /*
     * One table that we write by hand: the permitted ways to extend
     * the coordinate system outwards from a given metatile.
     *
     * One obvious approach would be to make a table of all the places
     * each metatile can appear in the expansion of another (e.g. H
     * can be subtile 0, 1 or 2 of another H, subtile 0 of a T, or 0
     * or 1 of a P or an F), and when we need to decide what our
     * current topmost tile turns out to be a subtile of, choose
     * equiprobably at random from those options.
     *
     * That's what I did originally, but a better approach is to skew
     * the probabilities. We'd like to generate our patch of actual
     * tiling uniformly at random, in the sense that if you selected
     * uniformly from a very large region of the plane, the
     * distribution of possible finite patches of tiling would
     * converge to some limit as that region tended to infinity, and
     * we'd be picking from that limiting distribution on finite
     * patches.
     *
     * For this we have to refer back to the original paper, which
     * indicates the subset of each metatile's expansion that can be
     * considered to 'belong' to that metatile, such that every
     * subtile belongs to exactly one parent metatile, and the
     * overlaps are eliminated. Reading out the diagrams from their
     * Figure 2.8:
     *
     * - H: we discard three of the outer F subtiles, in the symmetric
     *    positions index by our coordinates as 7, 10, 11. So we keep
     *    the remaining subtiles {0,1,2,3,4,5,6,8,9,12}, which consist
     *    of three H, one T, three P and three F.
     *
     * - T: only the central H expanded from a T is considered to
     *   belong to it, so we just keep {0}, a single H.
     *
     * - P: we discard everything intersected by a long edge of the
     *   parallelogram, leaving the central three tiles and the
     *   endmost pair of F. That is, we keep {0,1,4,5,10}, consisting
     *   of two H, one P and two F.
     *
     * - F: looks like P at one end, and we retain the corresponding
     *   set of tiles there, but at the other end we keep the two F on
     *   either side of the endmost one. So we keep {0,1,3,6,8,10},
     *   consisting of two H, one P and _three_ F.
     *
     * Adding up the tile numbers gives us this matrix system:
     *
     * (H_1)   (3 1 2 2)(H_0)
     * (T_1) = (1 0 0 0)(T_0)
     * (P_1)   (3 0 1 1)(P_0)
     * (F_1)   (3 0 2 3)(F_0)
     *
     * which says that if you have a patch of metatiling consisting of
     * H_0 H tiles, T_0 T tiles etc, then this matrix shows the number
     * H_1 of smaller H tiles, etc, expanded from it.
     *
     * If you expand _many_ times, that's equivalent to raising the
     * matrix to a power:
     *
     *                  n
     * (H_n)   (3 1 2 2) (H_0)
     * (T_n) = (1 0 0 0) (T_0)
     * (P_n)   (3 0 1 1) (P_0)
     * (F_n)   (3 0 2 3) (F_0)
     *
     * The limiting distribution of metatiles is obtained by looking
     * at the four-way ratio between H_n, T_n, P_n and F_n as n tends
     * to infinity. To calculate this, we find the eigenvalues and
     * eigenvectors of the matrix, and extract the eigenvector
     * corresponding to the eigenvalue of largest magnitude. (Things
     * get more complicated in cases where that's not unique, but
     * here, it is.)
     *
     * That eigenvector is
     *
     *   [          1          ]      [ 1                      ]
     *   [ (7 - 3 sqrt(5)) / 2 ]  ~=  [ 0.14589803375031545538 ]
     *   [    3 sqrt(5) - 6    ]      [ 0.70820393249936908922 ]
     *   [ (9 - 3 sqrt(5)) / 2 ]      [ 1.14589803375031545538 ]
     *
     * So those are the limiting relative proportions of metatiles.
     *
     * So if we have a particular metatile, how likely is it for its
     * parent to be one of those? We have to adjust by the number of
     * metatiles of each type that each tile has as its children. For
     * example, the P and F tiles have one P child each, but the H has
     * three P children. So if we have a P, the proportion of H in its
     * potential ancestry is three times what's shown here. (And T
     * can't occur at all as a parent.)
     *
     * In other words, we should choose _each coordinate_ with
     * probability corresponding to one of those numbers (scaled down
     * so they all sum to 1). Continuing to use P as an example, it
     * will be:
     *
     *  - child 4 of H with relative probability 1
     *  - child 5 of H with relative probability 1
     *  - child 6 of H with relative probability 1
     *  - child 4 of P with relative probability 0.70820393249936908922
     *  - child 3 of F with relative probability 1.14589803375031545538
     *
     * and then we obtain the true probabilities by scaling those
     * values down so that they sum to 1.
     *
     * The tables below give a reasonable approximation in 32-bit
     * integers to these proportions.
     */

    typedef struct MetatilePossibleParent {
        TileType type;
        unsigned index;
        unsigned long probability;
    } MetatilePossibleParent;

    /* The above probabilities scaled up by 10000000 */
    #define PROB_H 10000000
    #define PROB_T  1458980
    #define PROB_P  7082039
    #define PROB_F 11458980

    static const MetatilePossibleParent parents_H[] = {
        { TT_H,  0, PROB_H },
        { TT_H,  1, PROB_H },
        { TT_H,  2, PROB_H },
        { TT_T,  0, PROB_T },
        { TT_P,  0, PROB_P },
        { TT_P,  1, PROB_P },
        { TT_F,  0, PROB_F },
        { TT_F,  1, PROB_F },
    };
    static const MetatilePossibleParent parents_T[] = {
        { TT_H,  3, PROB_H },
    };
    static const MetatilePossibleParent parents_P[] = {
        { TT_H,  4, PROB_H },
        { TT_H,  5, PROB_H },
        { TT_H,  6, PROB_H },
        { TT_P,  4, PROB_P },
        { TT_F,  3, PROB_F },
    };
    static const MetatilePossibleParent parents_F[] = {
        { TT_H,  8, PROB_H },
        { TT_H,  9, PROB_H },
        { TT_H, 12, PROB_H },
        { TT_P,  5, PROB_P },
        { TT_P, 10, PROB_P },
        { TT_F,  6, PROB_F },
        { TT_F,  8, PROB_F },
        { TT_F, 10, PROB_F },
    };

    #undef PROB_H
    #undef PROB_T
    #undef PROB_P
    #undef PROB_F

    static const MetatilePossibleParent *const possible_parents[] = {
        parents_H, parents_T, parents_P, parents_F,
    };
    static const size_t n_possible_parents[] = {
        lenof(parents_H), lenof(parents_T), lenof(parents_P), lenof(parents_F),
    };

    if (ctx->prototype->nc < n) {
        hc_make_space(ctx->prototype, n);
        while (ctx->prototype->nc < n) {
            TileType type = ctx->prototype->c[ctx->prototype->nc - 1].type;
            assert(ctx->prototype->c[ctx->prototype->nc - 1].index == -1);
            const MetatilePossibleParent *parents = possible_parents[type];
            size_t parent_index;
            if (ctx->rs) {
                unsigned long limit = 0, value;
                size_t nparents = n_possible_parents[type], i;
                for (i = 0; i < nparents; i++)
                    limit += parents[i].probability;
                value = random_upto(ctx->rs, limit);
                for (i = 0; i < nparents; i++) {
                    if (value < parents[i].probability)
                        break;
                    value -= parents[i].probability;
                }
                assert(i < nparents);
                parent_index = i;
            } else {
                parent_index = 0;
            }
            ctx->prototype->c[ctx->prototype->nc - 1].index =
                parents[parent_index].index;
            ctx->prototype->c[ctx->prototype->nc].index = -1;
            ctx->prototype->c[ctx->prototype->nc].type =
                parents[parent_index].type;
            ctx->prototype->nc++;
        }
    }

    hc_make_space(hc, n);
    while (hc->nc < n) {
        assert(hc->c[hc->nc - 1].index == -1);
        assert(hc->c[hc->nc - 1].type == ctx->prototype->c[hc->nc - 1].type);
        hc->c[hc->nc - 1].index = ctx->prototype->c[hc->nc - 1].index;
        hc->c[hc->nc].index = -1;
        hc->c[hc->nc].type = ctx->prototype->c[hc->nc].type;
        hc->nc++;
    }
}

static void cleanup_coords(HatCoordContext *ctx)
{
    hc_free(ctx->prototype);
}

#ifdef DEBUG_COORDS
static inline void debug_coords(const char *prefix, HatCoords *hc,
                                const char *suffix)
{
    const char *sep = "";
    static const char *const types[] = {"H","T","P","F","kite","hat"};

    fputs(prefix, stderr);
    for (size_t i = 0; i < hc->nc; i++) {
        fprintf(stderr, "%s %s ", sep, types[hc->c[i].type]);
        sep = " .";
        if (hc->c[i].index == -1)
            fputs("?", stderr);
        else
            fprintf(stderr, "%d", hc->c[i].index);
    }
    fputs(suffix, stderr);
}
#else
#define debug_coords(p,c,s) ((void)0)
#endif

/*
 * The actual system for finding the coordinates of an adjacent kite.
 */

/*
 * Kitemap step: ensure we have enough coordinates to know two levels
 * of meta-tiling, and use the kite map for the outer layer to move
 * around the individual kites. If this fails, return NULL.
 */
static HatCoords *try_step_coords_kitemap(
    HatCoordContext *ctx, HatCoords *hc_in, KiteStep step)
{
    ensure_coords(ctx, hc_in, 4);
    debug_coords("  try kitemap  ", hc_in, "\n");
    unsigned kite = hc_in->c[0].index;
    unsigned hat = hc_in->c[1].index;
    unsigned meta = hc_in->c[2].index;
    TileType meta2type = hc_in->c[3].type;
    const KitemapEntry *ke = &kitemap[meta2type][
        kitemap_index(step, kite, hat, meta)];
    if (ke->kite >= 0) {
        /*
         * Success! We've got coordinates for the next kite in this
         * direction.
         */
        HatCoords *hc_out = hc_copy(hc_in);

        hc_out->c[2].index = ke->meta;
        hc_out->c[2].type = children[meta2type][ke->meta];
        hc_out->c[1].index = ke->hat;
        hc_out->c[1].type = TT_HAT;
        hc_out->c[0].index = ke->kite;
        hc_out->c[0].type = TT_KITE;

        debug_coords("  success!     ", hc_out, "\n");
        return hc_out;
    }

    return NULL;
}

/*
 * Recursive metamap step. Try using the metamap to rewrite the
 * coordinates at hc->c[depth] and hc->c[depth+1] (using the metamap
 * for the tile type described in hc->c[depth+2]). If successful,
 * recurse back down to see if this led to a successful step via the
 * kitemap. If even that fails (so that we need to try a higher-order
 * metamap rewrite), return NULL.
 */
static HatCoords *try_step_coords_metamap(
    HatCoordContext *ctx, HatCoords *hc_in, KiteStep step, size_t depth)
{
    HatCoords *hc_tmp = NULL, *hc_out;

    ensure_coords(ctx, hc_in, depth+3);
#ifdef DEBUG_COORDS
    fprintf(stderr, "  try meta %-4d", (int)depth);
    debug_coords("", hc_in, "\n");
#endif
    unsigned meta_orig = hc_in->c[depth].index;
    unsigned meta2_orig = hc_in->c[depth+1].index;
    TileType meta3type = hc_in->c[depth+2].type;

    unsigned meta = meta_orig, meta2 = meta2_orig;

    while (true) {
        const MetamapEntry *me;
        HatCoords *hc_curr = hc_tmp ? hc_tmp : hc_in;

        if (depth > 2)
            hc_out = try_step_coords_metamap(ctx, hc_curr, step, depth - 1);
        else
            hc_out = try_step_coords_kitemap(ctx, hc_curr, step);
        if (hc_out) {
            hc_free(hc_tmp);
            return hc_out;
        }

        me = &metamap[meta3type][metamap_index(meta, meta2)];
        assert(me->meta != -1);
        if (me->meta == meta_orig && me->meta2 == meta2_orig) {
            hc_free(hc_tmp);
            return NULL;
        }

        meta = me->meta;
        meta2 = me->meta2;

        /*
         * We must do the rewrite in a copy of hc_in. It's not
         * _necessarily_ obvious that that's the case (any successful
         * rewrite leaves the coordinates still valid and still
         * referring to the same kite, right?). But the problem is
         * that we might do a rewrite at this level more than once,
         * and in between, a metamap rewrite at the next level down
         * might have modified _one_ of the two coordinates we're
         * messing about with. So it's easiest to let the recursion
         * just use a separate copy.
         */
        if (!hc_tmp)
            hc_tmp = hc_copy(hc_in);

        hc_tmp->c[depth+1].index = meta2;
        hc_tmp->c[depth+1].type = children[meta3type][meta2];
        hc_tmp->c[depth].index = meta;
        hc_tmp->c[depth].type = children[hc_tmp->c[depth+1].type][meta];

        debug_coords("  rewritten -> ", hc_tmp, "\n");
    }
}

/*
 * The top-level algorithm for finding the next tile.
 */
static HatCoords *step_coords(HatCoordContext *ctx, HatCoords *hc_in,
                              KiteStep step)
{
    HatCoords *hc_out;
    size_t depth;

#ifdef DEBUG_COORDS
    static const char *const directions[] = {
        " left\n", " right\n", " forward left\n", " forward right\n" };
    debug_coords("step start     ", hc_in, directions[step]);
#endif

    /*
     * First, just try a kitemap step immediately. If that succeeds,
     * we're done.
     */
    if ((hc_out = try_step_coords_kitemap(ctx, hc_in, step)) != NULL)
        return hc_out;

    /*
     * Otherwise, try metamap rewrites at successively higher layers
     * until one works. Each one will recurse back down to the
     * kitemap, as described above.
     */
    for (depth = 2;; depth++) {
        if ((hc_out = try_step_coords_metamap(
                 ctx, hc_in, step, depth)) != NULL)
            return hc_out;
    }
}

/*
 * Generate a random set of parameters for a tiling of a given size.
 * To do this, we iterate over the whole tiling via first_kite and
 * next_kite, and for each kite, calculate its coordinates. But then
 * we throw the coordinates away and don't do anything with them!
 *
 * But the side effect of _calculating_ all those coordinates is that
 * we found out how far ctx->prototype needed to be extended, and did
 * so, pulling random choices out of our random_state. So after this
 * iteration, ctx->prototype contains everything we need to replicate
 * the same piece of tiling next time.
 */
void hat_tiling_randomise(struct HatPatchParams *hp, int w, int h,
                          random_state *rs)
{
    HatCoordContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    KiteEnum s[1];
    size_t i;

    init_coords_random(ctx, rs);
    for (i = 0; i < lenof(coords); i++)
        coords[i] = NULL;

    first_kite(s, w, h);
    coords[s->curr_index] = initial_coords(ctx);

    while (next_kite(s)) {
        hc_free(coords[s->curr_index]);
        coords[s->curr_index] = step_coords(
            ctx, coords[s->last_index], s->last_step);
    }

    hp->ncoords = ctx->prototype->nc - 1;
    hp->coords = snewn(hp->ncoords, unsigned char);
    for (i = 0; i < hp->ncoords; i++)
        hp->coords[i] = ctx->prototype->c[i].index;
    hp->final_metatile = tilechars[ctx->prototype->c[hp->ncoords].type];

    cleanup_coords(ctx);
    for (i = 0; i < lenof(coords); i++)
        hc_free(coords[i]);
}

const char *hat_tiling_params_invalid(const struct HatPatchParams *hp)
{
    TileType metatile;
    size_t i;

    if (hp->ncoords < 3)
        return "Grid parameters require at least three coordinates";
    if (metatile_char_to_enum(hp->final_metatile) < 0)
        return "Grid parameters contain an invalid final metatile";
    if (hp->coords[0] >= 8)
        return "Grid parameters contain an invalid kite index";

    metatile = metatile_char_to_enum(hp->final_metatile);
    for (i = hp->ncoords - 1; i > 1; i--) {
        if (hp->coords[i] >= nchildren[metatile])
            return "Grid parameters contain an invalid metatile index";
        metatile = children[metatile][hp->coords[i]];
    }

    if (hp->coords[1] >= hats_in_metatile[metatile])
        return "Grid parameters contain an invalid hat index";

    return NULL;
}

/*
 * For each kite generated by hat_tiling_generate, potentially
 * generate an output hat and give it to our caller.
 *
 * We do this by starting from kite #0 of each hat, and tracing round
 * the boundary. If the whole boundary is within the caller's bounding
 * region, we return it; if it goes off the edge, we don't.
 *
 * (Of course, every hat we _do_ want to return will have all its
 * kites inside the rectangle, so its kite #0 will certainly be caught
 * by this iteration.)
 */

typedef void (*internal_hat_callback_fn)(void *ctx, Kite kite0, HatCoords *hc,
                                         int *coords);

static void maybe_report_hat(int w, int h, Kite kite, HatCoords *hc,
                             internal_hat_callback_fn cb, void *cbctx)
{
    Kite kite0;
    Point vertices[14];
    size_t i, j;
    bool reversed = false;
    int coords[28];

    /* Only iterate from kite #0 of a hat */
    if (hc->c[0].index != 0)
        return;
    kite0 = kite;

    /*
     * Identify reflected hats: they are always hat #3 of an H
     * metatile. If we find one, reflect the starting kite so that the
     * kite_step operations below will go in the other direction.
     */
    if (hc->c[2].type == TT_H && hc->c[1].index == 3) {
        reversed = true;
        Point tmp = kite.left;
        kite.left = kite.right;
        kite.right = tmp;
    }

    vertices[0] = kite.centre;
    vertices[1] = kite.right;
    vertices[2] = kite.outer;
    vertices[3] = kite.left;
    kite = kite_left(kite);            /* now on kite #1 */
    kite = kite_forward_right(kite);   /* now on kite #2 */
    vertices[4] = kite.centre;
    kite = kite_right(kite);           /* now on kite #3 */
    vertices[5] = kite.right;
    vertices[6] = kite.outer;
    kite = kite_forward_left(kite);    /* now on kite #4 */
    vertices[7] = kite.left;
    vertices[8] = kite.centre;
    kite = kite_right(kite);           /* now on kite #5 */
    kite = kite_right(kite);           /* now on kite #6 */
    kite = kite_right(kite);           /* now on kite #7 */
    vertices[9] = kite.right;
    vertices[10] = kite.outer;
    vertices[11] = kite.left;
    kite = kite_left(kite);            /* now on kite #6 again */
    vertices[12] = kite.outer;
    vertices[13] = kite.left;

    if (reversed) {
        /* For a reversed kite, also reverse the vertex order, so that
         * we report every polygon in a consistent orientation */
        for (i = 0, j = 13; i < j; i++, j--) {
            Point tmp = vertices[i];
            vertices[i] = vertices[j];
            vertices[j] = tmp;
        }
    }

    /*
     * Convert from our internal coordinate system into the orthogonal
     * one used in this module's external API. In the same loop, we
     * might as well do the bounds check.
     */
    for (i = 0; i < 14; i++) {
        Point v = vertices[i];
        int x = (v.x * 2 + v.y) / 3, y = v.y;

        if (x < 0 || x > 4*w || y < 0 || y > 6*h)
            return;       /* a vertex of this kite is out of bounds */

        coords[2*i] = x;
        coords[2*i+1] = y;
    }

    cb(cbctx, kite0, hc, coords);
}

struct internal_ctx {
    hat_tile_callback_fn external_cb;
    void *external_cbctx;
};
static void report_hat(void *vctx, Kite kite0, HatCoords *hc, int *coords)
{
    struct internal_ctx *ctx = (struct internal_ctx *)vctx;
    ctx->external_cb(ctx->external_cbctx, 14, coords);
}

/*
 * Generate a hat tiling from a previously generated set of parameters.
 */
void hat_tiling_generate(const struct HatPatchParams *hp, int w, int h,
                         hat_tile_callback_fn cb, void *cbctx)
{
    HatCoordContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    KiteEnum s[1];
    size_t i;
    struct internal_ctx report_hat_ctx[1];

    report_hat_ctx->external_cb = cb;
    report_hat_ctx->external_cbctx = cbctx;

    init_coords_params(ctx, hp);
    for (i = 0; i < lenof(coords); i++)
        coords[i] = NULL;

    first_kite(s, w, h);
    coords[s->curr_index] = initial_coords(ctx);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     report_hat, report_hat_ctx);

    while (next_kite(s)) {
        hc_free(coords[s->curr_index]);
        coords[s->curr_index] = step_coords(
            ctx, coords[s->last_index], s->last_step);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         report_hat, report_hat_ctx);
    }

    cleanup_coords(ctx);
    for (i = 0; i < lenof(coords); i++)
        hc_free(coords[i]);
}

#ifdef TEST_HAT

#include <stdarg.h>

static HatCoords *hc_construct_v(TileType type, va_list ap)
{
    HatCoords *hc = hc_new();
    while (true) {
        int index = va_arg(ap, int);

        hc_make_space(hc, hc->nc + 1);
        hc->c[hc->nc].type = type;
        hc->c[hc->nc].index = index;
        hc->nc++;

        if (index < 0)
            return hc;

        type = va_arg(ap, TileType);
    }
}

static HatCoords *hc_construct(TileType type, ...)
{
    HatCoords *hc;
    va_list ap;

    va_start(ap, type);
    hc = hc_construct_v(type, ap);
    va_end(ap);

    return hc;
}

static bool hc_equal(HatCoords *hc1, HatCoords *hc2)
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

static bool hc_expect(const char *file, int line, HatCoords *hc,
                      TileType type, ...)
{
    bool equal;
    va_list ap;
    HatCoords *hce;

    va_start(ap, type);
    hce = hc_construct_v(type, ap);
    va_end(ap);

    equal = hc_equal(hc, hce);

    if (!equal) {
        fprintf(stderr, "%s:%d: coordinate mismatch\n", file, line);
        debug_coords("  expected: ", hce, "\n");
        debug_coords("  actual:   ", hc, "\n");
    }

    hc_free(hce);
    return equal;
}

#define EXPECT(hc, ...) do {                                    \
        if (!hc_expect(__FILE__, __LINE__, hc, __VA_ARGS__))    \
            fails++;                                            \
    } while (0)

static bool unit_tests(void)
{
    int fails = 0;
    HatCoordContext ctx[1];
    HatCoords *hc_in, *hc_out;

    ctx->rs = NULL;
    ctx->prototype = hc_construct(TT_KITE, 0, TT_HAT, 0, TT_H, -1);

    /* Simple steps within a hat */

    hc_in = hc_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_LEFT);
    EXPECT(hc_out, TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    hc_in = hc_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_RIGHT);
    EXPECT(hc_out, TT_KITE, 7, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    hc_in = hc_construct(TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_F_LEFT);
    EXPECT(hc_out, TT_KITE, 2, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    hc_in = hc_construct(TT_KITE, 5, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_F_RIGHT);
    EXPECT(hc_out, TT_KITE, 1, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    /* Step between hats in the same kitemap, which can change the
     * metatile type at layer 2 */

    hc_in = hc_construct(TT_KITE, 6, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_F_LEFT);
    EXPECT(hc_out, TT_KITE, 3, TT_HAT, 0, TT_H, 0, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    hc_in = hc_construct(TT_KITE, 7, TT_HAT, 2, TT_H, 1, TT_H, -1);
    hc_out = step_coords(ctx, hc_in, KS_F_RIGHT);
    EXPECT(hc_out, TT_KITE, 4, TT_HAT, 0, TT_T, 3, TT_H, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    /* Step off the edge of one kitemap, necessitating a metamap
     * rewrite of layers 2,3 to get into a different kitemap where
     * that step can be made */

    hc_in = hc_construct(TT_KITE, 6, TT_HAT, 0, TT_P, 2, TT_P, 3, TT_P, -1);
    hc_out = step_coords(ctx, hc_in, KS_F_RIGHT);
    /* Working:
     *     kite 6 . hat 0 . P 2 . P 3 . P ?
     *  -> kite 6 . hat 0 . P 6 . H 0 . P ?   (P metamap says 2.3 = 6.0)
     */
    EXPECT(hc_out, TT_KITE, 7, TT_HAT, 1, TT_H, 1, TT_H, 0, TT_P, -1);
    hc_free(hc_in);
    hc_free(hc_out);

    cleanup_coords(ctx);
    return fails == 0;
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

typedef enum OutFmt { OF_POSTSCRIPT, OF_PYTHON } OutFmt;

typedef struct drawctx {
    OutFmt outfmt;
    psbbox *bbox;
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
    switch (ctx->outfmt) {
      case OF_POSTSCRIPT: {
        float xext = ctx->bbox->tr.x - ctx->bbox->bl.x;
        float yext = ctx->bbox->tr.y - ctx->bbox->bl.y;
        float ext = (xext > yext ? xext : yext);
        float scale = 500 / ext;
        float ox = 287 - scale * (ctx->bbox->bl.x + ctx->bbox->tr.x) / 2;
        float oy = 421 - scale * (ctx->bbox->bl.y + ctx->bbox->tr.y) / 2;

        printf("%%!PS-Adobe-2.0\n%%%%Creator: hat-test from Simon Tatham's "
               "Portable Puzzle Collection\n%%%%Pages: 1\n"
               "%%%%BoundingBox: %f %f %f %f\n"
               "%%%%EndComments\n%%%%Page: 1 1\n",
               ox + scale * ctx->bbox->bl.x - 20,
               oy + scale * ctx->bbox->bl.y - 20,
               ox + scale * ctx->bbox->tr.x + 20,
               oy + scale * ctx->bbox->tr.y + 20);

        printf("%f %f translate %f dup scale\n", ox, oy, scale);
        printf("%f setlinewidth\n", scale * 0.03);
        printf("0 setgray 1 setlinejoin 1 setlinecap\n");
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
        if (hc->c[2].type == TT_H) {
            colour = (hc->c[1].index == 3 ? "0 0.5 0.8 setrgbcolor" :
                      "0.6 0.8 1 setrgbcolor");
        } else if (hc->c[2].type == TT_F) {
            colour = "0.7 setgray";
        } else {
            colour = "1 setgray";
        }
        printf(" %s fill grestore", colour);
        printf(" stroke\n");
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
      default:
        break;
    }
}

int main(int argc, char **argv)
{
    psbbox bbox[1];
    KiteEnum s[1];
    HatCoordContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    random_state *rs = random_new("12345", 5);
    int w = 10, h = 10;
    int argpos = 0;
    size_t i;
    drawctx dctx[1];

    dctx->outfmt = OF_POSTSCRIPT;

    while (--argc > 0) {
        const char *arg = *++argv;
        if (!strcmp(arg, "--help")) {
            printf("usage: hat-test [<width>] [<height>]\n"
                   "   or: hat-test --test\n");
            return 0;
        } else if (!strcmp(arg, "--test")) {
            return unit_tests() ? 0 : 1;
        } else if (!strcmp(arg, "--python")) {
            dctx->outfmt = OF_PYTHON;
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

    init_coords_random(ctx, rs);

    bbox->started = false;
    dctx->bbox = bbox;

    first_kite(s, w, h);
    coords[s->curr_index] = initial_coords(ctx);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     bbox_add_hat, dctx);
    while (next_kite(s)) {
        hc_free(coords[s->curr_index]);
        coords[s->curr_index] = step_coords(
            ctx, coords[s->last_index], s->last_step);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         bbox_add_hat, dctx);
    }
    for (i = 0; i < lenof(coords); i++) {
        hc_free(coords[i]);
        coords[i] = NULL;
    }

    header(dctx);

    first_kite(s, w, h);
    coords[s->curr_index] = initial_coords(ctx);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     draw_hat, dctx);
    while (next_kite(s)) {
        hc_free(coords[s->curr_index]);
        coords[s->curr_index] = step_coords(
            ctx, coords[s->last_index], s->last_step);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         draw_hat, dctx);
    }
    for (i = 0; i < lenof(coords); i++) {
        hc_free(coords[i]);
        coords[i] = NULL;
    }

    trailer(dctx);

    cleanup_coords(ctx);

    return 0;
}
#endif
