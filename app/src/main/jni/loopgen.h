/*
 * loopgen.h: interface file for loop generation functions for grid.[ch].
 */

#ifndef PUZZLES_LOOPGEN_H
#define PUZZLES_LOOPGEN_H

#include "puzzles.h"
#include "grid.h"

enum face_colour { FACE_WHITE, FACE_GREY, FACE_BLACK };

/* face should be of type grid_face* here. */
#define FACE_COLOUR(face) \
    ( (face) == NULL ? FACE_BLACK : \
	  board[(face) - g->faces] )

typedef int (*loopgen_bias_fn_t)(void *ctx, char *board, int face);

/* 'board' should be a char array whose length is the same as
 * g->num_faces: this will be filled in with FACE_WHITE or FACE_BLACK
 * after loop generation.
 *
 * If 'bias' is non-null, it should be a user-provided function which
 * rates a half-finished board (i.e. may include some FACE_GREYs) for
 * desirability; this will cause the loop generator to bias in favour
 * of loops with a high return value from that function. The 'face'
 * parameter to the bias function indicates which face of the grid has
 * been modified since the last call; it is guaranteed that only one
 * will have been (so that bias functions can work incrementally
 * rather than re-scanning the whole grid on every call). */
extern void generate_loop(grid *g, char *board, random_state *rs,
                          loopgen_bias_fn_t bias, void *biasctx);

#endif
