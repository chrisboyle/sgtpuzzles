/*
 * untangle.c: Game about planar graphs. You are given a graph
 * represented by points and straight lines, with some lines
 * crossing; your task is to drag the points into a configuration
 * where none of the lines cross.
 * 
 * Cloned from a Flash game called `Planarity', by John Tantalo.
 * <http://home.cwru.edu/~jnt5/Planarity> at the time of writing
 * this. The Flash game had a fixed set of levels; my added value,
 * as usual, is automatic generation of random games to order.
 */

/*
 * TODO:
 *
 *  - This puzzle, perhaps uniquely among the collection, could use
 *    support for non-aspect-ratio-preserving resizes. This would
 *    require some sort of fairly large redesign, unfortunately (since
 *    it would invalidate the basic assumption that puzzles' size
 *    requirements are adequately expressed by a single scalar tile
 *    size), and probably complicate the rest of the puzzles' API as a
 *    result. So I'm not sure I really want to do it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif
#if HAVE_STDINT_H
#  include <stdint.h>
#endif

#include "puzzles.h"
#include "tree234.h"

#ifndef CIRCLE_RADIUS
#  define CIRCLE_RADIUS 6
#endif
#ifndef DRAG_THRESHOLD
#  ifdef ANDROID
#    define DRAG_THRESHOLD (CIRCLE_RADIUS * 10)
#  else
#    define DRAG_THRESHOLD (CIRCLE_RADIUS * 2)
#  endif
#endif
#define PREFERRED_TILESIZE 64

#define FLASH_TIME 0.30F
#define ANIM_TIME 0.13F
#define SOLVEANIM_TIME 0.50F

enum {
    COL_SYSBACKGROUND,
    COL_BACKGROUND,
    COL_LINE,
    COL_CROSSEDLINE,
    COL_OUTLINE,
    COL_POINT,
    COL_DRAGPOINT,
    COL_CURSORPOINT,
    COL_NEIGHBOUR,
    COL_FLASH1,
    COL_FLASH2,
    NCOLOURS
};

enum {
  PREF_SNAP_TO_GRID,
  PREF_SHOW_CROSSED_EDGES,
  PREF_VERTEX_STYLE,
  N_PREF_ITEMS
};

typedef struct point {
    /*
     * Points are stored using rational coordinates, with the same
     * denominator for both coordinates.
     */
    long x, y, d;
} point;

typedef struct edge {
    /*
     * This structure is implicitly associated with a particular
     * point set, so all it has to do is to store two point
     * indices. It is required to store them in the order (lower,
     * higher), i.e. a < b always.
     */
    int a, b;
} edge;

struct game_params {
    int n;			       /* number of points */
};

struct graph {
    int refcount;		       /* for deallocation */
    tree234 *edges;		       /* stores `edge' structures */
};

struct game_state {
    game_params params;
    int w, h;			       /* extent of coordinate system only */
    point *pts;
    struct graph *graph;
#ifndef EDITOR
    int *crosses;		       /* mark edges which are crossed */
    bool completed, cheated, just_solved;
#endif
};

static int edgecmpC(const void *av, const void *bv)
{
    const edge *a = (const edge *)av;
    const edge *b = (const edge *)bv;

    if (a->a < b->a)
	return -1;
    else if (a->a > b->a)
	return +1;
    else if (a->b < b->b)
	return -1;
    else if (a->b > b->b)
	return +1;
    return 0;
}

static int edgecmp(void *av, void *bv) { return edgecmpC(av, bv); }

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->n = 10;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int n;
    char buf[80];

    switch (i) {
      case 0: n = 6; break;
      case 1: n = 10; break;
      case 2: n = 15; break;
      case 3: n = 20; break;
      case 4: n = 25; break;
      default: return false;
    }

    sprintf(buf, _("%d points"), n);
    *name = dupstr(buf);

    *params = ret = snew(game_params);
    ret->n = n;

    return true;
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

static void decode_params(game_params *params, char const *string)
{
    params->n = atoi(string);
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[80];

    sprintf(buf, "%d", params->n);

    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = _("Number of points");
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = NULL;
    ret[1].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->n = atoi(cfg[0].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
#ifndef EDITOR
    if (params->n < 4)
        return _("Number of points must be at least four");
#else
    if (params->n < 1)
        return _("Number of points must be at least one");
#endif
    if (params->n > INT_MAX / 3)
        return _("Number of points must not be unreasonably large");
    return NULL;
}

#ifndef EDITOR
/* ----------------------------------------------------------------------
 * Small number of 64-bit integer arithmetic operations, to prevent
 * integer overflow at the very core of cross().
 */

#if !HAVE_STDINT_H
/* For prehistoric C implementations, do this the hard way */

typedef struct {
    long hi;
    unsigned long lo;
} int64;

#define greater64(i,j) ( (i).hi>(j).hi || ((i).hi==(j).hi && (i).lo>(j).lo))
#define sign64(i) ((i).hi < 0 ? -1 : (i).hi==0 && (i).lo==0 ? 0 : +1)

static int64 mulu32to64(unsigned long x, unsigned long y)
{
    unsigned long a, b, c, d, t;
    int64 ret;

    a = (x & 0xFFFF) * (y & 0xFFFF);
    b = (x & 0xFFFF) * (y >> 16);
    c = (x >> 16) * (y & 0xFFFF);
    d = (x >> 16) * (y >> 16);

    ret.lo = a;
    ret.hi = d + (b >> 16) + (c >> 16);
    t = (b & 0xFFFF) << 16;
    ret.lo += t;
    if (ret.lo < t)
	ret.hi++;
    t = (c & 0xFFFF) << 16;
    ret.lo += t;
    if (ret.lo < t)
	ret.hi++;

#ifdef DIAGNOSTIC_VIA_LONGLONG
    assert(((unsigned long long)ret.hi << 32) + ret.lo ==
	   (unsigned long long)x * y);
#endif

    return ret;
}

static int64 mul32to64(long x, long y)
{
    int sign = +1;
    int64 ret;
#ifdef DIAGNOSTIC_VIA_LONGLONG
    long long realret = (long long)x * y;
#endif

    if (x < 0)
	x = -x, sign = -sign;
    if (y < 0)
	y = -y, sign = -sign;

    ret = mulu32to64(x, y);

    if (sign < 0) {
	ret.hi = -ret.hi;
	ret.lo = -ret.lo;
	if (ret.lo)
	    ret.hi--;
    }

#ifdef DIAGNOSTIC_VIA_LONGLONG
    assert(((unsigned long long)ret.hi << 32) + ret.lo == realret);
#endif

    return ret;
}

static int64 dotprod64(long a, long b, long p, long q)
{
    int64 ab, pq;

    ab = mul32to64(a, b);
    pq = mul32to64(p, q);
    ab.hi += pq.hi;
    ab.lo += pq.lo;
    if (ab.lo < pq.lo)
	ab.hi++;
    return ab;
}

#else /* HAVE_STDINT_H */

typedef int64_t int64;
#define greater64(i,j) ((i) > (j))
#define sign64(i) ((i) < 0 ? -1 : (i)==0 ? 0 : +1)
#define mulu32to64(x,y) ((int64_t)(unsigned long)(x) * (unsigned long)(y))
#define mul32to64(x,y) ((int64_t)(long)(x) * (long)(y))

static int64 dotprod64(long a, long b, long p, long q)
{
    return (int64)a * b + (int64)p * q;
}

#endif /* HAVE_STDINT_H */

/*
 * Determine whether the line segments between a1 and a2, and
 * between b1 and b2, intersect. We count it as an intersection if
 * any of the endpoints lies _on_ the other line.
 */
static bool cross(point a1, point a2, point b1, point b2)
{
    long b1x, b1y, b2x, b2y, px, py;
    int64 d1, d2, d3;

    /*
     * The condition for crossing is that b1 and b2 are on opposite
     * sides of the line a1-a2, and vice versa. We determine this
     * by taking the dot product of b1-a1 with a vector
     * perpendicular to a2-a1, and similarly with b2-a1, and seeing
     * if they have different signs.
     */

    /*
     * Construct the vector b1-a1. We don't have to worry too much
     * about the denominator, because we're only going to check the
     * sign of this vector; we just need to get the numerator
     * right.
     */
    b1x = b1.x * a1.d - a1.x * b1.d;
    b1y = b1.y * a1.d - a1.y * b1.d;
    /* Now construct b2-a1, and a vector perpendicular to a2-a1,
     * in the same way. */
    b2x = b2.x * a1.d - a1.x * b2.d;
    b2y = b2.y * a1.d - a1.y * b2.d;
    px = a1.y * a2.d - a2.y * a1.d;
    py = a2.x * a1.d - a1.x * a2.d;
    /* Take the dot products. Here we resort to 64-bit arithmetic. */
    d1 = dotprod64(b1x, px, b1y, py);
    d2 = dotprod64(b2x, px, b2y, py);
    /* If they have the same non-zero sign, the lines do not cross. */
    if ((sign64(d1) > 0 && sign64(d2) > 0) ||
	(sign64(d1) < 0 && sign64(d2) < 0))
	return false;

    /*
     * If the dot products are both exactly zero, then the two line
     * segments are collinear. At this point the intersection
     * condition becomes whether or not they overlap within their
     * line.
     */
    if (sign64(d1) == 0 && sign64(d2) == 0) {
	/* Construct the vector a2-a1. */
	px = a2.x * a1.d - a1.x * a2.d;
	py = a2.y * a1.d - a1.y * a2.d;
	/* Determine the dot products of b1-a1 and b2-a1 with this. */
	d1 = dotprod64(b1x, px, b1y, py);
	d2 = dotprod64(b2x, px, b2y, py);
	/* If they're both strictly negative, the lines do not cross. */
	if (sign64(d1) < 0 && sign64(d2) < 0)
	    return false;
	/* Otherwise, take the dot product of a2-a1 with itself. If
	 * the other two dot products both exceed this, the lines do
	 * not cross. */
	d3 = dotprod64(px, px, py, py);
	if (greater64(d1, d3) && greater64(d2, d3))
	    return false;
    }

    /*
     * We've eliminated the only important special case, and we
     * have determined that b1 and b2 are on opposite sides of the
     * line a1-a2. Now do the same thing the other way round and
     * we're done.
     */
    b1x = a1.x * b1.d - b1.x * a1.d;
    b1y = a1.y * b1.d - b1.y * a1.d;
    b2x = a2.x * b1.d - b1.x * a2.d;
    b2y = a2.y * b1.d - b1.y * a2.d;
    px = b1.y * b2.d - b2.y * b1.d;
    py = b2.x * b1.d - b1.x * b2.d;
    d1 = dotprod64(b1x, px, b1y, py);
    d2 = dotprod64(b2x, px, b2y, py);
    if ((sign64(d1) > 0 && sign64(d2) > 0) ||
	(sign64(d1) < 0 && sign64(d2) < 0))
	return false;

    /*
     * The lines must cross.
     */
    return true;
}

#endif /* EDITOR */

static unsigned long squarert(unsigned long n) {
    unsigned long d, a, b, di;

    d = n;
    a = 0;
    b = 1L << 30;		       /* largest available power of 4 */
    do {
        a >>= 1;
        di = 2*a + b;
        if (di <= d) {
            d -= di;
            a += b;
        }
        b >>= 2;
    } while (b);

    return a;
}

/*
 * Our solutions are arranged on a square grid big enough that n
 * points occupy about 1/POINTDENSITY of the grid.
 */
#define POINTDENSITY 3
#define MAXDEGREE 4
#define COORDLIMIT(n) squarert((n) * POINTDENSITY)

static void addedge(tree234 *edges, int a, int b)
{
    edge *e = snew(edge);

    assert(a != b);

    e->a = min(a, b);
    e->b = max(a, b);

    if (add234(edges, e) != e)
        /* Duplicate edge. */
        sfree(e);
}

#ifdef EDITOR
static void deledge(tree234 *edges, int a, int b)
{
    edge e, *found;

    assert(a != b);

    e.a = min(a, b);
    e.b = max(a, b);

    found = del234(edges, &e);
    sfree(found);
}
#endif

static bool isedge(tree234 *edges, int a, int b)
{
    edge e;

    assert(a != b);

    e.a = min(a, b);
    e.b = max(a, b);

    return find234(edges, &e, NULL) != NULL;
}

typedef struct vertex {
    int param;
    int vindex;
} vertex;

#ifndef EDITOR
static int vertcmpC(const void *av, const void *bv)
{
    const vertex *a = (const vertex *)av;
    const vertex *b = (const vertex *)bv;

    if (a->param < b->param)
	return -1;
    else if (a->param > b->param)
	return +1;
    else if (a->vindex < b->vindex)
	return -1;
    else if (a->vindex > b->vindex)
	return +1;
    return 0;
}
static int vertcmp(void *av, void *bv) { return vertcmpC(av, bv); }
#endif

/*
 * Construct point coordinates for n points arranged in a circle,
 * within the bounding box (0,0) to (w,w).
 */
static void make_circle(point *pts, int n, int w)
{
    long d, r, c, i;

    /*
     * First, decide on a denominator. Although in principle it
     * would be nice to set this really high so as to finely
     * distinguish all the points on the circle, I'm going to set
     * it at a fixed size to prevent integer overflow problems.
     */
    d = PREFERRED_TILESIZE;

    /*
     * Leave a little space outside the circle.
     */
    c = d * w / 2;
    r = d * w * 3 / 7;

    /*
     * Place the points.
     */
    for (i = 0; i < n; i++) {
	double angle = i * 2 * PI / n;
	double x = r * sin(angle), y = - r * cos(angle);
	pts[i].x = (long)(c + x + 0.5);
	pts[i].y = (long)(c + y + 0.5);
	pts[i].d = d;
    }
}

/*
 * Encode a graph in Untangle's game id: a comma-separated list of
 * dash-separated vertex number pairs, numbered from zero.
 *
 * If params != NULL, then the number of vertices is prefixed to the
 * front to make a full Untangle game id. Otherwise, we return just
 * the game description part.
 *
 * If mapping != NULL, then it is expected to be a mapping from the
 * graph's original vertex numbers to output vertex numbers.
 */
static char *encode_graph(const game_params *params, tree234 *edges,
                          const long *mapping)
{
    const char *sep;
    char buf[80];
    int i, k, m, retlen;
    edge *e, *ea;
    char *ret;

    retlen = 0;
    if (params)
        retlen += sprintf(buf, "%d:", params->n);

    m = count234(edges);
    ea = snewn(m, edge);
    for (i = 0; (e = index234(edges, i)) != NULL; i++) {
        int ma, mb;
        assert(i < m);
        if (mapping) {
            ma = mapping[e->a];
            mb = mapping[e->b];
        } else {
            ma = e->a;
            mb = e->b;
        }
        ea[i].a = min(ma, mb);
        ea[i].b = max(ma, mb);
        if (i > 0)
            retlen++; /* comma separator after the previous edge */
        retlen += sprintf(buf, "%d-%d", ea[i].a, ea[i].b);
    }
    assert(i == m);
    /* Re-sort to prevent side channels, if mapping was used */
    qsort(ea, m, sizeof(*ea), edgecmpC);

    ret = snewn(retlen + 1, char);
    sep = "";
    k = 0;
    if (params)
        k += sprintf(ret + k, "%d:", params->n);

    for (i = 0; i < m; i++) {
        k += sprintf(ret + k, "%s%d-%d", sep, ea[i].a, ea[i].b);
        sep = ",";
    }
    assert(k == retlen);

    sfree(ea);

    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
#ifndef EDITOR
    int n = params->n, i;
    long w, h, j, k, m;
    point *pts, *pts2;
    long *tmp;
    tree234 *edges, *vertices;
    edge *e, *e2;
    vertex *v, *vs, *vlist;
    char *ret;

    w = h = COORDLIMIT(n);

    /*
     * Choose n points from this grid.
     */
    pts = snewn(n, point);
    tmp = snewn(w*h, long);
    for (i = 0; i < w*h; i++)
	tmp[i] = i;
    shuffle(tmp, w*h, sizeof(*tmp), rs);
    for (i = 0; i < n; i++) {
	pts[i].x = tmp[i] % w;
	pts[i].y = tmp[i] / w;
	pts[i].d = 1;
    }
    sfree(tmp);

    /*
     * Now start adding edges between the points.
     * 
     * At all times, we attempt to add an edge to the lowest-degree
     * vertex we currently have, and we try the other vertices as
     * candidate second endpoints in order of distance from this
     * one. We stop as soon as we find an edge which
     * 
     *  (a) does not increase any vertex's degree beyond MAXDEGREE
     *  (b) does not cross any existing edges
     *  (c) does not intersect any actual point.
     */
    vs = snewn(n, vertex);
    vertices = newtree234(vertcmp);
    for (i = 0; i < n; i++) {
	v = vs + i;
	v->param = 0;		       /* in this tree, param is the degree */
	v->vindex = i;
	add234(vertices, v);
    }
    edges = newtree234(edgecmp);
    vlist = snewn(n, vertex);
    while (1) {
        bool added = false;

	for (i = 0; i < n; i++) {
	    v = index234(vertices, i);
	    j = v->vindex;

	    if (v->param >= MAXDEGREE)
		break;		       /* nothing left to add! */

	    /*
	     * Sort the other vertices into order of their distance
	     * from this one. Don't bother looking below i, because
	     * we've already tried those edges the other way round.
	     * Also here we rule out target vertices with too high
	     * a degree, and (of course) ones to which we already
	     * have an edge.
	     */
	    m = 0;
	    for (k = i+1; k < n; k++) {
		vertex *kv = index234(vertices, k);
		int ki = kv->vindex;
		int dx, dy;

		if (kv->param >= MAXDEGREE || isedge(edges, ki, j))
		    continue;

		vlist[m].vindex = ki;
		dx = pts[ki].x - pts[j].x;
		dy = pts[ki].y - pts[j].y;
		vlist[m].param = dx*dx + dy*dy;
		m++;
	    }

	    qsort(vlist, m, sizeof(*vlist), vertcmpC);

	    for (k = 0; k < m; k++) {
		int p;
		int ki = vlist[k].vindex;

		/*
		 * Check to see whether this edge intersects any
		 * existing edge or point.
		 */
		for (p = 0; p < n; p++)
		    if (p != ki && p != j && cross(pts[ki], pts[j],
						   pts[p], pts[p]))
			break;
		if (p < n)
		    continue;
		for (p = 0; (e = index234(edges, p)) != NULL; p++)
		    if (e->a != ki && e->a != j &&
			e->b != ki && e->b != j &&
			cross(pts[ki], pts[j], pts[e->a], pts[e->b]))
			break;
		if (e)
		    continue;

		/*
		 * We're done! Add this edge, modify the degrees of
		 * the two vertices involved, and break.
		 */
		addedge(edges, j, ki);
		added = true;
		del234(vertices, vs+j);
		vs[j].param++;
		add234(vertices, vs+j);
		del234(vertices, vs+ki);
		vs[ki].param++;
		add234(vertices, vs+ki);
		break;
	    }

	    if (k < m)
		break;
	}

	if (!added)
	    break;		       /* we're done. */
    }

    /*
     * That's our graph. Now shuffle the points, making sure that
     * they come out with at least one crossed line when arranged
     * in a circle (so that the puzzle isn't immediately solved!).
     */
    tmp = snewn(n, long);
    for (i = 0; i < n; i++)
	tmp[i] = i;
    pts2 = snewn(n, point);
    make_circle(pts2, n, w);
    while (1) {
	shuffle(tmp, n, sizeof(*tmp), rs);
	for (i = 0; (e = index234(edges, i)) != NULL; i++) {
	    for (j = i+1; (e2 = index234(edges, j)) != NULL; j++) {
		if (e2->a == e->a || e2->a == e->b ||
		    e2->b == e->a || e2->b == e->b)
		    continue;
		if (cross(pts2[tmp[e2->a]], pts2[tmp[e2->b]],
			  pts2[tmp[e->a]], pts2[tmp[e->b]]))
		    break;
	    }
	    if (e2)
		break;
	}
	if (e)
	    break;		       /* we've found a crossing */
    }

    /*
     * We're done. Encode the output graph as a string.
     */
    ret = encode_graph(NULL, edges, tmp);

    /*
     * Encode the solution we started with as an aux_info string.
     */
    {
	char buf[80];
	char *auxstr;
	int auxlen;

	auxlen = 2;		       /* leading 'S' and trailing '\0' */
	for (i = 0; i < n; i++) {
	    j = tmp[i];
	    pts2[j] = pts[i];
	    if (pts2[j].d & 1) {
		pts2[j].x *= 2;
		pts2[j].y *= 2;
		pts2[j].d *= 2;
	    }
	    pts2[j].x += pts2[j].d / 2;
	    pts2[j].y += pts2[j].d / 2;
	    auxlen += sprintf(buf, ";P%d:%ld,%ld/%ld", i,
			      pts2[j].x, pts2[j].y, pts2[j].d);
	}
	k = 0;
	auxstr = snewn(auxlen, char);
	auxstr[k++] = 'S';
	for (i = 0; i < n; i++)
	    k += sprintf(auxstr+k, ";P%d:%ld,%ld/%ld", i,
			 pts2[i].x, pts2[i].y, pts2[i].d);
	assert(k < auxlen);
	*aux = auxstr;
    }
    sfree(pts2);

    sfree(tmp);
    sfree(vlist);
    freetree234(vertices);
    sfree(vs);
    while ((e = delpos234(edges, 0)) != NULL)
	sfree(e);
    freetree234(edges);
    sfree(pts);

    return ret;
#else
    return dupstr("");
#endif
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int a, b;

    while (*desc) {
	a = atoi(desc);
	if (a < 0 || a >= params->n)
	    return _("Number out of range in game description");
	while (*desc && isdigit((unsigned char)*desc)) desc++;
	if (*desc != '-')
	    return _("Expected '-' after number in game description");
	desc++;			       /* eat dash */
	b = atoi(desc);
	if (b < 0 || b >= params->n)
	    return _("Number out of range in game description");
	while (*desc && isdigit((unsigned char)*desc)) desc++;
	if (*desc) {
	    if (*desc != ',')
		return _("Expected ',' after number in game description");
	    desc++;		       /* eat comma */
	}
        if (a == b)
            return "Node linked to itself in game description";
    }

    return NULL;
}

#ifndef EDITOR
static void mark_crossings(game_state *state)
{
    bool ok = true;
    int i, j;
    edge *e, *e2;

    for (i = 0; (e = index234(state->graph->edges, i)) != NULL; i++)
	state->crosses[i] = false;

    /*
     * Check correctness: for every pair of edges, see whether they
     * cross.
     */
    for (i = 0; (e = index234(state->graph->edges, i)) != NULL; i++) {
	for (j = i+1; (e2 = index234(state->graph->edges, j)) != NULL; j++) {
	    if (e2->a == e->a || e2->a == e->b ||
		e2->b == e->a || e2->b == e->b)
		continue;
	    if (cross(state->pts[e2->a], state->pts[e2->b],
		      state->pts[e->a], state->pts[e->b])) {
		ok = false;
		state->crosses[i] = state->crosses[j] = true;
	    }
	}
    }

    /*
     * e == NULL if we've gone through all the edge pairs
     * without finding a crossing.
     */
    if (ok)
	state->completed = true;
}
#endif

static key_label *game_request_keys(const game_params *params, int *nkeys, int *arrow_mode)
{
	*nkeys = 0;
	*arrow_mode = ANDROID_ARROWS_LEFT_RIGHT;
	return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int n = params->n;
    game_state *state = snew(game_state);
    int a, b;

    state->params = *params;
    state->w = state->h = COORDLIMIT(n);
    state->pts = snewn(n, point);
    make_circle(state->pts, n, state->w);
    state->graph = snew(struct graph);
    state->graph->refcount = 1;
    state->graph->edges = newtree234(edgecmp);
#ifndef EDITOR
    state->completed = state->cheated = state->just_solved = false;
#endif

    while (*desc) {
	a = atoi(desc);
	assert(a >= 0 && a < params->n);
	while (*desc && isdigit((unsigned char)*desc)) desc++;
	assert(*desc == '-');
	desc++;			       /* eat dash */
	b = atoi(desc);
	assert(b >= 0 && b < params->n);
	while (*desc && isdigit((unsigned char)*desc)) desc++;
	if (*desc) {
	    assert(*desc == ',');
	    desc++;		       /* eat comma */
	}
	addedge(state->graph->edges, a, b);
    }

#ifndef EDITOR
    state->crosses = snewn(count234(state->graph->edges), int);
    mark_crossings(state);	       /* sets up `crosses' and `completed' */
#endif

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int n = state->params.n;
    game_state *ret = snew(game_state);

    ret->params = state->params;
    ret->w = state->w;
    ret->h = state->h;
    ret->pts = snewn(n, point);
    memcpy(ret->pts, state->pts, n * sizeof(point));
#ifndef EDITOR
    ret->graph = state->graph;
    ret->graph->refcount++;
    ret->completed = state->completed;
    ret->cheated = state->cheated;
    ret->just_solved = state->just_solved;
    ret->crosses = snewn(count234(ret->graph->edges), int);
    memcpy(ret->crosses, state->crosses,
	   count234(ret->graph->edges) * sizeof(int));
#else
    /* For the graph editor, we must clone the whole graph */
    ret->graph = snew(struct graph);
    ret->graph->refcount = 1;
    ret->graph->edges = newtree234(edgecmp);
    {
        int i;
        struct edge *edge;
        for (i = 0; (edge = index234(state->graph->edges, i)) != NULL; i++) {
            addedge(ret->graph->edges, edge->a, edge->b);
        }
    }
#endif

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->graph->refcount <= 0) {
	edge *e;
	while ((e = delpos234(state->graph->edges, 0)) != NULL)
	    sfree(e);
	freetree234(state->graph->edges);
	sfree(state->graph);
    }
#ifndef EDITOR
    sfree(state->crosses);
#endif
    sfree(state->pts);
    sfree(state);
}

#ifndef EDITOR
static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int n = state->params.n;
    int matrix[4];
    point *pts;
    int i, j, besti;
    float bestd;
    char buf[80], *ret;
    int retlen, retsize;

    if (!aux) {
	*error = _("Solution not known for this puzzle");
	return NULL;
    }

    /*
     * Decode the aux_info to get the original point positions.
     */
    pts = snewn(n, point);
    aux++;                             /* eat 'S' */
    for (i = 0; i < n; i++) {
        int p, k;
        long x, y, d;
	int ret = sscanf(aux, ";P%d:%ld,%ld/%ld%n", &p, &x, &y, &d, &k);
        if (ret != 4 || p != i) {
            *error = _("Internal error: aux_info badly formatted");
            sfree(pts);
            return NULL;
        }
        pts[i].x = x;
        pts[i].y = y;
        pts[i].d = d;
        aux += k;
    }

    /*
     * Now go through eight possible symmetries of the point set.
     * For each one, work out the sum of the Euclidean distances
     * between the points' current positions and their new ones.
     * 
     * We're squaring distances here, which means we're at risk of
     * integer overflow. Fortunately, there's no real need to be
     * massively careful about rounding errors, since this is a
     * non-essential bit of the code; so I'll just work in floats
     * internally.
     */
    besti = -1;
    bestd = 0.0F;

    for (i = 0; i < 8; i++) {
        float d;

        matrix[0] = matrix[1] = matrix[2] = matrix[3] = 0;
        matrix[i & 1] = (i & 2) ? +1 : -1;
        matrix[3-(i&1)] = (i & 4) ? +1 : -1;

        d = 0.0F;
        for (j = 0; j < n; j++) {
            float px = (float)pts[j].x / pts[j].d;
            float py = (float)pts[j].y / pts[j].d;
            float sx = (float)currstate->pts[j].x / currstate->pts[j].d;
            float sy = (float)currstate->pts[j].y / currstate->pts[j].d;
            float cx = (float)currstate->w / 2;
            float cy = (float)currstate->h / 2;
            float ox, oy, dx, dy;

            px -= cx;
            py -= cy;

            ox = matrix[0] * px + matrix[1] * py;
            oy = matrix[2] * px + matrix[3] * py;

            ox += cx;
            oy += cy;

            dx = ox - sx;
            dy = oy - sy;

            d += dx*dx + dy*dy;
        }

        if (besti < 0 || bestd > d) {
            besti = i;
            bestd = d;
        }
    }

    assert(besti >= 0);

    /*
     * Now we know which symmetry is closest to the points' current
     * positions. Use it.
     */
    matrix[0] = matrix[1] = matrix[2] = matrix[3] = 0;
    matrix[besti & 1] = (besti & 2) ? +1 : -1;
    matrix[3-(besti&1)] = (besti & 4) ? +1 : -1;

    retsize = 256;
    ret = snewn(retsize, char);
    retlen = 0;
    ret[retlen++] = 'S';
    ret[retlen] = '\0';

    for (i = 0; i < n; i++) {
        float px = (float)pts[i].x / pts[i].d;
        float py = (float)pts[i].y / pts[i].d;
        float cx = (float)currstate->w / 2;
        float cy = (float)currstate->h / 2;
        float ox, oy;
        int extra;

        px -= cx;
        py -= cy;

        ox = matrix[0] * px + matrix[1] * py;
        oy = matrix[2] * px + matrix[3] * py;

        ox += cx;
        oy += cy;

        /*
         * Use a fixed denominator of 2, because we know the
         * original points were on an integer grid offset by 1/2.
         */
        pts[i].d = 2;
        ox *= pts[i].d;
        oy *= pts[i].d;
        pts[i].x = (long)(ox + 0.5F);
        pts[i].y = (long)(oy + 0.5F);

        extra = sprintf(buf, ";P%d:%ld,%ld/%ld", i,
                        pts[i].x, pts[i].y, pts[i].d);
        if (retlen + extra >= retsize) {
            retsize = retlen + extra + 256;
            ret = sresize(ret, retsize, char);
        }
        strcpy(ret + retlen, buf);
        retlen += extra;
    }

    sfree(pts);

    return ret;
}
#endif /* EDITOR */

#ifdef EDITOR
static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    return encode_graph(&state->params, state->graph->edges, NULL);
}
#endif /* EDITOR */

typedef enum DragType { DRAG_MOVE_POINT, DRAG_TOGGLE_EDGE } DragType;

struct game_ui {
    /* Invariant: at most one of {dragpoint, cursorpoint} may be valid
     * at any time. */
    int dragpoint;		       /* point being dragged; -1 if none */
    int cursorpoint;                   /* point being highlighted, but
                                        * not dragged by the cursor,
                                        * again -1 if none */

    point newpoint;		       /* where it's been dragged to so far */
    bool just_dragged;                 /* reset in game_changed_state */
    bool just_moved;                   /* _set_ in game_changed_state */
    float anim_length;

    DragType dragtype;

    /*
     * User preference option to snap dragged points to a coarse-ish
     * grid. Requested by a user who otherwise found themself spending
     * too much time struggling to get lines nicely horizontal or
     * vertical.
     */
    bool snap_to_grid;

    /*
     * User preference option to highlight graph edges involved in a
     * crossing.
     */
    bool show_crossed_edges;

    /*
     * User preference option to show vertices as numbers instead of
     * circular blobs, so you can easily tell them apart.
     */
    bool vertex_numbers;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->dragpoint = -1;
    ui->cursorpoint = -1;
    ui->just_moved = ui->just_dragged = false;
    ui->snap_to_grid = false;
    ui->show_crossed_edges = false;
    ui->vertex_numbers = false;
    return ui;
}

static config_item *get_prefs(game_ui *ui)
{
    config_item *cfg;

    cfg = snewn(N_PREF_ITEMS+1, config_item);

    cfg[PREF_SNAP_TO_GRID].name = "Snap points to a grid";
    cfg[PREF_SNAP_TO_GRID].kw = "snap-to-grid";
    cfg[PREF_SNAP_TO_GRID].type = C_BOOLEAN;
    cfg[PREF_SNAP_TO_GRID].u.boolean.bval = ui->snap_to_grid;

    cfg[PREF_SHOW_CROSSED_EDGES].name = "Show edges that cross another edge";
    cfg[PREF_SHOW_CROSSED_EDGES].kw = "show-crossed-edges";
    cfg[PREF_SHOW_CROSSED_EDGES].type = C_BOOLEAN;
    cfg[PREF_SHOW_CROSSED_EDGES].u.boolean.bval = ui->show_crossed_edges;

    cfg[PREF_VERTEX_STYLE].name = "Display style for vertices";
    cfg[PREF_VERTEX_STYLE].kw = "vertex-style";
    cfg[PREF_VERTEX_STYLE].type = C_CHOICES;
    cfg[PREF_VERTEX_STYLE].u.choices.choicenames = ":Circles:Numbers";
    cfg[PREF_VERTEX_STYLE].u.choices.choicekws = ":circle:number";
    cfg[PREF_VERTEX_STYLE].u.choices.selected = ui->vertex_numbers;

    cfg[N_PREF_ITEMS].name = NULL;
    cfg[N_PREF_ITEMS].type = C_END;

    return cfg;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->snap_to_grid = cfg[PREF_SNAP_TO_GRID].u.boolean.bval;
    ui->show_crossed_edges = cfg[PREF_SHOW_CROSSED_EDGES].u.boolean.bval;
    ui->vertex_numbers = cfg[PREF_VERTEX_STYLE].u.choices.selected;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static bool game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    ui->dragpoint = -1;
    ui->just_moved = ui->just_dragged;
    ui->just_dragged = false;

    return newstate->completed && !newstate->cheated && oldstate && !oldstate->completed;
}

struct game_drawstate {
    long tilesize;
    int bg, dragpoint, cursorpoint;
    long *x, *y;
};

static void place_dragged_point(const game_state *state, game_ui *ui,
                                const game_drawstate *ds, int x, int y)
{
    if (ui->snap_to_grid) {
        /*
         * We snap points to a grid that has n-1 vertices on each
         * side. This should be large enough to permit a straight-
         * line drawing of any n-vertex planar graph, and moreover,
         * any specific planar embedding of that graph.
         *
         * Source: David Eppstein's book 'Forbidden Configurations in
         * Discrete Geometry' mentions (section 16.3, page 182) that
         * the point configuration he describes as GRID(n-1,n-1) -
         * that is, the vertices of a square grid with n-1 vertices on
         * each side - is universal for n-vertex planar graphs. In
         * other words (from definitions earlier in the chapter), if a
         * graph G admits any drawing in the plane at all, then it can
         * be drawn with straight lines, and with all vertices being
         * vertices of that grid.
         *
         * That fact by itself only says that _some_ planar embedding
         * of G can be drawn in this grid. We'd prefer that _all_
         * embeddings of G can be so drawn, because 'snap to grid' is
         * supposed to be a UI affordance, not an extra puzzle
         * challenge, so we don't want to constrain the player's
         * choice of planar embedding.
         *
         * But it doesn't make a difference. Proof: given a specific
         * planar embedding of G, triangulate it, by adding extra
         * edges to every face of degree > 3. When this process
         * terminates with every face a triangle, we have a new graph
         * G' such that no edge can be added without it ceasing to be
         * planar. Standard theorems say that a maximal planar graph
         * is 3-connected, and that a 3-connected planar graph has a
         * _unique_ embedding. So any drawing of G' in the plane can
         * be converted into a drawing of G in the desired embedding,
         * by simply removing all the extra edges that we added to
         * turn G into G'. And G' is still an n-vertex planar graph,
         * hence it can be drawn in GRID(n-1,n-1). []
         */
        int d = state->params.n - 1;

        /* Calculate position in terms of the snapping grid. */
        x = d * x / (state->w * ds->tilesize);
        y = d * y / (state->h * ds->tilesize);
        /* Convert to standard co-ordinates, applying a half-square offset. */
        ui->newpoint.x = (x * 2 + 1) * state->w;
        ui->newpoint.y = (y * 2 + 1) * state->h;
        ui->newpoint.d = d * 2;
    } else {
        ui->newpoint.x = x;
        ui->newpoint.y = y;
        ui->newpoint.d = ds->tilesize;
    }
}

static float normsq(point pt) {
    return (pt.x * pt.x + pt.y * pt.y) / (pt.d * pt.d);
}

/*
 * Find a vertex within DRAG_THRESHOLD of the pointer, or return -1 if
 * no such point exists. In case of more than one, we return the one
 * _nearest_ to the pointer, so that if two points are very close it's
 * still possible to drag a specific one of them.
 */
static int point_under_mouse(const game_state *state,
                             const game_drawstate *ds, int x, int y)
{
    int n = state->params.n;
    int i, best;
    long bestd;

    best = -1;
    bestd = 0;

    for (i = 0; i < n; i++) {
        long px = state->pts[i].x * ds->tilesize / state->pts[i].d;
        long py = state->pts[i].y * ds->tilesize / state->pts[i].d;
        long dx = px - x;
        long dy = py - y;
        long d = dx*dx + dy*dy;

        if (best == -1 || bestd > d) {
            best = i;
            bestd = d;
        }
    }

    if (bestd <= DRAG_THRESHOLD * DRAG_THRESHOLD)
        return best;

    return -1;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int n = state->params.n;

    if (IS_MOUSE_DOWN(button)) {
	int p = point_under_mouse(state, ds, x, y);
        if (p >= 0) {
            ui->dragtype = DRAG_MOVE_POINT;
#ifdef EDITOR
            if (button == RIGHT_BUTTON)
                ui->dragtype = DRAG_TOGGLE_EDGE;
#endif

	    ui->dragpoint = p;
	    ui->cursorpoint = -1; /* eliminate the cursor point, if any */
            if (ui->dragtype == DRAG_MOVE_POINT)
                place_dragged_point(state, ui, ds, x, y);
	    return MOVE_UI_UPDATE;
	}
        return MOVE_NO_EFFECT;
    } else if (IS_MOUSE_DRAG(button) && ui->dragpoint >= 0 &&
               ui->dragtype == DRAG_MOVE_POINT) {
        place_dragged_point(state, ui, ds, x, y);
	return MOVE_UI_UPDATE;
    } else if (IS_MOUSE_RELEASE(button) && ui->dragpoint >= 0 &&
               ui->dragtype == DRAG_MOVE_POINT) {
	int p = ui->dragpoint;
	char buf[80];

	ui->dragpoint = -1;	       /* terminate drag, no matter what */
        ui->cursorpoint = -1;          /* also eliminate the cursor point */

	/*
	 * First, see if we're within range. The user can cancel a
	 * drag by dragging the point right off the window.
	 */
	if (ui->newpoint.x < 0 ||
            ui->newpoint.x >= (long)state->w*ui->newpoint.d ||
	    ui->newpoint.y < 0 ||
            ui->newpoint.y >= (long)state->h*ui->newpoint.d)
	    return MOVE_UI_UPDATE;

	/*
	 * We aren't cancelling the drag. Construct a move string
	 * indicating where this point is going to.
	 */
	sprintf(buf, "P%d:%ld,%ld/%ld", p,
		ui->newpoint.x, ui->newpoint.y, ui->newpoint.d);
	ui->just_dragged = true;
	return dupstr(buf);
#ifdef EDITOR
    } else if (IS_MOUSE_DRAG(button) && ui->dragpoint >= 0 &&
               ui->dragtype == DRAG_TOGGLE_EDGE) {
	ui->cursorpoint = point_under_mouse(state, ds, x, y);
	return MOVE_UI_UPDATE;
    } else if (IS_MOUSE_RELEASE(button) && ui->dragpoint >= 0 &&
               ui->dragtype == DRAG_TOGGLE_EDGE) {
        int p = ui->dragpoint;
	int q = point_under_mouse(state, ds, x, y);
	char buf[80];

	ui->dragpoint = -1;	       /* terminate drag, no matter what */
        ui->cursorpoint = -1;          /* also eliminate the cursor point */

        if (q < 0 || p == q)
            return MOVE_UI_UPDATE;

	sprintf(buf, "E%c%d,%d",
                isedge(state->graph->edges, p, q) ? 'D' : 'A',
                p, q);
	return dupstr(buf);
#endif /* EDITOR */
    } else if (IS_MOUSE_DRAG(button)) {
        return MOVE_NO_EFFECT;
    } else if (IS_MOUSE_RELEASE(button)) {
        assert(ui->dragpoint == -1);
        return MOVE_NO_EFFECT;
    }
    else if(IS_CURSOR_MOVE(button)) {
        if(ui->dragpoint < 0) {
            /*
	     * We're selecting a point with the cursor keys.
	     *
	     * If no point is currently highlighted, we assume the "0"
	     * point is highlighted to begin. Then, we search all the
	     * points and find the nearest one (by Euclidean distance)
	     * in the quadrant corresponding to the cursor key
	     * direction. A point is in the right quadrant if and only
	     * if the azimuth angle to that point from the cursor
	     * point is within a [-45 deg, +45 deg] interval from the
	     * direction vector of the cursor key.
	     *
	     * An important corner case here is if another point is in
	     * the exact same location as the currently highlighted
	     * point (which is a possibility with the "snap to grid"
	     * preference). In this case, we do not consider the other
	     * point as a candidate point, so as to prevent the cursor
	     * from being "stuck" on any point. The player can still
	     * select the overlapped point by dragging the highlighted
	     * point away and then navigating back.
	     */
            int i, best = -1;
            float bestd = 0;

            if(ui->cursorpoint < 0) {
                ui->cursorpoint = 0;
            }

	    point cur = state->pts[ui->cursorpoint];

            for (i = 0; i < n; i++) {
		point delta;
		float distsq;
		point p = state->pts[i];
                int right_direction = false;

                if(i == ui->cursorpoint)
                    continue;

		/* Compute the vector p - cur. Check that it lies in
		 * the correct quadrant. */
		delta.x = p.x * cur.d - cur.x * p.d;
		delta.y = p.y * cur.d - cur.y * p.d;
		delta.d = cur.d * p.d;

		if(delta.x == 0 && delta.y == 0)
		    continue; /* overlaps cursor point - skip */

		switch(button) {
		case CURSOR_UP:
		    right_direction = (delta.y <= -delta.x) && (delta.y <= delta.x);
		    break;
		case CURSOR_DOWN:
		    right_direction = (delta.y >= -delta.x) && (delta.y >= delta.x);
		    break;
		case CURSOR_LEFT:
		    right_direction = (delta.y >= delta.x) && (delta.y <= -delta.x);
		    break;
		case CURSOR_RIGHT:
		    right_direction = (delta.y <= delta.x) && (delta.y >= -delta.x);
		    break;
		}

		if(!right_direction)
		    continue;

		/* Compute squared Euclidean distance */
		distsq = normsq(delta);

                if (best == -1 || distsq < bestd) {
                    best = i;
                    bestd = distsq;
                }
            }

            if(best >= 0) {
                ui->cursorpoint = best;
                return MOVE_UI_UPDATE;
            }
        }
	else if(ui->dragpoint >= 0) {
            /* Dragging a point with the cursor keys. */
	    int movement_increment = ds->tilesize / 2;
	    int dx = 0, dy = 0;

            switch(button) {
            case CURSOR_UP:
		dy = -movement_increment;
		break;
            case CURSOR_DOWN:
		dy = movement_increment;
		break;
            case CURSOR_LEFT:
		dx = -movement_increment;
		break;
            case CURSOR_RIGHT:
		dx = movement_increment;
                break;
            default:
                break;
            }

	    /* This code has a slightly inconvenient interaction with
	     * the snap to grid feature: if the point being dragged
	     * originates on a non-grid point which is in the bottom
	     * half or right half (or both) of a grid cell (a 75%
	     * probability), then dragging point right (if it
	     * originates from the right half) or down (if it
	     * originates from the bottom half) will cause the point
	     * to move one more grid cell than intended in that
	     * direction. I (F. Wei) it wasn't worth handling this
	     * corner case - if anyone feels inclined, please feel
	     * free to do so. */
	    place_dragged_point(state, ui, ds,
				ui->newpoint.x * ds->tilesize / ui->newpoint.d + dx,
				ui->newpoint.y * ds->tilesize / ui->newpoint.d + dy);
	    return MOVE_UI_UPDATE;
        }
    } else if(button == CURSOR_SELECT) {
        if(ui->dragpoint < 0 && ui->cursorpoint >= 0) {
            /* begin drag */
            ui->dragtype = DRAG_MOVE_POINT;
            ui->dragpoint = ui->cursorpoint;
            ui->cursorpoint = -1;
            ui->newpoint.x = state->pts[ui->dragpoint].x * ds->tilesize / state->pts[ui->dragpoint].d;
            ui->newpoint.y = state->pts[ui->dragpoint].y * ds->tilesize / state->pts[ui->dragpoint].d;
            ui->newpoint.d = ds->tilesize;
            return MOVE_UI_UPDATE;
        }
        else if(ui->dragpoint >= 0) {
            /* end drag */
            int p = ui->dragpoint;
            char buf[80];

            ui->cursorpoint = ui->dragpoint;
            ui->dragpoint = -1;	       /* terminate drag, no matter what */

            /*
             * First, see if we're within range. The user can cancel a
             * drag by dragging the point right off the window.
             */
            if (ui->newpoint.x < 0 ||
                ui->newpoint.x >= (long)state->w*ui->newpoint.d ||
                ui->newpoint.y < 0 ||
                ui->newpoint.y >= (long)state->h*ui->newpoint.d)
                return MOVE_UI_UPDATE;

            /*
             * We aren't cancelling the drag. Construct a move string
             * indicating where this point is going to.
             */
            sprintf(buf, "P%d:%ld,%ld/%ld", p,
                    ui->newpoint.x, ui->newpoint.y, ui->newpoint.d);
            ui->just_dragged = true;
            return dupstr(buf);
        }
        else if(ui->cursorpoint < 0) {
            ui->cursorpoint = 0;
            return MOVE_UI_UPDATE;
        }
    } else if(STRIP_BUTTON_MODIFIERS(button) == CURSOR_SELECT2 ||
	      STRIP_BUTTON_MODIFIERS(button) == '\t') {
	/* Use spacebar or tab to cycle through the points. Shift
	 * reverses cycle direction. */
	if(ui->dragpoint >= 0)
	    return MOVE_NO_EFFECT;
	if(ui->cursorpoint < 0) {
	    ui->cursorpoint = 0;
	    return MOVE_UI_UPDATE;
	}
	assert(ui->cursorpoint >= 0);

        /* cursorpoint is valid - increment it */
	int direction = (button & MOD_SHFT) ? -1 : 1;
	ui->cursorpoint = (ui->cursorpoint + direction + state->params.n) % state->params.n;
	return MOVE_UI_UPDATE;
    }

    return MOVE_UNUSED;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int n = state->params.n;
    int p, k;
    long x, y, d;
    game_state *ret = dup_game(state);

#ifndef EDITOR
    ret->just_solved = false;
#endif

#ifdef EDITOR
    if (*move == 'E') {
        bool add;
        int a, b;

        move++;
        if (*move == 'A')
            add = true;
        else if (*move == 'D')
            add = false;
        else {
            free_game(ret);
            return NULL;
        }

        move++;
        a = atoi(move);
        while (*move && isdigit((unsigned char)*move))
            move++;

        if (*move != ',') {
            free_game(ret);
            return NULL;
        }
        move++;

        b = atoi(move);

        if (a >= 0 && a < n && b >= 0 && b < n && a != b) {
            if (add)
                addedge(ret->graph->edges, a, b);
            else
                deledge(ret->graph->edges, a, b);
            return ret;
        } else {
            free_game(ret);
            return NULL;
        }
    }
#endif

    while (*move) {
#ifndef EDITOR
	if (*move == 'S') {
	    move++;
	    if (*move == ';') move++;
	    ret->cheated = ret->just_solved = true;
	}
#endif
	if (*move == 'P' &&
	    sscanf(move+1, "%d:%ld,%ld/%ld%n", &p, &x, &y, &d, &k) == 4 &&
	    p >= 0 && p < n && d > 0) {
	    ret->pts[p].x = x;
	    ret->pts[p].y = y;
	    ret->pts[p].d = d;

	    move += k+1;
	    if (*move == ';') move++;
	} else {
	    free_game(ret);
	    return NULL;
	}
    }

#ifndef EDITOR
    mark_crossings(ret);
#endif

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    *x = *y = COORDLIMIT(params->n) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    /*
     * COL_BACKGROUND is what we use as the normal background colour.
     * Unusually, though, it isn't colour #0: COL_SYSBACKGROUND, a bit
     * darker, takes that place. This means that if the user resizes
     * an Untangle window so as to change its aspect ratio, the
     * still-square playable area will be distinguished from the dead
     * space around it.
     */
    game_mkhighlight(fe, ret, COL_BACKGROUND, -1, COL_SYSBACKGROUND);

    ret[COL_LINE * 3 + 0] = 0.0F;
    ret[COL_LINE * 3 + 1] = 0.0F;
    ret[COL_LINE * 3 + 2] = 0.0F;

    ret[COL_CROSSEDLINE * 3 + 0] = 1.0F;
    ret[COL_CROSSEDLINE * 3 + 1] = 0.0F;
    ret[COL_CROSSEDLINE * 3 + 2] = 0.0F;

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    ret[COL_POINT * 3 + 0] = 0.0F;
    ret[COL_POINT * 3 + 1] = 0.0F;
    ret[COL_POINT * 3 + 2] = 1.0F;

    ret[COL_DRAGPOINT * 3 + 0] = 1.0F;
    ret[COL_DRAGPOINT * 3 + 1] = 1.0F;
    ret[COL_DRAGPOINT * 3 + 2] = 1.0F;

    ret[COL_CURSORPOINT * 3 + 0] = 0.5F;
    ret[COL_CURSORPOINT * 3 + 1] = 0.5F;
    ret[COL_CURSORPOINT * 3 + 2] = 0.5F;

    ret[COL_NEIGHBOUR * 3 + 0] = 1.0F;
    ret[COL_NEIGHBOUR * 3 + 1] = 0.0F;
    ret[COL_NEIGHBOUR * 3 + 2] = 0.0F;

    ret[COL_FLASH1 * 3 + 0] = 0.5F;
    ret[COL_FLASH1 * 3 + 1] = 0.5F;
    ret[COL_FLASH1 * 3 + 2] = 0.5F;

    ret[COL_FLASH2 * 3 + 0] = 1.0F;
    ret[COL_FLASH2 * 3 + 1] = 1.0F;
    ret[COL_FLASH2 * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->x = snewn(state->params.n, long);
    ds->y = snewn(state->params.n, long);
    for (i = 0; i < state->params.n; i++)
        ds->x[i] = ds->y[i] = -1;
    ds->bg = -1;
    ds->dragpoint = -1;
    ds->cursorpoint = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->y);
    sfree(ds->x);
    sfree(ds);
}

static point mix(point a, point b, float distance)
{
    point ret;

    ret.d = a.d * b.d;
    ret.x = (long)(a.x * b.d + distance * (b.x * a.d - a.x * b.d));
    ret.y = (long)(a.y * b.d + distance * (b.y * a.d - a.y * b.d));

    return ret;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w, h;
    edge *e;
    int i, j;
    int bg;
    bool points_moved;
#ifdef EDITOR
    bool edges_changed;
#endif

    /*
     * There's no terribly sensible way to do partial redraws of
     * this game, so I'm going to have to resort to redrawing the
     * whole thing every time.
     */

    if (flashtime == 0)
        bg = COL_BACKGROUND;
    else if ((int)(flashtime * 4 / FLASH_TIME) % 2 == 0)
        bg = COL_FLASH1;
    else
        bg = COL_FLASH2;

    /*
     * To prevent excessive spinning on redraw during a completion
     * flash, we first check to see if _either_ the flash
     * background colour has changed _or_ at least one point has
     * moved _or_ a drag has begun or ended, and abandon the redraw
     * if neither is the case.
     * 
     * Also in this loop we work out the coordinates of all the
     * points for this redraw.
     */
    points_moved = false;
    for (i = 0; i < state->params.n; i++) {
        point p = state->pts[i];
        long x, y;

        if (ui->dragpoint == i && ui->dragtype == DRAG_MOVE_POINT)
            p = ui->newpoint;

        if (oldstate)
            p = mix(oldstate->pts[i], p, animtime / ui->anim_length);

	x = p.x * ds->tilesize / p.d;
	y = p.y * ds->tilesize / p.d;

        if (ds->x[i] != x || ds->y[i] != y)
            points_moved = true;

        ds->x[i] = x;
        ds->y[i] = y;
    }

#ifdef EDITOR
    edges_changed = false;
    if (oldstate) {
        for (i = 0;; i++) {
            edge *eold = index234(oldstate->graph->edges, i);
            edge *enew = index234(state->graph->edges, i);
            if (!eold && !enew)
                break;
            if (!eold || !enew) {
                edges_changed = true;
                break;
            }
            if (eold->a != enew->a || eold->b != enew->b) {
                edges_changed = true;
                break;
            }
        }
    }
#endif

    if (ds->bg == bg &&
	ds->dragpoint == ui->dragpoint &&
	ds->cursorpoint == ui->cursorpoint &&
#ifdef EDITOR
        !edges_changed &&
#endif
        !points_moved)
        return;                        /* nothing to do */

    ds->dragpoint = ui->dragpoint;
    ds->cursorpoint = ui->cursorpoint;
    ds->bg = bg;

    game_compute_size(&state->params, ds->tilesize, ui, &w, &h);
    draw_rect(dr, 0, 0, w, h, bg);

    /*
     * Draw the edges.
     */

    for (i = 0; (e = index234(state->graph->edges, i)) != NULL; i++) {
#ifndef EDITOR
        int colour = ui->show_crossed_edges &&
            (oldstate?oldstate:state)->crosses[i] ?
            COL_CROSSEDLINE : COL_LINE;
#else
      int colour = COL_LINE;
#endif

	draw_line(dr, ds->x[e->a], ds->y[e->a], ds->x[e->b], ds->y[e->b],
                  colour);
    }

    /*
     * Draw the points.
     *
     * When dragging, we vary the point colours to highlight the drag
     * point and neighbour points. The draw order is defined so that
     * the most relevant points (i.e., the dragged point and cursor
     * point) are drawn last, so they appear on top of other points.
     */
    static const int draw_order[] = {
	COL_POINT,
	COL_NEIGHBOUR,
	COL_CURSORPOINT,
	COL_DRAGPOINT
    };

    for (j = 0; j < 4; j++) {
	int thisc = draw_order[j];
	for (i = 0; i < state->params.n; i++) {
            int c;

	    if (ui->dragpoint == i) {
		c = COL_DRAGPOINT;
	    } else if(ui->cursorpoint == i) {
                c = COL_CURSORPOINT;
            } else if (ui->dragpoint >= 0 &&
		       isedge(state->graph->edges, ui->dragpoint, i)) {
		c = COL_NEIGHBOUR;
	    } else {
		c = COL_POINT;
	    }

	    if (c == thisc) {
                if (ui->vertex_numbers) {
                    char buf[80];
                    draw_circle(dr, ds->x[i], ds->y[i], CIRCLE_RADIUS*2, bg, bg);
                    sprintf(buf, "%d", i);
                    draw_text(dr, ds->x[i], ds->y[i], FONT_VARIABLE,
                              CIRCLE_RADIUS*3,
                              ALIGN_VCENTRE|ALIGN_HCENTRE, c, buf);
                } else {
                    draw_circle(dr, ds->x[i], ds->y[i], CIRCLE_RADIUS,
                                c, COL_OUTLINE);
                }
	    }
	}
    }

    draw_update(dr, 0, 0, w, h);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    if (ui->just_moved)
	return 0.0F;
#ifndef EDITOR
    if ((dir < 0 ? oldstate : newstate)->just_solved)
	ui->anim_length = SOLVEANIM_TIME;
    else
	ui->anim_length = ANIM_TIME;
#else
    ui->anim_length = ANIM_TIME;
#endif
    return ui->anim_length;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
#ifndef EDITOR
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
#endif
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    point pt;
    if (ui->dragpoint >= 0 && ui->dragtype == DRAG_MOVE_POINT)
	pt = ui->newpoint;
    else if(ui->cursorpoint >= 0)
	pt = state->pts[ui->cursorpoint];
    else
	return;

    int cx = ds->tilesize * pt.x / pt.d;
    int cy = ds->tilesize * pt.y / pt.d;

    *x = cx - CIRCLE_RADIUS;
    *y = cy - CIRCLE_RADIUS;
    *w = *h = 2 * CIRCLE_RADIUS + 1;
}

static int game_status(const game_state *state)
{
#ifdef EDITOR
    return 0;
#else
    return state->completed ? +1 : 0;
#endif
}

#ifdef COMBINED
#define thegame untangle
#endif

const struct game thegame = {
    "Untangle", "games.untangle", "untangle",
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
#ifndef EDITOR
    true, solve_game,
    false, NULL, NULL, /* can_format_as_text_now, text_format */
#else
    false, NULL,
    true, game_can_format_as_text_now, game_text_format,
#endif
    get_prefs, set_prefs,
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
    game_request_keys,
    NULL,  /* android_cursor_visibility */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
#ifndef NO_PRINTING
    false, false, NULL, NULL,          /* print_size, print */
#endif
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    SOLVE_ANIMATES,		       /* flags */
};
