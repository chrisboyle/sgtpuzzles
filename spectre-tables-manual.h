/*
 * Handwritten data tables for the Spectre tiling.
 *
 * This file is used by both the final tiling generator in spectre.c,
 * and by spectre-gen.c which auto-generates further tables to go with
 * these.
 */

/*
 * We generate the Spectre tiling based on the substitution system of
 * 9 types of marked hexagon shown in the paper.
 *
 * The substitution expands each hexagon into 8 others, except for the
 * G hex which expands to only seven. The layout, numbered with the
 * indices we use in the arrays here, is as follows:
 *
 *     0 1
 *    2 3
 *   4 5 6
 *      7
 *
 * That is: the hexes are oriented with a pair of vertical edges.
 * Hexes 0 and 1 are horizontally adjacent; 2 and 3 are adjacent on
 * the next row, with 3 nestling between 0 and 1; 4,5,6 are on the
 * third row with 5 between 2 and 3; and 7 is by itself on a fourth
 * row, between 5 and 6. In the expansion of the G hex, #7 is the
 * missing one, so its indices are still consecutive from 0.
 *
 * These arrays list the type of each child hex. The hexes also have
 * orientations, but those aren't listed here, because only
 * spectre-gen needs to know them - by the time it's finished
 * autogenerating transition tables, the orientations are baked into
 * those and don't need to be consulted separately.
 */

static const Hex subhexes_G[] = {
    HEX_F,
    HEX_X,
    HEX_G,
    HEX_S,
    HEX_P,
    HEX_D,
    HEX_J,
    /* hex #7 is not present for this tile */
};
static const Hex subhexes_D[] = {
    HEX_F,
    HEX_P,
    HEX_G,
    HEX_S,
    HEX_X,
    HEX_D,
    HEX_F,
    HEX_X,
};
static const Hex subhexes_J[] = {
    HEX_F,
    HEX_P,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_P,
};
static const Hex subhexes_L[] = {
    HEX_F,
    HEX_P,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_X,
};
static const Hex subhexes_X[] = {
    HEX_F,
    HEX_Y,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_P,
};
static const Hex subhexes_P[] = {
    HEX_F,
    HEX_Y,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_X,
};
static const Hex subhexes_S[] = {
    HEX_L,
    HEX_P,
    HEX_G,
    HEX_S,
    HEX_X,
    HEX_D,
    HEX_F,
    HEX_X,
};
static const Hex subhexes_F[] = {
    HEX_F,
    HEX_P,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_Y,
};
static const Hex subhexes_Y[] = {
    HEX_F,
    HEX_Y,
    HEX_G,
    HEX_S,
    HEX_Y,
    HEX_D,
    HEX_F,
    HEX_Y,
};

/*
 * Shape of the Spectre itself.
 *
 * Vertex 0 is taken to be at the top of the Spectre's "head"; vertex
 * 1 is the adjacent vertex, in the direction of the shorter edge of
 * its "cloak".
 *
 * This array indicates how far to turn at each vertex, in 1/12 turns.
 * All edges are the same length (counting the double-edge as two
 * edges, which we do).
 */
static const int spectre_angles[14] = {
    -3, -2, 3, -2, -3, 2, -3, 2, -3, -2, 0, -2, 3, -2,
};

/*
 * The relative probabilities of the nine hex types, in the limit, as
 * the expansion process is iterated more and more times. Used to
 * choose the initial hex coordinates as if the segment of tiling came
 * from the limiting distribution across the whole plane.
 *
 * This is obtained by finding the matrix that says how many hexes of
 * each type are expanded from each starting hex, and taking the
 * eigenvector that goes with its limiting eigenvalue.
 */
#define PROB_G 10000000                /* 1 */
#define PROB_D 10000000                /* 1 */
#define PROB_J  1270167                /* 4 - sqrt(15) */
#define PROB_L  1270167                /* 4 - sqrt(15) */
#define PROB_X  7459667                /* 2 sqrt(15) - 7 */
#define PROB_P  7459667                /* 2 sqrt(15) - 7 */
#define PROB_S 10000000                /* 1 */
#define PROB_F 17459667                /* 2 sqrt(15) - 6 */
#define PROB_Y 13810500                /* 13 - 3 sqrt(15) */
