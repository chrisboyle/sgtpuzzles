/* penrose.c
 *
 * Penrose tile generator.
 *
 * Uses half-tile technique outlined on:
 *
 * http://tartarus.org/simon/20110412-penrose/penrose.xhtml
 */

#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "puzzles.h" /* for malloc routines, and PI */
#include "penrose.h"

/* -------------------------------------------------------
 * 36-degree basis vector arithmetic routines.
 */

/* Imagine drawing a
 * ten-point 'clock face' like this:
 *
 *                     -E
 *                 -D   |   A
 *                   \  |  /
 *              -C.   \ | /   ,B
 *                 `-._\|/_,-'
 *                 ,-' /|\ `-.
 *              -B'   / | \   `C
 *                   /  |  \
 *                 -A   |   D
 *                      E
 *
 * In case the ASCII art isn't clear, those are supposed to be ten
 * vectors of length 1, all sticking out from the origin at equal
 * angular spacing (hence 36 degrees). Our basis vectors are A,B,C,D (I
 * choose them to be symmetric about the x-axis so that the final
 * translation into 2d coordinates will also be symmetric, which I
 * think will avoid minor rounding uglinesses), so our vector
 * representation sets
 *
 *   A = (1,0,0,0)
 *   B = (0,1,0,0)
 *   C = (0,0,1,0)
 *   D = (0,0,0,1)
 *
 * The fifth vector E looks at first glance as if it needs to be
 * another basis vector, but in fact it doesn't, because it can be
 * represented in terms of the other four. Imagine starting from the
 * origin and following the path -A, +B, -C, +D: you'll find you've
 * traced four sides of a pentagram, and ended up one E-vector away
 * from the origin. So we have
 *
 *   E = (-1,1,-1,1)
 *
 * This tells us that we can rotate any vector in this system by 36
 * degrees: if we start with a*A + b*B + c*C + d*D, we want to end up
 * with a*B + b*C + c*D + d*E, and we substitute our identity for E to
 * turn that into a*B + b*C + c*D + d*(-A+B-C+D). In other words,
 *
 *   rotate_one_notch_clockwise(a,b,c,d) = (-d, d+a, -d+b, d+c)
 *
 * and you can verify for yourself that applying that operation
 * repeatedly starting with (1,0,0,0) cycles round ten vectors and
 * comes back to where it started.
 *
 * The other operation that may be required is to construct vectors
 * with lengths that are multiples of phi. That can be done by
 * observing that the vector C-B is parallel to E and has length 1/phi,
 * and the vector D-A is parallel to E and has length phi. So this
 * tells us that given any vector, we can construct one which points in
 * the same direction and is 1/phi or phi times its length, like this:
 *
 *   divide_by_phi(vector) = rotate(vector, 2) - rotate(vector, 3)
 *   multiply_by_phi(vector) = rotate(vector, 1) - rotate(vector, 4)
 *
 * where rotate(vector, n) means applying the above
 * rotate_one_notch_clockwise primitive n times. Expanding out the
 * applications of rotate gives the following direct representation in
 * terms of the vector coordinates:
 *
 *   divide_by_phi(a,b,c,d) = (b-d, c+d-b, a+b-c, c-a)
 *   multiply_by_phi(a,b,c,d) = (a+b-d, c+d, a+b, c+d-a)
 *
 * and you can verify for yourself that those two operations are
 * inverses of each other (as you'd hope!).
 *
 * Having done all of this, testing for equality between two vectors is
 * a trivial matter of comparing the four integer coordinates. (Which
 * it _wouldn't_ have been if we'd kept E as a fifth basis vector,
 * because then (-1,1,-1,1,0) and (0,0,0,0,1) would have had to be
 * considered identical. So leaving E out is vital.)
 */

struct vector { int a, b, c, d; };

static vector v_origin(void)
{
    vector v;
    v.a = v.b = v.c = v.d = 0;
    return v;
}

/* We start with a unit vector of B: this means we can easily
 * draw an isoceles triangle centred on the X axis. */
#ifdef TEST_VECTORS

static vector v_unit(void)
{
    vector v;

    v.b = 1;
    v.a = v.c = v.d = 0;
    return v;
}

#endif

#define COS54 0.5877852
#define SIN54 0.8090169
#define COS18 0.9510565
#define SIN18 0.3090169

/* These two are a bit rough-and-ready for now. Note that B/C are
 * 18 degrees from the x-axis, and A/D are 54 degrees. */
double v_x(vector *vs, int i)
{
    return (vs[i].a + vs[i].d) * COS54 +
           (vs[i].b + vs[i].c) * COS18;
}

double v_y(vector *vs, int i)
{
    return (vs[i].a - vs[i].d) * SIN54 +
           (vs[i].b - vs[i].c) * SIN18;

}

static vector v_trans(vector v, vector trans)
{
    v.a += trans.a;
    v.b += trans.b;
    v.c += trans.c;
    v.d += trans.d;
    return v;
}

static vector v_rotate_36(vector v)
{
    vector vv;
    vv.a = -v.d;
    vv.b =  v.d + v.a;
    vv.c = -v.d + v.b;
    vv.d =  v.d + v.c;
    return vv;
}

static vector v_rotate(vector v, int ang)
{
    int i;

    assert((ang % 36) == 0);
    while (ang < 0) ang += 360;
    ang = 360-ang;
    for (i = 0; i < (ang/36); i++)
        v = v_rotate_36(v);
    return v;
}

#ifdef TEST_VECTORS

static vector v_scale(vector v, int sc)
{
    v.a *= sc;
    v.b *= sc;
    v.c *= sc;
    v.d *= sc;
    return v;
}

#endif

static vector v_growphi(vector v)
{
    vector vv;
    vv.a = v.a + v.b - v.d;
    vv.b = v.c + v.d;
    vv.c = v.a + v.b;
    vv.d = v.c + v.d - v.a;
    return vv;
}

static vector v_shrinkphi(vector v)
{
    vector vv;
    vv.a = v.b - v.d;
    vv.b = v.c + v.d - v.b;
    vv.c = v.a + v.b - v.c;
    vv.d = v.c - v.a;
    return vv;
}

#ifdef TEST_VECTORS

static const char *v_debug(vector v)
{
    static char buf[255];
    sprintf(buf,
             "(%d,%d,%d,%d)[%2.2f,%2.2f]",
             v.a, v.b, v.c, v.d, v_x(&v,0), v_y(&v,0));
    return buf;
}

#endif

/* -------------------------------------------------------
 * Tiling routines.
 */

static vector xform_coord(vector vo, int shrink, vector vtrans, int ang)
{
    if (shrink < 0)
        vo = v_shrinkphi(vo);
    else if (shrink > 0)
        vo = v_growphi(vo);

    vo = v_rotate(vo, ang);
    vo = v_trans(vo, vtrans);

    return vo;
}


#define XFORM(n,o,s,a) vs[(n)] = xform_coord(v_edge, (s), vs[(o)], (a))

static int penrose_p2_small(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge);

static int penrose_p2_large(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge)
{
    vector vv_orig, vv_edge;

#ifdef DEBUG_PENROSE
    {
        vector vs[3];
        vs[0] = v_orig;
        XFORM(1, 0, 0, 0);
        XFORM(2, 0, 0, -36*flip);

        state->new_tile(state, vs, 3, depth);
    }
#endif

    if (flip > 0) {
        vector vs[4];

        vs[0] = v_orig;
        XFORM(1, 0, 0, -36);
        XFORM(2, 0, 0, 0);
        XFORM(3, 0, 0, 36);

        state->new_tile(state, vs, 4, depth);
    }
    if (depth >= state->max_depth) return 0;

    vv_orig = v_trans(v_orig, v_rotate(v_edge, -36*flip));
    vv_edge = v_rotate(v_edge, 108*flip);

    penrose_p2_small(state, depth+1, flip,
                     v_orig, v_shrinkphi(v_edge));

    penrose_p2_large(state, depth+1, flip,
                     vv_orig, v_shrinkphi(vv_edge));
    penrose_p2_large(state, depth+1, -flip,
                     vv_orig, v_shrinkphi(vv_edge));

    return 0;
}

static int penrose_p2_small(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge)
{
    vector vv_orig;

#ifdef DEBUG_PENROSE
    {
        vector vs[3];
        vs[0] = v_orig;
        XFORM(1, 0, 0, 0);
        XFORM(2, 0, -1, -36*flip);

        state->new_tile(state, vs, 3, depth);
    }
#endif

    if (flip > 0) {
        vector vs[4];

        vs[0] = v_orig;
        XFORM(1, 0, 0, -72);
        XFORM(2, 0, -1, -36);
        XFORM(3, 0, 0, 0);

        state->new_tile(state, vs, 4, depth);
    }

    if (depth >= state->max_depth) return 0;

    vv_orig = v_trans(v_orig, v_edge);

    penrose_p2_large(state, depth+1, -flip,
                     v_orig, v_shrinkphi(v_rotate(v_edge, -36*flip)));

    penrose_p2_small(state, depth+1, flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, -144*flip)));

    return 0;
}

static int penrose_p3_small(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge);

static int penrose_p3_large(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge)
{
    vector vv_orig;

#ifdef DEBUG_PENROSE
    {
        vector vs[3];
        vs[0] = v_orig;
        XFORM(1, 0, 1, 0);
        XFORM(2, 0, 0, -36*flip);

        state->new_tile(state, vs, 3, depth);
    }
#endif

    if (flip > 0) {
        vector vs[4];

        vs[0] = v_orig;
        XFORM(1, 0, 0, -36);
        XFORM(2, 0, 1, 0);
        XFORM(3, 0, 0, 36);

        state->new_tile(state, vs, 4, depth);
    }
    if (depth >= state->max_depth) return 0;

    vv_orig = v_trans(v_orig, v_edge);

    penrose_p3_large(state, depth+1, -flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, 180)));

    penrose_p3_small(state, depth+1, flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, -108*flip)));

    vv_orig = v_trans(v_orig, v_growphi(v_edge));

    penrose_p3_large(state, depth+1, flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, -144*flip)));


    return 0;
}

static int penrose_p3_small(penrose_state *state, int depth, int flip,
                            vector v_orig, vector v_edge)
{
    vector vv_orig;

#ifdef DEBUG_PENROSE
    {
        vector vs[3];
        vs[0] = v_orig;
        XFORM(1, 0, 0, 0);
        XFORM(2, 0, 0, -36*flip);

        state->new_tile(state, vs, 3, depth);
    }
#endif

    if (flip > 0) {
        vector vs[4];

        vs[0] = v_orig;
        XFORM(1, 0, 0, -36);
        XFORM(3, 0, 0, 0);
        XFORM(2, 3, 0, -36);

        state->new_tile(state, vs, 4, depth);
    }
    if (depth >= state->max_depth) return 0;

    /* NB these two are identical to the first two of p3_large. */
    vv_orig = v_trans(v_orig, v_edge);

    penrose_p3_large(state, depth+1, -flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, 180)));

    penrose_p3_small(state, depth+1, flip,
                     vv_orig, v_shrinkphi(v_rotate(v_edge, -108*flip)));

    return 0;
}

/* -------------------------------------------------------
 * Utility routines.
 */

double penrose_side_length(double start_size, int depth)
{
  return start_size / pow(PHI, depth);
}

void penrose_count_tiles(int depth, int *nlarge, int *nsmall)
{
  /* Steal sgt's fibonacci thingummy. */
}

/*
 * It turns out that an acute isosceles triangle with sides in ratio 1:phi:phi
 * has an incentre which is conveniently 2*phi^-2 of the way from the apex to
 * the base. Why's that convenient? Because: if we situate the incentre of the
 * triangle at the origin, then we can place the apex at phi^-2 * (B+C), and
 * the other two vertices at apex-B and apex-C respectively. So that's an acute
 * triangle with its long sides of unit length, covering a circle about the
 * origin of radius 1-(2*phi^-2), which is conveniently enough phi^-3.
 *
 * (later mail: this is an overestimate by about 5%)
 */

int penrose(penrose_state *state, int which, int angle)
{
    vector vo = v_origin();
    vector vb = v_origin();

    vo.b = vo.c = -state->start_size;
    vo = v_shrinkphi(v_shrinkphi(vo));

    vb.b = state->start_size;

    vo = v_rotate(vo, angle);
    vb = v_rotate(vb, angle);

    if (which == PENROSE_P2)
        return penrose_p2_large(state, 0, 1, vo, vb);
    else
        return penrose_p3_small(state, 0, 1, vo, vb);
}

/*
 * We're asked for a MxN grid, which just means a tiling fitting into roughly
 * an MxN space in some kind of reasonable unit - say, the side length of the
 * two-arrow edges of the tiles. By some reasoning in a previous email, that
 * means we want to pick some subarea of a circle of radius 3.11*sqrt(M^2+N^2).
 * To cover that circle, we need to subdivide a triangle large enough that it
 * contains a circle of that radius.
 *
 * Hence: start with those three vectors marking triangle vertices, scale them
 * all up by phi repeatedly until the radius of the inscribed circle gets
 * bigger than the target, and then recurse into that triangle with the same
 * recursion depth as the number of times you scaled up. That will give you
 * tiles of unit side length, covering a circle big enough that if you randomly
 * choose an orientation and coordinates within the circle, you'll be able to
 * get any valid piece of Penrose tiling of size MxN.
 */
#define INCIRCLE_RADIUS 0.22426 /* phi^-3 less 5%: see above */

void penrose_calculate_size(int which, int tilesize, int w, int h,
                            double *required_radius, int *start_size, int *depth)
{
    double rradius, size;
    int n = 0;

    /*
     * Fudge factor to scale P2 and P3 tilings differently. This
     * doesn't seem to have much relevance to questions like the
     * average number of tiles per unit area; it's just aesthetic.
     */
    if (which == PENROSE_P2)
        tilesize = tilesize * 3 / 2;
    else
        tilesize = tilesize * 5 / 4;

    rradius = tilesize * 3.11 * sqrt((double)(w*w + h*h));
    size = tilesize;

    while ((size * INCIRCLE_RADIUS) < rradius) {
        n++;
        size = size * PHI;
    }

    *start_size = (int)size;
    *depth = n;
    *required_radius = rradius;
}

/* -------------------------------------------------------
 * Test code.
 */

#ifdef TEST_PENROSE

#include <stdio.h>
#include <string.h>

static int show_recursion = 0;
static int ntiles, nfinal;

static int test_cb(penrose_state *state, vector *vs, int n, int depth)
{
    int i, xoff = 0, yoff = 0;
    double l = penrose_side_length(state->start_size, depth);
    double rball = l / 10.0;
    const char *col;

    ntiles++;
    if (state->max_depth == depth) {
        col = n == 4 ? "black" : "green";
        nfinal++;
    } else {
        if (!show_recursion)
            return 0;
        col = n == 4 ? "red" : "blue";
    }
    if (n != 4) yoff = state->start_size;

    printf("<polygon points=\"");
    for (i = 0; i < n; i++) {
        printf("%s%f,%f", (i == 0) ? "" : " ",
               v_x(vs, i) + xoff, v_y(vs, i) + yoff);
    }
    printf("\" style=\"fill: %s; fill-opacity: 0.2; stroke: %s\" />\n", col, col);
    printf("<ellipse cx=\"%f\" cy=\"%f\" rx=\"%f\" ry=\"%f\" fill=\"%s\" />",
           v_x(vs, 0) + xoff, v_y(vs, 0) + yoff, rball, rball, col);

    return 0;
}

static void usage_exit(void)
{
    fprintf(stderr, "Usage: penrose-test [--recursion] P2|P3 SIZE DEPTH\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    penrose_state ps;
    int which = 0;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-h") || !strcmp(p, "--help")) {
            usage_exit();
        } else if (!strcmp(p, "--recursion")) {
            show_recursion = 1;
        } else if (*p == '-') {
            fprintf(stderr, "Unrecognised option '%s'\n", p);
            exit(1);
        } else {
            break;
        }
    }

    if (argc < 3) usage_exit();

    if (strcmp(argv[0], "P2") == 0) which = PENROSE_P2;
    else if (strcmp(argv[0], "P3") == 0) which = PENROSE_P3;
    else usage_exit();

    ps.start_size = atoi(argv[1]);
    ps.max_depth = atoi(argv[2]);
    ps.new_tile = test_cb;

    ntiles = nfinal = 0;

    printf("\
<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n\
<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 20010904//EN\"\n\
\"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n\
\n\
<svg xmlns=\"http://www.w3.org/2000/svg\"\n\
xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n\n");

    printf("<g>\n");
    penrose(&ps, which, 0);
    printf("</g>\n");

    printf("<!-- %d tiles and %d leaf tiles total -->\n",
           ntiles, nfinal);

    printf("</svg>");

    return 0;
}

#endif

#ifdef TEST_VECTORS

static void dbgv(const char *msg, vector v)
{
    printf("%s: %s\n", msg, v_debug(v));
}

int main(int argc, const char *argv[])
{
   vector v = v_unit();

   dbgv("unit vector", v);
   v = v_rotate(v, 36);
   dbgv("rotated 36", v);
   v = v_scale(v, 2);
   dbgv("scaled x2", v);
   v = v_shrinkphi(v);
   dbgv("shrunk phi", v);
   v = v_rotate(v, -36);
   dbgv("rotated -36", v);

   return 0;
}

#endif
/* vim: set shiftwidth=4 tabstop=8: */
