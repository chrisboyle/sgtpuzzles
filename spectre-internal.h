#include "spectre.h"

/*
 * List macro of the names for hexagon types, which will be reused all
 * over the place.
 *
 * (I have to call the parameter to this list macro something other
 * than X, because here, X is also one of the macro arguments!)
 */
#define HEX_LETTERS(Z) Z(G) Z(D) Z(J) Z(L) Z(X) Z(P) Z(S) Z(F) Z(Y)

typedef enum Hex {
    #define HEX_ENUM_DECL(x) HEX_##x,
    HEX_LETTERS(HEX_ENUM_DECL)
    #undef HEX_ENUM_DECL
} Hex;

static inline unsigned num_subhexes(Hex h)
{
    return h == HEX_G ? 7 : 8;
}

static inline unsigned num_spectres(Hex h)
{
    return h == HEX_G ? 2 : 1;
}

/*
 * Data types used in the lookup tables.
 */
struct MapEntry {
    bool internal;
    unsigned char hi, lo;
};
struct MapEdge {
    unsigned char startindex, len;
};
struct Possibility {
    unsigned char hi, lo;
    unsigned long prob;
};

/*
 * Coordinate system for tracking Spectres and their hexagonal
 * metatiles.
 *
 * SpectreCoords will store the index of a single Spectre within a
 * smallest-size hexagon, plus an array of HexCoord each indexing a
 * hexagon within the expansion of a larger hexagon.
 *
 * The last coordinate stored, sc->c[sc->nc-1], will have a hex type
 * but no index (represented by index==-1). This means "we haven't
 * decided yet what this level of metatile needs to be". If we need to
 * refer to this level during the hatctx_step algorithm, we make it up
 * at random, based on a table of what metatiles each type can
 * possibly be part of, at what index.
 */
typedef struct HexCoord {
    int index; /* index within that tile, or -1 if not yet known */
    Hex type;  /* type of this hexagon */
} HexCoord;

typedef struct SpectreCoords {
    int index;       /* index of Spectre within the order-0 hexagon */
    HexCoord *c;
    size_t nc, csize;

    /* Used by spectre-test to four-colour output tilings, and
     * maintained unconditionally because it's easier than making it
     * conditional */
    unsigned char hex_colour, prev_hex_colour, incoming_hex_edge;
} SpectreCoords;

SpectreCoords *spectre_coords_new(void);
void spectre_coords_free(SpectreCoords *hc);
void spectre_coords_make_space(SpectreCoords *hc, size_t size);
SpectreCoords *spectre_coords_copy(SpectreCoords *hc_in);

/*
 * Coordinate system for locating Spectres in the plane.
 *
 * The 'Point' structure represents a single point by means of an
 * integer linear combination of {1, d, d^2, d^3}, where d is the
 * complex number exp(i pi/6) representing 1/12 of a turn about the
 * origin.
 *
 * The 'Spectre' structure represents an entire Spectre in a tiling,
 * giving both the locations of all of its vertices and its
 * combinatorial coordinates. It also contains a linked-list pointer,
 * used during breadth-first search to generate all the Spectres in an
 * area.
 */
typedef struct Point {
    int coeffs[4];
} Point;
typedef struct Spectre Spectre;
struct Spectre {
    Point vertices[14];
    SpectreCoords *sc;
    Spectre *next; /* used in breadth-first search */
};

/* Fill in all the coordinates of a Spectre starting from any single edge */
void spectre_place(Spectre *spec, Point u, Point v, int index_of_u);

/* Free a Spectre and its contained coordinates */
void spectre_free(Spectre *spec);

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
static inline Point point_mul_by_d(Point x)
{
    Point r;
    /* Multiply by d by using the identity d^4 - d^2 + 1 = 0, so d^4 = d^2+1 */
    r.coeffs[0] = -x.coeffs[3];
    r.coeffs[1] = x.coeffs[0];
    r.coeffs[2] = x.coeffs[1] + x.coeffs[3];
    r.coeffs[3] = x.coeffs[2];
    return r;
}
static inline Point point_mul(Point a, Point b)
{
    size_t i, j;
    Point r;

    /* Initialise r to be a, scaled by b's d^3 term */
    for (j = 0; j < 4; j++)
        r.coeffs[j] = a.coeffs[j] * b.coeffs[3];

    /* Now iterate r = d*r + (next coefficient down), by Horner's rule */
    for (i = 3; i-- > 0 ;) {
        r = point_mul_by_d(r);
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
 * origin, i.e. a rotation by 30*s degrees or s*pi/6 radians.
 */
static inline Point point_rot(int s)
{
    Point r = {{ 1, 0, 0, 0 }};
    Point dpower = {{ 0, 1, 0, 0 }};

    /* Reduce to a sensible range */
    s = s % 12;
    if (s < 0)
        s += 12;

    while (true) {
        if (s & 1)
            r = point_mul(r, dpower);
        s >>= 1;
        if (!s)
            break;
        dpower = point_mul(dpower, dpower);
    }

    return r;
}

/*
 * SpectreContext is the shared context of a whole run of the
 * algorithm. Its 'prototype' SpectreCoords object represents the
 * coordinates of the starting Spectre, and is extended as necessary;
 * any other SpectreCoord that needs extending will copy the
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
 * For a normal (non-testing) caller, spectrectx_generate() is the
 * main useful function. It breadth-first searches a whole area to
 * generate all the Spectres in it, starting from a (typically
 * central) one with the coordinates of ctx->prototype. The callback
 * function processes each Spectre as it's generated, and returns true
 * or false to indicate whether that Spectre is within the bounds of
 * the target area (and therefore the search should continue exploring
 * its neighbours).
 */
typedef struct SpectreContext {
    random_state *rs;
    bool must_free_rs;
    Point start_vertices[2]; /* vertices 0,1 of the starting Spectre */
    int orientation;         /* orientation to put in SpectrePatchParams */
    SpectreCoords *prototype;
} SpectreContext;

void spectrectx_init_random(SpectreContext *ctx, random_state *rs);
void spectrectx_init_from_params(
    SpectreContext *ctx, const struct SpectrePatchParams *ps);
void spectrectx_cleanup(SpectreContext *ctx);
SpectreCoords *spectrectx_initial_coords(SpectreContext *ctx);
void spectrectx_extend_coords(SpectreContext *ctx, SpectreCoords *hc,
                              size_t n);
void spectrectx_step(SpectreContext *ctx, SpectreCoords *sc,
                     unsigned edge, unsigned *outedge);
void spectrectx_generate(SpectreContext *ctx,
                         bool (*callback)(void *cbctx, const Spectre *spec),
                         void *cbctx);

/* For spectre-test to directly generate a tiling of hexes */
void spectrectx_step_hex(SpectreContext *ctx, SpectreCoords *sc,
                         size_t depth, unsigned edge, unsigned *outedge);

/* Subroutines that step around the tiling specified by a SpectreCtx,
 * delivering both plane and combinatorial coordinates as they go */
Spectre *spectre_initial(SpectreContext *ctx);
Spectre *spectre_adjacent(SpectreContext *ctx, const Spectre *src_spec,
                          unsigned src_edge, unsigned *dst_edge);

/* For extracting the point coordinates */
typedef struct Coord {
    int c1, cr3;      /* coefficients of 1 and sqrt(3) respectively */
} Coord;

static inline Coord point_x(Point p)
{
    Coord x = { 2 * p.coeffs[0] + p.coeffs[2], p.coeffs[1] };
    return x;
}

static inline Coord point_y(Point p)
{
    Coord y = { 2 * p.coeffs[3] + p.coeffs[1], p.coeffs[2] };
    return y;
}

static inline int coord_sign(Coord x)
{
    if (x.c1 == 0 && x.cr3 == 0)
        return 0;
    if (x.c1 >= 0 && x.cr3 >= 0)
        return +1;
    if (x.c1 <= 0 && x.cr3 <= 0)
        return -1;

    if (x.c1 * x.c1 > 3 * x.cr3 * x.cr3)
        return x.c1 < 0 ? -1 : +1;
    else
        return x.cr3 < 0 ? -1 : +1;
}

static inline Coord coord_construct(int c1, int cr3)
{
    Coord c = { c1, cr3 };
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
    sum.cr3 = a.cr3 + b.cr3;
    return sum;
}

static inline Coord coord_sub(Coord a, Coord b)
{
    Coord diff;
    diff.c1 = a.c1 - b.c1;
    diff.cr3 = a.cr3 - b.cr3;
    return diff;
}

static inline Coord coord_mul(Coord a, Coord b)
{
    Coord prod;
    prod.c1 = a.c1 * b.c1 + 3 * a.cr3 * b.cr3;
    prod.cr3 = a.c1 * b.cr3 + a.cr3 * b.c1;
    return prod;
}

static inline Coord coord_abs(Coord a)
{
    int sign = coord_sign(a);
    Coord abs;
    abs.c1 = a.c1 * sign;
    abs.cr3 = a.cr3 * sign;
    return abs;
}

static inline int coord_cmp(Coord a, Coord b)
{
    return coord_sign(coord_sub(a, b));
}
