/*
 * galaxies.c: implementation of 'Tentai Show' from Nikoli,
 *             also sometimes called 'Spiral Galaxies'.
 *
 * Notes:
 *
 * Grid is stored as size (2n-1), holding edges as well as spaces
 * (and thus vertices too, at edge intersections).
 *
 * Any dot will thus be positioned at one of our grid points,
 * which saves any faffing with half-of-a-square stuff.
 *
 * Edges have on/off state; obviously the actual edges of the
 * board are fixed to on, and everything else starts as off.
 *
 * Future solver directions:
 *
 *  - Non-local version of the exclave extension? Suppose you have an
 *    exclave with multiple potential paths back home, but all of them
 *    go through the same tile somewhere in the middle of the path.
 *    Then _that_ critical square can be assigned to the home dot,
 *    even if we don't yet know the details of the path from it to
 *    either existing region.
 *
 *  - Permit non-simply-connected puzzle instances in sub-Unreasonable
 *    mode? Even the simplest case 5x3:ubb is graded Unreasonable at
 *    present, because we have no solution technique short of
 *    recursion that can handle it.
 *
 *    The reasoning a human uses for that puzzle is to observe that
 *    the centre left square has to connect to the centre dot, so it
 *    must have _some_ path back there. It could go round either side
 *    of the dot in the way. But _whichever_ way it goes, that rules
 *    out the left dot extending to the squares above and below it,
 *    because if it did that, that would block _both_ routes back to
 *    the centre.
 *
 *    But the exclave-extending deduction we have at present is only
 *    capable of extending an exclave with _one_ liberty. This has
 *    two, so the only technique we have available is to try them one
 *    by one via recursion.
 *
 *    My vague plan to fix this would be to re-run the exclave
 *    extension on a per-dot basis (probably after working out a
 *    non-local version as described above): instead of trying to find
 *    all exclaves at once, try it for one exclave at a time, or
 *    perhaps all exclaves relating to a particular home dot H. The
 *    point of this is that then you could spot pairs of squares with
 *    _two_ possible dots, one of which is H, and which are opposite
 *    to each other with respect to their other dot D (such as the
 *    squares above/below the left dot in this example). And then you
 *    merge those into one vertex of the connectivity graph, on the
 *    grounds that they're either both H or both D - and _then_ you
 *    have an exclave with only one path back home, and can make
 *    progress.
 *
 * Bugs:
 *
 * Notable puzzle IDs:
 *
 * Nikoli's example [web site has wrong highlighting]
 * (at http://www.nikoli.co.jp/en/puzzles/astronomical_show/):
 *  5x5:eBbbMlaBbOEnf
 *
 * The 'spiral galaxies puzzles are NP-complete' paper
 * (at http://www.stetson.edu/~efriedma/papers/spiral.pdf):
 *  7x7:chpgdqqqoezdddki
 *
 * Puzzle competition pdf examples
 * (at http://www.puzzleratings.org/Yurekli2006puz.pdf):
 *  6x6:EDbaMucCohbrecEi
 *  10x10:beFbufEEzowDlxldibMHezBQzCdcFzjlci
 *  13x13:dCemIHFFkJajjgDfdbdBzdzEgjccoPOcztHjBczLDjczqktJjmpreivvNcggFi
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

#ifdef DEBUGGING
#define solvep debug
#elif defined STANDALONE_SOLVER
static bool solver_show_working;
#define solvep(x) do { if (solver_show_working) { printf x; } } while(0)
#else
#define solvep(x) ((void)0)
#endif

#ifdef STANDALONE_PICTURE_GENERATOR
/*
 * Dirty hack to enable the generator to construct a game ID which
 * solves to a specified black-and-white bitmap. We define a global
 * variable here which gives the desired colour of each square, and
 * we arrange that the grid generator never merges squares of
 * different colours.
 *
 * The bitmap as stored here is a simple int array (at these sizes
 * it isn't worth doing fiddly bit-packing). picture[y*w+x] is 1
 * iff the pixel at (x,y) is intended to be black.
 *
 * (It might be nice to be able to specify some pixels as
 * don't-care, to give the generator more leeway. But that might be
 * fiddly.)
 */
static int *picture;
#endif

enum {
    COL_BACKGROUND,
    COL_WHITEBG,
    COL_BLACKBG,
    COL_WHITEDOT,
    COL_BLACKDOT,
    COL_GRID,
    COL_EDGE,
    COL_ARROW,
    COL_CURSOR,
    NCOLOURS
};

#define DIFFLIST(A)             \
    A(NORMAL,Normal,n)          \
    A(UNREASONABLE,Unreasonable,u)

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM)
    DIFF_IMPOSSIBLE, DIFF_AMBIGUOUS, DIFF_UNFINISHED, DIFF_MAX };
static char const *const galaxies_diffnames[] = {
    DIFFLIST(TITLE) "Impossible", "Ambiguous", "Unfinished" };
static char const galaxies_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    /* X and Y is the area of the board as seen by
     * the user, not the (2n+1) area the game uses. */
    int w, h, diff;
};

enum { s_tile, s_edge, s_vertex };

#define F_DOT           1       /* there's a dot here */
#define F_EDGE_SET      2       /* the edge is set */
#define F_TILE_ASSOC    4       /* this tile is associated with a dot. */
#define F_DOT_BLACK     8       /* (ui only) dot is black. */
#define F_MARK          16      /* scratch flag */
#define F_REACHABLE     32
#define F_SCRATCH       64
#define F_MULTIPLE      128
#define F_DOT_HOLD      256
#define F_GOOD          512

typedef struct space {
    int x, y;           /* its position */
    int type;
    unsigned int flags;
    int dotx, doty;     /* if flags & F_TILE_ASSOC */
    int nassoc;         /* if flags & F_DOT */
} space;

#define INGRID(s,x,y) ((x) >= 0 && (y) >= 0 &&                  \
                       (x) < (state)->sx && (y) < (state)->sy)
#define INUI(s,x,y)   ((x) > 0 && (y) > 0 &&                  \
                       (x) < ((state)->sx-1) && (y) < ((state)->sy-1))

#define GRID(s,g,x,y) ((s)->g[((y)*(s)->sx)+(x)])
#define SPACE(s,x,y) GRID(s,grid,x,y)

struct game_state {
    int w, h;           /* size from params */
    int sx, sy;         /* allocated size, (2x-1)*(2y-1) */
    space *grid;
    bool completed, used_solve;
    int ndots;
    space **dots;

    midend *me;         /* to call supersede_game_desc */
    int cdiff;          /* difficulty of current puzzle (for status bar),
                           or -1 if stale. */
};

static bool check_complete(const game_state *state, int *dsf, int *colours);
static int solver_state_inner(game_state *state, int maxdiff, int depth);
static int solver_state(game_state *state, int maxdiff);
static int solver_obvious(game_state *state);
static int solver_obvious_dot(game_state *state, space *dot);
static space *space_opposite_dot(const game_state *state, const space *sp,
                                 const space *dot);
static space *tile_opposite(const game_state *state, const space *sp);
static game_state *execute_move(const game_state *state, const char *move);

/* ----------------------------------------------------------
 * Game parameters and presets
 */

/* make up some sensible default sizes */

#define DEFAULT_PRESET 0

static const game_params galaxies_presets[] = {
    {  7,  7, DIFF_NORMAL },
    {  7,  7, DIFF_UNREASONABLE },
    { 10, 10, DIFF_NORMAL },
    { 10, 10, DIFF_UNREASONABLE },
    { 15, 15, DIFF_NORMAL },
    { 15, 15, DIFF_UNREASONABLE },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(galaxies_presets))
        return false;

    ret = snew(game_params);
    *ret = galaxies_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s", ret->w, ret->h,
            galaxies_diffnames[ret->diff]);

    if (name) *name = dupstr(buf);
    *params = ret;
    return true;
}

static game_params *default_params(void)
{
    game_params *ret;
    game_fetch_preset(DEFAULT_PRESET, NULL, &ret);
    return ret;
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
    params->h = params->w = atoi(string);
    params->diff = DIFF_NORMAL;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
        int i;
        string++;
        for (i = 0; i <= DIFF_UNREASONABLE; i++)
            if (*string == galaxies_diffchars[i])
                params->diff = i;
        if (*string) string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char str[80];
    sprintf(str, "%dx%d", params->w, params->h);
    if (full)
        sprintf(str + strlen(str), "d%c", galaxies_diffchars[params->diff]);
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = DIFFCONFIG;
    ret[2].u.choices.selected = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->diff = cfg[2].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 3 || params->h < 3)
        return "Width and height must both be at least 3";
    if (params->w > INT_MAX / 2 || params->h > INT_MAX / 2 ||
        params->w > (INT_MAX - params->w*2 - params->h*2 - 1) / 4 / params->h)
        return "Width times height must not be unreasonably large";

    /*
     * This shouldn't be able to happen at all, since decode_params
     * and custom_params will never generate anything that isn't
     * within range.
     */
    assert(params->diff <= DIFF_UNREASONABLE);

    return NULL;
}

/* ----------------------------------------------------------
 * Game utility functions.
 */

static void add_dot(space *space) {
    assert(!(space->flags & F_DOT));
    space->flags |= F_DOT;
    space->nassoc = 0;
}

static void remove_dot(space *space) {
    assert(space->flags & F_DOT);
    space->flags &= ~F_DOT;
}

static void remove_assoc(const game_state *state, space *tile) {
    if (tile->flags & F_TILE_ASSOC) {
        SPACE(state, tile->dotx, tile->doty).nassoc--;
        tile->flags &= ~F_TILE_ASSOC;
        tile->dotx = -1;
        tile->doty = -1;
    }
}

static void remove_assoc_with_opposite(game_state *state, space *tile) {
    space *opposite;

    if (!(tile->flags & F_TILE_ASSOC)) {
        return;
    }

    opposite = tile_opposite(state, tile);
    remove_assoc(state, tile);

    if (opposite != NULL && opposite != tile) {
        remove_assoc(state, opposite);
    }
}

static void add_assoc(const game_state *state, space *tile, space *dot) {
    remove_assoc(state, tile);

#ifdef STANDALONE_PICTURE_GENERATOR
    if (picture)
	assert(!picture[(tile->y/2) * state->w + (tile->x/2)] ==
	       !(dot->flags & F_DOT_BLACK));
#endif
    tile->flags |= F_TILE_ASSOC;
    tile->dotx = dot->x;
    tile->doty = dot->y;
    dot->nassoc++;
    /*debug(("add_assoc sp %d %d --> dot %d,%d, new nassoc %d.\n",
           tile->x, tile->y, dot->x, dot->y, dot->nassoc));*/
}

static bool ok_to_add_assoc_with_opposite_internal(
    const game_state *state, space *tile, space *opposite)
{
    int *colors;
    bool toret;

    if (tile->type != s_tile)
        return false;
    if (tile->flags & F_DOT)
        return false;
    if (opposite == NULL)
        return false;
    if (opposite->flags & F_DOT)
        return false;

    toret = true;
    colors = snewn(state->w * state->h, int);
    check_complete(state, NULL, colors);

    if (colors[(tile->y - 1)/2 * state->w + (tile->x - 1)/2])
        toret = false;
    if (colors[(opposite->y - 1)/2 * state->w + (opposite->x - 1)/2])
        toret = false;

    sfree(colors);
    return toret;
}

#ifndef EDITOR
static bool ok_to_add_assoc_with_opposite(
    const game_state *state, space *tile, space *dot)
{
    space *opposite = space_opposite_dot(state, tile, dot);
    return ok_to_add_assoc_with_opposite_internal(state, tile, opposite);
}
#endif

static void add_assoc_with_opposite(game_state *state, space *tile, space *dot) {
    space *opposite = space_opposite_dot(state, tile, dot);

    if(opposite && ok_to_add_assoc_with_opposite_internal(
           state, tile, opposite))
    {
        remove_assoc_with_opposite(state, tile);
        add_assoc(state, tile, dot);
        remove_assoc_with_opposite(state, opposite);
        add_assoc(state, opposite, dot);
    }
}

#ifndef EDITOR
static space *sp2dot(const game_state *state, int x, int y)
{
    space *sp = &SPACE(state, x, y);
    if (!(sp->flags & F_TILE_ASSOC)) return NULL;
    return &SPACE(state, sp->dotx, sp->doty);
}
#endif

#define IS_VERTICAL_EDGE(x) ((x % 2) == 0)

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *encode_game(const game_state *state);

static char *game_text_format(const game_state *state)
{
#ifdef EDITOR
    game_params par;
    char *params, *desc, *ret;
    par.w = state->w;
    par.h = state->h;
    par.diff = DIFF_MAX;               /* shouldn't be used */
    params = encode_params(&par, false);
    desc = encode_game(state);
    ret = snewn(strlen(params) + strlen(desc) + 2, char);
    sprintf(ret, "%s:%s", params, desc);
    sfree(params);
    sfree(desc);
    return ret;
#else
    int maxlen = (state->sx+1)*state->sy, x, y;
    char *ret, *p;
    space *sp;

    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->sy; y++) {
        for (x = 0; x < state->sx; x++) {
            sp = &SPACE(state, x, y);
            if (sp->flags & F_DOT)
                *p++ = 'o';
#if 0
            else if (sp->flags & (F_REACHABLE|F_MULTIPLE|F_MARK))
                *p++ = (sp->flags & F_MULTIPLE) ? 'M' :
                    (sp->flags & F_REACHABLE) ? 'R' : 'X';
#endif
            else {
                switch (sp->type) {
                case s_tile:
                    if (sp->flags & F_TILE_ASSOC) {
                        space *dot = sp2dot(state, sp->x, sp->y);
                        if (dot && dot->flags & F_DOT)
                            *p++ = (dot->flags & F_DOT_BLACK) ? 'B' : 'W';
                        else
                            *p++ = '?'; /* association with not-a-dot. */
                    } else
                        *p++ = ' ';
                    break;

                case s_vertex:
                    *p++ = '+';
                    break;

                case s_edge:
                    if (sp->flags & F_EDGE_SET)
                        *p++ = (IS_VERTICAL_EDGE(x)) ? '|' : '-';
                    else
                        *p++ = ' ';
                    break;

                default:
                    assert(!"shouldn't get here!");
                }
            }
        }
        *p++ = '\n';
    }

    assert(p - ret == maxlen);
    *p = '\0';

    return ret;
#endif
}

static void dbg_state(const game_state *state)
{
#ifdef DEBUGGING
    char *temp = game_text_format(state);
    debug(("%s\n", temp));
    sfree(temp);
#endif
}

/* Space-enumeration callbacks should all return 1 for 'progress made',
 * -1 for 'impossible', and 0 otherwise. */
typedef int (*space_cb)(game_state *state, space *sp, void *ctx);

#define IMPOSSIBLE_QUITS        1

static int foreach_sub(game_state *state, space_cb cb, unsigned int f,
                       void *ctx, int startx, int starty)
{
    int x, y, ret;
    bool progress = false, impossible = false;
    space *sp;

    for (y = starty; y < state->sy; y += 2) {
        sp = &SPACE(state, startx, y);
        for (x = startx; x < state->sx; x += 2) {
            ret = cb(state, sp, ctx);
            if (ret == -1) {
                if (f & IMPOSSIBLE_QUITS) return -1;
                impossible = true;
            } else if (ret == 1) {
                progress = true;
            }
            sp += 2;
        }
    }
    return impossible ? -1 : progress ? 1 : 0;
}

static int foreach_tile(game_state *state, space_cb cb, unsigned int f,
                        void *ctx)
{
    return foreach_sub(state, cb, f, ctx, 1, 1);
}

static int foreach_edge(game_state *state, space_cb cb, unsigned int f,
                        void *ctx)
{
    int ret1, ret2;

    ret1 = foreach_sub(state, cb, f, ctx, 0, 1);
    ret2 = foreach_sub(state, cb, f, ctx, 1, 0);

    if (ret1 == -1 || ret2 == -1) return -1;
    return (ret1 || ret2) ? 1 : 0;
}

#if 0
static int foreach_vertex(game_state *state, space_cb cb, unsigned int f,
                          void *ctx)
{
    return foreach_sub(state, cb, f, ctx, 0, 0);
}
#endif

#if 0
static int is_same_assoc(game_state *state,
                         int x1, int y1, int x2, int y2)
{
    space *s1, *s2;

    if (!INGRID(state, x1, y1) || !INGRID(state, x2, y2))
        return 0;

    s1 = &SPACE(state, x1, y1);
    s2 = &SPACE(state, x2, y2);
    assert(s1->type == s_tile && s2->type == s_tile);
    if ((s1->flags & F_TILE_ASSOC) && (s2->flags & F_TILE_ASSOC) &&
        s1->dotx == s2->dotx && s1->doty == s2->doty)
        return 1;
    return 0; /* 0 if not same or not both associated. */
}
#endif

#if 0
static int edges_into_vertex(game_state *state,
                             int x, int y)
{
    int dx, dy, nx, ny, count = 0;

    assert(SPACE(state, x, y).type == s_vertex);
    for (dx = -1; dx <= 1; dx++) {
        for (dy = -1; dy <= 1; dy++) {
            if (dx != 0 && dy != 0) continue;
            if (dx == 0 && dy == 0) continue;

            nx = x+dx; ny = y+dy;
            if (!INGRID(state, nx, ny)) continue;
            assert(SPACE(state, nx, ny).type == s_edge);
            if (SPACE(state, nx, ny).flags & F_EDGE_SET)
                count++;
        }
    }
    return count;
}
#endif

static space *space_opposite_dot(const game_state *state, const space *sp,
                                 const space *dot)
{
    int dx, dy, tx, ty;
    space *sp2;

    dx = sp->x - dot->x;
    dy = sp->y - dot->y;
    tx = dot->x - dx;
    ty = dot->y - dy;
    if (!INGRID(state, tx, ty)) return NULL;

    sp2 = &SPACE(state, tx, ty);
    assert(sp2->type == sp->type);
    return sp2;
}

static space *tile_opposite(const game_state *state, const space *sp)
{
    space *dot;

    assert(sp->flags & F_TILE_ASSOC);
    dot = &SPACE(state, sp->dotx, sp->doty);
    return space_opposite_dot(state, sp, dot);
}

static bool dotfortile(game_state *state, space *tile, space *dot)
{
    space *tile_opp = space_opposite_dot(state, tile, dot);

    if (!tile_opp) return false; /* opposite would be off grid */
    if (tile_opp->flags & F_TILE_ASSOC &&
            (tile_opp->dotx != dot->x || tile_opp->doty != dot->y))
            return false; /* opposite already associated with diff. dot */
    return true;
}

static void adjacencies(game_state *state, space *sp, space **a1s, space **a2s)
{
    int dxs[4] = {-1, 1, 0, 0}, dys[4] = {0, 0, -1, 1};
    int n, x, y;

    /* this function needs optimising. */

    for (n = 0; n < 4; n++) {
        x = sp->x+dxs[n];
        y = sp->y+dys[n];

        if (INGRID(state, x, y)) {
            a1s[n] = &SPACE(state, x, y);

            x += dxs[n]; y += dys[n];

            if (INGRID(state, x, y))
                a2s[n] = &SPACE(state, x, y);
            else
                a2s[n] = NULL;
        } else {
            a1s[n] = a2s[n] = NULL;
        }
    }
}

static bool outline_tile_fordot(game_state *state, space *tile, bool mark)
{
    space *tadj[4], *eadj[4];
    int i;
    bool didsth = false, edge, same;

    assert(tile->type == s_tile);
    adjacencies(state, tile, eadj, tadj);
    for (i = 0; i < 4; i++) {
        if (!eadj[i]) continue;

        edge = eadj[i]->flags & F_EDGE_SET;
        if (tadj[i]) {
            if (!(tile->flags & F_TILE_ASSOC))
                same = !(tadj[i]->flags & F_TILE_ASSOC);
            else
                same = ((tadj[i]->flags & F_TILE_ASSOC) &&
                    tile->dotx == tadj[i]->dotx &&
                    tile->doty == tadj[i]->doty);
        } else
            same = false;

        if (!edge && !same) {
            if (mark) eadj[i]->flags |= F_EDGE_SET;
            didsth = true;
        } else if (edge && same) {
            if (mark) eadj[i]->flags &= ~F_EDGE_SET;
            didsth = true;
        }
    }
    return didsth;
}

static void tiles_from_edge(game_state *state, space *sp, space **ts)
{
    int xs[2], ys[2];

    if (IS_VERTICAL_EDGE(sp->x)) {
        xs[0] = sp->x-1; ys[0] = sp->y;
        xs[1] = sp->x+1; ys[1] = sp->y;
    } else {
        xs[0] = sp->x; ys[0] = sp->y-1;
        xs[1] = sp->x; ys[1] = sp->y+1;
    }
    ts[0] = INGRID(state, xs[0], ys[0]) ? &SPACE(state, xs[0], ys[0]) : NULL;
    ts[1] = INGRID(state, xs[1], ys[1]) ? &SPACE(state, xs[1], ys[1]) : NULL;
}

/* Returns a move string for use by 'solve', including the initial
 * 'S' if issolve is true. */
static char *diff_game(const game_state *src, const game_state *dest,
                       bool issolve, int set_cdiff)
{
    int movelen = 0, movesize = 256, x, y, len;
    char *move = snewn(movesize, char), buf[80];
    const char *sep = "";
    char achar = issolve ? 'a' : 'A';
    space *sps, *spd;

    assert(src->sx == dest->sx && src->sy == dest->sy);

    if (issolve) {
        move[movelen++] = 'S';
        sep = ";";
    }
#ifdef EDITOR
    if (set_cdiff >= 0) {
        switch (set_cdiff) {
          case DIFF_IMPOSSIBLE:
            movelen += sprintf(move+movelen, "%sII", sep);
            break;
          case DIFF_AMBIGUOUS:
            movelen += sprintf(move+movelen, "%sIA", sep);
            break;
          case DIFF_UNFINISHED:
            movelen += sprintf(move+movelen, "%sIU", sep);
            break;
          default:
            movelen += sprintf(move+movelen, "%si%c",
                               sep, galaxies_diffchars[set_cdiff]);
            break;
        }
        sep = ";";
    }
#endif
    move[movelen] = '\0';
    for (x = 0; x < src->sx; x++) {
        for (y = 0; y < src->sy; y++) {
            sps = &SPACE(src, x, y);
            spd = &SPACE(dest, x, y);

            assert(sps->type == spd->type);

            len = 0;
            if (sps->type == s_tile) {
                if ((sps->flags & F_TILE_ASSOC) &&
                    (spd->flags & F_TILE_ASSOC)) {
                    if (sps->dotx != spd->dotx ||
                        sps->doty != spd->doty)
                    /* Both associated; change association, if different */
                        len = sprintf(buf, "%s%c%d,%d,%d,%d", sep,
                                      (int)achar, x, y, spd->dotx, spd->doty);
                } else if (sps->flags & F_TILE_ASSOC)
                    /* Only src associated; remove. */
                    len = sprintf(buf, "%sU%d,%d", sep, x, y);
                else if (spd->flags & F_TILE_ASSOC)
                    /* Only dest associated; add. */
                    len = sprintf(buf, "%s%c%d,%d,%d,%d", sep,
                                  (int)achar, x, y, spd->dotx, spd->doty);
            } else if (sps->type == s_edge) {
                if ((sps->flags & F_EDGE_SET) != (spd->flags & F_EDGE_SET))
                    /* edge flags are different; flip them. */
                    len = sprintf(buf, "%sE%d,%d", sep, x, y);
            }
            if (len) {
                if (movelen + len >= movesize) {
                    movesize = movelen + len + 256;
                    move = sresize(move, movesize, char);
                }
                strcpy(move + movelen, buf);
                movelen += len;
                sep = ";";
            }
        }
    }
    debug(("diff_game src then dest:\n"));
    dbg_state(src);
    dbg_state(dest);
    debug(("diff string %s\n", move));
    return move;
}

/* Returns true if a dot here would not be too close to any other dots
 * (and would avoid other game furniture). */
static bool dot_is_possible(const game_state *state, space *sp,
                            bool allow_assoc)
{
    int bx = 0, by = 0, dx, dy;
    space *adj;
#ifdef STANDALONE_PICTURE_GENERATOR
    int col = -1;
#endif

    switch (sp->type) {
    case s_tile:
        bx = by = 1; break;
    case s_edge:
        if (IS_VERTICAL_EDGE(sp->x)) {
            bx = 2; by = 1;
        } else {
            bx = 1; by = 2;
        }
        break;
    case s_vertex:
        bx = by = 2; break;
    }

    for (dx = -bx; dx <= bx; dx++) {
        for (dy = -by; dy <= by; dy++) {
            if (!INGRID(state, sp->x+dx, sp->y+dy)) continue;

            adj = &SPACE(state, sp->x+dx, sp->y+dy);

#ifdef STANDALONE_PICTURE_GENERATOR
            /*
             * Check that all the squares we're looking at have the
             * same colour.
             */
            if (picture) {
		if (adj->type == s_tile) {
		    int c = picture[(adj->y / 2) * state->w + (adj->x / 2)];
		    if (col < 0)
			col = c;
		    if (c != col)
			return false;          /* colour mismatch */
		}
	    }
#endif

	    if (!allow_assoc && (adj->flags & F_TILE_ASSOC))
		return false;

            if (dx != 0 || dy != 0) {
                /* Other than our own square, no dots nearby. */
                if (adj->flags & (F_DOT))
                    return false;
            }

            /* We don't want edges within our rectangle
             * (but don't care about edges on the edge) */
            if (abs(dx) < bx && abs(dy) < by &&
                adj->flags & F_EDGE_SET)
                return false;
        }
    }
    return true;
}

/* ----------------------------------------------------------
 * Game generation, structure creation, and descriptions.
 */

static game_state *blank_game(int w, int h)
{
    game_state *state = snew(game_state);
    int x, y;

    state->w = w;
    state->h = h;

    state->sx = (w*2)+1;
    state->sy = (h*2)+1;
    state->grid = snewn(state->sx * state->sy, space);
    state->completed = false;
    state->used_solve = false;

    for (x = 0; x < state->sx; x++) {
        for (y = 0; y < state->sy; y++) {
            space *sp = &SPACE(state, x, y);
            memset(sp, 0, sizeof(space));
            sp->x = x;
            sp->y = y;
            if ((x % 2) == 0 && (y % 2) == 0)
                sp->type = s_vertex;
            else if ((x % 2) == 0 || (y % 2) == 0) {
                sp->type = s_edge;
                if (x == 0 || y == 0 || x == state->sx-1 || y == state->sy-1)
                    sp->flags |= F_EDGE_SET;
            } else
                sp->type = s_tile;
        }
    }

    state->ndots = 0;
    state->dots = NULL;

    state->me = NULL; /* filled in by new_game. */
    state->cdiff = -1;

    return state;
}

static void game_update_dots(game_state *state)
{
    int i, n, sz = state->sx * state->sy;

    if (state->dots) sfree(state->dots);
    state->ndots = 0;

    for (i = 0; i < sz; i++) {
        if (state->grid[i].flags & F_DOT) state->ndots++;
    }
    state->dots = snewn(state->ndots, space *);
    n = 0;
    for (i = 0; i < sz; i++) {
        if (state->grid[i].flags & F_DOT)
            state->dots[n++] = &state->grid[i];
    }
}

static void clear_game(game_state *state, bool cleardots)
{
    int x, y;

    /* don't erase edge flags around outline! */
    for (x = 1; x < state->sx-1; x++) {
        for (y = 1; y < state->sy-1; y++) {
            if (cleardots)
                SPACE(state, x, y).flags = 0;
            else
                SPACE(state, x, y).flags &= (F_DOT|F_DOT_BLACK);
        }
    }
    if (cleardots) game_update_dots(state);
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = blank_game(state->w, state->h);

    ret->completed = state->completed;
    ret->used_solve = state->used_solve;

    memcpy(ret->grid, state->grid,
           ret->sx*ret->sy*sizeof(space));

    game_update_dots(ret);

    ret->me = state->me;
    ret->cdiff = state->cdiff;

    return ret;
}

static void free_game(game_state *state)
{
    if (state->dots) sfree(state->dots);
    sfree(state->grid);
    sfree(state);
}

/* Game description is a sequence of letters representing the number
 * of spaces (a = 0, y = 24) before the next dot; a-y for a white dot,
 * and A-Y for a black dot. 'z' is 25 spaces (and no dot).
 *
 * I know it's a bitch to generate by hand, so we provide
 * an edit mode.
 */

static char *encode_game(const game_state *state)
{
    char *desc, *p;
    int run, x, y, area;
    unsigned int f;

    area = (state->sx-2) * (state->sy-2);

    desc = snewn(area, char);
    p = desc;
    run = 0;
    for (y = 1; y < state->sy-1; y++) {
        for (x = 1; x < state->sx-1; x++) {
            f = SPACE(state, x, y).flags;

            /* a/A is 0 spaces between, b/B is 1 space, ...
             * y/Y is 24 spaces, za/zA is 25 spaces, ...
             * It's easier to count from 0 because we then
             * don't have to special-case the top left-hand corner
             * (which could be a dot with 0 spaces before it). */
            if (!(f & F_DOT))
                run++;
            else {
                while (run > 24) {
                    *p++ = 'z';
                    run -= 25;
                }
                *p++ = ((f & F_DOT_BLACK) ? 'A' : 'a') + run;
                run = 0;
            }
        }
    }
    assert(p - desc < area);
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    return desc;
}

struct movedot {
    int op;
    space *olddot, *newdot;
};

enum { MD_CHECK, MD_MOVE };

static int movedot_cb(game_state *state, space *tile, void *vctx)
{
   struct movedot *md = (struct movedot *)vctx;
   space *newopp = NULL;

   assert(tile->type == s_tile);
   assert(md->olddot && md->newdot);

   if (!(tile->flags & F_TILE_ASSOC)) return 0;
   if (tile->dotx != md->olddot->x || tile->doty != md->olddot->y)
       return 0;

   newopp = space_opposite_dot(state, tile, md->newdot);

   switch (md->op) {
   case MD_CHECK:
       /* If the tile is associated with the old dot, check its
        * opposite wrt the _new_ dot is empty or same assoc. */
       if (!newopp) return -1; /* no new opposite */
       if (newopp->flags & F_TILE_ASSOC) {
           if (newopp->dotx != md->olddot->x ||
               newopp->doty != md->olddot->y)
               return -1; /* associated, but wrong dot. */
       }
#ifdef STANDALONE_PICTURE_GENERATOR
       if (picture) {
	   /*
	    * Reject if either tile and the dot don't match in colour.
	    */
	   if (!(picture[(tile->y/2) * state->w + (tile->x/2)]) ^
	       !(md->newdot->flags & F_DOT_BLACK))
	       return -1;
	   if (!(picture[(newopp->y/2) * state->w + (newopp->x/2)]) ^
	       !(md->newdot->flags & F_DOT_BLACK))
	       return -1;
       }
#endif
       break;

   case MD_MOVE:
       /* Move dot associations: anything that was associated
        * with the old dot, and its opposite wrt the new dot,
        * become associated with the new dot. */
       assert(newopp);
       debug(("Associating %d,%d and %d,%d with new dot %d,%d.\n",
              tile->x, tile->y, newopp->x, newopp->y,
              md->newdot->x, md->newdot->y));
       add_assoc(state, tile, md->newdot);
       add_assoc(state, newopp, md->newdot);
       return 1; /* we did something! */
   }
   return 0;
}

/* For the given dot, first see if we could expand it into all the given
 * extra spaces (by checking for empty spaces on the far side), and then
 * see if we can move the dot to shift the CoG to include the new spaces.
 */
static bool dot_expand_or_move(game_state *state, space *dot,
                               space **toadd, int nadd)
{
    space *tileopp;
    int i, ret, nnew, cx, cy;
    struct movedot md;

    debug(("dot_expand_or_move: %d tiles for dot %d,%d\n",
           nadd, dot->x, dot->y));
    for (i = 0; i < nadd; i++)
        debug(("dot_expand_or_move:   dot %d,%d\n",
               toadd[i]->x, toadd[i]->y));
    assert(dot->flags & F_DOT);

#ifdef STANDALONE_PICTURE_GENERATOR
    if (picture) {
	/*
	 * Reject the expansion totally if any of the new tiles are
	 * the wrong colour.
	 */
	for (i = 0; i < nadd; i++) {
	    if (!(picture[(toadd[i]->y/2) * state->w + (toadd[i]->x/2)]) ^
		!(dot->flags & F_DOT_BLACK))
		return false;
	}
    }
#endif

    /* First off, could we just expand the current dot's tile to cover
     * the space(s) passed in and their opposites? */
    for (i = 0; i < nadd; i++) {
        tileopp = space_opposite_dot(state, toadd[i], dot);
        if (!tileopp) goto noexpand;
        if (tileopp->flags & F_TILE_ASSOC) goto noexpand;
#ifdef STANDALONE_PICTURE_GENERATOR
	if (picture) {
	    /*
	     * The opposite tiles have to be the right colour as well.
	     */
	    if (!(picture[(tileopp->y/2) * state->w + (tileopp->x/2)]) ^
		!(dot->flags & F_DOT_BLACK))
		goto noexpand;
	}
#endif
    }
    /* OK, all spaces have valid empty opposites: associate spaces and
     * opposites with our dot. */
    for (i = 0; i < nadd; i++) {
        tileopp = space_opposite_dot(state, toadd[i], dot);
        add_assoc(state, toadd[i], dot);
        add_assoc(state, tileopp, dot);
        debug(("Added associations %d,%d and %d,%d --> %d,%d\n",
               toadd[i]->x, toadd[i]->y,
               tileopp->x, tileopp->y,
               dot->x, dot->y));
        dbg_state(state);
    }
    return true;

noexpand:
    /* Otherwise, try to move dot so as to encompass given spaces: */
    /* first, calculate the 'centre of gravity' of the new dot. */
    nnew = dot->nassoc + nadd; /* number of tiles assoc. with new dot. */
    cx = dot->x * dot->nassoc;
    cy = dot->y * dot->nassoc;
    for (i = 0; i < nadd; i++) {
        cx += toadd[i]->x;
        cy += toadd[i]->y;
    }
    /* If the CoG isn't a whole number, it's not possible. */
    if ((cx % nnew) != 0 || (cy % nnew) != 0) {
        debug(("Unable to move dot %d,%d, CoG not whole number.\n",
               dot->x, dot->y));
        return false;
    }
    cx /= nnew; cy /= nnew;

    /* Check whether all spaces in the old tile would have a good
     * opposite wrt the new dot. */
    md.olddot = dot;
    md.newdot = &SPACE(state, cx, cy);
    md.op = MD_CHECK;
    ret = foreach_tile(state, movedot_cb, IMPOSSIBLE_QUITS, &md);
    if (ret == -1) {
        debug(("Unable to move dot %d,%d, new dot not symmetrical.\n",
               dot->x, dot->y));
        return false;
    }
    /* Also check whether all spaces we're adding would have a good
     * opposite wrt the new dot. */
    for (i = 0; i < nadd; i++) {
        tileopp = space_opposite_dot(state, toadd[i], md.newdot);
        if (tileopp && (tileopp->flags & F_TILE_ASSOC) &&
            (tileopp->dotx != dot->x || tileopp->doty != dot->y)) {
            tileopp = NULL;
        }
        if (!tileopp) {
            debug(("Unable to move dot %d,%d, new dot not symmetrical.\n",
               dot->x, dot->y));
            return false;
        }
#ifdef STANDALONE_PICTURE_GENERATOR
	if (picture) {
	    if (!(picture[(tileopp->y/2) * state->w + (tileopp->x/2)]) ^
		!(dot->flags & F_DOT_BLACK))
		return false;
	}
#endif
    }

    /* If we've got here, we're ok. First, associate all of 'toadd'
     * with the _old_ dot (so they'll get fixed up, with their opposites,
     * in the next step). */
    for (i = 0; i < nadd; i++) {
        debug(("Associating to-add %d,%d with old dot %d,%d.\n",
               toadd[i]->x, toadd[i]->y, dot->x, dot->y));
        add_assoc(state, toadd[i], dot);
    }

    /* Finally, move the dot and fix up all the old associations. */
    debug(("Moving dot at %d,%d to %d,%d\n",
           dot->x, dot->y, md.newdot->x, md.newdot->y));
    {
#ifdef STANDALONE_PICTURE_GENERATOR
        int f = dot->flags & F_DOT_BLACK;
#endif
        remove_dot(dot);
        add_dot(md.newdot);
#ifdef STANDALONE_PICTURE_GENERATOR
        md.newdot->flags |= f;
#endif
    }

    md.op = MD_MOVE;
    ret = foreach_tile(state, movedot_cb, 0, &md);
    assert(ret == 1);
    dbg_state(state);

    return true;
}

/* Hard-code to a max. of 2x2 squares, for speed (less malloc) */
#define MAX_TOADD 4
#define MAX_OUTSIDE 8

#define MAX_TILE_PERC 20

static bool generate_try_block(game_state *state, random_state *rs,
                               int x1, int y1, int x2, int y2)
{
    int x, y, nadd = 0, nout = 0, i, maxsz;
    space *sp, *toadd[MAX_TOADD], *outside[MAX_OUTSIDE], *dot;

    if (!INGRID(state, x1, y1) || !INGRID(state, x2, y2)) return false;

    /* We limit the maximum size of tiles to be ~2*sqrt(area); so,
     * a 5x5 grid shouldn't have anything >10 tiles, a 20x20 grid
     * nothing >40 tiles. */
    maxsz = (int)sqrt((double)(state->w * state->h)) * 2;
    debug(("generate_try_block, maxsz %d\n", maxsz));

    /* Make a static list of the spaces; if any space is already
     * associated then quit immediately. */
    for (x = x1; x <= x2; x += 2) {
        for (y = y1; y <= y2; y += 2) {
            assert(nadd < MAX_TOADD);
            sp = &SPACE(state, x, y);
            assert(sp->type == s_tile);
            if (sp->flags & F_TILE_ASSOC) return false;
            toadd[nadd++] = sp;
        }
    }

    /* Make a list of the spaces outside of our block, and shuffle it. */
#define OUTSIDE(x, y) do {                              \
    if (INGRID(state, (x), (y))) {                      \
        assert(nout < MAX_OUTSIDE);                     \
        outside[nout++] = &SPACE(state, (x), (y));      \
    }                                                   \
} while(0)
    for (x = x1; x <= x2; x += 2) {
        OUTSIDE(x, y1-2);
        OUTSIDE(x, y2+2);
    }
    for (y = y1; y <= y2; y += 2) {
        OUTSIDE(x1-2, y);
        OUTSIDE(x2+2, y);
    }
    shuffle(outside, nout, sizeof(space *), rs);

    for (i = 0; i < nout; i++) {
        if (!(outside[i]->flags & F_TILE_ASSOC)) continue;
        dot = &SPACE(state, outside[i]->dotx, outside[i]->doty);
        if (dot->nassoc >= maxsz) {
            debug(("Not adding to dot %d,%d, large enough (%d) already.\n",
                   dot->x, dot->y, dot->nassoc));
            continue;
        }
        if (dot_expand_or_move(state, dot, toadd, nadd)) return true;
    }
    return false;
}

#ifdef STANDALONE_SOLVER
static bool one_try; /* override for soak testing */
#endif

#define GP_DOTS   1

static void generate_pass(game_state *state, random_state *rs, int *scratch,
                         int perc, unsigned int flags)
{
    int sz = state->sx*state->sy, nspc, i, ret;

    /* Random list of squares to try and process, one-by-one. */
    for (i = 0; i < sz; i++) scratch[i] = i;
    shuffle(scratch, sz, sizeof(int), rs);

    /* This bug took me a, er, little while to track down. On PalmOS,
     * which has 16-bit signed ints, puzzles over about 9x9 started
     * failing to generate because the nspc calculation would start
     * to overflow, causing the dots not to be filled in properly. */
    nspc = (int)(((long)perc * (long)sz) / 100L);
    debug(("generate_pass: %d%% (%d of %dx%d) squares, flags 0x%x\n",
           perc, nspc, state->sx, state->sy, flags));

    for (i = 0; i < nspc; i++) {
        space *sp = &state->grid[scratch[i]];
        int x1 = sp->x, y1 = sp->y, x2 = sp->x, y2 = sp->y;

        if (sp->type == s_edge) {
            if (IS_VERTICAL_EDGE(sp->x)) {
                x1--; x2++;
            } else {
                y1--; y2++;
            }
        }
        if (sp->type != s_vertex) {
            /* heuristic; expanding from vertices tends to generate lots of
             * too-big regions of tiles. */
            if (generate_try_block(state, rs, x1, y1, x2, y2))
                continue; /* we expanded successfully. */
        }

        if (!(flags & GP_DOTS)) continue;

        if ((sp->type == s_edge) && (i % 2)) {
            debug(("Omitting edge %d,%d as half-of.\n", sp->x, sp->y));
            continue;
        }

        /* If we've got here we might want to put a dot down. Check
         * if we can, and add one if so. */
        if (dot_is_possible(state, sp, false)) {
            add_dot(sp);
#ifdef STANDALONE_PICTURE_GENERATOR
	    if (picture) {
		if (picture[(sp->y/2) * state->w + (sp->x/2)])
		    sp->flags |= F_DOT_BLACK;
	    }
#endif
            ret = solver_obvious_dot(state, sp);
            assert(ret != -1);
            debug(("Added dot (and obvious associations) at %d,%d\n",
                   sp->x, sp->y));
            dbg_state(state);
        }
    }
    dbg_state(state);
}

/*
 * We try several times to generate a grid at all, before even feeding
 * it to the solver. Then we pick whichever of the resulting grids was
 * the most 'wiggly', as measured by the number of inward corners in
 * the shape of any region.
 *
 * Rationale: wiggly shapes are what make this puzzle fun, and it's
 * disappointing to be served a game whose entire solution is a
 * collection of rectangles. But we also don't want to introduce a
 * _hard requirement_ of wiggliness, because a player who knew that
 * was there would be able to use it as an extra clue. This way, we
 * just probabilistically skew in favour of wiggliness.
 */
#define GENERATE_TRIES 10

static bool is_wiggle(const game_state *state, int x, int y, int dx, int dy)
{
    int x1 = x+2*dx, y1 = y+2*dy;
    int x2 = x-2*dy, y2 = y+2*dx;
    space *t, *t1, *t2;

    if (!INGRID(state, x1, y1) || !INGRID(state, x2, y2))
        return false;

    t = &SPACE(state, x, y);
    t1 = &SPACE(state, x1, y1);
    t2 = &SPACE(state, x2, y2);
    return ((t1->dotx == t2->dotx && t1->doty == t2->doty) &&
            !(t1->dotx == t->dotx && t1->doty == t->doty));
}

static int measure_wiggliness(const game_state *state, int *scratch)
{
    int sz = state->sx*state->sy;
    int x, y, nwiggles = 0;
    memset(scratch, 0, sz);

    for (y = 1; y < state->sy; y += 2) {
        for (x = 1; x < state->sx; x += 2) {
            if (y+2 < state->sy) {
                nwiggles += is_wiggle(state, x, y, 0, +1);
                nwiggles += is_wiggle(state, x, y, 0, -1);
                nwiggles += is_wiggle(state, x, y, +1, 0);
                nwiggles += is_wiggle(state, x, y, -1, 0);
            }
        }
    }

    return nwiggles;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    game_state *state = blank_game(params->w, params->h), *copy;
    char *desc;
    int *scratch, sz = state->sx*state->sy, i;
    int diff, best_wiggliness;
    bool cc;

    scratch = snewn(sz, int);

generate:
    best_wiggliness = -1;
    copy = NULL;
    for (i = 0; i < GENERATE_TRIES; i++) {
        int this_wiggliness;

        do {
            clear_game(state, true);
            generate_pass(state, rs, scratch, 100, GP_DOTS);
            game_update_dots(state);
        } while (state->ndots == 1);

        this_wiggliness = measure_wiggliness(state, scratch);
        debug(("Grid gen #%d: wiggliness=%d", i, this_wiggliness));
        if (this_wiggliness > best_wiggliness) {
            best_wiggliness = this_wiggliness;
            if (copy)
                free_game(copy);
            copy = dup_game(state);
            debug((" new best"));
        }
        debug(("\n"));
    }
    assert(copy);
    free_game(state);
    state = copy;

#ifdef DEBUGGING
    {
        char *tmp = encode_game(state);
        debug(("new_game_desc state %dx%d:%s\n", params->w, params->h, tmp));
        sfree(tmp);
    }
#endif

    for (i = 0; i < state->sx*state->sy; i++)
        if (state->grid[i].type == s_tile)
            outline_tile_fordot(state, &state->grid[i], true);
    cc = check_complete(state, NULL, NULL);
    assert(cc);

    copy = dup_game(state);
    clear_game(copy, false);
    dbg_state(copy);
    diff = solver_state(copy, params->diff);
    free_game(copy);

    assert(diff != DIFF_IMPOSSIBLE);
    if (diff != params->diff) {
        /*
         * If the puzzle was insoluble at this difficulty level (i.e.
         * too hard), _or_ soluble at a lower level (too easy), go
         * round again.
         *
         * An exception is in soak-testing mode, where we return the
         * first puzzle we got regardless.
         */
#ifdef STANDALONE_SOLVER
        if (!one_try)
#endif
            goto generate;
    }

#ifdef STANDALONE_PICTURE_GENERATOR
    /*
     * Postprocessing pass to prevent excessive numbers of adjacent
     * singletons. Iterate over all edges in random shuffled order;
     * for each edge that separates two regions, investigate
     * whether removing that edge and merging the regions would
     * still yield a valid and soluble puzzle. (The two regions
     * must also be the same colour, of course.) If so, do it.
     * 
     * This postprocessing pass is slow (due to repeated solver
     * invocations), and seems to be unnecessary during normal
     * unconstrained game generation. However, when generating a
     * game under colour constraints, excessive singletons seem to
     * turn up more often, so it's worth doing this.
     */
    {
	int *posns, nposns;
	int i, j, newdiff;
	game_state *copy2;

	nposns = params->w * (params->h+1) + params->h * (params->w+1);
	posns = snewn(nposns, int);
	for (i = j = 0; i < state->sx*state->sy; i++)
	    if (state->grid[i].type == s_edge)
		posns[j++] = i;
	assert(j == nposns);

	shuffle(posns, nposns, sizeof(*posns), rs);

	for (i = 0; i < nposns; i++) {
	    int x, y, x0, y0, x1, y1, cx, cy, cn, cx0, cy0, cx1, cy1, tx, ty;
	    space *s0, *s1, *ts, *d0, *d1, *dn;
	    bool ok;

	    /* Coordinates of edge space */
	    x = posns[i] % state->sx;
	    y = posns[i] / state->sx;

	    /* Coordinates of square spaces on either side of edge */
	    x0 = ((x+1) & ~1) - 1;     /* round down to next odd number */
	    y0 = ((y+1) & ~1) - 1;
	    x1 = 2*x-x0;	       /* and reflect about x to get x1 */
	    y1 = 2*y-y0;

	    if (!INGRID(state, x0, y0) || !INGRID(state, x1, y1))
		continue;	       /* outermost edge of grid */
	    s0 = &SPACE(state, x0, y0);
	    s1 = &SPACE(state, x1, y1);
	    assert(s0->type == s_tile && s1->type == s_tile);

	    if (s0->dotx == s1->dotx && s0->doty == s1->doty)
		continue;	       /* tiles _already_ owned by same dot */

	    d0 = &SPACE(state, s0->dotx, s0->doty);
	    d1 = &SPACE(state, s1->dotx, s1->doty);

	    if ((d0->flags ^ d1->flags) & F_DOT_BLACK)
		continue;	       /* different colours: cannot merge */

	    /*
	     * Work out where the centre of gravity of the new
	     * region would be.
	     */
	    cx = d0->nassoc * d0->x + d1->nassoc * d1->x;
	    cy = d0->nassoc * d0->y + d1->nassoc * d1->y;
	    cn = d0->nassoc + d1->nassoc;
	    if (cx % cn || cy % cn)
		continue;	       /* CoG not at integer coordinates */
	    cx /= cn;
	    cy /= cn;
	    assert(INUI(state, cx, cy));

	    /*
	     * Ensure that the CoG would actually be _in_ the new
	     * region, by verifying that all its surrounding tiles
	     * belong to one or other of our two dots.
	     */
	    cx0 = ((cx+1) & ~1) - 1;   /* round down to next odd number */
	    cy0 = ((cy+1) & ~1) - 1;
	    cx1 = 2*cx-cx0;	       /* and reflect about cx to get cx1 */
	    cy1 = 2*cy-cy0;
	    ok = true;
	    for (ty = cy0; ty <= cy1; ty += 2)
		for (tx = cx0; tx <= cx1; tx += 2) {
		    ts = &SPACE(state, tx, ty);
		    assert(ts->type == s_tile);
		    if ((ts->dotx != d0->x || ts->doty != d0->y) &&
			(ts->dotx != d1->x || ts->doty != d1->y))
			ok = false;
		}
	    if (!ok)
		continue;

	    /*
	     * Verify that for every tile in either source region,
	     * that tile's image in the new CoG is also in one of
	     * the two source regions.
	     */
	    for (ty = 1; ty < state->sy; ty += 2) {
		for (tx = 1; tx < state->sx; tx += 2) {
		    int tx1, ty1;

		    ts = &SPACE(state, tx, ty);
		    assert(ts->type == s_tile);
		    if ((ts->dotx != d0->x || ts->doty != d0->y) &&
			(ts->dotx != d1->x || ts->doty != d1->y))
			continue;      /* not part of these tiles anyway */
		    tx1 = 2*cx-tx;
		    ty1 = 2*cy-ty;
		    if (!INGRID(state, tx1, ty1)) {
			ok = false;
			break;
		    }
		    ts = &SPACE(state, cx+cx-tx, cy+cy-ty);
		    if ((ts->dotx != d0->x || ts->doty != d0->y) &&
			(ts->dotx != d1->x || ts->doty != d1->y)) {
			ok = false;
			break;
		    }
		}
		if (!ok)
		    break;
	    }
	    if (!ok)
		continue;

	    /*
	     * Now we're clear to attempt the merge. We take a copy
	     * of the game state first, so we can revert it easily
	     * if the resulting puzzle turns out to have become
	     * insoluble.
	     */
	    copy2 = dup_game(state);

	    remove_dot(d0);
	    remove_dot(d1);
	    dn = &SPACE(state, cx, cy);
	    add_dot(dn);
	    dn->flags |= (d0->flags & F_DOT_BLACK);
	    for (ty = 1; ty < state->sy; ty += 2) {
		for (tx = 1; tx < state->sx; tx += 2) {
		    ts = &SPACE(state, tx, ty);
		    assert(ts->type == s_tile);
		    if ((ts->dotx != d0->x || ts->doty != d0->y) &&
			(ts->dotx != d1->x || ts->doty != d1->y))
			continue;      /* not part of these tiles anyway */
		    add_assoc(state, ts, dn);
		}
	    }

	    copy = dup_game(state);
	    clear_game(copy, false);
	    dbg_state(copy);
	    newdiff = solver_state(copy, params->diff);
	    free_game(copy);
	    if (diff == newdiff) {
		/* Still just as soluble. Let the merge stand. */
		free_game(copy2);
	    } else {
		/* Became insoluble. Revert. */
		free_game(state);
		state = copy2;
	    }
	}
        sfree(posns);
    }
#endif

    desc = encode_game(state);
#ifndef STANDALONE_SOLVER
    debug(("new_game_desc generated: \n"));
    dbg_state(state);
#endif

    game_state *blank = blank_game(params->w, params->h);
    *aux = diff_game(blank, state, true, -1);
    free_game(blank);

    free_game(state);
    sfree(scratch);

    return desc;
}

static bool dots_too_close(game_state *state)
{
    /* Quick-and-dirty check, using half the solver:
     * solver_obvious will only fail if the dots are
     * too close together, so dot-proximity associations
     * overlap. */
    game_state *tmp = dup_game(state);
    int ret = solver_obvious(tmp);
    free_game(tmp);
    return ret == -1;
}

static game_state *load_game(const game_params *params, const char *desc,
                             const char **why_r)
{
    game_state *state = blank_game(params->w, params->h);
    const char *why = NULL;
    int i, x, y, n;
    unsigned int df;

    i = 0;
    while (*desc) {
        n = *desc++;
        if (n == 'z') {
            i += 25;
            continue;
        }
        if (n >= 'a' && n <= 'y') {
            i += n - 'a';
            df = 0;
        } else if (n >= 'A' && n <= 'Y') {
            i += n - 'A';
            df = F_DOT_BLACK;
        } else {
            why = "Invalid characters in game description"; goto fail;
        }
        /* if we got here we incremented i and have a dot to add. */
        y = (i / (state->sx-2)) + 1;
        x = (i % (state->sx-2)) + 1;
        if (!INUI(state, x, y)) {
            why = "Too much data to fit in grid"; goto fail;
        }
        add_dot(&SPACE(state, x, y));
        SPACE(state, x, y).flags |= df;
        i++;
    }
    game_update_dots(state);

    if (dots_too_close(state)) {
        why = "Dots too close together"; goto fail;
    }

    return state;

fail:
    free_game(state);
    if (why_r) *why_r = why;
    return NULL;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *why = NULL;
    game_state *dummy = load_game(params, desc, &why);
    if (dummy) {
        free_game(dummy);
        assert(!why);
    } else
        assert(why);
    return why;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = load_game(params, desc, NULL);
    if (!state) {
        assert("Unable to load ?validated game.");
        return NULL;
    }
#ifdef EDITOR
    state->me = me;
#endif
    return state;
}

/* ----------------------------------------------------------
 * Solver and all its little wizards.
 */

#if defined DEBUGGING || defined STANDALONE_SOLVER
static int solver_recurse_depth;
#define STATIC_RECURSION_DEPTH
#endif

typedef struct solver_ctx {
    game_state *state;
    int sz;             /* state->sx * state->sy */
    space **scratch;    /* size sz */
    int *dsf;           /* size sz */
    int *iscratch;      /* size sz */
} solver_ctx;

static solver_ctx *new_solver(game_state *state)
{
    solver_ctx *sctx = snew(solver_ctx);
    sctx->state = state;
    sctx->sz = state->sx*state->sy;
    sctx->scratch = snewn(sctx->sz, space *);
    sctx->dsf = snew_dsf(sctx->sz);
    sctx->iscratch = snewn(sctx->sz, int);
    return sctx;
}

static void free_solver(solver_ctx *sctx)
{
    sfree(sctx->scratch);
    sfree(sctx->dsf);
    sfree(sctx->iscratch);
    sfree(sctx);
}

    /* Solver ideas so far:
     *
     * For any empty space, work out how many dots it could associate
     * with:
       * it needs line-of-sight
       * it needs an empty space on the far side
       * any adjacent lines need corresponding line possibilities.
     */

/* The solver_ctx should keep a list of dot positions, for quicker looping.
 *
 * Solver techniques, in order of difficulty:
   * obvious adjacency to dots
   * transferring tiles to opposite side
   * transferring lines to opposite side
   * one possible dot for a given tile based on opposite availability
   * tile with 3 definite edges next to an associated tile must associate
      with same dot.
   *
   * one possible dot for a given tile based on line-of-sight
 */

static int solver_add_assoc(game_state *state, space *tile, int dx, int dy,
                            const char *why)
{
    space *dot, *tile_opp;

    dot = &SPACE(state, dx, dy);
    tile_opp = space_opposite_dot(state, tile, dot);

    assert(tile->type == s_tile);
    if (tile->flags & F_TILE_ASSOC) {
        if ((tile->dotx != dx) || (tile->doty != dy)) {
            solvep(("%*sSet %d,%d --> %d,%d (%s) impossible; "
                    "already --> %d,%d.\n",
                    solver_recurse_depth*4, "",
                    tile->x, tile->y, dx, dy, why,
                    tile->dotx, tile->doty));
            return -1;
        }
        return 0; /* no-op */
    }
    if (!tile_opp) {
        solvep(("%*s%d,%d --> %d,%d impossible, no opposite tile.\n",
                solver_recurse_depth*4, "", tile->x, tile->y, dx, dy));
        return -1;
    }
    if (tile_opp->flags & F_TILE_ASSOC &&
        (tile_opp->dotx != dx || tile_opp->doty != dy)) {
        solvep(("%*sSet %d,%d --> %d,%d (%s) impossible; "
                "opposite already --> %d,%d.\n",
                solver_recurse_depth*4, "",
                tile->x, tile->y, dx, dy, why,
                tile_opp->dotx, tile_opp->doty));
        return -1;
    }

    add_assoc(state, tile, dot);
    add_assoc(state, tile_opp, dot);
    solvep(("%*sSetting %d,%d --> %d,%d (%s).\n",
            solver_recurse_depth*4, "",
            tile->x, tile->y,dx, dy, why));
    solvep(("%*sSetting %d,%d --> %d,%d (%s, opposite).\n",
            solver_recurse_depth*4, "",
            tile_opp->x, tile_opp->y, dx, dy, why));
    return 1;
}

static int solver_obvious_dot(game_state *state, space *dot)
{
    int dx, dy, ret, didsth = 0;
    space *tile;

    debug(("%*ssolver_obvious_dot for %d,%d.\n",
           solver_recurse_depth*4, "", dot->x, dot->y));

    assert(dot->flags & F_DOT);
    for (dx = -1; dx <= 1; dx++) {
        for (dy = -1; dy <= 1; dy++) {
            if (!INGRID(state, dot->x+dx, dot->y+dy)) continue;

            tile = &SPACE(state, dot->x+dx, dot->y+dy);
            if (tile->type == s_tile) {
                ret = solver_add_assoc(state, tile, dot->x, dot->y,
                                       "next to dot");
                if (ret < 0) return -1;
                if (ret > 0) didsth = 1;
            }
        }
    }
    return didsth;
}

static int solver_obvious(game_state *state)
{
    int i, didsth = 0, ret;

    debug(("%*ssolver_obvious.\n", solver_recurse_depth*4, ""));

    for (i = 0; i < state->ndots; i++) {
        ret = solver_obvious_dot(state, state->dots[i]);
        if (ret < 0) return -1;
        if (ret > 0) didsth = 1;
    }
    return didsth;
}

static int solver_lines_opposite_cb(game_state *state, space *edge, void *ctx)
{
    int didsth = 0, n, dx, dy;
    space *tiles[2], *tile_opp, *edge_opp;

    assert(edge->type == s_edge);

    tiles_from_edge(state, edge, tiles);

    /* if tiles[0] && tiles[1] && they're both associated
     * and they're both associated with different dots,
     * ensure the line is set. */
    if (!(edge->flags & F_EDGE_SET) &&
        tiles[0] && tiles[1] &&
        (tiles[0]->flags & F_TILE_ASSOC) &&
        (tiles[1]->flags & F_TILE_ASSOC) &&
        (tiles[0]->dotx != tiles[1]->dotx ||
         tiles[0]->doty != tiles[1]->doty)) {
        /* No edge, but the two adjacent tiles are both
         * associated with different dots; add the edge. */
        solvep(("%*sSetting edge %d,%d - tiles different dots.\n",
               solver_recurse_depth*4, "", edge->x, edge->y));
        edge->flags |= F_EDGE_SET;
        didsth = 1;
    }

    if (!(edge->flags & F_EDGE_SET)) return didsth;
    for (n = 0; n < 2; n++) {
        if (!tiles[n]) continue;
        assert(tiles[n]->type == s_tile);
        if (!(tiles[n]->flags & F_TILE_ASSOC)) continue;

        tile_opp = tile_opposite(state, tiles[n]);
        if (!tile_opp) {
            solvep(("%*simpossible: edge %d,%d has assoc. tile %d,%d"
                   " with no opposite.\n",
                   solver_recurse_depth*4, "",
                   edge->x, edge->y, tiles[n]->x, tiles[n]->y));
            /* edge of tile has no opposite edge (off grid?);
             * this is impossible. */
            return -1;
        }

        dx = tiles[n]->x - edge->x;
        dy = tiles[n]->y - edge->y;
        assert(INGRID(state, tile_opp->x+dx, tile_opp->y+dy));
        edge_opp = &SPACE(state, tile_opp->x+dx, tile_opp->y+dy);
        if (!(edge_opp->flags & F_EDGE_SET)) {
            solvep(("%*sSetting edge %d,%d as opposite %d,%d\n",
                   solver_recurse_depth*4, "",
                   tile_opp->x+dx, tile_opp->y+dy, edge->x, edge->y));
            edge_opp->flags |= F_EDGE_SET;
            didsth = 1;
        }
    }
    return didsth;
}

static int solver_spaces_oneposs_cb(game_state *state, space *tile, void *ctx)
{
    int n, eset, ret;
    space *edgeadj[4], *tileadj[4];
    int dotx, doty;

    assert(tile->type == s_tile);
    if (tile->flags & F_TILE_ASSOC) return 0;

    adjacencies(state, tile, edgeadj, tileadj);

    /* Empty tile. If each edge is either set, or associated with
     * the same dot, we must also associate with dot. */
    eset = 0; dotx = -1; doty = -1;
    for (n = 0; n < 4; n++) {
        assert(edgeadj[n]);
        assert(edgeadj[n]->type == s_edge);
        if (edgeadj[n]->flags & F_EDGE_SET) {
            eset++;
        } else {
            assert(tileadj[n]);
            assert(tileadj[n]->type == s_tile);

            /* If an adjacent tile is empty we can't make any deductions.*/
            if (!(tileadj[n]->flags & F_TILE_ASSOC))
                return 0;

            /* If an adjacent tile is assoc. with a different dot
             * we can't make any deductions. */
            if (dotx != -1 && doty != -1 &&
                (tileadj[n]->dotx != dotx ||
                 tileadj[n]->doty != doty))
                return 0;

            dotx = tileadj[n]->dotx;
            doty = tileadj[n]->doty;
        }
    }
    if (eset == 4) {
        solvep(("%*simpossible: empty tile %d,%d has 4 edges\n",
               solver_recurse_depth*4, "",
               tile->x, tile->y));
        return -1;
    }
    assert(dotx != -1 && doty != -1);

    ret = solver_add_assoc(state, tile, dotx, doty, "rest are edges");
    if (ret == -1) return -1;
    assert(ret != 0); /* really should have done something. */

    return 1;
}

/* Improved algorithm for tracking line-of-sight from dots, and not spaces.
 *
 * The solver_ctx already stores a list of dots: the algorithm proceeds by
 * expanding outwards from each dot in turn, expanding first to the boundary
 * of its currently-connected tile and then to all empty tiles that could see
 * it. Empty tiles will be flagged with a 'can see dot <x,y>' sticker.
 *
 * Expansion will happen by (symmetrically opposite) pairs of squares; if
 * a square hasn't an opposite number there's no point trying to expand through
 * it. Empty tiles will therefore also be tagged in pairs.
 *
 * If an empty tile already has a 'can see dot <x,y>' tag from a previous dot,
 * it (and its partner) gets untagged (or, rather, a 'can see two dots' tag)
 * because we're looking for single-dot possibilities.
 *
 * Once we've gone through all the dots, any which still have a 'can see dot'
 * tag get associated with that dot (because it must have been the only one);
 * any without any tag (i.e. that could see _no_ dots) cause an impossibility
 * marked.
 *
 * The expansion will happen each time with a stored list of (space *) pairs,
 * rather than a mark-and-sweep idea; that's horrifically inefficient.
 *
 * expansion algorithm:
 *
 * * allocate list of (space *) the size of s->sx*s->sy.
 * * allocate second grid for (flags, dotx, doty) size of sx*sy.
 *
 * clear second grid (flags = 0, all dotx and doty = 0)
 * flags: F_REACHABLE, F_MULTIPLE
 *
 *
 * * for each dot, start with one pair of tiles that are associated with it --
 *   * vertex --> (dx+1, dy+1), (dx-1, dy-1)
 *   * edge --> (adj1, adj2)
 *   * tile --> (tile, tile) ???
 * * mark that pair of tiles with F_MARK, clear all other F_MARKs.
 * * add two tiles to start of list.
 *
 * set start = 0, end = next = 2
 *
 * from (start to end-1, step 2) {
 * * we have two tiles (t1, t2), opposites wrt our dot.
 * * for each (at1) sensible adjacent tile to t1 (i.e. not past an edge):
 *   * work out at2 as the opposite to at1
 *   * assert at1 and at2 have the same F_MARK values.
 *   * if at1 & F_MARK ignore it (we've been there on a previous sweep)
 *   * if either are associated with a different dot
 *     * mark both with F_MARK (so we ignore them later)
 *   * otherwise (assoc. with our dot, or empty):
 *     * mark both with F_MARK
 *     * add their space * values to the end of the list, set next += 2.
 * }
 *
 * if (end == next)
 * * we didn't add any new squares; exit the loop.
 * else
 * * set start = next+1, end = next. go round again
 *
 * We've finished expanding from the dot. Now, for each square we have
 * in our list (--> each square with F_MARK):
 * * if the tile is empty:
 *   * if F_REACHABLE was already set
 *     * set F_MULTIPLE
 *   * otherwise
 *     * set F_REACHABLE, set dotx and doty to our dot.
 *
 * Then, continue the whole thing for each dot in turn.
 *
 * Once we've done for each dot, go through the entire grid looking for
 * empty tiles: for each empty tile:
   * if F_REACHABLE and not F_MULTIPLE, set that dot (and its double)
   * if !F_REACHABLE, return as impossible.
 *
 */

/* Returns true if this tile is either already associated with this dot,
 * or blank. */
static bool solver_expand_checkdot(space *tile, space *dot)
{
    if (!(tile->flags & F_TILE_ASSOC)) return true;
    if (tile->dotx == dot->x && tile->doty == dot->y) return true;
    return false;
}

static void solver_expand_fromdot(game_state *state, space *dot, solver_ctx *sctx)
{
    int i, j, x, y, start, end, next;
    space *sp;

    /* Clear the grid of the (space) flags we'll use. */

    /* This is well optimised; analysis showed that:
        for (i = 0; i < sctx->sz; i++) state->grid[i].flags &= ~F_MARK;
       took up ~85% of the total function time! */
    for (y = 1; y < state->sy; y += 2) {
        sp = &SPACE(state, 1, y);
        for (x = 1; x < state->sx; x += 2, sp += 2)
            sp->flags &= ~F_MARK;
    }

    /* Seed the list of marked squares with two that must be associated
     * with our dot (possibly the same space) */
    if (dot->type == s_tile) {
        sctx->scratch[0] = sctx->scratch[1] = dot;
    } else if (dot->type == s_edge) {
        tiles_from_edge(state, dot, sctx->scratch);
    } else if (dot->type == s_vertex) {
        /* pick two of the opposite ones arbitrarily. */
        sctx->scratch[0] = &SPACE(state, dot->x-1, dot->y-1);
        sctx->scratch[1] = &SPACE(state, dot->x+1, dot->y+1);
    }
    assert(sctx->scratch[0]->flags & F_TILE_ASSOC);
    assert(sctx->scratch[1]->flags & F_TILE_ASSOC);

    sctx->scratch[0]->flags |= F_MARK;
    sctx->scratch[1]->flags |= F_MARK;

    debug(("%*sexpand from dot %d,%d seeded with %d,%d and %d,%d.\n",
           solver_recurse_depth*4, "", dot->x, dot->y,
           sctx->scratch[0]->x, sctx->scratch[0]->y,
           sctx->scratch[1]->x, sctx->scratch[1]->y));

    start = 0; end = 2; next = 2;

expand:
    debug(("%*sexpand: start %d, end %d, next %d\n",
           solver_recurse_depth*4, "", start, end, next));
    for (i = start; i < end; i += 2) {
        space *t1 = sctx->scratch[i]/*, *t2 = sctx->scratch[i+1]*/;
        space *edges[4], *tileadj[4], *tileadj2;

        adjacencies(state, t1, edges, tileadj);

        for (j = 0; j < 4; j++) {
            assert(edges[j]);
            if (edges[j]->flags & F_EDGE_SET) continue;
            assert(tileadj[j]);

            if (tileadj[j]->flags & F_MARK) continue; /* seen before. */

            /* We have a tile adjacent to t1; find its opposite. */
            tileadj2 = space_opposite_dot(state, tileadj[j], dot);
            if (!tileadj2) {
                debug(("%*sMarking %d,%d, no opposite.\n",
                       solver_recurse_depth*4, "",
                       tileadj[j]->x, tileadj[j]->y));
                tileadj[j]->flags |= F_MARK;
                continue; /* no opposite, so mark for next time. */
            }
            /* If the tile had an opposite we should have either seen both of
             * these, or neither of these, before. */
            assert(!(tileadj2->flags & F_MARK));

            if (solver_expand_checkdot(tileadj[j], dot) &&
                solver_expand_checkdot(tileadj2, dot)) {
                /* Both tiles could associate with this dot; add them to
                 * our list. */
                debug(("%*sAdding %d,%d and %d,%d to possibles list.\n",
                       solver_recurse_depth*4, "",
                       tileadj[j]->x, tileadj[j]->y, tileadj2->x, tileadj2->y));
                sctx->scratch[next++] = tileadj[j];
                sctx->scratch[next++] = tileadj2;
            }
            /* Either way, we've seen these tiles already so mark them. */
            debug(("%*sMarking %d,%d and %d,%d.\n",
                   solver_recurse_depth*4, "",
                       tileadj[j]->x, tileadj[j]->y, tileadj2->x, tileadj2->y));
            tileadj[j]->flags |= F_MARK;
            tileadj2->flags |= F_MARK;
        }
    }
    if (next > end) {
        /* We added more squares; go back and try again. */
        start = end; end = next; goto expand;
    }

    /* We've expanded as far as we can go. Now we update the main flags
     * on all tiles we've expanded into -- if they were empty, we have
     * found possible associations for this dot. */
    for (i = 0; i < end; i++) {
        if (sctx->scratch[i]->flags & F_TILE_ASSOC) continue;
        if (sctx->scratch[i]->flags & F_REACHABLE) {
            /* This is (at least) the second dot this tile could
             * associate with. */
            debug(("%*sempty tile %d,%d could assoc. other dot %d,%d\n",
                   solver_recurse_depth*4, "",
                   sctx->scratch[i]->x, sctx->scratch[i]->y, dot->x, dot->y));
            sctx->scratch[i]->flags |= F_MULTIPLE;
        } else {
            /* This is the first (possibly only) dot. */
            debug(("%*sempty tile %d,%d could assoc. 1st dot %d,%d\n",
                   solver_recurse_depth*4, "",
                   sctx->scratch[i]->x, sctx->scratch[i]->y, dot->x, dot->y));
            sctx->scratch[i]->flags |= F_REACHABLE;
            sctx->scratch[i]->dotx = dot->x;
            sctx->scratch[i]->doty = dot->y;
        }
    }
    dbg_state(state);
}

static int solver_expand_postcb(game_state *state, space *tile, void *ctx)
{
    assert(tile->type == s_tile);

    if (tile->flags & F_TILE_ASSOC) return 0;

    if (!(tile->flags & F_REACHABLE)) {
        solvep(("%*simpossible: space (%d,%d) can reach no dots.\n",
                solver_recurse_depth*4, "", tile->x, tile->y));
        return -1;
    }
    if (tile->flags & F_MULTIPLE) return 0;

    return solver_add_assoc(state, tile, tile->dotx, tile->doty,
                            "single possible dot after expansion");
}

static int solver_expand_dots(game_state *state, solver_ctx *sctx)
{
    int i;

    for (i = 0; i < sctx->sz; i++)
        state->grid[i].flags &= ~(F_REACHABLE|F_MULTIPLE);

    for (i = 0; i < state->ndots; i++)
        solver_expand_fromdot(state, state->dots[i], sctx);

    return foreach_tile(state, solver_expand_postcb, IMPOSSIBLE_QUITS, sctx);
}

static int solver_extend_exclaves(game_state *state, solver_ctx *sctx)
{
    int x, y, done_something = 0;

    /*
     * Make a dsf by unifying any two adjacent tiles associated with
     * the same dot. This will identify separate connected components
     * of the tiles belonging to a given dot. Any such component that
     * doesn't contain its own dot is an 'exclave', and will need some
     * kind of path of tiles to connect it back to the dot.
     */
    dsf_init(sctx->dsf, sctx->sz);
    for (x = 1; x < state->sx; x += 2) {
        for (y = 1; y < state->sy; y += 2) {
            int dotx, doty;
            space *tile, *othertile;

            tile = &SPACE(state, x, y);
            if (!(tile->flags & F_TILE_ASSOC))
                continue;              /* not associated with any dot */
            dotx = tile->dotx;
            doty = tile->doty;

            if (INGRID(state, x+2, y)) {
                othertile = &SPACE(state, x+2, y);
                if ((othertile->flags & F_TILE_ASSOC) &&
                    othertile->dotx == dotx && othertile->doty == doty)
                    dsf_merge(sctx->dsf, y*state->sx+x, y*state->sx+(x+2));
            }

            if (INGRID(state, x, y+2)) {
                othertile = &SPACE(state, x, y+2);
                if ((othertile->flags & F_TILE_ASSOC) &&
                    othertile->dotx == dotx && othertile->doty == doty)
                    dsf_merge(sctx->dsf, y*state->sx+x, (y+2)*state->sx+x);
            }
        }
    }

    /*
     * Go through the grid again, and count the 'liberties' of each
     * connected component, in the Go sense, i.e. the number of
     * currently unassociated squares adjacent to the component. The
     * idea is that if an exclave has just one liberty, then that
     * square _must_ extend the exclave, or else it will be completely
     * cut off from connecting back to its home dot.
     *
     * We need to count each adjacent square just once, even if it
     * borders the component on multiple edges. So we'll go through
     * each unassociated square, check all four of its neighbours, and
     * de-duplicate them.
     *
     * We'll store the count of liberties in the entry of iscratch
     * corresponding to the square centre (i.e. with odd coordinates).
     * Every time we find a liberty, we store its index in the square
     * to the left of that, so that when a component has exactly one
     * liberty we can remember what it was.
     *
     * Square centres that are not the canonical dsf element of a
     * connected component will get their liberty count set to -1,
     * which will allow us to identify them in the later loop (after
     * we start making changes and need to spot that an associated
     * square _now_ was not associated when the dsf was built).
     */

    /* Initialise iscratch */
    for (x = 1; x < state->sx; x += 2) {
        for (y = 1; y < state->sy; y += 2) {
            int index = y * state->sx + x;
            if (!(SPACE(state, x, y).flags & F_TILE_ASSOC) ||
                dsf_canonify(sctx->dsf, index) != index) {
                sctx->iscratch[index] = -1; /* mark as not a component */
            } else {
                sctx->iscratch[index] = 0; /* zero liberty count */
                sctx->iscratch[index-1] = 0; /* initialise neighbour id */
            }
        }
    }

    /* Find each unassociated square and see what it's a liberty of */
    for (x = 1; x < state->sx; x += 2) {
        for (y = 1; y < state->sy; y += 2) {
            int dx, dy, ni[4], nn, i;

            if ((SPACE(state, x, y).flags & F_TILE_ASSOC))
                continue;              /* not an unassociated square */

            /* Find distinct indices of adjacent components */
            nn = 0;
            for (dx = -1; dx <= 1; dx++) {
                for (dy = -1; dy <= 1; dy++) {
                    if (dx != 0 && dy != 0) continue;
                    if (dx == 0 && dy == 0) continue;

                    if (INGRID(state, x+2*dx, y+2*dy) &&
                        (SPACE(state, x+2*dx, y+2*dy).flags & F_TILE_ASSOC)) {
                        /* Find id of the component adjacent to x,y */
                        int nindex = (y+2*dy) * state->sx + (x+2*dx);
                        nindex = dsf_canonify(sctx->dsf, nindex);

                        /* See if we've seen it before in another direction */
                        for (i = 0; i < nn; i++)
                            if (ni[i] == nindex)
                                break;
                        if (i == nn) {
                            /* No, it's new. Mark x,y as a liberty of it */
                            sctx->iscratch[nindex]++;
                            assert(nindex > 0);
                            sctx->iscratch[nindex-1] = y * state->sx + x;

                            /* And record this component as having been seen */
                            ni[nn++] = nindex;
                        }
                    }
                }
            }
        }
    }

    /*
     * Now we have all the data we need to find exclaves with exactly
     * one liberty. In each case, associate the unique liberty square
     * with the same dot.
     */
    for (x = 1; x < state->sx; x += 2) {
        for (y = 1; y < state->sy; y += 2) {
            int index, dotx, doty, ex, ey, added;
            space *tile;

            index = y*state->sx+x;
            if (sctx->iscratch[index] == -1)
                continue;    /* wasn't canonical when dsf was built */

            tile = &SPACE(state, x, y);
            if (!(tile->flags & F_TILE_ASSOC))
                continue;              /* not associated with any dot */
            dotx = tile->dotx;
            doty = tile->doty;

            if (index == dsf_canonify(
                    sctx->dsf, (doty | 1) * state->sx + (dotx | 1)))
                continue;    /* not an exclave - contains its own dot */

            if (sctx->iscratch[index] == 0) {
                solvep(("%*sExclave at %d,%d has no liberties!\n",
                        solver_recurse_depth*4, "", x, y));
                return -1;
            }

            if (sctx->iscratch[index] != 1)
                continue; /* more than one liberty, can't be sure which */

            assert(sctx->iscratch[index-1] != 0);
            ex = sctx->iscratch[index-1] % state->sx;
            ey = sctx->iscratch[index-1] / state->sx;
            tile = &SPACE(state, ex, ey);
            if (tile->flags & F_TILE_ASSOC)
                continue; /* already done by earlier iteration of this loop */

            added = solver_add_assoc(state, tile, dotx, doty,
                                     "to connect exclave");
            if (added < 0)
                return -1;
            if (added > 0)
                done_something = 1;
        }
    }

    return done_something;
}

struct recurse_ctx {
    space *best;
    int bestn;
};

static int solver_recurse_cb(game_state *state, space *tile, void *ctx)
{
    struct recurse_ctx *rctx = (struct recurse_ctx *)ctx;
    int i, n = 0;

    assert(tile->type == s_tile);
    if (tile->flags & F_TILE_ASSOC) return 0;

    /* We're unassociated: count up all the dots we could associate with. */
    for (i = 0; i < state->ndots; i++) {
        if (dotfortile(state, tile, state->dots[i]))
            n++;
    }
    if (n > rctx->bestn) {
        rctx->bestn = n;
        rctx->best = tile;
    }
    return 0;
}

#define MAXRECURSE 5

static int solver_recurse(game_state *state, int maxdiff, int depth)
{
    int diff = DIFF_IMPOSSIBLE, ret, n, gsz = state->sx * state->sy;
    space *ingrid, *outgrid = NULL, *bestopp;
    struct recurse_ctx rctx;

    if (depth >= MAXRECURSE) {
        solvep(("Limiting recursion to %d, returning.\n", MAXRECURSE));
        return DIFF_UNFINISHED;
    }

    /* Work out the cell to recurse on; go through all unassociated tiles
     * and find which one has the most possible dots it could associate
     * with. */
    rctx.best = NULL;
    rctx.bestn = 0;

    foreach_tile(state, solver_recurse_cb, 0, &rctx);
    if (rctx.bestn == 0) return DIFF_IMPOSSIBLE; /* or assert? */
    assert(rctx.best);

    solvep(("%*sRecursing around %d,%d, with %d possible dots.\n",
           solver_recurse_depth*4, "",
           rctx.best->x, rctx.best->y, rctx.bestn));

    ingrid = snewn(gsz, space);
    memcpy(ingrid, state->grid, gsz * sizeof(space));

    for (n = 0; n < state->ndots; n++) {
        memcpy(state->grid, ingrid, gsz * sizeof(space));

        if (!dotfortile(state, rctx.best, state->dots[n])) continue;

        /* set cell (temporarily) pointing to that dot. */
        solver_add_assoc(state, rctx.best,
                         state->dots[n]->x, state->dots[n]->y,
                         "Attempting for recursion");

        ret = solver_state_inner(state, maxdiff, depth + 1);

#ifdef STATIC_RECURSION_DEPTH
        solver_recurse_depth = depth;  /* restore after recursion returns */
#endif

        if (diff == DIFF_IMPOSSIBLE && ret != DIFF_IMPOSSIBLE) {
            /* we found our first solved grid; copy it away. */
            assert(!outgrid);
            outgrid = snewn(gsz, space);
            memcpy(outgrid, state->grid, gsz * sizeof(space));
        }
        /* reset cell back to unassociated. */
        bestopp = tile_opposite(state, rctx.best);
        assert(bestopp && bestopp->flags & F_TILE_ASSOC);

        remove_assoc(state, rctx.best);
        remove_assoc(state, bestopp);

        if (ret == DIFF_AMBIGUOUS || ret == DIFF_UNFINISHED)
            diff = ret;
        else if (ret == DIFF_IMPOSSIBLE)
            /* no change */;
        else {
            /* precisely one solution */
            if (diff == DIFF_IMPOSSIBLE)
                diff = DIFF_UNREASONABLE;
            else
                diff = DIFF_AMBIGUOUS;
        }
        /* if we've found >1 solution, or ran out of recursion,
         * give up immediately. */
        if (diff == DIFF_AMBIGUOUS || diff == DIFF_UNFINISHED)
            break;
    }

    if (outgrid) {
        /* we found (at least one) soln; copy it back to state */
        memcpy(state->grid, outgrid, gsz * sizeof(space));
        sfree(outgrid);
    }
    sfree(ingrid);
    return diff;
}

static int solver_state_inner(game_state *state, int maxdiff, int depth)
{
    solver_ctx *sctx = new_solver(state);
    int ret, diff = DIFF_NORMAL;

#ifdef STANDALONE_PICTURE_GENERATOR
    /* hack, hack: set picture to NULL during solving so that add_assoc
     * won't complain when we attempt recursive guessing and guess wrong */
    int *savepic = picture;
    picture = NULL;
#endif

#ifdef STATIC_RECURSION_DEPTH
    solver_recurse_depth = depth;
#endif

    ret = solver_obvious(state);
    if (ret < 0) {
        diff = DIFF_IMPOSSIBLE;
        goto got_result;
    }

#define CHECKRET(d) do {                                        \
    if (ret < 0) { diff = DIFF_IMPOSSIBLE; goto got_result; }   \
    if (ret > 0) { diff = max(diff, (d)); goto cont; }          \
} while(0)

    while (1) {
cont:
        ret = foreach_edge(state, solver_lines_opposite_cb,
                           IMPOSSIBLE_QUITS, sctx);
        CHECKRET(DIFF_NORMAL);

        ret = foreach_tile(state, solver_spaces_oneposs_cb,
                           IMPOSSIBLE_QUITS, sctx);
        CHECKRET(DIFF_NORMAL);

        ret = solver_expand_dots(state, sctx);
        CHECKRET(DIFF_NORMAL);

        ret = solver_extend_exclaves(state, sctx);
        CHECKRET(DIFF_NORMAL);

        if (maxdiff <= DIFF_NORMAL)
            break;

        /* harder still? */

        /* if we reach here, we've made no deductions, so we terminate. */
        break;
    }

    if (check_complete(state, NULL, NULL)) goto got_result;

    diff = (maxdiff >= DIFF_UNREASONABLE) ?
        solver_recurse(state, maxdiff, depth) : DIFF_UNFINISHED;

got_result:
    free_solver(sctx);
#ifndef STANDALONE_SOLVER
    debug(("solver_state ends, diff %s:\n", galaxies_diffnames[diff]));
    dbg_state(state);
#endif

#ifdef STANDALONE_PICTURE_GENERATOR
    picture = savepic;
#endif

    return diff;
}

static int solver_state(game_state *state, int maxdiff)
{
    return solver_state_inner(state, maxdiff, 0);
}

#ifndef EDITOR
static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *tosolve;
    char *ret;
    int i;
    int diff;

    if (aux) {
        tosolve = execute_move(state, aux);
        goto solved;
    } else {
        tosolve = dup_game(currstate);
        diff = solver_state(tosolve, DIFF_UNREASONABLE);
        if (diff != DIFF_UNFINISHED && diff != DIFF_IMPOSSIBLE) {
            debug(("solve_game solved with current state.\n"));
            goto solved;
        }
        free_game(tosolve);

        tosolve = dup_game(state);
        diff = solver_state(tosolve, DIFF_UNREASONABLE);
        if (diff != DIFF_UNFINISHED && diff != DIFF_IMPOSSIBLE) {
            debug(("solve_game solved with original state.\n"));
            goto solved;
        }
        free_game(tosolve);
    }

    return NULL;

solved:
    /*
     * Clear tile associations: the solution will only include the
     * edges.
     */
    for (i = 0; i < tosolve->sx*tosolve->sy; i++)
        tosolve->grid[i].flags &= ~F_TILE_ASSOC;
    ret = diff_game(currstate, tosolve, true, -1);
    free_game(tosolve);
    return ret;
}
#endif

/* ----------------------------------------------------------
 * User interface.
 */

struct game_ui {
    bool dragging;
    int dx, dy;         /* pixel coords of drag pos. */
    int dotx, doty;     /* grid coords of dot we're dragging from. */
    int srcx, srcy;     /* grid coords of drag start */
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->dragging = false;
    ui->cur_x = ui->cur_y = 1;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
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

#define FLASH_TIME 0.15F

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define DOT_SIZE        (TILE_SIZE / 4)
#define EDGE_THICKNESS (max(TILE_SIZE / 16, 2))
#define BORDER TILE_SIZE

#define COORD(x) ( (x) * TILE_SIZE + BORDER )
#define SCOORD(x) ( ((x) * TILE_SIZE)/2 + BORDER )
#define FROMCOORD(x) ( ((x) - BORDER) / TILE_SIZE )

#define DRAW_WIDTH      (BORDER * 2 + ds->w * TILE_SIZE)
#define DRAW_HEIGHT     (BORDER * 2 + ds->h * TILE_SIZE)

#define CURSOR_SIZE DOT_SIZE

struct game_drawstate {
    bool started;
    int w, h;
    int tilesize;
    unsigned long *grid;
    int *dx, *dy;
    blitter *bl;
    blitter *blmirror;

    bool dragging;
    int dragx, dragy, oppx, oppy;

    int *colour_scratch;

    int cx, cy;
    bool cur_visible;
    blitter *cur_bl;
};

#define CORNER_TOLERANCE 0.15F
#define CENTRE_TOLERANCE 0.15F

/*
 * Round FP coordinates to the centre of the nearest edge.
 */
#ifndef EDITOR
static void coord_round_to_edge(float x, float y, int *xr, int *yr)
{
    float xs, ys, xv, yv, dx, dy;

    /*
     * Find the nearest square-centre.
     */
    xs = (float)floor(x) + 0.5F;
    ys = (float)floor(y) + 0.5F;

    /*
     * Find the nearest grid vertex.
     */
    xv = (float)floor(x + 0.5F);
    yv = (float)floor(y + 0.5F);

    /*
     * Determine whether the horizontal or vertical edge from that
     * vertex alongside that square is closer to us, by comparing
     * distances from the square cente.
     */
    dx = (float)fabs(x - xs);
    dy = (float)fabs(y - ys);
    if (dx > dy) {
        /* Vertical edge: x-coord of corner,
         * y-coord of square centre. */
        *xr = 2 * (int)xv;
        *yr = 1 + 2 * (int)floor(ys);
    } else {
        /* Horizontal edge: x-coord of square centre,
         * y-coord of corner. */
        *xr = 1 + 2 * (int)floor(xs);
        *yr = 2 * (int)yv;
    }
}
#endif

#ifdef EDITOR
static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    char buf[80];
    int px, py;
    space *sp;

    px = 2*FROMCOORD((float)x) + 0.5F;
    py = 2*FROMCOORD((float)y) + 0.5F;

    if (button == 'C' || button == 'c') return dupstr("C");

    if (button == 'S' || button == 's') {
        char *ret;
        game_state *tmp = dup_game(state);
        int cdiff = solver_state(tmp, DIFF_UNREASONABLE-1);
        ret = diff_game(state, tmp, 0, cdiff);
        free_game(tmp);
        return ret;
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        if (!INUI(state, px, py)) return NULL;
        sp = &SPACE(state, px, py);
        if (!dot_is_possible(state, sp, 1)) return NULL;
        sprintf(buf, "%c%d,%d",
                (char)((button == LEFT_BUTTON) ? 'D' : 'd'), px, py);
        return dupstr(buf);
    }

    return NULL;
}
#else
static bool edge_placement_legal(const game_state *state, int x, int y)
{
    space *sp = &SPACE(state, x, y);
    if (sp->type != s_edge)
        return false;   /* this is a face-centre or a grid vertex */

    /* Check this line doesn't actually intersect a dot */
    unsigned int flags = (GRID(state, grid, x, y).flags |
                          GRID(state, grid, x & ~1U, y & ~1U).flags |
                          GRID(state, grid, (x+1) & ~1U, (y+1) & ~1U).flags);
    if (flags & F_DOT)
        return false;
    return true;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    space *sp;

    if (IS_CURSOR_SELECT(button) && ui->cur_visible) {
        sp = &SPACE(state, ui->cur_x, ui->cur_y);
        if (ui->dragging) {
            if (ui->cur_x == ui->srcx && ui->cur_y == ui->srcy)
                return "Cancel";
            if (ok_to_add_assoc_with_opposite(
                    state, &SPACE(state, ui->cur_x, ui->cur_y),
                    &SPACE(state, ui->dotx, ui->doty)))
                return "Place";
            return (ui->srcx == ui->dotx && ui->srcy == ui->doty) ?
                "Cancel" : "Remove";
        } else if (sp->flags & F_DOT)
            return "New arrow";
        else if (sp->flags & F_TILE_ASSOC)
            return "Move arrow";
        else if (sp->type == s_edge &&
                 edge_placement_legal(state, ui->cur_x, ui->cur_y))
            return (sp->flags & F_EDGE_SET) ? "Clear" : "Edge";
    }
    return "";
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    /* UI operations (play mode):
     *
     * Toggle edge (set/unset) (left-click on edge)
     * Associate space with dot (left-drag from dot)
     * Unassociate space (left-drag from space off grid)
     * Autofill lines around shape? (right-click?)
     *
     * (edit mode; will clear all lines/associations)
     *
     * Add or remove dot (left-click)
     */
    char buf[80];
    const char *sep = "";
    int px, py;
    space *sp, *dot;

    buf[0] = '\0';

    if (button == 'H' || button == 'h') {
        char *ret;
        game_state *tmp = dup_game(state);
        solver_obvious(tmp);
        ret = diff_game(state, tmp, false, -1);
        free_game(tmp);
        return ret;
    }

    if (button == LEFT_BUTTON) {
        ui->cur_visible = false;
        coord_round_to_edge(FROMCOORD((float)x), FROMCOORD((float)y),
                            &px, &py);

        if (!INUI(state, px, py)) return NULL;
        if (!edge_placement_legal(state, px, py))
            return NULL;

        sprintf(buf, "E%d,%d", px, py);
        return dupstr(buf);
    } else if (button == RIGHT_BUTTON) {
        int px1, py1;

        ui->cur_visible = false;

        px = (int)(2*FROMCOORD((float)x) + 0.5F);
        py = (int)(2*FROMCOORD((float)y) + 0.5F);

        dot = NULL;

        /*
         * If there's a dot anywhere nearby, we pick up an arrow
         * pointing at that dot.
         */
        for (py1 = py-1; py1 <= py+1; py1++)
            for (px1 = px-1; px1 <= px+1; px1++) {
                if (px1 >= 0 && px1 < state->sx &&
                    py1 >= 0 && py1 < state->sy &&
                    x >= SCOORD(px1-1) && x < SCOORD(px1+1) &&
                    y >= SCOORD(py1-1) && y < SCOORD(py1+1) &&
                    SPACE(state, px1, py1).flags & F_DOT) {
                    /*
                     * Found a dot. Begin a drag from it.
                     */
                    dot = &SPACE(state, px1, py1);
                    ui->srcx = px1;
                    ui->srcy = py1;
                    goto done;         /* multi-level break */
                }
            }

        /*
         * Otherwise, find the nearest _square_, and pick up the
         * same arrow as it's got on it, if any.
         */
        if (!dot) {
            px = 2*FROMCOORD(x+TILE_SIZE) - 1;
            py = 2*FROMCOORD(y+TILE_SIZE) - 1;
            if (px >= 0 && px < state->sx && py >= 0 && py < state->sy) {
                sp = &SPACE(state, px, py);
                if (sp->flags & F_TILE_ASSOC) {
                    dot = &SPACE(state, sp->dotx, sp->doty);
                    ui->srcx = px;
                    ui->srcy = py;
                }
            }
        }

        done:
        /*
         * Now, if we've managed to find a dot, begin a drag.
         */
        if (dot) {
            ui->dragging = true;
            ui->dx = x;
            ui->dy = y;
            ui->dotx = dot->x;
            ui->doty = dot->y;
            return UI_UPDATE;
        }
    } else if (button == RIGHT_DRAG && ui->dragging) {
        /* just move the drag coords. */
        ui->dx = x;
        ui->dy = y;
        return UI_UPDATE;
    } else if (button == RIGHT_RELEASE && ui->dragging) {
        /*
         * Drags are always targeted at a single square.
         */
        px = 2*FROMCOORD(x+TILE_SIZE) - 1;
        py = 2*FROMCOORD(y+TILE_SIZE) - 1;

        dropped: /* We arrive here from the end of a keyboard drag. */
        ui->dragging = false;
	/*
	 * Dragging an arrow on to the same square it started from
	 * is a null move; just update the ui and finish.
	 */
	if (px == ui->srcx && py == ui->srcy)
	    return UI_UPDATE;

	/*
	 * Otherwise, we remove the arrow from its starting
	 * square if we didn't start from a dot...
	 */
	if ((ui->srcx != ui->dotx || ui->srcy != ui->doty) &&
	    SPACE(state, ui->srcx, ui->srcy).flags & F_TILE_ASSOC) {
	    sprintf(buf + strlen(buf), "%sU%d,%d", sep, ui->srcx, ui->srcy);
	    sep = ";";
	}

	/*
	 * ... and if the square we're moving it _to_ is valid, we
	 * add one there instead.
	 */
        if (INUI(state, px, py)) {
            sp = &SPACE(state, px, py);
            dot = &SPACE(state, ui->dotx, ui->doty);

            /*
             * Exception: if it's not actually legal to add an arrow
             * and its opposite at this position, we don't try,
             * because otherwise we'd append an empty entry to the
             * undo chain.
             */
            if (ok_to_add_assoc_with_opposite(state, sp, dot))
		sprintf(buf + strlen(buf), "%sA%d,%d,%d,%d",
			sep, px, py, ui->dotx, ui->doty);
	}

	if (buf[0])
	    return dupstr(buf);
	else
	    return UI_UPDATE;
    } else if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cur_x, &ui->cur_y, state->sx-1, state->sy-1, false);
        if (ui->cur_x < 1) ui->cur_x = 1;
        if (ui->cur_y < 1) ui->cur_y = 1;
        ui->cur_visible = true;
        if (ui->dragging) {
            ui->dx = SCOORD(ui->cur_x);
            ui->dy = SCOORD(ui->cur_y);
        }
        return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        sp = &SPACE(state, ui->cur_x, ui->cur_y);
        if (ui->dragging) {
            px = ui->cur_x; py = ui->cur_y;
            goto dropped;
        } else if (sp->flags & F_DOT) {
            ui->dragging = true;
            ui->dx = SCOORD(ui->cur_x);
            ui->dy = SCOORD(ui->cur_y);
            ui->dotx = ui->srcx = ui->cur_x;
            ui->doty = ui->srcy = ui->cur_y;
            return UI_UPDATE;
        } else if (sp->flags & F_TILE_ASSOC) {
            assert(sp->type == s_tile);
            ui->dragging = true;
            ui->dx = SCOORD(ui->cur_x);
            ui->dy = SCOORD(ui->cur_y);
            ui->dotx = sp->dotx;
            ui->doty = sp->doty;
            ui->srcx = ui->cur_x;
            ui->srcy = ui->cur_y;
            return UI_UPDATE;
        } else if (sp->type == s_edge &&
                   edge_placement_legal(state, ui->cur_x, ui->cur_y)) {
            sprintf(buf, "E%d,%d", ui->cur_x, ui->cur_y);
            return dupstr(buf);
        }
    }

    return NULL;
}
#endif

static bool check_complete(const game_state *state, int *dsf, int *colours)
{
    int w = state->w, h = state->h;
    int x, y, i;
    bool ret;

    bool free_dsf;
    struct sqdata {
        int minx, miny, maxx, maxy;
        int cx, cy;
        bool valid;
        int colour;
    } *sqdata;

    if (!dsf) {
	dsf = snew_dsf(w*h);
	free_dsf = true;
    } else {
	dsf_init(dsf, w*h);
	free_dsf = false;
    }

    /*
     * During actual game play, completion checking is done on the
     * basis of the edges rather than the square associations. So
     * first we must go through the grid figuring out the connected
     * components into which the edges divide it.
     */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            if (y+1 < h && !(SPACE(state, 2*x+1, 2*y+2).flags & F_EDGE_SET))
                dsf_merge(dsf, y*w+x, (y+1)*w+x);
            if (x+1 < w && !(SPACE(state, 2*x+2, 2*y+1).flags & F_EDGE_SET))
                dsf_merge(dsf, y*w+x, y*w+(x+1));
        }

    /*
     * That gives us our connected components. Now, for each
     * component, decide whether it's _valid_. A valid component is
     * one which:
     *
     *  - is 180-degree rotationally symmetric
     *  - has a dot at its centre of symmetry
     *  - has no other dots anywhere within it (including on its
     *    boundary)
     *  - contains no internal edges (i.e. edges separating two
     *    squares which are both part of the component).
     */

    /*
     * First, go through the grid finding the bounding box of each
     * component.
     */
    sqdata = snewn(w*h, struct sqdata);
    for (i = 0; i < w*h; i++) {
        sqdata[i].minx = w+1;
        sqdata[i].miny = h+1;
        sqdata[i].maxx = sqdata[i].maxy = -1;
        sqdata[i].valid = false;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            i = dsf_canonify(dsf, y*w+x);
            if (sqdata[i].minx > x)
                sqdata[i].minx = x;
            if (sqdata[i].maxx < x)
                sqdata[i].maxx = x;
            if (sqdata[i].miny > y)
                sqdata[i].miny = y;
            if (sqdata[i].maxy < y)
                sqdata[i].maxy = y;
            sqdata[i].valid = true;
        }

    /*
     * Now we're in a position to loop over each actual component
     * and figure out where its centre of symmetry has to be if
     * it's anywhere.
     */
    for (i = 0; i < w*h; i++)
        if (sqdata[i].valid) {
            int cx, cy;
            cx = sqdata[i].cx = sqdata[i].minx + sqdata[i].maxx + 1;
            cy = sqdata[i].cy = sqdata[i].miny + sqdata[i].maxy + 1;
            if (!(SPACE(state, sqdata[i].cx, sqdata[i].cy).flags & F_DOT))
                sqdata[i].valid = false;   /* no dot at centre of symmetry */
            if (dsf_canonify(dsf, (cy-1)/2*w+(cx-1)/2) != i ||
                dsf_canonify(dsf, (cy)/2*w+(cx-1)/2) != i ||
                dsf_canonify(dsf, (cy-1)/2*w+(cx)/2) != i ||
                dsf_canonify(dsf, (cy)/2*w+(cx)/2) != i)
                sqdata[i].valid = false;   /* dot at cx,cy isn't ours */
            if (SPACE(state, sqdata[i].cx, sqdata[i].cy).flags & F_DOT_BLACK)
                sqdata[i].colour = 2;
            else
                sqdata[i].colour = 1;
        }

    /*
     * Now we loop over the whole grid again, this time finding
     * extraneous dots (any dot which wholly or partially overlaps
     * a square and is not at the centre of symmetry of that
     * square's component disqualifies the component from validity)
     * and extraneous edges (any edge separating two squares
     * belonging to the same component also disqualifies that
     * component).
     */
    for (y = 1; y < state->sy-1; y++)
        for (x = 1; x < state->sx-1; x++) {
            space *sp = &SPACE(state, x, y);

            if (sp->flags & F_DOT) {
                /*
                 * There's a dot here. Use it to disqualify any
                 * component which deserves it.
                 */
                int cx, cy;
                for (cy = (y-1) >> 1; cy <= y >> 1; cy++)
                    for (cx = (x-1) >> 1; cx <= x >> 1; cx++) {
                        i = dsf_canonify(dsf, cy*w+cx);
                        if (x != sqdata[i].cx || y != sqdata[i].cy)
                            sqdata[i].valid = false;
                    }
            }

            if (sp->flags & F_EDGE_SET) {
                /*
                 * There's an edge here. Use it to disqualify a
                 * component if necessary.
                 */
                int cx1 = (x-1) >> 1, cx2 = x >> 1;
                int cy1 = (y-1) >> 1, cy2 = y >> 1;
                assert((cx1==cx2) ^ (cy1==cy2));
                i = dsf_canonify(dsf, cy1*w+cx1);
                if (i == dsf_canonify(dsf, cy2*w+cx2))
                    sqdata[i].valid = false;
            }
        }

    /*
     * And finally we test rotational symmetry: for each square in
     * the grid, find which component it's in, test that that
     * component also has a square in the symmetric position, and
     * disqualify it if it doesn't.
     */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int x2, y2;

            i = dsf_canonify(dsf, y*w+x);

            x2 = sqdata[i].cx - 1 - x;
            y2 = sqdata[i].cy - 1 - y;
            if (i != dsf_canonify(dsf, y2*w+x2))
                sqdata[i].valid = false;
        }

    /*
     * That's it. We now have all the connected components marked
     * as valid or not valid. So now we return a `colours' array if
     * we were asked for one, and also we return an overall
     * true/false value depending on whether _every_ square in the
     * grid is part of a valid component.
     */
    ret = true;
    for (i = 0; i < w*h; i++) {
        int ci = dsf_canonify(dsf, i);
        bool thisok = sqdata[ci].valid;
        if (colours)
            colours[i] = thisok ? sqdata[ci].colour : 0;
        ret = ret && thisok;
    }

    sfree(sqdata);
    if (free_dsf)
	sfree(dsf);

    return ret;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int x, y, ax, ay, n, dx, dy;
    game_state *ret = dup_game(state);
    space *sp, *dot;
    bool currently_solving = false;

    debug(("%s\n", move));

    while (*move) {
        char c = *move;
        if (c == 'E' || c == 'U' || c == 'M'
#ifdef EDITOR
            || c == 'D' || c == 'd'
#endif
            ) {
            move++;
            if (sscanf(move, "%d,%d%n", &x, &y, &n) != 2 ||
                !INUI(ret, x, y))
                goto badmove;

            sp = &SPACE(ret, x, y);
#ifdef EDITOR
            if (c == 'D' || c == 'd') {
                unsigned int currf, newf, maskf;

                if (!dot_is_possible(ret, sp, 1)) goto badmove;

                newf = F_DOT | (c == 'd' ? F_DOT_BLACK : 0);
                currf = GRID(ret, grid, x, y).flags;
                maskf = F_DOT | F_DOT_BLACK;
                /* if we clicked 'white dot':
                 *   white --> empty, empty --> white, black --> white.
                 * if we clicked 'black dot':
                 *   black --> empty, empty --> black, white --> black.
                 */
                if (currf & maskf) {
                    sp->flags &= ~maskf;
                    if ((currf & maskf) != newf)
                        sp->flags |= newf;
                } else
                    sp->flags |= newf;
                sp->nassoc = 0; /* edit-mode disallows associations. */
                game_update_dots(ret);
            } else
#endif
                   if (c == 'E') {
                if (sp->type != s_edge) goto badmove;
                sp->flags ^= F_EDGE_SET;
            } else if (c == 'U') {
                if (sp->type != s_tile || !(sp->flags & F_TILE_ASSOC))
                    goto badmove;
                /* The solver doesn't assume we'll mirror things */
                if (currently_solving)
                    remove_assoc(ret, sp);
                else
                    remove_assoc_with_opposite(ret, sp);
            } else if (c == 'M') {
                if (!(sp->flags & F_DOT)) goto badmove;
                sp->flags ^= F_DOT_HOLD;
            }
            move += n;
        } else if (c == 'A' || c == 'a') {
            move++;
            if (sscanf(move, "%d,%d,%d,%d%n", &x, &y, &ax, &ay, &n) != 4 ||
                x < 1 || y < 1 || x >= (ret->sx-1) || y >= (ret->sy-1) ||
                ax < 1 || ay < 1 || ax >= (ret->sx-1) || ay >= (ret->sy-1))
                goto badmove;

            dot = &GRID(ret, grid, ax, ay);
            if (!(dot->flags & F_DOT))goto badmove;
            if (dot->flags & F_DOT_HOLD) goto badmove;

            for (dx = -1; dx <= 1; dx++) {
                for (dy = -1; dy <= 1; dy++) {
                    sp = &GRID(ret, grid, x+dx, y+dy);
                    if (sp->type != s_tile) continue;
                    if (sp->flags & F_TILE_ASSOC) {
                        space *dot = &SPACE(ret, sp->dotx, sp->doty);
                        if (dot->flags & F_DOT_HOLD) continue;
                    }
                    /* The solver doesn't assume we'll mirror things */
                    if (currently_solving)
                        add_assoc(ret, sp, dot);
                    else
                        add_assoc_with_opposite(ret, sp, dot);
                }
            }
            move += n;
#ifdef EDITOR
        } else if (c == 'C') {
            move++;
            clear_game(ret, true);
        } else if (c == 'i') {
            int diff;
            move++;
            for (diff = 0; diff <= DIFF_UNREASONABLE; diff++)
                if (*move == galaxies_diffchars[diff])
                    break;
            if (diff > DIFF_UNREASONABLE)
                goto badmove;

            ret->cdiff = diff;
            move++;
        } else if (c == 'I') {
            int diff;
            move++;
            switch (*move) {
              case 'A':
                diff = DIFF_AMBIGUOUS;
                break;
              case 'I':
                diff = DIFF_IMPOSSIBLE;
                break;
              case 'U':
                diff = DIFF_UNFINISHED;
                break;
              default:
                goto badmove;
            }
            ret->cdiff = diff;
            move++;
#endif
        } else if (c == 'S') {
            move++;
	    ret->used_solve = true;
            currently_solving = true;
        } else
            goto badmove;

        if (*move == ';')
            move++;
        else if (*move)
            goto badmove;
    }
    if (check_complete(ret, NULL, NULL))
        ret->completed = true;
    return ret;

badmove:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

/* Lines will be much smaller size than squares; say, 1/8 the size?
 *
 * Need a 'top-left corner of location XxY' to take this into account;
 * alternaticaly, that could give the middle of that location, and the
 * drawing code would just know the expected dimensions.
 *
 * We also need something to take a click and work out what it was
 * we were interested in. Clicking on vertices is required because
 * we may want to drag from them, for example.
 */

static void game_compute_size(const game_params *params, int sz,
                              int *x, int *y)
{
    struct { int tilesize, w, h; } ads, *ds = &ads;

    ds->tilesize = sz;
    ds->w = params->w;
    ds->h = params->h;

    *x = DRAW_WIDTH;
    *y = DRAW_HEIGHT;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int sz)
{
    ds->tilesize = sz;

    assert(TILE_SIZE > 0);

    assert(!ds->bl);
    ds->bl = blitter_new(dr, TILE_SIZE, TILE_SIZE);

    assert(!ds->blmirror);
    ds->blmirror = blitter_new(dr, TILE_SIZE, TILE_SIZE);

    assert(!ds->cur_bl);
    ds->cur_bl = blitter_new(dr, TILE_SIZE, TILE_SIZE);
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    /*
     * We call game_mkhighlight to ensure the background colour
     * isn't completely white. We don't actually use the high- and
     * lowlight colours it generates.
     */
    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_WHITEBG, COL_BLACKBG);

    for (i = 0; i < 3; i++) {
        /*
         * Currently, white dots and white-background squares are
         * both pure white.
         */
        ret[COL_WHITEDOT * 3 + i] = 1.0F;
        ret[COL_WHITEBG * 3 + i] = 1.0F;

        /*
         * But black-background squares are a dark grey, whereas
         * black dots are really black.
         */
        ret[COL_BLACKDOT * 3 + i] = 0.0F;
        ret[COL_BLACKBG * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.3F;

        /*
         * In unfilled squares, we draw a faint gridwork.
         */
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.8F;

        /*
         * Edges and arrows are filled in in pure black.
         */
        ret[COL_EDGE * 3 + i] = 0.0F;
        ret[COL_ARROW * 3 + i] = 0.0F;
    }

#ifdef EDITOR
    /* tinge the edit background to bluey */
    ret[COL_BACKGROUND * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.8F;
    ret[COL_BACKGROUND * 3 + 1] = ret[COL_BACKGROUND * 3 + 0] * 0.8F;
    ret[COL_BACKGROUND * 3 + 2] = min(ret[COL_BACKGROUND * 3 + 0] * 1.4F, 1.0F);
#endif

    ret[COL_CURSOR * 3 + 0] = min(ret[COL_BACKGROUND * 3 + 0] * 1.4F, 1.0F);
    ret[COL_CURSOR * 3 + 1] = ret[COL_BACKGROUND * 3 + 0] * 0.8F;
    ret[COL_CURSOR * 3 + 2] = ret[COL_BACKGROUND * 3 + 0] * 0.8F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = false;
    ds->w = state->w;
    ds->h = state->h;

    ds->grid = snewn(ds->w*ds->h, unsigned long);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->grid[i] = 0xFFFFFFFFUL;
    ds->dx = snewn(ds->w*ds->h, int);
    ds->dy = snewn(ds->w*ds->h, int);

    ds->bl = NULL;
    ds->blmirror = NULL;
    ds->dragging = false;
    ds->dragx = ds->dragy = ds->oppx = ds->oppy = 0;

    ds->colour_scratch = snewn(ds->w * ds->h, int);

    ds->cur_bl = NULL;
    ds->cx = ds->cy = 0;
    ds->cur_visible = false;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    if (ds->cur_bl) blitter_free(dr, ds->cur_bl);
    sfree(ds->colour_scratch);
    if (ds->blmirror) blitter_free(dr, ds->blmirror);
    if (ds->bl) blitter_free(dr, ds->bl);
    sfree(ds->dx);
    sfree(ds->dy);
    sfree(ds->grid);
    sfree(ds);
}

#define DRAW_EDGE_L    0x0001
#define DRAW_EDGE_R    0x0002
#define DRAW_EDGE_U    0x0004
#define DRAW_EDGE_D    0x0008
#define DRAW_CORNER_UL 0x0010
#define DRAW_CORNER_UR 0x0020
#define DRAW_CORNER_DL 0x0040
#define DRAW_CORNER_DR 0x0080
#define DRAW_WHITE     0x0100
#define DRAW_BLACK     0x0200
#define DRAW_ARROW     0x0400
#define DRAW_CURSOR    0x0800
#define DOT_SHIFT_C    12
#define DOT_SHIFT_M    2
#define DOT_WHITE      1UL
#define DOT_BLACK      2UL

/*
 * Draw an arrow centred on (cx,cy), pointing in the direction
 * (ddx,ddy). (I.e. pointing at the point (cx+ddx, cy+ddy).
 */
static void draw_arrow(drawing *dr, game_drawstate *ds,
                       int cx, int cy, int ddx, int ddy, int col)
{
    int sqdist = ddx*ddx+ddy*ddy;
    if (sqdist == 0)
        return;                        /* avoid division by zero */
    float vlen = (float)sqrt(sqdist);
    float xdx = ddx/vlen, xdy = ddy/vlen;
    float ydx = -xdy, ydy = xdx;
    int e1x = cx + (int)(xdx*TILE_SIZE/3), e1y = cy + (int)(xdy*TILE_SIZE/3);
    int e2x = cx - (int)(xdx*TILE_SIZE/3), e2y = cy - (int)(xdy*TILE_SIZE/3);
    int adx = (int)((ydx-xdx)*TILE_SIZE/8), ady = (int)((ydy-xdy)*TILE_SIZE/8);
    int adx2 = (int)((-ydx-xdx)*TILE_SIZE/8), ady2 = (int)((-ydy-xdy)*TILE_SIZE/8);

    draw_line(dr, e1x, e1y, e2x, e2y, col);
    draw_line(dr, e1x, e1y, e1x+adx, e1y+ady, col);
    draw_line(dr, e1x, e1y, e1x+adx2, e1y+ady2, col);
}

static void draw_square(drawing *dr, game_drawstate *ds, int x, int y,
                        unsigned long flags, int ddx, int ddy)
{
    int lx = COORD(x), ly = COORD(y);
    int dx, dy;
    int gridcol;

    clip(dr, lx, ly, TILE_SIZE, TILE_SIZE);

    /*
     * Draw the tile background.
     */
    draw_rect(dr, lx, ly, TILE_SIZE, TILE_SIZE,
              (flags & DRAW_WHITE ? COL_WHITEBG :
               flags & DRAW_BLACK ? COL_BLACKBG : COL_BACKGROUND));

    /*
     * Draw the grid.
     */
    gridcol = (flags & DRAW_BLACK ? COL_BLACKDOT : COL_GRID);
    draw_rect(dr, lx, ly, 1, TILE_SIZE, gridcol);
    draw_rect(dr, lx, ly, TILE_SIZE, 1, gridcol);

    /*
     * Draw the arrow, if present, or the cursor, if here.
     */
    if (flags & DRAW_ARROW)
        draw_arrow(dr, ds, lx + TILE_SIZE/2, ly + TILE_SIZE/2, ddx, ddy,
                   (flags & DRAW_CURSOR) ? COL_CURSOR : COL_ARROW);
    else if (flags & DRAW_CURSOR)
        draw_rect_outline(dr,
                          lx + TILE_SIZE/2 - CURSOR_SIZE,
                          ly + TILE_SIZE/2 - CURSOR_SIZE,
                          2*CURSOR_SIZE+1, 2*CURSOR_SIZE+1,
                          COL_CURSOR);

    /*
     * Draw the edges.
     */
    if (flags & DRAW_EDGE_L)
        draw_rect(dr, lx, ly, EDGE_THICKNESS, TILE_SIZE, COL_EDGE);
    if (flags & DRAW_EDGE_R)
        draw_rect(dr, lx + TILE_SIZE - EDGE_THICKNESS + 1, ly,
                  EDGE_THICKNESS - 1, TILE_SIZE, COL_EDGE);
    if (flags & DRAW_EDGE_U)
        draw_rect(dr, lx, ly, TILE_SIZE, EDGE_THICKNESS, COL_EDGE);
    if (flags & DRAW_EDGE_D)
        draw_rect(dr, lx, ly + TILE_SIZE - EDGE_THICKNESS + 1,
                  TILE_SIZE, EDGE_THICKNESS - 1, COL_EDGE);
    if (flags & DRAW_CORNER_UL)
        draw_rect(dr, lx, ly, EDGE_THICKNESS, EDGE_THICKNESS, COL_EDGE);
    if (flags & DRAW_CORNER_UR)
        draw_rect(dr, lx + TILE_SIZE - EDGE_THICKNESS + 1, ly,
                  EDGE_THICKNESS - 1, EDGE_THICKNESS, COL_EDGE);
    if (flags & DRAW_CORNER_DL)
        draw_rect(dr, lx, ly + TILE_SIZE - EDGE_THICKNESS + 1,
                  EDGE_THICKNESS, EDGE_THICKNESS - 1, COL_EDGE);
    if (flags & DRAW_CORNER_DR)
        draw_rect(dr, lx + TILE_SIZE - EDGE_THICKNESS + 1,
                  ly + TILE_SIZE - EDGE_THICKNESS + 1,
                  EDGE_THICKNESS - 1, EDGE_THICKNESS - 1, COL_EDGE);

    /*
     * Draw the dots.
     */
    for (dy = 0; dy < 3; dy++)
        for (dx = 0; dx < 3; dx++) {
            int dotval = (flags >> (DOT_SHIFT_C + DOT_SHIFT_M*(dy*3+dx)));
            dotval &= (1 << DOT_SHIFT_M)-1;

            if (dotval)
                draw_circle(dr, lx+dx*TILE_SIZE/2, ly+dy*TILE_SIZE/2,
                            DOT_SIZE,
                            (dotval == 1 ? COL_WHITEDOT : COL_BLACKDOT),
                            COL_BLACKDOT);
        }

    unclip(dr);
    draw_update(dr, lx, ly, TILE_SIZE, TILE_SIZE);
}

static void calculate_opposite_point(const game_ui *ui,
                                     const game_drawstate *ds, const int x,
                                     const int y, int *oppositex,
                                     int *oppositey)
{
    /* oppositex - dotx = dotx - x <=> oppositex = 2 * dotx - x */
    *oppositex = 2 * SCOORD(ui->dotx) - x;
    *oppositey = 2 * SCOORD(ui->doty) - y;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = ds->w, h = ds->h;
    int x, y;
    bool flashing = false;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_TIME);
        flashing = (frame % 2 == 0);
    }

    if (ds->dragging) {
        assert(ds->bl);
        assert(ds->blmirror);
        blitter_load(dr, ds->blmirror, ds->oppx, ds->oppy);
        draw_update(dr, ds->oppx, ds->oppy, TILE_SIZE, TILE_SIZE);
        blitter_load(dr, ds->bl, ds->dragx, ds->dragy);
        draw_update(dr, ds->dragx, ds->dragy, TILE_SIZE, TILE_SIZE);
        ds->dragging = false;
    }
    if (ds->cur_visible) {
        assert(ds->cur_bl);
        blitter_load(dr, ds->cur_bl, ds->cx, ds->cy);
        draw_update(dr, ds->cx, ds->cy, CURSOR_SIZE*2+1, CURSOR_SIZE*2+1);
        ds->cur_visible = false;
    }

    if (!ds->started) {
        draw_rect(dr, BORDER - EDGE_THICKNESS + 1, BORDER - EDGE_THICKNESS + 1,
                  w*TILE_SIZE + EDGE_THICKNESS*2 - 1,
                  h*TILE_SIZE + EDGE_THICKNESS*2 - 1, COL_EDGE);
        draw_update(dr, 0, 0, DRAW_WIDTH, DRAW_HEIGHT);
        ds->started = true;
    }

    check_complete(state, NULL, ds->colour_scratch);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned long flags = 0;
            int ddx = 0, ddy = 0;
            space *sp, *opp;
            int dx, dy;

            /*
             * Set up the flags for this square. Firstly, see if we
             * have edges.
             */
            if (SPACE(state, x*2, y*2+1).flags & F_EDGE_SET)
                flags |= DRAW_EDGE_L;
            if (SPACE(state, x*2+2, y*2+1).flags & F_EDGE_SET)
                flags |= DRAW_EDGE_R;
            if (SPACE(state, x*2+1, y*2).flags & F_EDGE_SET)
                flags |= DRAW_EDGE_U;
            if (SPACE(state, x*2+1, y*2+2).flags & F_EDGE_SET)
                flags |= DRAW_EDGE_D;

            /*
             * Also, mark corners of neighbouring edges.
             */
            if ((x > 0 && SPACE(state, x*2-1, y*2).flags & F_EDGE_SET) ||
                (y > 0 && SPACE(state, x*2, y*2-1).flags & F_EDGE_SET))
                flags |= DRAW_CORNER_UL;
            if ((x+1 < w && SPACE(state, x*2+3, y*2).flags & F_EDGE_SET) ||
                (y > 0 && SPACE(state, x*2+2, y*2-1).flags & F_EDGE_SET))
                flags |= DRAW_CORNER_UR;
            if ((x > 0 && SPACE(state, x*2-1, y*2+2).flags & F_EDGE_SET) ||
                (y+1 < h && SPACE(state, x*2, y*2+3).flags & F_EDGE_SET))
                flags |= DRAW_CORNER_DL;
            if ((x+1 < w && SPACE(state, x*2+3, y*2+2).flags & F_EDGE_SET) ||
                (y+1 < h && SPACE(state, x*2+2, y*2+3).flags & F_EDGE_SET))
                flags |= DRAW_CORNER_DR;

            /*
             * If this square is part of a valid region, paint it
             * that region's colour. Exception: if we're flashing,
             * everything goes briefly back to background colour.
             */
            sp = &SPACE(state, x*2+1, y*2+1);
            if (sp->flags & F_TILE_ASSOC) {
                opp = tile_opposite(state, sp);
            } else {
                opp = NULL;
            }
            if (ds->colour_scratch[y*w+x] && !flashing) {
                flags |= (ds->colour_scratch[y*w+x] == 2 ?
                          DRAW_BLACK : DRAW_WHITE);
            }

            /*
             * If this square is associated with a dot but it isn't
             * part of a valid region, draw an arrow in it pointing
             * in the direction of that dot.
	     * 
	     * Exception: if this is the source point of an active
	     * drag, we don't draw the arrow.
             */
            if ((sp->flags & F_TILE_ASSOC) && !ds->colour_scratch[y*w+x]) {
		if (ui->dragging && ui->srcx == x*2+1 && ui->srcy == y*2+1) {
                    /* tile is the source, don't do it */
                } else if (ui->dragging && opp && ui->srcx == opp->x && ui->srcy == opp->y) {
                    /* opposite tile is the source, don't do it */
		} else if (sp->doty != y*2+1 || sp->dotx != x*2+1) {
                    flags |= DRAW_ARROW;
                    ddy = sp->doty - (y*2+1);
                    ddx = sp->dotx - (x*2+1);
                }
            }

            /*
             * Now go through the nine possible places we could
             * have dots.
             */
            for (dy = 0; dy < 3; dy++)
                for (dx = 0; dx < 3; dx++) {
                    sp = &SPACE(state, x*2+dx, y*2+dy);
                    if (sp->flags & F_DOT) {
                        unsigned long dotval = (sp->flags & F_DOT_BLACK ?
                                                DOT_BLACK : DOT_WHITE);
                        flags |= dotval << (DOT_SHIFT_C +
                                            DOT_SHIFT_M*(dy*3+dx));
                    }
                }

            /*
             * Now work out if we have to draw a cursor for this square;
             * cursors-on-lines are taken care of below.
             */
            if (ui->cur_visible &&
                ui->cur_x == x*2+1 && ui->cur_y == y*2+1 &&
                !(SPACE(state, x*2+1, y*2+1).flags & F_DOT))
                flags |= DRAW_CURSOR;

            /*
             * Now we have everything we're going to need. Draw the
             * square.
             */
            if (ds->grid[y*w+x] != flags ||
                ds->dx[y*w+x] != ddx ||
                ds->dy[y*w+x] != ddy) {
                draw_square(dr, ds, x, y, flags, ddx, ddy);
                ds->grid[y*w+x] = flags;
                ds->dx[y*w+x] = ddx;
                ds->dy[y*w+x] = ddy;
            }
        }

    /*
     * Draw a cursor. This secondary blitter is much less invasive than trying
     * to fix up all of the rest of the code with sufficient flags to be able to
     * display this sensibly.
     */
    if (ui->cur_visible) {
        space *sp = &SPACE(state, ui->cur_x, ui->cur_y);
        ds->cur_visible = true;
        ds->cx = SCOORD(ui->cur_x) - CURSOR_SIZE;
        ds->cy = SCOORD(ui->cur_y) - CURSOR_SIZE;
        blitter_save(dr, ds->cur_bl, ds->cx, ds->cy);
        if (sp->flags & F_DOT) {
            /* draw a red dot (over the top of whatever would be there already) */
            draw_circle(dr, SCOORD(ui->cur_x), SCOORD(ui->cur_y), DOT_SIZE,
                        COL_CURSOR, COL_BLACKDOT);
        } else if (sp->type != s_tile) {
            /* draw an edge/vertex square; tile cursors are dealt with above. */
            int dx = (ui->cur_x % 2) ? CURSOR_SIZE : CURSOR_SIZE/3;
            int dy = (ui->cur_y % 2) ? CURSOR_SIZE : CURSOR_SIZE/3;
            int x1 = SCOORD(ui->cur_x)-dx, y1 = SCOORD(ui->cur_y)-dy;
            int xs = dx*2+1, ys = dy*2+1;

            draw_rect(dr, x1, y1, xs, ys, COL_CURSOR);
        }
        draw_update(dr, ds->cx, ds->cy, CURSOR_SIZE*2+1, CURSOR_SIZE*2+1);
    }

    if (ui->dragging) {
        int oppx, oppy;

        ds->dragging = true;
        ds->dragx = ui->dx - TILE_SIZE/2;
        ds->dragy = ui->dy - TILE_SIZE/2;
        calculate_opposite_point(ui, ds, ui->dx, ui->dy, &oppx, &oppy);
        ds->oppx = oppx - TILE_SIZE/2;
        ds->oppy = oppy - TILE_SIZE/2;

        blitter_save(dr, ds->bl, ds->dragx, ds->dragy);
        clip(dr, ds->dragx, ds->dragy, TILE_SIZE, TILE_SIZE);
        draw_arrow(dr, ds, ui->dx, ui->dy, SCOORD(ui->dotx) - ui->dx,
                   SCOORD(ui->doty) - ui->dy, COL_ARROW);
        unclip(dr);
        draw_update(dr, ds->dragx, ds->dragy, TILE_SIZE, TILE_SIZE);

        blitter_save(dr, ds->blmirror, ds->oppx, ds->oppy);
        clip(dr, ds->oppx, ds->oppy, TILE_SIZE, TILE_SIZE);
        draw_arrow(dr, ds, oppx, oppy, SCOORD(ui->dotx) - oppx,
                   SCOORD(ui->doty) - oppy, COL_ARROW);
        unclip(dr);
        draw_update(dr, ds->oppx, ds->oppy, TILE_SIZE, TILE_SIZE);
    }
#ifdef EDITOR
    {
        char buf[256];
        if (state->cdiff != -1)
            sprintf(buf, "Puzzle is %s.", galaxies_diffnames[state->cdiff]);
        else
            buf[0] = '\0';
        status_bar(dr, buf);
    }
#endif
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if ((!oldstate->completed && newstate->completed) &&
        !(newstate->used_solve))
        return 3 * FLASH_TIME;
    else
        return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        space *sp = &SPACE(state, ui->cur_x, ui->cur_y);

        if(sp->flags & F_DOT) {
            *x = SCOORD(ui->cur_x) - DOT_SIZE;
            *y = SCOORD(ui->cur_y) - DOT_SIZE;
            *w = *h = 2 * DOT_SIZE + 1;
        } else if(sp->type != s_tile) {
            int dx = (ui->cur_x % 2) ? CURSOR_SIZE : CURSOR_SIZE/3;
            int dy = (ui->cur_y % 2) ? CURSOR_SIZE : CURSOR_SIZE/3;
            int x1 = SCOORD(ui->cur_x)-dx, y1 = SCOORD(ui->cur_y)-dy;
            int xs = dx*2+1, ys = dy*2+1;

            *x = x1;
            *y = y1;
            *w = xs;
            *h = ys;
        } else {
            *x = SCOORD(ui->cur_x) - CURSOR_SIZE;
            *y = SCOORD(ui->cur_y) - CURSOR_SIZE;
            *w = *h = 2 * CURSOR_SIZE + 1;
        }
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

#ifndef EDITOR
static void game_print_size(const game_params *params, float *x, float *y)
{
   int pw, ph;

   /*
    * 8mm squares by default. (There isn't all that much detail
    * that needs to go in each square.)
    */
   game_compute_size(params, 800, &pw, &ph);
   *x = pw / 100.0F;
   *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int sz)
{
    int w = state->w, h = state->h;
    int white, black, blackish;
    int x, y, i, j;
    int *colours, *dsf;
    int *coords = NULL;
    int ncoords = 0, coordsize = 0;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    ds->tilesize = sz;

    white = print_mono_colour(dr, 1);
    black = print_mono_colour(dr, 0);
    blackish = print_hatched_colour(dr, HATCH_X);

    /*
     * Get the completion information.
     */
    dsf = snewn(w * h, int);
    colours = snewn(w * h, int);
    check_complete(state, dsf, colours);

    /*
     * Draw the grid.
     */
    print_line_width(dr, TILE_SIZE / 64);
    for (x = 1; x < w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), black);
    for (y = 1; y < h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), black);

    /*
     * Shade the completed regions. Just in case any particular
     * printing platform deals badly with adjacent
     * similarly-hatched regions, we'll fill each one as a single
     * polygon.
     */
    for (i = 0; i < w*h; i++) {
	j = dsf_canonify(dsf, i);
	if (colours[j] != 0) {
	    int dx, dy, t;

	    /*
	     * This is the first square we've run into belonging to
	     * this polyomino, which means an edge of the polyomino
	     * is certain to be to our left. (After we finish
	     * tracing round it, we'll set the colours[] entry to
	     * zero to prevent accidentally doing it again.)
	     */

	    x = i % w;
	    y = i / w;
	    dx = -1;
	    dy = 0;
	    ncoords = 0;
	    while (1) {
		/*
		 * We are currently sitting on square (x,y), which
		 * we know to be in our polyomino, and we also know
		 * that (x+dx,y+dy) is not. The way I visualise
		 * this is that we're standing to the right of a
		 * boundary line, stretching our left arm out to
		 * point to the exterior square on the far side.
		 */

		/*
		 * First, check if we've gone round the entire
		 * polyomino.
		 */
		if (ncoords > 0 &&
		    (x == i%w && y == i/w && dx == -1 && dy == 0))
		    break;

		/*
		 * Add to our coordinate list the coordinate
		 * backwards and to the left of where we are.
		 */
		if (ncoords + 2 > coordsize) {
		    coordsize = (ncoords * 3 / 2) + 64;
		    coords = sresize(coords, coordsize, int);
		}
		coords[ncoords++] = COORD((2*x+1 + dx + dy) / 2);
		coords[ncoords++] = COORD((2*y+1 + dy - dx) / 2);

		/*
		 * Follow the edge round. If the square directly in
		 * front of us is not part of the polyomino, we
		 * turn right; if it is and so is the square in
		 * front of (x+dx,y+dy), we turn left; otherwise we
		 * go straight on.
		 */
		if (x-dy < 0 || x-dy >= w || y+dx < 0 || y+dx >= h ||
		    dsf_canonify(dsf, (y+dx)*w+(x-dy)) != j) {
		    /* Turn right. */
		    t = dx;
		    dx = -dy;
		    dy = t;
		} else if (x+dx-dy >= 0 && x+dx-dy < w &&
			   y+dy+dx >= 0 && y+dy+dx < h &&
			   dsf_canonify(dsf, (y+dy+dx)*w+(x+dx-dy)) == j) {
		    /* Turn left. */
		    x += dx;
		    y += dy;
		    t = dx;
		    dx = dy;
		    dy = -t;
		    x -= dx;
		    y -= dy;
		} else {
		    /* Straight on. */
		    x -= dy;
		    y += dx;
		}
	    }

	    /*
	     * Now we have our polygon complete, so fill it.
	     */
	    draw_polygon(dr, coords, ncoords/2,
			 colours[j] == 2 ? blackish : -1, black);

	    /*
	     * And mark this polyomino as done.
	     */
	    colours[j] = 0;
	}
    }

    /*
     * Draw the edges.
     */
    for (y = 0; y <= h; y++)
	for (x = 0; x <= w; x++) {
	    if (x < w && SPACE(state, x*2+1, y*2).flags & F_EDGE_SET)
		draw_rect(dr, COORD(x)-EDGE_THICKNESS, COORD(y)-EDGE_THICKNESS,
			  EDGE_THICKNESS * 2 + TILE_SIZE, EDGE_THICKNESS * 2,
			  black);
	    if (y < h && SPACE(state, x*2, y*2+1).flags & F_EDGE_SET)
		draw_rect(dr, COORD(x)-EDGE_THICKNESS, COORD(y)-EDGE_THICKNESS,
			  EDGE_THICKNESS * 2, EDGE_THICKNESS * 2 + TILE_SIZE,
			  black);
	}

    /*
     * Draw the dots.
     */
    for (y = 0; y <= 2*h; y++)
	for (x = 0; x <= 2*w; x++)
	    if (SPACE(state, x, y).flags & F_DOT) {
                draw_circle(dr, (int)COORD(x/2.0), (int)COORD(y/2.0), DOT_SIZE,
                            (SPACE(state, x, y).flags & F_DOT_BLACK ?
			     black : white), black);
	    }

    sfree(dsf);
    sfree(colours);
    sfree(coords);
}
#endif

#ifdef COMBINED
#define thegame galaxies
#endif

const struct game thegame = {
    "Galaxies", "games.galaxies", "galaxies",
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
#ifdef EDITOR
    false, NULL,
#else
    true, solve_game,
#endif
    true, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
#ifdef EDITOR
    NULL,
#else
    current_key_label,
#endif
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
#ifdef EDITOR
    false, false, NULL, NULL,
    true,                              /* wants_statusbar */
#else
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
#endif
    false, NULL,                       /* timing_state */
    REQUIRE_RBUTTON,		       /* flags */
};

#ifdef STANDALONE_SOLVER

static const char *quis;

#include <time.h>

static void usage_exit(const char *msg)
{
    if (msg)
        fprintf(stderr, "%s: %s\n", quis, msg);
    fprintf(stderr, "Usage: %s [--seed SEED] --soak <params> | [game_id [game_id ...]]\n", quis);
    exit(1);
}

static void dump_state(game_state *state)
{
    char *temp = game_text_format(state);
    printf("%s\n", temp);
    sfree(temp);
}

static int gen(game_params *p, random_state *rs, bool debug)
{
    char *desc;
    int diff;
    game_state *state;

#ifndef DEBUGGING
    solver_show_working = debug;
#endif
    printf("Generating a %dx%d %s puzzle.\n",
           p->w, p->h, galaxies_diffnames[p->diff]);

    desc = new_game_desc(p, rs, NULL, false);
    state = new_game(NULL, p, desc);
    dump_state(state);

    diff = solver_state(state, DIFF_UNREASONABLE);
    printf("Generated %s game %dx%d:%s\n",
           galaxies_diffnames[diff], p->w, p->h, desc);
    dump_state(state);

    free_game(state);
    sfree(desc);

    return diff;
}

static void soak(game_params *p, random_state *rs)
{
    time_t tt_start, tt_now, tt_last;
    char *desc;
    game_state *st;
    int diff, n = 0, i, diffs[DIFF_MAX], ndots = 0, nspaces = 0;

#ifndef DEBUGGING
    solver_show_working = false;
#endif
    tt_start = tt_now = time(NULL);
    for (i = 0; i < DIFF_MAX; i++) diffs[i] = 0;
    one_try = true;

    printf("Soak-generating a %dx%d grid, max. diff %s.\n",
           p->w, p->h, galaxies_diffnames[p->diff]);
    printf("   [");
    for (i = 0; i < DIFF_MAX; i++)
        printf("%s%s", (i == 0) ? "" : ", ", galaxies_diffnames[i]);
    printf("]\n");

    while (1) {
        char *aux;
        desc = new_game_desc(p, rs, &aux, false);
        sfree(aux);
        st = new_game(NULL, p, desc);
        diff = solver_state(st, p->diff);
        nspaces += st->w*st->h;
        for (i = 0; i < st->sx*st->sy; i++)
            if (st->grid[i].flags & F_DOT) ndots++;
        free_game(st);
        sfree(desc);

        diffs[diff]++;
        n++;
        tt_last = time(NULL);
        if (tt_last > tt_now) {
            tt_now = tt_last;
            printf("%d total, %3.1f/s, [",
                   n, (double)n / ((double)tt_now - tt_start));
            for (i = 0; i < DIFF_MAX; i++)
                printf("%s%.1f%%", (i == 0) ? "" : ", ",
                       100.0 * ((double)diffs[i] / (double)n));
            printf("], %.1f%% dots\n",
                   100.0 * ((double)ndots / (double)nspaces));
        }
    }
}

int main(int argc, char **argv)
{
    game_params *p;
    char *id = NULL, *desc;
    const char *err;
    game_state *s;
    int diff;
    bool do_soak = false, verbose = false;
    random_state *rs;
    time_t seed = time(NULL);

    quis = argv[0];
    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            verbose = true;
        } else if (!strcmp(p, "--seed")) {
            if (argc == 0) usage_exit("--seed needs an argument");
            seed = (time_t)atoi(*++argv);
            argc--;
        } else if (!strcmp(p, "--soak")) {
            do_soak = true;
        } else if (*p == '-') {
            usage_exit("unrecognised option");
        } else {
            id = p;
        }
    }

    p = default_params();
    rs = random_new((void*)&seed, sizeof(time_t));

    if (do_soak) {
        if (!id) usage_exit("need one argument for --soak");
        decode_params(p, *argv);
        soak(p, rs);
        return 0;
    }

    if (!id) {
        while (1) {
            p->w = random_upto(rs, 15) + 3;
            p->h = random_upto(rs, 15) + 3;
            p->diff = random_upto(rs, DIFF_UNREASONABLE);
            diff = gen(p, rs, false);
        }
        return 0;
    }

    desc = strchr(id, ':');
    if (!desc) {
        decode_params(p, id);
        gen(p, rs, verbose);
    } else {
#ifndef DEBUGGING
        solver_show_working = true;
#endif
        *desc++ = '\0';
        decode_params(p, id);
        err = validate_desc(p, desc);
        if (err) {
            fprintf(stderr, "%s: %s\n", argv[0], err);
            exit(1);
        }
        s = new_game(NULL, p, desc);
        diff = solver_state(s, DIFF_UNREASONABLE);
        dump_state(s);
        printf("Puzzle is %s.\n", galaxies_diffnames[diff]);
        free_game(s);
    }

    free_params(p);

    return 0;
}

#endif

#ifdef STANDALONE_PICTURE_GENERATOR

/*
 * Main program for the standalone picture generator. To use it,
 * simply provide it with an XBM-format bitmap file (note XBM, not
 * XPM) on standard input, and it will output a game ID in return.
 * For example:
 *
 *   $ ./galaxiespicture < badly-drawn-cat.xbm
 *   11x11:eloMBLzFeEzLNMWifhaWYdDbixCymBbBMLoDdewGg
 *
 * If you want a puzzle with a non-standard difficulty level, pass
 * a partial parameters string as a command-line argument (e.g.
 * `./galaxiespicture du < foo.xbm', where `du' is the same suffix
 * which if it appeared in a random-seed game ID would set the
 * difficulty level to Unreasonable). However, be aware that if the
 * generator fails to produce an adequately difficult puzzle too
 * many times then it will give up and return an easier one (just
 * as it does during normal GUI play). To be sure you really have
 * the difficulty you asked for, use galaxiessolver to
 * double-check.
 * 
 * (Perhaps I ought to include an option to make this standalone
 * generator carry on looping until it really does get the right
 * difficulty. Hmmm.)
 */

#include <time.h>

int main(int argc, char **argv)
{
    game_params *par;
    char *params, *desc;
    random_state *rs;
    time_t seed = time(NULL);
    char buf[4096];
    int i;
    int x, y;

    par = default_params();
    if (argc > 1)
	decode_params(par, argv[1]);   /* get difficulty */
    par->w = par->h = -1;

    /*
     * Now read an XBM file from standard input. This is simple and
     * hacky and will do very little error detection, so don't feed
     * it bogus data.
     */
    picture = NULL;
    x = y = 0;
    while (fgets(buf, sizeof(buf), stdin)) {
	buf[strcspn(buf, "\r\n")] = '\0';
	if (!strncmp(buf, "#define", 7)) {
	    /*
	     * Lines starting `#define' give the width and height.
	     */
	    char *num = buf + strlen(buf);
	    char *symend;

	    while (num > buf && isdigit((unsigned char)num[-1]))
		num--;
	    symend = num;
	    while (symend > buf && isspace((unsigned char)symend[-1]))
		symend--;

	    if (symend-5 >= buf && !strncmp(symend-5, "width", 5))
		par->w = atoi(num);
	    else if (symend-6 >= buf && !strncmp(symend-6, "height", 6))
		par->h = atoi(num);
	} else {
	    /*
	     * Otherwise, break the string up into words and take
	     * any word of the form `0x' plus hex digits to be a
	     * byte.
	     */
	    char *p, *wordstart;

	    if (!picture) {
		if (par->w < 0 || par->h < 0) {
		    printf("failed to read width and height\n");
		    return 1;
		}
		picture = snewn(par->w * par->h, int);
		for (i = 0; i < par->w * par->h; i++)
		    picture[i] = -1;
	    }

	    p = buf;
	    while (*p) {
		while (*p && (*p == ',' || isspace((unsigned char)*p)))
		    p++;
		wordstart = p;
		while (*p && !(*p == ',' || *p == '}' ||
			       isspace((unsigned char)*p)))
		    p++;
		if (*p)
		    *p++ = '\0';

		if (wordstart[0] == '0' &&
		    (wordstart[1] == 'x' || wordstart[1] == 'X') &&
		    !wordstart[2 + strspn(wordstart+2,
					  "0123456789abcdefABCDEF")]) {
		    unsigned long byte = strtoul(wordstart+2, NULL, 16);
		    for (i = 0; i < 8; i++) {
			int bit = (byte >> i) & 1;
			if (y < par->h && x < par->w)
			    picture[y * par->w + x] = bit;
			x++;
		    }

		    if (x >= par->w) {
			x = 0;
			y++;
		    }
		}
	    }
	}
    }

    for (i = 0; i < par->w * par->h; i++)
	if (picture[i] < 0) {
	    fprintf(stderr, "failed to read enough bitmap data\n");
	    return 1;
	}

    rs = random_new((void*)&seed, sizeof(time_t));

    desc = new_game_desc(par, rs, NULL, false);
    params = encode_params(par, false);
    printf("%s:%s\n", params, desc);

    sfree(desc);
    sfree(params);
    free_params(par);
    random_free(rs);

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
