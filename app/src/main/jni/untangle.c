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
 *
 *  - It would be nice if we could somehow auto-detect a real `long
 *    long' type on the host platform and use it in place of my
 *    hand-hacked int64s. It'd be faster and more reliable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

#define CIRCLE_RADIUS 6
#ifdef ANDROID
#define DRAG_THRESHOLD (CIRCLE_RADIUS * 10)
#else
#define DRAG_THRESHOLD (CIRCLE_RADIUS * 2)
#endif
#define PREFERRED_TILESIZE 64

#define FLASH_TIME 0.30F
#define ANIM_TIME 0.13F
#define SOLVEANIM_TIME 0.50F

enum {
    COL_SYSBACKGROUND,
    COL_BACKGROUND,
    COL_LINE,
#ifdef SHOW_CROSSINGS
    COL_CROSSEDLINE,
#endif
    COL_OUTLINE,
    COL_POINT,
    COL_DRAGPOINT,
    COL_NEIGHBOUR,
    COL_FLASH1,
    COL_FLASH2,
    NCOLOURS
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
#ifdef SHOW_CROSSINGS
    int *crosses;		       /* mark edges which are crossed */
#endif
    struct graph *graph;
    int completed, cheated, just_solved;
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

static int game_fetch_preset(int i, char **name, game_params **params)
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
      default: return FALSE;
    }

    sprintf(buf, _("%d points"), n);
    *name = dupstr(buf);

    *params = ret = snew(game_params);
    ret->n = n;

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

static void decode_params(game_params *params, char const *string)
{
    params->n = atoi(string);
}

static char *encode_params(const game_params *params, int full)
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
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = NULL;
    ret[1].type = C_END;
    ret[1].sval = NULL;
    ret[1].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->n = atoi(cfg[0].sval);

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->n < 4)
        return _("Number of points must be at least four");
    return NULL;
}

/* ----------------------------------------------------------------------
 * Small number of 64-bit integer arithmetic operations, to prevent
 * integer overflow at the very core of cross().
 */

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

/*
 * Determine whether the line segments between a1 and a2, and
 * between b1 and b2, intersect. We count it as an intersection if
 * any of the endpoints lies _on_ the other line.
 */
static int cross(point a1, point a2, point b1, point b2)
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
	return FALSE;

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
	    return FALSE;
	/* Otherwise, take the dot product of a2-a1 with itself. If
	 * the other two dot products both exceed this, the lines do
	 * not cross. */
	d3 = dotprod64(px, px, py, py);
	if (greater64(d1, d3) && greater64(d2, d3))
	    return FALSE;
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
	return FALSE;

    /*
     * The lines must cross.
     */
    return TRUE;
}

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

    add234(edges, e);
}

static int isedge(tree234 *edges, int a, int b)
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

static int vertcmpC(const void *av, const void *bv)
{
    const vertex *a = (vertex *)av;
    const vertex *b = (vertex *)bv;

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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
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
	int added = FALSE;

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
		added = TRUE;
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
     * We're done. Now encode the graph in a string format. Let's
     * use a comma-separated list of dash-separated vertex number
     * pairs, numbered from zero. We'll sort the list to prevent
     * side channels.
     */
    ret = NULL;
    {
	char *sep;
	char buf[80];
	int retlen;
	edge *ea;

	retlen = 0;
	m = count234(edges);
	ea = snewn(m, edge);
	for (i = 0; (e = index234(edges, i)) != NULL; i++) {
	    assert(i < m);
	    ea[i].a = min(tmp[e->a], tmp[e->b]);
	    ea[i].b = max(tmp[e->a], tmp[e->b]);
	    retlen += 1 + sprintf(buf, "%d-%d", ea[i].a, ea[i].b);
	}
	assert(i == m);
	qsort(ea, m, sizeof(*ea), edgecmpC);

	ret = snewn(retlen, char);
	sep = "";
	k = 0;

	for (i = 0; i < m; i++) {
	    k += sprintf(ret + k, "%s%d-%d", sep, ea[i].a, ea[i].b);
	    sep = ",";
	}
	assert(k < retlen);

	sfree(ea);
    }

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
}

static char *validate_desc(const game_params *params, const char *desc)
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
    }

    return NULL;
}

static void mark_crossings(game_state *state)
{
    int ok = TRUE;
    int i, j;
    edge *e, *e2;

#ifdef SHOW_CROSSINGS
    for (i = 0; (e = index234(state->graph->edges, i)) != NULL; i++)
	state->crosses[i] = FALSE;
#endif

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
		ok = FALSE;
#ifdef SHOW_CROSSINGS
		state->crosses[i] = state->crosses[j] = TRUE;
#else
		goto done;	       /* multi-level break - sorry */
#endif
	    }
	}
    }

    /*
     * e == NULL if we've gone through all the edge pairs
     * without finding a crossing.
     */
#ifndef SHOW_CROSSINGS
    done:
#endif
    if (ok)
	state->completed = TRUE;
}

#ifdef ANDROID
static void android_request_keys(const game_params *params)
{
	android_keys("", ANDROID_NO_ARROWS);
}
#endif

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
    state->completed = state->cheated = state->just_solved = FALSE;

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

#ifdef SHOW_CROSSINGS
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
    ret->graph = state->graph;
    ret->graph->refcount++;
    ret->completed = state->completed;
    ret->cheated = state->cheated;
    ret->just_solved = state->just_solved;
#ifdef SHOW_CROSSINGS
    ret->crosses = snewn(count234(ret->graph->edges), int);
    memcpy(ret->crosses, state->crosses,
	   count234(ret->graph->edges) * sizeof(int));
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
    sfree(state->pts);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
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

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

struct game_ui {
    int dragpoint;		       /* point being dragged; -1 if none */
    point newpoint;		       /* where it's been dragged to so far */
    int just_dragged;		       /* reset in game_changed_state */
    int just_moved;		       /* _set_ in game_changed_state */
    float anim_length;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->dragpoint = -1;
    ui->just_moved = ui->just_dragged = FALSE;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
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
    ui->dragpoint = -1;
    ui->just_moved = ui->just_dragged;
    ui->just_dragged = FALSE;
#ifdef ANDROID
    if (newstate->completed && ! newstate->cheated && oldstate && ! oldstate->completed) android_completed();
#endif
}

struct game_drawstate {
    long tilesize;
    int bg, dragpoint;
    long *x, *y;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int n = state->params.n;

    if (IS_MOUSE_DOWN(button)) {
	int i, best;
        long bestd;

	/*
	 * Begin drag. We drag the vertex _nearest_ to the pointer,
	 * just in case one is nearly on top of another and we want
	 * to drag the latter. However, we drag nothing at all if
	 * the nearest vertex is outside DRAG_THRESHOLD.
	 */
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

	if (bestd <= DRAG_THRESHOLD * DRAG_THRESHOLD) {
	    ui->dragpoint = best;
	    ui->newpoint.x = x;
	    ui->newpoint.y = y;
	    ui->newpoint.d = ds->tilesize;
	    return "";
	}

    } else if (IS_MOUSE_DRAG(button) && ui->dragpoint >= 0) {
	ui->newpoint.x = x;
	ui->newpoint.y = y;
	ui->newpoint.d = ds->tilesize;
	return "";
    } else if (IS_MOUSE_RELEASE(button) && ui->dragpoint >= 0) {
	int p = ui->dragpoint;
	char buf[80];

	ui->dragpoint = -1;	       /* terminate drag, no matter what */

	/*
	 * First, see if we're within range. The user can cancel a
	 * drag by dragging the point right off the window.
	 */
	if (ui->newpoint.x < 0 ||
            ui->newpoint.x >= (long)state->w*ui->newpoint.d ||
	    ui->newpoint.y < 0 ||
            ui->newpoint.y >= (long)state->h*ui->newpoint.d)
	    return "";

	/*
	 * We aren't cancelling the drag. Construct a move string
	 * indicating where this point is going to.
	 */
	sprintf(buf, "P%d:%ld,%ld/%ld", p,
		ui->newpoint.x, ui->newpoint.y, ui->newpoint.d);
	ui->just_dragged = TRUE;
	return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int n = state->params.n;
    int p, k;
    long x, y, d;
    game_state *ret = dup_game(state);

    ret->just_solved = FALSE;

    while (*move) {
	if (*move == 'S') {
	    move++;
	    if (*move == ';') move++;
	    ret->cheated = ret->just_solved = TRUE;
	}
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

    mark_crossings(ret);

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
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

#ifdef SHOW_CROSSINGS
    ret[COL_CROSSEDLINE * 3 + 0] = 1.0F;
    ret[COL_CROSSEDLINE * 3 + 1] = 0.0F;
    ret[COL_CROSSEDLINE * 3 + 2] = 0.0F;
#endif

    ret[COL_OUTLINE * 3 + 0] = 0.0F;
    ret[COL_OUTLINE * 3 + 1] = 0.0F;
    ret[COL_OUTLINE * 3 + 2] = 0.0F;

    ret[COL_POINT * 3 + 0] = 0.0F;
    ret[COL_POINT * 3 + 1] = 0.0F;
    ret[COL_POINT * 3 + 2] = 1.0F;

    ret[COL_DRAGPOINT * 3 + 0] = 1.0F;
    ret[COL_DRAGPOINT * 3 + 1] = 1.0F;
    ret[COL_DRAGPOINT * 3 + 2] = 1.0F;

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
    int bg, points_moved;

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
    points_moved = FALSE;
    for (i = 0; i < state->params.n; i++) {
        point p = state->pts[i];
        long x, y;

        if (ui->dragpoint == i)
            p = ui->newpoint;

        if (oldstate)
            p = mix(oldstate->pts[i], p, animtime / ui->anim_length);

	x = p.x * ds->tilesize / p.d;
	y = p.y * ds->tilesize / p.d;

        if (ds->x[i] != x || ds->y[i] != y)
            points_moved = TRUE;

        ds->x[i] = x;
        ds->y[i] = y;
    }

    if (ds->bg == bg && ds->dragpoint == ui->dragpoint && !points_moved)
        return;                        /* nothing to do */

    ds->dragpoint = ui->dragpoint;
    ds->bg = bg;

    game_compute_size(&state->params, ds->tilesize, &w, &h);
    draw_rect(dr, 0, 0, w, h, bg);

    /*
     * Draw the edges.
     */

    for (i = 0; (e = index234(state->graph->edges, i)) != NULL; i++) {
	draw_line(dr, ds->x[e->a], ds->y[e->a], ds->x[e->b], ds->y[e->b],
#ifdef SHOW_CROSSINGS
		  (oldstate?oldstate:state)->crosses[i] ?
		  COL_CROSSEDLINE :
#endif
		  COL_LINE);
    }

    /*
     * Draw the points.
     * 
     * When dragging, we should not only vary the colours, but
     * leave the point being dragged until last.
     */
    for (j = 0; j < 3; j++) {
	int thisc = (j == 0 ? COL_POINT :
		     j == 1 ? COL_NEIGHBOUR : COL_DRAGPOINT);
	for (i = 0; i < state->params.n; i++) {
            int c;

	    if (ui->dragpoint == i) {
		c = COL_DRAGPOINT;
	    } else if (ui->dragpoint >= 0 &&
		       isedge(state->graph->edges, ui->dragpoint, i)) {
		c = COL_NEIGHBOUR;
	    } else {
		c = COL_POINT;
	    }

	    if (c == thisc) {
#ifdef VERTEX_NUMBERS
		draw_circle(dr, ds->x[i], ds->y[i], DRAG_THRESHOLD, bg, bg);
		{
		    char buf[80];
		    sprintf(buf, "%d", i);
		    draw_text(dr, ds->x[i], ds->y[i], FONT_VARIABLE,
                              DRAG_THRESHOLD*3/2,
			      ALIGN_VCENTRE|ALIGN_HCENTRE, c, buf);
		}
#else
		draw_circle(dr, ds->x[i], ds->y[i], CIRCLE_RADIUS,
                            c, COL_OUTLINE);
#endif
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
    if ((dir < 0 ? oldstate : newstate)->just_solved)
	ui->anim_length = SOLVEANIM_TIME;
    else
	ui->anim_length = ANIM_TIME;
    return ui->anim_length;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
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
#define thegame untangle
#endif

const struct game thegame = {
    "Untangle", "games.untangle", "untangle",
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
    TRUE, solve_game,
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
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
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
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    SOLVE_ANIMATES,		       /* flags */
};
