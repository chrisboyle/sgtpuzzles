/*
 * cube.c: Cube game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"

const char *const game_name = "Cube";

#define MAXVERTICES 20
#define MAXFACES 20
#define MAXORDER 4
struct solid {
    int nvertices;
    float vertices[MAXVERTICES * 3];   /* 3*npoints coordinates */
    int order;
    int nfaces;
    int faces[MAXFACES * MAXORDER];    /* order*nfaces point indices */
    float normals[MAXFACES * 3];       /* 3*npoints vector components */
    float shear;                       /* isometric shear for nice drawing */
    float border;                      /* border required around arena */
};

static const struct solid tetrahedron = {
    4,
    {
        0.0F, -0.57735026919F, -0.20412414523F,
        -0.5F, 0.28867513459F, -0.20412414523F,
        0.0F, -0.0F, 0.6123724357F,
        0.5F, 0.28867513459F, -0.20412414523F,
    },
    3, 4,
    {
        0,2,1, 3,1,2, 2,0,3, 1,3,0
    },
    {
        -0.816496580928F, -0.471404520791F, 0.333333333334F,
        0.0F, 0.942809041583F, 0.333333333333F,
        0.816496580928F, -0.471404520791F, 0.333333333334F,
        0.0F, 0.0F, -1.0F,
    },
    0.0F, 0.3F
};

static const struct solid cube = {
    8,
    {
        -0.5F,-0.5F,-0.5F, -0.5F,-0.5F,+0.5F,
	-0.5F,+0.5F,-0.5F, -0.5F,+0.5F,+0.5F,
        +0.5F,-0.5F,-0.5F, +0.5F,-0.5F,+0.5F,
	+0.5F,+0.5F,-0.5F, +0.5F,+0.5F,+0.5F,
    },
    4, 6,
    {
        0,1,3,2, 1,5,7,3, 5,4,6,7, 4,0,2,6, 0,4,5,1, 3,7,6,2
    },
    {
        -1.0F,0.0F,0.0F, 0.0F,0.0F,+1.0F,
	+1.0F,0.0F,0.0F, 0.0F,0.0F,-1.0F,
	0.0F,-1.0F,0.0F, 0.0F,+1.0F,0.0F
    },
    0.3F, 0.5F
};

static const struct solid octahedron = {
    6,
    {
        -0.5F, -0.28867513459472505F, 0.4082482904638664F,
        0.5F, 0.28867513459472505F, -0.4082482904638664F,
        -0.5F, 0.28867513459472505F, -0.4082482904638664F,
        0.5F, -0.28867513459472505F, 0.4082482904638664F,
        0.0F, -0.57735026918945009F, -0.4082482904638664F,
        0.0F, 0.57735026918945009F, 0.4082482904638664F,
    },
    3, 8,
    {
        4,0,2, 0,5,2, 0,4,3, 5,0,3, 1,4,2, 5,1,2, 4,1,3, 1,5,3
    },
    {
        -0.816496580928F, -0.471404520791F, -0.333333333334F,
        -0.816496580928F, 0.471404520791F, 0.333333333334F,
        0.0F, -0.942809041583F, 0.333333333333F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, -1.0F,
        0.0F, 0.942809041583F, -0.333333333333F,
        0.816496580928F, -0.471404520791F, -0.333333333334F,
        0.816496580928F, 0.471404520791F, 0.333333333334F,
    },
    0.0F, 0.5F
};

static const struct solid icosahedron = {
    12,
    {
        0.0F, 0.57735026919F, 0.75576131408F,
        0.0F, -0.93417235896F, 0.17841104489F,
        0.0F, 0.93417235896F, -0.17841104489F,
        0.0F, -0.57735026919F, -0.75576131408F,
        -0.5F, -0.28867513459F, 0.75576131408F,
        -0.5F, 0.28867513459F, -0.75576131408F,
        0.5F, -0.28867513459F, 0.75576131408F,
        0.5F, 0.28867513459F, -0.75576131408F,
        -0.80901699437F, 0.46708617948F, 0.17841104489F,
        0.80901699437F, 0.46708617948F, 0.17841104489F,
        -0.80901699437F, -0.46708617948F, -0.17841104489F,
        0.80901699437F, -0.46708617948F, -0.17841104489F,
    },
    3, 20,
    {
        8,0,2,  0,9,2,  1,10,3, 11,1,3,  0,4,6,
        4,1,6,  5,2,7,  3,5,7,  4,8,10,  8,5,10,
        9,6,11, 7,9,11,  0,8,4,  9,0,6,  10,1,4,
        1,11,6, 8,2,5,  2,9,7,  3,10,5, 11,3,7,
    },
    {
        -0.356822089773F, 0.87267799625F, 0.333333333333F,
        0.356822089773F, 0.87267799625F, 0.333333333333F,
        -0.356822089773F, -0.87267799625F, -0.333333333333F,
        0.356822089773F, -0.87267799625F, -0.333333333333F,
        -0.0F, 0.0F, 1.0F,
        0.0F, -0.666666666667F, 0.745355992501F,
        0.0F, 0.666666666667F, -0.745355992501F,
        0.0F, 0.0F, -1.0F,
        -0.934172358963F, -0.12732200375F, 0.333333333333F,
        -0.934172358963F, 0.12732200375F, -0.333333333333F,
        0.934172358963F, -0.12732200375F, 0.333333333333F,
        0.934172358963F, 0.12732200375F, -0.333333333333F,
        -0.57735026919F, 0.333333333334F, 0.745355992501F,
        0.57735026919F, 0.333333333334F, 0.745355992501F,
        -0.57735026919F, -0.745355992501F, 0.333333333334F,
        0.57735026919F, -0.745355992501F, 0.333333333334F,
        -0.57735026919F, 0.745355992501F, -0.333333333334F,
        0.57735026919F, 0.745355992501F, -0.333333333334F,
        -0.57735026919F, -0.333333333334F, -0.745355992501F,
        0.57735026919F, -0.333333333334F, -0.745355992501F,
    },
    0.0F, 0.8F
};

enum {
    TETRAHEDRON, CUBE, OCTAHEDRON, ICOSAHEDRON
};
static const struct solid *solids[] = {
    &tetrahedron, &cube, &octahedron, &icosahedron
};

enum {
    COL_BACKGROUND,
    COL_BORDER,
    COL_BLUE,
    NCOLOURS
};

enum { LEFT, RIGHT, UP, DOWN, UP_LEFT, UP_RIGHT, DOWN_LEFT, DOWN_RIGHT };

#define GRID_SCALE 48.0F
#define ROLLTIME 0.1F

#define SQ(x) ( (x) * (x) )

#define MATMUL(ra,m,a) do { \
    float rx, ry, rz, xx = (a)[0], yy = (a)[1], zz = (a)[2], *mat = (m); \
    rx = mat[0] * xx + mat[3] * yy + mat[6] * zz; \
    ry = mat[1] * xx + mat[4] * yy + mat[7] * zz; \
    rz = mat[2] * xx + mat[5] * yy + mat[8] * zz; \
    (ra)[0] = rx; (ra)[1] = ry; (ra)[2] = rz; \
} while (0)

#define APPROXEQ(x,y) ( SQ(x-y) < 0.1 )

struct grid_square {
    float x, y;
    int npoints;
    float points[8];                   /* maximum */
    int directions[8];                 /* bit masks showing point pairs */
    int flip;
    int blue;
    int tetra_class;
};

struct game_params {
    int solid;
    /*
     * Grid dimensions. For a square grid these are width and
     * height respectively; otherwise the grid is a hexagon, with
     * the top side and the two lower diagonals having length d1
     * and the remaining three sides having length d2 (so that
     * d1==d2 gives a regular hexagon, and d2==0 gives a triangle).
     */
    int d1, d2;
};

struct game_state {
    struct game_params params;
    const struct solid *solid;
    int *facecolours;
    struct grid_square *squares;
    int nsquares;
    int current;                       /* index of current grid square */
    int sgkey[2];                      /* key-point indices into grid sq */
    int dgkey[2];                      /* key-point indices into grid sq */
    int spkey[2];                      /* key-point indices into polyhedron */
    int dpkey[2];                      /* key-point indices into polyhedron */
    int previous;
    float angle;
    int completed;
    int movecount;
};

game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->solid = CUBE;
    ret->d1 = 4;
    ret->d2 = 4;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret = snew(game_params);
    char *str;

    switch (i) {
      case 0:
        str = "Cube";
        ret->solid = CUBE;
        ret->d1 = 4;
        ret->d2 = 4;
        break;
      case 1:
        str = "Tetrahedron";
        ret->solid = TETRAHEDRON;
        ret->d1 = 2;
        ret->d2 = 1;
        break;
      case 2:
        str = "Octahedron";
        ret->solid = OCTAHEDRON;
        ret->d1 = 2;
        ret->d2 = 2;
        break;
      case 3:
        str = "Icosahedron";
        ret->solid = ICOSAHEDRON;
        ret->d1 = 3;
        ret->d2 = 3;
        break;
      default:
        sfree(ret);
        return FALSE;
    }

    *name = dupstr(str);
    *params = ret;
    return TRUE;
}

void free_params(game_params *params)
{
    sfree(params);
}

game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void enum_grid_squares(game_params *params,
                              void (*callback)(void *, struct grid_square *),
                              void *ctx)
{
    const struct solid *solid = solids[params->solid];

    if (solid->order == 4) {
        int x, y;

        for (x = 0; x < params->d1; x++)
            for (y = 0; y < params->d2; y++) {
                struct grid_square sq;

                sq.x = (float)x;
                sq.y = (float)y;
                sq.points[0] = x - 0.5F;
                sq.points[1] = y - 0.5F;
                sq.points[2] = x - 0.5F;
                sq.points[3] = y + 0.5F;
                sq.points[4] = x + 0.5F;
                sq.points[5] = y + 0.5F;
                sq.points[6] = x + 0.5F;
                sq.points[7] = y - 0.5F;
                sq.npoints = 4;

                sq.directions[LEFT]  = 0x03;   /* 0,1 */
                sq.directions[RIGHT] = 0x0C;   /* 2,3 */
                sq.directions[UP]    = 0x09;   /* 0,3 */
                sq.directions[DOWN]  = 0x06;   /* 1,2 */
                sq.directions[UP_LEFT] = 0;   /* no diagonals in a square */
                sq.directions[UP_RIGHT] = 0;   /* no diagonals in a square */
                sq.directions[DOWN_LEFT] = 0;   /* no diagonals in a square */
                sq.directions[DOWN_RIGHT] = 0;   /* no diagonals in a square */

                sq.flip = FALSE;

                /*
                 * This is supremely irrelevant, but just to avoid
                 * having any uninitialised structure members...
                 */
                sq.tetra_class = 0;

                callback(ctx, &sq);
            }
    } else {
        int row, rowlen, other, i, firstix = -1;
        float theight = (float)(sqrt(3) / 2.0);

        for (row = 0; row < params->d1 + params->d2; row++) {
            if (row < params->d1) {
                other = +1;
                rowlen = row + params->d2;
            } else {
                other = -1;
                rowlen = 2*params->d1 + params->d2 - row;
            }

            /*
             * There are `rowlen' down-pointing triangles.
             */
            for (i = 0; i < rowlen; i++) {
                struct grid_square sq;
                int ix;
                float x, y;

                ix = (2 * i - (rowlen-1));
                x = ix * 0.5F;
                y = theight * row;
                sq.x = x;
                sq.y = y + theight / 3;
                sq.points[0] = x - 0.5F;
                sq.points[1] = y;
                sq.points[2] = x;
                sq.points[3] = y + theight;
                sq.points[4] = x + 0.5F;
                sq.points[5] = y;
                sq.npoints = 3;

                sq.directions[LEFT]  = 0x03;   /* 0,1 */
                sq.directions[RIGHT] = 0x06;   /* 1,2 */
                sq.directions[UP]    = 0x05;   /* 0,2 */
                sq.directions[DOWN]  = 0;      /* invalid move */

                /*
                 * Down-pointing triangle: both the up diagonals go
                 * up, and the down ones go left and right.
                 */
                sq.directions[UP_LEFT] = sq.directions[UP_RIGHT] =
                    sq.directions[UP];
                sq.directions[DOWN_LEFT] = sq.directions[LEFT];
                sq.directions[DOWN_RIGHT] = sq.directions[RIGHT];

                sq.flip = TRUE;

                if (firstix < 0)
                    firstix = ix & 3;
                ix -= firstix;
                sq.tetra_class = ((row+(ix&1)) & 2) ^ (ix & 3);

                callback(ctx, &sq);
            }

            /*
             * There are `rowlen+other' up-pointing triangles.
             */
            for (i = 0; i < rowlen+other; i++) {
                struct grid_square sq;
                int ix;
                float x, y;

                ix = (2 * i - (rowlen+other-1));
                x = ix * 0.5F;
                y = theight * row;
                sq.x = x;
                sq.y = y + 2*theight / 3;
                sq.points[0] = x + 0.5F;
                sq.points[1] = y + theight;
                sq.points[2] = x;
                sq.points[3] = y;
                sq.points[4] = x - 0.5F;
                sq.points[5] = y + theight;
                sq.npoints = 3;

                sq.directions[LEFT]  = 0x06;   /* 1,2 */
                sq.directions[RIGHT] = 0x03;   /* 0,1 */
                sq.directions[DOWN]  = 0x05;   /* 0,2 */
                sq.directions[UP]    = 0;      /* invalid move */

                /*
                 * Up-pointing triangle: both the down diagonals go
                 * down, and the up ones go left and right.
                 */
                sq.directions[DOWN_LEFT] = sq.directions[DOWN_RIGHT] =
                    sq.directions[DOWN];
                sq.directions[UP_LEFT] = sq.directions[LEFT];
                sq.directions[UP_RIGHT] = sq.directions[RIGHT];

                sq.flip = FALSE;

                if (firstix < 0)
                    firstix = ix;
                ix -= firstix;
                sq.tetra_class = ((row+(ix&1)) & 2) ^ (ix & 3);

                callback(ctx, &sq);
            }
        }
    }
}

static int grid_area(int d1, int d2, int order)
{
    /*
     * An NxM grid of squares has NM squares in it.
     * 
     * A grid of triangles with dimensions A and B has a total of
     * A^2 + B^2 + 4AB triangles in it. (You can divide it up into
     * a side-A triangle containing A^2 subtriangles, a side-B
     * triangle containing B^2, and two congruent parallelograms,
     * each with side lengths A and B, each therefore containing AB
     * two-triangle rhombuses.)
     */
    if (order == 4)
        return d1 * d2;
    else
        return d1*d1 + d2*d2 + 4*d1*d2;
}

struct grid_data {
    int *gridptrs[4];
    int nsquares[4];
    int nclasses;
    int squareindex;
};

static void classify_grid_square_callback(void *ctx, struct grid_square *sq)
{
    struct grid_data *data = (struct grid_data *)ctx;
    int thisclass;

    if (data->nclasses == 4)
	thisclass = sq->tetra_class;
    else if (data->nclasses == 2)
	thisclass = sq->flip;
    else
	thisclass = 0;

    data->gridptrs[thisclass][data->nsquares[thisclass]++] =
	data->squareindex++;
}

char *new_game_seed(game_params *params)
{
    struct grid_data data;
    int i, j, k, m, area, facesperclass;
    int *flags;
    char *seed, *p;

    /*
     * Enumerate the grid squares, dividing them into equivalence
     * classes as appropriate. (For the tetrahedron, there is one
     * equivalence class for each face; for the octahedron there
     * are two classes; for the other two solids there's only one.)
     */

    area = grid_area(params->d1, params->d2, solids[params->solid]->order);
    if (params->solid == TETRAHEDRON)
	data.nclasses = 4;
    else if (params->solid == OCTAHEDRON)
	data.nclasses = 2;
    else
	data.nclasses = 1;
    data.gridptrs[0] = snewn(data.nclasses * area, int);
    for (i = 0; i < data.nclasses; i++) {
	data.gridptrs[i] = data.gridptrs[0] + i * area;
	data.nsquares[i] = 0;
    }
    data.squareindex = 0;
    enum_grid_squares(params, classify_grid_square_callback, &data);

    facesperclass = solids[params->solid]->nfaces / data.nclasses;

    for (i = 0; i < data.nclasses; i++)
	assert(data.nsquares[i] >= facesperclass);
    assert(data.squareindex == area);

    /*
     * So now we know how many faces to allocate in each class. Get
     * on with it.
     */
    flags = snewn(area, int);
    for (i = 0; i < area; i++)
	flags[i] = FALSE;

    for (i = 0; i < data.nclasses; i++) {
	for (j = 0; j < facesperclass; j++) {
	    unsigned long divisor = RAND_MAX / data.nsquares[i];
	    unsigned long max = divisor * data.nsquares[i];
	    unsigned long n;

	    do {
		n = rand();
	    } while (n >= max);

	    n /= divisor;

	    assert(!flags[data.gridptrs[i][n]]);
	    flags[data.gridptrs[i][n]] = TRUE;

	    /*
	     * Move everything else up the array. I ought to use a
	     * better data structure for this, but for such small
	     * numbers it hardly seems worth the effort.
	     */
	    while ((int)n < data.nsquares[i]-1) {
		data.gridptrs[i][n] = data.gridptrs[i][n+1];
		n++;
	    }
	    data.nsquares[i]--;
	}
    }

    /*
     * Now we know precisely which squares are blue. Encode this
     * information in hex. While we're looping over this, collect
     * the non-blue squares into a list in the now-unused gridptrs
     * array.
     */
    seed = snewn(area / 4 + 40, char);
    p = seed;
    j = 0;
    k = 8;
    m = 0;
    for (i = 0; i < area; i++) {
	if (flags[i]) {
	    j |= k;
	} else {
	    data.gridptrs[0][m++] = i;
	}
	k >>= 1;
	if (!k) {
	    *p++ = "0123456789ABCDEF"[j];
	    k = 8;
	    j = 0;
	}
    }
    if (k != 8)
	*p++ = "0123456789ABCDEF"[j];

    /*
     * Choose a non-blue square for the polyhedron.
     */
    {
	unsigned long divisor = RAND_MAX / m;
	unsigned long max = divisor * m;
	unsigned long n;

	do {
	    n = rand();
	} while (n >= max);

	n /= divisor;

	sprintf(p, ":%d", data.gridptrs[0][n]);
    }

    sfree(data.gridptrs[0]);
    sfree(flags);

    return seed;
}

static void add_grid_square_callback(void *ctx, struct grid_square *sq)
{
    game_state *state = (game_state *)ctx;

    state->squares[state->nsquares] = *sq;   /* structure copy */
    state->squares[state->nsquares].blue = FALSE;
    state->nsquares++;
}

static int lowest_face(const struct solid *solid)
{
    int i, j, best;
    float zmin;

    best = 0;
    zmin = 0.0;
    for (i = 0; i < solid->nfaces; i++) {
        float z = 0;

        for (j = 0; j < solid->order; j++) {
            int f = solid->faces[i*solid->order + j];
            z += solid->vertices[f*3+2];
        }

        if (i == 0 || zmin > z) {
            zmin = z;
            best = i;
        }
    }

    return best;
}

static int align_poly(const struct solid *solid, struct grid_square *sq,
                      int *pkey)
{
    float zmin;
    int i, j;
    int flip = (sq->flip ? -1 : +1);

    /*
     * First, find the lowest z-coordinate present in the solid.
     */
    zmin = 0.0;
    for (i = 0; i < solid->nvertices; i++)
        if (zmin > solid->vertices[i*3+2])
            zmin = solid->vertices[i*3+2];

    /*
     * Now go round the grid square. For each point in the grid
     * square, we're looking for a point of the polyhedron with the
     * same x- and y-coordinates (relative to the square's centre),
     * and z-coordinate equal to zmin (near enough).
     */
    for (j = 0; j < sq->npoints; j++) {
        int matches, index;

        matches = 0;
        index = -1;

        for (i = 0; i < solid->nvertices; i++) {
            float dist = 0;

            dist += SQ(solid->vertices[i*3+0] * flip - sq->points[j*2+0] + sq->x);
            dist += SQ(solid->vertices[i*3+1] * flip - sq->points[j*2+1] + sq->y);
            dist += SQ(solid->vertices[i*3+2] - zmin);

            if (dist < 0.1) {
                matches++;
                index = i;
            }
        }

        if (matches != 1 || index < 0)
            return FALSE;
        pkey[j] = index;
    }

    return TRUE;
}

static void flip_poly(struct solid *solid, int flip)
{
    int i;

    if (flip) {
        for (i = 0; i < solid->nvertices; i++) {
            solid->vertices[i*3+0] *= -1;
            solid->vertices[i*3+1] *= -1;
        }
        for (i = 0; i < solid->nfaces; i++) {
            solid->normals[i*3+0] *= -1;
            solid->normals[i*3+1] *= -1;
        }
    }
}

static struct solid *transform_poly(const struct solid *solid, int flip,
                                    int key0, int key1, float angle)
{
    struct solid *ret = snew(struct solid);
    float vx, vy, ax, ay;
    float vmatrix[9], amatrix[9], vmatrix2[9];
    int i;

    *ret = *solid;                     /* structure copy */

    flip_poly(ret, flip);

    /*
     * Now rotate the polyhedron through the given angle. We must
     * rotate about the Z-axis to bring the two vertices key0 and
     * key1 into horizontal alignment, then rotate about the
     * X-axis, then rotate back again.
     */
    vx = ret->vertices[key1*3+0] - ret->vertices[key0*3+0];
    vy = ret->vertices[key1*3+1] - ret->vertices[key0*3+1];
    assert(APPROXEQ(vx*vx + vy*vy, 1.0));

    vmatrix[0] =  vx; vmatrix[3] = vy; vmatrix[6] = 0;
    vmatrix[1] = -vy; vmatrix[4] = vx; vmatrix[7] = 0;
    vmatrix[2] =   0; vmatrix[5] =  0; vmatrix[8] = 1;

    ax = (float)cos(angle);
    ay = (float)sin(angle);

    amatrix[0] = 1; amatrix[3] =   0; amatrix[6] =  0;
    amatrix[1] = 0; amatrix[4] =  ax; amatrix[7] = ay;
    amatrix[2] = 0; amatrix[5] = -ay; amatrix[8] = ax;

    memcpy(vmatrix2, vmatrix, sizeof(vmatrix));
    vmatrix2[1] = vy;
    vmatrix2[3] = -vy;

    for (i = 0; i < ret->nvertices; i++) {
        MATMUL(ret->vertices + 3*i, vmatrix, ret->vertices + 3*i);
        MATMUL(ret->vertices + 3*i, amatrix, ret->vertices + 3*i);
        MATMUL(ret->vertices + 3*i, vmatrix2, ret->vertices + 3*i);
    }
    for (i = 0; i < ret->nfaces; i++) {
        MATMUL(ret->normals + 3*i, vmatrix, ret->normals + 3*i);
        MATMUL(ret->normals + 3*i, amatrix, ret->normals + 3*i);
        MATMUL(ret->normals + 3*i, vmatrix2, ret->normals + 3*i);
    }

    return ret;
}

game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);
    int area;

    state->params = *params;           /* structure copy */
    state->solid = solids[params->solid];

    area = grid_area(params->d1, params->d2, state->solid->order);
    state->squares = snewn(area, struct grid_square);
    state->nsquares = 0;
    enum_grid_squares(params, add_grid_square_callback, state);
    assert(state->nsquares == area);

    state->facecolours = snewn(state->solid->nfaces, int);
    memset(state->facecolours, 0, state->solid->nfaces * sizeof(int));

    /*
     * Set up the blue squares and polyhedron position according to
     * the game seed.
     */
    {
	char *p = seed;
	int i, j, v;

	j = 8;
	v = 0;
	for (i = 0; i < state->nsquares; i++) {
	    if (j == 8) {
		v = *p++;
		if (v >= '0' && v <= '9')
		    v -= '0';
		else if (v >= 'A' && v <= 'F')
		    v -= 'A' - 10;
		else if (v >= 'a' && v <= 'f')
		    v -= 'a' - 10;
		else
		    break;
	    }
	    if (v & j)
		state->squares[i].blue = TRUE;
	    j >>= 1;
	    if (j == 0)
		j = 8;
	}

	if (*p == ':')
	    p++;

	state->current = atoi(p);
	if (state->current < 0 || state->current >= state->nsquares)
	    state->current = 0;	       /* got to do _something_ */
    }

    /*
     * Align the polyhedron with its grid square and determine
     * initial key points.
     */
    {
        int pkey[4];
        int ret;

        ret = align_poly(state->solid, &state->squares[state->current], pkey);
        assert(ret);

        state->dpkey[0] = state->spkey[0] = pkey[0];
        state->dpkey[1] = state->spkey[0] = pkey[1];
        state->dgkey[0] = state->sgkey[0] = 0;
        state->dgkey[1] = state->sgkey[0] = 1;
    }

    state->previous = state->current;
    state->angle = 0.0;
    state->completed = FALSE;
    state->movecount = 0;

    return state;
}

game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->params = state->params;           /* structure copy */
    ret->solid = state->solid;
    ret->facecolours = snewn(ret->solid->nfaces, int);
    memcpy(ret->facecolours, state->facecolours,
           ret->solid->nfaces * sizeof(int));
    ret->nsquares = state->nsquares;
    ret->squares = snewn(ret->nsquares, struct grid_square);
    memcpy(ret->squares, state->squares,
           ret->nsquares * sizeof(struct grid_square));
    ret->dpkey[0] = state->dpkey[0];
    ret->dpkey[1] = state->dpkey[1];
    ret->dgkey[0] = state->dgkey[0];
    ret->dgkey[1] = state->dgkey[1];
    ret->spkey[0] = state->spkey[0];
    ret->spkey[1] = state->spkey[1];
    ret->sgkey[0] = state->sgkey[0];
    ret->sgkey[1] = state->sgkey[1];
    ret->previous = state->previous;
    ret->angle = state->angle;
    ret->completed = state->completed;
    ret->movecount = state->movecount;

    return ret;
}

void free_game(game_state *state)
{
    sfree(state);
}

game_state *make_move(game_state *from, int x, int y, int button)
{
    int direction;
    int pkey[2], skey[2], dkey[2];
    float points[4];
    game_state *ret;
    float angle;
    int i, j, dest, mask;
    struct solid *poly;

    /*
     * All moves are made with the cursor keys.
     */
    if (button == CURSOR_UP)
        direction = UP;
    else if (button == CURSOR_DOWN)
        direction = DOWN;
    else if (button == CURSOR_LEFT)
        direction = LEFT;
    else if (button == CURSOR_RIGHT)
        direction = RIGHT;
    else if (button == CURSOR_UP_LEFT)
        direction = UP_LEFT;
    else if (button == CURSOR_DOWN_LEFT)
        direction = DOWN_LEFT;
    else if (button == CURSOR_UP_RIGHT)
        direction = UP_RIGHT;
    else if (button == CURSOR_DOWN_RIGHT)
        direction = DOWN_RIGHT;
    else
        return NULL;

    /*
     * Find the two points in the current grid square which
     * correspond to this move.
     */
    mask = from->squares[from->current].directions[direction];
    if (mask == 0)
        return NULL;
    for (i = j = 0; i < from->squares[from->current].npoints; i++)
        if (mask & (1 << i)) {
            points[j*2] = from->squares[from->current].points[i*2];
            points[j*2+1] = from->squares[from->current].points[i*2+1];
            skey[j] = i;
            j++;
        }
    assert(j == 2);

    /*
     * Now find the other grid square which shares those points.
     * This is our move destination.
     */
    dest = -1;
    for (i = 0; i < from->nsquares; i++)
        if (i != from->current) {
            int match = 0;
            float dist;

            for (j = 0; j < from->squares[i].npoints; j++) {
                dist = (SQ(from->squares[i].points[j*2] - points[0]) +
                        SQ(from->squares[i].points[j*2+1] - points[1]));
                if (dist < 0.1)
                    dkey[match++] = j;
                dist = (SQ(from->squares[i].points[j*2] - points[2]) +
                        SQ(from->squares[i].points[j*2+1] - points[3]));
                if (dist < 0.1)
                    dkey[match++] = j;
            }

            if (match == 2) {
                dest = i;
                break;
            }
        }

    if (dest < 0)
        return NULL;

    ret = dup_game(from);
    ret->current = i;

    /*
     * So we know what grid square we're aiming for, and we also
     * know the two key points (as indices in both the source and
     * destination grid squares) which are invariant between source
     * and destination.
     * 
     * Next we must roll the polyhedron on to that square. So we
     * find the indices of the key points within the polyhedron's
     * vertex array, then use those in a call to transform_poly,
     * and align the result on the new grid square.
     */
    {
        int all_pkey[4];
        align_poly(from->solid, &from->squares[from->current], all_pkey);
        pkey[0] = all_pkey[skey[0]];
        pkey[1] = all_pkey[skey[1]];
        /*
         * Now pkey[0] corresponds to skey[0] and dkey[0], and
         * likewise [1].
         */
    }

    /*
     * Now find the angle through which to rotate the polyhedron.
     * Do this by finding the two faces that share the two vertices
     * we've found, and taking the dot product of their normals.
     */
    {
        int f[2], nf = 0;
        float dp;

        for (i = 0; i < from->solid->nfaces; i++) {
            int match = 0;
            for (j = 0; j < from->solid->order; j++)
                if (from->solid->faces[i*from->solid->order + j] == pkey[0] ||
                    from->solid->faces[i*from->solid->order + j] == pkey[1])
                    match++;
            if (match == 2) {
                assert(nf < 2);
                f[nf++] = i;
            }
        }

        assert(nf == 2);

        dp = 0;
        for (i = 0; i < 3; i++)
            dp += (from->solid->normals[f[0]*3+i] *
                   from->solid->normals[f[1]*3+i]);
        angle = (float)acos(dp);
    }

    /*
     * Now transform the polyhedron. We aren't entirely sure
     * whether we need to rotate through angle or -angle, and the
     * simplest way round this is to try both and see which one
     * aligns successfully!
     * 
     * Unfortunately, _both_ will align successfully if this is a
     * cube, which won't tell us anything much. So for that
     * particular case, I resort to gross hackery: I simply negate
     * the angle before trying the alignment, depending on the
     * direction. Which directions work which way is determined by
     * pure trial and error. I said it was gross :-/
     */
    {
        int all_pkey[4];
        int success;

        if (from->solid->order == 4 && direction == UP)
            angle = -angle;            /* HACK */

        poly = transform_poly(from->solid,
                              from->squares[from->current].flip,
                              pkey[0], pkey[1], angle);
        flip_poly(poly, from->squares[ret->current].flip);
        success = align_poly(poly, &from->squares[ret->current], all_pkey);

        if (!success) {
            angle = -angle;
            poly = transform_poly(from->solid,
                                  from->squares[from->current].flip,
                                  pkey[0], pkey[1], angle);
            flip_poly(poly, from->squares[ret->current].flip);
            success = align_poly(poly, &from->squares[ret->current], all_pkey);
        }

        assert(success);
    }

    /*
     * Now we have our rotated polyhedron, which we expect to be
     * exactly congruent to the one we started with - but with the
     * faces permuted. So we map that congruence and thereby figure
     * out how to permute the faces as a result of the polyhedron
     * having rolled.
     */
    {
        int *newcolours = snewn(from->solid->nfaces, int);

        for (i = 0; i < from->solid->nfaces; i++)
            newcolours[i] = -1;

        for (i = 0; i < from->solid->nfaces; i++) {
            int nmatch = 0;

            /*
             * Now go through the transformed polyhedron's faces
             * and figure out which one's normal is approximately
             * equal to this one.
             */
            for (j = 0; j < poly->nfaces; j++) {
                float dist;
                int k;

                dist = 0;

                for (k = 0; k < 3; k++)
                    dist += SQ(poly->normals[j*3+k] -
                               from->solid->normals[i*3+k]);

                if (APPROXEQ(dist, 0)) {
                    nmatch++;
                    newcolours[i] = ret->facecolours[j];
                }
            }

            assert(nmatch == 1);
        }

        for (i = 0; i < from->solid->nfaces; i++)
            assert(newcolours[i] != -1);

        sfree(ret->facecolours);
        ret->facecolours = newcolours;
    }

    /*
     * And finally, swap the colour between the bottom face of the
     * polyhedron and the face we've just landed on.
     * 
     * We don't do this if the game is already complete, since we
     * allow the user to roll the fully blue polyhedron around the
     * grid as a feeble reward.
     */
    if (!ret->completed) {
        i = lowest_face(from->solid);
        j = ret->facecolours[i];
        ret->facecolours[i] = ret->squares[ret->current].blue;
        ret->squares[ret->current].blue = j;

        /*
         * Detect game completion.
         */
        j = 0;
        for (i = 0; i < ret->solid->nfaces; i++)
            if (ret->facecolours[i])
                j++;
        if (j == ret->solid->nfaces)
            ret->completed = TRUE;
    }

    sfree(poly);

    /*
     * Align the normal polyhedron with its grid square, to get key
     * points for non-animated display.
     */
    {
        int pkey[4];
        int success;

        success = align_poly(ret->solid, &ret->squares[ret->current], pkey);
        assert(success);

        ret->dpkey[0] = pkey[0];
        ret->dpkey[1] = pkey[1];
        ret->dgkey[0] = 0;
        ret->dgkey[1] = 1;
    }


    ret->spkey[0] = pkey[0];
    ret->spkey[1] = pkey[1];
    ret->sgkey[0] = skey[0];
    ret->sgkey[1] = skey[1];
    ret->previous = from->current;
    ret->angle = angle;
    ret->movecount++;

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct bbox {
    float l, r, u, d;
};

struct game_drawstate {
    int ox, oy;                        /* pixel position of float origin */
};

static void find_bbox_callback(void *ctx, struct grid_square *sq)
{
    struct bbox *bb = (struct bbox *)ctx;
    int i;

    for (i = 0; i < sq->npoints; i++) {
        if (bb->l > sq->points[i*2]) bb->l = sq->points[i*2];
        if (bb->r < sq->points[i*2]) bb->r = sq->points[i*2];
        if (bb->u > sq->points[i*2+1]) bb->u = sq->points[i*2+1];
        if (bb->d < sq->points[i*2+1]) bb->d = sq->points[i*2+1];
    }
}

static struct bbox find_bbox(game_params *params)
{
    struct bbox bb;

    /*
     * These should be hugely more than the real bounding box will
     * be.
     */
    bb.l = 2.0F * (params->d1 + params->d2);
    bb.r = -2.0F * (params->d1 + params->d2);
    bb.u = 2.0F * (params->d1 + params->d2);
    bb.d = -2.0F * (params->d1 + params->d2);
    enum_grid_squares(params, find_bbox_callback, &bb);

    return bb;
}

void game_size(game_params *params, int *x, int *y)
{
    struct bbox bb = find_bbox(params);
    *x = (int)((bb.r - bb.l + 2*solids[params->solid]->border) * GRID_SCALE);
    *y = (int)((bb.d - bb.u + 2*solids[params->solid]->border) * GRID_SCALE);
}

float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_BORDER * 3 + 0] = 0.0;
    ret[COL_BORDER * 3 + 1] = 0.0;
    ret[COL_BORDER * 3 + 2] = 0.0;

    ret[COL_BLUE * 3 + 0] = 0.0;
    ret[COL_BLUE * 3 + 1] = 0.0;
    ret[COL_BLUE * 3 + 2] = 1.0;

    *ncolours = NCOLOURS;
    return ret;
}

game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    struct bbox bb = find_bbox(&state->params);

    ds->ox = (int)(-(bb.l - state->solid->border) * GRID_SCALE);
    ds->oy = (int)(-(bb.u - state->solid->border) * GRID_SCALE);

    return ds;
}

void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds);
}

void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, float animtime)
{
    int i, j;
    struct bbox bb = find_bbox(&state->params);
    struct solid *poly;
    int *pkey, *gkey;
    float t[3];
    float angle;
    game_state *newstate;
    int square;

    draw_rect(fe, 0, 0, (int)((bb.r-bb.l+2.0F) * GRID_SCALE),
              (int)((bb.d-bb.u+2.0F) * GRID_SCALE), COL_BACKGROUND);

    if (oldstate && oldstate->movecount > state->movecount) {
        game_state *t;

        /*
         * This is an Undo. So reverse the order of the states, and
         * run the roll timer backwards.
         */
        t = oldstate;
        oldstate = state;
        state = t;

        animtime = ROLLTIME - animtime;
    }

    if (!oldstate) {
        oldstate = state;
        angle = 0.0;
        square = state->current;
        pkey = state->dpkey;
        gkey = state->dgkey;
    } else {
        angle = state->angle * animtime / ROLLTIME;
        square = state->previous;
        pkey = state->spkey;
        gkey = state->sgkey;
    }
    newstate = state;
    state = oldstate;

    for (i = 0; i < state->nsquares; i++) {
        int coords[8];

        for (j = 0; j < state->squares[i].npoints; j++) {
            coords[2*j] = ((int)(state->squares[i].points[2*j] * GRID_SCALE)
			   + ds->ox);
            coords[2*j+1] = ((int)(state->squares[i].points[2*j+1]*GRID_SCALE)
			     + ds->oy);
        }

        draw_polygon(fe, coords, state->squares[i].npoints, TRUE,
                     state->squares[i].blue ? COL_BLUE : COL_BACKGROUND);
        draw_polygon(fe, coords, state->squares[i].npoints, FALSE, COL_BORDER);
    }

    /*
     * Now compute and draw the polyhedron.
     */
    poly = transform_poly(state->solid, state->squares[square].flip,
                          pkey[0], pkey[1], angle);

    /*
     * Compute the translation required to align the two key points
     * on the polyhedron with the same key points on the current
     * face.
     */
    for (i = 0; i < 3; i++) {
        float tc = 0.0;

        for (j = 0; j < 2; j++) {
            float grid_coord;

            if (i < 2) {
                grid_coord =
                    state->squares[square].points[gkey[j]*2+i];
            } else {
                grid_coord = 0.0;
            }

            tc += (grid_coord - poly->vertices[pkey[j]*3+i]);
        }

        t[i] = tc / 2;
    }
    for (i = 0; i < poly->nvertices; i++)
        for (j = 0; j < 3; j++)
            poly->vertices[i*3+j] += t[j];

    /*
     * Now actually draw each face.
     */
    for (i = 0; i < poly->nfaces; i++) {
        float points[8];
        int coords[8];

        for (j = 0; j < poly->order; j++) {
            int f = poly->faces[i*poly->order + j];
            points[j*2] = (poly->vertices[f*3+0] -
                           poly->vertices[f*3+2] * poly->shear);
            points[j*2+1] = (poly->vertices[f*3+1] -
                             poly->vertices[f*3+2] * poly->shear);
        }

        for (j = 0; j < poly->order; j++) {
            coords[j*2] = (int)(points[j*2] * GRID_SCALE) + ds->ox;
            coords[j*2+1] = (int)(points[j*2+1] * GRID_SCALE) + ds->oy;
        }

        /*
         * Find out whether these points are in a clockwise or
         * anticlockwise arrangement. If the latter, discard the
         * face because it's facing away from the viewer.
         *
         * This would involve fiddly winding-number stuff for a
         * general polygon, but for the simple parallelograms we'll
         * be seeing here, all we have to do is check whether the
         * corners turn right or left. So we'll take the vector
         * from point 0 to point 1, turn it right 90 degrees,
         * and check the sign of the dot product with that and the
         * next vector (point 1 to point 2).
         */
        {
            float v1x = points[2]-points[0];
            float v1y = points[3]-points[1];
            float v2x = points[4]-points[2];
            float v2y = points[5]-points[3];
            float dp = v1x * v2y - v1y * v2x;

            if (dp <= 0)
                continue;
        }

        draw_polygon(fe, coords, poly->order, TRUE,
                     state->facecolours[i] ? COL_BLUE : COL_BACKGROUND);
        draw_polygon(fe, coords, poly->order, FALSE, COL_BORDER);
    }
    sfree(poly);

    draw_update(fe, 0, 0, (int)((bb.r-bb.l+2.0F) * GRID_SCALE),
                (int)((bb.d-bb.u+2.0F) * GRID_SCALE));
}

float game_anim_length(game_state *oldstate, game_state *newstate)
{
    return ROLLTIME;
}
