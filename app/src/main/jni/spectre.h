#ifndef PUZZLES_SPECTRE_H
#define PUZZLES_SPECTRE_H

struct SpectrePatchParams {
    /*
     * A patch of Spectre tiling is identified by giving
     *
     *  - the coordinates of the central Spectre, using a
     *    combinatorial coordinate system similar to the Hat tiling in
     *    hat.h
     *
     *  - the orientation of that Spectre, as a number from 0 to 11 (a
     *    multiple of 30 degrees), with 0 representing the 'head' of
     *    the Spectre facing upwards, and numbers counting
     *    anticlockwise.
     *
     * Coordinates are a sequence of small non-negative integers. The
     * valid range for each coordinate depends on the next coordinate,
     * or on final_hex if it's the last one in the list. The largest
     * valid range is {0,...,7}.
     *
     * 'final_hex' is one of the letters GDJLXPSFY.
     * spectre_valid_hex_letter() will check that.
     */
    int orientation;
    size_t ncoords;
    unsigned char *coords;
    char final_hex;
};

bool spectre_valid_hex_letter(char c);

/*
 * Fill in SpectrePatchParams with a randomly selected set of
 * coordinates, in enough detail to generate a patch of tiling filling
 * a w x h area. The unit of measurement is 1/(2*sqrt(2)) of a Spectre
 * edge, i.e. such that a single Spectre edge at 45 degrees would
 * correspond to the vector (2,2).
 *
 * The 'coords' field of the structure will be filled in with a new
 * dynamically allocated array. Any previous pointer in that field
 * will be overwritten.
 */
void spectre_tiling_randomise(struct SpectrePatchParams *params, int w, int h,
                              random_state *rs);

/*
 * Validate a SpectrePatchParams to ensure it contains no illegal
 * coordinates. Returns NULL if it's acceptable, or an error string if
 * not.
 */
const char *spectre_tiling_params_invalid(
    const struct SpectrePatchParams *params);

/*
 * Generate the actual set of Spectre tiles from a SpectrePatchParams,
 * passing each one to a callback. The callback receives the vertices
 * of each point, in the form of an array of 4*14 integers. Each
 * vertex is represented by four consecutive integers in this array,
 * with the first two giving the x coordinate and the last two the y
 * coordinate. Each pair of integers a,b represent a single coordinate
 * whose value is a + b*sqrt(3). The unit of measurement is as above.
 */
typedef void (*spectre_tile_callback_fn)(void *ctx, const int *coords);

#define SPECTRE_NVERTICES 14

void spectre_tiling_generate(
    const struct SpectrePatchParams *params, int w, int h,
    spectre_tile_callback_fn cb, void *cbctx);

#endif
