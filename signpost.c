/*
 * signpost.c: implementation of the janko game 'arrow path'
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

#define PREFERRED_TILE_SIZE 48
#define TILE_SIZE (ds->tilesize)
#define BLITTER_SIZE TILE_SIZE
#define BORDER    (TILE_SIZE / 2)

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define INGRID(s,x,y) ((x) >= 0 && (x) < (s)->w && (y) >= 0 && (y) < (s)->h)

#define FLASH_SPIN 0.7F

#define NBACKGROUNDS 16

enum {
    COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT,
    COL_GRID, COL_CURSOR, COL_ERROR, COL_DRAG_ORIGIN,
    COL_ARROW, COL_ARROW_BG_DIM,
    COL_NUMBER, COL_NUMBER_SET, COL_NUMBER_SET_MID,
    COL_B0,                             /* background colours */
    COL_M0 =   COL_B0 + 1*NBACKGROUNDS, /* mid arrow colours */
    COL_D0 =   COL_B0 + 2*NBACKGROUNDS, /* dim arrow colours */
    COL_X0 =   COL_B0 + 3*NBACKGROUNDS, /* dim arrow colours */
    NCOLOURS = COL_B0 + 4*NBACKGROUNDS
};

struct game_params {
    int w, h;
    bool force_corner_start;
};

enum { DIR_N = 0, DIR_NE, DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW, DIR_MAX };
static const char *dirstrings[8] = { "N ", "NE", "E ", "SE", "S ", "SW", "W ", "NW" };

static const int dxs[DIR_MAX] = {  0,  1, 1, 1, 0, -1, -1, -1 };
static const int dys[DIR_MAX] = { -1, -1, 0, 1, 1,  1,  0, -1 };

#define DIR_OPPOSITE(d) ((d+4)%8)

struct game_state {
    int w, h, n;
    bool completed, used_solve, impossible;
    int *dirs;                  /* direction enums, size n */
    int *nums;                  /* numbers, size n */
    unsigned int *flags;        /* flags, size n */
    int *next, *prev;           /* links to other cell indexes, size n (-1 absent) */
    int *dsf;                   /* connects regions with a dsf. */
    int *numsi;                 /* for each number, which index is it in? (-1 absent) */
};

#define FLAG_IMMUTABLE  1
#define FLAG_ERROR      2

/* --- Generally useful functions --- */

#define ISREALNUM(state, num) ((num) > 0 && (num) <= (state)->n)

static int whichdir(int fromx, int fromy, int tox, int toy)
{
    int i, dx, dy;

    dx = tox - fromx;
    dy = toy - fromy;

    if (dx && dy && abs(dx) != abs(dy)) return -1;

    if (dx) dx = dx / abs(dx); /* limit to (-1, 0, 1) */
    if (dy) dy = dy / abs(dy); /* ditto */

    for (i = 0; i < DIR_MAX; i++) {
        if (dx == dxs[i] && dy == dys[i]) return i;
    }
    return -1;
}

static int whichdiri(game_state *state, int fromi, int toi)
{
    int w = state->w;
    return whichdir(fromi%w, fromi/w, toi%w, toi/w);
}

static bool ispointing(const game_state *state, int fromx, int fromy,
                       int tox, int toy)
{
    int w = state->w, dir = state->dirs[fromy*w+fromx];

    /* (by convention) squares do not point to themselves. */
    if (fromx == tox && fromy == toy) return false;

    /* the final number points to nothing. */
    if (state->nums[fromy*w + fromx] == state->n) return false;

    while (1) {
        if (!INGRID(state, fromx, fromy)) return false;
        if (fromx == tox && fromy == toy) return true;
        fromx += dxs[dir]; fromy += dys[dir];
    }
    return false; /* not reached */
}

static bool ispointingi(game_state *state, int fromi, int toi)
{
    int w = state->w;
    return ispointing(state, fromi%w, fromi/w, toi%w, toi/w);
}

/* Taking the number 'num', work out the gap between it and the next
 * available number up or down (depending on d). Return true if the
 * region at (x,y) will fit in that gap. */
static bool move_couldfit(
    const game_state *state, int num, int d, int x, int y)
{
    int n, gap, i = y*state->w+x, sz;

    assert(d != 0);
    /* The 'gap' is the number of missing numbers in the grid between
     * our number and the next one in the sequence (up or down), or
     * the end of the sequence (if we happen not to have 1/n present) */
    for (n = num + d, gap = 0;
         ISREALNUM(state, n) && state->numsi[n] == -1;
         n += d, gap++) ; /* empty loop */

    if (gap == 0) {
        /* no gap, so the only allowable move is that that directly
         * links the two numbers. */
        n = state->nums[i];
        return n != num+d;
    }
    if (state->prev[i] == -1 && state->next[i] == -1)
        return true; /* single unconnected square, always OK */

    sz = dsf_size(state->dsf, i);
    return sz <= gap;
}

static bool isvalidmove(const game_state *state, bool clever,
                        int fromx, int fromy, int tox, int toy)
{
    int w = state->w, from = fromy*w+fromx, to = toy*w+tox;
    int nfrom, nto;

    if (!INGRID(state, fromx, fromy) || !INGRID(state, tox, toy))
        return false;

    /* can only move where we point */
    if (!ispointing(state, fromx, fromy, tox, toy))
        return false;

    nfrom = state->nums[from]; nto = state->nums[to];

    /* can't move _from_ the preset final number, or _to_ the preset 1. */
    if (((nfrom == state->n) && (state->flags[from] & FLAG_IMMUTABLE)) ||
        ((nto   == 1)        && (state->flags[to]   & FLAG_IMMUTABLE)))
        return false;

    /* can't create a new connection between cells in the same region
     * as that would create a loop. */
    if (dsf_canonify(state->dsf, from) == dsf_canonify(state->dsf, to))
        return false;

    /* if both cells are actual numbers, can't drag if we're not
     * one digit apart. */
    if (ISREALNUM(state, nfrom) && ISREALNUM(state, nto)) {
        if (nfrom != nto-1)
            return false;
    } else if (clever && ISREALNUM(state, nfrom)) {
        if (!move_couldfit(state, nfrom, +1, tox, toy))
            return false;
    } else if (clever && ISREALNUM(state, nto)) {
        if (!move_couldfit(state, nto, -1, fromx, fromy))
            return false;
    }

    return true;
}

static void makelink(game_state *state, int from, int to)
{
    if (state->next[from] != -1)
        state->prev[state->next[from]] = -1;
    state->next[from] = to;

    if (state->prev[to] != -1)
        state->next[state->prev[to]] = -1;
    state->prev[to] = from;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    if (params->w * params->h >= 100) return false;
    return true;
}

static char *game_text_format(const game_state *state)
{
    int len = state->h * 2 * (4*state->w + 1) + state->h + 2;
    int x, y, i, num, n, set;
    char *ret, *p;

    p = ret = snewn(len, char);

    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->h; x++) {
            i = y*state->w+x;
            *p++ = dirstrings[state->dirs[i]][0];
            *p++ = dirstrings[state->dirs[i]][1];
            *p++ = (state->flags[i] & FLAG_IMMUTABLE) ? 'I' : ' ';
            *p++ = ' ';
        }
        *p++ = '\n';
        for (x = 0; x < state->h; x++) {
            i = y*state->w+x;
            num = state->nums[i];
            if (num == 0) {
                *p++ = ' ';
                *p++ = ' ';
                *p++ = ' ';
            } else {
                n = num % (state->n+1);
                set = num / (state->n+1);

                assert(n <= 99); /* two digits only! */

                if (set != 0)
                    *p++ = set+'a'-1;

                *p++ = (n >= 10) ? ('0' + (n/10)) : ' ';
                *p++ = '0' + (n%10);

                if (set == 0)
                    *p++ = ' ';
            }
            *p++ = ' ';
        }
        *p++ = '\n';
        *p++ = '\n';
    }
    *p++ = '\0';

    return ret;
}

static void debug_state(const char *desc, game_state *state)
{
#ifdef DEBUGGING
    char *dbg;
    if (state->n >= 100) {
        debug(("[ no game_text_format for this size ]"));
        return;
    }
    dbg = game_text_format(state);
    debug(("%s\n%s", desc, dbg));
    sfree(dbg);
#endif
}


static void strip_nums(game_state *state) {
    int i;
    for (i = 0; i < state->n; i++) {
        if (!(state->flags[i] & FLAG_IMMUTABLE))
            state->nums[i] = 0;
    }
    memset(state->next, -1, state->n*sizeof(int));
    memset(state->prev, -1, state->n*sizeof(int));
    memset(state->numsi, -1, (state->n+1)*sizeof(int));
    dsf_init(state->dsf, state->n);
}

static bool check_nums(game_state *orig, game_state *copy, bool only_immutable)
{
    int i;
    bool ret = true;
    assert(copy->n == orig->n);
    for (i = 0; i < copy->n; i++) {
        if (only_immutable && !(copy->flags[i] & FLAG_IMMUTABLE)) continue;
        assert(copy->nums[i] >= 0);
        assert(copy->nums[i] <= copy->n);
        if (copy->nums[i] != orig->nums[i]) {
            debug(("check_nums: (%d,%d) copy=%d, orig=%d.",
                   i%orig->w, i/orig->w, copy->nums[i], orig->nums[i]));
            ret = false;
        }
    }
    return ret;
}

/* --- Game parameter/presets functions --- */

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    ret->w = ret->h = 4;
    ret->force_corner_start = true;

    return ret;
}

static const struct game_params signpost_presets[] = {
  { 4, 4, 1 },
  { 4, 4, 0 },
  { 5, 5, 1 },
  { 5, 5, 0 },
  { 6, 6, 1 },
  { 7, 7, 1 }
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(signpost_presets))
        return false;

    ret = default_params();
    *ret = signpost_presets[i];
    *params = ret;

    sprintf(buf, "%dx%d%s", ret->w, ret->h,
            ret->force_corner_start ? "" : ", free ends");
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

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    ret->force_corner_start = false;
    if (*string == 'c') {
        string++;
        ret->force_corner_start = true;
    }

}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    if (full)
        sprintf(data, "%dx%d%s", params->w, params->h,
                params->force_corner_start ? "c" : "");
    else
        sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
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

    ret[2].name = "Start and end in corners";
    ret[2].type = C_BOOLEAN;
    ret[2].u.boolean.bval = params->force_corner_start;

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->force_corner_start = cfg[2].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 1) return "Width must be at least one";
    if (params->h < 1) return "Height must be at least one";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (full && params->w == 1 && params->h == 1)
	/* The UI doesn't let us move these from unsolved to solved,
	 * so we disallow generating (but not playing) them. */
	return "Width and height cannot both be one";
    return NULL;
}

/* --- Game description string generation and unpicking --- */

static void blank_game_into(game_state *state)
{
    memset(state->dirs, 0, state->n*sizeof(int));
    memset(state->nums, 0, state->n*sizeof(int));
    memset(state->flags, 0, state->n*sizeof(unsigned int));
    memset(state->next, -1, state->n*sizeof(int));
    memset(state->prev, -1, state->n*sizeof(int));
    memset(state->numsi, -1, (state->n+1)*sizeof(int));
}

static game_state *blank_game(int w, int h)
{
    game_state *state = snew(game_state);

    memset(state, 0, sizeof(game_state));
    state->w = w;
    state->h = h;
    state->n = w*h;

    state->dirs  = snewn(state->n, int);
    state->nums  = snewn(state->n, int);
    state->flags = snewn(state->n, unsigned int);
    state->next  = snewn(state->n, int);
    state->prev  = snewn(state->n, int);
    state->dsf = snew_dsf(state->n);
    state->numsi  = snewn(state->n+1, int);

    blank_game_into(state);

    return state;
}

static void dup_game_to(game_state *to, const game_state *from)
{
    to->completed = from->completed;
    to->used_solve = from->used_solve;
    to->impossible = from->impossible;

    memcpy(to->dirs, from->dirs, to->n*sizeof(int));
    memcpy(to->flags, from->flags, to->n*sizeof(unsigned int));
    memcpy(to->nums, from->nums, to->n*sizeof(int));

    memcpy(to->next, from->next, to->n*sizeof(int));
    memcpy(to->prev, from->prev, to->n*sizeof(int));

    memcpy(to->dsf, from->dsf, to->n*sizeof(int));
    memcpy(to->numsi, from->numsi, (to->n+1)*sizeof(int));
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = blank_game(state->w, state->h);
    dup_game_to(ret, state);
    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->dirs);
    sfree(state->nums);
    sfree(state->flags);
    sfree(state->next);
    sfree(state->prev);
    sfree(state->dsf);
    sfree(state->numsi);
    sfree(state);
}

static void unpick_desc(const game_params *params, const char *desc,
                        game_state **sout, const char **mout)
{
    game_state *state = blank_game(params->w, params->h);
    const char *msg = NULL;
    char c;
    int num = 0, i = 0;

    while (*desc) {
        if (i >= state->n) {
            msg = "Game description longer than expected";
            goto done;
        }

        c = *desc;
        if (isdigit((unsigned char)c)) {
            num = (num*10) + (int)(c-'0');
            if (num > state->n) {
                msg = "Number too large";
                goto done;
            }
        } else if ((c-'a') >= 0 && (c-'a') < DIR_MAX) {
            state->nums[i] = num;
            state->flags[i] = num ? FLAG_IMMUTABLE : 0;
            num = 0;

            state->dirs[i] = c - 'a';
            i++;
        } else if (!*desc) {
            msg = "Game description shorter than expected";
            goto done;
        } else {
            msg = "Game description contains unexpected characters";
            goto done;
        }
        desc++;
    }
    if (i < state->n) {
        msg = "Game description shorter than expected";
        goto done;
    }

done:
    if (msg) { /* sth went wrong. */
        if (mout) *mout = msg;
        free_game(state);
    } else {
        if (mout) *mout = NULL;
        if (sout) *sout = state;
        else free_game(state);
    }
}

static char *generate_desc(game_state *state, bool issolve)
{
    char *ret, buf[80];
    int retlen, i, k;

    ret = NULL; retlen = 0;
    if (issolve) {
        ret = sresize(ret, 2, char);
        ret[0] = 'S'; ret[1] = '\0';
        retlen += 1;
    }
    for (i = 0; i < state->n; i++) {
        if (state->nums[i])
            k = sprintf(buf, "%d%c", state->nums[i], (int)(state->dirs[i]+'a'));
        else
            k = sprintf(buf, "%c", (int)(state->dirs[i]+'a'));
        ret = sresize(ret, retlen + k + 1, char);
        strcpy(ret + retlen, buf);
        retlen += k;
    }
    return ret;
}

/* --- Game generation --- */

/* Fills in preallocated arrays ai (indices) and ad (directions)
 * showing all non-numbered cells adjacent to index i, returns length */
/* This function has been somewhat optimised... */
static int cell_adj(game_state *state, int i, int *ai, int *ad)
{
    int n = 0, a, x, y, sx, sy, dx, dy, newi;
    int w = state->w, h = state->h;

    sx = i % w; sy = i / w;

    for (a = 0; a < DIR_MAX; a++) {
        x = sx; y = sy;
        dx = dxs[a]; dy = dys[a];
        while (1) {
            x += dx; y += dy;
            if (x < 0 || y < 0 || x >= w || y >= h) break;

            newi = y*w + x;
            if (state->nums[newi] == 0) {
                ai[n] = newi;
                ad[n] = a;
                n++;
            }
        }
    }
    return n;
}

static bool new_game_fill(game_state *state, random_state *rs,
                          int headi, int taili)
{
    int nfilled, an, j;
    bool ret = false;
    int *aidx, *adir;

    aidx = snewn(state->n, int);
    adir = snewn(state->n, int);

    debug(("new_game_fill: headi=%d, taili=%d.", headi, taili));

    memset(state->nums, 0, state->n*sizeof(int));

    state->nums[headi] = 1;
    state->nums[taili] = state->n;

    state->dirs[taili] = 0;
    nfilled = 2;
    assert(state->n > 1);

    while (nfilled < state->n) {
        /* Try and expand _from_ headi; keep going if there's only one
         * place to go to. */
        an = cell_adj(state, headi, aidx, adir);
        do {
            if (an == 0) goto done;
            j = random_upto(rs, an);
            state->dirs[headi] = adir[j];
            state->nums[aidx[j]] = state->nums[headi] + 1;
            nfilled++;
            headi = aidx[j];
            an = cell_adj(state, headi, aidx, adir);
        } while (an == 1);

	if (nfilled == state->n) break;

        /* Try and expand _to_ taili; keep going if there's only one
         * place to go to. */
        an = cell_adj(state, taili, aidx, adir);
        do {
            if (an == 0) goto done;
            j = random_upto(rs, an);
            state->dirs[aidx[j]] = DIR_OPPOSITE(adir[j]);
            state->nums[aidx[j]] = state->nums[taili] - 1;
            nfilled++;
            taili = aidx[j];
            an = cell_adj(state, taili, aidx, adir);
        } while (an == 1);
    }
    /* If we get here we have headi and taili set but unconnected
     * by direction: we need to set headi's direction so as to point
     * at taili. */
    state->dirs[headi] = whichdiri(state, headi, taili);

    /* it could happen that our last two weren't in line; if that's the
     * case, we have to start again. */
    if (state->dirs[headi] != -1) ret = true;

done:
    sfree(aidx);
    sfree(adir);
    return ret;
}

/* Better generator: with the 'generate, sprinkle numbers, solve,
 * repeat' algorithm we're _never_ generating anything greater than
 * 6x6, and spending all of our time in new_game_fill (and very little
 * in solve_state).
 *
 * So, new generator steps:
   * generate the grid, at random (same as now). Numbers 1 and N get
      immutable flag immediately.
   * squirrel that away for the solved state.
   *
   * (solve:) Try and solve it.
   * If we solved it, we're done:
     * generate the description from current immutable numbers,
     * free stuff that needs freeing,
     * return description + solved state.
   * If we didn't solve it:
     * count #tiles in state we've made deductions about.
     * while (1):
       * randomise a scratch array.
       * for each index in scratch (in turn):
         * if the cell isn't empty, continue (through scratch array)
         * set number + immutable in state.
         * try and solve state.
         * if we've solved it, we're done.
         * otherwise, count #tiles. If it's more than we had before:
           * good, break from this loop and re-randomise.
         * otherwise (number didn't help):
           * remove number and try next in scratch array.
       * if we've got to the end of the scratch array, no luck:
          free everything we need to, and go back to regenerate the grid.
   */

static int solve_state(game_state *state);

static void debug_desc(const char *what, game_state *state)
{
#if DEBUGGING
    {
        char *desc = generate_desc(state, 0);
        debug(("%s game state: %dx%d:%s", what, state->w, state->h, desc));
        sfree(desc);
    }
#endif
}

/* Expects a fully-numbered game_state on input, and makes sure
 * FLAG_IMMUTABLE is only set on those numbers we need to solve
 * (as for a real new-game); returns true if it managed
 * this (such that it could solve it), or false if not. */
static bool new_game_strip(game_state *state, random_state *rs)
{
    int *scratch, i, j;
    bool ret = true;
    game_state *copy = dup_game(state);

    debug(("new_game_strip."));

    strip_nums(copy);
    debug_desc("Stripped", copy);

    if (solve_state(copy) > 0) {
        debug(("new_game_strip: soluble immediately after strip."));
        free_game(copy);
        return true;
    }

    scratch = snewn(state->n, int);
    for (i = 0; i < state->n; i++) scratch[i] = i;
    shuffle(scratch, state->n, sizeof(int), rs);

    /* This is scungy. It might just be quick enough.
     * It goes through, adding set numbers in empty squares
     * until either we run out of empty squares (in the one
     * we're half-solving) or else we solve it properly.
     * NB that we run the entire solver each time, which
     * strips the grid beforehand; we will save time if we
     * avoid that. */
    for (i = 0; i < state->n; i++) {
        j = scratch[i];
        if (copy->nums[j] > 0 && copy->nums[j] <= state->n)
            continue; /* already solved to a real number here. */
        assert(state->nums[j] <= state->n);
        debug(("new_game_strip: testing add IMMUTABLE number %d at square (%d,%d).",
               state->nums[j], j%state->w, j/state->w));
        copy->nums[j] = state->nums[j];
        copy->flags[j] |= FLAG_IMMUTABLE;
        state->flags[j] |= FLAG_IMMUTABLE;
        debug_state("Copy of state: ", copy);
        strip_nums(copy);
        if (solve_state(copy) > 0) goto solved;
        assert(check_nums(state, copy, true));
    }
    ret = false;
    goto done;

solved:
    debug(("new_game_strip: now solved."));
    /* Since we added basically at random, try now to remove numbers
     * and see if we can still solve it; if we can (still), really
     * remove the number. Make sure we don't remove the anchor numbers
     * 1 and N. */
    for (i = 0; i < state->n; i++) {
        j = scratch[i];
        if ((state->flags[j] & FLAG_IMMUTABLE) &&
            (state->nums[j] != 1 && state->nums[j] != state->n)) {
            debug(("new_game_strip: testing remove IMMUTABLE number %d at square (%d,%d).",
                  state->nums[j], j%state->w, j/state->w));
            state->flags[j] &= ~FLAG_IMMUTABLE;
            dup_game_to(copy, state);
            strip_nums(copy);
            if (solve_state(copy) > 0) {
                assert(check_nums(state, copy, false));
                debug(("new_game_strip: OK, removing number"));
            } else {
                assert(state->nums[j] <= state->n);
                debug(("new_game_strip: cannot solve, putting IMMUTABLE back."));
                copy->nums[j] = state->nums[j];
                state->flags[j] |= FLAG_IMMUTABLE;
            }
        }
    }

done:
    debug(("new_game_strip: %ssuccessful.", ret ? "" : "not "));
    sfree(scratch);
    free_game(copy);
    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    game_state *state = blank_game(params->w, params->h);
    char *ret;
    int headi, taili;

    /* this shouldn't happen (validate_params), but let's play it safe */
    if (params->w == 1 && params->h == 1) return dupstr("1a");

generate:
    blank_game_into(state);

    /* keep trying until we fill successfully. */
    do {
        if (params->force_corner_start) {
            headi = 0;
            taili = state->n-1;
        } else {
            do {
                headi = random_upto(rs, state->n);
                taili = random_upto(rs, state->n);
            } while (headi == taili);
        }
    } while (!new_game_fill(state, rs, headi, taili));

    debug_state("Filled game:", state);

    assert(state->nums[headi] <= state->n);
    assert(state->nums[taili] <= state->n);

    state->flags[headi] |= FLAG_IMMUTABLE;
    state->flags[taili] |= FLAG_IMMUTABLE;

    /* This will have filled in directions and _all_ numbers.
     * Store the game definition for this, as the solved-state. */
    if (!new_game_strip(state, rs)) {
        goto generate;
    }
    strip_nums(state);
    {
        game_state *tosolve = dup_game(state);
        assert(solve_state(tosolve) > 0);
        free_game(tosolve);
    }
    ret = generate_desc(state, false);
    free_game(state);
    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *ret = NULL;

    unpick_desc(params, desc, NULL, &ret);
    return ret;
}

/* --- Linked-list and numbers array --- */

/* Assuming numbers are always up-to-date, there are only four possibilities
 * for regions changing after a single valid move:
 *
 * 1) two differently-coloured regions being combined (the resulting colouring
 *     should be based on the larger of the two regions)
 * 2) a numbered region having a single number added to the start (the
 *     region's colour will remain, and the numbers will shift by 1)
 * 3) a numbered region having a single number added to the end (the
 *     region's colour and numbering remains as-is)
 * 4) two unnumbered squares being joined (will pick the smallest unused set
 *     of colours to use for the new region).
 *
 * There should never be any complications with regions containing 3 colours
 * being combined, since two of those colours should have been merged on a
 * previous move.
 *
 * Most of the complications are in ensuring we don't accidentally set two
 * regions with the same colour (e.g. if a region was split). If this happens
 * we always try and give the largest original portion the original colour.
 */

#define COLOUR(a) ((a) / (state->n+1))
#define START(c) ((c) * (state->n+1))

struct head_meta {
    int i;      /* position */
    int sz;     /* size of region */
    int start;  /* region start number preferred, or 0 if !preference */
    int preference; /* 0 if we have no preference (and should just pick one) */
    const char *why;
};

static void head_number(game_state *state, int i, struct head_meta *head)
{
    int off = 0, ss, j = i, c, n, sz;

    /* Insist we really were passed the head of a chain. */
    assert(state->prev[i] == -1 && state->next[i] != -1);

    head->i = i;
    head->sz = dsf_size(state->dsf, i);
    head->why = NULL;

    /* Search through this chain looking for real numbers, checking that
     * they match up (if there are more than one). */
    head->preference = 0;
    while (j != -1) {
        if (state->flags[j] & FLAG_IMMUTABLE) {
            ss = state->nums[j] - off;
            if (!head->preference) {
                head->start = ss;
                head->preference = 1;
                head->why = "contains cell with immutable number";
            } else if (head->start != ss) {
                debug(("head_number: chain with non-sequential numbers!"));
                state->impossible = true;
            }
        }
        off++;
        j = state->next[j];
        assert(j != i); /* we have created a loop, obviously wrong */
    }
    if (head->preference) goto done;

    if (state->nums[i] == 0 && state->nums[state->next[i]] > state->n) {
        /* (probably) empty cell onto the head of a coloured region:
         * make sure we start at a 0 offset. */
        head->start = START(COLOUR(state->nums[state->next[i]]));
        head->preference = 1;
        head->why = "adding blank cell to head of numbered region";
    } else if (state->nums[i] <= state->n) {
        /* if we're 0 we're probably just blank -- but even if we're a
         * (real) numbered region, we don't have an immutable number
         * in it (any more) otherwise it'd have been caught above, so
         * reassign the colour. */
        head->start = 0;
        head->preference = 0;
        head->why = "lowest available colour group";
    } else {
        c = COLOUR(state->nums[i]);
        n = 1;
        sz = dsf_size(state->dsf, i);
        j = i;
        while (state->next[j] != -1) {
            j = state->next[j];
            if (state->nums[j] == 0 && state->next[j] == -1) {
                head->start = START(c);
                head->preference = 1;
                head->why = "adding blank cell to end of numbered region";
                goto done;
            }
            if (COLOUR(state->nums[j]) == c)
                n++;
            else {
                int start_alternate = START(COLOUR(state->nums[j]));
                if (n < (sz - n)) {
                    head->start = start_alternate;
                    head->preference = 1;
                    head->why = "joining two coloured regions, swapping to larger colour";
                } else {
                    head->start = START(c);
                    head->preference = 1;
                    head->why = "joining two coloured regions, taking largest";
                }
                goto done;
            }
        }
        /* If we got here then we may have split a region into
         * two; make sure we don't assign a colour we've already used. */
        if (c == 0) {
            /* not convinced this shouldn't be an assertion failure here. */
            head->start = 0;
            head->preference = 0;
        } else {
            head->start = START(c);
            head->preference = 1;
        }
        head->why = "got to end of coloured region";
    }

done:
    assert(head->why != NULL);
    if (head->preference)
        debug(("Chain at (%d,%d) numbered for preference at %d (colour %d): %s.",
               head->i%state->w, head->i/state->w,
               head->start, COLOUR(head->start), head->why));
    else
        debug(("Chain at (%d,%d) using next available colour: %s.",
               head->i%state->w, head->i/state->w,
               head->why));
}

#if 0
static void debug_numbers(game_state *state)
{
    int i, w=state->w;

    for (i = 0; i < state->n; i++) {
        debug(("(%d,%d) --> (%d,%d) --> (%d,%d)",
               state->prev[i]==-1 ? -1 : state->prev[i]%w,
               state->prev[i]==-1 ? -1 : state->prev[i]/w,
               i%w, i/w,
               state->next[i]==-1 ? -1 : state->next[i]%w,
               state->next[i]==-1 ? -1 : state->next[i]/w));
    }
    w = w+1;
}
#endif

static void connect_numbers(game_state *state)
{
    int i, di, dni;

    dsf_init(state->dsf, state->n);
    for (i = 0; i < state->n; i++) {
        if (state->next[i] != -1) {
            assert(state->prev[state->next[i]] == i);
            di = dsf_canonify(state->dsf, i);
            dni = dsf_canonify(state->dsf, state->next[i]);
            if (di == dni) {
                debug(("connect_numbers: chain forms a loop."));
                state->impossible = true;
            }
            dsf_merge(state->dsf, di, dni);
        }
    }
}

static int compare_heads(const void *a, const void *b)
{
    const struct head_meta *ha = (const struct head_meta *)a;
    const struct head_meta *hb = (const struct head_meta *)b;

    /* Heads with preferred colours first... */
    if (ha->preference && !hb->preference) return -1;
    if (hb->preference && !ha->preference) return 1;

    /* ...then heads with low colours first... */
    if (ha->start < hb->start) return -1;
    if (ha->start > hb->start) return 1;

    /* ... then large regions first... */
    if (ha->sz > hb->sz) return -1;
    if (ha->sz < hb->sz) return 1;

    /* ... then position. */
    if (ha->i > hb->i) return -1;
    if (ha->i < hb->i) return 1;

    return 0;
}

static int lowest_start(game_state *state, struct head_meta *heads, int nheads)
{
    int n, c;

    /* NB start at 1: colour 0 is real numbers */
    for (c = 1; c < state->n; c++) {
        for (n = 0; n < nheads; n++) {
            if (COLOUR(heads[n].start) == c)
                goto used;
        }
        return c;
used:
        ;
    }
    assert(!"No available colours!");
    return 0;
}

static void update_numbers(game_state *state)
{
    int i, j, n, nnum, nheads;
    struct head_meta *heads = snewn(state->n, struct head_meta);

    for (n = 0; n < state->n; n++)
        state->numsi[n] = -1;

    for (i = 0; i < state->n; i++) {
        if (state->flags[i] & FLAG_IMMUTABLE) {
            assert(state->nums[i] > 0);
            assert(state->nums[i] <= state->n);
            state->numsi[state->nums[i]] = i;
        }
        else if (state->prev[i] == -1 && state->next[i] == -1)
            state->nums[i] = 0;
    }
    connect_numbers(state);

    /* Construct an array of the heads of all current regions, together
     * with their preferred colours. */
    nheads = 0;
    for (i = 0; i < state->n; i++) {
        /* Look for a cell that is the start of a chain
         * (has a next but no prev). */
        if (state->prev[i] != -1 || state->next[i] == -1) continue;

        head_number(state, i, &heads[nheads++]);
    }

    /* Sort that array:
     * - heads with preferred colours first, then
     * - heads with low colours first, then
     * - large regions first
     */
    qsort(heads, nheads, sizeof(struct head_meta), compare_heads);

    /* Remove duplicate-coloured regions. */
    for (n = nheads-1; n >= 0; n--) { /* order is important! */
        if ((n != 0) && (heads[n].start == heads[n-1].start)) {
            /* We have a duplicate-coloured region: since we're
             * sorted in size order and this is not the first
             * of its colour it's not the largest: recolour it. */
            heads[n].start = START(lowest_start(state, heads, nheads));
            heads[n].preference = -1; /* '-1' means 'was duplicate' */
        }
        else if (!heads[n].preference) {
            assert(heads[n].start == 0);
            heads[n].start = START(lowest_start(state, heads, nheads));
        }
    }

    debug(("Region colouring after duplicate removal:"));

    for (n = 0; n < nheads; n++) {
        debug(("  Chain at (%d,%d) sz %d numbered at %d (colour %d): %s%s",
               heads[n].i % state->w, heads[n].i / state->w, heads[n].sz,
               heads[n].start, COLOUR(heads[n].start), heads[n].why,
               heads[n].preference == 0 ? " (next available)" :
               heads[n].preference < 0 ? " (duplicate, next available)" : ""));

        nnum = heads[n].start;
        j = heads[n].i;
        while (j != -1) {
            if (!(state->flags[j] & FLAG_IMMUTABLE)) {
                if (nnum > 0 && nnum <= state->n)
                    state->numsi[nnum] = j;
                state->nums[j] = nnum;
            }
            nnum++;
            j = state->next[j];
            assert(j != heads[n].i); /* loop?! */
        }
    }
    /*debug_numbers(state);*/
    sfree(heads);
}

static bool check_completion(game_state *state, bool mark_errors)
{
    int n, j, k;
    bool error = false, complete;

    /* NB This only marks errors that are possible to perpetrate with
     * the current UI in interpret_move. Things like forming loops in
     * linked sections and having numbers not add up should be forbidden
     * by the code elsewhere, so we don't bother marking those (because
     * it would add lots of tricky drawing code for very little gain). */
    if (mark_errors) {
        for (j = 0; j < state->n; j++)
            state->flags[j] &= ~FLAG_ERROR;
    }

    /* Search for repeated numbers. */
    for (j = 0; j < state->n; j++) {
        if (state->nums[j] > 0 && state->nums[j] <= state->n) {
            for (k = j+1; k < state->n; k++) {
                if (state->nums[k] == state->nums[j]) {
                    if (mark_errors) {
                        state->flags[j] |= FLAG_ERROR;
                        state->flags[k] |= FLAG_ERROR;
                    }
                    error = true;
                }
            }
        }
    }

    /* Search and mark numbers n not pointing to n+1; if any numbers
     * are missing we know we've not completed. */
    complete = true;
    for (n = 1; n < state->n; n++) {
        if (state->numsi[n] == -1 || state->numsi[n+1] == -1)
            complete = false;
        else if (!ispointingi(state, state->numsi[n], state->numsi[n+1])) {
            if (mark_errors) {
                state->flags[state->numsi[n]] |= FLAG_ERROR;
                state->flags[state->numsi[n+1]] |= FLAG_ERROR;
            }
            error = true;
        } else {
            /* make sure the link is explicitly made here; for instance, this
             * is nice if the user drags from 2 out (making 3) and a 4 is also
             * visible; this ensures that the link from 3 to 4 is also made. */
            if (mark_errors)
                makelink(state, state->numsi[n], state->numsi[n+1]);
        }
    }

    /* Search and mark numbers less than 0, or 0 with links. */
    for (n = 1; n < state->n; n++) {
        if ((state->nums[n] < 0) ||
            (state->nums[n] == 0 &&
             (state->next[n] != -1 || state->prev[n] != -1))) {
            error = true;
            if (mark_errors)
                state->flags[n] |= FLAG_ERROR;
        }
    }

    if (error) return false;
    return complete;
}
static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = NULL;

    unpick_desc(params, desc, &state, NULL);
    if (!state) assert(!"new_game failed to unpick");

    update_numbers(state);
    check_completion(state, true); /* update any auto-links */

    return state;
}

/* --- Solver --- */

/* If a tile has a single tile it can link _to_, or there's only a single
 * location that can link to a given tile, fill that link in. */
static int solve_single(game_state *state, game_state *copy, int *from)
{
    int i, j, sx, sy, x, y, d, poss, w=state->w, nlinks = 0;

    /* The from array is a list of 'which square can link _to_ us';
     * we start off with from as '-1' (meaning 'not found'); if we find
     * something that can link to us it is set to that index, and then if
     * we find another we set it to -2. */

    memset(from, -1, state->n*sizeof(int));

    /* poss is 'can I link to anything' with the same meanings. */

    for (i = 0; i < state->n; i++) {
        if (state->next[i] != -1) continue;
        if (state->nums[i] == state->n) continue; /* no next from last no. */

        d = state->dirs[i];
        poss = -1;
        sx = x = i%w; sy = y = i/w;
        while (1) {
            x += dxs[d]; y += dys[d];
            if (!INGRID(state, x, y)) break;
            if (!isvalidmove(state, true, sx, sy, x, y)) continue;

            /* can't link to somewhere with a back-link we would have to
             * break (the solver just doesn't work like this). */
            j = y*w+x;
            if (state->prev[j] != -1) continue;

            if (state->nums[i] > 0 && state->nums[j] > 0 &&
                state->nums[i] <= state->n && state->nums[j] <= state->n &&
                state->nums[j] == state->nums[i]+1) {
                debug(("Solver: forcing link through existing consecutive numbers."));
                poss = j;
                from[j] = i;
                break;
            }

            /* if there's been a valid move already, we have to move on;
             * we can't make any deductions here. */
            poss = (poss == -1) ? j : -2;

            /* Modify the from array as described above (which is enumerating
             * what points to 'j' in a similar way). */
            from[j] = (from[j] == -1) ? i : -2;
        }
        if (poss == -2) {
            /*debug(("Solver: (%d,%d) has multiple possible next squares.", sx, sy));*/
            ;
        } else if (poss == -1) {
            debug(("Solver: nowhere possible for (%d,%d) to link to.", sx, sy));
            copy->impossible = true;
            return -1;
        } else {
            debug(("Solver: linking (%d,%d) to only possible next (%d,%d).",
                   sx, sy, poss%w, poss/w));
            makelink(copy, i, poss);
            nlinks++;
        }
    }

    for (i = 0; i < state->n; i++) {
        if (state->prev[i] != -1) continue;
        if (state->nums[i] == 1) continue; /* no prev from 1st no. */

        x = i%w; y = i/w;
        if (from[i] == -1) {
            debug(("Solver: nowhere possible to link to (%d,%d)", x, y));
            copy->impossible = true;
            return -1;
        } else if (from[i] == -2) {
            /*debug(("Solver: (%d,%d) has multiple possible prev squares.", x, y));*/
            ;
        } else {
            debug(("Solver: linking only possible prev (%d,%d) to (%d,%d).",
                   from[i]%w, from[i]/w, x, y));
            makelink(copy, from[i], i);
            nlinks++;
        }
    }

    return nlinks;
}

/* Returns 1 if we managed to solve it, 0 otherwise. */
static int solve_state(game_state *state)
{
    game_state *copy = dup_game(state);
    int *scratch = snewn(state->n, int), ret;

    debug_state("Before solver: ", state);

    while (1) {
        update_numbers(state);

        if (solve_single(state, copy, scratch)) {
            dup_game_to(state, copy);
            if (state->impossible) break; else continue;
        }
        break;
    }
    free_game(copy);
    sfree(scratch);

    update_numbers(state);
    ret = state->impossible ? -1 : check_completion(state, false);
    debug(("Solver finished: %s",
           ret < 0 ? "impossible" : ret > 0 ? "solved" : "not solved"));
    debug_state("After solver: ", state);
    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *tosolve;
    char *ret = NULL;
    int result;

    tosolve = dup_game(currstate);
    result = solve_state(tosolve);
    if (result > 0)
        ret = generate_desc(tosolve, true);
    free_game(tosolve);
    if (ret) return ret;

    tosolve = dup_game(state);
    result = solve_state(tosolve);
    if (result < 0)
        *error = "Puzzle is impossible.";
    else if (result == 0)
        *error = "Unable to solve puzzle.";
    else
        ret = generate_desc(tosolve, true);

    free_game(tosolve);

    return ret;
}

/* --- UI and move routines. --- */


struct game_ui {
    int cx, cy;
    bool cshow;

    bool dragging, drag_is_from;
    int sx, sy;         /* grid coords of start cell */
    int dx, dy;         /* pixel coords of drag posn */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    /* NB: if this is ever changed to as to require more than a structure
     * copy to clone, there's code that needs fixing in game_redraw too. */

    ui->cx = ui->cy = 0;
    ui->cshow = getenv_bool("PUZZLES_SHOW_CURSOR", false);

    ui->dragging = false;
    ui->sx = ui->sy = ui->dx = ui->dy = 0;

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
    if (!oldstate->completed && newstate->completed) {
        ui->cshow = false;
        ui->dragging = false;
    }
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cshow) {
        if (ui->dragging) {
            if (ui->drag_is_from) {
                if (isvalidmove(state, false, ui->sx, ui->sy, ui->cx, ui->cy))
                    return "To here";
            } else {
                if (isvalidmove(state, false, ui->cx, ui->cy, ui->sx, ui->sy))
                    return "From here";
            }
            return "Cancel";
        } else {
            return button == CURSOR_SELECT ? "From here" : "To here";
        }
    }
    return "";
}

struct game_drawstate {
    int tilesize;
    bool started, solved;
    int w, h, n;
    int *nums, *dirp;
    unsigned int *f;
    double angle_offset;

    bool dragging;
    int dx, dy;
    blitter *dragb;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int mx, int my, int button)
{
    int x = FROMCOORD(mx), y = FROMCOORD(my), w = state->w;
    char buf[80];

    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cx, &ui->cy, state->w, state->h, false);
        ui->cshow = true;
        if (ui->dragging) {
            ui->dx = COORD(ui->cx) + TILE_SIZE/2;
            ui->dy = COORD(ui->cy) + TILE_SIZE/2;
        }
        return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cshow)
            ui->cshow = true;
        else if (ui->dragging) {
            ui->dragging = false;
            if (ui->sx == ui->cx && ui->sy == ui->cy) return UI_UPDATE;
            if (ui->drag_is_from) {
                if (!isvalidmove(state, false, ui->sx, ui->sy, ui->cx, ui->cy))
                    return UI_UPDATE;
                sprintf(buf, "L%d,%d-%d,%d", ui->sx, ui->sy, ui->cx, ui->cy);
            } else {
                if (!isvalidmove(state, false, ui->cx, ui->cy, ui->sx, ui->sy))
                    return UI_UPDATE;
                sprintf(buf, "L%d,%d-%d,%d", ui->cx, ui->cy, ui->sx, ui->sy);
            }
            return dupstr(buf);
        } else {
            ui->dragging = true;
            ui->sx = ui->cx;
            ui->sy = ui->cy;
            ui->dx = COORD(ui->cx) + TILE_SIZE/2;
            ui->dy = COORD(ui->cy) + TILE_SIZE/2;
            ui->drag_is_from = (button == CURSOR_SELECT);
        }
        return UI_UPDATE;
    }
    if (IS_MOUSE_DOWN(button)) {
        if (ui->cshow) {
            ui->cshow = false;
            ui->dragging = false;
        }
        assert(!ui->dragging);
        if (!INGRID(state, x, y)) return NULL;

        if (button == LEFT_BUTTON) {
            /* disallow dragging from the final number. */
            if ((state->nums[y*w+x] == state->n) &&
                (state->flags[y*w+x] & FLAG_IMMUTABLE))
                return NULL;
        } else if (button == RIGHT_BUTTON) {
            /* disallow dragging to the first number. */
            if ((state->nums[y*w+x] == 1) &&
                (state->flags[y*w+x] & FLAG_IMMUTABLE))
                return NULL;
        }

        ui->dragging = true;
        ui->drag_is_from = (button == LEFT_BUTTON);
        ui->sx = x;
        ui->sy = y;
        ui->dx = mx;
        ui->dy = my;
        ui->cshow = false;
        return UI_UPDATE;
    } else if (IS_MOUSE_DRAG(button) && ui->dragging) {
        ui->dx = mx;
        ui->dy = my;
        return UI_UPDATE;
    } else if (IS_MOUSE_RELEASE(button) && ui->dragging) {
        ui->dragging = false;
        if (ui->sx == x && ui->sy == y) return UI_UPDATE; /* single click */

        if (!INGRID(state, x, y)) {
            int si = ui->sy*w+ui->sx;
            if (state->prev[si] == -1 && state->next[si] == -1)
                return UI_UPDATE;
            sprintf(buf, "%c%d,%d",
                    (int)(ui->drag_is_from ? 'C' : 'X'), ui->sx, ui->sy);
            return dupstr(buf);
        }

        if (ui->drag_is_from) {
            if (!isvalidmove(state, false, ui->sx, ui->sy, x, y))
                return UI_UPDATE;
            sprintf(buf, "L%d,%d-%d,%d", ui->sx, ui->sy, x, y);
        } else {
            if (!isvalidmove(state, false, x, y, ui->sx, ui->sy))
                return UI_UPDATE;
            sprintf(buf, "L%d,%d-%d,%d", x, y, ui->sx, ui->sy);
        }
        return dupstr(buf);
    } /* else if (button == 'H' || button == 'h')
        return dupstr("H"); */
    else if ((button == 'x' || button == 'X') && ui->cshow) {
        int si = ui->cy*w + ui->cx;
        if (state->prev[si] == -1 && state->next[si] == -1)
            return UI_UPDATE;
        sprintf(buf, "%c%d,%d",
                (int)((button == 'x') ? 'C' : 'X'), ui->cx, ui->cy);
        return dupstr(buf);
    }

    return NULL;
}

static void unlink_cell(game_state *state, int si)
{
    debug(("Unlinking (%d,%d).", si%state->w, si/state->w));
    if (state->prev[si] != -1) {
        debug((" ... removing prev link from (%d,%d).",
               state->prev[si]%state->w, state->prev[si]/state->w));
        state->next[state->prev[si]] = -1;
        state->prev[si] = -1;
    }
    if (state->next[si] != -1) {
        debug((" ... removing next link to (%d,%d).",
               state->next[si]%state->w, state->next[si]/state->w));
        state->prev[state->next[si]] = -1;
        state->next[si] = -1;
    }
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = NULL;
    int sx, sy, ex, ey, si, ei, w = state->w;
    char c;

    debug(("move: %s", move));

    if (move[0] == 'S') {
        game_params p;
	game_state *tmp;
        const char *valid;
	int i;

        p.w = state->w; p.h = state->h;
        valid = validate_desc(&p, move+1);
        if (valid) {
            debug(("execute_move: move not valid: %s", valid));
            return NULL;
        }
	ret = dup_game(state);
        tmp = new_game(NULL, &p, move+1);
	for (i = 0; i < state->n; i++) {
	    ret->prev[i] = tmp->prev[i];
	    ret->next[i] = tmp->next[i];
	}
	free_game(tmp);
        ret->used_solve = true;
    } else if (sscanf(move, "L%d,%d-%d,%d", &sx, &sy, &ex, &ey) == 4) {
        if (!isvalidmove(state, false, sx, sy, ex, ey)) return NULL;

        ret = dup_game(state);

        si = sy*w+sx; ei = ey*w+ex;
        makelink(ret, si, ei);
    } else if (sscanf(move, "%c%d,%d", &c, &sx, &sy) == 3) {
        int sset;

        if (c != 'C' && c != 'X') return NULL;
        if (!INGRID(state, sx, sy)) return NULL;
        si = sy*w+sx;
        if (state->prev[si] == -1 && state->next[si] == -1)
            return NULL;

        ret = dup_game(state);

        sset = state->nums[si] / (state->n+1);
        if (c == 'C' || (c == 'X' && sset == 0)) {
            /* Unlink the single cell we dragged from the board. */
            unlink_cell(ret, si);
        } else {
            int i, set;
            for (i = 0; i < state->n; i++) {
                /* Unlink all cells in the same set as the one we dragged
                 * from the board. */

                if (state->nums[i] == 0) continue;
                set = state->nums[i] / (state->n+1);
                if (set != sset) continue;

                unlink_cell(ret, i);
            }
        }
    } else if (strcmp(move, "H") == 0) {
        ret = dup_game(state);
        solve_state(ret);
    }
    if (ret) {
        update_numbers(ret);
        if (check_completion(ret, true)) ret->completed = true;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize, order; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    assert(TILE_SIZE > 0);

    assert(!ds->dragb);
    ds->dragb = blitter_new(dr, BLITTER_SIZE, BLITTER_SIZE);
}

/* Colours chosen from the webby palette to work as a background to black text,
 * W then some plausible approximation to pastelly ROYGBIV; we then interpolate
 * between consecutive pairs to give another 8 (and then the drawing routine
 * will reuse backgrounds). */
static const unsigned long bgcols[8] = {
    0xffffff, /* white */
    0xffa07a, /* lightsalmon */
    0x98fb98, /* green */
    0x7fffd4, /* aquamarine */
    0x9370db, /* medium purple */
    0xffa500, /* orange */
    0x87cefa, /* lightskyblue */
    0xffff00, /* yellow */
};

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int c, i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_NUMBER * 3 + i] = 0.0F;
        ret[COL_ARROW * 3 + i] = 0.0F;
        ret[COL_CURSOR * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 2.0F;
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 1.3F;
    }
    ret[COL_NUMBER_SET * 3 + 0] = 0.0F;
    ret[COL_NUMBER_SET * 3 + 1] = 0.0F;
    ret[COL_NUMBER_SET * 3 + 2] = 0.9F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_DRAG_ORIGIN * 3 + 0] = 0.2F;
    ret[COL_DRAG_ORIGIN * 3 + 1] = 1.0F;
    ret[COL_DRAG_ORIGIN * 3 + 2] = 0.2F;

    for (c = 0; c < 8; c++) {
         ret[(COL_B0 + c) * 3 + 0] = (float)((bgcols[c] & 0xff0000) >> 16) / 256.0F;
         ret[(COL_B0 + c) * 3 + 1] = (float)((bgcols[c] & 0xff00) >> 8) / 256.0F;
         ret[(COL_B0 + c) * 3 + 2] = (float)((bgcols[c] & 0xff)) / 256.0F;
    }
    for (c = 0; c < 8; c++) {
        for (i = 0; i < 3; i++) {
           ret[(COL_B0 + 8 + c) * 3 + i] =
               (ret[(COL_B0 + c) * 3 + i] + ret[(COL_B0 + c + 1) * 3 + i]) / 2.0F;
        }
    }

#define average(r,a,b,w) do { \
    for (i = 0; i < 3; i++) \
	ret[(r)*3+i] = ret[(a)*3+i] + w * (ret[(b)*3+i] - ret[(a)*3+i]); \
} while (0)
    average(COL_ARROW_BG_DIM, COL_BACKGROUND, COL_ARROW, 0.1F);
    average(COL_NUMBER_SET_MID, COL_B0, COL_NUMBER_SET, 0.3F);
    for (c = 0; c < NBACKGROUNDS; c++) {
	/* I assume here that COL_ARROW and COL_NUMBER are the same.
	 * Otherwise I'd need two sets of COL_M*. */
	average(COL_M0 + c, COL_B0 + c, COL_NUMBER, 0.3F);
	average(COL_D0 + c, COL_B0 + c, COL_NUMBER, 0.1F);
	average(COL_X0 + c, COL_BACKGROUND, COL_B0 + c, 0.5F);
    }

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->started = false;
    ds->solved = false;
    ds->w = state->w;
    ds->h = state->h;
    ds->n = state->n;

    ds->nums = snewn(state->n, int);
    ds->dirp = snewn(state->n, int);
    ds->f = snewn(state->n, unsigned int);
    for (i = 0; i < state->n; i++) {
        ds->nums[i] = 0;
        ds->dirp[i] = -1;
        ds->f[i] = 0;
    }

    ds->angle_offset = 0.0F;

    ds->dragging = false;
    ds->dx = ds->dy = 0;
    ds->dragb = NULL;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->nums);
    sfree(ds->dirp);
    sfree(ds->f);
    if (ds->dragb) blitter_free(dr, ds->dragb);

    sfree(ds);
}

/* cx, cy are top-left corner. sz is the 'radius' of the arrow.
 * ang is in radians, clockwise from 0 == straight up. */
static void draw_arrow(drawing *dr, int cx, int cy, int sz, double ang,
                       int cfill, int cout)
{
    int coords[14];
    int xdx, ydx, xdy, ydy, xdx3, xdy3;
    double s = sin(ang), c = cos(ang);

    xdx3 = (int)(sz * (c/3 + 1) + 0.5) - sz;
    xdy3 = (int)(sz * (s/3 + 1) + 0.5) - sz;
    xdx = (int)(sz * (c + 1) + 0.5) - sz;
    xdy = (int)(sz * (s + 1) + 0.5) - sz;
    ydx = -xdy;
    ydy = xdx;


    coords[2*0 + 0] = cx - ydx;
    coords[2*0 + 1] = cy - ydy;
    coords[2*1 + 0] = cx + xdx;
    coords[2*1 + 1] = cy + xdy;
    coords[2*2 + 0] = cx + xdx3;
    coords[2*2 + 1] = cy + xdy3;
    coords[2*3 + 0] = cx + xdx3 + ydx;
    coords[2*3 + 1] = cy + xdy3 + ydy;
    coords[2*4 + 0] = cx - xdx3 + ydx;
    coords[2*4 + 1] = cy - xdy3 + ydy;
    coords[2*5 + 0] = cx - xdx3;
    coords[2*5 + 1] = cy - xdy3;
    coords[2*6 + 0] = cx - xdx;
    coords[2*6 + 1] = cy - xdy;

    draw_polygon(dr, coords, 7, cfill, cout);
}

static void draw_arrow_dir(drawing *dr, int cx, int cy, int sz, int dir,
                           int cfill, int cout, double angle_offset)
{
    double ang = 2.0 * PI * (double)dir / 8.0 + angle_offset;
    draw_arrow(dr, cx, cy, sz, ang, cfill, cout);
}

/* cx, cy are centre coordinates.. */
static void draw_star(drawing *dr, int cx, int cy, int rad, int npoints,
                      int cfill, int cout, double angle_offset)
{
    int *coords, n;
    double a, r;

    assert(npoints > 0);

    coords = snewn(npoints * 2 * 2, int);

    for (n = 0; n < npoints * 2; n++) {
        a = 2.0 * PI * ((double)n / ((double)npoints * 2.0)) + angle_offset;
        r = (n % 2) ? (double)rad/2.0 : (double)rad;

        /* We're rotating the point at (0, -r) by a degrees */
        coords[2*n+0] = cx + (int)( r * sin(a));
        coords[2*n+1] = cy + (int)(-r * cos(a));
    }
    draw_polygon(dr, coords, npoints*2, cfill, cout);
    sfree(coords);
}

static int num2col(game_drawstate *ds, int num)
{
    int set = num / (ds->n+1);

    if (num <= 0 || set == 0) return COL_B0;
    return COL_B0 + 1 + ((set-1) % 15);
}

#define ARROW_HALFSZ (7 * TILE_SIZE / 32)

#define F_CUR           0x001   /* Cursor on this tile. */
#define F_DRAG_SRC      0x002   /* Tile is source of a drag. */
#define F_ERROR         0x004   /* Tile marked in error. */
#define F_IMMUTABLE     0x008   /* Tile (number) is immutable. */
#define F_ARROW_POINT   0x010   /* Tile points to other tile */
#define F_ARROW_INPOINT 0x020   /* Other tile points in here. */
#define F_DIM           0x040   /* Tile is dim */

static void tile_redraw(drawing *dr, game_drawstate *ds, int tx, int ty,
                        int dir, int dirp, int num, unsigned int f,
                        double angle_offset, int print_ink)
{
    int cb = TILE_SIZE / 16, textsz;
    char buf[20];
    int arrowcol, sarrowcol, setcol, textcol;
    int acx, acy, asz;
    bool empty = false;

    if (num == 0 && !(f & F_ARROW_POINT) && !(f & F_ARROW_INPOINT)) {
        empty = true;
        /*
         * We don't display text in empty cells: typically these are
         * signified by num=0. However, in some cases a cell could
         * have had the number 0 assigned to it if the user made an
         * error (e.g. tried to connect a chain of length 5 to the
         * immutable number 4) so we _do_ display the 0 if the cell
         * has a link in or a link out.
         */
    }

    /* Calculate colours. */

    if (print_ink >= 0) {
	/*
	 * We're printing, so just do everything in black.
	 */
	arrowcol = textcol = print_ink;
	setcol = sarrowcol = -1;       /* placate optimiser */
    } else {

	setcol = empty ? COL_BACKGROUND : num2col(ds, num);

#define dim(fg,bg) ( \
      (bg)==COL_BACKGROUND ? COL_ARROW_BG_DIM : \
      (bg) + COL_D0 - COL_B0 \
    )

#define mid(fg,bg) ( \
      (fg)==COL_NUMBER_SET ? COL_NUMBER_SET_MID : \
      (bg) + COL_M0 - COL_B0 \
    )

#define dimbg(bg) ( \
      (bg)==COL_BACKGROUND ? COL_BACKGROUND : \
      (bg) + COL_X0 - COL_B0 \
    )

	if (f & F_DRAG_SRC) arrowcol = COL_DRAG_ORIGIN;
	else if (f & F_DIM) arrowcol = dim(COL_ARROW, setcol);
	else if (f & F_ARROW_POINT) arrowcol = mid(COL_ARROW, setcol);
	else arrowcol = COL_ARROW;

	if ((f & F_ERROR) && !(f & F_IMMUTABLE)) textcol = COL_ERROR;
	else {
	    if (f & F_IMMUTABLE) textcol = COL_NUMBER_SET;
	    else textcol = COL_NUMBER;

	    if (f & F_DIM) textcol = dim(textcol, setcol);
	    else if (((f & F_ARROW_POINT) || num==ds->n) &&
		     ((f & F_ARROW_INPOINT) || num==1))
		textcol = mid(textcol, setcol);
	}

	if (f & F_DIM) sarrowcol = dim(COL_ARROW, setcol);
	else sarrowcol = COL_ARROW;
    }

    /* Clear tile background */

    if (print_ink < 0) {
	draw_rect(dr, tx, ty, TILE_SIZE, TILE_SIZE,
		  (f & F_DIM) ? dimbg(setcol) : setcol);
    }

    /* Draw large (outwards-pointing) arrow. */

    asz = ARROW_HALFSZ;         /* 'radius' of arrow/star. */
    acx = tx+TILE_SIZE/2+asz;   /* centre x */
    acy = ty+TILE_SIZE/2+asz;   /* centre y */

    if (num == ds->n && (f & F_IMMUTABLE))
        draw_star(dr, acx, acy, asz, 5, arrowcol, arrowcol, angle_offset);
    else
        draw_arrow_dir(dr, acx, acy, asz, dir, arrowcol, arrowcol, angle_offset);
    if (print_ink < 0 && (f & F_CUR))
        draw_rect_corners(dr, acx, acy, asz+1, COL_CURSOR);

    /* Draw dot iff this tile requires a predecessor and doesn't have one. */

    if (print_ink < 0) {
	acx = tx+TILE_SIZE/2-asz;
	acy = ty+TILE_SIZE/2+asz;

	if (!(f & F_ARROW_INPOINT) && num != 1) {
	    draw_circle(dr, acx, acy, asz / 4, sarrowcol, sarrowcol);
	}
    }

    /* Draw text (number or set). */

    if (!empty) {
        int set = (num <= 0) ? 0 : num / (ds->n+1);

        char *p = buf;
        if (set == 0 || num <= 0) {
            sprintf(buf, "%d", num);
        } else {
            int n = num % (ds->n+1);
            p += sizeof(buf) - 1;

            if (n != 0) {
                sprintf(buf, "+%d", n);  /* Just to get the length... */
                p -= strlen(buf);
                sprintf(p, "+%d", n);
            } else {
                *p = '\0';
            }
            do {
                set--;
                p--;
                *p = (char)((set % 26)+'a');
                set /= 26;
            } while (set);
        }
        textsz = min(2*asz, (TILE_SIZE - 2 * cb) / (int)strlen(p));
        draw_text(dr, tx + cb, ty + TILE_SIZE/4, FONT_VARIABLE, textsz,
                  ALIGN_VCENTRE | ALIGN_HLEFT, textcol, p);
    }

    if (print_ink < 0) {
	draw_rect_outline(dr, tx, ty, TILE_SIZE, TILE_SIZE, COL_GRID);
	draw_update(dr, tx, ty, TILE_SIZE, TILE_SIZE);
    }
}

static void draw_drag_indicator(drawing *dr, game_drawstate *ds,
                                const game_state *state, const game_ui *ui,
                                bool validdrag)
{
    int dir, w = ds->w, acol = COL_ARROW;
    int fx = FROMCOORD(ui->dx), fy = FROMCOORD(ui->dy);
    double ang;

    if (validdrag) {
        /* If we could move here, lock the arrow to the appropriate direction. */
        dir = ui->drag_is_from ? state->dirs[ui->sy*w+ui->sx] : state->dirs[fy*w+fx];

        ang = (2.0 * PI * dir) / 8.0; /* similar to calculation in draw_arrow_dir. */
    } else {
        /* Draw an arrow pointing away from/towards the origin cell. */
        int ox = COORD(ui->sx) + TILE_SIZE/2, oy = COORD(ui->sy) + TILE_SIZE/2;
        double tana, offset;
        double xdiff = abs(ox - ui->dx), ydiff = abs(oy - ui->dy);

        if (xdiff == 0) {
            ang = (oy > ui->dy) ? 0.0F : PI;
        } else if (ydiff == 0) {
            ang = (ox > ui->dx) ? 3.0F*PI/2.0F : PI/2.0F;
        } else {
            if (ui->dx > ox && ui->dy < oy) {
                tana = xdiff / ydiff;
                offset = 0.0F;
            } else if (ui->dx > ox && ui->dy > oy) {
                tana = ydiff / xdiff;
                offset = PI/2.0F;
            } else if (ui->dx < ox && ui->dy > oy) {
                tana = xdiff / ydiff;
                offset = PI;
            } else {
                tana = ydiff / xdiff;
                offset = 3.0F * PI / 2.0F;
            }
            ang = atan(tana) + offset;
        }

        if (!ui->drag_is_from) ang += PI; /* point to origin, not away from. */

    }
    draw_arrow(dr, ui->dx, ui->dy, ARROW_HALFSZ, ang, acol, acol);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y, i, w = ds->w, dirp;
    bool force = false;
    unsigned int f;
    double angle_offset = 0.0;
    game_state *postdrop = NULL;

    if (flashtime > 0.0F)
        angle_offset = 2.0 * PI * (flashtime / FLASH_SPIN);
    if (angle_offset != ds->angle_offset) {
        ds->angle_offset = angle_offset;
        force = true;
    }

    if (ds->dragging) {
        assert(ds->dragb);
        blitter_load(dr, ds->dragb, ds->dx, ds->dy);
        draw_update(dr, ds->dx, ds->dy, BLITTER_SIZE, BLITTER_SIZE);
        ds->dragging = false;
    }

    /* If an in-progress drag would make a valid move if finished, we
     * reflect that move in the board display. We let interpret_move do
     * most of the heavy lifting for us: we have to copy the game_ui so
     * as not to stomp on the real UI's drag state. */
    if (ui->dragging) {
        game_ui uicopy = *ui;
        char *movestr = interpret_move(state, &uicopy, ds, ui->dx, ui->dy, LEFT_RELEASE);

        if (movestr != NULL && strcmp(movestr, "") != 0) {
            postdrop = execute_move(state, movestr);
            sfree(movestr);

            state = postdrop;
        }
    }

    if (!ds->started) {
        int aw = TILE_SIZE * state->w;
        int ah = TILE_SIZE * state->h;
        draw_rect_outline(dr, BORDER - 1, BORDER - 1, aw + 2, ah + 2, COL_GRID);
        draw_update(dr, 0, 0, aw + 2 * BORDER, ah + 2 * BORDER);
    }
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*w + x;
            f = 0;
            dirp = -1;

            if (ui->cshow && x == ui->cx && y == ui->cy)
                f |= F_CUR;

            if (ui->dragging) {
                if (x == ui->sx && y == ui->sy)
                    f |= F_DRAG_SRC;
                else if (ui->drag_is_from) {
                    if (!ispointing(state, ui->sx, ui->sy, x, y))
                        f |= F_DIM;
                } else {
                    if (!ispointing(state, x, y, ui->sx, ui->sy))
                        f |= F_DIM;
                }
            }

            if (state->impossible ||
                state->nums[i] < 0 || state->flags[i] & FLAG_ERROR)
                f |= F_ERROR;
            if (state->flags[i] & FLAG_IMMUTABLE)
                f |= F_IMMUTABLE;

            if (state->next[i] != -1)
                f |= F_ARROW_POINT;

            if (state->prev[i] != -1) {
                /* Currently the direction here is from our square _back_
                 * to its previous. We could change this to give the opposite
                 * sense to the direction. */
                f |= F_ARROW_INPOINT;
                dirp = whichdir(x, y, state->prev[i]%w, state->prev[i]/w);
            }

            if (state->nums[i] != ds->nums[i] ||
                f != ds->f[i] || dirp != ds->dirp[i] ||
                force || !ds->started) {
                int sign;
                {
                    /*
                     * Trivial and foolish configurable option done on
                     * purest whim. With this option enabled, the
                     * victory flash is done by rotating each square
                     * in the opposite direction from its immediate
                     * neighbours, so that they behave like a field of
                     * interlocking gears. With it disabled, they all
                     * rotate in the same direction. Choose for
                     * yourself which is more brain-twisting :-)
                     */
                    static int gear_mode = -1;
                    if (gear_mode < 0)
                        gear_mode = getenv_bool("SIGNPOST_GEARS", false);
                    if (gear_mode)
                        sign = 1 - 2 * ((x ^ y) & 1);
                    else
                        sign = 1;
                }
                tile_redraw(dr, ds,
                            BORDER + x * TILE_SIZE,
                            BORDER + y * TILE_SIZE,
                            state->dirs[i], dirp, state->nums[i], f,
                            sign * angle_offset, -1);
                ds->nums[i] = state->nums[i];
                ds->f[i] = f;
                ds->dirp[i] = dirp;
            }
        }
    }
    if (ui->dragging) {
        ds->dragging = true;
        ds->dx = ui->dx - BLITTER_SIZE/2;
        ds->dy = ui->dy - BLITTER_SIZE/2;
        blitter_save(dr, ds->dragb, ds->dx, ds->dy);

        draw_drag_indicator(dr, ds, state, ui, postdrop != NULL);
    }
    if (postdrop) free_game(postdrop);
    if (!ds->started) ds->started = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed &&
        newstate->completed && !newstate->used_solve)
        return FLASH_SPIN;
    else
        return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cshow) {
        *x = COORD(ui->cx);
        *y = COORD(ui->cy);
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

    game_compute_size(params, 1300, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int x, y;

    /* Fake up just enough of a drawstate */
    game_drawstate ads, *ds = &ads;
    ds->tilesize = tilesize;
    ds->n = state->n;

    /*
     * Border and grid.
     */
    print_line_width(dr, TILE_SIZE / 40);
    for (x = 1; x < state->w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(state->h), ink);
    for (y = 1; y < state->h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(state->w), COORD(y), ink);
    print_line_width(dr, 2*TILE_SIZE / 40);
    draw_rect_outline(dr, COORD(0), COORD(0), TILE_SIZE*state->w,
		      TILE_SIZE*state->h, ink);

    /*
     * Arrows and numbers.
     */
    print_line_width(dr, 0);
    for (y = 0; y < state->h; y++)
	for (x = 0; x < state->w; x++)
	    tile_redraw(dr, ds, COORD(x), COORD(y), state->dirs[y*state->w+x],
			0, state->nums[y*state->w+x], 0, 0.0, ink);
}

#ifdef COMBINED
#define thegame signpost
#endif

const struct game thegame = {
    "Signpost", "games.signpost", "signpost",
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
    REQUIRE_RBUTTON,		       /* flags */
};

#ifdef STANDALONE_SOLVER

#include <time.h>
#include <stdarg.h>

static const char *quis = NULL;

static void usage(FILE *out) {
    fprintf(out, "usage: %s [--stdin] [--soak] [--seed SEED] <params>|<game id>\n", quis);
}

static void cycle_seed(char **seedstr, random_state *rs)
{
    char newseed[16];
    int j;

    newseed[15] = '\0';
    newseed[0] = '1' + (char)random_upto(rs, 9);
    for (j = 1; j < 15; j++)
        newseed[j] = '0' + (char)random_upto(rs, 10);
    sfree(*seedstr);
    *seedstr = dupstr(newseed);
}

static void start_soak(game_params *p, char *seedstr)
{
    time_t tt_start, tt_now, tt_last;
    char *desc, *aux;
    random_state *rs;
    long n = 0, nnums = 0, i;
    game_state *state;

    tt_start = tt_now = time(NULL);
    printf("Soak-generating a %dx%d grid.\n", p->w, p->h);

    while (1) {
       rs = random_new(seedstr, strlen(seedstr));
       desc = thegame.new_desc(p, rs, &aux, 0);

       state = thegame.new_game(NULL, p, desc);
       for (i = 0; i < state->n; i++) {
           if (state->flags[i] & FLAG_IMMUTABLE)
               nnums++;
       }
       thegame.free_game(state);

       sfree(desc);
       cycle_seed(&seedstr, rs);
       random_free(rs);

       n++;
       tt_last = time(NULL);
       if (tt_last > tt_now) {
           tt_now = tt_last;
           printf("%ld total, %3.1f/s, %3.1f nums/grid (%3.1f%%).\n",
                  n,
                  (double)n / ((double)tt_now - tt_start),
                  (double)nnums / (double)n,
                  ((double)nnums * 100.0) / ((double)n * (double)p->w * (double)p->h) );
       }
    }
}

static void process_desc(char *id)
{
    char *desc, *solvestr;
    const char *err;
    game_params *p;
    game_state *s;

    printf("%s\n  ", id);

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: expecting game description.", quis);
        exit(1);
    }

    *desc++ = '\0';

    p = thegame.default_params();
    thegame.decode_params(p, id);
    err = thegame.validate_params(p, 1);
    if (err) {
        fprintf(stderr, "%s: %s", quis, err);
        thegame.free_params(p);
        return;
    }

    err = thegame.validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\nDescription: %s\n", quis, err, desc);
        thegame.free_params(p);
        return;
    }

    s = thegame.new_game(NULL, p, desc);

    solvestr = thegame.solve(s, s, NULL, &err);
    if (!solvestr)
        fprintf(stderr, "%s\n", err);
    else
        printf("Puzzle is soluble.\n");

    thegame.free_game(s);
    thegame.free_params(p);
}

int main(int argc, char *argv[])
{
    char *id = NULL, *desc, *aux = NULL;
    const char *err;
    bool soak = false, verbose = false, stdin_desc = false;
    int n = 1, i;
    char *seedstr = NULL, newseed[16];

    setvbuf(stdout, NULL, _IONBF, 0);

    quis = argv[0];
    while (--argc > 0) {
        char *p = (char*)(*++argv);
        if (!strcmp(p, "-v") || !strcmp(p, "--verbose"))
            verbose = true;
        else if (!strcmp(p, "--stdin"))
            stdin_desc = true;
        else if (!strcmp(p, "-e") || !strcmp(p, "--seed")) {
            seedstr = dupstr(*++argv);
            argc--;
        } else if (!strcmp(p, "-n") || !strcmp(p, "--number")) {
            n = atoi(*++argv);
            argc--;
        } else if (!strcmp(p, "-s") || !strcmp(p, "--soak")) {
            soak = true;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            usage(stderr);
            exit(1);
        } else {
            id = p;
        }
    }

    sprintf(newseed, "%lu", (unsigned long) time(NULL));
    seedstr = dupstr(newseed);

    if (id || !stdin_desc) {
        if (id && strchr(id, ':')) {
            /* Parameters and description passed on cmd-line:
             * try and solve it. */
            process_desc(id);
        } else {
            /* No description passed on cmd-line: decode parameters
             * (with optional seed too) */

            game_params *p = thegame.default_params();

            if (id) {
                char *cmdseed = strchr(id, '#');
                if (cmdseed) {
                    *cmdseed++ = '\0';
                    sfree(seedstr);
                    seedstr = dupstr(cmdseed);
                }

                thegame.decode_params(p, id);
            }

            err = thegame.validate_params(p, 1);
            if (err) {
                fprintf(stderr, "%s: %s", quis, err);
                thegame.free_params(p);
                exit(1);
            }

            /* We have a set of valid parameters; either soak with it
             * or generate a single game description and print to stdout. */
            if (soak)
                start_soak(p, seedstr);
            else {
                char *pstring = thegame.encode_params(p, 0);

                for (i = 0; i < n; i++) {
                    random_state *rs = random_new(seedstr, strlen(seedstr));

                    if (verbose) printf("%s#%s\n", pstring, seedstr);
                    desc = thegame.new_desc(p, rs, &aux, 0);
                    printf("%s:%s\n", pstring, desc);
                    sfree(desc);

                    cycle_seed(&seedstr, rs);

                    random_free(rs);
                }

                sfree(pstring);
            }
            thegame.free_params(p);
        }
    }

    if (stdin_desc) {
        char buf[4096];

        while (fgets(buf, sizeof(buf), stdin)) {
	    buf[strcspn(buf, "\r\n")] = '\0';
            process_desc(buf);
        }
    }
    sfree(seedstr);

    return 0;
}

#endif


/* vim: set shiftwidth=4 tabstop=8: */
