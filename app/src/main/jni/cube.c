/*
 * cube.c: Cube game.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

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

static const struct solid s_tetrahedron = {
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

static const struct solid s_cube = {
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

static const struct solid s_octahedron = {
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

static const struct solid s_icosahedron = {
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
    &s_tetrahedron, &s_cube, &s_octahedron, &s_icosahedron
};

enum {
    COL_BACKGROUND,
    COL_BORDER,
    COL_BLUE,
    NCOLOURS
};

enum { LEFT, RIGHT, UP, DOWN, UP_LEFT, UP_RIGHT, DOWN_LEFT, DOWN_RIGHT };

#define PREFERRED_GRID_SCALE 48
#define GRID_SCALE (ds->gridscale)
#define ROLLTIME 0.13F

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

typedef struct game_grid game_grid;
struct game_grid {
    int refcount;
    struct grid_square *squares;
    int nsquares;
};

#define SET_SQUARE(state, i, val) \
    ((state)->bluemask[(i)/32] &= ~(1 << ((i)%32)), \
     (state)->bluemask[(i)/32] |= ((!!val) << ((i)%32)))
#define GET_SQUARE(state, i) \
    (((state)->bluemask[(i)/32] >> ((i)%32)) & 1)

struct game_state {
    struct game_params params;
    const struct solid *solid;
    int *facecolours;
    game_grid *grid;
    unsigned long *bluemask;
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

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->solid = CUBE;
    ret->d1 = 4;
    ret->d2 = 4;

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret = snew(game_params);
    char *str;

    switch (i) {
      case 0:
        str = _("Cube");
        ret->solid = CUBE;
        ret->d1 = 4;
        ret->d2 = 4;
        break;
      case 1:
        str = _("Tetrahedron");
        ret->solid = TETRAHEDRON;
        ret->d1 = 1;
        ret->d2 = 2;
        break;
      case 2:
        str = _("Octahedron");
        ret->solid = OCTAHEDRON;
        ret->d1 = 2;
        ret->d2 = 2;
        break;
      case 3:
        str = _("Icosahedron");
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

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    switch (*string) {
      case 't': ret->solid = TETRAHEDRON; string++; break;
      case 'c': ret->solid = CUBE;        string++; break;
      case 'o': ret->solid = OCTAHEDRON;  string++; break;
      case 'i': ret->solid = ICOSAHEDRON; string++; break;
      default: break;
    }
    ret->d1 = ret->d2 = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->d2 = atoi(string);
    }
}

static char *encode_params(const game_params *params, int full)
{
    char data[256];

    assert(params->solid >= 0 && params->solid < 4);
    sprintf(data, "%c%dx%d", "tcoi"[params->solid], params->d1, params->d2);

    return dupstr(data);
}
typedef void (*egc_callback)(void *, struct grid_square *);

static void enum_grid_squares(const game_params *params, egc_callback callback,
                              void *ctx)
{
    const struct solid *solid = solids[params->solid];

    if (solid->order == 4) {
        int x, y;

	for (y = 0; y < params->d2; y++)
	    for (x = 0; x < params->d1; x++) {
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
            if (row < params->d2) {
                other = +1;
                rowlen = row + params->d1;
            } else {
                other = -1;
                rowlen = 2*params->d2 + params->d1 - row;
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
                    firstix = (ix - 1) & 3;
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

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[80];

    ret[0].name = _("Type of solid");
    ret[0].type = C_CHOICES;
    ret[0].sval = _(":Tetrahedron:Cube:Octahedron:Icosahedron");
    ret[0].ival = params->solid;

    ret[1].name = _("Width / top");
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->d1);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = _("Height / bottom");
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->d2);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->solid = cfg[0].ival;
    ret->d1 = atoi(cfg[1].sval);
    ret->d2 = atoi(cfg[2].sval);

    return ret;
}

static void count_grid_square_callback(void *ctx, struct grid_square *sq)
{
    int *classes = (int *)ctx;
    int thisclass;

    if (classes[4] == 4)
	thisclass = sq->tetra_class;
    else if (classes[4] == 2)
	thisclass = sq->flip;
    else
	thisclass = 0;

    classes[thisclass]++;
}

static char *validate_params(const game_params *params, int full)
{
    int classes[5];
    int i;

    if (params->solid < 0 || params->solid >= lenof(solids))
	return _("Unrecognised solid type");

    if (solids[params->solid]->order == 4) {
	if (params->d1 <= 0 || params->d2 <= 0)
	    return _("Both grid dimensions must be greater than zero");
    } else {
	if (params->d1 <= 0 && params->d2 <= 0)
	    return _("At least one grid dimension must be greater than zero");
    }

    for (i = 0; i < 4; i++)
	classes[i] = 0;
    if (params->solid == TETRAHEDRON)
	classes[4] = 4;
    else if (params->solid == OCTAHEDRON)
	classes[4] = 2;
    else
	classes[4] = 1;
    enum_grid_squares(params, count_grid_square_callback, classes);

    for (i = 0; i < classes[4]; i++)
	if (classes[i] < solids[params->solid]->nfaces / classes[4])
	    return _("Not enough grid space to place all blue faces");

    if (grid_area(params->d1, params->d2, solids[params->solid]->order) <
	solids[params->solid]->nfaces + 1)
	return _("Not enough space to place the solid on an empty square");

    return NULL;
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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    struct grid_data data;
    int i, j, k, m, area, facesperclass;
    int *flags;
    char *desc, *p;

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
            int n = random_upto(rs, data.nsquares[i]);

	    assert(!flags[data.gridptrs[i][n]]);
	    flags[data.gridptrs[i][n]] = TRUE;

	    /*
	     * Move everything else up the array. I ought to use a
	     * better data structure for this, but for such small
	     * numbers it hardly seems worth the effort.
	     */
	    while (n < data.nsquares[i]-1) {
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
    desc = snewn(area / 4 + 40, char);
    p = desc;
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
    sprintf(p, ",%d", data.gridptrs[0][random_upto(rs, m)]);

    sfree(data.gridptrs[0]);
    sfree(flags);

    return desc;
}

static void add_grid_square_callback(void *ctx, struct grid_square *sq)
{
    game_grid *grid = (game_grid *)ctx;

    grid->squares[grid->nsquares++] = *sq;   /* structure copy */
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

static char *validate_desc(const game_params *params, const char *desc)
{
    int area = grid_area(params->d1, params->d2, solids[params->solid]->order);
    int i, j;

    i = (area + 3) / 4;
    for (j = 0; j < i; j++) {
	int c = desc[j];
	if (c >= '0' && c <= '9') continue;
	if (c >= 'A' && c <= 'F') continue;
	if (c >= 'a' && c <= 'f') continue;
	return _("Not enough hex digits at start of string");
	/* NB if desc[j]=='\0' that will also be caught here, so we're safe */
    }

    if (desc[i] != ',')
	return _("Expected ',' after hex digits");

    i++;
    do {
	if (desc[i] < '0' || desc[i] > '9')
	    return _("Expected decimal integer after ','");
	i++;
    } while (desc[i]);

    return NULL;
}

#ifdef ANDROID
static void android_request_keys(const game_params *params)
{
    android_keys("", ANDROID_ARROWS_ONLY);
}
#endif

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_grid *grid = snew(game_grid);
    game_state *state = snew(game_state);
    int area;

    state->params = *params;           /* structure copy */
    state->solid = solids[params->solid];

    area = grid_area(params->d1, params->d2, state->solid->order);
    grid->squares = snewn(area, struct grid_square);
    grid->nsquares = 0;
    enum_grid_squares(params, add_grid_square_callback, grid);
    assert(grid->nsquares == area);
    state->grid = grid;
    grid->refcount = 1;

    state->facecolours = snewn(state->solid->nfaces, int);
    memset(state->facecolours, 0, state->solid->nfaces * sizeof(int));

    state->bluemask = snewn((state->grid->nsquares + 31) / 32, unsigned long);
    memset(state->bluemask, 0, (state->grid->nsquares + 31) / 32 *
	   sizeof(unsigned long));

    /*
     * Set up the blue squares and polyhedron position according to
     * the game description.
     */
    {
	const char *p = desc;
	int i, j, v;

	j = 8;
	v = 0;
	for (i = 0; i < state->grid->nsquares; i++) {
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
		SET_SQUARE(state, i, TRUE);
	    j >>= 1;
	    if (j == 0)
		j = 8;
	}

	if (*p == ',')
	    p++;

	state->current = atoi(p);
	if (state->current < 0 || state->current >= state->grid->nsquares)
	    state->current = 0;	       /* got to do _something_ */
    }

    /*
     * Align the polyhedron with its grid square and determine
     * initial key points.
     */
    {
        int pkey[4];
        int ret;

        ret = align_poly(state->solid, &state->grid->squares[state->current], pkey);
        assert(ret);

        state->dpkey[0] = state->spkey[0] = pkey[0];
        state->dpkey[1] = state->spkey[0] = pkey[1];
        state->dgkey[0] = state->sgkey[0] = 0;
        state->dgkey[1] = state->sgkey[0] = 1;
    }

    state->previous = state->current;
    state->angle = 0.0;
    state->completed = 0;
    state->movecount = 0;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->params = state->params;           /* structure copy */
    ret->solid = state->solid;
    ret->facecolours = snewn(ret->solid->nfaces, int);
    memcpy(ret->facecolours, state->facecolours,
           ret->solid->nfaces * sizeof(int));
    ret->current = state->current;
    ret->grid = state->grid;
    ret->grid->refcount++;
    ret->bluemask = snewn((ret->grid->nsquares + 31) / 32, unsigned long);
    memcpy(ret->bluemask, state->bluemask, (ret->grid->nsquares + 31) / 32 *
	   sizeof(unsigned long));
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

static void free_game(game_state *state)
{
    if (--state->grid->refcount <= 0) {
	sfree(state->grid->squares);
	sfree(state->grid);
    }
    sfree(state->bluemask);
    sfree(state->facecolours);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
    return NULL;
}

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

static game_ui *new_ui(const game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
#ifdef ANDROID
    if (newstate->completed && oldstate && ! oldstate->completed) android_completed();
#endif
}

struct game_drawstate {
    float gridscale;
    int ox, oy;                        /* pixel position of float origin */
};

/*
 * Code shared between interpret_move() and execute_move().
 */
static int find_move_dest(const game_state *from, int direction,
			  int *skey, int *dkey)
{
    int mask, dest, i, j;
    float points[4];

    /*
     * Find the two points in the current grid square which
     * correspond to this move.
     */
    mask = from->grid->squares[from->current].directions[direction];
    if (mask == 0)
        return -1;
    for (i = j = 0; i < from->grid->squares[from->current].npoints; i++)
        if (mask & (1 << i)) {
            points[j*2] = from->grid->squares[from->current].points[i*2];
            points[j*2+1] = from->grid->squares[from->current].points[i*2+1];
            skey[j] = i;
            j++;
        }
    assert(j == 2);

    /*
     * Now find the other grid square which shares those points.
     * This is our move destination.
     */
    dest = -1;
    for (i = 0; i < from->grid->nsquares; i++)
        if (i != from->current) {
            int match = 0;
            float dist;

            for (j = 0; j < from->grid->squares[i].npoints; j++) {
                dist = (SQ(from->grid->squares[i].points[j*2] - points[0]) +
                        SQ(from->grid->squares[i].points[j*2+1] - points[1]));
                if (dist < 0.1)
                    dkey[match++] = j;
                dist = (SQ(from->grid->squares[i].points[j*2] - points[2]) +
                        SQ(from->grid->squares[i].points[j*2+1] - points[3]));
                if (dist < 0.1)
                    dkey[match++] = j;
            }

            if (match == 2) {
                dest = i;
                break;
            }
        }

    return dest;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int direction, mask, i;
    int skey[2], dkey[2];

    button = button & (~MOD_MASK | MOD_NUM_KEYPAD);

    /*
     * Moves can be made with the cursor keys or numeric keypad, or
     * alternatively you can left-click and the polyhedron will
     * move in the general direction of the mouse pointer.
     */
    if (button == CURSOR_UP || button == (MOD_NUM_KEYPAD | '8'))
        direction = UP;
    else if (button == CURSOR_DOWN || button == (MOD_NUM_KEYPAD | '2'))
        direction = DOWN;
    else if (button == CURSOR_LEFT || button == (MOD_NUM_KEYPAD | '4'))
        direction = LEFT;
    else if (button == CURSOR_RIGHT || button == (MOD_NUM_KEYPAD | '6'))
        direction = RIGHT;
    else if (button == (MOD_NUM_KEYPAD | '7'))
        direction = UP_LEFT;
    else if (button == (MOD_NUM_KEYPAD | '1'))
        direction = DOWN_LEFT;
    else if (button == (MOD_NUM_KEYPAD | '9'))
        direction = UP_RIGHT;
    else if (button == (MOD_NUM_KEYPAD | '3'))
        direction = DOWN_RIGHT;
    else if (button == LEFT_BUTTON) {
        /*
         * Find the bearing of the click point from the current
         * square's centre.
         */
        int cx, cy;
        double angle;

        cx = (int)(state->grid->squares[state->current].x * GRID_SCALE) + ds->ox;
        cy = (int)(state->grid->squares[state->current].y * GRID_SCALE) + ds->oy;

        if (x == cx && y == cy)
            return NULL;               /* clicked in exact centre!  */
        angle = atan2(y - cy, x - cx);

        /*
         * There are three possibilities.
         * 
         *  - This square is a square, so we choose between UP,
         *    DOWN, LEFT and RIGHT by dividing the available angle
         *    at the 45-degree points.
         * 
         *  - This square is an up-pointing triangle, so we choose
         *    between DOWN, LEFT and RIGHT by dividing into
         *    120-degree arcs.
         * 
         *  - This square is a down-pointing triangle, so we choose
         *    between UP, LEFT and RIGHT in the inverse manner.
         * 
         * Don't forget that since our y-coordinates increase
         * downwards, `angle' is measured _clockwise_ from the
         * x-axis, not anticlockwise as most mathematicians would
         * instinctively assume.
         */
        if (state->grid->squares[state->current].npoints == 4) {
            /* Square. */
            if (fabs(angle) > 3*PI/4)
                direction = LEFT;
            else if (fabs(angle) < PI/4)
                direction = RIGHT;
            else if (angle > 0)
                direction = DOWN;
            else
                direction = UP;
        } else if (state->grid->squares[state->current].directions[UP] == 0) {
            /* Up-pointing triangle. */
            if (angle < -PI/2 || angle > 5*PI/6)
                direction = LEFT;
            else if (angle > PI/6)
                direction = DOWN;
            else
                direction = RIGHT;
        } else {
            /* Down-pointing triangle. */
            assert(state->grid->squares[state->current].directions[DOWN] == 0);
            if (angle > PI/2 || angle < -5*PI/6)
                direction = LEFT;
            else if (angle < -PI/6)
                direction = UP;
            else
                direction = RIGHT;
        }
    } else
        return NULL;

    mask = state->grid->squares[state->current].directions[direction];
    if (mask == 0)
        return NULL;

    /*
     * Translate diagonal directions into orthogonal ones.
     */
    if (direction > DOWN) {
	for (i = LEFT; i <= DOWN; i++)
	    if (state->grid->squares[state->current].directions[i] == mask) {
		direction = i;
		break;
	    }
	assert(direction <= DOWN);
    }

    if (find_move_dest(state, direction, skey, dkey) < 0)
	return NULL;

    if (direction == LEFT)  return dupstr("L");
    if (direction == RIGHT) return dupstr("R");
    if (direction == UP)    return dupstr("U");
    if (direction == DOWN)  return dupstr("D");

    return NULL;		       /* should never happen */
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    float angle;
    struct solid *poly;
    int pkey[2];
    int skey[2], dkey[2];
    int i, j, dest;
    int direction;

    switch (*move) {
      case 'L': direction = LEFT; break;
      case 'R': direction = RIGHT; break;
      case 'U': direction = UP; break;
      case 'D': direction = DOWN; break;
      default: return NULL;
    }

    dest = find_move_dest(from, direction, skey, dkey);
    if (dest < 0)
        return NULL;

    ret = dup_game(from);
    ret->current = dest;

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
        align_poly(from->solid, &from->grid->squares[from->current], all_pkey);
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
                              from->grid->squares[from->current].flip,
                              pkey[0], pkey[1], angle);
        flip_poly(poly, from->grid->squares[ret->current].flip);
        success = align_poly(poly, &from->grid->squares[ret->current], all_pkey);

        if (!success) {
            sfree(poly);
            angle = -angle;
            poly = transform_poly(from->solid,
                                  from->grid->squares[from->current].flip,
                                  pkey[0], pkey[1], angle);
            flip_poly(poly, from->grid->squares[ret->current].flip);
            success = align_poly(poly, &from->grid->squares[ret->current], all_pkey);
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

    ret->movecount++;

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
        ret->facecolours[i] = GET_SQUARE(ret, ret->current);
        SET_SQUARE(ret, ret->current, j);

        /*
         * Detect game completion.
         */
        j = 0;
        for (i = 0; i < ret->solid->nfaces; i++)
            if (ret->facecolours[i])
                j++;
        if (j == ret->solid->nfaces) {
            ret->completed = ret->movecount;
        }

    }

    sfree(poly);

    /*
     * Align the normal polyhedron with its grid square, to get key
     * points for non-animated display.
     */
    {
        int pkey[4];
        int success;

        success = align_poly(ret->solid, &ret->grid->squares[ret->current], pkey);
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

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct bbox {
    float l, r, u, d;
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

static struct bbox find_bbox(const game_params *params)
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

#define XSIZE(gs, bb, solid) \
    ((int)(((bb).r - (bb).l + 2*(solid)->border) * gs))
#define YSIZE(gs, bb, solid) \
    ((int)(((bb).d - (bb).u + 2*(solid)->border) * gs))

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    struct bbox bb = find_bbox(params);

    *x = XSIZE(tilesize, bb, solids[params->solid]);
    *y = YSIZE(tilesize, bb, solids[params->solid]);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    struct bbox bb = find_bbox(params);

    ds->gridscale = (float)tilesize;
    ds->ox = (int)(-(bb.l - solids[params->solid]->border) * ds->gridscale);
    ds->oy = (int)(-(bb.u - solids[params->solid]->border) * ds->gridscale);
}

static float *game_colours(frontend *fe, int *ncolours)
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

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->ox = ds->oy = 0;
    ds->gridscale = 0.0F; /* not decided yet */

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, j;
    struct bbox bb = find_bbox(&state->params);
    struct solid *poly;
    const int *pkey, *gkey;
    float t[3];
    float angle;
    int square;

    draw_rect(dr, 0, 0, XSIZE(GRID_SCALE, bb, state->solid),
	      YSIZE(GRID_SCALE, bb, state->solid), COL_BACKGROUND);

    if (dir < 0) {
        const game_state *t;

        /*
         * This is an Undo. So reverse the order of the states, and
         * run the roll timer backwards.
         */
	assert(oldstate);

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
    state = oldstate;

    for (i = 0; i < state->grid->nsquares; i++) {
        int coords[8];

        for (j = 0; j < state->grid->squares[i].npoints; j++) {
            coords[2*j] = ((int)(state->grid->squares[i].points[2*j] * GRID_SCALE)
			   + ds->ox);
            coords[2*j+1] = ((int)(state->grid->squares[i].points[2*j+1]*GRID_SCALE)
			     + ds->oy);
        }

        draw_polygon(dr, coords, state->grid->squares[i].npoints,
                     GET_SQUARE(state, i) ? COL_BLUE : COL_BACKGROUND,
		     COL_BORDER);
    }

    /*
     * Now compute and draw the polyhedron.
     */
    poly = transform_poly(state->solid, state->grid->squares[square].flip,
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
                    state->grid->squares[square].points[gkey[j]*2+i];
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
            coords[j*2] = (int)floor(points[j*2] * GRID_SCALE) + ds->ox;
            coords[j*2+1] = (int)floor(points[j*2+1] * GRID_SCALE) + ds->oy;
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

        draw_polygon(dr, coords, poly->order,
                     state->facecolours[i] ? COL_BLUE : COL_BACKGROUND,
		     COL_BORDER);
    }
    sfree(poly);

    draw_update(dr, 0, 0, XSIZE(GRID_SCALE, bb, state->solid),
		YSIZE(GRID_SCALE, bb, state->solid));

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];

	if (state->completed) {
		strcpy(statusbuf, _("COMPLETED!"));
		strcpy(statusbuf+strlen(statusbuf), " ");
	} else statusbuf[0] = '\0';
	sprintf(statusbuf+strlen(statusbuf), _("Moves: %d"),
		(state->completed ? state->completed : state->movecount));

	status_bar(dr, statusbuf);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return ROLLTIME;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
}

#ifndef NO_PRINTING
static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}
#endif

#ifdef COMBINED
#define thegame cube
#endif

const struct game thegame = {
    "Cube", "games.cube", "cube",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    FALSE, solve_game,
    FALSE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    android_request_keys,
    NULL,  /* android_cursor_visibility */
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_GRID_SCALE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
#ifndef NO_PRINTING
    FALSE, FALSE, game_print_size, game_print,
#endif
    TRUE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};
