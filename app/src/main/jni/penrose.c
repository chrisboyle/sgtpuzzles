/*
 * Generate Penrose tilings via combinatorial coordinates.
 *
 * For general explanation of the algorithm:
 * https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/aperiodic-tilings/
 *
 * I use exactly the same indexing system here that's described in the
 * article. For the P2 tiling, acute isosceles triangles (half-kites)
 * are assigned letters A,B, and obtuse ones (half-darts) U,V; for P3,
 * acute triangles (half of a thin rhomb) are C,D and obtuse ones
 * (half a thick rhomb) are X,Y. Edges of all triangles are indexed
 * anticlockwise around the triangle, with 0 being the base and 1,2
 * being the two equal legs.
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "puzzles.h"
#include "penrose.h"
#include "penrose-internal.h"
#include "tree234.h"

bool penrose_valid_letter(char c, int which)
{
    switch (c) {
      case 'A': case 'B': case 'U': case 'V':
        return which == PENROSE_P2;
      case 'C': case 'D': case 'X': case 'Y':
        return which == PENROSE_P3;
      default:
        return false;
    }
}

/*
 * Result of attempting a transition within the coordinate system.
 * INTERNAL means we've moved to a different child of the same parent,
 * so the 'internal' substructure gives the type of the new triangle
 * and which edge of it we came in through; EXTERNAL means we've moved
 * out of the parent entirely, and the 'external' substructure tells
 * us which edge of the parent triangle we left by, and if it's
 * divided in two, which end of that edge (-1 for the left end or +1
 * for the right end). If the parent edge is undivided, end == 0.
 *
 * The type FAIL _shouldn't_ ever come up! It occurs if you try to
 * compute an incoming transition with an illegal value of 'end' (i.e.
 * having the wrong idea of whether the edge is divided), or if you
 * refer to a child triangle type that doesn't exist in that parent.
 * If it ever happens in the production code then an assertion will
 * fail. But it might be useful to other users of the same code.
 */
typedef struct TransitionResult {
    enum { INTERNAL, EXTERNAL, FAIL } type;
    union {
        struct {
            char new_child;
            unsigned char new_edge;
        } internal;
        struct {
            unsigned char parent_edge;
            signed char end;
        } external;
    } u;
} TransitionResult;

/* Construction function to make an INTERNAL-type TransitionResult */
static inline TransitionResult internal(char new_child, unsigned new_edge)
{
    TransitionResult tr;
    tr.type = INTERNAL;
    tr.u.internal.new_child = new_child;
    tr.u.internal.new_edge = new_edge;
    return tr;
}

/* Construction function to make an EXTERNAL-type TransitionResult */
static inline TransitionResult external(unsigned parent_edge, int end)
{
    TransitionResult tr;
    tr.type = EXTERNAL;
    tr.u.external.parent_edge = parent_edge;
    tr.u.external.end = end;
    return tr;
}

/* Construction function to make a FAIL-type TransitionResult */
static inline TransitionResult fail(void)
{
    TransitionResult tr;
    tr.type = FAIL;
    return tr;
}

/*
 * Compute a transition out of a triangle. Can return either INTERNAL
 * or EXTERNAL types (or FAIL if it gets invalid data).
 */
static TransitionResult transition(char parent, char child, unsigned edge)
{
    switch (parent) {
      case 'A':
        switch (child) {
          case 'A':
            switch (edge) {
              case 0: return external(2, -1);
              case 1: return external(0, 0);
              case 2: return internal('B', 1);
            }
          case 'B':
            switch (edge) {
              case 0: return internal('U', 1);
              case 1: return internal('A', 2);
              case 2: return external(1, +1);
            }
          case 'U':
            switch (edge) {
              case 0: return external(2, +1);
              case 1: return internal('B', 0);
              case 2: return external(1, -1);
            }
          default:
            return fail();
        }
      case 'B':
        switch (child) {
          case 'A':
            switch (edge) {
              case 0: return internal('V', 2);
              case 1: return external(2, -1);
              case 2: return internal('B', 1);
            }
          case 'B':
            switch (edge) {
              case 0: return external(1, +1);
              case 1: return internal('A', 2);
              case 2: return external(0, 0);
            }
          case 'V':
            switch (edge) {
              case 0: return external(1, -1);
              case 1: return external(2, +1);
              case 2: return internal('A', 0);
            }
          default:
            return fail();
        }
      case 'U':
        switch (child) {
          case 'B':
            switch (edge) {
              case 0: return internal('U', 1);
              case 1: return external(2, 0);
              case 2: return external(0, +1);
            }
          case 'U':
            switch (edge) {
              case 0: return external(1, 0);
              case 1: return internal('B', 0);
              case 2: return external(0, -1);
            }
          default:
            return fail();
        }
      case 'V':
        switch (child) {
          case 'A':
            switch (edge) {
              case 0: return internal('V', 2);
              case 1: return external(0, -1);
              case 2: return external(1, 0);
            }
          case 'V':
            switch (edge) {
              case 0: return external(2, 0);
              case 1: return external(0, +1);
              case 2: return internal('A', 0);
            }
          default:
            return fail();
        }
      case 'C':
        switch (child) {
          case 'C':
            switch (edge) {
              case 0: return external(1, +1);
              case 1: return internal('Y', 1);
              case 2: return external(0, 0);
            }
          case 'Y':
            switch (edge) {
              case 0: return external(2, 0);
              case 1: return internal('C', 1);
              case 2: return external(1, -1);
            }
          default:
            return fail();
        }
      case 'D':
        switch (child) {
          case 'D':
            switch (edge) {
              case 0: return external(2, -1);
              case 1: return external(0, 0);
              case 2: return internal('X', 2);
            }
          case 'X':
            switch (edge) {
              case 0: return external(1, 0);
              case 1: return external(2, +1);
              case 2: return internal('D', 2);
            }
          default:
            return fail();
        }
      case 'X':
        switch (child) {
          case 'C':
            switch (edge) {
              case 0: return external(2, +1);
              case 1: return internal('Y', 1);
              case 2: return internal('X', 1);
            }
          case 'X':
            switch (edge) {
              case 0: return external(1, 0);
              case 1: return internal('C', 2);
              case 2: return external(0, -1);
            }
          case 'Y':
            switch (edge) {
              case 0: return external(0, +1);
              case 1: return internal('C', 1);
              case 2: return external(2, -1);
            }
          default:
            return fail();
        }
      case 'Y':
        switch (child) {
          case 'D':
            switch (edge) {
              case 0: return external(1, -1);
              case 1: return internal('Y', 2);
              case 2: return internal('X', 2);
            }
          case 'X':
            switch (edge) {
              case 0: return external(0, -1);
              case 1: return external(1, +1);
              case 2: return internal('D', 2);
            }
          case 'Y':
            switch (edge) {
              case 0: return external(2, 0);
              case 1: return external(0, +1);
              case 2: return internal('D', 1);
            }
          default:
            return fail();
        }
      default:
        return fail();
    }
}

/*
 * Compute a transition into a parent triangle, after the above
 * function reported an EXTERNAL transition out of a neighbouring
 * parent and we had to recurse. Because we're coming inwards, this
 * should always return an INTERNAL TransitionResult (or FAIL if it
 * gets invalid data).
 */
static TransitionResult transition_in(char parent, unsigned edge, int end)
{
    #define EDGEEND(edge, end) (3 * (edge) + 1 + (end))

    switch (parent) {
      case 'A':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, 0): return internal('A', 1);
          case EDGEEND(1, -1): return internal('B', 2);
          case EDGEEND(1, +1): return internal('U', 2);
          case EDGEEND(2, -1): return internal('U', 0);
          case EDGEEND(2, +1): return internal('A', 0);
          default:
            return fail();
        }
      case 'B':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, 0): return internal('B', 2);
          case EDGEEND(1, -1): return internal('B', 0);
          case EDGEEND(1, +1): return internal('V', 0);
          case EDGEEND(2, -1): return internal('V', 1);
          case EDGEEND(2, +1): return internal('A', 1);
          default:
            return fail();
        }
      case 'U':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, -1): return internal('B', 2);
          case EDGEEND(0, +1): return internal('U', 2);
          case EDGEEND(1, 0): return internal('U', 0);
          case EDGEEND(2, 0): return internal('B', 1);
          default:
            return fail();
        }
      case 'V':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, -1): return internal('V', 1);
          case EDGEEND(0, +1): return internal('A', 1);
          case EDGEEND(1, 0): return internal('A', 2);
          case EDGEEND(2, 0): return internal('V', 0);
          default:
            return fail();
        }
      case 'C':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, 0): return internal('C', 2);
          case EDGEEND(1, -1): return internal('C', 0);
          case EDGEEND(1, +1): return internal('Y', 2);
          case EDGEEND(2, 0): return internal('Y', 0);
          default:
            return fail();
        }
      case 'D':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, 0): return internal('D', 1);
          case EDGEEND(1, 0): return internal('X', 0);
          case EDGEEND(2, -1): return internal('X', 1);
          case EDGEEND(2, +1): return internal('D', 0);
          default:
            return fail();
        }
      case 'X':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, -1): return internal('Y', 0);
          case EDGEEND(0, +1): return internal('X', 2);
          case EDGEEND(1, 0): return internal('X', 0);
          case EDGEEND(2, -1): return internal('C', 0);
          case EDGEEND(2, +1): return internal('Y', 2);
          default:
            return fail();
        }
      case 'Y':
        switch (EDGEEND(edge, end)) {
          case EDGEEND(0, +1): return internal('X', 0);
          case EDGEEND(0, -1): return internal('Y', 1);
          case EDGEEND(1, -1): return internal('X', 1);
          case EDGEEND(1, +1): return internal('D', 0);
          case EDGEEND(2, 0): return internal('Y', 0);
          default:
            return fail();
        }
      default:
        return fail();
    }

    #undef EDGEEND
}

PenroseCoords *penrose_coords_new(void)
{
    PenroseCoords *pc = snew(PenroseCoords);
    pc->nc = pc->csize = 0;
    pc->c = NULL;
    return pc;
}

void penrose_coords_free(PenroseCoords *pc)
{
    if (pc) {
        sfree(pc->c);
        sfree(pc);
    }
}

void penrose_coords_make_space(PenroseCoords *pc, size_t size)
{
    if (pc->csize < size) {
        pc->csize = pc->csize * 5 / 4 + 16;
        if (pc->csize < size)
            pc->csize = size;
        pc->c = sresize(pc->c, pc->csize, char);
    }
}

PenroseCoords *penrose_coords_copy(PenroseCoords *pc_in)
{
    PenroseCoords *pc_out = penrose_coords_new();
    penrose_coords_make_space(pc_out, pc_in->nc);
    memcpy(pc_out->c, pc_in->c, pc_in->nc * sizeof(*pc_out->c));
    pc_out->nc = pc_in->nc;
    return pc_out;
}

/*
 * The main recursive function for computing the next triangle's
 * combinatorial coordinates.
 */
static void penrosectx_step_recurse(
    PenroseContext *ctx, PenroseCoords *pc, size_t depth,
    unsigned edge, unsigned *outedge)
{
    TransitionResult tr;

    penrosectx_extend_coords(ctx, pc, depth+2);

    /* Look up the transition out of the starting triangle */
    tr = transition(pc->c[depth+1], pc->c[depth], edge);

    /* If we've left the parent triangle, recurse to find out what new
     * triangle we've landed in at the next size up, and then call
     * transition_in to find out which child of that parent we're
     * going to */
    if (tr.type == EXTERNAL) {
        unsigned parent_outedge;
        penrosectx_step_recurse(
            ctx, pc, depth+1, tr.u.external.parent_edge, &parent_outedge);
        tr = transition_in(pc->c[depth+1], parent_outedge, tr.u.external.end);
    }

    /* Now we should definitely have ended up in a child of the
     * (perhaps rewritten) parent triangle */
    assert(tr.type == INTERNAL);
    pc->c[depth] = tr.u.internal.new_child;
    *outedge = tr.u.internal.new_edge;
}

void penrosectx_step(PenroseContext *ctx, PenroseCoords *pc,
                     unsigned edge, unsigned *outedge)
{
    /* Allow outedge to be NULL harmlessly, just in case */
    unsigned dummy_outedge;
    if (!outedge)
        outedge = &dummy_outedge;

    penrosectx_step_recurse(ctx, pc, 0, edge, outedge);
}

static Point penrose_triangle_post_edge(char c, unsigned edge)
{
    static const Point acute_post_edge[3] = {
        {{-1, 1, 0, 1}}, /* phi * t^3 */
        {{-1, 1, -1, 1}}, /* t^4 */
        {{-1, 1, 0, 0}}, /* 1/phi * t^3 */
    };
    static const Point obtuse_post_edge[3] = {
        {{0, -1, 1, 0}}, /* 1/phi * t^4 */
        {{0, 0, 1, 0}}, /* t^2 */
        {{-1, 0, 0, 1}}, /* phi * t^4 */
    };

    switch (c) {
      case 'A': case 'B': case 'C': case 'D':
        return acute_post_edge[edge];
      default: /* case 'U': case 'V': case 'X': case 'Y': */
        return obtuse_post_edge[edge];
    }
}

void penrose_place(PenroseTriangle *tri, Point u, Point v, int index_of_u)
{
    unsigned i;
    Point here = u, delta = point_sub(v, u);

    for (i = 0; i < 3; i++) {
        unsigned edge = (index_of_u + i) % 3;
        tri->vertices[edge] = here;
        here = point_add(here, delta);
        delta = point_mul(delta, penrose_triangle_post_edge(
                              tri->pc->c[0], edge));
    }
}

void penrose_free(PenroseTriangle *tri)
{
    penrose_coords_free(tri->pc);
    sfree(tri);
}

static bool penrose_relative_probability(char c)
{
    /* Penrose tile probability ratios are always phi, so we can
     * approximate that very well using two consecutive Fibonacci
     * numbers */
    switch (c) {
      case 'A': case 'B': case 'X': case 'Y':
        return 165580141;
      case 'C': case 'D': case 'U': case 'V':
        return 102334155;
      default:
        return 0;
    }
}

static char penrose_choose_random(const char *possibilities, random_state *rs)
{
    const char *p;
    unsigned long value, limit = 0;

    for (p = possibilities; *p; p++)
        limit += penrose_relative_probability(*p);
    value = random_upto(rs, limit);
    for (p = possibilities; *p; p++) {
        unsigned long curr = penrose_relative_probability(*p);
        if (value < curr)
            return *p;
        value -= curr;
    }
    assert(false && "Probability overflow!");
    return possibilities[0];
}

static const char *penrose_starting_tiles(int which)
{
    return which == PENROSE_P2 ? "ABUV" : "CDXY";
}

static const char *penrose_valid_parents(char tile)
{
    switch (tile) {
      case 'A': return "ABV";
      case 'B': return "ABU";
      case 'U': return "AU";
      case 'V': return "BV";
      case 'C': return "CX";
      case 'D': return "DY";
      case 'X': return "DXY";
      case 'Y': return "CXY";
      default: return NULL;
    }
}

void penrosectx_init_random(PenroseContext *ctx, random_state *rs, int which)
{
    ctx->rs = rs;
    ctx->must_free_rs = false;
    ctx->prototype = penrose_coords_new();
    penrose_coords_make_space(ctx->prototype, 1);
    ctx->prototype->c[0] = penrose_choose_random(
        penrose_starting_tiles(which), rs);
    ctx->prototype->nc = 1;
    ctx->start_vertex = random_upto(rs, 3);
    ctx->orientation = random_upto(rs, 10);
}

void penrosectx_init_from_params(
    PenroseContext *ctx, const struct PenrosePatchParams *ps)
{
    size_t i;

    ctx->rs = NULL;
    ctx->must_free_rs = false;
    ctx->prototype = penrose_coords_new();
    penrose_coords_make_space(ctx->prototype, ps->ncoords);
    for (i = 0; i < ps->ncoords; i++)
        ctx->prototype->c[i] = ps->coords[i];
    ctx->prototype->nc = ps->ncoords;
    ctx->start_vertex = ps->start_vertex;
    ctx->orientation = ps->orientation;
}

void penrosectx_cleanup(PenroseContext *ctx)
{
    if (ctx->must_free_rs)
        random_free(ctx->rs);
    penrose_coords_free(ctx->prototype);
}

PenroseCoords *penrosectx_initial_coords(PenroseContext *ctx)
{
    return penrose_coords_copy(ctx->prototype);
}

void penrosectx_extend_coords(PenroseContext *ctx, PenroseCoords *pc,
                              size_t n)
{
    if (ctx->prototype->nc < n) {
        penrose_coords_make_space(ctx->prototype, n);
        while (ctx->prototype->nc < n) {
            if (!ctx->rs) {
                /*
                 * For safety, similarly to spectre.c, we respond to a
                 * lack of available random_state by making a
                 * deterministic one.
                 */
                ctx->rs = random_new("dummy", 5);
                ctx->must_free_rs = true;
            }

            ctx->prototype->c[ctx->prototype->nc] = penrose_choose_random(
                penrose_valid_parents(ctx->prototype->c[ctx->prototype->nc-1]),
                ctx->rs);
            ctx->prototype->nc++;
        }
    }

    penrose_coords_make_space(pc, n);
    while (pc->nc < n) {
        pc->c[pc->nc] = ctx->prototype->c[pc->nc];
        pc->nc++;
    }
}

static Point penrose_triangle_edge_0_length(char c)
{
    static const Point one = {{ 1, 0, 0, 0 }};
    static const Point phi = {{ 1, 0, 1, -1 }};
    static const Point invphi = {{ 0, 0, 1, -1 }};

    switch (c) {
        /* P2 tiling: unit-length edges are the long edges, i.e. edges
         * 1,2 of AB and edge 0 of UV. So AB have edge 0 short. */
      case 'A': case 'B':
        return invphi;
      case 'U': case 'V':
        return one;

        /* P3 tiling: unit-length edges are edges 1,2 of everything,
         * so CD have edge 0 short and XY have it long. */
      case 'C': case 'D':
        return invphi;
      default: /* case 'X': case 'Y': */
        return phi;
    }
}

PenroseTriangle *penrose_initial(PenroseContext *ctx)
{
    char type = ctx->prototype->c[0];
    Point origin = {{ 0, 0, 0, 0 }};
    Point edge0 = penrose_triangle_edge_0_length(type);
    Point negoffset;
    size_t i;
    PenroseTriangle *tri = snew(PenroseTriangle);

    /* Orient the triangle by deciding what vector edge #0 should traverse */
    edge0 = point_mul(edge0, point_rot(ctx->orientation));

    /* Place the triangle at an arbitrary position, in that orientation */
    tri->pc = penrose_coords_copy(ctx->prototype);
    penrose_place(tri, origin, edge0, 0);

    /* Now translate so that the appropriate vertex is at the origin */
    negoffset = tri->vertices[ctx->start_vertex];
    for (i = 0; i < 3; i++)
        tri->vertices[i] = point_sub(tri->vertices[i], negoffset);

    return tri;
}

PenroseTriangle *penrose_adjacent(PenroseContext *ctx,
                                  const PenroseTriangle *src_tri,
                                  unsigned src_edge, unsigned *dst_edge_out)
{
    unsigned dst_edge;
    PenroseTriangle *dst_tri = snew(PenroseTriangle);
    dst_tri->pc = penrose_coords_copy(src_tri->pc);
    penrosectx_step(ctx, dst_tri->pc, src_edge, &dst_edge);
    penrose_place(dst_tri, src_tri->vertices[(src_edge+1) % 3],
                  src_tri->vertices[src_edge], dst_edge);
    if (dst_edge_out)
        *dst_edge_out = dst_edge;
    return dst_tri;
}

static int penrose_cmp(void *av, void *bv)
{
    PenroseTriangle *a = (PenroseTriangle *)av, *b = (PenroseTriangle *)bv;
    size_t i, j;

    /* We should only ever need to compare the first two vertices of
     * any triangle, because those force the rest */
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

static unsigned penrose_sibling_edge_index(char c)
{
    switch (c) {
      case 'A': case 'U': return 2;
      case 'B': case 'V': return 1;
      default: return 0;
    }
}

void penrosectx_generate(
    PenroseContext *ctx,
    bool (*inbounds)(void *inboundsctx,
                     const PenroseTriangle *tri), void *inboundsctx,
    void (*tile)(void *tilectx, const Point *vertices), void *tilectx)
{
    tree234 *placed = newtree234(penrose_cmp);
    PenroseTriangle *qhead = NULL, *qtail = NULL;

    {
        PenroseTriangle *tri = penrose_initial(ctx);

        add234(placed, tri);

        tri->next = NULL;
        tri->reported = false;

        if (inbounds(inboundsctx, tri))
            qhead = qtail = tri;
    }

    while (qhead) {
        PenroseTriangle *tri = qhead;
        unsigned edge;
        unsigned sibling_edge = penrose_sibling_edge_index(tri->pc->c[0]);

        for (edge = 0; edge < 3; edge++) {
            PenroseTriangle *new_tri, *found_tri;

            new_tri = penrose_adjacent(ctx, tri, edge, NULL);

            if (!inbounds(inboundsctx, new_tri)) {
                penrose_free(new_tri);
                continue;
            }

            found_tri = find234(placed, new_tri, NULL);
            if (found_tri) {
                if (edge == sibling_edge && !tri->reported &&
                    !found_tri->reported) {
                    /*
                     * found_tri and tri are opposite halves of the
                     * same tile; both are in the tree, and haven't
                     * yet been reported as a completed tile.
                     */
                    unsigned new_sibling_edge = penrose_sibling_edge_index(
                        found_tri->pc->c[0]);
                    Point tilevertices[4] = {
                        tri->vertices[(sibling_edge + 1) % 3],
                        tri->vertices[(sibling_edge + 2) % 3],
                        found_tri->vertices[(new_sibling_edge + 1) % 3],
                        found_tri->vertices[(new_sibling_edge + 2) % 3],
                    };
                    tile(tilectx, tilevertices);

                    tri->reported = true;
                    found_tri->reported = true;
                }

                penrose_free(new_tri);
                continue;
            }

            add234(placed, new_tri);
            qtail->next = new_tri;
            qtail = new_tri;
            new_tri->next = NULL;
            new_tri->reported = false;
        }

        qhead = qhead->next;
    }

    {
        PenroseTriangle *tri;
        while ((tri = delpos234(placed, 0)) != NULL)
            penrose_free(tri);
        freetree234(placed);
    }
}

const char *penrose_tiling_params_invalid(
    const struct PenrosePatchParams *params, int which)
{
    size_t i;

    if (params->ncoords == 0)
        return "expected at least one coordinate";

    for (i = 0; i < params->ncoords; i++) {
        if (!penrose_valid_letter(params->coords[i], which))
            return "invalid coordinate letter";
        if (i > 0 && !strchr(penrose_valid_parents(params->coords[i-1]),
                             params->coords[i]))
            return "invalid pair of consecutive coordinates";
    }

    return NULL;
}

struct PenroseOutputCtx {
    int xoff, yoff;
    Coord xmin, xmax, ymin, ymax;

    penrose_tile_callback_fn external_cb;
    void *external_cbctx;
};

static bool inbounds(void *vctx, const PenroseTriangle *tri)
{
    struct PenroseOutputCtx *octx = (struct PenroseOutputCtx *)vctx;
    size_t i;

    for (i = 0; i < 3; i++) {
        Coord x = point_x(tri->vertices[i]);
        Coord y = point_y(tri->vertices[i]);

        if (coord_cmp(x, octx->xmin) < 0 || coord_cmp(x, octx->xmax) > 0 ||
            coord_cmp(y, octx->ymin) < 0 || coord_cmp(y, octx->ymax) > 0)
            return false;
    }

    return true;
}

static void null_output_tile(void *vctx, const Point *vertices)
{
}

static void really_output_tile(void *vctx, const Point *vertices)
{
    struct PenroseOutputCtx *octx = (struct PenroseOutputCtx *)vctx;
    size_t i;
    int coords[16];

    for (i = 0; i < 4; i++) {
        Coord x = point_x(vertices[i]);
        Coord y = point_y(vertices[i]);

        coords[4*i + 0] = x.c1 + octx->xoff;
        coords[4*i + 1] = x.cr5;
        coords[4*i + 2] = y.c1 + octx->yoff;
        coords[4*i + 3] = y.cr5;
    }

    octx->external_cb(octx->external_cbctx, coords);
}

static void penrose_set_bounds(struct PenroseOutputCtx *octx, int w, int h)
{
    octx->xoff = w/2;
    octx->yoff = h/2;
    octx->xmin.c1 = -octx->xoff;
    octx->xmax.c1 = -octx->xoff + w;
    octx->ymin.c1 = octx->yoff - h;
    octx->ymax.c1 = octx->yoff;
    octx->xmin.cr5 = 0;
    octx->xmax.cr5 = 0;
    octx->ymin.cr5 = 0;
    octx->ymax.cr5 = 0;
}

void penrose_tiling_randomise(struct PenrosePatchParams *params, int which,
                              int w, int h, random_state *rs)
{
    PenroseContext ctx[1];
    struct PenroseOutputCtx octx[1];

    penrose_set_bounds(octx, w, h);

    penrosectx_init_random(ctx, rs, which);
    penrosectx_generate(ctx, inbounds, octx, null_output_tile, NULL);

    params->orientation = ctx->orientation;
    params->start_vertex = ctx->start_vertex;
    params->ncoords = ctx->prototype->nc;
    params->coords = snewn(params->ncoords, char);
    memcpy(params->coords, ctx->prototype->c, params->ncoords);

    penrosectx_cleanup(ctx);
}

void penrose_tiling_generate(
    const struct PenrosePatchParams *params, int w, int h,
    penrose_tile_callback_fn cb, void *cbctx)
{
    PenroseContext ctx[1];
    struct PenroseOutputCtx octx[1];

    penrose_set_bounds(octx, w, h);
    octx->external_cb = cb;
    octx->external_cbctx = cbctx;

    penrosectx_init_from_params(ctx, params);
    penrosectx_generate(ctx, inbounds, octx, really_output_tile, octx);
    penrosectx_cleanup(ctx);
}
