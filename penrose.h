/* penrose.h
 *
 * Penrose tiling functions.
 *
 * Provides an interface with which to generate Penrose tilings
 * by recursive subdivision of an initial tile of choice (one of the
 * four sets of two pairs kite/dart, or thin/thick rhombus).
 *
 * You supply a callback function and a context pointer, which is
 * called with each tile in turn: you choose how many times to recurse.
 */

#ifndef PUZZLES_PENROSE_H
#define PUZZLES_PENROSE_H

#ifndef PHI
#define PHI 1.6180339887
#endif

typedef struct vector vector;

double v_x(vector *vs, int i);
double v_y(vector *vs, int i);

typedef struct penrose_state penrose_state;

/* Return non-zero to clip the tree here (i.e. not recurse
 * below this tile).
 *
 * Parameters are state, vector array, npoints, depth.
 * ctx is inside state.
 */
typedef int (*tile_callback)(penrose_state *, vector *, int, int);

struct penrose_state {
    int start_size;  /* initial side length */
    int max_depth;      /* Recursion depth */

    tile_callback new_tile;
    void *ctx;          /* for callback */
};

enum { PENROSE_P2, PENROSE_P3 };

extern int penrose(penrose_state *state, int which, int angle);

/* Returns the side-length of a penrose tile at recursion level
 * gen, given a starting side length. */
extern double penrose_side_length(double start_size, int depth);

/* Returns the count of each type of tile at a given recursion depth. */
extern void penrose_count_tiles(int gen, int *nlarge, int *nsmall);

/* Calculate start size and recursion depth required to produce a
 * width-by-height sized patch of penrose tiles with the given tilesize */
extern void penrose_calculate_size(int which, int tilesize, int w, int h,
                                   double *required_radius, int *start_size, int *depth);

#endif
