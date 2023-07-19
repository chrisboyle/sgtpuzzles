#include "penrose.h"

static inline unsigned num_subtriangles(char t)
{
    return (t == 'A' || t == 'B' || t == 'X' || t == 'Y') ? 3 : 2;
}

static inline unsigned sibling_edge(char t)
{
    switch (t) {
      case 'A': case 'U': return 2;
      case 'B': case 'V': return 1;
      default: return 0;
    }
}

/*
 * Coordinate system for tracking Penrose-tile half-triangles.
 * PenroseCoords simply stores an array of triangle types.
 */
typedef struct PenroseCoords {
    char *c;
    size_t nc, csize;
} PenroseCoords;

PenroseCoords *penrose_coords_new(void);
void penrose_coords_free(PenroseCoords *pc);
void penrose_coords_make_space(PenroseCoords *pc, size_t size);
PenroseCoords *penrose_coords_copy(PenroseCoords *pc_in);

/*
 * Coordinate system for locating Penrose tiles in the plane.
 *
 * The 'Point' structure represents a single point by means of an
 * integer linear combination of {1, t, t^2, t^3}, where t is the
 * complex number exp(i pi/5) representing 1/10 of a turn about the
 * origin.
 *
 * The 'PenroseTriangle' structure represents a half-tile triangle,
 * giving both the locations of its vertices and its combinatorial
 * coordinates. It also contains a linked-list pointer and a boolean
 * flag, used during breadth-first search to generate all the tiles in
 * an area and report them exactly once.
 */
typedef struct Point {
    int coeffs[4];
} Point;
typedef struct PenroseTriangle PenroseTriangle;
struct PenroseTriangle {
    Point vertices[3];
    PenroseCoords *pc;
    PenroseTriangle *next; /* used in breadth-first search */
    bool reported;
};

/* Fill in all the coordinates of a triangle starting from any single edge.
 * Requires tri->pc to have been filled in, so that we know which shape of
 * triangle we're placing. */
void penrose_place(PenroseTriangle *tri, Point u, Point v, int index_of_u);

/* Free a PenroseHalf and its contained coordinates, or a whole PenroseTile */
void penrose_free(PenroseTriangle *tri);

/*
 * A Point is really a complex number, so we can add, subtract and
 * multiply them.
 */
static inline Point point_add(Point a, Point b)
{
    Point r;
    size_t i;
    for (i = 0; i < 4; i++)
        r.coeffs[i] = a.coeffs[i] + b.coeffs[i];
    return r;
}
static inline Point point_sub(Point a, Point b)
{
    Point r;
    size_t i;
    for (i = 0; i < 4; i++)
        r.coeffs[i] = a.coeffs[i] - b.coeffs[i];
    return r;
}
static inline Point point_mul_by_t(Point x)
{
    Point r;
    /* Multiply by t by using the identity t^4 - t^3 + t^2 - t + 1 = 0,
     * so t^4 = t^3 - t^2 + t - 1 */
    r.coeffs[0] = -x.coeffs[3];
    r.coeffs[1] = x.coeffs[0] + x.coeffs[3];
    r.coeffs[2] = x.coeffs[1] - x.coeffs[3];
    r.coeffs[3] = x.coeffs[2] + x.coeffs[3];
    return r;
}
static inline Point point_mul(Point a, Point b)
{
    size_t i, j;
    Point r;

    /* Initialise r to be a, scaled by b's t^3 term */
    for (j = 0; j < 4; j++)
        r.coeffs[j] = a.coeffs[j] * b.coeffs[3];

    /* Now iterate r = t*r + (next coefficient down), by Horner's rule */
    for (i = 3; i-- > 0 ;) {
        r = point_mul_by_t(r);
        for (j = 0; j < 4; j++)
            r.coeffs[j] += a.coeffs[j] * b.coeffs[i];
    }

    return r;
}
static inline bool point_equal(Point a, Point b)
{
    size_t i;
    for (i = 0; i < 4; i++)
        if (a.coeffs[i] != b.coeffs[i])
            return false;
    return true;
}

/*
 * Return the Point corresponding to a rotation of s steps around the
 * origin, i.e. a rotation by 36*s degrees or s*pi/5 radians.
 */
static inline Point point_rot(int s)
{
    Point r = {{ 1, 0, 0, 0 }};
    Point tpower = {{ 0, 1, 0, 0 }};

    /* Reduce to a sensible range */
    s = s % 10;
    if (s < 0)
        s += 10;

    while (true) {
        if (s & 1)
            r = point_mul(r, tpower);
        s >>= 1;
        if (!s)
            break;
        tpower = point_mul(tpower, tpower);
    }

    return r;
}

/*
 * PenroseContext is the shared context of a whole run of the
 * algorithm. Its 'prototype' PenroseCoords object represents the
 * coordinates of the starting triangle, and is extended as necessary;
 * any other PenroseCoord that needs extending will copy the
 * higher-order values from ctx->prototype as needed, so that once
 * each choice has been made, it remains consistent.
 *
 * When we're inventing a random piece of tiling in the first place,
 * we append to ctx->prototype by choosing a random (but legal)
 * higher-level metatile for the current topmost one to turn out to be
 * part of. When we're replaying a generation whose parameters are
 * already stored, we don't have a random_state, and we make fixed
 * decisions if not enough coordinates were provided, as in the
 * corresponding hat.c system.
 *
 * For a normal (non-testing) caller, penrosectx_generate() is the
 * main useful function. It breadth-first searches a whole area to
 * generate all the triangles in it, starting from a (typically
 * central) one with the coordinates of ctx->prototype. It takes two
 * callback function: one that checks whether a triangle is within the
 * bounds of the target area (and therefore the search should continue
 * exploring its neighbours), and another that reports a full Penrose
 * tile once both of its halves have been found and determined to be
 * in bounds.
 */
typedef struct PenroseContext {
    random_state *rs;
    bool must_free_rs;
    unsigned start_vertex; /* which vertex of 'prototype' is at the origin? */
    int orientation;    /* orientation to put in PenrosePatchParams */
    PenroseCoords *prototype;
} PenroseContext;

void penrosectx_init_random(PenroseContext *ctx, random_state *rs, int which);
void penrosectx_init_from_params(
    PenroseContext *ctx, const struct PenrosePatchParams *ps);
void penrosectx_cleanup(PenroseContext *ctx);
PenroseCoords *penrosectx_initial_coords(PenroseContext *ctx);
void penrosectx_extend_coords(PenroseContext *ctx, PenroseCoords *pc,
                              size_t n);
void penrosectx_step(PenroseContext *ctx, PenroseCoords *pc,
                     unsigned edge, unsigned *outedge);
void penrosectx_generate(
    PenroseContext *ctx,
    bool (*inbounds)(void *inboundsctx,
                     const PenroseTriangle *tri), void *inboundsctx,
    void (*tile)(void *tilectx, const Point *vertices), void *tilectx);

/* Subroutines that step around the tiling specified by a PenroseCtx,
 * delivering both plane and combinatorial coordinates as they go */
PenroseTriangle *penrose_initial(PenroseContext *ctx);
PenroseTriangle *penrose_adjacent(PenroseContext *ctx,
                                  const PenroseTriangle *src_spec,
                                  unsigned src_edge, unsigned *dst_edge);

/* For extracting the point coordinates */
typedef struct Coord {
    int c1, cr5;      /* coefficients of 1 and sqrt(5) respectively */
} Coord;

static inline Coord point_x(Point p)
{
    Coord x = {
        4 * p.coeffs[0] + p.coeffs[1] - p.coeffs[2] + p.coeffs[3],
        p.coeffs[1] + p.coeffs[2] - p.coeffs[3],
    };
    return x;
}

static inline Coord point_y(Point p)
{
    Coord y = {
        2 * p.coeffs[1] + p.coeffs[2] + p.coeffs[3],
        p.coeffs[2] + p.coeffs[3],
    };
    return y;
}

static inline int coord_sign(Coord x)
{
    if (x.c1 == 0 && x.cr5 == 0)
        return 0;
    if (x.c1 >= 0 && x.cr5 >= 0)
        return +1;
    if (x.c1 <= 0 && x.cr5 <= 0)
        return -1;

    if (x.c1 * x.c1 > 5 * x.cr5 * x.cr5)
        return x.c1 < 0 ? -1 : +1;
    else
        return x.cr5 < 0 ? -1 : +1;
}

static inline Coord coord_construct(int c1, int cr5)
{
    Coord c = { c1, cr5 };
    return c;
}

static inline Coord coord_integer(int c1)
{
    return coord_construct(c1, 0);
}

static inline Coord coord_add(Coord a, Coord b)
{
    Coord sum;
    sum.c1 = a.c1 + b.c1;
    sum.cr5 = a.cr5 + b.cr5;
    return sum;
}

static inline Coord coord_sub(Coord a, Coord b)
{
    Coord diff;
    diff.c1 = a.c1 - b.c1;
    diff.cr5 = a.cr5 - b.cr5;
    return diff;
}

static inline Coord coord_mul(Coord a, Coord b)
{
    Coord prod;
    prod.c1 = a.c1 * b.c1 + 5 * a.cr5 * b.cr5;
    prod.cr5 = a.c1 * b.cr5 + a.cr5 * b.c1;
    return prod;
}

static inline Coord coord_abs(Coord a)
{
    int sign = coord_sign(a);
    Coord abs;
    abs.c1 = a.c1 * sign;
    abs.cr5 = a.cr5 * sign;
    return abs;
}

static inline int coord_cmp(Coord a, Coord b)
{
    return coord_sign(coord_sub(a, b));
}
