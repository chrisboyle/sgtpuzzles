/*
 * fifteen.c: standard 15-puzzle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"

const char *const game_name = "Fifteen";
const int game_can_configure = TRUE;

#define TILE_SIZE 48
#define BORDER    (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define ANIM_TIME 0.1F
#define FLASH_FRAME 0.1F

#define X(state, i) ( (i) % (state)->w )
#define Y(state, i) ( (i) / (state)->w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    NCOLOURS
};

struct game_params {
    int w, h;
};

struct game_state {
    int w, h, n;
    int *tiles;
    int gap_pos;
    int completed;
    int movecount;
};

game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 4;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
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

config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

char *validate_params(game_params *params)
{
    if (params->w < 2 && params->h < 2)
	return "Width and height must both be at least two";

    return NULL;
}

int perm_parity(int *perm, int n)
{
    int i, j, ret;

    ret = 0;

    for (i = 0; i < n-1; i++)
        for (j = i+1; j < n; j++)
            if (perm[i] > perm[j])
                ret = !ret;

    return ret;
}

char *new_game_seed(game_params *params)
{
    int gap, n, i, x;
    int x1, x2, p1, p2, parity;
    int *tiles, *used;
    char *ret;
    int retlen;

    n = params->w * params->h;

    tiles = snewn(n, int);
    used = snewn(n, int);

    for (i = 0; i < n; i++) {
        tiles[i] = -1;
        used[i] = FALSE;
    }

    gap = rand_upto(n);
    tiles[gap] = 0;
    used[0] = TRUE;

    /*
     * Place everything else except the last two tiles.
     */
    for (x = 0, i = n-1; i > 2; i--) {
        int k = rand_upto(i);
        int j;

        for (j = 0; j < n; j++)
            if (!used[j] && (k-- == 0))
                break;

        assert(j < n && !used[j]);
        used[j] = TRUE;

        while (tiles[x] >= 0)
            x++;
        assert(x < n);
        tiles[x] = j;
    }

    /*
     * Find the last two locations, and the last two pieces.
     */
    while (tiles[x] >= 0)
        x++;
    assert(x < n);
    x1 = x;
    x++;
    while (tiles[x] >= 0)
        x++;
    assert(x < n);
    x2 = x;

    for (i = 0; i < n; i++)
        if (!used[i])
            break;
    p1 = i;
    for (i = p1+1; i < n; i++)
        if (!used[i])
            break;
    p2 = i;

    /*
     * Determine the required parity of the overall permutation.
     * This is the XOR of:
     * 
     *  - The chessboard parity ((x^y)&1) of the gap square. The
     *    bottom right, and therefore also the top left, count as
     *    even.
     * 
     *  - The parity of n. (The target permutation is 1,...,n-1,0
     *    rather than 0,...,n-1; this is a cyclic permutation of
     *    the starting point and hence is odd iff n is even.)
     */
    parity = (X(params, gap) ^ Y(params, gap) ^ (n+1)) & 1;

    /*
     * Try the last two tiles one way round. If that fails, swap
     * them.
     */
    tiles[x1] = p1;
    tiles[x2] = p2;
    if (perm_parity(tiles, n) != parity) {
        tiles[x1] = p2;
        tiles[x2] = p1;
        assert(perm_parity(tiles, n) == parity);
    }

    /*
     * Now construct the game seed, by describing the tile array as
     * a simple sequence of comma-separated integers.
     */
    ret = NULL;
    retlen = 0;
    for (i = 0; i < n; i++) {
        char buf[80];
        int k;

        k = sprintf(buf, "%d,", tiles[i]);

        ret = sresize(ret, retlen + k + 1, char);
        strcpy(ret + retlen, buf);
        retlen += k;
    }
    ret[retlen-1] = '\0';              /* delete last comma */

    sfree(tiles);
    sfree(used);

    return ret;
}

game_state *new_game(game_params *params, char *seed)
{
    game_state *state = snew(game_state);
    int i;
    char *p;

    state->w = params->w;
    state->h = params->h;
    state->n = params->w * params->h;
    state->tiles = snewn(state->n, int);

    state->gap_pos = 0;
    p = seed;
    i = 0;
    for (i = 0; i < state->n; i++) {
        assert(*p);
        state->tiles[i] = atoi(p);
        if (state->tiles[i] == 0)
            state->gap_pos = i;
        while (*p && *p != ',')
            p++;
        if (*p) p++;                   /* eat comma */
    }
    assert(!*p);
    assert(state->tiles[state->gap_pos] == 0);

    state->completed = state->movecount = 0;

    return state;
}

game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->tiles = snewn(state->w * state->h, int);
    memcpy(ret->tiles, state->tiles, state->w * state->h * sizeof(int));
    ret->gap_pos = state->gap_pos;
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
    int gx, gy, dx, dy, ux, uy, up, p;
    game_state *ret;

    gx = X(from, from->gap_pos);
    gy = Y(from, from->gap_pos);

    if (button == CURSOR_RIGHT && gx > 0)
        dx = gx - 1, dy = gy;
    else if (button == CURSOR_LEFT && gx < from->w-1)
        dx = gx + 1, dy = gy;
    else if (button == CURSOR_DOWN && gy > 0)
        dy = gy - 1, dx = gx;
    else if (button == CURSOR_UP && gy < from->h-1)
        dy = gy + 1, dx = gx;
    else if (button == LEFT_BUTTON) {
        dx = FROMCOORD(x);
        dy = FROMCOORD(y);
        if (dx < 0 || dx >= from->w || dy < 0 || dy >= from->h)
            return NULL;               /* out of bounds */
        /*
         * Any click location should be equal to the gap location
         * in _precisely_ one coordinate.
         */
        if ((dx == gx && dy == gy) || (dx != gx && dy != gy))
            return NULL;
    } else
        return NULL;                   /* no move */

    /*
     * Find the unit displacement from the original gap
     * position towards this one.
     */
    ux = (dx < gx ? -1 : dx > gx ? +1 : 0);
    uy = (dy < gy ? -1 : dy > gy ? +1 : 0);
    up = C(from, ux, uy);

    ret = dup_game(from);

    ret->gap_pos = C(from, dx, dy);
    assert(ret->gap_pos >= 0 && ret->gap_pos < ret->n);

    ret->tiles[ret->gap_pos] = 0;

    for (p = from->gap_pos; p != ret->gap_pos; p += up) {
        assert(p >= 0 && p < from->n);
        ret->tiles[p] = from->tiles[p + up];
	ret->movecount++;
    }

    /*
     * See if the game has been completed.
     */
    if (!ret->completed) {
        ret->completed = ret->movecount;
        for (p = 0; p < ret->n; p++)
            if (ret->tiles[p] != (p < ret->n-1 ? p+1 : 0))
                ret->completed = 0;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

struct game_drawstate {
    int started;
    int w, h, bgcolour;
    int *tiles;
};

void game_size(game_params *params, int *x, int *y)
{
    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;
    float max;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /*
     * Drop the background colour so that the highlight is
     * noticeably brighter than it while still being under 1.
     */
    max = ret[COL_BACKGROUND*3];
    for (i = 1; i < 3; i++)
        if (ret[COL_BACKGROUND*3+i] > max)
            max = ret[COL_BACKGROUND*3+i];
    if (max * 1.2F > 1.0F) {
        for (i = 0; i < 3; i++)
            ret[COL_BACKGROUND*3+i] /= (max * 1.2F);
    }

    for (i = 0; i < 3; i++) {
        ret[COL_HIGHLIGHT * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 1.2F;
        ret[COL_LOWLIGHT * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.8F;
        ret[COL_TEXT * 3 + i] = 0.0;
    }

    *ncolours = NCOLOURS;
    return ret;
}

game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->bgcolour = COL_BACKGROUND;
    ds->tiles = snewn(ds->w*ds->h, int);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = -1;

    return ds;
}

void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

static void draw_tile(frontend *fe, game_state *state, int x, int y,
                      int tile, int flash_colour)
{
    if (tile == 0) {
        draw_rect(fe, x, y, TILE_SIZE, TILE_SIZE,
                  flash_colour);
    } else {
        int coords[6];
        char str[40];

        coords[0] = x + TILE_SIZE - 1;
        coords[1] = y + TILE_SIZE - 1;
        coords[2] = x + TILE_SIZE - 1;
        coords[3] = y;
        coords[4] = x;
        coords[5] = y + TILE_SIZE - 1;
        draw_polygon(fe, coords, 3, TRUE, COL_LOWLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_LOWLIGHT);

        coords[0] = x;
        coords[1] = y;
        draw_polygon(fe, coords, 3, TRUE, COL_HIGHLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_HIGHLIGHT);

        draw_rect(fe, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
                  TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
                  flash_colour);

        sprintf(str, "%d", tile);
        draw_text(fe, x + TILE_SIZE/2, y + TILE_SIZE/2,
                  FONT_VARIABLE, TILE_SIZE/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
                  COL_TEXT, str);
    }
    draw_update(fe, x, y, TILE_SIZE, TILE_SIZE);
}

void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *state, float animtime, float flashtime)
{
    int i, pass, bgcolour;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
        bgcolour = COL_BACKGROUND;

    if (!ds->started) {
        int coords[6];

	draw_rect(fe, 0, 0,
		  TILE_SIZE * state->w + 2 * BORDER,
		  TILE_SIZE * state->h + 2 * BORDER, COL_BACKGROUND);
	draw_update(fe, 0, 0,
		    TILE_SIZE * state->w + 2 * BORDER,
		    TILE_SIZE * state->h + 2 * BORDER);

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[4] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[5] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        draw_polygon(fe, coords, 3, TRUE, COL_HIGHLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_HIGHLIGHT);

        coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
        draw_polygon(fe, coords, 3, TRUE, COL_LOWLIGHT);
        draw_polygon(fe, coords, 3, FALSE, COL_LOWLIGHT);

        ds->started = TRUE;
    }

    /*
     * Now draw each tile. We do this in two passes to make
     * animation easy.
     */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < state->n; i++) {
            int t, t0;
            /*
             * Figure out what should be displayed at this
             * location. It's either a simple tile, or it's a
             * transition between two tiles (in which case we say
             * -1 because it must always be drawn).
             */

            if (oldstate && oldstate->tiles[i] != state->tiles[i])
                t = -1;
            else
                t = state->tiles[i];

            t0 = t;

            if (ds->bgcolour != bgcolour ||   /* always redraw when flashing */
                ds->tiles[i] != t || ds->tiles[i] == -1 || t == -1) {
                int x, y;

                /*
                 * Figure out what to _actually_ draw, and where to
                 * draw it.
                 */
                if (t == -1) {
                    int x0, y0, x1, y1;
                    int j;

                    /*
                     * On the first pass, just blank the tile.
                     */
                    if (pass == 0) {
                        x = COORD(X(state, i));
                        y = COORD(Y(state, i));
                        t = 0;
                    } else {
                        float c;

                        t = state->tiles[i];

                        /*
                         * Don't bother moving the gap; just don't
                         * draw it.
                         */
                        if (t == 0)
                            continue;

                        /*
                         * Find the coordinates of this tile in the old and
                         * new states.
                         */
                        x1 = COORD(X(state, i));
                        y1 = COORD(Y(state, i));
                        for (j = 0; j < oldstate->n; j++)
                            if (oldstate->tiles[j] == state->tiles[i])
                                break;
                        assert(j < oldstate->n);
                        x0 = COORD(X(state, j));
                        y0 = COORD(Y(state, j));

                        c = (animtime / ANIM_TIME);
                        if (c < 0.0F) c = 0.0F;
                        if (c > 1.0F) c = 1.0F;

                        x = x0 + (int)(c * (x1 - x0));
                        y = y0 + (int)(c * (y1 - y0));
                    }

                } else {
                    if (pass == 0)
                        continue;
                    x = COORD(X(state, i));
                    y = COORD(Y(state, i));
                }

                draw_tile(fe, state, x, y, t, bgcolour);
            }
            ds->tiles[i] = t0;
        }
    }
    ds->bgcolour = bgcolour;

    /*
     * Update the status bar.
     */
    {
	char statusbuf[256];

        /*
         * Don't show the new status until we're also showing the
         * new _state_ - after the game animation is complete.
         */
        if (oldstate)
            state = oldstate;

	sprintf(statusbuf, "%sMoves: %d",
		(state->completed ? "COMPLETED! " : ""),
		(state->completed ? state->completed : state->movecount));

	status_bar(fe, statusbuf);
    }
}

float game_anim_length(game_state *oldstate, game_state *newstate)
{
    return ANIM_TIME;
}

float game_flash_length(game_state *oldstate, game_state *newstate)
{
    if (!oldstate->completed && newstate->completed)
        return 2 * FLASH_FRAME;
    else
        return 0.0F;
}

int game_wants_statusbar(void)
{
    return TRUE;
}
