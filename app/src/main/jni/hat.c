/*
 * Code to generate patches of the aperiodic 'hat' tiling discovered
 * in 2023.
 *
 * This uses the 'combinatorial coordinates' system documented in my
 * public article
 * https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/aperiodic-tilings/
 *
 * The internal document auxiliary/doc/hats.html also contains an
 * explanation of the basic ideas of this algorithm (less polished but
 * containing more detail).
 *
 * Neither of those documents can really be put in a source file,
 * because they just have too many complicated diagrams. So read at
 * least one of those first; the comments in here will refer to it.
 *
 * Discoverers' website: https://cs.uwaterloo.ca/~csk/hat/
 * Preprint of paper:    https://arxiv.org/abs/2303.10798
 */

#include <assert.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"
#include "hat.h"
#include "hat-internal.h"

void hat_kiteenum_first(KiteEnum *s, int w, int h)
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

bool hat_kiteenum_next(KiteEnum *s)
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
 * The actual tables.
 */
#include "hat-tables.h"

/*
 * One set of tables that we write by hand: the permitted ways to
 * extend the coordinate system outwards from a given metatile.
 *
 * One obvious approach would be to make a table of all the places
 * each metatile can appear in the expansion of another (e.g. H can be
 * subtile 0, 1 or 2 of another H, subtile 0 of a T, or 0 or 1 of a P
 * or an F), and when we need to decide what our current topmost tile
 * turns out to be a subtile of, choose equiprobably at random from
 * those options.
 *
 * That's what I did originally, but a better approach is to skew the
 * probabilities. We'd like to generate our patch of actual tiling
 * uniformly at random, in the sense that if you selected uniformly
 * from a very large region of the plane, the distribution of possible
 * finite patches of tiling would converge to some limit as that
 * region tended to infinity, and we'd be picking from that limiting
 * distribution on finite patches.
 *
 * For this we have to refer back to the original paper, which
 * indicates the subset of each metatile's expansion that can be
 * considered to 'belong' to that metatile, such that every subtile
 * belongs to exactly one parent metatile, and the overlaps are
 * eliminated. Reading out the diagrams from their Figure 2.8:
 *
 * - H: we discard three of the outer F subtiles, in the symmetric
 *    positions index by our coordinates as 7, 10, 11. So we keep the
 *    remaining subtiles {0,1,2,3,4,5,6,8,9,12}, which consist of
 *    three H, one T, three P and three F.
 *
 * - T: only the central H expanded from a T is considered to belong
 *   to it, so we just keep {0}, a single H.
 *
 * - P: we discard everything intersected by a long edge of the
 *   parallelogram, leaving the central three tiles and the endmost
 *   pair of F. That is, we keep {0,1,4,5,10}, consisting of two H,
 *   one P and two F.
 *
 * - F: looks like P at one end, and we retain the corresponding set
 *   of tiles there, but at the other end we keep the two F on either
 *   side of the endmost one. So we keep {0,1,3,6,8,10}, consisting of
 *   two H, one P and _three_ F.
 *
 * Adding up the tile numbers gives us this matrix system:
 *
 * (H_1)   (3 1 2 2)(H_0)
 * (T_1) = (1 0 0 0)(T_0)
 * (P_1)   (3 0 1 1)(P_0)
 * (F_1)   (3 0 2 3)(F_0)
 *
 * which says that if you have a patch of metatiling consisting of H_0
 * H tiles, T_0 T tiles etc, then this matrix shows the number H_1 of
 * smaller H tiles, etc, expanded from it.
 *
 * If you expand _many_ times, that's equivalent to raising the matrix
 * to a power:
 *
 *                  n
 * (H_n)   (3 1 2 2) (H_0)
 * (T_n) = (1 0 0 0) (T_0)
 * (P_n)   (3 0 1 1) (P_0)
 * (F_n)   (3 0 2 3) (F_0)
 *
 * The limiting distribution of metatiles is obtained by looking at
 * the four-way ratio between H_n, T_n, P_n and F_n as n tends to
 * infinity. To calculate this, we find the eigenvalues and
 * eigenvectors of the matrix, and extract the eigenvector
 * corresponding to the eigenvalue of largest magnitude. (Things get
 * more complicated in cases where there isn't a _unique_ eigenvalue
 * of largest magnitude, but here, there is.)
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
 * potential ancestry is three times what's shown here. (And T can't
 * occur at all as a parent.)
 *
 * In other words, we should choose _each coordinate_ with probability
 * corresponding to one of those numbers (scaled down so they all sum
 * to 1). Continuing to use P as an example, it will be:
 *
 *  - child 4 of H with relative probability 1
 *  - child 5 of H with relative probability 1
 *  - child 6 of H with relative probability 1
 *  - child 4 of P with relative probability 0.70820393249936908922
 *  - child 3 of F with relative probability 1.14589803375031545538
 *
 * and then we obtain the true probabilities by scaling those values
 * down so that they sum to 1.
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

static const MetatilePossibleParent *const possible_parents[] = {
    parents_H, parents_T, parents_P, parents_F,
};
static const size_t n_possible_parents[] = {
    lenof(parents_H), lenof(parents_T), lenof(parents_P), lenof(parents_F),
};

/*
 * Similarly, we also want to choose our absolute starting hat with
 * close to uniform probability, which again we do by looking at the
 * limiting ratio of the metatile types, and this time, scaling by the
 * number of hats in each metatile.
 *
 * We cheatingly use the same MetatilePossibleParent struct, because
 * it's got all the right fields, even if it has an inappropriate
 * name.
 */
static const MetatilePossibleParent starting_hats[] = {
    { TT_H,  0, PROB_H },
    { TT_H,  1, PROB_H },
    { TT_H,  2, PROB_H },
    { TT_H,  3, PROB_H },
    { TT_T,  0, PROB_P },
    { TT_P,  0, PROB_P },
    { TT_P,  1, PROB_P },
    { TT_F,  0, PROB_F },
    { TT_F,  1, PROB_F },
};

#undef PROB_H
#undef PROB_T
#undef PROB_P
#undef PROB_F

HatCoords *hat_coords_new(void)
{
    HatCoords *hc = snew(HatCoords);
    hc->nc = hc->csize = 0;
    hc->c = NULL;
    return hc;
}

void hat_coords_free(HatCoords *hc)
{
    if (hc) {
        sfree(hc->c);
        sfree(hc);
    }
}

void hat_coords_make_space(HatCoords *hc, size_t size)
{
    if (hc->csize < size) {
        hc->csize = hc->csize * 5 / 4 + 16;
        if (hc->csize < size)
            hc->csize = size;
        hc->c = sresize(hc->c, hc->csize, HatCoord);
    }
}

HatCoords *hat_coords_copy(HatCoords *hc_in)
{
    HatCoords *hc_out = hat_coords_new();
    hat_coords_make_space(hc_out, hc_in->nc);
    memcpy(hc_out->c, hc_in->c, hc_in->nc * sizeof(*hc_out->c));
    hc_out->nc = hc_in->nc;
    return hc_out;
}

static const MetatilePossibleParent *choose_mpp(
    random_state *rs, const MetatilePossibleParent *parents, size_t nparents)
{
    /*
     * If we needed to do this _efficiently_, we'd rewrite all those
     * tables above as cumulative frequency tables and use binary
     * search. But this happens about log n times in a grid of area n,
     * so it hardly matters, and it's easier to keep the tables
     * legible.
     */
    unsigned long limit = 0, value;
    size_t i;

    for (i = 0; i < nparents; i++)
        limit += parents[i].probability;

    value = random_upto(rs, limit);

    for (i = 0; i+1 < nparents; i++) {
        if (value < parents[i].probability)
            return &parents[i];
        value -= parents[i].probability;
    }

    assert(i == nparents - 1);
    assert(value < parents[i].probability);
    return &parents[i];
}
void hatctx_init_random(HatContext *ctx, random_state *rs)
{
    const MetatilePossibleParent *starting_hat = choose_mpp(
        rs, starting_hats, lenof(starting_hats));

    ctx->rs = rs;
    ctx->prototype = hat_coords_new();
    hat_coords_make_space(ctx->prototype, 3);
    ctx->prototype->c[2].type = starting_hat->type;
    ctx->prototype->c[2].index = -1;
    ctx->prototype->c[1].type = TT_HAT;
    ctx->prototype->c[1].index = starting_hat->index;
    ctx->prototype->c[0].type = TT_KITE;
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

static void init_coords_params(HatContext *ctx,
                               const struct HatPatchParams *hp)
{
    size_t i;

    ctx->rs = NULL;
    ctx->prototype = hat_coords_new();

    assert(hp->ncoords >= 3);

    hat_coords_make_space(ctx->prototype, hp->ncoords + 1);
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

HatCoords *hatctx_initial_coords(HatContext *ctx)
{
    return hat_coords_copy(ctx->prototype);
}

/*
 * Extend hc until it has at least n coordinates in, by copying from
 * ctx->prototype if needed, and extending ctx->prototype if needed in
 * order to do that.
 */
void hatctx_extend_coords(HatContext *ctx, HatCoords *hc, size_t n)
{
    if (ctx->prototype->nc < n) {
        hat_coords_make_space(ctx->prototype, n);
        while (ctx->prototype->nc < n) {
            TileType type = ctx->prototype->c[ctx->prototype->nc - 1].type;
            assert(ctx->prototype->c[ctx->prototype->nc - 1].index == -1);
            const MetatilePossibleParent *parent;

            if (ctx->rs)
                parent = choose_mpp(ctx->rs, possible_parents[type],
                                    n_possible_parents[type]);
            else
                parent = possible_parents[type];

            ctx->prototype->c[ctx->prototype->nc - 1].index = parent->index;
            ctx->prototype->c[ctx->prototype->nc].index = -1;
            ctx->prototype->c[ctx->prototype->nc].type = parent->type;
            ctx->prototype->nc++;
        }
    }

    hat_coords_make_space(hc, n);
    while (hc->nc < n) {
        assert(hc->c[hc->nc - 1].index == -1);
        assert(hc->c[hc->nc - 1].type == ctx->prototype->c[hc->nc - 1].type);
        hc->c[hc->nc - 1].index = ctx->prototype->c[hc->nc - 1].index;
        hc->c[hc->nc].index = -1;
        hc->c[hc->nc].type = ctx->prototype->c[hc->nc].type;
        hc->nc++;
    }
}

void hatctx_cleanup(HatContext *ctx)
{
    hat_coords_free(ctx->prototype);
}

/*
 * The actual system for finding the coordinates of an adjacent kite.
 */

/*
 * Kitemap step: ensure we have enough coordinates to know two levels
 * of meta-tiling, and use the kite map for the outer layer to move
 * around the individual kites. If this fails, return NULL.
 */
static HatCoords *try_step_coords_kitemap(
    HatContext *ctx, HatCoords *hc_in, KiteStep step)
{
    hatctx_extend_coords(ctx, hc_in, 4);
    hat_coords_debug("  try kitemap  ", hc_in, "\n");
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
        HatCoords *hc_out = hat_coords_copy(hc_in);

        hc_out->c[2].index = ke->meta;
        hc_out->c[2].type = children[meta2type][ke->meta];
        hc_out->c[1].index = ke->hat;
        hc_out->c[1].type = TT_HAT;
        hc_out->c[0].index = ke->kite;
        hc_out->c[0].type = TT_KITE;

        hat_coords_debug("  success!     ", hc_out, "\n");
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
    HatContext *ctx, HatCoords *hc_in, KiteStep step, size_t depth)
{
    HatCoords *hc_tmp = NULL, *hc_out;

    hatctx_extend_coords(ctx, hc_in, depth+3);
#ifdef HAT_COORDS_DEBUG
    fprintf(stderr, "  try meta %-4d", (int)depth);
    hat_coords_debug("", hc_in, "\n");
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
            hat_coords_free(hc_tmp);
            return hc_out;
        }

        me = &metamap[meta3type][metamap_index(meta, meta2)];
        assert(me->meta != -1);
        if (me->meta == meta_orig && me->meta2 == meta2_orig) {
            hat_coords_free(hc_tmp);
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
            hc_tmp = hat_coords_copy(hc_in);

        hc_tmp->c[depth+1].index = meta2;
        hc_tmp->c[depth+1].type = children[meta3type][meta2];
        hc_tmp->c[depth].index = meta;
        hc_tmp->c[depth].type = children[hc_tmp->c[depth+1].type][meta];

        hat_coords_debug("  rewritten -> ", hc_tmp, "\n");
    }
}

/*
 * The top-level algorithm for finding the next tile.
 */
HatCoords *hatctx_step(HatContext *ctx, HatCoords *hc_in, KiteStep step)
{
    HatCoords *hc_out;
    size_t depth;

#ifdef HAT_COORDS_DEBUG
    static const char *const directions[] = {
        " left\n", " right\n", " forward left\n", " forward right\n" };
    hat_coords_debug("step start     ", hc_in, directions[step]);
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
 * To do this, we iterate over the whole tiling via hat_kiteenum_first
 * and hat_kiteenum_next, and for each kite, calculate its
 * coordinates. But then we throw the coordinates away and don't do
 * anything with them!
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
    HatContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    KiteEnum s[1];
    size_t i;

    hatctx_init_random(ctx, rs);
    for (i = 0; i < lenof(coords); i++)
        coords[i] = NULL;

    hat_kiteenum_first(s, w, h);
    coords[s->curr_index] = hatctx_initial_coords(ctx);

    while (hat_kiteenum_next(s)) {
        hat_coords_free(coords[s->curr_index]);
        coords[s->curr_index] = hatctx_step(
            ctx, coords[s->last_index], s->last_step);
    }

    hp->ncoords = ctx->prototype->nc - 1;
    hp->coords = snewn(hp->ncoords, unsigned char);
    for (i = 0; i < hp->ncoords; i++)
        hp->coords[i] = ctx->prototype->c[i].index;
    hp->final_metatile = tilechars[ctx->prototype->c[hp->ncoords].type];

    hatctx_cleanup(ctx);
    for (i = 0; i < lenof(coords); i++)
        hat_coords_free(coords[i]);
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

void maybe_report_hat(int w, int h, Kite kite, HatCoords *hc,
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
    HatContext ctx[1];
    HatCoords *coords[KE_NKEEP];
    KiteEnum s[1];
    size_t i;
    struct internal_ctx report_hat_ctx[1];

    report_hat_ctx->external_cb = cb;
    report_hat_ctx->external_cbctx = cbctx;

    init_coords_params(ctx, hp);
    for (i = 0; i < lenof(coords); i++)
        coords[i] = NULL;

    hat_kiteenum_first(s, w, h);
    coords[s->curr_index] = hatctx_initial_coords(ctx);
    maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                     report_hat, report_hat_ctx);

    while (hat_kiteenum_next(s)) {
        hat_coords_free(coords[s->curr_index]);
        coords[s->curr_index] = hatctx_step(
            ctx, coords[s->last_index], s->last_step);
        maybe_report_hat(w, h, *s->curr, coords[s->curr_index],
                         report_hat, report_hat_ctx);
    }

    hatctx_cleanup(ctx);
    for (i = 0; i < lenof(coords); i++)
        hat_coords_free(coords[i]);
}
