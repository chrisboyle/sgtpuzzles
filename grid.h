/*
 * (c) Lambros Lambrou 2008
 *
 * Code for working with general grids, which can be any planar graph
 * with faces, edges and vertices (dots).  Includes generators for a few
 * types of grid, including square, hexagonal, triangular and others.
 */

#ifndef PUZZLES_GRID_H
#define PUZZLES_GRID_H

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
  int has_incentre;
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

grid *grid_new_square(int width, int height);
grid *grid_new_honeycomb(int width, int height);
grid *grid_new_triangular(int width, int height);
grid *grid_new_snubsquare(int width, int height);
grid *grid_new_cairo(int width, int height);
grid *grid_new_greathexagonal(int width, int height);
grid *grid_new_octagonal(int width, int height);
grid *grid_new_kites(int width, int height);
grid *grid_new_floret(int width, int height);
grid *grid_new_dodecagonal(int width, int height);
grid *grid_new_greatdodecagonal(int width, int height);

void grid_free(grid *g);

grid_edge *grid_nearest_edge(grid *g, int x, int y);

void grid_find_incentre(grid_face *f);

#endif /* PUZZLES_GRID_H */
