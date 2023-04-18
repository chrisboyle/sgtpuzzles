/*
 * (c) Lambros Lambrou 2008
 *
 * Code for working with general grids, which can be any planar graph
 * with faces, edges and vertices (dots).  Includes generators for a few
 * types of grid, including square, hexagonal, triangular and others.
 */

#ifndef PUZZLES_GRID_H
#define PUZZLES_GRID_H

#include "puzzles.h" /* for random_state */

/* Useful macros */
#define SQ(x) ( (x) * (x) )

/* ----------------------------------------------------------------------
 * Grid structures:
 * A grid is made up of faces, edges and dots.  These structures hold
 * the incidence relationships between these types.  For example, an
 * edge always joins two dots, and is adjacent to two faces.
 * The "grid_xxx **" members are lists of pointers which are dynamically
 * allocated during grid generation.
 * A pointer to a face/edge/dot will always point somewhere inside one of the
 * three lists of the main "grid" structure: faces, edges, dots.
 * Could have used integer offsets into these lists, but using actual
 * pointers instead gives us type-safety.
 */

/* Need forward declarations */
typedef struct grid_face grid_face;
typedef struct grid_edge grid_edge;
typedef struct grid_dot grid_dot;

struct grid_face {
  int order; /* Number of edges, also the number of dots */
  grid_edge **edges; /* edges around this face */
  grid_dot **dots; /* corners of this face */
  /*
   * For each face, we optionally compute and store its 'incentre'.
   * The incentre of a triangle is the centre of a circle tangent to
   * all three edges; I generalise the concept to arbitrary polygons
   * by defining it to be the centre of the largest circle you can fit
   * anywhere in the polygon. It's a useful thing to know because if
   * you want to draw any symbol or text in the face (e.g. clue
   * numbers in Loopy), that's the place it will most easily fit.
   *
   * When a grid is first generated, no face has this information
   * computed, because it's fiddly to do. You can call
   * grid_find_incentre() on a face, and it will fill in ix,iy below
   * and set has_incentre to indicate that it's done so.
   */
  bool has_incentre;
  int ix, iy;      /* incentre (centre of largest inscribed circle) */
};
struct grid_edge {
  grid_dot *dot1, *dot2;
  grid_face *face1, *face2; /* Use NULL for the infinite outside face */
};
struct grid_dot {
  int order;
  grid_edge **edges;
  grid_face **faces; /* A NULL grid_face* means infinite outside face */

  /* Position in some fairly arbitrary (Cartesian) coordinate system.
   * Use large enough values such that we can get away with
   * integer arithmetic, but small enough such that arithmetic
   * won't overflow. */
  int x, y;
};
typedef struct grid {
  /* These are (dynamically allocated) arrays of all the
   * faces, edges, dots that are in the grid. */
  int num_faces; grid_face *faces;
  int num_edges; grid_edge *edges;
  int num_dots;  grid_dot *dots;

  /* Cache the bounding-box of the grid, so the drawing-code can quickly
   * figure out the proper scaling to draw onto a given area. */
  int lowest_x, lowest_y, highest_x, highest_y;

  /* A measure of tile size for this grid (in grid coordinates), to help
   * the renderer decide how large to draw the grid.
   * Roughly the size of a single tile - for example the side-length
   * of a square cell. */
  int tilesize;

  /* We really don't want to copy this monstrosity!
   * A grid is immutable once generated.
   */
  int refcount;
} grid;

/* Grids are specified by type: GRID_SQUARE, GRID_KITE, etc. */

#define GRIDGEN_LIST(A) \
  A(SQUARE,square) \
  A(HONEYCOMB,honeycomb) \
  A(TRIANGULAR,triangular) \
  A(SNUBSQUARE,snubsquare) \
  A(CAIRO,cairo) \
  A(GREATHEXAGONAL,greathexagonal) \
  A(KAGOME,kagome) \
  A(OCTAGONAL,octagonal) \
  A(KITE,kites) \
  A(FLORET,floret) \
  A(DODECAGONAL,dodecagonal) \
  A(GREATDODECAGONAL,greatdodecagonal) \
  A(GREATGREATDODECAGONAL,greatgreatdodecagonal) \
  A(COMPASSDODECAGONAL,compassdodecagonal) \
  A(PENROSE_P2,penrose_p2_kite) \
  A(PENROSE_P3,penrose_p3_thick) \
  A(HATS,hats) \
  /* end of list */

#define ENUM(upper,lower) GRID_ ## upper,
typedef enum grid_type { GRIDGEN_LIST(ENUM) GRID_TYPE_MAX } grid_type;
#undef ENUM

const char *grid_validate_params(grid_type type, int width, int height);

/* Free directly after use if non-NULL. Will never contain an underscore
 * (so clients can safely use that as a separator). */
char *grid_new_desc(grid_type type, int width, int height, random_state *rs);
const char *grid_validate_desc(grid_type type, int width, int height,
                               const char *desc);

grid *grid_new(grid_type type, int width, int height, const char *desc);

void grid_free(grid *g);

grid_edge *grid_nearest_edge(grid *g, int x, int y);

void grid_compute_size(grid_type type, int width, int height,
                       int *tilesize, int *xextent, int *yextent);

void grid_find_incentre(grid_face *f);

#endif /* PUZZLES_GRID_H */
