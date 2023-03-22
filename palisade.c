/* -*- indent-tabs-mode: nil; tab-width: 1000 -*- */

/*
 * palisade.c: Nikoli's `Five Cells' puzzle.
 *
 * See http://nikoli.co.jp/en/puzzles/five_cells.html
 */

/* TODO:
 *
 * - better solver: implement the sketched-out deductions
 *
 * - improve the victory flash?
 *    - the LINE_NOs look ugly against COL_FLASH.
 *    - white-blink the edges (instead), a la loopy?
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#define setmem(ptr, byte, len) memset((ptr), (byte), (len) * sizeof (ptr)[0])
#define scopy(dst, src, len) memcpy((dst), (src), (len) * sizeof (dst)[0])
#define dupmem(p, n) memcpy(smalloc(n * sizeof (*p)), p, n * sizeof (*p))
#define snewa(ptr, len) (ptr) = smalloc((len) * sizeof (*ptr))
#define clone(ptr) (dupmem((ptr), 1))

static char *string(int n, const char *fmt, ...)
{
    va_list va;
    char *ret;
    int m;
    va_start(va, fmt);
    m = vsprintf(snewa(ret, n + 1), fmt, va);
    va_end(va);
    if (m > n) fatal("memory corruption");
    return ret;
}

struct game_params {
    int w, h, k;
};

typedef signed char clue;
typedef unsigned char borderflag;

typedef struct shared_state {
    game_params params;
    clue *clues;
    int refcount;
} shared_state;

struct game_state {
    shared_state *shared;
    borderflag *borders; /* length w*h */

    bool completed, cheated;
};

#define DEFAULT_PRESET 0
static struct game_params presets[] = {
    {5, 5, 5}, {8, 6, 6}, {10, 8, 8}, {15, 12, 10}
    /* I definitely want 5x5n5 since that gives "Five Cells" its name.
     * But how about the others?  By which criteria do I choose? */
};

static game_params *default_params(void)
{
    return clone(&presets[DEFAULT_PRESET]);
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    if (i < 0 || i >= lenof(presets)) return false;

    *params = clone(&presets[i]);
    *name = string(60, "%d x %d, regions of size %d",
                   presets[i].w, presets[i].h, presets[i].k);

    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    return clone(params);
}

static void decode_params(game_params *params, char const *string)
{
    params->w = params->h = params->k = atoi(string);
    while (*string && isdigit((unsigned char)*string)) ++string;
    if (*string == 'x') {
        params->h = atoi(++string);
        while (*string && isdigit((unsigned char)*string)) ++string;
    }
    if (*string == 'n') params->k = atoi(++string);
}

static char *encode_params(const game_params *params, bool full)
{
    return string(40, "%dx%dn%d", params->w, params->h, params->k);
}

#define CONFIG(i, nm, ty, iv, sv) \
    (ret[i].name = nm, ret[i].type = ty, ret[i].ival = iv, ret[i].sval = sv)

static config_item *game_configure(const game_params *params)
{
    config_item *ret = snewn(4, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    ret[0].u.string.sval = string(20, "%d", params->w);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    ret[1].u.string.sval = string(20, "%d", params->h);

    ret[2].name = "Region size";
    ret[2].type = C_STRING;
    ret[2].u.string.sval = string(20, "%d", params->k);

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *params = snew(game_params);

    params->w = atoi(cfg[0].u.string.sval);
    params->h = atoi(cfg[1].u.string.sval);
    params->k = atoi(cfg[2].u.string.sval);

    return params;
}

/* +---+  <<  The one possible domino (up to symmetry).      +---+---+
 * | 3 |                                                     | 3 | 3 |
 * |   |   If two dominos are adjacent as depicted here  >>  +---+---+
 * | 3 |   then it's ambiguous whether the edge between      | 3 | 3 |
 * +---+   the dominos is horizontal or vertical.            +---+---+
 */

static const char *validate_params(const game_params *params, bool full)
{
    int w = params->w, h = params->h, k = params->k, wh;

    if (k < 1) return "Region size must be at least one";
    if (w < 1) return "Width must be at least one";
    if (h < 1) return "Height must be at least one";
    if (w > INT_MAX / h)
        return "Width times height must not be unreasonably large";
    wh = w * h;
    if (wh % k) return "Region size must divide grid area";
    if (!full) return NULL; /* succeed partial validation */

    /* MAYBE FIXME: we (just?) don't have the UI for winning these. */
    if (k == wh) return "Region size must be less than the grid area";
    assert (k < wh); /* or wh % k != 0 */

    if (k == 2 && w != 1 && h != 1)
        return "Region size can't be two unless width or height is one";

    return NULL; /* succeed full validation */
}

/* --- Solver ------------------------------------------------------- */

/* the solver may write at will to these arrays, but shouldn't free them */
/* it's up to the client to dup/free as needed */
typedef struct solver_ctx {
    const game_params *params;  /* also in shared_state */
    clue *clues;                /* also in shared_state */
    borderflag *borders;        /* also in game_state */
    int *dsf;                   /* particular to the solver */
} solver_ctx;

/* Deductions:
 *
 * - If two adjacent clues do not have a border between them, this
 *   gives a lower limit on the size of their region (which is also an
 *   upper limit if both clues are 3).  Rule out any non-border which
 *   would make its region either too large or too small.
 *
 * - If a clue, k, is adjacent to k borders or (4 - k) non-borders,
 *   the remaining edges incident to the clue are readily decided.
 *
 * - If a region has only one other region (e.g. square) to grow into
 *   and it's not of full size yet, grow it into that one region.
 *
 * - If two regions are adjacent and their combined size would be too
 *   large, put an edge between them.
 *
 * - If a border is adjacent to two non-borders, its last vertex-mate
 *   must also be a border.  If a maybe-border is adjacent to three
 *   nonborders, the maybe-border is a non-border.
 *
 * - If a clue square is adjacent to several squares belonging to the
 *   same region, and enabling (disabling) those borders would violate
 *   the clue, those borders must be disabled (enabled).
 *
 * - If there's a path crossing only non-borders between two squares,
 *   the maybe-border between them is a non-border.
 *   (This is implicitly computed in the dsf representation)
 */

/* TODO deductions:
 *
 * If a vertex is adjacent to a LINE_YES and (4-3)*LINE_NO, at least
 * one of the last two edges are LINE_YES.  If they're adjacent to a
 * 1, then the other two edges incident to that 1 are LINE_NO.
 *
 * For each square: set all as unknown, then for each k-omino and each
 * way of placing it on that square, if that way is consistent with
 * the board, mark its edges and interior as possible LINE_YES and
 * LINE_NO, respectively.  When all k-ominos are through, see what
 * isn't possible and remove those impossibilities from the board.
 * (Sounds pretty nasty for k > 4 or so.)
 *
 * A black-bordered subregion must have a size divisible by k.  So,
 * draw a graph with one node per dsf component and edges between
 * those dsf components which have adjacent squares.  Identify cut
 * vertices and edges.  If a cut-vertex-delimited component contains a
 * number of squares not divisible by k, cut vertex not included, then
 * the cut vertex must belong to the component.  If it has exactly one
 * edge _out_ of the component, the line(s) corresponding to that edge
 * are all LINE_YES (i.e. a BORDER()).
 * (This sounds complicated, but visually it is rather easy.)
 *
 * [Look at loopy and see how the at-least/-most k out of m edges
 * thing is done.  See how it is propagated across multiple squares.]
 */

#define EMPTY (~0)

#define BIT(i) (1 << (i))
#define BORDER(i) BIT(i)
#define BORDER_U BORDER(0)
#define BORDER_R BORDER(1)
#define BORDER_D BORDER(2)
#define BORDER_L BORDER(3)
#define FLIP(i) ((i) ^ 2)
#define BORDER_MASK (BORDER_U|BORDER_R|BORDER_D|BORDER_L)
#define DISABLED(border) ((border) << 4)
#define UNDISABLED(border) ((border) >> 4)

static const int dx[4] = { 0, +1,  0, -1};
static const int dy[4] = {-1,  0, +1,  0};
static const int bitcount[16] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
/* bitcount[x & BORDER_MASK] == number of enabled borders */

#define COMPUTE_J (-1)

static void connect(solver_ctx *ctx, int i, int j)
{
    dsf_merge(ctx->dsf, i, j);
}

static bool connected(solver_ctx *ctx, int i, int j, int dir)
{
    if (j == COMPUTE_J) j = i + dx[dir] + ctx->params->w*dy[dir];
    return dsf_canonify(ctx->dsf, i) == dsf_canonify(ctx->dsf, j);
}

static void disconnect(solver_ctx *ctx, int i, int j, int dir)
{
    if (j == COMPUTE_J) j = i + dx[dir] + ctx->params->w*dy[dir];
    ctx->borders[i] |= BORDER(dir);
    ctx->borders[j] |= BORDER(FLIP(dir));
}

static bool disconnected(solver_ctx *ctx, int i, int j, int dir)
{
    assert (j == COMPUTE_J || j == i + dx[dir] + ctx->params->w*dy[dir]);
    return ctx->borders[i] & BORDER(dir);
}

static bool maybe(solver_ctx *ctx, int i, int j, int dir)
{
    assert (j == COMPUTE_J || j == i + dx[dir] + ctx->params->w*dy[dir]);
    return !disconnected(ctx, i, j, dir) && !connected(ctx, i, j, dir);
    /* the ordering is important: disconnected works for invalid
     * squares (i.e. out of bounds), connected doesn't. */
}

static void solver_connected_clues_versus_region_size(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, wh = w*h, i, dir;

    /* If i is connected to j and i has borders with p of the
     * remaining three squares and j with q of the remaining three
     * squares, then the region has size at least 1+(3-p) + 1+(3-q).
     * If p = q = 3 then the region has size exactly 2. */

    for (i = 0; i < wh; ++i) {
        if (ctx->clues[i] == EMPTY) continue;
        for (dir = 0; dir < 4; ++dir) {
            int j = i + dx[dir] + w*dy[dir];
            if (disconnected(ctx, i, j, dir)) continue;
            if (ctx->clues[j] == EMPTY) continue;
            if ((8 - ctx->clues[i] - ctx->clues[j] > ctx->params->k) ||
                (ctx->clues[i] == 3 && ctx->clues[j] == 3 &&
                 ctx->params->k != 2))
            {
                disconnect(ctx, i, j, dir);
                /* changed = true, but this is a one-shot... */
            }
        }
    }
}

static bool solver_number_exhausted(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, wh = w*h, i, dir, off;
    bool changed = false;

    for (i = 0; i < wh; ++i) {
        if (ctx->clues[i] == EMPTY) continue;

        if (bitcount[(ctx->borders[i] & BORDER_MASK)] == ctx->clues[i]) {
            for (dir = 0; dir < 4; ++dir) {
                int j = i + dx[dir] + w*dy[dir];
                if (!maybe(ctx, i, j, dir)) continue;
                connect(ctx, i, j);
                changed = true;
            }
            continue;
        }

        for (off = dir = 0; dir < 4; ++dir) {
            int j = i + dx[dir] + w*dy[dir];
            if (!disconnected(ctx, i, j, dir) && connected(ctx, i, j, dir))
                ++off; /* ^^^ bounds checking before ^^^^^ */
        }

        if (ctx->clues[i] == 4 - off)
            for (dir = 0; dir < 4; ++dir) {
                int j = i + dx[dir] + w*dy[dir];
                if (!maybe(ctx, i, j, dir)) continue;
                disconnect(ctx, i, j, dir);
                changed = true;
            }
    }

    return changed;
}

static bool solver_not_too_big(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, wh = w*h, i, dir;
    bool changed = false;

    for (i = 0; i < wh; ++i) {
        int size = dsf_size(ctx->dsf, i);
        for (dir = 0; dir < 4; ++dir) {
            int j = i + dx[dir] + w*dy[dir];
            if (!maybe(ctx, i, j, dir)) continue;
            if (size + dsf_size(ctx->dsf, j) <= ctx->params->k) continue;
            disconnect(ctx, i, j, dir);
            changed = true;
        }
    }

    return changed;
}

static bool solver_not_too_small(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, wh = w*h, i, dir;
    int *outs, k = ctx->params->k, ci;
    bool changed = false;

    snewa(outs, wh);
    setmem(outs, -1, wh);

    for (i = 0; i < wh; ++i) {
        ci = dsf_canonify(ctx->dsf, i);
        if (dsf_size(ctx->dsf, ci) == k) continue;
        for (dir = 0; dir < 4; ++dir) {
            int j = i + dx[dir] + w*dy[dir];
            if (!maybe(ctx, i, j, dir)) continue;
            if (outs[ci] == -1) outs[ci] = dsf_canonify(ctx->dsf, j);
            else if (outs[ci] != dsf_canonify(ctx->dsf, j)) outs[ci] = -2;
        }
    }

    for (i = 0; i < wh; ++i) {
        int j = outs[i];
        if (i != dsf_canonify(ctx->dsf, i)) continue;
        if (j < 0) continue;
        connect(ctx, i, j); /* only one place for i to grow */
        changed = true;
    }

    sfree(outs);
    return changed;
}

static bool solver_no_dangling_edges(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, r, c;
    bool changed = false;

    /* for each vertex */
    for (r = 1; r < h; ++r)
        for (c = 1; c < w; ++c) {
            int i = r * w + c, j = i - w - 1, noline = 0, dir;
            int squares[4], e = -1, f = -1, de = -1, df = -1;

            /* feels hacky: I align these with BORDER_[U0 R1 D2 L3] */
            squares[1] = squares[2] = j;
            squares[0] = squares[3] = i;

            /* for each edge adjacent to the vertex */
            for (dir = 0; dir < 4; ++dir)
                if (!connected(ctx, squares[dir], COMPUTE_J, dir)) {
                    df = dir;
                    f = squares[df];
                    if (e != -1) continue;
                    e = f;
                    de = df;
                } else ++noline;

            if (4 - noline == 1) {
                assert (e != -1);
                disconnect(ctx, e, COMPUTE_J, de);
                changed = true;
                continue;
            }

            if (4 - noline != 2) continue;

            assert (e != -1);
            assert (f != -1);

            if (ctx->borders[e] & BORDER(de)) {
                if (!(ctx->borders[f] & BORDER(df))) {
                    disconnect(ctx, f, COMPUTE_J, df);
                    changed = true;
                }
            } else if (ctx->borders[f] & BORDER(df)) {
                disconnect(ctx, e, COMPUTE_J, de);
                changed = true;
            }
        }

    return changed;
}

static bool solver_equivalent_edges(solver_ctx *ctx)
{
    int w = ctx->params->w, h = ctx->params->h, wh = w*h, i, dirj;
    bool changed = false;

    /* if a square is adjacent to two connected squares, the two
     * borders (i,j) and (i,k) are either both on or both off. */

    for (i = 0; i < wh; ++i) {
        int n_on = 0, n_off = 0;
        if (ctx->clues[i] < 1 || ctx->clues[i] > 3) continue;

        if (ctx->clues[i] == 2 /* don't need it otherwise */)
            for (dirj = 0; dirj < 4; ++dirj) {
                int j = i + dx[dirj] + w*dy[dirj];
                if (disconnected(ctx, i, j, dirj)) ++n_on;
                else if (connected(ctx, i, j, dirj)) ++n_off;
            }

        for (dirj = 0; dirj < 4; ++dirj) {
            int j = i + dx[dirj] + w*dy[dirj], dirk;
            if (!maybe(ctx, i, j, dirj)) continue;

            for (dirk = dirj + 1; dirk < 4; ++dirk) {
                int k = i + dx[dirk] + w*dy[dirk];
                if (!maybe(ctx, i, k, dirk)) continue;
                if (!connected(ctx, j, k, -1)) continue;

                if (n_on + 2 > ctx->clues[i]) {
                    connect(ctx, i, j);
                    connect(ctx, i, k);
                    changed = true;
                } else if (n_off + 2 > 4 - ctx->clues[i]) {
                    disconnect(ctx, i, j, dirj);
                    disconnect(ctx, i, k, dirk);
                    changed = true;
                }
            }
        }
    }

    return changed;
}

/* build connected components in `dsf', along the lines of `borders'. */
static void build_dsf(int w, int h, borderflag *border, int *dsf, bool black)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x+1 < w && (black ? !(border[y*w+x] & BORDER_R) :
                            (border[y*w+x] & DISABLED(BORDER_R))))
                dsf_merge(dsf, y*w+x, y*w+(x+1));
            if (y+1 < h && (black ? !(border[y*w+x] & BORDER_D) :
                            (border[y*w+x] & DISABLED(BORDER_D))))
                dsf_merge(dsf, y*w+x, (y+1)*w+x);
        }
    }
}

static bool is_solved(const game_params *params, clue *clues,
                      borderflag *border)
{
    int w = params->w, h = params->h, wh = w*h, k = params->k;
    int i, x, y;
    int *dsf = snew_dsf(wh);

    build_dsf(w, h, border, dsf, true);

    /*
     * A game is solved if:
     *
     *  - the borders drawn on the grid divide it into connected
     *    components such that every square is in a component of the
     *    correct size
     *  - the borders also satisfy the clue set
     */
    for (i = 0; i < wh; ++i) {
        if (dsf_size(dsf, i) != k) goto error;
        if (clues[i] == EMPTY) continue;
        if (clues[i] != bitcount[border[i] & BORDER_MASK]) goto error;
    }

    /*
     * ... and thirdly:
     *
     *  - there are no *stray* borders, in that every border is
     *    actually part of the division between two components.
     *    Otherwise you could cheat by finding a subdivision which did
     *    not *exceed* any clue square's counter, and then adding a
     *    few extra edges.
     */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x+1 < w && (border[y*w+x] & BORDER_R) &&
                dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, y*w+(x+1)))
                goto error;
            if (y+1 < h && (border[y*w+x] & BORDER_D) &&
                dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, (y+1)*w+x))
                goto error;
        }
    }

    sfree(dsf);
    return true;

error:
    sfree(dsf);
    return false;
}

static bool solver(const game_params *params, clue *clues, borderflag *borders)
{
    int w = params->w, h = params->h, wh = w*h;
    bool changed;
    solver_ctx ctx;

    ctx.params = params;
    ctx.clues = clues;
    ctx.borders = borders;
    ctx.dsf = snew_dsf(wh);

    solver_connected_clues_versus_region_size(&ctx); /* idempotent */
    do {
        changed  = false;
        changed |= solver_number_exhausted(&ctx);
        changed |= solver_not_too_big(&ctx);
        changed |= solver_not_too_small(&ctx);
        changed |= solver_no_dangling_edges(&ctx);
        changed |= solver_equivalent_edges(&ctx);
    } while (changed);

    sfree(ctx.dsf);

    return is_solved(params, clues, borders);
}

/* --- Generator ---------------------------------------------------- */

static void init_borders(int w, int h, borderflag *borders)
{
    int r, c;
    setmem(borders, 0, w*h);
    for (c = 0; c < w; ++c) {
        borders[c] |= BORDER_U;
        borders[w*h-1 - c] |= BORDER_D;
    }
    for (r = 0; r < h; ++r) {
        borders[r*w] |= BORDER_L;
        borders[w*h-1 - r*w] |= BORDER_R;
    }
}

#define OUT_OF_BOUNDS(x, y, w, h) \
    ((x) < 0 || (x) >= (w) || (y) < 0 || (y) >= (h))

#define xshuffle(ptr, len, rs) shuffle((ptr), (len), sizeof (ptr)[0], (rs))

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    int w = params->w, h = params->h, wh = w*h, k = params->k;

    clue *numbers = snewn(wh + 1, clue);
    borderflag *rim = snewn(wh, borderflag);
    borderflag *scratch_borders = snewn(wh, borderflag);

    char *soln = snewa(*aux, wh + 2);
    int *shuf = snewn(wh, int);
    int *dsf = NULL, i, r, c;

    int attempts = 0;

    for (i = 0; i < wh; ++i) shuf[i] = i;
    xshuffle(shuf, wh, rs);

    init_borders(w, h, rim);

    assert (!('@' & BORDER_MASK));
    *soln++ = 'S';
    soln[wh] = '\0';

    do {
        ++attempts;
        setmem(soln, '@', wh);

        sfree(dsf);
        dsf = divvy_rectangle(w, h, k, rs);

        for (r = 0; r < h; ++r)
            for (c = 0; c < w; ++c) {
                int i = r * w + c, dir;
                numbers[i] = 0;
                for (dir = 0; dir < 4; ++dir) {
                    int rr = r + dy[dir], cc = c + dx[dir], ii = rr * w + cc;
                    if (OUT_OF_BOUNDS(cc, rr, w, h) ||
                        dsf_canonify(dsf, i) != dsf_canonify(dsf, ii)) {
                        ++numbers[i];
                        soln[i] |= BORDER(dir);
                    }
                }
            }

        scopy(scratch_borders, rim, wh);
    } while (!solver(params, numbers, scratch_borders));

    for (i = 0; i < wh; ++i) {
        int j = shuf[i];
        clue copy = numbers[j];

        scopy(scratch_borders, rim, wh);
        numbers[j] = EMPTY; /* strip away unnecssary clues */
        if (!solver(params, numbers, scratch_borders))
            numbers[j] = copy;
    }

    numbers[wh] = '\0';

    sfree(scratch_borders);
    sfree(rim);
    sfree(shuf);
    sfree(dsf);

    char *output = snewn(wh + 1, char), *p = output;

    r = 0;
    for (i = 0; i < wh; ++i) {
        if (numbers[i] != EMPTY) {
            while (r) {
                while (r > 26) {
                    *p++ = 'z';
                    r -= 26;
                }
                *p++ = 'a'-1 + r;
                r = 0;
            }
            *p++ = '0' + numbers[i];
        } else ++r;
    }
    *p++ = '\0';

    sfree(numbers);
    return sresize(output, p - output, char);
}

static const char *validate_desc(const game_params *params, const char *desc)
{

    int w = params->w, h = params->h, wh = w*h, squares = 0;

    for (/* nop */; *desc; ++desc) {
        if (islower((unsigned char)*desc)) {
            squares += *desc - 'a' + 1;
        } else if (isdigit((unsigned char)*desc)) {
            if (*desc > '4') {
                static char buf[] = "Invalid (too large) number: '5'";
                assert (isdigit((unsigned char)buf[lenof(buf) - 3]));
                buf[lenof(buf) - 3] = *desc; /* ... or 6, 7, 8, 9 :-) */
                return buf;
            }
            ++squares;
        } else if (isprint((unsigned char)*desc)) {
            static char buf[] = "Invalid character in data: '?'";
            buf[lenof(buf) - 3] = *desc;
            return buf;
        } else return "Invalid (unprintable) character in data";
    }

    if (squares > wh) return "Data describes too many squares";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, h = params->h, wh = w*h, i;
    game_state *state = snew(game_state);

    state->shared = snew(shared_state);
    state->shared->refcount = 1;
    state->shared->params = *params; /* struct copy */
    snewa(state->shared->clues, wh);

    setmem(state->shared->clues, EMPTY, wh);
    for (i = 0; *desc; ++desc) {
        if (isdigit((unsigned char)*desc)) state->shared->clues[i++] = *desc - '0';
        else if (isalpha((unsigned char)*desc)) i += *desc - 'a' + 1;
    }

    snewa(state->borders, wh);
    init_borders(w, h, state->borders);

    state->completed = (params->k == wh);
    state->cheated = false;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    int wh = state->shared->params.w * state->shared->params.h;
    game_state *ret = snew(game_state);

    ret->borders = dupmem(state->borders, wh);

    ret->shared = state->shared;
    ++ret->shared->refcount;

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    if (--state->shared->refcount == 0) {
        sfree(state->shared->clues);
        sfree(state->shared);
    }
    sfree(state->borders);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int w = state->shared->params.w, h = state->shared->params.h, wh = w*h;
    borderflag *move;

    if (aux) return dupstr(aux);

    snewa(move, wh + 2);

    move[0] = 'S';
    init_borders(w, h, move + 1);
    move[wh + 1] = '\0';

    if (solver(&state->shared->params, state->shared->clues, move + 1)) {
        int i;
        for (i = 0; i < wh; i++)
            move[i+1] |= '@';          /* turn into sensible ASCII */
        return (char *) move;
    }

    *error = "Sorry, I can't solve this puzzle";
    sfree(move);
    return NULL;

    {
        /* compile-time-assert (borderflag is-a-kind-of char).
         *
         * depends on zero-size arrays being disallowed.  GCC says
         * ISO C forbids this, pointing to [-Werror=edantic].  Also,
         * it depends on type-checking of (obviously) dead code. */
        borderflag b[sizeof (borderflag) == sizeof (char)];
        char c = b[0]; b[0] = c;
        /* we could at least in principle put this anywhere, but it
         * seems silly to not put it where the assumption is used. */
    }
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->shared->params.w, h = state->shared->params.h, r, c;
    int cw = 4, ch = 2, gw = cw*w + 2, gh = ch * h + 1, len = gw * gh;
    char *board;

    setmem(snewa(board, len + 1), ' ', len);
    for (r = 0; r < h; ++r) {
        for (c = 0; c < w; ++c) {
            int cell = r*ch*gw + cw*c, center = cell + gw*ch/2 + cw/2;
            int i = r * w + c, clue = state->shared->clues[i];

            if (clue != EMPTY) board[center] = '0' + clue;

            board[cell] = '+';

            if (state->borders[i] & BORDER_U)
                setmem(board + cell + 1, '-', cw - 1);
            else if (state->borders[i] & DISABLED(BORDER_U))
                board[cell + cw / 2] = 'x';

            if (state->borders[i] & BORDER_L)
                board[cell + gw] = '|';
            else if (state->borders[i] & DISABLED(BORDER_L))
                board[cell + gw] = 'x';
        }

        for (c = 0; c < ch; ++c) {
            board[(r*ch + c)*gw + gw - 2] = c ? '|' : '+';
            board[(r*ch + c)*gw + gw - 1] = '\n';
        }
    }

    scopy(board + len - gw, board, gw);
    board[len] = '\0';

    return board;
}

struct game_ui {
    int x, y;
    bool show;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->x = ui->y = 0;
    ui->show = getenv_bool("PUZZLES_SHOW_CURSOR", false);
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
}

typedef unsigned short dsflags;

struct game_drawstate {
    int tilesize;
    dsflags *grid;
};

#define TILESIZE (ds->tilesize)
#define MARGIN (ds->tilesize / 2)
#define WIDTH (3*TILESIZE/32 > 1 ? 3*TILESIZE/32 : 1)
#define CENTER ((ds->tilesize / 2) + WIDTH/2)

#define FROMCOORD(x) (((x) - MARGIN) / TILESIZE)

enum {MAYBE_LEFT, MAYBE_RIGHT, ON_LEFT, ON_RIGHT, OFF_LEFT, OFF_RIGHT};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds, int x, int y, int button)
{
    int w = state->shared->params.w, h = state->shared->params.h;
    bool control = button & MOD_CTRL, shift = button & MOD_SHFT;

    button &= ~MOD_MASK;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        int gx = FROMCOORD(x), gy = FROMCOORD(y), possible = BORDER_MASK;
        int px = (x - MARGIN) % TILESIZE, py = (y - MARGIN) % TILESIZE;
        int hx, hy, dir, i;

        if (OUT_OF_BOUNDS(gx, gy, w, h)) return NULL;

        ui->x = gx;
        ui->y = gy;

        /* find edge closest to click point */
        possible &=~ (2*px < TILESIZE ? BORDER_R : BORDER_L);
        possible &=~ (2*py < TILESIZE ? BORDER_D : BORDER_U);
        px = min(px, TILESIZE - px);
        py = min(py, TILESIZE - py);
        possible &=~ (px < py ? (BORDER_U|BORDER_D) : (BORDER_L|BORDER_R));

        for (dir = 0; dir < 4 && BORDER(dir) != possible; ++dir);
        if (dir == 4) return NULL; /* there's not exactly one such edge */

        hx = gx + dx[dir];
        hy = gy + dy[dir];

        if (OUT_OF_BOUNDS(hx, hy, w, h)) return NULL;

        ui->show = false;

        i = gy * w + gx;
        switch ((button == RIGHT_BUTTON) |
                ((state->borders[i] & BORDER(dir)) >> dir << 1) |
                ((state->borders[i] & DISABLED(BORDER(dir))) >> dir >> 2)) {

        case MAYBE_LEFT:
        case ON_LEFT:
        case ON_RIGHT:
            return string(80, "F%d,%d,%dF%d,%d,%d",
                          gx, gy, BORDER(dir),
                          hx, hy, BORDER(FLIP(dir)));

        case MAYBE_RIGHT:
        case OFF_LEFT:
        case OFF_RIGHT:
            return string(80, "F%d,%d,%dF%d,%d,%d",
                          gx, gy, DISABLED(BORDER(dir)),
                          hx, hy, DISABLED(BORDER(FLIP(dir))));
        }
    }

    if (IS_CURSOR_MOVE(button)) {
        ui->show = true;
        if (control || shift) {
            borderflag flag = 0, newflag;
            int dir, i =  ui->y * w + ui->x;
            x = ui->x;
            y = ui->y;
            move_cursor(button, &x, &y, w, h, false);
            if (OUT_OF_BOUNDS(x, y, w, h)) return NULL;

            for (dir = 0; dir < 4; ++dir)
                if (dx[dir] == x - ui->x && dy[dir] == y - ui->y) break;
            if (dir == 4) return NULL; /* how the ... ?! */

            if (control) flag |= BORDER(dir);
            if (shift) flag |= DISABLED(BORDER(dir));

            newflag = state->borders[i] ^ flag;
            if (newflag & BORDER(dir) && newflag & DISABLED(BORDER(dir)))
                return NULL;

            newflag = 0;
            if (control) newflag |= BORDER(FLIP(dir));
            if (shift) newflag |= DISABLED(BORDER(FLIP(dir)));
            return string(80, "F%d,%d,%dF%d,%d,%d",
                          ui->x, ui->y, flag, x, y, newflag);
        } else {
            move_cursor(button, &ui->x, &ui->y, w, h, false);
            return UI_UPDATE;
        }
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int w = state->shared->params.w, h = state->shared->params.h, wh = w * h;
    game_state *ret = dup_game(state);
    int nchars, x, y, flag, i;

    if (*move == 'S') {
        ++move;
        for (i = 0; i < wh && move[i]; ++i)
            ret->borders[i] =
                (move[i] & BORDER_MASK) | DISABLED(~move[i] & BORDER_MASK);
        if (i < wh || move[i]) goto badmove;
        ret->cheated = ret->completed = true;
        return ret;
    }

    while (sscanf(move, "F%d,%d,%d%n", &x, &y, &flag, &nchars) == 3 &&
           !OUT_OF_BOUNDS(x, y, w, h)) {
        move += nchars;
        for (i = 0; i < 4; i++)
            if ((flag & BORDER(i)) &&
                OUT_OF_BOUNDS(x+dx[i], y+dy[i], w, h))
                /* No toggling the borders of the grid! */
                goto badmove;
        ret->borders[y*w + x] ^= flag;
    }

    if (*move) goto badmove;

    if (!ret->completed)
        ret->completed = is_solved(&ret->shared->params, ret->shared->clues,
                                   ret->borders);

    return ret;

  badmove:
    free_game(ret);
    return NULL;
}

/* --- Drawing routines --------------------------------------------- */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = (params->w + 1) * tilesize;
    *y = (params->h + 1) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

enum {
    COL_BACKGROUND,
    COL_FLASH,
    COL_GRID,
    COL_CLUE = COL_GRID,
    COL_LINE_YES = COL_GRID,
    COL_LINE_MAYBE,
    COL_LINE_NO,
    COL_ERROR,

    NCOLOURS
};

#define COLOUR(i, r, g, b) \
   ((ret[3*(i)+0] = (r)), (ret[3*(i)+1] = (g)), (ret[3*(i)+2] = (b)))
#define DARKER 0.9F

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_BACKGROUND, -1, COL_FLASH);

    COLOUR(COL_GRID,   0.0F, 0.0F, 0.0F); /* black */
    COLOUR(COL_ERROR,  1.0F, 0.0F, 0.0F); /* red */

    COLOUR(COL_LINE_MAYBE, /* yellow */
           ret[COL_BACKGROUND*3 + 0] * DARKER,
           ret[COL_BACKGROUND*3 + 1] * DARKER,
           0.0F);

    COLOUR(COL_LINE_NO,
           ret[COL_BACKGROUND*3 + 0] * DARKER,
           ret[COL_BACKGROUND*3 + 1] * DARKER,
           ret[COL_BACKGROUND*3 + 2] * DARKER);

    *ncolours = NCOLOURS;
    return ret;
}
#undef COLOUR

#define BORDER_ERROR(x) ((x) << 8)
#define F_ERROR_U BORDER_ERROR(BORDER_U) /* BIT( 8) */
#define F_ERROR_R BORDER_ERROR(BORDER_R) /* BIT( 9) */
#define F_ERROR_D BORDER_ERROR(BORDER_D) /* BIT(10) */
#define F_ERROR_L BORDER_ERROR(BORDER_L) /* BIT(11) */
#define F_ERROR_CLUE BIT(12)
#define F_FLASH BIT(13)
#define F_CURSOR BIT(14)

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->grid = NULL;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

#define COLOUR(border)                                                  \
    (flags & BORDER_ERROR((border)) ? COL_ERROR :                       \
     flags & (border)               ? COL_LINE_YES :                    \
     flags & DISABLED((border))     ? COL_LINE_NO :                     \
                                      COL_LINE_MAYBE)

static void draw_tile(drawing *dr, game_drawstate *ds, int r, int c,
                      dsflags flags, int clue)
{
    int x = MARGIN + TILESIZE * c, y = MARGIN + TILESIZE * r;

    clip(dr, x, y, TILESIZE + WIDTH, TILESIZE + WIDTH); /* { */

    draw_rect(dr, x + WIDTH, y + WIDTH, TILESIZE - WIDTH, TILESIZE - WIDTH,
              (flags & F_FLASH ? COL_FLASH : COL_BACKGROUND));

    if (flags & F_CURSOR)
        draw_rect_corners(dr, x + CENTER, y + CENTER, TILESIZE / 3, COL_GRID);

    if (clue != EMPTY) {
        char buf[2];
        buf[0] = '0' + clue;
        buf[1] = '\0';
        draw_text(dr, x + CENTER, y + CENTER, FONT_VARIABLE,
                  TILESIZE / 2, ALIGN_VCENTRE | ALIGN_HCENTRE,
                  (flags & F_ERROR_CLUE ? COL_ERROR : COL_CLUE), buf);
    }


#define ts TILESIZE
#define w WIDTH
    draw_rect(dr, x + w,  y,      ts - w, w,      COLOUR(BORDER_U));
    draw_rect(dr, x + ts, y + w,  w,      ts - w, COLOUR(BORDER_R));
    draw_rect(dr, x + w,  y + ts, ts - w, w,      COLOUR(BORDER_D));
    draw_rect(dr, x,      y + w,  w,      ts - w, COLOUR(BORDER_L));
#undef ts
#undef w

    unclip(dr); /* } */
    draw_update(dr, x, y, TILESIZE + WIDTH, TILESIZE + WIDTH);
}

#define FLASH_TIME 0.7F

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->shared->params.w, h = state->shared->params.h, wh = w*h;
    int r, c, flash = ((int) (flashtime * 5 / FLASH_TIME)) % 2;
    int *black_border_dsf = snew_dsf(wh), *yellow_border_dsf = snew_dsf(wh);
    int k = state->shared->params.k;

    if (!ds->grid) {
        char buf[40];
        int bgw = (w+1) * ds->tilesize, bgh = (h+1) * ds->tilesize;

        for (r = 0; r <= h; ++r)
            for (c = 0; c <= w; ++c)
                draw_rect(dr, MARGIN + TILESIZE * c, MARGIN + TILESIZE * r,
                          WIDTH, WIDTH, COL_GRID);
        draw_update(dr, 0, 0, bgw, bgh);

        snewa(ds->grid, wh);
        setmem(ds->grid, ~0, wh);

        sprintf(buf, "Region size: %d", state->shared->params.k);
        status_bar(dr, buf);
    }

    build_dsf(w, h, state->borders, black_border_dsf, true);
    build_dsf(w, h, state->borders, yellow_border_dsf, false);

    for (r = 0; r < h; ++r)
        for (c = 0; c < w; ++c) {
            int i = r * w + c, clue = state->shared->clues[i], flags, dir;
            int on = bitcount[state->borders[i] & BORDER_MASK];
            int off = bitcount[(state->borders[i] >> 4) & BORDER_MASK];

            flags = state->borders[i];

            if (flash) flags |= F_FLASH;

            if (clue != EMPTY && (on > clue || clue > 4 - off))
                flags |= F_ERROR_CLUE;

            if (ui->show && ui->x == c && ui->y == r)
                flags |= F_CURSOR;

            /* border errors */
            for (dir = 0; dir < 4; ++dir) {
                int rr = r + dy[dir], cc = c + dx[dir], ii = rr * w + cc;

                if (OUT_OF_BOUNDS(cc, rr, w, h)) continue;

                /* we draw each border twice, except the outermost
                 * big border, so we have to check for errors on
                 * both sides of each border.*/
                if (/* region too large */
                    ((dsf_size(yellow_border_dsf, i) > k ||
                      dsf_size(yellow_border_dsf, ii) > k) &&
                     (dsf_canonify(yellow_border_dsf, i) !=
                      dsf_canonify(yellow_border_dsf, ii)))

                    ||
                    /* region too small */
                    ((dsf_size(black_border_dsf, i) < k ||
                      dsf_size(black_border_dsf, ii) < k) &&
                     dsf_canonify(black_border_dsf, i) !=
                     dsf_canonify(black_border_dsf, ii))

                    ||
                    /* dangling borders within a single region */
                    ((state->borders[i] & BORDER(dir)) &&
                     /* we know it's a single region because there's a
                      * path crossing no border from i to ii... */
                     (dsf_canonify(yellow_border_dsf, i) ==
                      dsf_canonify(yellow_border_dsf, ii) ||
                      /* or because any such border would be an error */
                      (dsf_size(black_border_dsf, i) <= k &&
                       dsf_canonify(black_border_dsf, i) ==
                       dsf_canonify(black_border_dsf, ii)))))

                    flags |= BORDER_ERROR(BORDER(dir));
            }

            if (flags == ds->grid[i]) continue;
            ds->grid[i] = flags;
            draw_tile(dr, ds, r, c, ds->grid[i], clue);
        }

    sfree(black_border_dsf);
    sfree(yellow_border_dsf);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate,
                               int dir, game_ui *ui)
{
    if (newstate->completed && !newstate->cheated && !oldstate->completed)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->show) {
        *x = MARGIN + TILESIZE * ui->x;
        *y = MARGIN + TILESIZE * ui->y;
        *w = *h = TILESIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    game_compute_size(params, 700, &pw, &ph); /* 7mm, like loopy */

    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void print_line(drawing *dr, int x1, int y1, int x2, int y2,
                       int colour, bool full)
{
    if (!full) {
        int i, subdivisions = 8;
        for (i = 1; i < subdivisions; ++i) {
            int x = (x1 * (subdivisions - i) + x2 * i) / subdivisions;
            int y = (y1 * (subdivisions - i) + y2 * i) / subdivisions;
            draw_circle(dr, x, y, 3, colour, colour);
        }
    } else draw_line(dr, x1, y1, x2, y2, colour);
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->shared->params.w, h = state->shared->params.h;
    int ink = print_mono_colour(dr, 0);
    game_drawstate for_tilesize_macros, *ds = &for_tilesize_macros;
    int r, c;

    ds->tilesize = tilesize;

    for (r = 0; r < h; ++r)
        for (c = 0; c < w; ++c) {
            int x = MARGIN + TILESIZE * c, y = MARGIN + TILESIZE * r;
            int i = r * w + c, clue = state->shared->clues[i];

            if (clue != EMPTY) {
                char buf[2];
                buf[0] = '0' + clue;
                buf[1] = '\0';
                draw_text(dr, x + CENTER, y + CENTER, FONT_VARIABLE,
                          TILESIZE / 2, ALIGN_VCENTRE | ALIGN_HCENTRE,
                          ink, buf);
            }

#define ts TILESIZE
#define FULL(DIR) (state->borders[i] & (BORDER_ ## DIR))
            print_line(dr, x,      y,      x + ts, y,      ink, FULL(U));
            print_line(dr, x + ts, y,      x + ts, y + ts, ink, FULL(R));
            print_line(dr, x,      y + ts, x + ts, y + ts, ink, FULL(D));
            print_line(dr, x,      y,      x,      y + ts, ink, FULL(L));
#undef ts
#undef FULL
        }

    for (r = 1; r < h; ++r)
        for (c = 1; c < w; ++c) {
            int j = r * w + c, i = j - 1 - w;
            int x = MARGIN + TILESIZE * c, y = MARGIN + TILESIZE * r;
            if (state->borders[i] & (BORDER_D|BORDER_R)) continue;
            if (state->borders[j] & (BORDER_U|BORDER_L)) continue;
            draw_circle(dr, x, y, 3, ink, ink);
        }
}

#ifdef COMBINED
#define thegame palisade
#endif

const struct game thegame = {
    "Palisade", "games.palisade", "palisade",
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
    true, solve_game,
    true, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    48, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    true, false, game_print_size, game_print,
    true,                                     /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,                                         /* flags */
};
