/*
 * lightup.c: Implementation of the Nikoli game 'Light Up'.
 *
 * Possible future solver enhancements:
 *
 *  - In a situation where two clues are diagonally adjacent, you can
 *    deduce bounds on the number of lights shared between them. For
 *    instance, suppose a 3 clue is diagonally adjacent to a 1 clue:
 *    of the two squares adjacent to both clues, at least one must be
 *    a light (or the 3 would be unsatisfiable) and yet at most one
 *    must be a light (or the 1 would be overcommitted), so in fact
 *    _exactly_ one must be a light, and hence the other two squares
 *    adjacent to the 3 must also be lights and the other two adjacent
 *    to the 1 must not. Likewise if the 3 is replaced with a 2 but
 *    one of its other two squares is known not to be a light, and so
 *    on.
 *
 *  - In a situation where two clues are orthogonally separated (not
 *    necessarily directly adjacent), you may be able to deduce
 *    something about the squares that align with each other. For
 *    instance, suppose two clues are vertically adjacent. Consider
 *    the pair of squares A,B horizontally adjacent to the top clue,
 *    and the pair C,D horizontally adjacent to the bottom clue.
 *    Assuming no intervening obstacles, A and C align with each other
 *    and hence at most one of them can be a light, and B and D
 *    likewise, so we must have at most two lights between the four
 *    squares. So if the clues indicate that there are at _least_ two
 *    lights in those four squares because the top clue requires at
 *    least one of AB to be a light and the bottom one requires at
 *    least one of CD, then we can in fact deduce that there are
 *    _exactly_ two lights between the four squares, and fill in the
 *    other squares adjacent to each clue accordingly. For instance,
 *    if both clues are 3s, then we instantly deduce that all four of
 *    the squares _vertically_ adjacent to the two clues must be
 *    lights. (For that to happen, of course, there'd also have to be
 *    a black square in between the clues, so the two inner lights
 *    don't light each other.)
 *
 *  - I haven't thought it through carefully, but there's always the
 *    possibility that both of the above deductions are special cases
 *    of some more general pattern which can be made computationally
 *    feasible...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

/*
 * In standalone solver mode, `verbose' is a variable which can be
 * set by command-line option; in debugging mode it's simply always
 * true.
 */
#if defined STANDALONE_SOLVER
#define SOLVER_DIAGNOSTICS
static int verbose = 0;
#undef debug
#define debug(x) printf x
#elif defined SOLVER_DIAGNOSTICS
#define verbose 2
#endif

/* --- Constants, structure definitions, etc. --- */

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE       (ds->tilesize)
#define BORDER          (TILE_SIZE / 2)
#define TILE_RADIUS     (ds->crad)

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define FLASH_TIME 0.30F

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_BLACK,			       /* black */
    COL_LIGHT,			       /* white */
    COL_LIT,			       /* yellow */
    COL_ERROR,			       /* red */
    COL_CURSOR,
    NCOLOURS
};

enum { SYMM_NONE, SYMM_REF2, SYMM_ROT2, SYMM_REF4, SYMM_ROT4, SYMM_MAX };

#define DIFFCOUNT 2

struct game_params {
    int w, h;
    int blackpc;        /* %age of black squares */
    int symm;
    int difficulty;     /* 0 to DIFFCOUNT */
};

#define F_BLACK         1

/* flags for black squares */
#define F_NUMBERED      2       /* it has a number attached */
#define F_NUMBERUSED    4       /* this number was useful for solving */

/* flags for non-black squares */
#define F_IMPOSSIBLE    8       /* can't put a light here */
#define F_LIGHT         16

#define F_MARK          32

struct game_state {
    int w, h, nlights;
    int *lights;        /* For black squares, (optionally) the number
                           of surrounding lights. For non-black squares,
                           the number of times it's lit. size h*w*/
    unsigned int *flags;        /* size h*w */
    bool completed, used_solve;
};

#define GRID(gs,grid,x,y) (gs->grid[(y)*((gs)->w) + (x)])

/* A ll_data holds information about which lights would be lit by
 * a particular grid location's light (or conversely, which locations
 * could light a specific other location). */
/* most things should consider this struct opaque. */
typedef struct {
    int ox,oy;
    int minx, maxx, miny, maxy;
    bool include_origin;
} ll_data;

/* Macro that executes 'block' once per light in lld, including
 * the origin if include_origin is specified. 'block' can use
 * lx and ly as the coords. */
#define FOREACHLIT(lld,block) do {                              \
  int lx,ly;                                                    \
  ly = (lld)->oy;                                               \
  for (lx = (lld)->minx; lx <= (lld)->maxx; lx++) {             \
    if (lx == (lld)->ox) continue;                              \
    block                                                       \
  }                                                             \
  lx = (lld)->ox;                                               \
  for (ly = (lld)->miny; ly <= (lld)->maxy; ly++) {             \
    if (!(lld)->include_origin && ly == (lld)->oy) continue;    \
    block                                                       \
  }                                                             \
} while(0)


typedef struct {
    struct { int x, y; unsigned int f; } points[4];
    int npoints;
} surrounds;

/* Fills in (doesn't allocate) a surrounds structure with the grid locations
 * around a given square, taking account of the edges. */
static void get_surrounds(const game_state *state, int ox, int oy,
                          surrounds *s)
{
    assert(ox >= 0 && ox < state->w && oy >= 0 && oy < state->h);
    s->npoints = 0;
#define ADDPOINT(cond,nx,ny) do {\
    if (cond) { \
        s->points[s->npoints].x = (nx); \
        s->points[s->npoints].y = (ny); \
        s->points[s->npoints].f = 0; \
        s->npoints++; \
    } } while(0)
    ADDPOINT(ox > 0,            ox-1, oy);
    ADDPOINT(ox < (state->w-1), ox+1, oy);
    ADDPOINT(oy > 0,            ox,   oy-1);
    ADDPOINT(oy < (state->h-1), ox,   oy+1);
}

/* --- Game parameter functions --- */

#define DEFAULT_PRESET 0

static const struct game_params lightup_presets[] = {
    { 7, 7, 20, SYMM_ROT4, 0 },
    { 7, 7, 20, SYMM_ROT4, 1 },
    { 7, 7, 20, SYMM_ROT4, 2 },
    { 10, 10, 20, SYMM_ROT2, 0 },
    { 10, 10, 20, SYMM_ROT2, 1 },
#ifdef SLOW_SYSTEM
    { 12, 12, 20, SYMM_ROT2, 0 },
    { 12, 12, 20, SYMM_ROT2, 1 },
#else
    { 10, 10, 20, SYMM_ROT2, 2 },
    { 14, 14, 20, SYMM_ROT2, 0 },
    { 14, 14, 20, SYMM_ROT2, 1 },
    { 14, 14, 20, SYMM_ROT2, 2 }
#endif
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = lightup_presets[DEFAULT_PRESET];

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(lightup_presets))
        return false;

    ret = default_params();
    *ret = lightup_presets[i];
    *params = ret;

    sprintf(buf, "%dx%d %s",
            ret->w, ret->h,
            ret->difficulty == 2 ? "hard" :
            ret->difficulty == 1 ? "tricky" : "easy");
    *name = dupstr(buf);

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

#define EATNUM(x) do { \
    (x) = atoi(string); \
    while (*string && isdigit((unsigned char)*string)) string++; \
} while(0)

static void decode_params(game_params *params, char const *string)
{
    EATNUM(params->w);
    if (*string == 'x') {
        string++;
        EATNUM(params->h);
    }
    if (*string == 'b') {
        string++;
        EATNUM(params->blackpc);
    }
    if (*string == 's') {
        string++;
        EATNUM(params->symm);
    } else {
        /* cope with user input such as '18x10' by ensuring symmetry
         * is not selected by default to be incompatible with dimensions */
        if (params->symm == SYMM_ROT4 && params->w != params->h)
            params->symm = SYMM_ROT2;
    }
    params->difficulty = 0;
    /* cope with old params */
    if (*string == 'r') {
        params->difficulty = 2;
        string++;
    }
    if (*string == 'd') {
        string++;
        EATNUM(params->difficulty);
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[80];

    if (full) {
        sprintf(buf, "%dx%db%ds%dd%d",
                params->w, params->h, params->blackpc,
                params->symm,
                params->difficulty);
    } else {
        sprintf(buf, "%dx%d", params->w, params->h);
    }
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "%age of black squares";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->blackpc);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Symmetry";
    ret[3].type = C_CHOICES;
    ret[3].u.choices.choicenames = ":None"
                  ":2-way mirror:2-way rotational"
                  ":4-way mirror:4-way rotational";
    ret[3].u.choices.selected = params->symm;

    ret[4].name = "Difficulty";
    ret[4].type = C_CHOICES;
    ret[4].u.choices.choicenames = ":Easy:Tricky:Hard";
    ret[4].u.choices.selected = params->difficulty;

    ret[5].name = NULL;
    ret[5].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w =       atoi(cfg[0].u.string.sval);
    ret->h =       atoi(cfg[1].u.string.sval);
    ret->blackpc = atoi(cfg[2].u.string.sval);
    ret->symm =    cfg[3].u.choices.selected;
    ret->difficulty = cfg[4].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 || params->h < 2)
        return "Width and height must be at least 2";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (full) {
        if (params->blackpc < 5 || params->blackpc > 100)
            return "Percentage of black squares must be between 5% and 100%";
        if (params->w != params->h) {
            if (params->symm == SYMM_ROT4)
                return "4-fold symmetry is only available with square grids";
        }
        if ((params->symm == SYMM_ROT4 || params->symm == SYMM_REF4) && params->w < 3 && params->h < 3)
            return "Width or height must be at least 3 for 4-way symmetry";
        if (params->symm < 0 || params->symm >= SYMM_MAX)
            return "Unknown symmetry type";
        if (params->difficulty < 0 || params->difficulty > DIFFCOUNT)
            return "Unknown difficulty level";
    }
    return NULL;
}

/* --- Game state construction/freeing helper functions --- */

static game_state *new_state(const game_params *params)
{
    game_state *ret = snew(game_state);

    ret->w = params->w;
    ret->h = params->h;
    ret->lights = snewn(ret->w * ret->h, int);
    ret->nlights = 0;
    memset(ret->lights, 0, ret->w * ret->h * sizeof(int));
    ret->flags = snewn(ret->w * ret->h, unsigned int);
    memset(ret->flags, 0, ret->w * ret->h * sizeof(unsigned int));
    ret->completed = false;
    ret->used_solve = false;
    return ret;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;

    ret->lights = snewn(ret->w * ret->h, int);
    memcpy(ret->lights, state->lights, ret->w * ret->h * sizeof(int));
    ret->nlights = state->nlights;

    ret->flags = snewn(ret->w * ret->h, unsigned int);
    memcpy(ret->flags, state->flags, ret->w * ret->h * sizeof(unsigned int));

    ret->completed = state->completed;
    ret->used_solve = state->used_solve;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->lights);
    sfree(state->flags);
    sfree(state);
}

static void debug_state(game_state *state)
{
    int x, y;
    char c = '?';

    (void)c; /* placate -Wunused-but-set-variable if debug() does nothing */

    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            c = '.';
            if (GRID(state, flags, x, y) & F_BLACK) {
                if (GRID(state, flags, x, y) & F_NUMBERED)
                    c = GRID(state, lights, x, y) + '0';
                else
                    c = '#';
            } else {
                if (GRID(state, flags, x, y) & F_LIGHT)
                    c = 'O';
                else if (GRID(state, flags, x, y) & F_IMPOSSIBLE)
                    c = 'X';
            }
            debug(("%c", (int)c));
        }
        debug(("     "));
        for (x = 0; x < state->w; x++) {
            if (GRID(state, flags, x, y) & F_BLACK)
                c = '#';
            else {
                c = (GRID(state, flags, x, y) & F_LIGHT) ? 'A' : 'a';
                c += GRID(state, lights, x, y);
            }
            debug(("%c", (int)c));
        }
        debug(("\n"));
    }
}

/* --- Game completion test routines. --- */

/* These are split up because occasionally functions are only
 * interested in one particular aspect. */

/* Returns true if all grid spaces are lit. */
static bool grid_lit(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (GRID(state,flags,x,y) & F_BLACK) continue;
            if (GRID(state,lights,x,y) == 0)
                return false;
        }
    }
    return true;
}

/* Returns non-zero if any lights are lit by other lights. */
static bool grid_overlap(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state, flags, x, y) & F_LIGHT)) continue;
            if (GRID(state, lights, x, y) > 1)
                return true;
        }
    }
    return false;
}

static bool number_wrong(const game_state *state, int x, int y)
{
    surrounds s;
    int i, n, empty, lights = GRID(state, lights, x, y);

    /*
     * This function computes the display hint for a number: we
     * turn the number red if it is definitely wrong. This means
     * that either
     * 
     *  (a) it has too many lights around it, or
     * 	(b) it would have too few lights around it even if all the
     * 	    plausible squares (not black, lit or F_IMPOSSIBLE) were
     * 	    filled with lights.
     */

    assert(GRID(state, flags, x, y) & F_NUMBERED);
    get_surrounds(state, x, y, &s);

    empty = n = 0;
    for (i = 0; i < s.npoints; i++) {
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_LIGHT) {
	    n++;
	    continue;
	}
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_BLACK)
	    continue;
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_IMPOSSIBLE)
	    continue;
	if (GRID(state,lights,s.points[i].x,s.points[i].y))
	    continue;
	empty++;
    }
    return (n > lights || (n + empty < lights));
}

static bool number_correct(game_state *state, int x, int y)
{
    surrounds s;
    int n = 0, i, lights = GRID(state, lights, x, y);

    assert(GRID(state, flags, x, y) & F_NUMBERED);
    get_surrounds(state, x, y, &s);
    for (i = 0; i < s.npoints; i++) {
        if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_LIGHT)
            n++;
    }
    return n == lights;
}

/* Returns true if any numbers add up incorrectly. */
static bool grid_addsup(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state, flags, x, y) & F_NUMBERED)) continue;
            if (!number_correct(state, x, y)) return false;
        }
    }
    return true;
}

static bool grid_correct(game_state *state)
{
    if (grid_lit(state) &&
        !grid_overlap(state) &&
        grid_addsup(state)) return true;
    return false;
}

/* --- Board initial setup (blacks, lights, numbers) --- */

static void clean_board(game_state *state, bool leave_blacks)
{
    int x,y;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (leave_blacks)
                GRID(state, flags, x, y) &= F_BLACK;
            else
                GRID(state, flags, x, y) = 0;
            GRID(state, lights, x, y) = 0;
        }
    }
    state->nlights = 0;
}

static void set_blacks(game_state *state, const game_params *params,
                       random_state *rs)
{
    int x, y, degree = 0, nblack;
    bool rotate = false;
    int rh, rw, i;
    int wodd = (state->w % 2) ? 1 : 0;
    int hodd = (state->h % 2) ? 1 : 0;
    int xs[4], ys[4];

    switch (params->symm) {
    case SYMM_NONE: degree = 1; rotate = false; break;
    case SYMM_ROT2: degree = 2; rotate = true; break;
    case SYMM_REF2: degree = 2; rotate = false; break;
    case SYMM_ROT4: degree = 4; rotate = true; break;
    case SYMM_REF4: degree = 4; rotate = false; break;
    default: assert(!"Unknown symmetry type");
    }
    if (params->symm == SYMM_ROT4 && (state->h != state->w))
        assert(!"4-fold symmetry unavailable without square grid");

    if (degree == 4) {
        rw = state->w/2;
        rh = state->h/2;
        if (!rotate) rw += wodd; /* ... but see below. */
        rh += hodd;
    } else if (degree == 2) {
        rw = state->w;
        rh = state->h/2;
        rh += hodd;
    } else {
        rw = state->w;
        rh = state->h;
    }

    /* clear, then randomise, required region. */
    clean_board(state, false);
    nblack = (rw * rh * params->blackpc) / 100;
    for (i = 0; i < nblack; i++) {
        do {
            x = random_upto(rs,rw);
            y = random_upto(rs,rh);
        } while (GRID(state,flags,x,y) & F_BLACK);
        GRID(state, flags, x, y) |= F_BLACK;
    }

    /* Copy required region. */
    if (params->symm == SYMM_NONE) return;

    for (x = 0; x < rw; x++) {
        for (y = 0; y < rh; y++) {
            if (degree == 4) {
                xs[0] = x;
                ys[0] = y;
                xs[1] = state->w - 1 - (rotate ? y : x);
                ys[1] = rotate ? x : y;
                xs[2] = rotate ? (state->w - 1 - x) : x;
                ys[2] = state->h - 1 - y;
                xs[3] = rotate ? y : (state->w - 1 - x);
                ys[3] = state->h - 1 - (rotate ? x : y);
            } else {
                xs[0] = x;
                ys[0] = y;
                xs[1] = rotate ? (state->w - 1 - x) : x;
                ys[1] = state->h - 1 - y;
            }
            for (i = 1; i < degree; i++) {
                GRID(state, flags, xs[i], ys[i]) =
                    GRID(state, flags, xs[0], ys[0]);
            }
        }
    }
    /* SYMM_ROT4 misses the middle square above; fix that here. */
    if (degree == 4 && rotate && wodd &&
        (random_upto(rs,100) <= (unsigned int)params->blackpc))
        GRID(state,flags,
             state->w/2 + wodd - 1, state->h/2 + hodd - 1) |= F_BLACK;

#ifdef SOLVER_DIAGNOSTICS
    if (verbose) debug_state(state);
#endif
}

/* Fills in (does not allocate) a ll_data with all the tiles that would
 * be illuminated by a light at point (ox,oy). If origin is true then the
 * origin is included in this list. */
static void list_lights(game_state *state, int ox, int oy, bool origin,
                        ll_data *lld)
{
    int x,y;

    lld->ox = lld->minx = lld->maxx = ox;
    lld->oy = lld->miny = lld->maxy = oy;
    lld->include_origin = origin;

    y = oy;
    for (x = ox-1; x >= 0; x--) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (x < lld->minx) lld->minx = x;
    }
    for (x = ox+1; x < state->w; x++) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (x > lld->maxx) lld->maxx = x;
    }

    x = ox;
    for (y = oy-1; y >= 0; y--) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (y < lld->miny) lld->miny = y;
    }
    for (y = oy+1; y < state->h; y++) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (y > lld->maxy) lld->maxy = y;
    }
}

/* Makes sure a light is the given state, editing the lights table to suit the
 * new state if necessary. */
static void set_light(game_state *state, int ox, int oy, bool on)
{
    ll_data lld;
    int diff = 0;

    assert(!(GRID(state,flags,ox,oy) & F_BLACK));

    if (!on && GRID(state,flags,ox,oy) & F_LIGHT) {
        diff = -1;
        GRID(state,flags,ox,oy) &= ~F_LIGHT;
        state->nlights--;
    } else if (on && !(GRID(state,flags,ox,oy) & F_LIGHT)) {
        diff = 1;
        GRID(state,flags,ox,oy) |= F_LIGHT;
        state->nlights++;
    }

    if (diff != 0) {
        list_lights(state,ox,oy,true,&lld);
        FOREACHLIT(&lld, GRID(state,lights,lx,ly) += diff; );
    }
}

/* Returns 1 if removing a light at (x,y) would cause a square to go dark. */
static int check_dark(game_state *state, int x, int y)
{
    ll_data lld;

    list_lights(state, x, y, true, &lld);
    FOREACHLIT(&lld, if (GRID(state,lights,lx,ly) == 1) { return 1; } );
    return 0;
}

/* Sets up an initial random correct position (i.e. every
 * space lit, and no lights lit by other lights) by filling the
 * grid with lights and then removing lights one by one at random. */
static void place_lights(game_state *state, random_state *rs)
{
    int i, x, y, n, *numindices, wh = state->w*state->h;
    ll_data lld;

    numindices = snewn(wh, int);
    for (i = 0; i < wh; i++) numindices[i] = i;
    shuffle(numindices, wh, sizeof(*numindices), rs);

    /* Place a light on all grid squares without lights. */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            GRID(state, flags, x, y) &= ~F_MARK; /* we use this later. */
            if (GRID(state, flags, x, y) & F_BLACK) continue;
            set_light(state, x, y, true);
        }
    }

    for (i = 0; i < wh; i++) {
        y = numindices[i] / state->w;
        x = numindices[i] % state->w;
        if (!(GRID(state, flags, x, y) & F_LIGHT)) continue;
        if (GRID(state, flags, x, y) & F_MARK) continue;
        list_lights(state, x, y, false, &lld);

        /* If we're not lighting any lights ourself, don't remove anything. */
        n = 0;
        FOREACHLIT(&lld, if (GRID(state,flags,lx,ly) & F_LIGHT) { n += 1; } );
        if (n == 0) continue; /* [1] */

        /* Check whether removing lights we're lighting would cause anything
         * to go dark. */
        n = 0;
        FOREACHLIT(&lld, if (GRID(state,flags,lx,ly) & F_LIGHT) { n += check_dark(state,lx,ly); } );
        if (n == 0) {
            /* No, it wouldn't, so we can remove them all. */
            FOREACHLIT(&lld, set_light(state,lx,ly, false); );
            GRID(state,flags,x,y) |= F_MARK;
        }

        if (!grid_overlap(state)) {
            sfree(numindices);
            return; /* we're done. */
        }
        assert(grid_lit(state));
    }
    /* could get here if the line at [1] continue'd out of the loop. */
    if (grid_overlap(state)) {
        debug_state(state);
        assert(!"place_lights failed to resolve overlapping lights!");
    }
    sfree(numindices);
}

/* Fills in all black squares with numbers of adjacent lights. */
static void place_numbers(game_state *state)
{
    int x, y, i, n;
    surrounds s;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state,flags,x,y) & F_BLACK)) continue;
            get_surrounds(state, x, y, &s);
            n = 0;
            for (i = 0; i < s.npoints; i++) {
                if (GRID(state,flags,s.points[i].x, s.points[i].y) & F_LIGHT)
                    n++;
            }
            GRID(state,flags,x,y) |= F_NUMBERED;
            GRID(state,lights,x,y) = n;
        }
    }
}

/* --- Actual solver, with helper subroutines. --- */

static void tsl_callback(game_state *state,
                         int lx, int ly, int *x, int *y, int *n)
{
    if (GRID(state,flags,lx,ly) & F_IMPOSSIBLE) return;
    if (GRID(state,lights,lx,ly) > 0) return;
    *x = lx; *y = ly; (*n)++;
}

static bool try_solve_light(game_state *state, int ox, int oy,
                            unsigned int flags, int lights)
{
    ll_data lld;
    int sx = 0, sy = 0, n = 0;

    if (lights > 0) return false;
    if (flags & F_BLACK) return false;

    /* We have an unlit square; count how many ways there are left to
     * place a light that lights us (including this square); if only
     * one, we must put a light there. Squares that could light us
     * are, of course, the same as the squares we would light... */
    list_lights(state, ox, oy, true, &lld);
    FOREACHLIT(&lld, { tsl_callback(state, lx, ly, &sx, &sy, &n); });
    if (n == 1) {
        set_light(state, sx, sy, true);
#ifdef SOLVER_DIAGNOSTICS
        debug(("(%d,%d) can only be lit from (%d,%d); setting to LIGHT\n",
                ox,oy,sx,sy));
        if (verbose) debug_state(state);
#endif
        return true;
    }

    return false;
}

static bool could_place_light(unsigned int flags, int lights)
{
    if (flags & (F_BLACK | F_IMPOSSIBLE)) return false;
    return !(lights > 0);
}

static bool could_place_light_xy(game_state *state, int x, int y)
{
    int lights = GRID(state,lights,x,y);
    unsigned int flags = GRID(state,flags,x,y);
    return could_place_light(flags, lights);
}

/* For a given number square, determine whether we have enough info
 * to unambiguously place its lights. */
static bool try_solve_number(game_state *state, int nx, int ny,
                             unsigned int nflags, int nlights)
{
    surrounds s;
    int x, y, nl, ns, i, lights;
    bool ret = false;
    unsigned int flags;

    if (!(nflags & F_NUMBERED)) return false;
    nl = nlights;
    get_surrounds(state,nx,ny,&s);
    ns = s.npoints;

    /* nl is no. of lights we need to place, ns is no. of spaces we
     * have to place them in. Try and narrow these down, and mark
     * points we can ignore later. */
    for (i = 0; i < s.npoints; i++) {
        x = s.points[i].x; y = s.points[i].y;
        flags = GRID(state,flags,x,y);
        lights = GRID(state,lights,x,y);
        if (flags & F_LIGHT) {
            /* light here already; one less light for one less place. */
            nl--; ns--;
            s.points[i].f |= F_MARK;
        } else if (!could_place_light(flags, lights)) {
            ns--;
            s.points[i].f |= F_MARK;
        }
    }
    if (ns == 0) return false; /* nowhere to put anything. */
    if (nl == 0) {
        /* we have placed all lights we need to around here; all remaining
         * surrounds are therefore IMPOSSIBLE. */
        GRID(state,flags,nx,ny) |= F_NUMBERUSED;
        for (i = 0; i < s.npoints; i++) {
            if (!(s.points[i].f & F_MARK)) {
                GRID(state,flags,s.points[i].x,s.points[i].y) |= F_IMPOSSIBLE;
                ret = true;
            }
        }
#ifdef SOLVER_DIAGNOSTICS
        printf("Clue at (%d,%d) full; setting unlit to IMPOSSIBLE.\n",
               nx,ny);
        if (verbose) debug_state(state);
#endif
    } else if (nl == ns) {
        /* we have as many lights to place as spaces; fill them all. */
        GRID(state,flags,nx,ny) |= F_NUMBERUSED;
        for (i = 0; i < s.npoints; i++) {
            if (!(s.points[i].f & F_MARK)) {
                set_light(state, s.points[i].x,s.points[i].y, true);
                ret = true;
            }
        }
#ifdef SOLVER_DIAGNOSTICS
        printf("Clue at (%d,%d) trivial; setting unlit to LIGHT.\n",
               nx,ny);
        if (verbose) debug_state(state);
#endif
    }
    return ret;
}

struct setscratch {
    int x, y;
    int n;
};

#define SCRATCHSZ (state->w+state->h)

/* New solver algorithm: overlapping sets can add IMPOSSIBLE flags.
 * Algorithm thanks to Simon:
 *
 * (a) Any square where you can place a light has a set of squares
 *     which would become non-lights as a result. (This includes
 *     squares lit by the first square, and can also include squares
 *     adjacent to the same clue square if the new light is the last
 *     one around that clue.) Call this MAKESDARK(x,y) with (x,y) being
 *     the square you place a light.

 * (b) Any unlit square has a set of squares on which you could place
 *     a light to illuminate it. (Possibly including itself, of
 *     course.) This set of squares has the property that _at least
 *     one_ of them must contain a light. Sets of this type also arise
 *     from clue squares. Call this MAKESLIGHT(x,y), again with (x,y)
 *     the square you would place a light.

 * (c) If there exists (dx,dy) and (lx,ly) such that MAKESDARK(dx,dy) is
 *     a superset of MAKESLIGHT(lx,ly), this implies that placing a light at
 *     (dx,dy) would either leave no remaining way to illuminate a certain
 *     square, or would leave no remaining way to fulfill a certain clue
 *     (at lx,ly). In either case, a light can be ruled out at that position.
 *
 * So, we construct all possible MAKESLIGHT sets, both from unlit squares
 * and clue squares, and then we look for plausible MAKESDARK sets that include
 * our (lx,ly) to see if we can find a (dx,dy) to rule out. By the time we have
 * constructed the MAKESLIGHT set we don't care about (lx,ly), just the set
 * members.
 *
 * Once we have such a set, Simon came up with a Cunning Plan to find
 * the most sensible MAKESDARK candidate:
 *
 * (a) for each square S in your set X, find all the squares which _would_
 *     rule it out. That means any square which would light S, plus
 *     any square adjacent to the same clue square as S (provided
 *     that clue square has only one remaining light to be placed).
 *     It's not hard to make this list. Don't do anything with this
 *     data at the moment except _count_ the squares.

 * (b) Find the square S_min in the original set which has the
 *     _smallest_ number of other squares which would rule it out.

 * (c) Find all the squares that rule out S_min (it's probably
 *     better to recompute this than to have stored it during step
 *     (a), since the CPU requirement is modest but the storage
 *     cost would get ugly.) For each of these squares, see if it
 *     rules out everything else in the set X. Any which does can
 *     be marked as not-a-light.
 *
 */

typedef void (*trl_cb)(game_state *state, int dx, int dy,
                       struct setscratch *scratch, int n, void *ctx);

static void try_rule_out(game_state *state, int x, int y,
                         struct setscratch *scratch, int n,
                         trl_cb cb, void *ctx);

static void trl_callback_search(game_state *state, int dx, int dy,
                       struct setscratch *scratch, int n, void *ignored)
{
    int i;

#ifdef SOLVER_DIAGNOSTICS
    if (verbose) debug(("discount cb: light at (%d,%d)\n", dx, dy));
#endif

    for (i = 0; i < n; i++) {
        if (dx == scratch[i].x && dy == scratch[i].y) {
            scratch[i].n = 1;
            return;
        }
    }
}

static void trl_callback_discount(game_state *state, int dx, int dy,
                       struct setscratch *scratch, int n, void *ctx)
{
    bool *didsth = (bool *)ctx;
    int i;

    if (GRID(state,flags,dx,dy) & F_IMPOSSIBLE) {
#ifdef SOLVER_DIAGNOSTICS
        debug(("Square at (%d,%d) already impossible.\n", dx,dy));
#endif
        return;
    }

    /* Check whether a light at (dx,dy) rules out everything
     * in scratch, and mark (dx,dy) as IMPOSSIBLE if it does.
     * We can use try_rule_out for this as well, as the set of
     * squares which would rule out (x,y) is the same as the
     * set of squares which (x,y) would rule out. */

#ifdef SOLVER_DIAGNOSTICS
    if (verbose) debug(("Checking whether light at (%d,%d) rules out everything in scratch.\n", dx, dy));
#endif

    for (i = 0; i < n; i++)
        scratch[i].n = 0;
    try_rule_out(state, dx, dy, scratch, n, trl_callback_search, NULL);
    for (i = 0; i < n; i++) {
        if (scratch[i].n == 0) return;
    }
    /* The light ruled out everything in scratch. Yay. */
    GRID(state,flags,dx,dy) |= F_IMPOSSIBLE;
#ifdef SOLVER_DIAGNOSTICS
    debug(("Set reduction discounted square at (%d,%d):\n", dx,dy));
    if (verbose) debug_state(state);
#endif

    *didsth = true;
}

static void trl_callback_incn(game_state *state, int dx, int dy,
                       struct setscratch *scratch, int n, void *ctx)
{
    struct setscratch *s = (struct setscratch *)ctx;
    s->n++;
}

static void try_rule_out(game_state *state, int x, int y,
                         struct setscratch *scratch, int n,
                         trl_cb cb, void *ctx)
{
    /* XXX Find all the squares which would rule out (x,y); anything
     * that would light it as well as squares adjacent to same clues
     * as X assuming that clue only has one remaining light.
     * Call the callback with each square. */
    ll_data lld;
    surrounds s, ss;
    int i, j, curr_lights, tot_lights;

    /* Find all squares that would rule out a light at (x,y) and call trl_cb
     * with them: anything that would light (x,y)... */

    list_lights(state, x, y, false, &lld);
    FOREACHLIT(&lld, { if (could_place_light_xy(state, lx, ly)) { cb(state, lx, ly, scratch, n, ctx); } });

    /* ... as well as any empty space (that isn't x,y) next to any clue square
     * next to (x,y) that only has one light left to place. */

    get_surrounds(state, x, y, &s);
    for (i = 0; i < s.npoints; i++) {
        if (!(GRID(state,flags,s.points[i].x,s.points[i].y) & F_NUMBERED))
            continue;
        /* we have an adjacent clue square; find /its/ surrounds
         * and count the remaining lights it needs. */
        get_surrounds(state,s.points[i].x,s.points[i].y,&ss);
        curr_lights = 0;
        for (j = 0; j < ss.npoints; j++) {
            if (GRID(state,flags,ss.points[j].x,ss.points[j].y) & F_LIGHT)
                curr_lights++;
        }
        tot_lights = GRID(state, lights, s.points[i].x, s.points[i].y);
        /* We have a clue with tot_lights to fill, and curr_lights currently
         * around it. If adding a light at (x,y) fills up the clue (i.e.
         * curr_lights + 1 = tot_lights) then we need to discount all other
         * unlit squares around the clue. */
        if ((curr_lights + 1) == tot_lights) {
            for (j = 0; j < ss.npoints; j++) {
                int lx = ss.points[j].x, ly = ss.points[j].y;
                if (lx == x && ly == y) continue;
                if (could_place_light_xy(state, lx, ly))
                    cb(state, lx, ly, scratch, n, ctx);
            }
        }
    }
}

#ifdef SOLVER_DIAGNOSTICS
static void debug_scratch(const char *msg, struct setscratch *scratch, int n)
{
    int i;
    debug(("%s scratch (%d elements):\n", msg, n));
    for (i = 0; i < n; i++) {
        debug(("  (%d,%d) n%d\n", scratch[i].x, scratch[i].y, scratch[i].n));
    }
}
#endif

static bool discount_set(game_state *state,
                         struct setscratch *scratch, int n)
{
    int i, besti, bestn;
    bool didsth = false;

#ifdef SOLVER_DIAGNOSTICS
    if (verbose > 1) debug_scratch("discount_set", scratch, n);
#endif
    if (n == 0) return false;

    for (i = 0; i < n; i++) {
        try_rule_out(state, scratch[i].x, scratch[i].y, scratch, n,
                     trl_callback_incn, (void*)&(scratch[i]));
    }
#ifdef SOLVER_DIAGNOSTICS
    if (verbose > 1) debug_scratch("discount_set after count", scratch, n);
#endif

    besti = -1; bestn = SCRATCHSZ;
    for (i = 0; i < n; i++) {
        if (scratch[i].n < bestn) {
            bestn = scratch[i].n;
            besti = i;
        }
    }
#ifdef SOLVER_DIAGNOSTICS
    if (verbose > 1) debug(("best square (%d,%d) with n%d.\n",
           scratch[besti].x, scratch[besti].y, scratch[besti].n));
#endif
    try_rule_out(state, scratch[besti].x, scratch[besti].y, scratch, n,
                 trl_callback_discount, (void*)&didsth);
#ifdef SOLVER_DIAGNOSTICS
    if (didsth) debug((" [from square (%d,%d)]\n",
                       scratch[besti].x, scratch[besti].y));
#endif

    return didsth;
}

static void discount_clear(game_state *state, struct setscratch *scratch, int *n)
{
    *n = 0;
    memset(scratch, 0, SCRATCHSZ * sizeof(struct setscratch));
}

static void unlit_cb(game_state *state, int lx, int ly,
                     struct setscratch *scratch, int *n)
{
    if (could_place_light_xy(state, lx, ly)) {
        scratch[*n].x = lx; scratch[*n].y = ly; (*n)++;
    }
}

/* Construct a MAKESLIGHT set from an unlit square. */
static bool discount_unlit(game_state *state, int x, int y,
                           struct setscratch *scratch)
{
    ll_data lld;
    int n;
    bool didsth;

#ifdef SOLVER_DIAGNOSTICS
    if (verbose) debug(("Trying to discount for unlit square at (%d,%d).\n", x, y));
    if (verbose > 1) debug_state(state);
#endif

    discount_clear(state, scratch, &n);

    list_lights(state, x, y, true, &lld);
    FOREACHLIT(&lld, { unlit_cb(state, lx, ly, scratch, &n); });
    didsth = discount_set(state, scratch, n);
#ifdef SOLVER_DIAGNOSTICS
    if (didsth) debug(("  [from unlit square at (%d,%d)].\n", x, y));
#endif
    return didsth;

}

/* Construct a series of MAKESLIGHT sets from a clue square.
 *  for a clue square with N remaining spaces that must contain M lights, every
 *  subset of size N-M+1 of those N spaces forms such a set.
 */

static bool discount_clue(game_state *state, int x, int y,
                          struct setscratch *scratch)
{
    int slen, m = GRID(state, lights, x, y), n, i, lights;
    bool didsth = false;
    unsigned int flags;
    surrounds s, sempty;
    combi_ctx *combi;

    if (m == 0) return false;

#ifdef SOLVER_DIAGNOSTICS
    if (verbose) debug(("Trying to discount for sets at clue (%d,%d).\n", x, y));
    if (verbose > 1) debug_state(state);
#endif

    /* m is no. of lights still to place; starts off at the clue value
     * and decreases when we find a light already down.
     * n is no. of spaces left; starts off at 0 and goes up when we find
     * a plausible space. */

    get_surrounds(state, x, y, &s);
    memset(&sempty, 0, sizeof(surrounds));
    for (i = 0; i < s.npoints; i++) {
        int lx = s.points[i].x, ly = s.points[i].y;
        flags = GRID(state,flags,lx,ly);
        lights = GRID(state,lights,lx,ly);

        if (flags & F_LIGHT) m--;

        if (could_place_light(flags, lights)) {
            sempty.points[sempty.npoints].x = lx;
            sempty.points[sempty.npoints].y = ly;
            sempty.npoints++;
        }
    }
    n = sempty.npoints; /* sempty is now a surrounds of only blank squares. */
    if (n == 0) return false; /* clue is full already. */

    if (m < 0 || m > n) return false; /* become impossible. */

    combi = new_combi(n - m + 1, n);
    while (next_combi(combi)) {
        discount_clear(state, scratch, &slen);
        for (i = 0; i < combi->r; i++) {
            scratch[slen].x = sempty.points[combi->a[i]].x;
            scratch[slen].y = sempty.points[combi->a[i]].y;
            slen++;
        }
        if (discount_set(state, scratch, slen)) didsth = true;
    }
    free_combi(combi);
#ifdef SOLVER_DIAGNOSTICS
    if (didsth) debug(("  [from clue at (%d,%d)].\n", x, y));
#endif
    return didsth;
}

#define F_SOLVE_FORCEUNIQUE     1
#define F_SOLVE_DISCOUNTSETS    2
#define F_SOLVE_ALLOWRECURSE    4

static unsigned int flags_from_difficulty(int difficulty)
{
    unsigned int sflags = F_SOLVE_FORCEUNIQUE;
    assert(difficulty <= DIFFCOUNT);
    if (difficulty >= 1) sflags |= F_SOLVE_DISCOUNTSETS;
    if (difficulty >= 2) sflags |= F_SOLVE_ALLOWRECURSE;
    return sflags;
}

#define MAXRECURSE 5

static int solve_sub(game_state *state,
                     unsigned int solve_flags, int depth,
                     int *maxdepth)
{
    unsigned int flags;
    int x, y, ncanplace, lights;
    bool didstuff;
    int bestx, besty, n, bestn, copy_soluble, self_soluble, ret, maxrecurse = 0;
    game_state *scopy;
    ll_data lld;
    struct setscratch *sscratch = NULL;

#ifdef SOLVER_DIAGNOSTICS
    printf("solve_sub: depth = %d\n", depth);
#endif
    if (maxdepth && *maxdepth < depth) *maxdepth = depth;
    if (solve_flags & F_SOLVE_ALLOWRECURSE) maxrecurse = MAXRECURSE;

    while (1) {
        if (grid_overlap(state)) {
            /* Our own solver, from scratch, should never cause this to happen
             * (assuming a soluble grid). However, if we're trying to solve
             * from a half-completed *incorrect* grid this might occur; we
             * just return the 'no solutions' code in this case. */
            ret = 0; goto done;
        }

        if (grid_correct(state)) { ret = 1; goto done; }

        ncanplace = 0;
        didstuff = false;
        /* These 2 loops, and the functions they call, are the critical loops
         * for timing; any optimisations should look here first. */
        for (x = 0; x < state->w; x++) {
            for (y = 0; y < state->h; y++) {
                flags = GRID(state,flags,x,y);
                lights = GRID(state,lights,x,y);
                ncanplace += could_place_light(flags, lights);

                if (try_solve_light(state, x, y, flags, lights))
                    didstuff = true;
                if (try_solve_number(state, x, y, flags, lights))
                    didstuff = true;
            }
        }
        if (didstuff) continue;
        if (!ncanplace) {
            /* nowhere to put a light, puzzle is unsoluble. */
            ret = 0; goto done;
        }

        if (solve_flags & F_SOLVE_DISCOUNTSETS) {
            if (!sscratch) sscratch = snewn(SCRATCHSZ, struct setscratch);
            /* Try a more cunning (and more involved) way... more details above. */
            for (x = 0; x < state->w; x++) {
                for (y = 0; y < state->h; y++) {
                    flags = GRID(state,flags,x,y);
                    lights = GRID(state,lights,x,y);

                    if (!(flags & F_BLACK) && lights == 0) {
                        if (discount_unlit(state, x, y, sscratch)) {
                            didstuff = true;
                            goto reduction_success;
                        }
                    } else if (flags & F_NUMBERED) {
                        if (discount_clue(state, x, y, sscratch)) {
                            didstuff = true;
                            goto reduction_success;
                        }
                    }
                }
            }
        }
reduction_success:
        if (didstuff) continue;

        /* We now have to make a guess; we have places to put lights but
         * no definite idea about where they can go. */
        if (depth >= maxrecurse) {
            /* mustn't delve any deeper. */
            ret = -1; goto done;
        }
        /* Of all the squares that we could place a light, pick the one
         * that would light the most currently unlit squares. */
        /* This heuristic was just plucked from the air; there may well be
         * a more efficient way of choosing a square to flip to minimise
         * recursion. */
        bestn = 0;
        bestx = besty = -1; /* suyb */
        for (x = 0; x < state->w; x++) {
            for (y = 0; y < state->h; y++) {
                flags = GRID(state,flags,x,y);
                lights = GRID(state,lights,x,y);
                if (!could_place_light(flags, lights)) continue;

                n = 0;
                list_lights(state, x, y, true, &lld);
                FOREACHLIT(&lld, { if (GRID(state,lights,lx,ly) == 0) n++; });
                if (n > bestn) {
                    bestn = n; bestx = x; besty = y;
                }
            }
        }
        assert(bestn > 0);
	assert(bestx >= 0 && besty >= 0);

        /* Now we've chosen a plausible (x,y), try to solve it once as 'lit'
         * and once as 'impossible'; we need to make one copy to do this. */

        scopy = dup_game(state);
#ifdef SOLVER_DIAGNOSTICS
        debug(("Recursing #1: trying (%d,%d) as IMPOSSIBLE\n", bestx, besty));
#endif
        GRID(state,flags,bestx,besty) |= F_IMPOSSIBLE;
        self_soluble = solve_sub(state, solve_flags,  depth+1, maxdepth);

        if (!(solve_flags & F_SOLVE_FORCEUNIQUE) && self_soluble > 0) {
            /* we didn't care about finding all solutions, and we just
             * found one; return with it immediately. */
            free_game(scopy);
            ret = self_soluble;
            goto done;
        }

#ifdef SOLVER_DIAGNOSTICS
        debug(("Recursing #2: trying (%d,%d) as LIGHT\n", bestx, besty));
#endif
        set_light(scopy, bestx, besty, true);
        copy_soluble = solve_sub(scopy, solve_flags, depth+1, maxdepth);

        /* If we wanted a unique solution but we hit our recursion limit
         * (on either branch) then we have to assume we didn't find possible
         * extra solutions, and return 'not soluble'. */
        if ((solve_flags & F_SOLVE_FORCEUNIQUE) &&
            ((copy_soluble < 0) || (self_soluble < 0))) {
            ret = -1;
        /* Make sure that whether or not it was self or copy (or both) that
         * were soluble, that we return a solved state in self. */
        } else if (copy_soluble <= 0) {
            /* copy wasn't soluble; keep self state and return that result. */
            ret = self_soluble;
        } else if (self_soluble <= 0) {
            /* copy solved and we didn't, so copy in copy's (now solved)
             * flags and light state. */
            memcpy(state->lights, scopy->lights,
                   scopy->w * scopy->h * sizeof(int));
            memcpy(state->flags, scopy->flags,
                   scopy->w * scopy->h * sizeof(unsigned int));
            ret = copy_soluble;
        } else {
            ret = copy_soluble + self_soluble;
        }
        free_game(scopy);
        goto done;
    }
done:
    if (sscratch) sfree(sscratch);
#ifdef SOLVER_DIAGNOSTICS
    if (ret < 0)
        debug(("solve_sub: depth = %d returning, ran out of recursion.\n",
               depth));
    else
        debug(("solve_sub: depth = %d returning, %d solutions.\n",
               depth, ret));
#endif
    return ret;
}

/* Fills in the (possibly partially-complete) game_state as far as it can,
 * returning the number of possible solutions. If it returns >0 then the
 * game_state will be in a solved state, but you won't know which one. */
static int dosolve(game_state *state, int solve_flags, int *maxdepth)
{
    int x, y, nsol;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            GRID(state,flags,x,y) &= ~F_NUMBERUSED;
        }
    }
    nsol = solve_sub(state, solve_flags, 0, maxdepth);
    return nsol;
}

static int strip_unused_nums(game_state *state)
{
    int x,y,n=0;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if ((GRID(state,flags,x,y) & F_NUMBERED) &&
                !(GRID(state,flags,x,y) & F_NUMBERUSED)) {
                GRID(state,flags,x,y) &= ~F_NUMBERED;
                GRID(state,lights,x,y) = 0;
                n++;
            }
        }
    }
    debug(("Stripped %d unused numbers.\n", n));
    return n;
}

static void unplace_lights(game_state *state)
{
    int x,y;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (GRID(state,flags,x,y) & F_LIGHT)
                set_light(state,x,y,false);
            GRID(state,flags,x,y) &= ~F_IMPOSSIBLE;
            GRID(state,flags,x,y) &= ~F_NUMBERUSED;
        }
    }
}

static bool puzzle_is_good(game_state *state, int difficulty)
{
    int nsol, mdepth = 0;
    unsigned int sflags = flags_from_difficulty(difficulty);

    unplace_lights(state);

#ifdef SOLVER_DIAGNOSTICS
    debug(("Trying to solve with difficulty %d (0x%x):\n",
           difficulty, sflags));
    if (verbose) debug_state(state);
#endif

    nsol = dosolve(state, sflags, &mdepth);
    /* if we wanted an easy puzzle, make sure we didn't need recursion. */
    if (!(sflags & F_SOLVE_ALLOWRECURSE) && mdepth > 0) {
        debug(("Ignoring recursive puzzle.\n"));
        return false;
    }

    debug(("%d solutions found.\n", nsol));
    if (nsol <= 0) return false;
    if (nsol > 1) return false;
    return true;
}

/* --- New game creation and user input code. --- */

/* The basic algorithm here is to generate the most complex grid possible
 * while honouring two restrictions:
 *
 *  * we require a unique solution, and
 *  * either we require solubility with no recursion (!params->recurse)
 *  * or we require some recursion. (params->recurse).
 *
 * The solver helpfully keeps track of the numbers it needed to use to
 * get its solution, so we use that to remove an initial set of numbers
 * and check we still satsify our requirements (on uniqueness and
 * non-recursiveness, if applicable; we don't check explicit recursiveness
 * until the end).
 *
 * Then we try to remove all numbers in a random order, and see if we
 * still satisfy requirements (putting them back if we didn't).
 *
 * Removing numbers will always, in general terms, make a puzzle require
 * more recursion but it may also mean a puzzle becomes non-unique.
 *
 * Once we're done, if we wanted a recursive puzzle but the most difficult
 * puzzle we could come up with was non-recursive, we give up and try a new
 * grid. */

#define MAX_GRIDGEN_TRIES 20

static char *new_game_desc(const game_params *params_in, random_state *rs,
			   char **aux, bool interactive)
{
    game_params params_copy = *params_in; /* structure copy */
    game_params *params = &params_copy;
    game_state *news = new_state(params), *copys;
    int i, j, run, x, y, wh = params->w*params->h, num;
    char *ret, *p;
    int *numindices;

    /* Construct a shuffled list of grid positions; we only
     * do this once, because if it gets used more than once it'll
     * be on a different grid layout. */
    numindices = snewn(wh, int);
    for (j = 0; j < wh; j++) numindices[j] = j;
    shuffle(numindices, wh, sizeof(*numindices), rs);

    while (1) {
        for (i = 0; i < MAX_GRIDGEN_TRIES; i++) {
            set_blacks(news, params, rs); /* also cleans board. */

            /* set up lights and then the numbers, and remove the lights */
            place_lights(news, rs);
            debug(("Generating initial grid.\n"));
            place_numbers(news);
            if (!puzzle_is_good(news, params->difficulty)) continue;

            /* Take a copy, remove numbers we didn't use and check there's
             * still a unique solution; if so, use the copy subsequently. */
            copys = dup_game(news);
            strip_unused_nums(copys);
            if (!puzzle_is_good(copys, params->difficulty)) {
                debug(("Stripped grid is not good, reverting.\n"));
                free_game(copys);
            } else {
                free_game(news);
                news = copys;
            }

            /* Go through grid removing numbers at random one-by-one and
             * trying to solve again; if it ceases to be good put the number back. */
            for (j = 0; j < wh; j++) {
                y = numindices[j] / params->w;
                x = numindices[j] % params->w;
                if (!(GRID(news, flags, x, y) & F_NUMBERED)) continue;
                num = GRID(news, lights, x, y);
                GRID(news, lights, x, y) = 0;
                GRID(news, flags, x, y) &= ~F_NUMBERED;
                if (!puzzle_is_good(news, params->difficulty)) {
                    GRID(news, lights, x, y) = num;
                    GRID(news, flags, x, y) |= F_NUMBERED;
                } else
                    debug(("Removed (%d,%d) still soluble.\n", x, y));
            }
            if (params->difficulty > 0) {
                /* Was the maximally-difficult puzzle difficult enough?
                 * Check we can't solve it with a more simplistic solver. */
                if (puzzle_is_good(news, params->difficulty-1)) {
                    debug(("Maximally-hard puzzle still not hard enough, skipping.\n"));
                    continue;
                }
            }

            goto goodpuzzle;
        }
        /* Couldn't generate a good puzzle in however many goes. Ramp up the
         * %age of black squares (if we didn't already have lots; in which case
         * why couldn't we generate a puzzle?) and try again. */
        if (params->blackpc < 90) params->blackpc += 5;
        debug(("New black layout %d%%.\n", params->blackpc));
    }
goodpuzzle:
    /* Game is encoded as a long string one character per square;
     * 'S' is a space
     * 'B' is a black square with no number
     * '0', '1', '2', '3', '4' is a black square with a number. */
    ret = snewn((params->w * params->h) + 1, char);
    p = ret;
    run = 0;
    for (y = 0; y < params->h; y++) {
	for (x = 0; x < params->w; x++) {
            if (GRID(news,flags,x,y) & F_BLACK) {
		if (run) {
		    *p++ = ('a'-1) + run;
		    run = 0;
		}
                if (GRID(news,flags,x,y) & F_NUMBERED)
                    *p++ = '0' + GRID(news,lights,x,y);
                else
                    *p++ = 'B';
            } else {
		if (run == 26) {
		    *p++ = ('a'-1) + run;
		    run = 0;
		}
		run++;
	    }
        }
    }
    if (run) {
	*p++ = ('a'-1) + run;
	run = 0;
    }
    *p = '\0';
    assert(p - ret <= params->w * params->h);
    free_game(news);
    sfree(numindices);

    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int i;
    for (i = 0; i < params->w*params->h; i++) {
        if (*desc >= '0' && *desc <= '4')
            /* OK */;
        else if (*desc == 'B')
            /* OK */;
        else if (*desc >= 'a' && *desc <= 'z')
            i += *desc - 'a';	       /* and the i++ will add another one */
        else if (!*desc)
            return "Game description shorter than expected";
        else
            return "Game description contained unexpected character";
        desc++;
    }
    if (*desc || i > params->w*params->h)
        return "Game description longer than expected";

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *ret = new_state(params);
    int x,y;
    int run = 0;

    for (y = 0; y < params->h; y++) {
	for (x = 0; x < params->w; x++) {
            char c = '\0';

	    if (run == 0) {
		c = *desc++;
		assert(c != 'S');
		if (c >= 'a' && c <= 'z')
		    run = c - 'a' + 1;
	    }

	    if (run > 0) {
		c = 'S';
		run--;
	    }

            switch (c) {
	      case '0': case '1': case '2': case '3': case '4':
                GRID(ret,flags,x,y) |= F_NUMBERED;
                GRID(ret,lights,x,y) = (c - '0');
                /* run-on... */

	      case 'B':
                GRID(ret,flags,x,y) |= F_BLACK;
                break;

	      case 'S':
		/* empty square */
                break;

	      default:
		assert(!"Malformed desc.");
		break;
            }
        }
    }
    if (*desc) assert(!"Over-long desc.");

    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *solved;
    char *move = NULL, buf[80];
    int movelen, movesize, x, y, len;
    unsigned int oldflags, solvedflags, sflags;

    /* We don't care here about non-unique puzzles; if the
     * user entered one themself then I doubt they care. */

    sflags = F_SOLVE_ALLOWRECURSE | F_SOLVE_DISCOUNTSETS;

    /* Try and solve from where we are now (for non-unique
     * puzzles this may produce a different answer). */
    solved = dup_game(currstate);
    if (dosolve(solved, sflags, NULL) > 0) goto solved;
    free_game(solved);

    /* That didn't work; try solving from the clean puzzle. */
    solved = dup_game(state);
    if (dosolve(solved, sflags, NULL) > 0) goto solved;
    *error = "Unable to find a solution to this puzzle.";
    goto done;

solved:
    movesize = 256;
    move = snewn(movesize, char);
    movelen = 0;
    move[movelen++] = 'S';
    move[movelen] = '\0';
    for (x = 0; x < currstate->w; x++) {
        for (y = 0; y < currstate->h; y++) {
            len = 0;
            oldflags = GRID(currstate, flags, x, y);
            solvedflags = GRID(solved, flags, x, y);
            if ((oldflags & F_LIGHT) != (solvedflags & F_LIGHT))
                len = sprintf(buf, ";L%d,%d", x, y);
            else if ((oldflags & F_IMPOSSIBLE) != (solvedflags & F_IMPOSSIBLE))
                len = sprintf(buf, ";I%d,%d", x, y);
            if (len) {
                if (movelen + len >= movesize) {
                    movesize = movelen + len + 256;
                    move = sresize(move, movesize, char);
                }
                strcpy(move + movelen, buf);
                movelen += len;
            }
        }
    }

done:
    free_game(solved);
    return move;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

/* 'borrowed' from slant.c, mainly. I could have printed it one
 * character per cell (like debug_state) but that comes out tiny.
 * 'L' is used for 'light here' because 'O' looks too much like '0'
 * (black square with no surrounding lights). */
static char *game_text_format(const game_state *state)
{
    int w = state->w, h = state->h, W = w+1, H = h+1;
    int x, y, len, lights;
    unsigned int flags;
    char *ret, *p;

    len = (h+H) * (w+W+1) + 1;
    ret = snewn(len, char);
    p = ret;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            *p++ = '+';
            if (x < w)
                *p++ = '-';
        }
        *p++ = '\n';
        if (y < h) {
            for (x = 0; x < W; x++) {
                *p++ = '|';
                if (x < w) {
                    /* actual interesting bit. */
                    flags = GRID(state, flags, x, y);
                    lights = GRID(state, lights, x, y);
                    if (flags & F_BLACK) {
                        if (flags & F_NUMBERED)
                            *p++ = '0' + lights;
                        else
                            *p++ = '#';
                    } else {
                        if (flags & F_LIGHT)
                            *p++ = 'L';
                        else if (flags & F_IMPOSSIBLE)
                            *p++ = 'x';
                        else if (lights > 0)
                            *p++ = '.';
                        else
                            *p++ = ' ';
                    }
                }
            }
            *p++ = '\n';
        }
    }
    *p++ = '\0';

    assert(p - ret == len);
    return ret;
}

struct game_ui {
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    /* nothing to encode. */
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    /* nothing to decode. */
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    if (newstate->completed)
        ui->cur_visible = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int cx = ui->cur_x, cy = ui->cur_y;
    unsigned int flags = GRID(state, flags, cx, cy);

    if (!ui->cur_visible) return "";
    if (button == CURSOR_SELECT) {
        if (flags & (F_BLACK | F_IMPOSSIBLE)) return "";
        if (flags & F_LIGHT) return "Clear";
        return "Light";
    }
    if (button == CURSOR_SELECT2) {
        if (flags & (F_BLACK | F_LIGHT)) return "";
        if (flags & F_IMPOSSIBLE) return "Clear";
        return "Mark";
    }
    return "";
}

#define DF_BLACK        1       /* black square */
#define DF_NUMBERED     2       /* black square with number */
#define DF_LIT          4       /* display (white) square lit up */
#define DF_LIGHT        8       /* display light in square */
#define DF_OVERLAP      16      /* display light as overlapped */
#define DF_CURSOR       32      /* display cursor */
#define DF_NUMBERWRONG  64      /* display black numbered square as error. */
#define DF_FLASH        128     /* background flash is on. */
#define DF_IMPOSSIBLE   256     /* display non-light little square */

struct game_drawstate {
    int tilesize, crad;
    int w, h;
    unsigned int *flags;         /* width * height */
    bool started;
};


/* Believe it or not, this empty = "" hack is needed to get around a bug in
 * the prc-tools gcc when optimisation is turned on; before, it produced:
    lightup-sect.c: In function `interpret_move':
    lightup-sect.c:1416: internal error--unrecognizable insn:
    (insn 582 580 583 (set (reg:SI 134)
            (pc)) -1 (nil)
        (nil))
 */
static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    enum { NONE, FLIP_LIGHT, FLIP_IMPOSSIBLE } action = NONE;
    int cx = -1, cy = -1;
    unsigned int flags;
    char buf[80], *nullret = UI_UPDATE, *empty = UI_UPDATE, c;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        if (ui->cur_visible)
            nullret = empty;
        ui->cur_visible = false;
        cx = FROMCOORD(x);
        cy = FROMCOORD(y);
        action = (button == LEFT_BUTTON) ? FLIP_LIGHT : FLIP_IMPOSSIBLE;
    } else if (IS_CURSOR_SELECT(button) ||
               button == 'i' || button == 'I') {
        if (ui->cur_visible) {
            /* Only allow cursor-effect operations if the cursor is visible
             * (otherwise you have no idea which square it might be affecting) */
            cx = ui->cur_x;
            cy = ui->cur_y;
            action = (button == 'i' || button == 'I' || button == CURSOR_SELECT2) ?
                FLIP_IMPOSSIBLE : FLIP_LIGHT;
        }
        ui->cur_visible = true;
    } else if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cur_x, &ui->cur_y, state->w, state->h, false);
        ui->cur_visible = true;
        nullret = empty;
    } else
        return NULL;

    switch (action) {
    case FLIP_LIGHT:
    case FLIP_IMPOSSIBLE:
        if (cx < 0 || cy < 0 || cx >= state->w || cy >= state->h)
            return nullret;
        flags = GRID(state, flags, cx, cy);
        if (flags & F_BLACK)
            return nullret;
        if (action == FLIP_LIGHT) {
#ifdef STYLUS_BASED
            if (flags & F_IMPOSSIBLE || flags & F_LIGHT) c = 'I'; else c = 'L';
#else
            if (flags & F_IMPOSSIBLE) return nullret;
            c = 'L';
#endif
        } else {
#ifdef STYLUS_BASED
            if (flags & F_IMPOSSIBLE || flags & F_LIGHT) c = 'L'; else c = 'I';
#else
            if (flags & F_LIGHT) return nullret;
            c = 'I';
#endif
        }
        sprintf(buf, "%c%d,%d", (int)c, cx, cy);
        break;

    case NONE:
        return nullret;

    default:
        assert(!"Shouldn't get here!");
    }
    return dupstr(buf);
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    int x, y, n, flags;
    char c;

    if (!*move) goto badmove;

    while (*move) {
        c = *move;
        if (c == 'S') {
            ret->used_solve = true;
            move++;
        } else if (c == 'L' || c == 'I') {
            move++;
            if (sscanf(move, "%d,%d%n", &x, &y, &n) != 2 ||
                x < 0 || y < 0 || x >= ret->w || y >= ret->h)
                goto badmove;

            flags = GRID(ret, flags, x, y);
            if (flags & F_BLACK) goto badmove;

            /* LIGHT and IMPOSSIBLE are mutually exclusive. */
            if (c == 'L') {
                GRID(ret, flags, x, y) &= ~F_IMPOSSIBLE;
                set_light(ret, x, y, !(flags & F_LIGHT));
            } else {
                set_light(ret, x, y, false);
                GRID(ret, flags, x, y) ^= F_IMPOSSIBLE;
            }
            move += n;
        } else goto badmove;

        if (*move == ';')
            move++;
        else if (*move) goto badmove;
    }
    if (grid_correct(ret)) ret->completed = true;
    return ret;

badmove:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

/* XXX entirely cloned from fifteen.c; separate out? */
static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->crad = 3*(tilesize-1)/8;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    for (i = 0; i < 3; i++) {
        ret[COL_BLACK * 3 + i] = 0.0F;
        ret[COL_LIGHT * 3 + i] = 1.0F;
        ret[COL_CURSOR * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 2.0F;
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 1.5F;

    }

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.25F;
    ret[COL_ERROR * 3 + 2] = 0.25F;

    ret[COL_LIT * 3 + 0] = 1.0F;
    ret[COL_LIT * 3 + 1] = 1.0F;
    ret[COL_LIT * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = ds->crad = 0;
    ds->w = state->w; ds->h = state->h;

    ds->flags = snewn(ds->w*ds->h, unsigned int);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->flags[i] = -1;

    ds->started = false;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->flags);
    sfree(ds);
}

/* At some stage we should put these into a real options struct.
 * Note that tile_redraw has no #ifdeffery; it relies on tile_flags not
 * to put those flags in. */
#define HINT_LIGHTS
#define HINT_OVERLAPS
#define HINT_NUMBERS

static unsigned int tile_flags(game_drawstate *ds, const game_state *state,
                               const game_ui *ui, int x, int y, bool flashing)
{
    unsigned int flags = GRID(state, flags, x, y);
    int lights = GRID(state, lights, x, y);
    unsigned int ret = 0;

    if (flashing) ret |= DF_FLASH;
    if (ui && ui->cur_visible && x == ui->cur_x && y == ui->cur_y)
        ret |= DF_CURSOR;

    if (flags & F_BLACK) {
        ret |= DF_BLACK;
        if (flags & F_NUMBERED) {
#ifdef HINT_NUMBERS
            if (number_wrong(state, x, y))
		ret |= DF_NUMBERWRONG;
#endif
            ret |= DF_NUMBERED;
        }
    } else {
#ifdef HINT_LIGHTS
        if (lights > 0) ret |= DF_LIT;
#endif
        if (flags & F_LIGHT) {
            ret |= DF_LIGHT;
#ifdef HINT_OVERLAPS
            if (lights > 1) ret |= DF_OVERLAP;
#endif
        }
        if (flags & F_IMPOSSIBLE) ret |= DF_IMPOSSIBLE;
    }
    return ret;
}

static void tile_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *state, int x, int y)
{
    unsigned int ds_flags = GRID(ds, flags, x, y);
    int dx = COORD(x), dy = COORD(y);
    int lit = (ds_flags & DF_FLASH) ? COL_GRID : COL_LIT;

    if (ds_flags & DF_BLACK) {
        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_BLACK);
        if (ds_flags & DF_NUMBERED) {
            int ccol = (ds_flags & DF_NUMBERWRONG) ? COL_ERROR : COL_LIGHT;
            char str[32];

            /* We know that this won't change over the course of the game
             * so it's OK to ignore this when calculating whether or not
             * to redraw the tile. */
            sprintf(str, "%d", GRID(state, lights, x, y));
            draw_text(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE*3/5,
		      ALIGN_VCENTRE | ALIGN_HCENTRE, ccol, str);
        }
    } else {
        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE,
                  (ds_flags & DF_LIT) ? lit : COL_BACKGROUND);
        draw_rect_outline(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_GRID);
        if (ds_flags & DF_LIGHT) {
            int lcol = (ds_flags & DF_OVERLAP) ? COL_ERROR : COL_LIGHT;
            draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2, TILE_RADIUS,
                        lcol, COL_BLACK);
        } else if ((ds_flags & DF_IMPOSSIBLE)) {
            static int draw_blobs_when_lit = -1;
            if (draw_blobs_when_lit < 0)
		draw_blobs_when_lit = getenv_bool("LIGHTUP_LIT_BLOBS", true);
            if (!(ds_flags & DF_LIT) || draw_blobs_when_lit) {
                int rlen = TILE_SIZE / 4;
                draw_rect(dr, dx + TILE_SIZE/2 - rlen/2,
                          dy + TILE_SIZE/2 - rlen/2,
                          rlen, rlen, COL_BLACK);
            }
        }
    }

    if (ds_flags & DF_CURSOR) {
        int coff = TILE_SIZE/8;
        draw_rect_outline(dr, dx + coff, dy + coff,
                          TILE_SIZE - coff*2, TILE_SIZE - coff*2, COL_CURSOR);
    }

    draw_update(dr, dx, dy, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    bool flashing = false;
    int x,y;

    if (flashtime) flashing = (int)(flashtime * 3 / FLASH_TIME) != 1;

    if (!ds->started) {
        draw_rect_outline(dr, COORD(0)-1, COORD(0)-1,
                          TILE_SIZE * ds->w + 2,
                          TILE_SIZE * ds->h + 2,
                          COL_GRID);

        draw_update(dr, 0, 0,
                    TILE_SIZE * ds->w + 2 * BORDER,
                    TILE_SIZE * ds->h + 2 * BORDER);
        ds->started = true;
    }

    for (x = 0; x < ds->w; x++) {
        for (y = 0; y < ds->h; y++) {
            unsigned int ds_flags = tile_flags(ds, state, ui, x, y, flashing);
            if (ds_flags != GRID(ds, flags, x, y)) {
                GRID(ds, flags, x, y) = ds_flags;
                tile_redraw(dr, ds, state, x, y);
            }
        }
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->used_solve && !newstate->used_solve)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        *x = COORD(ui->cur_x);
        *y = COORD(ui->cur_y);
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->w, h = state->h;
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Border.
     */
    print_line_width(dr, TILE_SIZE / 16);
    draw_rect_outline(dr, COORD(0), COORD(0),
		      TILE_SIZE * w, TILE_SIZE * h, ink);

    /*
     * Grid.
     */
    print_line_width(dr, TILE_SIZE / 24);
    for (x = 1; x < w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), ink);
    for (y = 1; y < h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), ink);

    /*
     * Grid contents.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
            unsigned int ds_flags = tile_flags(ds, state, NULL, x, y, false);
	    int dx = COORD(x), dy = COORD(y);
	    if (ds_flags & DF_BLACK) {
		draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, ink);
		if (ds_flags & DF_NUMBERED) {
		    char str[32];
		    sprintf(str, "%d", GRID(state, lights, x, y));
		    draw_text(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
			      FONT_VARIABLE, TILE_SIZE*3/5,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, paper, str);
		}
	    } else if (ds_flags & DF_LIGHT) {
		draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
			    TILE_RADIUS, -1, ink);
	    }
	}
}

#ifdef COMBINED
#define thegame lightup
#endif

const struct game thegame = {
    "Light Up", "games.lightup", "lightup",
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
    current_key_label,
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
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *result;
    const char *err;
    int nsol, diff, really_verbose = 0;
    unsigned int sflags;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            really_verbose++;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    /* Run the solvers easiest to hardest until we find one that
     * can solve our puzzle. If it's soluble we know that the
     * hardest (recursive) solver will always find the solution. */
    nsol = sflags = 0;
    for (diff = 0; diff <= DIFFCOUNT; diff++) {
        printf("\nSolving with difficulty %d.\n", diff);
        sflags = flags_from_difficulty(diff);
        unplace_lights(s);
        nsol = dosolve(s, sflags, NULL);
        if (nsol == 1) break;
    }

    printf("\n");
    if (nsol == 0) {
        printf("Puzzle has no solution.\n");
    } else if (nsol < 0) {
        printf("Unable to find a unique solution.\n");
    } else if (nsol > 1) {
        printf("Puzzle has multiple solutions.\n");
    } else {
        verbose = really_verbose;
        unplace_lights(s);
        printf("Puzzle has difficulty %d: solving...\n", diff);
        dosolve(s, sflags, NULL); /* sflags from last successful solve */
        result = game_text_format(s);
        printf("%s", result);
        sfree(result);
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
