#ifndef PUZZLES_PENROSE_H
#define PUZZLES_PENROSE_H

struct PenrosePatchParams {
    /*
     * A patch of Penrose tiling is identified by giving
     *
     *  - the coordinates of the starting triangle, using a
     *    combinatorial coordinate system
     *
     *  - which vertex of that triangle is at the centre point of the
     *    tiling
     *
     *  - the orientation of the triangle's base edge, as a number
     *    from 0 to 9, measured in tenths of a turn
     *
     * Coordinates are a sequence of letters. For a P2 tiling all
     * letters are from the set {A,B,U,V}; for P3, {C,D,X,Y}.
     */
    unsigned start_vertex;
    int orientation;
    size_t ncoords;
    char *coords;
};

#ifndef PENROSE_ENUM_DEFINED
#define PENROSE_ENUM_DEFINED
enum { PENROSE_P2, PENROSE_P3 };
#endif

bool penrose_valid_letter(char c, int which);

/*
 * Fill in PenrosePatchParams with a randomly selected set of
 * coordinates, in enough detail to generate a patch of tiling filling
 * a w x h area.
 *
 * Units of measurement: the tiling will be oriented such that
 * horizontal tile edges are possible (and therefore vertical ones are
 * not). Therefore, all x-coordinates will be rational linear
 * combinations of 1 and sqrt(5), and all y-coordinates will be
 * sin(pi/5) times such a rational linear combination. By scaling up
 * appropriately we can turn those rational combinations into
 * _integer_ combinations, so we do. Therefore, w is measured in units
 * of 1/4, and h is measured in units of sin(pi/5)/2, on a scale where
 * a length of 1 corresponds to the legs of the acute isosceles
 * triangles in the tiling (hence, the long edges of P2 kites and
 * darts, or all edges of P3 rhombs).
 *
 * (In case it's useful, the y scale factor sin(pi/5)/2 is an
 * algebraic number. Its minimal polynomial is 256x^4 - 80x^2 + 5.)
 *
 * The 'coords' field of the structure will be filled in with a new
 * dynamically allocated array. Any previous pointer in that field
 * will be overwritten.
 */
void penrose_tiling_randomise(struct PenrosePatchParams *params, int which,
                              int w, int h, random_state *rs);

/*
 * Validate a PenrosePatchParams to ensure it contains no illegal
 * coordinates. Returns NULL if it's acceptable, or an error string if
 * not.
 */
const char *penrose_tiling_params_invalid(
    const struct PenrosePatchParams *params, int which);

/*
 * Generate the actual set of Penrose tiles from a PenrosePatchParams,
 * passing each one to a callback. The callback receives the vertices
 * of each point, in the form of an array of 4*4 integers. Each vertex
 * is represented by four consecutive integers in this array, with the
 * first two giving the x coordinate and the last two the y
 * coordinate. Each pair of integers a,b represent a single coordinate
 * whose value is a + b*sqrt(5). The units of measurement for x and y
 * are as described above.
 */
typedef void (*penrose_tile_callback_fn)(void *ctx, const int *coords);

#define PENROSE_NVERTICES 4

void penrose_tiling_generate(
    const struct PenrosePatchParams *params, int w, int h,
    penrose_tile_callback_fn cb, void *cbctx);

#endif
