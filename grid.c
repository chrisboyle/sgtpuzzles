/*
 * (c) Lambros Lambrou 2008
 *
 * Code for working with general grids, which can be any planar graph
 * with faces, edges and vertices (dots).  Includes generators for a few
 * types of grid, including square, hexagonal, triangular and others.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"
#include "grid.h"

/* Debugging options */

/*
#define DEBUG_GRID
*/

/* ----------------------------------------------------------------------
 * Deallocate or dereference a grid
 */
void grid_free(grid *g)
{
    assert(g->refcount);

    g->refcount--;
    if (g->refcount == 0) {
        int i;
        for (i = 0; i < g->num_faces; i++) {
            sfree(g->faces[i].dots);
            sfree(g->faces[i].edges);
        }
        for (i = 0; i < g->num_dots; i++) {
            sfree(g->dots[i].faces);
            sfree(g->dots[i].edges);
        }
        sfree(g->faces);
        sfree(g->edges);
        sfree(g->dots);
        sfree(g);
    }
}

/* Used by the other grid generators.  Create a brand new grid with nothing
 * initialised (all lists are NULL) */
static grid *grid_new(void)
{
    grid *g = snew(grid);
    g->faces = NULL;
    g->edges = NULL;
    g->dots = NULL;
    g->num_faces = g->num_edges = g->num_dots = 0;
    g->middle_face = NULL;
    g->refcount = 1;
    g->lowest_x = g->lowest_y = g->highest_x = g->highest_y = 0;
    return g;
}

/* Helper function to calculate perpendicular distance from
 * a point P to a line AB.  A and B mustn't be equal here.
 *
 * Well-known formula for area A of a triangle:
 *                             /  1   1   1 \
 * 2A = determinant of matrix  | px  ax  bx |
 *                             \ py  ay  by /
 *
 * Also well-known: 2A = base * height
 *                     = perpendicular distance * line-length.
 *
 * Combining gives: distance = determinant / line-length(a,b)
 */
static double point_line_distance(long px, long py,
                                  long ax, long ay,
                                  long bx, long by)
{
    long det = ax*by - bx*ay + bx*py - px*by + px*ay - ax*py;
    double len;
    det = max(det, -det);
    len = sqrt(SQ(ax - bx) + SQ(ay - by));
    return det / len;
}

/* Determine nearest edge to where the user clicked.
 * (x, y) is the clicked location, converted to grid coordinates.
 * Returns the nearest edge, or NULL if no edge is reasonably
 * near the position.
 *
 * This algorithm is nice and generic, and doesn't depend on any particular
 * geometric layout of the grid:
 *   Start at any dot (pick one next to middle_face).
 *   Walk along a path by choosing, from all nearby dots, the one that is
 *   nearest the target (x,y).  Hopefully end up at the dot which is closest
 *   to (x,y).  Should work, as long as faces aren't too badly shaped.
 *   Then examine each edge around this dot, and pick whichever one is
 *   closest (perpendicular distance) to (x,y).
 *   Using perpendicular distance is not quite right - the edge might be
 *   "off to one side".  So we insist that the triangle with (x,y) has
 *   acute angles at the edge's dots.
 *
 *     edge1
 *  *---------*------
 *            |
 *            |      *(x,y)
 *      edge2 |
 *            |   edge2 is OK, but edge1 is not, even though
 *            |   edge1 is perpendicularly closer to (x,y)
 *            *
 *
 */
grid_edge *grid_nearest_edge(grid *g, int x, int y)
{
    grid_dot *cur;
    grid_edge *best_edge;
    double best_distance = 0;
    int i;

    cur = g->middle_face->dots[0];

    for (;;) {
        /* Target to beat */
        long dist = SQ((long)cur->x - (long)x) + SQ((long)cur->y - (long)y);
        /* Look for nearer dot - if found, store in 'new'. */
        grid_dot *new = cur;
        int i;
        /* Search all dots in all faces touching this dot.  Some shapes
         * (such as in Cairo) don't quite work properly if we only search
         * the dot's immediate neighbours. */
        for (i = 0; i < cur->order; i++) {
            grid_face *f = cur->faces[i];
            int j;
            if (!f) continue;
            for (j = 0; j < f->order; j++) {
		long new_dist;
                grid_dot *d = f->dots[j];
                if (d == cur) continue;
                new_dist = SQ((long)d->x - (long)x) + SQ((long)d->y - (long)y);
                if (new_dist < dist) {
                    new = d;
                    break; /* found closer dot */
                }
            }
            if (new != cur)
                break; /* found closer dot */
        }

        if (new == cur) {
            /* Didn't find a closer dot among the neighbours of 'cur' */
            break;
        } else {
            cur = new;
        }
    }
    /* 'cur' is nearest dot, so find which of the dot's edges is closest. */
    best_edge = NULL;

    for (i = 0; i < cur->order; i++) {
        grid_edge *e = cur->edges[i];
        long e2; /* squared length of edge */
        long a2, b2; /* squared lengths of other sides */
        double dist;

        /* See if edge e is eligible - the triangle must have acute angles
         * at the edge's dots.
         * Pythagoras formula h^2 = a^2 + b^2 detects right-angles,
         * so detect acute angles by testing for h^2 < a^2 + b^2 */
        e2 = SQ((long)e->dot1->x - (long)e->dot2->x) + SQ((long)e->dot1->y - (long)e->dot2->y);
        a2 = SQ((long)e->dot1->x - (long)x) + SQ((long)e->dot1->y - (long)y);
        b2 = SQ((long)e->dot2->x - (long)x) + SQ((long)e->dot2->y - (long)y);
        if (a2 >= e2 + b2) continue;
        if (b2 >= e2 + a2) continue;
         
        /* e is eligible so far.  Now check the edge is reasonably close
         * to where the user clicked.  Don't want to toggle an edge if the
         * click was way off the grid.
         * There is room for experimentation here.  We could check the
         * perpendicular distance is within a certain fraction of the length
         * of the edge.  That amounts to testing a rectangular region around
         * the edge.
         * Alternatively, we could check that the angle at the point is obtuse.
         * That would amount to testing a circular region with the edge as
         * diameter. */
        dist = point_line_distance((long)x, (long)y,
                                   (long)e->dot1->x, (long)e->dot1->y,
                                   (long)e->dot2->x, (long)e->dot2->y);
        /* Is dist more than half edge length ? */
        if (4 * SQ(dist) > e2)
            continue;

        if (best_edge == NULL || dist < best_distance) {
            best_edge = e;
            best_distance = dist;
        }
    }
    return best_edge;
}

/* ----------------------------------------------------------------------
 * Grid generation
 */

#ifdef DEBUG_GRID
/* Show the basic grid information, before doing grid_make_consistent */
static void grid_print_basic(grid *g)
{
    /* TODO: Maybe we should generate an SVG image of the dots and lines
     * of the grid here, before grid_make_consistent.
     * Would help with debugging grid generation. */
    int i;
    printf("--- Basic Grid Data ---\n");
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        printf("Face %d: dots[", i);
        int j;
        for (j = 0; j < f->order; j++) {
            grid_dot *d = f->dots[j];
            printf("%s%d", j ? "," : "", (int)(d - g->dots)); 
        }
        printf("]\n");
    }
    printf("Middle face: %d\n", (int)(g->middle_face - g->faces));
}
/* Show the derived grid information, computed by grid_make_consistent */
static void grid_print_derived(grid *g)
{
    /* edges */
    int i;
    printf("--- Derived Grid Data ---\n");
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        printf("Edge %d: dots[%d,%d] faces[%d,%d]\n",
            i, (int)(e->dot1 - g->dots), (int)(e->dot2 - g->dots),
            e->face1 ? (int)(e->face1 - g->faces) : -1,
            e->face2 ? (int)(e->face2 - g->faces) : -1);
    }
    /* faces */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int j;
        printf("Face %d: faces[", i);
        for (j = 0; j < f->order; j++) {
            grid_edge *e = f->edges[j];
            grid_face *f2 = (e->face1 == f) ? e->face2 : e->face1;
            printf("%s%d", j ? "," : "", f2 ? (int)(f2 - g->faces) : -1);
        }
        printf("]\n");
    }
    /* dots */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int j;
        printf("Dot %d: dots[", i);
        for (j = 0; j < d->order; j++) {
            grid_edge *e = d->edges[j];
            grid_dot *d2 = (e->dot1 == d) ? e->dot2 : e->dot1;
            printf("%s%d", j ? "," : "", (int)(d2 - g->dots));
        }
        printf("] faces[");
        for (j = 0; j < d->order; j++) {
            grid_face *f = d->faces[j];
            printf("%s%d", j ? "," : "", f ? (int)(f - g->faces) : -1);
        }
        printf("]\n");
    }
}
#endif /* DEBUG_GRID */

/* Helper function for building incomplete-edges list in
 * grid_make_consistent() */
static int grid_edge_bydots_cmpfn(void *v1, void *v2)
{
    grid_edge *a = v1;
    grid_edge *b = v2;
    grid_dot *da, *db;

    /* Pointer subtraction is valid here, because all dots point into the
     * same dot-list (g->dots).
     * Edges are not "normalised" - the 2 dots could be stored in any order,
     * so we need to take this into account when comparing edges. */

    /* Compare first dots */
    da = (a->dot1 < a->dot2) ? a->dot1 : a->dot2;
    db = (b->dot1 < b->dot2) ? b->dot1 : b->dot2;
    if (da != db)
        return db - da;
    /* Compare last dots */
    da = (a->dot1 < a->dot2) ? a->dot2 : a->dot1;
    db = (b->dot1 < b->dot2) ? b->dot2 : b->dot1;
    if (da != db)
        return db - da;

    return 0;
}

/* Input: grid has its dots and faces initialised:
 * - dots have (optionally) x and y coordinates, but no edges or faces
 * (pointers are NULL).
 * - edges not initialised at all
 * - faces initialised and know which dots they have (but no edges yet).  The
 * dots around each face are assumed to be clockwise.
 *
 * Output: grid is complete and valid with all relationships defined.
 */
static void grid_make_consistent(grid *g)
{
    int i;
    tree234 *incomplete_edges;
    grid_edge *next_new_edge; /* Where new edge will go into g->edges */

#ifdef DEBUG_GRID
    grid_print_basic(g);
#endif

    /* ====== Stage 1 ======
     * Generate edges
     */

    /* We know how many dots and faces there are, so we can find the exact
     * number of edges from Euler's polyhedral formula: F + V = E + 2 .
     * We use "-1", not "-2" here, because Euler's formula includes the
     * infinite face, which we don't count. */
    g->num_edges = g->num_faces + g->num_dots - 1;
    g->edges = snewn(g->num_edges, grid_edge);
    next_new_edge = g->edges;

    /* Iterate over faces, and over each face's dots, generating edges as we
     * go.  As we find each new edge, we can immediately fill in the edge's
     * dots, but only one of the edge's faces.  Later on in the iteration, we
     * will find the same edge again (unless it's on the border), but we will
     * know the other face.
     * For efficiency, maintain a list of the incomplete edges, sorted by
     * their dots. */
    incomplete_edges = newtree234(grid_edge_bydots_cmpfn);
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int j;
        for (j = 0; j < f->order; j++) {
            grid_edge e; /* fake edge for searching */
            grid_edge *edge_found;
            int j2 = j + 1;
            if (j2 == f->order)
                j2 = 0;
            e.dot1 = f->dots[j];
            e.dot2 = f->dots[j2];
            /* Use del234 instead of find234, because we always want to
             * remove the edge if found */
            edge_found = del234(incomplete_edges, &e);
            if (edge_found) {
                /* This edge already added, so fill out missing face.
                 * Edge is already removed from incomplete_edges. */
                edge_found->face2 = f;
            } else {
                assert(next_new_edge - g->edges < g->num_edges);
                next_new_edge->dot1 = e.dot1;
                next_new_edge->dot2 = e.dot2;
                next_new_edge->face1 = f;
                next_new_edge->face2 = NULL; /* potentially infinite face */
                add234(incomplete_edges, next_new_edge);
                ++next_new_edge;
            }
        }
    }
    freetree234(incomplete_edges);
    
    /* ====== Stage 2 ======
     * For each face, build its edge list.
     */

    /* Allocate space for each edge list.  Can do this, because each face's
     * edge-list is the same size as its dot-list. */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int j;
        f->edges = snewn(f->order, grid_edge*);
        /* Preload with NULLs, to help detect potential bugs. */
        for (j = 0; j < f->order; j++)
            f->edges[j] = NULL;
    }
    
    /* Iterate over each edge, and over both its faces.  Add this edge to
     * the face's edge-list, after finding where it should go in the
     * sequence. */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int j;
        for (j = 0; j < 2; j++) {
            grid_face *f = j ? e->face2 : e->face1;
            int k, k2;
            if (f == NULL) continue;
            /* Find one of the dots around the face */
            for (k = 0; k < f->order; k++) {
                if (f->dots[k] == e->dot1)
                    break; /* found dot1 */
            }
            assert(k != f->order); /* Must find the dot around this face */

            /* Labelling scheme: as we walk clockwise around the face,
             * starting at dot0 (f->dots[0]), we hit:
             * (dot0), edge0, dot1, edge1, dot2,...
             *
             *     0
             *  0-----1
             *        |
             *        |1
             *        |
             *  3-----2
             *     2
             *
             * Therefore, edgeK joins dotK and dot{K+1}
             */
            
            /* Around this face, either the next dot or the previous dot
             * must be e->dot2.  Otherwise the edge is wrong. */
            k2 = k + 1;
            if (k2 == f->order)
                k2 = 0;
            if (f->dots[k2] == e->dot2) {
                /* dot1(k) and dot2(k2) go clockwise around this face, so add
                 * this edge at position k (see diagram). */
                assert(f->edges[k] == NULL);
                f->edges[k] = e;
                continue;
            }
            /* Try previous dot */
            k2 = k - 1;
            if (k2 == -1)
                k2 = f->order - 1;
            if (f->dots[k2] == e->dot2) {
                /* dot1(k) and dot2(k2) go anticlockwise around this face. */
                assert(f->edges[k2] == NULL);
                f->edges[k2] = e;
                continue;
            }
            assert(!"Grid broken: bad edge-face relationship");
        }
    }

    /* ====== Stage 3 ======
     * For each dot, build its edge-list and face-list.
     */

    /* We don't know how many edges/faces go around each dot, so we can't
     * allocate the right space for these lists.  Pre-compute the sizes by
     * iterating over each edge and recording a tally against each dot. */
    for (i = 0; i < g->num_dots; i++) {
        g->dots[i].order = 0;
    }
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        ++(e->dot1->order);
        ++(e->dot2->order);
    }
    /* Now we have the sizes, pre-allocate the edge and face lists. */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int j;
        assert(d->order >= 2); /* sanity check */
        d->edges = snewn(d->order, grid_edge*);
        d->faces = snewn(d->order, grid_face*);
        for (j = 0; j < d->order; j++) {
            d->edges[j] = NULL;
            d->faces[j] = NULL;
        }
    }
    /* For each dot, need to find a face that touches it, so we can seed
     * the edge-face-edge-face process around each dot. */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int j;
        for (j = 0; j < f->order; j++) {
            grid_dot *d = f->dots[j];
            d->faces[0] = f;
        }
    }
    /* Each dot now has a face in its first slot.  Generate the remaining
     * faces and edges around the dot, by searching both clockwise and
     * anticlockwise from the first face.  Need to do both directions,
     * because of the possibility of hitting the infinite face, which
     * blocks progress.  But there's only one such face, so we will
     * succeed in finding every edge and face this way. */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int current_face1 = 0; /* ascends clockwise */
        int current_face2 = 0; /* descends anticlockwise */
        
        /* Labelling scheme: as we walk clockwise around the dot, starting
         * at face0 (d->faces[0]), we hit:
         * (face0), edge0, face1, edge1, face2,...
         *
         *       0
         *       |
         *    0  |  1
         *       |
         *  -----d-----1
         *       |
         *       |  2
         *       |
         *       2
         *
         * So, for example, face1 should be joined to edge0 and edge1,
         * and those edges should appear in an anticlockwise sense around
         * that face (see diagram). */
 
        /* clockwise search */
        while (TRUE) {
            grid_face *f = d->faces[current_face1];
            grid_edge *e;
            int j;
            assert(f != NULL);
            /* find dot around this face */
            for (j = 0; j < f->order; j++) {
                if (f->dots[j] == d)
                    break;
            }
            assert(j != f->order); /* must find dot */
            
            /* Around f, required edge is anticlockwise from the dot.  See
             * the other labelling scheme higher up, for why we subtract 1
             * from j. */
            j--;
            if (j == -1)
                j = f->order - 1;
            e = f->edges[j];
            d->edges[current_face1] = e; /* set edge */
            current_face1++;
            if (current_face1 == d->order)
                break;
            else {
                /* set face */
                d->faces[current_face1] =
                    (e->face1 == f) ? e->face2 : e->face1;
                if (d->faces[current_face1] == NULL)
                    break; /* cannot progress beyond infinite face */
            }
        }
        /* If the clockwise search made it all the way round, don't need to
         * bother with the anticlockwise search. */
        if (current_face1 == d->order)
            continue; /* this dot is complete, move on to next dot */
        
        /* anticlockwise search */
        while (TRUE) {
            grid_face *f = d->faces[current_face2];
            grid_edge *e;
            int j;
            assert(f != NULL);
            /* find dot around this face */
            for (j = 0; j < f->order; j++) {
                if (f->dots[j] == d)
                    break;
            }
            assert(j != f->order); /* must find dot */
            
            /* Around f, required edge is clockwise from the dot. */
            e = f->edges[j];
            
            current_face2--;
            if (current_face2 == -1)
                current_face2 = d->order - 1;
            d->edges[current_face2] = e; /* set edge */

            /* set face */
            if (current_face2 == current_face1)
                break;
            d->faces[current_face2] =
                    (e->face1 == f) ? e->face2 : e->face1;
            /* There's only 1 infinite face, so we must get all the way
             * to current_face1 before we hit it. */
            assert(d->faces[current_face2]);
        }
    }

    /* ====== Stage 4 ======
     * Compute other grid settings
     */

    /* Bounding rectangle */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        if (i == 0) {
            g->lowest_x = g->highest_x = d->x;
            g->lowest_y = g->highest_y = d->y;
        } else {
            g->lowest_x = min(g->lowest_x, d->x);
            g->highest_x = max(g->highest_x, d->x);
            g->lowest_y = min(g->lowest_y, d->y);
            g->highest_y = max(g->highest_y, d->y);
        }
    }
    
#ifdef DEBUG_GRID
    grid_print_derived(g);
#endif
}

/* Helpers for making grid-generation easier.  These functions are only
 * intended for use during grid generation. */

/* Comparison function for the (tree234) sorted dot list */
static int grid_point_cmp_fn(void *v1, void *v2)
{
    grid_dot *p1 = v1;
    grid_dot *p2 = v2;
    if (p1->y != p2->y)
        return p2->y - p1->y;
    else
        return p2->x - p1->x;
}
/* Add a new face to the grid, with its dot list allocated.
 * Assumes there's enough space allocated for the new face in grid->faces */
static void grid_face_add_new(grid *g, int face_size)
{
    int i;
    grid_face *new_face = g->faces + g->num_faces;
    new_face->order = face_size;
    new_face->dots = snewn(face_size, grid_dot*);
    for (i = 0; i < face_size; i++)
        new_face->dots[i] = NULL;
    new_face->edges = NULL;
    g->num_faces++;
}
/* Assumes dot list has enough space */
static grid_dot *grid_dot_add_new(grid *g, int x, int y)
{
    grid_dot *new_dot = g->dots + g->num_dots;
    new_dot->order = 0;
    new_dot->edges = NULL;
    new_dot->faces = NULL;
    new_dot->x = x;
    new_dot->y = y;
    g->num_dots++;
    return new_dot;
}
/* Retrieve a dot with these (x,y) coordinates.  Either return an existing dot
 * in the dot_list, or add a new dot to the grid (and the dot_list) and
 * return that.
 * Assumes g->dots has enough capacity allocated */
static grid_dot *grid_get_dot(grid *g, tree234 *dot_list, int x, int y)
{
    grid_dot test = {0, NULL, NULL, x, y};
    grid_dot *ret = find234(dot_list, &test, NULL);
    if (ret)
        return ret;

    ret = grid_dot_add_new(g, x, y);
    add234(dot_list, ret);
    return ret;
}

/* Sets the last face of the grid to include this dot, at this position
 * around the face. Assumes num_faces is at least 1 (a new face has
 * previously been added, with the required number of dots allocated) */
static void grid_face_set_dot(grid *g, grid_dot *d, int position)
{
    grid_face *last_face = g->faces + g->num_faces - 1;
    last_face->dots[position] = d;
}

/* ------ Generate various types of grid ------ */

/* General method is to generate faces, by calculating their dot coordinates.
 * As new faces are added, we keep track of all the dots so we can tell when
 * a new face reuses an existing dot.  For example, two squares touching at an
 * edge would generate six unique dots: four dots from the first face, then
 * two additional dots for the second face, because we detect the other two
 * dots have already been taken up.  This list is stored in a tree234
 * called "points".  No extra memory-allocation needed here - we store the
 * actual grid_dot* pointers, which all point into the g->dots list.
 * For this reason, we have to calculate coordinates in such a way as to
 * eliminate any rounding errors, so we can detect when a dot on one
 * face precisely lands on a dot of a different face.  No floating-point
 * arithmetic here!
 */

grid *grid_new_square(int width, int height)
{
    int x, y;
    /* Side length */
    int a = 20;

    /* Upper bounds - don't have to be exact */
    int max_faces = width * height;
    int max_dots = (width + 1) * (height + 1);

    tree234 *points;

    grid *g = grid_new();
    g->tilesize = a;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    /* generate square faces */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* face position */
            int px = a * x;
            int py = a * y;

            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + a, py);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px + a, py + a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px, py + a);
            grid_face_set_dot(g, d, 3);
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

grid *grid_new_honeycomb(int width, int height)
{
    int x, y;
    /* Vector for side of hexagon - ratio is close to sqrt(3) */
    int a = 15;
    int b = 26;

    /* Upper bounds - don't have to be exact */
    int max_faces = width * height;
    int max_dots = 2 * (width + 1) * (height + 1);
    
    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 3 * a;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    /* generate hexagonal faces */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* face centre */
            int cx = 3 * a * x;
            int cy = 2 * b * y;
            if (x % 2)
                cy += b;
            grid_face_add_new(g, 6);

            d = grid_get_dot(g, points, cx - a, cy - b);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, cx + a, cy - b);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, cx + 2*a, cy);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, cx + a, cy + b);
            grid_face_set_dot(g, d, 3);
            d = grid_get_dot(g, points, cx - a, cy + b);
            grid_face_set_dot(g, d, 4);
            d = grid_get_dot(g, points, cx - 2*a, cy);
            grid_face_set_dot(g, d, 5);
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

/* Doesn't use the previous method of generation, it pre-dates it!
 * A triangular grid is just about simple enough to do by "brute force" */
grid *grid_new_triangular(int width, int height)
{
    int x,y;
    
    /* Vector for side of triangle - ratio is close to sqrt(3) */
    int vec_x = 15;
    int vec_y = 26;
    
    int index;

    /* convenient alias */
    int w = width + 1;

    grid *g = grid_new();
    g->tilesize = 18; /* adjust to your taste */

    g->num_faces = width * height * 2;
    g->num_dots = (width + 1) * (height + 1);
    g->faces = snewn(g->num_faces, grid_face);
    g->dots = snewn(g->num_dots, grid_dot);

    /* generate dots */
    index = 0;
    for (y = 0; y <= height; y++) {
        for (x = 0; x <= width; x++) {
            grid_dot *d = g->dots + index;
            /* odd rows are offset to the right */
            d->order = 0;
            d->edges = NULL;
            d->faces = NULL;
            d->x = x * 2 * vec_x + ((y % 2) ? vec_x : 0);
            d->y = y * vec_y;
            index++;
        }
    }
    
    /* generate faces */
    index = 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            /* initialise two faces for this (x,y) */
            grid_face *f1 = g->faces + index;
            grid_face *f2 = f1 + 1;
            f1->edges = NULL;
            f1->order = 3;
            f1->dots = snewn(f1->order, grid_dot*);
            f2->edges = NULL;
            f2->order = 3;
            f2->dots = snewn(f2->order, grid_dot*);

            /* face descriptions depend on whether the row-number is
             * odd or even */
            if (y % 2) {
                f1->dots[0] = g->dots + y       * w + x;
                f1->dots[1] = g->dots + (y + 1) * w + x + 1;
                f1->dots[2] = g->dots + (y + 1) * w + x;
                f2->dots[0] = g->dots + y       * w + x;
                f2->dots[1] = g->dots + y       * w + x + 1;
                f2->dots[2] = g->dots + (y + 1) * w + x + 1;
            } else {
                f1->dots[0] = g->dots + y       * w + x;
                f1->dots[1] = g->dots + y       * w + x + 1;
                f1->dots[2] = g->dots + (y + 1) * w + x;
                f2->dots[0] = g->dots + y       * w + x + 1;
                f2->dots[1] = g->dots + (y + 1) * w + x + 1;
                f2->dots[2] = g->dots + (y + 1) * w + x;
            }
            index += 2;
        }
    }

    /* "+ width" takes us to the middle of the row, because each row has
     * (2*width) faces. */
    g->middle_face = g->faces + (height / 2) * 2 * width + width;

    grid_make_consistent(g);
    return g;
}

grid *grid_new_snubsquare(int width, int height)
{
    int x, y;
    /* Vector for side of triangle - ratio is close to sqrt(3) */
    int a = 15;
    int b = 26;

    /* Upper bounds - don't have to be exact */
    int max_faces = 3 * width * height;
    int max_dots = 2 * (width + 1) * (height + 1);
    
    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 18;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* face position */
            int px = (a + b) * x;
            int py = (a + b) * y;

            /* generate square faces */
            grid_face_add_new(g, 4);
            if ((x + y) % 2) {
                d = grid_get_dot(g, points, px + a, py);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + a + b, py + a);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px + b, py + a + b);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px, py + b);
                grid_face_set_dot(g, d, 3);
            } else {
                d = grid_get_dot(g, points, px + b, py);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + a + b, py + b);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px + a, py + a + b);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px, py + a);
                grid_face_set_dot(g, d, 3);
            }

            /* generate up/down triangles */
            if (x > 0) {
                grid_face_add_new(g, 3);
                if ((x + y) % 2) {
                    d = grid_get_dot(g, points, px + a, py);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px, py + b);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px - a, py);
                    grid_face_set_dot(g, d, 2);
                } else {
                    d = grid_get_dot(g, points, px, py + a);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + a, py + a + b);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px - a, py + a + b);
                    grid_face_set_dot(g, d, 2);
                }
            }

            /* generate left/right triangles */
            if (y > 0) {
                grid_face_add_new(g, 3);
                if ((x + y) % 2) {
                    d = grid_get_dot(g, points, px + a, py);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + a + b, py - a);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px + a + b, py + a);
                    grid_face_set_dot(g, d, 2);
                } else {
                    d = grid_get_dot(g, points, px, py - a);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + b, py);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px, py + a);
                    grid_face_set_dot(g, d, 2);
                }
            }
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

grid *grid_new_cairo(int width, int height)
{
    int x, y;
    /* Vector for side of pentagon - ratio is close to (4+sqrt(7))/3 */
    int a = 14;
    int b = 31;

    /* Upper bounds - don't have to be exact */
    int max_faces = 2 * width * height;
    int max_dots = 3 * (width + 1) * (height + 1);
    
    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 40;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* cell position */
            int px = 2 * b * x;
            int py = 2 * b * y;

            /* horizontal pentagons */
            if (y > 0) {
                grid_face_add_new(g, 5);
                if ((x + y) % 2) {
                    d = grid_get_dot(g, points, px + a, py - b);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + 2*b - a, py - b);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px + 2*b, py);
                    grid_face_set_dot(g, d, 2);
                    d = grid_get_dot(g, points, px + b, py + a);
                    grid_face_set_dot(g, d, 3);
                    d = grid_get_dot(g, points, px, py);
                    grid_face_set_dot(g, d, 4);
                } else {
                    d = grid_get_dot(g, points, px, py);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + b, py - a);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px + 2*b, py);
                    grid_face_set_dot(g, d, 2);
                    d = grid_get_dot(g, points, px + 2*b - a, py + b);
                    grid_face_set_dot(g, d, 3);
                    d = grid_get_dot(g, points, px + a, py + b);
                    grid_face_set_dot(g, d, 4);
                }
            }
            /* vertical pentagons */
            if (x > 0) {
                grid_face_add_new(g, 5);
                if ((x + y) % 2) {
                    d = grid_get_dot(g, points, px, py);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + b, py + a);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px + b, py + 2*b - a);
                    grid_face_set_dot(g, d, 2);
                    d = grid_get_dot(g, points, px, py + 2*b);
                    grid_face_set_dot(g, d, 3);
                    d = grid_get_dot(g, points, px - a, py + b);
                    grid_face_set_dot(g, d, 4);
                } else {
                    d = grid_get_dot(g, points, px, py);
                    grid_face_set_dot(g, d, 0);
                    d = grid_get_dot(g, points, px + a, py + b);
                    grid_face_set_dot(g, d, 1);
                    d = grid_get_dot(g, points, px, py + 2*b);
                    grid_face_set_dot(g, d, 2);
                    d = grid_get_dot(g, points, px - b, py + 2*b - a);
                    grid_face_set_dot(g, d, 3);
                    d = grid_get_dot(g, points, px - b, py + a);
                    grid_face_set_dot(g, d, 4);
                }
            }
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

grid *grid_new_greathexagonal(int width, int height)
{
    int x, y;
    /* Vector for side of triangle - ratio is close to sqrt(3) */
    int a = 15;
    int b = 26;

    /* Upper bounds - don't have to be exact */
    int max_faces = 6 * (width + 1) * (height + 1);
    int max_dots = 6 * width * height;

    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 18;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* centre of hexagon */
            int px = (3*a + b) * x;
            int py = (2*a + 2*b) * y;
            if (x % 2)
                py += a + b;

            /* hexagon */
            grid_face_add_new(g, 6);
            d = grid_get_dot(g, points, px - a, py - b);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + a, py - b);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px + 2*a, py);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px + a, py + b);
            grid_face_set_dot(g, d, 3);
            d = grid_get_dot(g, points, px - a, py + b);
            grid_face_set_dot(g, d, 4);
            d = grid_get_dot(g, points, px - 2*a, py);
            grid_face_set_dot(g, d, 5);

            /* square below hexagon */
            if (y < height - 1) {
                grid_face_add_new(g, 4);
                d = grid_get_dot(g, points, px - a, py + b);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + a, py + b);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px + a, py + 2*a + b);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px - a, py + 2*a + b);
                grid_face_set_dot(g, d, 3);
            }

            /* square below right */
            if ((x < width - 1) && (((x % 2) == 0) || (y < height - 1))) {
                grid_face_add_new(g, 4);
                d = grid_get_dot(g, points, px + 2*a, py);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + 2*a + b, py + a);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px + a + b, py + a + b);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px + a, py + b);
                grid_face_set_dot(g, d, 3);
            }

            /* square below left */
            if ((x > 0) && (((x % 2) == 0) || (y < height - 1))) {
                grid_face_add_new(g, 4);
                d = grid_get_dot(g, points, px - 2*a, py);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px - a, py + b);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px - a - b, py + a + b);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px - 2*a - b, py + a);
                grid_face_set_dot(g, d, 3);
            }
           
            /* Triangle below right */
            if ((x < width - 1) && (y < height - 1)) {
                grid_face_add_new(g, 3);
                d = grid_get_dot(g, points, px + a, py + b);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + a + b, py + a + b);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px + a, py + 2*a + b);
                grid_face_set_dot(g, d, 2);
            }

            /* Triangle below left */
            if ((x > 0) && (y < height - 1)) {
                grid_face_add_new(g, 3);
                d = grid_get_dot(g, points, px - a, py + b);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px - a, py + 2*a + b);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px - a - b, py + a + b);
                grid_face_set_dot(g, d, 2);
            }
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

grid *grid_new_octagonal(int width, int height)
{
    int x, y;
    /* b/a approx sqrt(2) */
    int a = 29;
    int b = 41;

    /* Upper bounds - don't have to be exact */
    int max_faces = 2 * width * height;
    int max_dots = 4 * (width + 1) * (height + 1);

    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 40;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* cell position */
            int px = (2*a + b) * x;
            int py = (2*a + b) * y;
            /* octagon */
            grid_face_add_new(g, 8);
            d = grid_get_dot(g, points, px + a, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + a + b, py);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px + 2*a + b, py + a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px + 2*a + b, py + a + b);
            grid_face_set_dot(g, d, 3);
            d = grid_get_dot(g, points, px + a + b, py + 2*a + b);
            grid_face_set_dot(g, d, 4);
            d = grid_get_dot(g, points, px + a, py + 2*a + b);
            grid_face_set_dot(g, d, 5);
            d = grid_get_dot(g, points, px, py + a + b);
            grid_face_set_dot(g, d, 6);
            d = grid_get_dot(g, points, px, py + a);
            grid_face_set_dot(g, d, 7);

            /* diamond */
            if ((x > 0) && (y > 0)) {
                grid_face_add_new(g, 4);
                d = grid_get_dot(g, points, px, py - a);
                grid_face_set_dot(g, d, 0);
                d = grid_get_dot(g, points, px + a, py);
                grid_face_set_dot(g, d, 1);
                d = grid_get_dot(g, points, px, py + a);
                grid_face_set_dot(g, d, 2);
                d = grid_get_dot(g, points, px - a, py);
                grid_face_set_dot(g, d, 3);
            }
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + (height/2) * width + (width/2);

    grid_make_consistent(g);
    return g;
}

grid *grid_new_kites(int width, int height)
{
    int x, y;
    /* b/a approx sqrt(3) */
    int a = 15;
    int b = 26;

    /* Upper bounds - don't have to be exact */
    int max_faces = 6 * width * height;
    int max_dots = 6 * (width + 1) * (height + 1);

    tree234 *points;

    grid *g = grid_new();
    g->tilesize = 40;
    g->faces = snewn(max_faces, grid_face);
    g->dots = snewn(max_dots, grid_dot);

    points = newtree234(grid_point_cmp_fn);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            grid_dot *d;
            /* position of order-6 dot */
            int px = 4*b * x;
            int py = 6*a * y;
            if (y % 2)
                px += 2*b;

            /* kite pointing up-left */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + 2*b, py);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px + 2*b, py + 2*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px + b, py + 3*a);
            grid_face_set_dot(g, d, 3);

            /* kite pointing up */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + b, py + 3*a);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px, py + 4*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px - b, py + 3*a);
            grid_face_set_dot(g, d, 3);

            /* kite pointing up-right */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px - b, py + 3*a);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px - 2*b, py + 2*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px - 2*b, py);
            grid_face_set_dot(g, d, 3);

            /* kite pointing down-right */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px - 2*b, py);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px - 2*b, py - 2*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px - b, py - 3*a);
            grid_face_set_dot(g, d, 3);

            /* kite pointing down */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px - b, py - 3*a);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px, py - 4*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px + b, py - 3*a);
            grid_face_set_dot(g, d, 3);

            /* kite pointing down-left */
            grid_face_add_new(g, 4);
            d = grid_get_dot(g, points, px, py);
            grid_face_set_dot(g, d, 0);
            d = grid_get_dot(g, points, px + b, py - 3*a);
            grid_face_set_dot(g, d, 1);
            d = grid_get_dot(g, points, px + 2*b, py - 2*a);
            grid_face_set_dot(g, d, 2);
            d = grid_get_dot(g, points, px + 2*b, py);
            grid_face_set_dot(g, d, 3);
        }
    }

    freetree234(points);
    assert(g->num_faces <= max_faces);
    assert(g->num_dots <= max_dots);
    g->middle_face = g->faces + 6 * ((height/2) * width + (width/2));

    grid_make_consistent(g);
    return g;
}

/* ----------- End of grid generators ------------- */
