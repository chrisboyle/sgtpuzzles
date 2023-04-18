#ifndef PUZZLES_HAT_H
#define PUZZLES_HAT_H

struct HatPatchParams {
    /*
     * A patch of hat tiling is identified by giving the coordinates
     * of the kite in one corner, using a multi-level coordinate
     * system based on metatile expansions. Coordinates are a sequence
     * of small non-negative integers. The valid range for each
     * coordinate depends on the next coordinate, or on final_metatile
     * if it's the last one in the list. The largest valid range is
     * {0,...,12}.
     *
     * 'final_metatile' is one of the characters 'H', 'T', 'P' or 'F'.
     */
    size_t ncoords;
    unsigned char *coords;
    char final_metatile;
};

/*
 * Fill in HatPatchParams with a randomly selected set of coordinates,
 * in enough detail to generate a patch of tiling covering an area of
 * w x h 'squares' of a kite tiling.
 *
 * The kites grid is considered to be oriented so that it includes
 * horizontal lines and not vertical ones. So each of the smallest
 * equilateral triangles in the grid has a bounding rectangle whose
 * height is sqrt(3)/2 times its width, and either the top or the
 * bottom of that bounding rectangle is the horizontal edge of the
 * triangle. A 'square' of a kite tiling (for convenience of choosing
 * grid dimensions) counts as one of those bounding rectangles.
 *
 * The 'coords' field of the structure will be filled in with a new
 * dynamically allocated array. Any previous pointer in that field
 * will be overwritten.
 */
void hat_tiling_randomise(struct HatPatchParams *params, int w, int h,
                          random_state *rs);

/*
 * Validate a HatPatchParams to ensure it contains no illegal
 * coordinates. Returns NULL if it's acceptable, or an error string if
 * not.
 */
const char *hat_tiling_params_invalid(const struct HatPatchParams *params);

/*
 * Generate the actual set of hat tiles from a HatPatchParams, passing
 * each one to a callback. The callback receives the vertices of each
 * point, as a sequence of 2*nvertices integers, with x,y coordinates
 * interleaved.
 *
 * The x coordinates are measured in units of 1/4 of the side length
 * of the smallest equilateral triangle, or equivalently, 1/2 the
 * length of one of the long edges of a single kite. The y coordinates
 * are measured in units of 1/6 the height of the triangle, which is
 * also 1/2 the length of the short edge of a kite. Therefore, you can
 * expect x to go up to 4*w and y up to 6*h.
 */
typedef void (*hat_tile_callback_fn)(void *ctx, size_t nvertices,
                                     int *coords);

void hat_tiling_generate(const struct HatPatchParams *params, int w, int h,
                         hat_tile_callback_fn cb, void *cbctx);

#endif
