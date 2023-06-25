/*
 * Code to generate patches of the aperiodic 'spectre' tiling
 * discovered in 2023.
 *
 * Resources about the tiling from its discoverers:
 * https://cs.uwaterloo.ca/~csk/spectre/
 * https://arxiv.org/abs/2305.17743
 *
 * Writeup of the generation algorithm:
 * https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/aperiodic-spectre/
 */

#include <assert.h>
#include <string.h>

#include "puzzles.h"
#include "tree234.h"

#include "spectre-internal.h"

#include "spectre-tables-manual.h"
#include "spectre-tables-auto.h"

static const char *const letters =
    #define STRINGIFY(x) #x
    HEX_LETTERS(STRINGIFY)
    #undef STRINGIFY
    ;

bool spectre_valid_hex_letter(char letter)
{
    return strchr(letters, letter) != NULL;
}

static Hex hex_from_letter(char letter)
{
    char buf[2];
    buf[0] = letter;
    buf[1] = '\0';
    return strcspn(letters, buf);
}

static Hex hex_to_letter(unsigned char letter)
{
    return letters[letter];
}

struct HexData {
    const struct MapEntry *hexmap, *hexin, *specmap, *specin;
    const struct MapEdge *hexedges, *specedges;
    const Hex *subhexes;
    const struct Possibility *poss;
    size_t nposs;
};

static const struct HexData hexdata[] = {
    #define HEXDATA_ENTRY(x) { hexmap_##x, hexin_##x, specmap_##x, \
            specin_##x, hexedges_##x, specedges_##x, subhexes_##x, \
            poss_##x, lenof(poss_##x) },
    HEX_LETTERS(HEXDATA_ENTRY)
    #undef HEXDATA_ENTRY
};

static const struct Possibility *choose_poss(
    random_state *rs, const struct Possibility *poss, size_t nposs)
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

    for (i = 0; i < nposs; i++)
        limit += poss[i].prob;

    value = random_upto(rs, limit);

    for (i = 0; i+1 < nposs; i++) {
        if (value < poss[i].prob)
            return &poss[i];
        value -= poss[i].prob;
    }

    assert(i == nposs - 1);
    assert(value < poss[i].prob);
    return &poss[i];
}

SpectreCoords *spectre_coords_new(void)
{
    SpectreCoords *sc = snew(SpectreCoords);
    sc->nc = sc->csize = 0;
    sc->c = NULL;
    return sc;
}

void spectre_coords_free(SpectreCoords *sc)
{
    if (sc) {
        sfree(sc->c);
        sfree(sc);
    }
}

void spectre_coords_make_space(SpectreCoords *sc, size_t size)
{
    if (sc->csize < size) {
        sc->csize = sc->csize * 5 / 4 + 16;
        if (sc->csize < size)
            sc->csize = size;
        sc->c = sresize(sc->c, sc->csize, HexCoord);
    }
}

SpectreCoords *spectre_coords_copy(SpectreCoords *sc_in)
{
    SpectreCoords *sc_out = spectre_coords_new();
    spectre_coords_make_space(sc_out, sc_in->nc);
    memcpy(sc_out->c, sc_in->c, sc_in->nc * sizeof(*sc_out->c));
    sc_out->nc = sc_in->nc;
    sc_out->index = sc_in->index;
    sc_out->hex_colour = sc_in->hex_colour;
    sc_out->prev_hex_colour = sc_in->prev_hex_colour;
    sc_out->incoming_hex_edge = sc_in->incoming_hex_edge;
    return sc_out;
}

void spectre_place(Spectre *spec, Point u, Point v, int index_of_u)
{
    size_t i;
    Point disp;

    /* Vector from u to v */
    disp = point_sub(v, u);

    for (i = 0; i < 14; i++) {
        spec->vertices[(i + index_of_u) % 14] = u;
        u = point_add(u, disp);
        disp = point_mul(disp, point_rot(
                             spectre_angles[(i + 1 + index_of_u) % 14]));
    }
}

Spectre *spectre_initial(SpectreContext *ctx)
{
    Spectre *spec = snew(Spectre);
    spectre_place(spec, ctx->start_vertices[0], ctx->start_vertices[1], 0);
    spec->sc = spectre_coords_copy(ctx->prototype);
    return spec;
}

Spectre *spectre_adjacent(SpectreContext *ctx, const Spectre *src_spec,
                          unsigned src_edge, unsigned *dst_edge_out)
{
    unsigned dst_edge;
    Spectre *dst_spec = snew(Spectre);
    dst_spec->sc = spectre_coords_copy(src_spec->sc);
    spectrectx_step(ctx, dst_spec->sc, src_edge, &dst_edge);
    spectre_place(dst_spec, src_spec->vertices[(src_edge+1) % 14],
                  src_spec->vertices[src_edge], dst_edge);
    if (dst_edge_out)
        *dst_edge_out = dst_edge;
    return dst_spec;
}

static int spectre_cmp(void *av, void *bv)
{
    Spectre *a = (Spectre *)av, *b = (Spectre *)bv;
    size_t i, j;

    /* We should only ever need to compare the first two vertices of
     * any Spectre, because those force the rest */
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            int ac = a->vertices[i].coeffs[j], bc = b->vertices[i].coeffs[j];
            if (ac < bc)
                return -1;
            if (ac > bc)
                return +1;
        }
    }

    return 0;
}

void spectre_free(Spectre *spec)
{
    spectre_coords_free(spec->sc);
    sfree(spec);
}

static void spectrectx_start_vertices(SpectreContext *ctx, int orientation)
{
    Point minus_sqrt3 = point_add(point_rot(5), point_rot(-5));
    Point basicedge = point_mul(point_add(point_rot(0), point_rot(-3)),
                                point_rot(orientation));
    Point diagonal = point_add(basicedge, point_mul(basicedge, point_rot(-3)));
    ctx->start_vertices[0] = point_mul(diagonal, minus_sqrt3);
    ctx->start_vertices[1] = point_add(ctx->start_vertices[0], basicedge);
    ctx->orientation = orientation;
}

void spectrectx_init_random(SpectreContext *ctx, random_state *rs)
{
    const struct Possibility *poss;

    ctx->rs = rs;
    ctx->must_free_rs = false;
    ctx->prototype = spectre_coords_new();
    spectre_coords_make_space(ctx->prototype, 1);
    poss = choose_poss(rs, poss_spectre, lenof(poss_spectre));
    ctx->prototype->index = poss->lo;
    ctx->prototype->c[0].type = poss->hi;
    ctx->prototype->c[0].index = -1;
    ctx->prototype->nc = 1;

    /*
     * Choose a random orientation for the starting Spectre.
     *
     * The obvious thing is to choose the orientation out of all 12
     * possibilities. But we do it a more complicated way.
     *
     * The Spectres in a tiling can be partitioned into two
     * equivalence classes under the relation 'orientation differs by
     * a multiple of 1/6 turn'. One class is much more common than the
     * other class: the 'odd'-orientation Spectres occur rarely (very
     * like the rare reflected hats in the hats tiling).
     *
     * I think it's nicer to arrange that there's a consistent
     * orientation for the _common_ class of Spectres, so that there
     * will always be plenty of them in the 'canonical' orientation
     * with the head upwards. So if the starting Spectre is in the
     * even class, we pick an even orientation for it, and if it's in
     * the odd class, we pick an odd orientation.
     *
     * An odd-class Spectre is easy to identify from SpectreCoords.
     * They're precisely the ones expanded from a G hex with index 1,
     * which means they're the ones that have index 1 _at all_.
     */
    spectrectx_start_vertices(ctx, random_upto(rs, 6) * 2 +
                              ctx->prototype->index);

    /* Initialiise the colouring fields deterministically but unhelpfully.
     * spectre-test will set these up properly if it wants to */
    ctx->prototype->hex_colour = 0;
    ctx->prototype->prev_hex_colour = 0;
    ctx->prototype->incoming_hex_edge = 0;
}

void spectrectx_init_from_params(
    SpectreContext *ctx, const struct SpectrePatchParams *ps)
{
    size_t i;

    ctx->rs = NULL;
    ctx->must_free_rs = false;
    ctx->prototype = spectre_coords_new();
    spectre_coords_make_space(ctx->prototype, ps->ncoords);

    ctx->prototype->index = ps->coords[0];
    for (i = 1; i < ps->ncoords; i++)
        ctx->prototype->c[i-1].index = ps->coords[i];
    ctx->prototype->c[ps->ncoords-1].index = -1;
    ctx->prototype->nc = ps->ncoords;

    ctx->prototype->c[ps->ncoords-1].type = hex_from_letter(ps->final_hex);
    for (i = ps->ncoords - 1; i-- > 0 ;) {
        const struct HexData *h = &hexdata[ctx->prototype->c[i+1].type];
        ctx->prototype->c[i].type = h->subhexes[ctx->prototype->c[i].index];
    }

    spectrectx_start_vertices(ctx, ps->orientation);

    ctx->prototype->hex_colour = 0;
    ctx->prototype->prev_hex_colour = 0;
    ctx->prototype->incoming_hex_edge = 0;
}

void spectrectx_cleanup(SpectreContext *ctx)
{
    if (ctx->must_free_rs)
        random_free(ctx->rs);
    spectre_coords_free(ctx->prototype);
}

SpectreCoords *spectrectx_initial_coords(SpectreContext *ctx)
{
    return spectre_coords_copy(ctx->prototype);
}

/*
 * Extend sc until it has at least n coordinates in, by copying from
 * ctx->prototype if needed, and extending ctx->prototype if needed in
 * order to do that.
 */
void spectrectx_extend_coords(SpectreContext *ctx, SpectreCoords *sc, size_t n)
{
    if (ctx->prototype->nc < n) {
        spectre_coords_make_space(ctx->prototype, n);
        while (ctx->prototype->nc < n) {
            const struct HexData *h = &hexdata[
                ctx->prototype->c[ctx->prototype->nc-1].type];
            const struct Possibility *poss;

            if (!ctx->rs) {
                /*
                 * If there's no random_state available, it must be
                 * because we were given an explicit coordinate string
                 * and ran off the end of it.
                 *
                 * The obvious thing to do here would be to make up an
                 * answer non-randomly. But in fact there's a danger
                 * that this leads to endless recursion within a
                 * single coordinate step, if the hex edge we were
                 * trying to traverse turns into another copy of
                 * itself at the higher level. That happened in early
                 * testing before I put the random_state in at all.
                 *
                 * To avoid that risk, in this situation - which
                 * _shouldn't_ come up at all in sensibly play - we
                 * make up a random_state, and free it when the
                 * context goes away.
                 */
                ctx->rs = random_new("dummy", 5);
                ctx->must_free_rs = true;
            }

            poss = choose_poss(ctx->rs, h->poss, h->nposs);
            ctx->prototype->c[ctx->prototype->nc-1].index = poss->lo;
            ctx->prototype->c[ctx->prototype->nc].type = poss->hi;
            ctx->prototype->c[ctx->prototype->nc].index = -1;
            ctx->prototype->nc++;
        }
    }

    spectre_coords_make_space(sc, n);
    while (sc->nc < n) {
        assert(sc->c[sc->nc - 1].index == -1);
        assert(sc->c[sc->nc - 1].type == ctx->prototype->c[sc->nc - 1].type);
        sc->c[sc->nc - 1].index = ctx->prototype->c[sc->nc - 1].index;
        sc->c[sc->nc].index = -1;
        sc->c[sc->nc].type = ctx->prototype->c[sc->nc].type;
        sc->nc++;
    }
}

void spectrectx_step_hex(SpectreContext *ctx, SpectreCoords *sc,
                         size_t depth, unsigned edge, unsigned *outedge)
{
    const struct HexData *h;
    const struct MapEntry *m;

    spectrectx_extend_coords(ctx, sc, depth+2);

    assert(0 <= sc->c[depth].index);
    assert(sc->c[depth].index < num_subhexes(sc->c[depth].type));
    assert(0 <= edge);
    assert(edge < 6);

    h = &hexdata[sc->c[depth+1].type];
    m = &h->hexmap[6 * sc->c[depth].index + edge];
    if (!m->internal) {
        unsigned recedge;
        const struct MapEdge *me;
        spectrectx_step_hex(ctx, sc, depth+1, m->hi, &recedge);
        assert(recedge < 6);
        h = &hexdata[sc->c[depth+1].type];
        me = &h->hexedges[recedge];
        assert(m->lo < me->len);
        m = &h->hexin[me->startindex + me->len - 1 - m->lo];
        assert(m->internal);
    }
    sc->c[depth].index = m->hi;
    sc->c[depth].type = h->subhexes[sc->c[depth].index];
    *outedge = m->lo;

    if (depth == 0) {
        /*
         * Update the colouring fields to track the colour of the new
         * hexagon.
         */
        unsigned char new_hex_colour;

        if (!((edge ^ sc->incoming_hex_edge) & 1)) {
            /* We're going out via the same parity of edge we came in
             * on, so the new hex colour is the same as the previous
             * one. */
            new_hex_colour = sc->prev_hex_colour;
        } else {
            /* We're going out via the opposite parity of edge, so the
             * new colour is the one of {0,1,2} that is neither this
             * _nor_ the previous colour. */
            new_hex_colour = 0+1+2 - sc->hex_colour - sc->prev_hex_colour;
        }

        sc->prev_hex_colour = sc->hex_colour;
        sc->hex_colour = new_hex_colour;
        sc->incoming_hex_edge = m->lo;
    }
}

void spectrectx_step(SpectreContext *ctx, SpectreCoords *sc,
                     unsigned edge, unsigned *outedge)
{
    const struct HexData *h;
    const struct MapEntry *m;

    assert(0 <= sc->index);
    assert(sc->index < num_spectres(sc->c[0].type));
    assert(0 <= edge);
    assert(edge < 14);

    h = &hexdata[sc->c[0].type];
    m = &h->specmap[14 * sc->index + edge];

    while (!m->internal) {
        unsigned recedge;
        const struct MapEdge *me;
        spectrectx_step_hex(ctx, sc, 0, m->hi, &recedge);
        assert(recedge < 6);
        h = &hexdata[sc->c[0].type];
        me = &h->specedges[recedge];
        assert(m->lo < me->len);
        m = &h->specin[me->startindex + me->len - 1 - m->lo];
    }
    sc->index = m->hi;
    *outedge = m->lo;
}

void spectrectx_generate(SpectreContext *ctx,
                         bool (*callback)(void *cbctx, const Spectre *spec),
                         void *cbctx)
{
    tree234 *placed = newtree234(spectre_cmp);
    Spectre *qhead = NULL, *qtail = NULL;

    {
        Spectre *spec = spectre_initial(ctx);

        add234(placed, spec);

        spec->next = NULL;

        if (callback(cbctx, spec))
            qhead = qtail = spec;
    }

    while (qhead) {
        unsigned edge;
        Spectre *spec = qhead;

        for (edge = 0; edge < 14; edge++) {
            Spectre *new_spec;

            new_spec = spectre_adjacent(ctx, spec, edge, NULL);

            if (find234(placed, new_spec, NULL)) {
                spectre_free(new_spec);
                continue;
            }

            if (!callback(cbctx, new_spec)) {
                spectre_free(new_spec);
                continue;
            }

            add234(placed, new_spec);
            qtail->next = new_spec;
            qtail = new_spec;
            new_spec->next = NULL;
        }

        qhead = qhead->next;
    }

    {
        Spectre *spec;
        while ((spec = delpos234(placed, 0)) != NULL)
            spectre_free(spec);
        freetree234(placed);
    }
}

const char *spectre_tiling_params_invalid(
    const struct SpectrePatchParams *params)
{
    size_t i;
    Hex h;

    if (params->ncoords == 0)
        return "expected at least one numeric coordinate";
    if (!spectre_valid_hex_letter(params->final_hex))
        return "invalid final hexagon type";

    h = hex_from_letter(params->final_hex);
    for (i = params->ncoords; i-- > 0 ;) {
        unsigned limit = (i == 0) ? num_spectres(h) : num_subhexes(h);
        if (params->coords[i] >= limit)
            return "coordinate out of range";

        if (i > 0)
            h = hexdata[h].subhexes[params->coords[i]];
    }

    return NULL;
}

struct SpectreCallbackContext {
    int xoff, yoff;
    Coord xmin, xmax, ymin, ymax;

    spectre_tile_callback_fn external_cb;
    void *external_cbctx;
};

static bool spectre_internal_callback(void *vctx, const Spectre *spec)
{
    struct SpectreCallbackContext *ctx = (struct SpectreCallbackContext *)vctx;
    size_t i;
    int output_coords[4*14];

    for (i = 0; i < 14; i++) {
        Point p = spec->vertices[i];
        Coord x = point_x(p), y = point_y(p);
        if (coord_cmp(x, ctx->xmin) < 0 || coord_cmp(x, ctx->xmax) > 0 ||
            coord_cmp(y, ctx->ymin) < 0 || coord_cmp(y, ctx->ymax) > 0)
            return false;

        output_coords[4*i + 0] = ctx->xoff + x.c1;
        output_coords[4*i + 1] = x.cr3;
        output_coords[4*i + 2] = ctx->yoff - y.c1;
        output_coords[4*i + 3] = -y.cr3;
    }

    if (ctx->external_cb)
        ctx->external_cb(ctx->external_cbctx, output_coords);

    return true;
}

static void spectre_set_bounds(struct SpectreCallbackContext *cbctx,
                               int w, int h)
{
    cbctx->xoff = w/2;
    cbctx->yoff = h/2;
    cbctx->xmin.c1 = -cbctx->xoff;
    cbctx->xmax.c1 = -cbctx->xoff + w;
    cbctx->ymin.c1 = cbctx->yoff - h;
    cbctx->ymax.c1 = cbctx->yoff;
    cbctx->xmin.cr3 = 0;
    cbctx->xmax.cr3 = 0;
    cbctx->ymin.cr3 = 0;
    cbctx->ymax.cr3 = 0;
}

void spectre_tiling_randomise(struct SpectrePatchParams *ps, int w, int h,
                              random_state *rs)
{
    SpectreContext ctx[1];
    struct SpectreCallbackContext cbctx[1];
    size_t i;

    spectre_set_bounds(cbctx, w, h);
    cbctx->external_cb = NULL;
    cbctx->external_cbctx = NULL;

    spectrectx_init_random(ctx, rs);
    spectrectx_generate(ctx, spectre_internal_callback, cbctx);

    ps->orientation = ctx->orientation;
    ps->ncoords = ctx->prototype->nc;
    ps->coords = snewn(ps->ncoords, unsigned char);
    ps->coords[0] = ctx->prototype->index;
    for (i = 1; i < ps->ncoords; i++)
        ps->coords[i] = ctx->prototype->c[i-1].index;
    ps->final_hex = hex_to_letter(ctx->prototype->c[ps->ncoords-1].type);

    spectrectx_cleanup(ctx);
}

void spectre_tiling_generate(
    const struct SpectrePatchParams *params, int w, int h,
    spectre_tile_callback_fn external_cb, void *external_cbctx)
{
    SpectreContext ctx[1];
    struct SpectreCallbackContext cbctx[1];

    spectre_set_bounds(cbctx, w, h);
    cbctx->external_cb = external_cb;
    cbctx->external_cbctx = external_cbctx;

    spectrectx_init_from_params(ctx, params);
    spectrectx_generate(ctx, spectre_internal_callback, cbctx);
    spectrectx_cleanup(ctx);
}
