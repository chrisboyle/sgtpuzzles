/*
 * Generate patches of tiling by the 'hat' aperiodic monotile
 * discovered in 2023.
 *
 * This implementation of hat-tiling generation was intended to be the
 * basis for generating hat grids for Loopy. However, it turned out
 * that I found a better strategy, so this source file isn't used by
 * the main puzzle system. I've kept it anyway because I ended up
 * adapting it to generate the file hat-tables.h containing the lookup
 * tables for the algorithm I _did_ end up using. It also generates
 * diagrams that are useful for understanding that algorithm, and for
 * debugging it if anything still turns out to be wrong with it.
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
#include "tree234.h"
#include "hat.h"

/*
 * General strategy:
 *
 * We construct the hat tiling by means of a substitution system of
 * 'metatiles' which come in four types, called H,T,P,F. A (valid)
 * tiling of these metatiles can be expanded to a larger one by a set
 * of recursive subdivision rules. Once we have a large enough patch
 * of metatiles, we apply a final transformation that converts each
 * metatile into 1, 2 or 4 instances of the aperiodically tiling
 * 'hat'.
 *
 * Unlike the similar substitution system for Penrose tilings, the
 * expansion rules are not geometrically precise: the larger versions
 * of each metatile fit together combinatorially in the same way, but
 * their shapes are distorted slightly. So we must construct our
 * expanded meta-tiling by breadth-first search out from a starting
 * metatile, because we won't quite know the coordinates of the
 * expanded version of each metatile until we know one of the ones
 * next to it.
 */

/*
 * Coordinate system:
 *
 * Everything in this code lives on the tiling known to grid.c as
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

static inline Point left6(Point p)
{
    /* r satisfies the equation r^2 = r-1. Hence, multiplying by r
     * (achieving a rotation anticlockwise by 1/6 turn) transforms
     * x+yr into xr+yr^2 = xr+y(r-1) = (-y) + (x+y)r.
     *
     * It's easy to check that iterating this transformation six times
     * gives you back the same coordinates you started with. */
    Point q = { -p.y, p.x + p.y };
    return q;
}

static inline Point right6(Point p)
{
    /* Conversely, 1/r = 1 - r, so dividing by r turns x+yr into x/r+y
     * = x(1-r) + y = (x+y) + (-x)r. */
    Point q = { p.x + p.y, -p.x };
    return q;
}

typedef enum MetatileType { MT_H, MT_T, MT_P, MT_F } MetatileType;
typedef struct Metatile Metatile;
typedef struct MetaCoord {
    Metatile *parent;
    int index;
} MetaCoord;

struct Metatile {
    /* Data fields describing the metatile and its position. */
    MetatileType type;
    Point start, orientation;

    MetaCoord coords[4];
    size_t ncoords;

    /* Auxiliary fields used to store the progress of the expansion
     * algorithm. */
    bool queued;
    Metatile *qnext;
};

#define MT_MAXVERT 6 /* largest number of vertices of any metatile */
#define MT_MAXVDEGREE 3 /* largest degree of any vertex of a meta-tiling */
#define MT_MAXEXPAND 13 /* largest number of metatiles in any expansion */
#define MT_MAXHAT 4 /* largest number of hats in a metatile */
#define HAT_NVERT 14 /* vertices of a single hat (counting the straight one) */
#define HAT_NKITE 8 /* kites in a single hat */

static int metatile_cmp(void *av, void *bv)
{
    Metatile *a = (Metatile *)av, *b = (Metatile *)bv;
    if (a->type < b->type) return -1;
    if (a->type > b->type) return +1;
    if (a->start.x < b->start.x) return -1;
    if (a->start.x > b->start.x) return +1;
    if (a->start.y < b->start.y) return -1;
    if (a->start.y > b->start.y) return +1;
    if (a->orientation.x < b->orientation.x) return -1;
    if (a->orientation.x > b->orientation.x) return +1;
    if (a->orientation.y < b->orientation.y) return -1;
    if (a->orientation.y > b->orientation.y) return +1;
    return 0;
}

/*
 * Return the coordinates of the vertices of a metatile, given the
 * coordinates of the vertex we deem to be distinguished, and a vector
 * of Euclidean length 1 showing its direction.
 *
 * If 'expanded' is true, we instead return the coordinates of the
 * corresponding vertices of the expanded version of the same
 * metatile.
 */
static size_t metatile_vertices(Metatile m, Point *out, bool expanded)
{
    static const Point vertices_H[] = {
        {0, 0}, {4, -2}, {12, 6}, {10, 10}, {-6, 18}, {-8, 16},
    };
    static const Point vertices_T[] = {
        {0, 0}, {6, 6}, {-6, 12},
    };
    static const Point vertices_P[] = {
        {0, 0}, {4, 4}, {-4, 20}, {-8, 16},
    };
    static const Point vertices_F[] = {
        {0, 0}, {4, -2}, {6, 0}, {-2, 16}, {-6, 12},
    };

    static const Point expanded_H[] = {
        {0, 0}, {12, -6}, {30, 12}, {24, 24}, {-12, 42}, {-18, 36},
    };
    static const Point expanded_T[] = {
        {0, 0}, {12, 12}, {-12, 24},
    };
    static const Point expanded_P[] = {
        {0, 0}, {14, 8}, {-4, 44}, {-18, 36},
    };
    static const Point expanded_F[] = {
        {0, 0}, {14, -4}, {18, 6}, {0, 42}, {-14, 34},
    };

    const Point *vertices;
    size_t nvertices;
    size_t i;

    switch (m.type) {
      case MT_H:
        vertices = expanded ? expanded_H : vertices_H;
        nvertices = lenof(vertices_H);
        break;
      case MT_T:
        vertices = expanded ? expanded_T : vertices_T;
        nvertices = lenof(vertices_T);
        break;
      case MT_P:
        vertices = expanded ? expanded_P : vertices_P;
        nvertices = lenof(vertices_P);
        break;
      default /* case MT_F */:
        vertices = expanded ? expanded_F : vertices_F;
        nvertices = lenof(vertices_F);
        break;
    }
    assert(nvertices <= MT_MAXVERT);

    Point orientation_r = left6(m.orientation);
    for (i = 0; i < nvertices; i++) {
        Point v = vertices[i];
        out[i].x = m.start.x + v.x * m.orientation.x + v.y * orientation_r.x;
        out[i].y = m.start.y + v.x * m.orientation.y + v.y * orientation_r.y;
    }
    return nvertices;
}

/*
 * Return a list of metatiles that arise from expanding a given tile.
 */
static size_t metatile_expand(Metatile m, Metatile *out)
{
    static const Metatile tiles_H[] = {
        {MT_H, {-4, 20}, {1, 0}},
        {MT_H, {2, 2}, {1, 0}},
        {MT_H, {8, 26}, {0, -1}},
        {MT_T, {6, 24}, {-1, 0}},
        {MT_P, {-8, 16}, {1, 0}},
        {MT_P, {4, 34}, {0, -1}},
        {MT_P, {6, 0}, {1, -1}},
        {MT_F, {-10, 38}, {-1, 1}},
        {MT_F, {-10, 44}, {0, -1}},
        {MT_F, {-4, 2}, {1, 0}},
        {MT_F, {2, 2}, {0, -1}},
        {MT_F, {26, 14}, {1, 0}},
        {MT_F, {32, 8}, {-1, 1}},
    };
    static const Metatile tiles_T[] = {
        {MT_H, {10, 10}, {-1, 1}},
        {MT_P, {-6, 0}, {1, 0}},
        {MT_P, {8, 14}, {0, 1}},
        {MT_P, {18, 6}, {-1, 1}},
        {MT_F, {-14, 34}, {-1, 0}},
        {MT_F, {-8, -2}, {1, -1}},
        {MT_F, {22, 4}, {0, 1}},
    };
    static const Metatile tiles_P[] = {
        {MT_H, {4, 22}, {0, 1}},
        {MT_H, {10, 10}, {-1, 1}},
        {MT_P, {-6, 0}, {1, 0}},
        {MT_P, {6, 24}, {1, 0}},
        {MT_P, {8, 14}, {0, 1}},
        {MT_F, {-20, 40}, {1, -1}},
        {MT_F, {-14, 34}, {-1, 0}},
        {MT_F, {-8, -2}, {1, -1}},
        {MT_F, {4, 46}, {-1, 1}},
        {MT_F, {10, 10}, {1, 0}},
        {MT_F, {16, 4}, {-1, 1}},
    };
    static const Metatile tiles_F[] = {
        {MT_H, {8, 20}, {0, 1}},
        {MT_H, {14, 8}, {-1, 1}},
        {MT_P, {10, 22}, {1, 0}},
        {MT_P, {12, 12}, {0, 1}},
        {MT_F, {-16, 38}, {1, -1}},
        {MT_F, {-10, 32}, {-1, 0}},
        {MT_F, {-4, 2}, {1, 0}},
        {MT_F, {2, 2}, {0, -1}},
        {MT_F, {8, 44}, {-1, 1}},
        {MT_F, {14, 8}, {1, 0}},
        {MT_F, {20, 2}, {-1, 1}},
    };

    const Metatile *tiles;
    size_t ntiles;
    size_t i;

    switch (m.type) {
      case MT_H:
        tiles = tiles_H;
        ntiles = lenof(tiles_H);
        break;
      case MT_T:
        tiles = tiles_T;
        ntiles = lenof(tiles_T);
        break;
      case MT_P:
        tiles = tiles_P;
        ntiles = lenof(tiles_P);
        break;
      default /* case MT_F */:
        tiles = tiles_F;
        ntiles = lenof(tiles_F);
        break;
    }
    assert(ntiles <= MT_MAXEXPAND);

    Point orientation_r = left6(m.orientation);
    for (i = 0; i < ntiles; i++) {
        Metatile t = tiles[i];
        out[i].type = t.type;
        out[i].start.x = (m.start.x + t.start.x * m.orientation.x +
                          t.start.y * orientation_r.x);
        out[i].start.y = (m.start.y + t.start.x * m.orientation.y +
                          t.start.y * orientation_r.y);
        out[i].orientation.x = (t.orientation.x * m.orientation.x +
                                t.orientation.y * orientation_r.x);
        out[i].orientation.y = (t.orientation.x * m.orientation.y +
                                t.orientation.y * orientation_r.y);
    }
    return ntiles;
}

/* Store data about each vertex during an expansion. */
typedef struct VertexMapping {
    Point in;

    /* Metatiles sharing this vertex */
    Metatile *tiles[MT_MAXVDEGREE];
    size_t ntiles;

    /* The expanded coordinates of this vertex, if known */
    bool mapped;
    Point out;
} VertexMapping;

static int vertexmapping_cmp(void *av, void *bv)
{
    VertexMapping *a = (VertexMapping *)av, *b = (VertexMapping *)bv;
    if (a->in.x < b->in.x) return -1;
    if (a->in.x > b->in.x) return +1;
    if (a->in.y < b->in.y) return -1;
    if (a->in.y > b->in.y) return +1;
    return 0;
}

static int vertexmapping_find(void *av, void *bv)
{
    Point *a = (Point *)av;
    VertexMapping *b = (VertexMapping *)bv;
    if (a->x < b->in.x) return -1;
    if (a->x > b->in.x) return +1;
    if (a->y < b->in.y) return -1;
    if (a->y > b->in.y) return +1;
    return 0;
}

typedef struct MetatileSet {
    /* The tiles in the set */
    tree234 *tiles;

    /*
     * Bounding box of a rectangular region within the original single
     * tile this set was expanded from. We need this in order to pick
     * a random chunk out of the tiling to return to our client: this
     * box is the limit of where we might select our chunk from.
     *
     * The box is obtained by starting from the two obtuse vertices of
     * the starting P metatile, and then mapping those two vertices
     * through each expansion pass. This wouldn't work for the _other_
     * two vertices of the P metatile, which end up in the middle of
     * another metatile after the first expansion, so that the next
     * expansion wouldn't find that point in its VertexMapping. But
     * luckily the two inner P vertices do continue working: they
     * alternate in subsequent expansions between vertex 1 and vertex
     * 4 of an F metatile. And those are the ones we need to define a
     * reliable bounding box - phew!
     */
    Point vertices[2];
    size_t nvertices;
} MetatileSet;

static MetatileSet *metatile_initial_set(MetatileType type)
{
    MetatileSet *s;
    Metatile *m;
    Point vertices[MT_MAXVERT];
    size_t nv;

    s = snew(MetatileSet);
    s->tiles = newtree234(metatile_cmp);

    m = snew(Metatile);
    m->type = type;
    m->start.x = 0;
    m->start.y = 0;
    m->orientation.x = 1;
    m->orientation.y = 0;
    m->ncoords = 0;
    add234(s->tiles, m);

    if (type == MT_P) {
        nv = metatile_vertices(*m, vertices, false);
        assert(nv == 4);
        s->vertices[0] = vertices[1];
        s->vertices[1] = vertices[3];
        s->nvertices = 2;
    } else {
        s->nvertices = 0;
    }

    return s;
}

static void metatile_free_set(MetatileSet *s)
{
    Metatile *m;

    while ((m = delpos234(s->tiles, 0)) != NULL)
        sfree(m);
    freetree234(s->tiles);
    sfree(s);
}

typedef struct Queue {
    Metatile *head, *tail;
} Queue;

static void map_vertex(VertexMapping *vm, Point out, Queue *queue)
{
    size_t i;

    debug(("map_vertex %d,%d -> %d,%d", vm->in.x, vm->in.y, out.x, out.y));
    if (vm->mapped) {
        debug((" (already done)\n"));
        return;
    }
    debug(("\n"));

    vm->mapped = true;
    vm->out = out;

    for (i = 0; i < vm->ntiles; i++) {
        Metatile *t = vm->tiles[i];
        if (!t->queued) {
            t->queued = true;
            t->qnext = NULL;
            if (queue->tail)
                queue->tail->qnext = t;
            else
                queue->head = t;
            queue->tail = t;
            debug(("queued %c @ %d,%d d=%d,%d\n", "HTPF"[t->type], t->start.x,
                   t->start.y, t->orientation.x, t->orientation.y));
        }
    }
}

/*
 * Expand a set of metatiles into its next-generation set. Returns the
 * new set. The old set is not freed, but the auxiliary fields of its
 * Metatile structures will be used as intermediate storage.
 */
static MetatileSet *metatile_set_expand(MetatileSet *si)
{
    tree234 *vmap;
    VertexMapping *vm;
    Metatile *m;
    Queue queue = { NULL, NULL };
    size_t i, j;
    MetatileSet *so = snew(MetatileSet);

    so->tiles = newtree234(metatile_cmp);

    /*
     * Enumerate all the vertices in our tiling, and store the set of
     * tiles they belong to.
     */
    vmap = newtree234(vertexmapping_cmp);
    for (i = 0; (m = index234(si->tiles, i)) != NULL; i++) {
        Point vertices[MT_MAXVERT];
        size_t nv = metatile_vertices(*m, vertices, false);

        for (j = 0; j < nv; j++) {
            VertexMapping *newvm = snew(VertexMapping);
            newvm->in = vertices[j];
            newvm->ntiles = 0;
            newvm->mapped = false;

            vm = add234(vmap, newvm);
            if (vm != newvm)
                sfree(newvm);

            assert(vm->ntiles < MT_MAXVDEGREE);
            vm->tiles[vm->ntiles++] = m;
        }

        m->queued = false;
    }

    for (i = 0; (vm = index234(vmap, i)) != NULL; i++) {
        debug(("vertex @ %d,%d {", vm->in.x, vm->in.y));
        for (j = 0; j < vm->ntiles; j++) {
            m = vm->tiles[j];
            debug(("%s%c @ %d,%d d=%d,%d", j?", ":"", "HTPF"[m->type],
                   m->start.x, m->start.y, m->orientation.x,
                   m->orientation.y));
        }
        debug(("}\n"));
    }

    /*
     * Initialise an arbitrary vertex to a known location.
     */
    {
        Point p = {0, 0};
        m = index234(si->tiles, 0);
        vm = find234(vmap, &m->start, vertexmapping_find);
        map_vertex(vm, p, &queue);
    }

    /*
     * Now process the queue of tiles to be expanded.
     */
    debug(("-- start\n"));
    while (queue.head) {
        Metatile *m, m_moved;
        Metatile t[MT_MAXEXPAND];
        Point vi[MT_MAXVERT], vo[MT_MAXVERT];
        size_t nv, nt;

        m = queue.head;
        queue.head = queue.head->qnext;
        if (!queue.head)
            queue.tail = NULL;

        debug(("unqueued %c @ %d,%d d=%d,%d\n", "HTPF"[m->type],
               m->start.x, m->start.y, m->orientation.x, m->orientation.y));

        nv = metatile_vertices(*m, vi, false);
        metatile_vertices(*m, vo, true);

        /* Find a vertex of this tile that's already mapped, and use
         * it to determine the placement. */
        int dx, dy;
        for (i = 0; i < nv; i++) {
            vm = find234(vmap, &vi[i], vertexmapping_find);
            assert(vm);
            if (vm->mapped) {
                dx = vm->out.x - vo[i].x;
                dy = vm->out.y - vo[i].y;
                debug(("found mapped vertex %d,%d -> %d,%d: "
                       "offset=%d,%d\n",
                       vm->in.x, vm->in.y, vm->out.x, vm->out.y, dx, dy));
                break;
            }
        }
        assert(i < nv && "Why was this tile queued without a mapped vertex?");

        /* Now map all the rest of the vertices of the tile. */
        for (i = 0; i < nv; i++) {
            vm = find234(vmap, &vi[i], vertexmapping_find);
            vo[i].x += dx;
            vo[i].y += dy;
            map_vertex(vm, vo[i], &queue);
        }

        /* And expand it, substituting in its new starting coordinate. */
        m_moved = *m; /* structure copy */
        m_moved.start = vo[0];
        nt = metatile_expand(m_moved, t);
        for (i = 0; i < nt; i++) {
            Metatile *newmt = snew(Metatile);
            *newmt = t[i]; /* structure copy */
            newmt->ncoords = 0;
            debug(("expanded %c @ %d,%d d=%d,%d\n", "HTPF"[newmt->type],
                   newmt->start.x, newmt->start.y, newmt->orientation.x,
                   newmt->orientation.y));

            Metatile *added = add234(so->tiles, newmt);
            if (added != newmt)
                sfree(newmt);
            assert(added->ncoords < lenof(added->coords));
            added->coords[added->ncoords].parent = m;
            added->coords[added->ncoords].index = i;
            added->ncoords++;
        }
    }

    for (i = 0; (m = index234(si->tiles, i)) != NULL; i++) {
        if (!m->queued)
            debug(("OMITTED %c @ %d,%d d=%d,%d\n", "HTPF"[m->type],
                   m->start.x, m->start.y, m->orientation.x,
                   m->orientation.y));
    }

    /*
     * Write out the remapped versions of the tile set's bounding
     * vertices.
     */
    for (i = 0; i < si->nvertices; i++) {
        vm = find234(vmap, &si->vertices[i], vertexmapping_find);
        so->vertices[i] = vm->out;
    }
    so->nvertices = si->nvertices;

    while ((vm = delpos234(vmap, 0)) != NULL)
        sfree(vm);
    freetree234(vmap);

    return so;
}

typedef struct Hat {
    Point start, orientation;
    bool reversed;
    const Metatile *parent;
    int index;
} Hat;

static size_t metatile_hats(const Metatile *m, Hat *out)
{
    static const Hat hats_H[] = {
        {{6, 0}, {1, 0}, false},
        {{6, 6}, {0, -1}, false},
        {{0, 12}, {1, 0}, false},
        {{0, 6}, {-1, 0}, true},
    };
    static const Hat hats_T[] = {
        {{-2, 10}, {-1, 1}, false},
    };
    static const Hat hats_P[] = {
        {{-2, 10}, {-1, 1}, false},
        {{-2, 16}, {0, 1}, false},
    };
    static const Hat hats_F[] = {
        {{0, 6}, {-1, 1}, false},
        {{0, 12}, {0, 1}, false},
    };

    const Hat *hats;
    size_t nhats;
    size_t i;

    switch (m->type) {
      case MT_H:
        hats = hats_H;
        nhats = lenof(hats_H);
        break;
      case MT_T:
        hats = hats_T;
        nhats = lenof(hats_T);
        break;
      case MT_P:
        hats = hats_P;
        nhats = lenof(hats_P);
        break;
      default /* case MT_F */:
        hats = hats_F;
        nhats = lenof(hats_F);
        break;
    }
    assert(nhats <= MT_MAXHAT);

    Point orientation_r = left6(m->orientation);
    for (i = 0; i < nhats; i++) {
        Hat h = hats[i];
        out[i].parent = m;
        out[i].index = i;
        out[i].start.x = (m->start.x + h.start.x * m->orientation.x +
                          h.start.y * orientation_r.x);
        out[i].start.y = (m->start.y + h.start.x * m->orientation.y +
                          h.start.y * orientation_r.y);
        out[i].orientation.x = (h.orientation.x * m->orientation.x +
                                h.orientation.y * orientation_r.x);
        out[i].orientation.y = (h.orientation.x * m->orientation.y +
                                h.orientation.y * orientation_r.y);
        out[i].reversed = h.reversed;
    }
    return nhats;
}

static size_t hat_vertices(Hat h, Point *out)
{
    static const Point reference_hat[] = {
        {0, 0}, {3, 0}, {2, 2}, {0, 3}, {-2, 4}, {-3, 3}, {-6, 6}, {-9, 6},
        {-8, 4}, {-6, 3}, {-6, 0}, {-3, -3}, {-2, -2}, {0, -3},
    };

    size_t i;

    Point orientation_r;
    if (h.reversed)
        orientation_r = right6(h.orientation);
    else
        orientation_r = left6(h.orientation);
    assert(lenof(reference_hat) == HAT_NVERT);

    for (i = 0; i < lenof(reference_hat); i++) {
        Point v = reference_hat[h.reversed ? HAT_NVERT-1-i : i];
        out[i].x = h.start.x + v.x * h.orientation.x + v.y * orientation_r.x;
        out[i].y = h.start.y + v.x * h.orientation.y + v.y * orientation_r.y;
    }
    return lenof(reference_hat);
}

typedef struct BoundingBox {
    Point bl, tr;
} BoundingBox;

static bool point_in_bbox(Point p, const BoundingBox *bbox)
{
    int xl, xr, x;

    if (!bbox)
        return true;

    /*
     * Bounding boxes have vertical edges, not aligned with our basis
     * vector r. So the 'true' x coordinate of (x,y) is proportional
     * to 2x+y.
     */
    if (p.y < bbox->bl.y || p.y > bbox->tr.y)
        return false;

    xl = 2*bbox->bl.x + bbox->bl.y;
    xr = 2*bbox->tr.x + bbox->tr.y;
    x = 2*p.x + p.y;

    if (x < xl || x > xr)
        return false;

    return true;
}

static bool hat_in_bbox(Hat h, const BoundingBox *bbox)
{
    Point p[HAT_NVERT];
    size_t i, np;

    if (!bbox)
        return true;

    np = hat_vertices(h, p);
    for (i = 0; i < np; i++)
        if (!point_in_bbox(p[i], bbox))
            return false;

    return true;
}

static Hat *metatile_set_to_hats(MetatileSet *s, size_t *nhats,
                                 const BoundingBox *bbox)
{
    Metatile *m;
    size_t i, j, k, n;
    Hat *h;

    n = 0;
    for (i = 0; (m = index234(s->tiles, i)) != NULL; i++) {
        Hat htmp[MT_MAXHAT];
        size_t nthis = metatile_hats(m, htmp);
        for (k = 0; k < nthis; k++)
            if (hat_in_bbox(htmp[k], bbox))
                n++;
    }

    *nhats = n;
    h = snewn(n, Hat);

    j = 0;
    for (i = 0; (m = index234(s->tiles, i)) != NULL; i++) {
        Hat htmp[MT_MAXHAT];
        size_t nthis = metatile_hats(m, htmp);
        for (k = 0; k < nthis; k++) {
            if (hat_in_bbox(htmp[k], bbox)) {
                assert(j < n);
                h[j++] = htmp[k]; /* structure copy */
            }
        }
    }

    assert(j == n);
    return h;
}

#if 0
void hat_tiling_randomise(struct HatPatchParams *params, random_state *rs)
{
    MetatileSet *s, *s2;
    int x0, x1, y0, y1;

    /*
     * Iterate until we have a good-sized patch to select a rectangle
     * from.
     */
    s = metatile_initial_set(MT_P);
    params->iterations = 0;

    while (true) {
        x0 = 2 * s->vertices[0].x + s->vertices[0].y;
        x1 = 2 * s->vertices[1].x + s->vertices[1].y;
        if (x1 < x0) {
            int t = x1;
            x1 = x0;
            x0 = t;
        }
        y0 = s->vertices[0].y;
        y1 = s->vertices[1].y;
        if (y1 < y0) {
            int t = y1;
            y1 = y0;
            y0 = t;
        }

        if (50*params->w <= x1-x0 && 50*params->h <= y1-y0)
            break;

        params->iterations++;
        s2 = metatile_set_expand(s);
        metatile_free_set(s);
        s = s2;
    }

    /*
     * Now select that rectangle.
     */
    params->x = x0 + random_upto(rs, x1 - x0 - params->w + 1);
    params->y = y0 + random_upto(rs, y1 - y0 - params->h + 1);
}

void hat_tiling_generate(struct HatPatchParams *params,
                         hat_tile_callback_fn cb, void *cbctx)
{
    MetatileSet *s, *s2;
    unsigned i;
    size_t j, nh;
    BoundingBox bbox;
    Hat *hats;

    s = metatile_initial_set(MT_P);
    for (i = 0; i < params->iterations; i++) {
        s2 = metatile_set_expand(s);
        metatile_free_set(s);
        s = s2;
    }

    bbox.bl.x = (params->x - params->y) / 2;
    bbox.tr.x = ((params->x + params->w) - (params->y + params->h)) / 2;
    bbox.bl.y = params->y;
    bbox.tr.y = params->y + params->h;
    hats = metatile_set_to_hats(s, &nh, &bbox);
    for (j = 0; j < nh; j++) {
        Point vertices[HAT_NVERT];
        size_t nv = hat_vertices(hats[j], vertices);
        int out[2 * HAT_NVERT];
        size_t k;

        for (k = 0; k < nv; k++) {
            out[2*k] = 2 * vertices[k].x + vertices[k].y;
            out[2*k+1] = vertices[k].y;
        }

        cb(cbctx, nv, out);
    }
    sfree(hats);

    metatile_free_set(s);
}

#endif

#ifdef TEST_HAT

/*
 * Assortment of test modes that output Postscript diagrams.
 */

static size_t hat_kite_centres(Hat h, Point *out)
{
    static const Point reference_hat[] = {
        {-7,5},{-5,4},{-5,1},{-4,-1},{-1,-1},{-2,1},{-1,2},{1,1},
    };

    size_t i;

    Point orientation_r;
    if (h.reversed)
        orientation_r = right6(h.orientation);
    else
        orientation_r = left6(h.orientation);
    assert(lenof(reference_hat) == HAT_NKITE);

    for (i = 0; i < lenof(reference_hat); i++) {
        Point v = reference_hat[i];
        out[i].x = h.start.x + v.x * h.orientation.x + v.y * orientation_r.x;
        out[i].y = h.start.y + v.x * h.orientation.y + v.y * orientation_r.y;
    }
    return lenof(reference_hat);
}

static inline int round6(int x)
{
    int sign = x<0 ? -1 : +1;
    x *= sign;
    x += 3;
    x /= 6;
    x *= 6;
    x *= sign;
    return x;
}

static inline Point kite_left(Point k)
{
    Point centre = { round6(k.x), round6(k.y) };
    Point offset = { k.x - centre.x, k.y - centre.y };
    offset = left6(offset);
    Point r = { centre.x + offset.x, centre.y + offset.y };
    return r;
}

static inline Point kite_right(Point k)
{
    Point centre = { round6(k.x), round6(k.y) };
    Point offset = { k.x - centre.x, k.y - centre.y };
    offset = right6(offset);
    Point r = { centre.x + offset.x, centre.y + offset.y };
    return r;
}

static inline Point kite_forward_left(Point k)
{
    Point centre = { round6(k.x), round6(k.y) };
    Point offset = { k.x - centre.x, k.y - centre.y };
    Point rotate = left6(offset);
    Point r = { k.x + rotate.x + offset.x, k.y + rotate.y + offset.y };
    return r;
}

static inline Point kite_forward_right(Point k)
{
    Point centre = { round6(k.x), round6(k.y) };
    Point offset = { k.x - centre.x, k.y - centre.y };
    Point rotate = right6(offset);
    Point r = { k.x + rotate.x + offset.x, k.y + rotate.y + offset.y };
    return r;
}

typedef struct pspoint {
    float x, y;
} pspoint;

static inline pspoint pscoords(Point p)
{
    pspoint q = { p.x + p.y / 2.0F, p.y * sqrt(0.75) };
    return q;
}

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

static void draw_metatiles_svg(const Metatile *tiles, size_t n,
                               const Point *bounds, bool coords)
{
    size_t i, j;
    psbbox bbox = { false };

    for (i = 0; i < n; i++) {
        Point vertices[MT_MAXVERT];
        size_t nv = metatile_vertices(tiles[i], vertices, false);
        for (j = 0; j < nv; j++)
            psbbox_add(&bbox, pscoords(vertices[j]));
    }

    float ascale = 10, xscale = ascale, yscale = -ascale;
    float border = 0.2 * ascale; /* leave room for strokes at the edges */
    float ox = -xscale * bbox.bl.x + border;
    float oy = -yscale * bbox.tr.y + border;

    printf("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
           "width=\"%g\" height=\"%g\">\n",
           ceil(ox + xscale * bbox.tr.x + 2*border),
           ceil(oy + yscale * bbox.bl.y + 2*border));

    for (i = 0; i < n; i++) {
        Point vertices[MT_MAXVERT];
        size_t nv = metatile_vertices(tiles[i], vertices, false);
        pspoint pp[MT_MAXVERT];

        for (j = 0; j < nv; j++) {
            pp[j] = pscoords(vertices[j]);
            pp[j].x = ox + xscale * pp[j].x;
            pp[j].y = oy + yscale * pp[j].y;
        }

        printf("<path style=\""
               "fill: none; "
               "stroke: black; "
               "stroke-width: %f; "
               "stroke-linejoin: round; "
               "stroke-linecap: round; "
               "\" d=\"", 0.2 * ascale);
        for (j = 0; j < nv; j++)
            printf("%s %f %f \n", j ? "L" : "M", pp[j].x, pp[j].y);
        printf("z\" />\n");

        if (tiles[i].type != MT_F) {
            /*
             * Mark arrows on three of the metatile types (H, T, P),
             * following the diagrams in the paper. (The metatile
             * shapes other than F each have some symmetry, but their
             * roles in the metatile substitution system are not
             * similarly symmetric, so for diagnostic diagrams you
             * want to mark their orientation.)
             */
            pspoint lstart, lend;
            pspoint astart, aend, aforward, aleft;
            double d;

            /*
             * Determine endpoints of a line crossing the polygon in
             * the appropriate direction, by case analysis on the
             * individual tile types.
             */
            switch (tiles[i].type) {
              case MT_H:
                lstart.x = (pp[4].x + pp[5].x) / 2;
                lstart.y = (pp[4].y + pp[5].y) / 2;
                lend.x = (pp[1].x + pp[2].x) / 2;
                lend.y = (pp[1].y + pp[2].y) / 2;
                break;
              case MT_T:
                lstart = pp[0];
                lend.x = (pp[1].x + pp[2].x) / 2;
                lend.y = (pp[1].y + pp[2].y) / 2;
                break;
              default /* case MT_P */:
                lstart.x = (5*pp[3].x + 3*pp[0].x) / 8;
                lstart.y = (5*pp[3].y + 3*pp[0].y) / 8;
                lend.x = (5*pp[1].x + 3*pp[2].x) / 8;
                lend.y = (5*pp[1].y + 3*pp[2].y) / 8;
                break;
            }

            /*
             * Now shorten that line a little and give it an arrowhead.
             */
            astart.x = (4 * lstart.x + lend.x) / 5;
            astart.y = (4 * lstart.y + lend.y) / 5;
            aend.x = (lstart.x + 4 * lend.x) / 5;
            aend.y = (lstart.y + 4 * lend.y) / 5;
            aforward.x = aend.x - astart.x;
            aforward.y = aend.y - astart.y;
            d = sqrt(aforward.x*aforward.x + aforward.y*aforward.y);
            aforward.x /= d;
            aforward.y /= d;
            aleft.x = -aforward.y;
            aleft.y = +aforward.x;

            printf("<path style=\""
                   "fill: none; "
                   "stroke: black; "
                   "stroke-width: %f; "
                   "stroke-opacity: 0.2; "
                   "stroke-linejoin: round; "
                   "stroke-linecap: round; "
                   "\" d=\"", 0.9 * ascale);

            printf("M %f %f L %f %f ", astart.x, astart.y,
                   aend.x, aend.y);
            printf("L %f %f ",
                   aend.x - 1.2 * ascale * (aforward.x + aleft.x),
                   aend.y - 1.2 * ascale * (aforward.y + aleft.y));
            printf("M %f %f L %f %f ", aend.x, aend.y,
                   aend.x - 1.2 * ascale * (aforward.x - aleft.x),
                   aend.y - 1.2 * ascale * (aforward.y - aleft.y));
            printf("\" />\n");
        }

        if (coords) {
            /*
             * Print each tile's coordinates.
             */
            pspoint centre;
            size_t j;

            switch (tiles[i].type) {
              case MT_H:
                centre.x = (pp[0].x + pp[2].x + pp[4].x) / 3;
                centre.y = (pp[0].y + pp[2].y + pp[4].y) / 3;
                break;
              case MT_T:
                centre.x = (pp[0].x + pp[1].x + pp[2].x) / 3;
                centre.y = (pp[0].y + pp[1].y + pp[2].y) / 3;
                break;
              case MT_P:
                centre.x = (pp[0].x + pp[2].x) / 2;
                centre.y = (pp[0].y + pp[2].y) / 2;
                break;
              default /* case MT_F */:
                centre.x = (pp[2].x + pp[4].x) / 2;
                centre.y = (pp[2].y + pp[4].y) / 2;
                break;
            }

            double lineheight = ascale * 1.5;
            double charheight = lineheight * 0.6; /* close enough */
            double allheight = lineheight * (tiles[i].ncoords-1) + charheight;

            for (j = 0; j < tiles[i].ncoords; j++) {
                const Metatile *it;
                unsigned cindex;

                printf("<text style=\""
                       "fill: black; "
                       "font-family: Sans; "
                       "font-size: %g; "
                       "text-anchor: middle; "
                       "text-align: center; "
                       "\" x=\"%g\" y=\"%g\">", lineheight,
                       centre.x,
                       centre.y - allheight/2 + charheight + lineheight*j);

                it = &tiles[i];
                cindex = j;
                while (cindex < it->ncoords) {
                    if (it != &tiles[i])
                        printf(".");
                    printf("%d", (int)it->coords[cindex].index);

                    it = it->coords[cindex].parent;
                    cindex = 0; /* BODGING AHOY */
                }

                printf("</text>\n");
            }
        }

    }

    printf("</svg>\n");
}

static void draw_metatile_set_svg(
    MetatileSet *tiles, const Point *bounds, bool coords)
{
    /*
     * Slurp the tree234 of tiles into an array for display.
     * Tedious, but this test code doesn't have to be particularly
     * efficient.
     */
    size_t nt = count234(tiles->tiles);
    Metatile *t = snewn(nt, Metatile);
    size_t i;
    for (i = 0; i < nt; i++) {
        Metatile *m = index234(tiles->tiles, i);
        t[i] = *m; /* structure copy */
    }
    draw_metatiles_svg(t, nt, bounds, coords);
    sfree(t);
}

static void draw_hats_svg(const Hat *hats, size_t n,
                          const Point *bounds, bool kites, char coordtype)
{
    size_t i, j;
    psbbox bbox = { false };

    for (i = 0; i < n; i++) {
        Point vertices[HAT_NVERT];
        size_t nv = hat_vertices(hats[i], vertices);
        for (j = 0; j < nv; j++)
            psbbox_add(&bbox, pscoords(vertices[j]));
    }

    float ascale = (coordtype == 'k' || coordtype == 'K' ? 20 : 10);
    float xscale = ascale, yscale = -ascale;
    float border = 0.2 * ascale; /* leave room for strokes at the edges */
    float ox = -xscale * bbox.bl.x + border;
    float oy = -yscale * bbox.tr.y + border;

    printf("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
           "width=\"%g\" height=\"%g\">\n",
           ceil(ox + xscale * bbox.tr.x + 2*border),
           ceil(oy + yscale * bbox.bl.y + 2*border));

    for (i = 0; i < n; i++) {
        Point vertices[HAT_NVERT];
        pspoint psvs[HAT_NVERT];
        size_t nv = hat_vertices(hats[i], vertices);
        int is = hats[i].reversed ? -1 : +1;
        int io = hats[i].reversed ? 13 : 0;

        printf("<path style=\""
               "fill: %s; "
               "stroke: black; "
               "stroke-width: %f; "
               "stroke-linejoin: round; "
               "stroke-linecap: round; "
               "\" d=\"",
               hats[i].reversed ? "rgba(0,0,0,0.2)" : "none",
               0.2 * ascale);
        for (j = 0; j < nv; j++) {
            psvs[j] = pscoords(vertices[j]);
            printf("%s %f %f\n", j ? "L" : "M",
                   ox + xscale * psvs[j].x, oy + yscale * psvs[j].y);
        }
        printf("z\" />\n");

        if (kites) {
            /*
             * Draw internal lines within each hat dividing it into
             * kites. This is done in a rather bodgy way, sorry.
             */
            const char *fmt = "<path style=\""
                "fill: none; "
                "stroke: rgba(0,0,0,0.2); "
                "stroke-width: %f; "
                "stroke-linejoin: round; "
                "stroke-linecap: round; "
                "\" d=\"M %f %f L %f %f\" />\n";
            float strokewidth = 0.1 * ascale;

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*0].x,
                   oy + yscale * psvs[io+is*0].y,
                   ox + xscale * psvs[io+is*3].x,
                   oy + yscale * psvs[io+is*3].y);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*0].x,
                   oy + yscale * psvs[io+is*0].y,
                   ox + xscale * psvs[io+is*5].x,
                   oy + yscale * psvs[io+is*5].y);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*6].x,
                   oy + yscale * psvs[io+is*6].y,
                   ox + xscale * psvs[io+is*9].x,
                   oy + yscale * psvs[io+is*9].y);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*0].x,
                   oy + yscale * psvs[io+is*0].y,
                   ox + xscale * psvs[io+is*10].x,
                   oy + yscale * psvs[io+is*10].y);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*9].x,
                   oy + yscale * psvs[io+is*9].y,
                   ox + xscale * (psvs[io+is*6].x + psvs[io+is*12].x) / 2,
                   oy + yscale * (psvs[io+is*6].y + psvs[io+is*12].y) / 2);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*5].x,
                   oy + yscale * psvs[io+is*5].y,
                   ox + xscale * (psvs[io+is*6].x + psvs[io+is*12].x) / 2,
                   oy + yscale * (psvs[io+is*6].y + psvs[io+is*12].y) / 2);

            printf(fmt, strokewidth,
                   ox + xscale * psvs[io+is*12].x,
                   oy + yscale * psvs[io+is*12].y,
                   ox + xscale * (psvs[io+is*6].x + psvs[io+is*12].x) / 2,
                   oy + yscale * (psvs[io+is*6].y + psvs[io+is*12].y) / 2);
        }

        if (coordtype == 'h') {
            double lineheight = ascale * 2;
            double charheight = lineheight * 0.6; /* close enough */

            printf("<text style=\""
                   "fill: black; "
                   "font-family: Sans; "
                   "font-size: %gpx; "
                   "text-anchor: middle; "
                   "text-align: center; "
                   "\" x=\"%g\" y=\"%g\">", lineheight,
                   ox + xscale * (psvs[io+is*0].x + psvs[io+is*10].x) / 2,
                   oy + yscale * (psvs[io+is*0].y + psvs[io+is*10].y) / 2
                   + charheight/2);
            printf("%d", (int)i);
            printf("</text>\n");
        } else if (coordtype == 'k') {
            Point kites[HAT_NKITE];
            size_t nk = hat_kite_centres(hats[i], kites);

            double lineheight = ascale * 0.5;
            double charheight = lineheight * 0.6; /* close enough */

            for (j = 0; j < nk; j++) {
                pspoint p = pscoords(kites[j]);

                printf("<text style=\""
                       "fill: black; "
                       "font-family: Sans; "
                       "font-size: %gpx; "
                       "text-anchor: middle; "
                       "text-align: center; "
                       "\" x=\"%g\" y=\"%g\">", lineheight,
                       ox + xscale * p.x,
                       oy + yscale * p.y + charheight/2);

                printf("%d.%d.%d", (int)j, hats[i].index,
                       hats[i].parent->coords[0].index);

                printf("</text>\n");
            }
        } else if (coordtype == 'K') {
            Point kites[HAT_NKITE];
            size_t nk = hat_kite_centres(hats[i], kites);

            double lineheight = ascale * 1.1;
            double charheight = lineheight * 0.6; /* close enough */

            for (j = 0; j < nk; j++) {
                pspoint p = pscoords(kites[j]);

                printf("<text style=\""
                       "fill: black; "
                       "font-family: Sans; "
                       "font-size: %gpx; "
                       "text-anchor: middle; "
                       "text-align: center; "
                       "\" x=\"%g\" y=\"%g\">", lineheight,
                       ox + xscale * p.x,
                       oy + yscale * p.y + charheight/2);
                printf("%d", (int)j);
                printf("</text>\n");
            }
        }
    }

    printf("</svg>\n");
}

typedef enum KiteStep { KS_LEFT, KS_RIGHT, KS_F_LEFT, KS_F_RIGHT } KiteStep;

static inline Point kite_step(Point k, KiteStep step)
{
    switch (step) {
      case KS_LEFT: return kite_left(k);
      case KS_RIGHT: return kite_right(k);
      case KS_F_LEFT: return kite_forward_left(k);
      default /* case KS_F_RIGHT */: return kite_forward_right(k);
    }
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        printf("usage: hat-test <mode>\n");
        printf("modes: H,T,P,F        display a single unexpanded tile\n");
        printf("       xH,xT,xP,xF    display the expansion of one tile\n");
        printf("       cH,cT,cP,cF    display expansion with tile coords\n");
        printf("       CH,CT,CP,CF    display double expansion with coords\n");
        printf("       hH,hT,hP,hF    display the hats from one tile\n");
        printf("       HH,HT,HP,HF    hats from an expansion, with coords\n");
        printf("       m1, m2, ...    nth expansion of one H metatile\n");
        printf("       M1, M2, ...    nth expansion turned into real hats\n");
        printf("       --hat          show the kites in a single hat\n");
        printf("       --tables       generate hat-tables.h for hat.c\n");
        return 0;
    }

    if (!strcmp(argv[1], "H") || !strcmp(argv[1], "T") ||
        !strcmp(argv[1], "P") || !strcmp(argv[1], "F")) {
        MetatileType type = (argv[1][0] == 'H' ? MT_H :
                             argv[1][0] == 'T' ? MT_T :
                             argv[1][0] == 'P' ? MT_P : MT_F);
        Metatile m = {type, {0, 0}, {1, 0}};
        draw_metatiles_svg(&m, 1, NULL, false);
        return 0;
    }

    if (!strcmp(argv[1], "xH") || !strcmp(argv[1], "xT") ||
        !strcmp(argv[1], "xP") || !strcmp(argv[1], "xF")) {
        MetatileType type = (argv[1][1] == 'H' ? MT_H :
                             argv[1][1] == 'T' ? MT_T :
                             argv[1][1] == 'P' ? MT_P : MT_F);
        Metatile m = {type, {0, 0}, {1, 0}};
        Metatile t[MT_MAXEXPAND];
        size_t nt = metatile_expand(m, t);
        draw_metatiles_svg(t, nt, NULL, false);
        return 0;
    }

    if (argv[1][0] && argv[1][1] &&
        strchr("cC", argv[1][0]) && strchr("HTPF", argv[1][1])) {
        MetatileType type = (argv[1][1] == 'H' ? MT_H :
                             argv[1][1] == 'T' ? MT_T :
                             argv[1][1] == 'P' ? MT_P : MT_F);
        MetatileSet *t[3];
        int nsets = (argv[1][0] == 'c' ? 2 : 3);
        int i;

        t[0] = metatile_initial_set(type);
        for (i = 1; i < nsets; i++)
            t[i] = metatile_set_expand(t[i-1]);
        draw_metatile_set_svg(t[nsets-1], NULL, true);
        for (i = 0; i < nsets; i++)
            metatile_free_set(t[i]);
        return 0;
    }

    if (!strcmp(argv[1], "hH") || !strcmp(argv[1], "hT") ||
        !strcmp(argv[1], "hP") || !strcmp(argv[1], "hF")) {
        MetatileType type = (
            !strcmp(argv[1], "hH") ? MT_H :
            !strcmp(argv[1], "hT") ? MT_T :
            !strcmp(argv[1], "hP") ? MT_P : MT_F);
        Metatile m = {type, {0, 0}, {1, 0}};
        Hat h[MT_MAXHAT];
        size_t nh = metatile_hats(&m, h);
        draw_hats_svg(h, nh, NULL, false, 'h');
        return 0;
    }

    if (!strcmp(argv[1], "--hat")) {
        Hat h = { { 0, 0 }, { 1, 0 }, false, NULL, 0 };
        draw_hats_svg(&h, 1, NULL, true, 'K');
        return 0;
    }

    if (argv[1][0] == 'H' && argv[1][1] && strchr("HTPF", argv[1][1])) {
        MetatileType type = (argv[1][1] == 'H' ? MT_H :
                             argv[1][1] == 'T' ? MT_T :
                             argv[1][1] == 'P' ? MT_P : MT_F);
        MetatileSet *t[2];
        size_t i, nh;

        t[0] = metatile_initial_set(type);
        t[1] = metatile_set_expand(t[0]);
        Hat *h = metatile_set_to_hats(t[1], &nh, NULL);
        draw_hats_svg(h, nh, NULL, true, 'k');
        sfree(h);
        for (i = 0; i < 2; i++)
            metatile_free_set(t[i]);
        return 0;
    }

    if (argv[1][0] == 'm' || argv[1][0] == 'M') {
        int niter = atoi(argv[1] + 1);
        MetatileSet *tiles = metatile_initial_set(MT_P);

        while (niter-- > 0) {
            MetatileSet *t2 = metatile_set_expand(tiles);
            metatile_free_set(tiles);
            tiles = t2;
        }

        if (argv[1][0] == 'M') {
            size_t nh;
            Hat *h = metatile_set_to_hats(tiles, &nh, NULL);
            draw_hats_svg(h, nh, tiles->vertices, false, 0);
            sfree(h);
        } else {
            draw_metatile_set_svg(tiles, tiles->vertices, false);
        }

        metatile_free_set(tiles);

        return 0;
    }

    if (!strcmp(argv[1], "--tables")) {
        size_t i, j, k;

        printf("/*\n"
               " * Header file autogenerated by auxiliary/hatgen.c\n"
               " *\n"
               " * To regenerate, run 'hatgen --tables > hat-tables.h'\n"
               " */\n\n");

        static const char HTPF[] = "HTPF";

        printf("static const unsigned hats_in_metatile[] = {");
        for (i = 0; i < 4; i++) {
            Metatile m = {i, {0, 0}, {1, 0}};
            Hat h[MT_MAXHAT];
            size_t nh = metatile_hats(&m, h);
            printf(" %zu,", nh);
        }
        printf(" };\n\n");

        {
            size_t psizes[4] = {0, 0, 0, 0};
            size_t csizes[4] = {0, 0, 0, 0};

            for (i = 0; i < 4; i++) {
                Metatile m = {i, {0, 0}, {1, 0}};
                Metatile t[MT_MAXEXPAND];
                size_t nt = metatile_expand(m, t);

                printf("static const TileType children_%c[] = {\n"
                       "   ", HTPF[i]);
                for (j = 0; j < nt; j++) {
                    MetatileType c = t[j].type;
                    psizes[c]++;
                    csizes[i]++;
                    printf(" TT_%c,", HTPF[c]);
                }
                printf("\n};\n");
            }

            printf("static const TileType *const children[] = {\n");
            for (i = 0; i < 4; i++)
                printf("    children_%c,\n", HTPF[i]);
            printf("};\n");
            printf("static const size_t nchildren[] = {\n");
            for (i = 0; i < 4; i++)
                printf("    %u,\n", (unsigned)csizes[i]);
            printf("};\n\n");
        }

        {
            for (i = 0; i < 4; i++) {
                MetatileSet *t[2];
                size_t j, k, nh, nmeta, ti;
                struct list {
                    Point kite;
                    unsigned ik, ih, im;
                } list[8*MT_MAXHAT*MT_MAXEXPAND];
                size_t len = 0;

                t[0] = metatile_initial_set(i);
                t[1] = metatile_set_expand(t[0]);
                Hat *h = metatile_set_to_hats(t[1], &nh, NULL);

                printf("static const KitemapEntry kitemap_%c[] = {\n",
                       HTPF[i]);

                Point origin = h[0].start;
                for (j = 0; j < nh; j++) {
                    Point kites[HAT_NKITE];
                    size_t nk = hat_kite_centres(h[j], kites);
                    for (k = 0; k < nk; k++) {
                        struct list *le = &list[len++];
                        le->kite.x = kites[k].x - origin.x;
                        le->kite.y = kites[k].y - origin.y;
                        le->ik = k;
                        le->ih = h[j].index;
                        le->im = h[j].parent->coords[0].index;
#if 0
                        printf("// %d,%d = %u.%u.%u\n", le->kite.x, le->kite.y, le->ik, le->ih, le->im);
#endif
                    }
                }

                nmeta = count234(t[1]->tiles);
                for (ti = 0; ti < 8 * 4 * nmeta; ti++) {
                    unsigned ik = ti % 8;
                    unsigned ih = ti / 8 % 4;
                    unsigned im = ti / (8*4);
                    struct list *src = NULL, *dst = NULL;
                    int istep;

                    for (j = 0; j < len; j++) {
                        struct list *tmp = &list[j];
                        if (tmp->ik == ik && tmp->ih == ih && tmp->im == im) {
                            src = tmp;
                            break;
                        }
                    }
                    if (ik == 0) {
                        printf("    /* hat #%u in metatile #%u (type %c)",
                               ih, im, HTPF[((Metatile *)index234(
                                                 t[1]->tiles, im))->type]);
                        if (!src)
                            printf(" does not exist");
                        printf(" */\n");
                    }
#if 0
                    if (src)
                        printf("    // src=%d,%d\n", src->kite.x, src->kite.y);
#endif
                    printf("   ");

                    for (istep = 0; istep < 4; istep++) {
                        KiteStep step = istep;
                        dst = NULL;
                        if (src) {
                            Point pdst = kite_step(src->kite, step);
#if 0
                            printf(" /* dst=%d,%d */", pdst.x, pdst.y);
#endif
                            for (k = 0; k < len; k++) {
                                struct list *tmp = &list[k];
                                if (tmp->kite.x == pdst.x &&
                                    tmp->kite.y == pdst.y) {
                                    dst = tmp;
                                    break;
                                }
                            }
                        }
                        if (!dst) {
                            printf(" {-1,-1,-1},");
                        } else {
                            printf(" {%u,%u,%u},", dst->ik, dst->ih, dst->im);
                        }
                    }
                    printf("\n");
                }
                printf("};\n");

                sfree(h);
                for (j = 0; j < 2; j++)
                    metatile_free_set(t[j]);
            }

            printf("static const KitemapEntry *const "
                   "kitemap[] = {\n");
            for (i = 0; i < 4; i++)
                printf("    kitemap_%c,\n", HTPF[i]);
            printf("};\n\n");

        }

        {
            for (i = 0; i < 4; i++) {
                MetatileSet *t[3];
                Metatile *m;
                int map[MT_MAXEXPAND * MT_MAXEXPAND];
                size_t maplen;

                for (j = 0; j < lenof(map); j++)
                    map[j] = -1;

                t[0] = metatile_initial_set(i);
                for (j = 1; j < 3; j++)
                    t[j] = metatile_set_expand(t[j-1]);

                for (j = 0; (m = index234(t[2]->tiles, j)) != NULL; j++) {
                    unsigned coords[4];
                    size_t ncoords = 0;
                    int cindex;

#if 0
                    printf("// ***\n");
#endif
                    for (cindex = 0; cindex < m->ncoords; cindex++) {
#if 0
                        printf("// %d.%d\n", (int)m->coords[cindex].index,
                               (int)m->coords[cindex].parent->coords[0].index);
#endif
                        coords[ncoords++] = (
                            m->coords[cindex].index + MT_MAXEXPAND *
                            m->coords[cindex].parent->coords[0].index);
                    }

                    unsigned prev = ncoords-1;
                    for (k = 0; k < ncoords; k++) {
                        map[coords[prev]] = coords[k];
                        prev = k;
                    }
                }

                printf("static const MetamapEntry metamap_%c[] = {\n",
                       HTPF[i]);
                maplen = MT_MAXEXPAND * count234(t[1]->tiles);
                for (j = 0; j < maplen; j++) {
                    printf("    /* %u, %u -> */ ",
                           (unsigned)(j % MT_MAXEXPAND),
                           (unsigned)(j / MT_MAXEXPAND));
                    if (map[j] == -1) {
                        printf("{-1,-1}, /* does not exist */\n");
                    } else {
                        printf("{%u, %u},",
                               (unsigned)(map[j] % MT_MAXEXPAND),
                               (unsigned)(map[j] / MT_MAXEXPAND));
                        if (map[j] == j)
                            printf(" /* no alternatives */");
                        printf("\n");
                    }
                }
                printf("};\n");

                for (j = 0; j < 3; j++)
                    metatile_free_set(t[j]);
            }

            printf("static const MetamapEntry *const "
                   "metamap[] = {\n");
            for (i = 0; i < 4; i++)
                printf("    metamap_%c,\n", HTPF[i]);
            printf("};\n");
        }

        return 0;
    }

    fprintf(stderr, "unknown test mode '%s'\n", argv[1]);
    return 1;
}
#endif
