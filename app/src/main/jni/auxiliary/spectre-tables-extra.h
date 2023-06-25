/*
 * Further data tables used to generate the final transition maps.
 */

/*
 * Locations in the plane of the centres of the 8 hexagons in the
 * expansion of each hex.
 *
 * We take the centre-to-centre distance to be 6 units, so that other
 * locations in the hex tiling (e.g. edge midpoints and vertices) will
 * still have integer coefficients.
 *
 * These locations are represented using the same Point type used for
 * the whole tiling, but all our angles are 60 degrees, so we don't
 * ever need the coefficients of d or d^3, only of 1 and d^2.
 */
static const Point hex_centres[] = {
    {{0, 0, 0, 0}}, {{6, 0, 0, 0}},                        /*   0 1 */
    {{0, 0, -6, 0}}, {{6, 0, -6, 0}},                      /*  2 3  */
    {{0, 0, -12, 0}}, {{6, 0, -12, 0}}, {{12, 0, -12, 0}}, /* 4 5 6 */
    {{12, 0, -18, 0}},                                     /*    7  */
};

/*
 * Orientations of all the sub-hexes in the expansion of each hex.
 * Measured anticlockwise (that is, as a power of s) from 0, where 0
 * means the hex is upright, with its own vertex #0 at the top.
 */

static const unsigned orientations_G[] = {
    2, /* HEX_F */
    1, /* HEX_X */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_P */
    5, /* HEX_D */
    0, /* HEX_J */
    /* hex #7 is not present for this tile */
};
static const unsigned orientations_D[] = {
    2, /* HEX_F */
    1, /* HEX_P */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_X */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_X */
};
static const unsigned orientations_J[] = {
    2, /* HEX_F */
    1, /* HEX_P */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_P */
};
static const unsigned orientations_L[] = {
    2, /* HEX_F */
    1, /* HEX_P */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_X */
};
static const unsigned orientations_X[] = {
    2, /* HEX_F */
    1, /* HEX_Y */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_P */
};
static const unsigned orientations_P[] = {
    2, /* HEX_F */
    1, /* HEX_Y */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_X */
};
static const unsigned orientations_S[] = {
    2, /* HEX_L */
    1, /* HEX_P */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_X */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_X */
};
static const unsigned orientations_F[] = {
    2, /* HEX_F */
    1, /* HEX_P */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_Y */
};
static const unsigned orientations_Y[] = {
    2, /* HEX_F */
    1, /* HEX_Y */
    0, /* HEX_G */
    1, /* HEX_S */
    4, /* HEX_Y */
    5, /* HEX_D */
    0, /* HEX_F */
    5, /* HEX_Y */
};

/*
 * For each hex type, indicate the point on the boundary of the
 * expansion that corresponds to vertex 0 of the superhex. Also,
 * indicate the initial direction we head in to go round the edge.
 */
#define HEX_OUTLINE_START_COMMON {{ -4, 0, -10, 0 }}, {{ +2, 0, +2, 0 }}
#define HEX_OUTLINE_START_RARE {{ -2, 0, -14, 0 }}, {{ -2, 0, +4, 0 }}
#define HEX_OUTLINE_START_G HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_D HEX_OUTLINE_START_RARE
#define HEX_OUTLINE_START_J HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_L HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_X HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_P HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_S HEX_OUTLINE_START_RARE
#define HEX_OUTLINE_START_F HEX_OUTLINE_START_COMMON
#define HEX_OUTLINE_START_Y HEX_OUTLINE_START_COMMON

/*
 * Similarly, for each hex type, indicate the point on the boundary of
 * its Spectre expansion that corresponds to hex vertex 0.
 *
 * This time, it's easiest just to indicate which vertex of which
 * sub-Spectre we take in each case, because the Spectre outlines
 * don't take predictable turns between the edge expansions, so the
 * routine consuming this data will have to look things up in its
 * edgemap anyway.
 */
#define SPEC_OUTLINE_START_COMMON 0, 9
#define SPEC_OUTLINE_START_RARE 0, 8
#define SPEC_OUTLINE_START_G SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_D SPEC_OUTLINE_START_RARE
#define SPEC_OUTLINE_START_J SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_L SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_X SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_P SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_S SPEC_OUTLINE_START_RARE
#define SPEC_OUTLINE_START_F SPEC_OUTLINE_START_COMMON
#define SPEC_OUTLINE_START_Y SPEC_OUTLINE_START_COMMON

/*
 * The paper also defines a set of 8 different classes of edges for
 * the hexagons. (You can imagine these as different shapes of
 * jigsaw-piece tab, constraining how the hexes can fit together). So
 * for each hex, we need a list of its edge types.
 *
 * Most edge types come in two matching pairs, which the paper labels
 * with the same lowercase Greek letter and a + or - superscript, e.g.
 * alpha^+ and alpha^-. The usual rule is that when two edges meet,
 * they have to be the + and - versions of the same letter. The
 * exception to this rule is the 'eta' edge, which has no sign: it's
 * symmetric, so any two eta edges can validly meet.
 *
 * We express this here by defining an enumeration in which eta = 0
 * and all other edge types have positive values, so that integer
 * negation can be used to indicate the other edge that fits with this
 * one (and for eta, it doesn't change the value).
 */
enum Edge {
    edge_eta = 0,
    edge_alpha,
    edge_beta,
    edge_gamma,
    edge_delta,
    edge_epsilon,
    edge_zeta,
    edge_theta,
};

/*
 * Edge types for each hex are specified anticlockwise, starting from
 * the top vertex, so that edge #0 is the top-left diagonal edge, edge
 * #1 the left-hand vertical edge, etc.
 */
static const int edges_G[6] = {
    -edge_beta, -edge_alpha, +edge_alpha,
    -edge_gamma, -edge_delta, +edge_beta,
};
static const int edges_D[6] = {
    -edge_zeta, +edge_gamma, +edge_beta,
    -edge_epsilon, +edge_alpha, -edge_gamma,
};
static const int edges_J[6] = {
    -edge_beta, +edge_gamma, +edge_beta,
    +edge_theta, +edge_beta, edge_eta,
};
static const int edges_L[6] = {
    -edge_beta, +edge_gamma, +edge_beta,
    -edge_epsilon, +edge_alpha, -edge_theta,
};
static const int edges_X[6] = {
    -edge_beta, -edge_alpha, +edge_epsilon,
    +edge_theta, +edge_beta, edge_eta,
};
static const int edges_P[6] = {
    -edge_beta, -edge_alpha, +edge_epsilon,
    -edge_epsilon, +edge_alpha, -edge_theta,
};
static const int edges_S[6] = {
    +edge_delta, +edge_zeta, +edge_beta,
    -edge_epsilon, +edge_alpha, -edge_gamma,
};
static const int edges_F[6] = {
    -edge_beta, +edge_gamma, +edge_beta,
    -edge_epsilon, +edge_epsilon, edge_eta,
};
static const int edges_Y[6] = {
    -edge_beta, -edge_alpha, +edge_epsilon,
    -edge_epsilon, +edge_epsilon, edge_eta,
};

/*
 * Now specify the actual shape of each edge type, in terms of the
 * angles of turns as you traverse the edge.
 *
 * Edges around the outline of a hex expansion are traversed
 * _clockwise_, because each expansion step flips the handedness of
 * the whole system.
 *
 * Each array has one fewer element than the number of sub-edges in
 * the edge shape (for the usual reason - n edges in a path have only
 * n-1 vertices separating them).
 *
 * These arrays show the positive version of each edge type. The
 * negative version is obtained by reversing the order of the turns
 * and also the sign of each turn.
 */
static const int hex_edge_shape_eta[] = { +2, +2, -2, -2 };
static const int hex_edge_shape_alpha[] = { +2, -2 };
static const int hex_edge_shape_beta[] = { -2 };
static const int hex_edge_shape_gamma[] = { +2, -2, -2, +2 };
static const int hex_edge_shape_delta[] = { -2, +2, -2, +2 };
static const int hex_edge_shape_epsilon[] = { +2, -2, -2 };
static const int hex_edge_shape_zeta[] = { -2, +2 };
static const int hex_edge_shape_theta[] = { +2, +2, -2, -2, +2 };

static const int *const hex_edge_shapes[] = {
    hex_edge_shape_eta,
    hex_edge_shape_alpha,
    hex_edge_shape_beta,
    hex_edge_shape_gamma,
    hex_edge_shape_delta,
    hex_edge_shape_epsilon,
    hex_edge_shape_zeta,
    hex_edge_shape_theta,
};
static const size_t hex_edge_lengths[] = {
    lenof(hex_edge_shape_eta) + 1,
    lenof(hex_edge_shape_alpha) + 1,
    lenof(hex_edge_shape_beta) + 1,
    lenof(hex_edge_shape_gamma) + 1,
    lenof(hex_edge_shape_delta) + 1,
    lenof(hex_edge_shape_epsilon) + 1,
    lenof(hex_edge_shape_zeta) + 1,
    lenof(hex_edge_shape_theta) + 1,
};

static const int spec_edge_shape_eta[] = { 0 };
static const int spec_edge_shape_alpha[] = { -2, +3 };
static const int spec_edge_shape_beta[] = { +3, -2 };
static const int spec_edge_shape_gamma[] = { +2 };
static const int spec_edge_shape_delta[] = { +2, +3, +2, -3, +2 };
static const int spec_edge_shape_epsilon[] = { +3 };
static const int spec_edge_shape_zeta[] = { -2 };
/* In expansion to Spectres, a theta edge corresponds to just one
 * Spectre edge, so its turns array would be completely empty! */

static const int *const spec_edge_shapes[] = {
    spec_edge_shape_eta,
    spec_edge_shape_alpha,
    spec_edge_shape_beta,
    spec_edge_shape_gamma,
    spec_edge_shape_delta,
    spec_edge_shape_epsilon,
    spec_edge_shape_zeta,
    NULL, /* theta has no turns */
};
static const size_t spec_edge_lengths[] = {
    lenof(spec_edge_shape_eta) + 1,
    lenof(spec_edge_shape_alpha) + 1,
    lenof(spec_edge_shape_beta) + 1,
    lenof(spec_edge_shape_gamma) + 1,
    lenof(spec_edge_shape_delta) + 1,
    lenof(spec_edge_shape_epsilon) + 1,
    lenof(spec_edge_shape_zeta) + 1,
    1, /* theta is only one edge long */
};

/*
 * Each edge type corresponds to a fixed number of edges of the
 * hexagon layout in the expansion of each hex, and also to a fixed
 * number of edges of the Spectre(s) that each hex expands to in the
 * final step.
 */
static const int edgelen_hex[] = {
    5, /* edge_eta */
    3, /* edge_alpha */
    2, /* edge_beta */
    5, /* edge_gamma */
    5, /* edge_delta */
    4, /* edge_epsilon */
    3, /* edge_zeta */
    6, /* edge_theta */
};

static const int edgelen_spectre[] = {
    2, /* edge_eta */
    3, /* edge_alpha */
    3, /* edge_beta */
    2, /* edge_gamma */
    6, /* edge_delta */
    2, /* edge_epsilon */
    2, /* edge_zeta */
    1, /* edge_theta */
};
