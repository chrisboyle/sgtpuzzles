/*
 * sixteen.c: `16-puzzle', a sliding-tiles jigsaw which differs
 * from the 15-puzzle in that you toroidally rotate a row or column
 * at a time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"

const char *const game_name = "Sixteen";
const int game_can_configure = TRUE;

#define TILE_SIZE 48
#define BORDER    TILE_SIZE            /* big border to fill with arrows */
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + 2*TILE_SIZE) / TILE_SIZE - 2 )

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
    int completed;
    int movecount;
    int last_movement_sense;
};

game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 4;

    return ret;
}

int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    int w, h;
    char buf[80];

    switch (i) {
      case 0: w = 3, h = 3; break;
      case 1: w = 4, h = 3; break;
      case 2: w = 4, h = 4; break;
      case 3: w = 5, h = 4; break;
      case 4: w = 5, h = 5; break;
      default: return FALSE;
    }

    sprintf(buf, "%dx%d", w, h);
    *name = dupstr(buf);
    *params = ret = snew(game_params);
    ret->w = w;
    ret->h = h;
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
    int stop, n, i, x;
    int x1, x2, p1, p2;
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

    /*
     * If both dimensions are odd, there is a parity constraint.
     */
    if (params->w & params->h & 1)
        stop = 2;
    else
        stop = 0;

    /*
     * Place everything except (possibly) the last two tiles.
     */
    for (x = 0, i = n; i > stop; i--) {
        int k = i > 1 ? rand_upto(i) : 0;
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

    if (stop) {
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
         * Try the last two tiles one way round. If that fails, swap
         * them.
         */
        tiles[x1] = p1;
        tiles[x2] = p2;
        if (perm_parity(tiles, n) != 0) {
            tiles[x1] = p2;
            tiles[x2] = p1;
            assert(perm_parity(tiles, n) == 0);
        }
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

        k = sprintf(buf, "%d,", tiles[i]+1);

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

    p = seed;
    i = 0;
    for (i = 0; i < state->n; i++) {
        assert(*p);
        state->tiles[i] = atoi(p);
        while (*p && *p != ',')
            p++;
        if (*p) p++;                   /* eat comma */
    }
    assert(!*p);

    state->completed = state->movecount = 0;
    state->last_movement_sense = 0;

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
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->last_movement_sense = state->last_movement_sense;

    return ret;
}

void free_game(game_state *state)
{
    sfree(state);
}

game_state *make_move(game_state *from, int x, int y, int button)
{
    int cx, cy;
    int dx, dy, tx, ty, n;
    game_state *ret;

    if (button != LEFT_BUTTON)
        return NULL;

    cx = FROMCOORD(x);
    cy = FROMCOORD(y);
    if (cx == -1 && cy >= 0 && cy < from->h)
        n = from->w, dx = +1, dy = 0;
    else if (cx == from->w && cy >= 0 && cy < from->h)
        n = from->w, dx = -1, dy = 0;
    else if (cy == -1 && cx >= 0 && cx < from->w)
        n = from->h, dy = +1, dx = 0;
    else if (cy == from->h && cx >= 0 && cx < from->w)
        n = from->h, dy = -1, dx = 0;
    else
        return NULL;                   /* invalid click location */

    ret = dup_game(from);

    do {
        cx += dx;
        cy += dy;
        tx = (cx + dx + from->w) % from->w;
        ty = (cy + dy + from->h) % from->h;
        ret->tiles[C(ret, cx, cy)] = from->tiles[C(from, tx, ty)];
    } while (--n > 0);

    ret->movecount++;

    ret->last_movement_sense = -(dx+dy);

    /*
     * See if the game has been completed.
     */
    if (!ret->completed) {
        ret->completed = ret->movecount;
        for (n = 0; n < ret->n; n++)
            if (ret->tiles[n] != n+1)
                ret->completed = FALSE;
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

static void draw_arrow(frontend *fe, int x, int y, int xdx, int xdy)
{
    int coords[14];
    int ydy = -xdx, ydx = xdy;

#define POINT(n, xx, yy) ( \
    coords[2*(n)+0] = x + (xx)*xdx + (yy)*ydx, \
    coords[2*(n)+1] = y + (xx)*xdy + (yy)*ydy)

    POINT(0, TILE_SIZE / 2, 3 * TILE_SIZE / 4);   /* top of arrow */
    POINT(1, 3 * TILE_SIZE / 4, TILE_SIZE / 2);   /* right corner */
    POINT(2, 5 * TILE_SIZE / 8, TILE_SIZE / 2);   /* right concave */
    POINT(3, 5 * TILE_SIZE / 8, TILE_SIZE / 4);   /* bottom right */
    POINT(4, 3 * TILE_SIZE / 8, TILE_SIZE / 4);   /* bottom left */
    POINT(5, 3 * TILE_SIZE / 8, TILE_SIZE / 2);   /* left concave */
    POINT(6,     TILE_SIZE / 4, TILE_SIZE / 2);   /* left corner */

    draw_polygon(fe, coords, 7, TRUE, COL_LOWLIGHT);
    draw_polygon(fe, coords, 7, FALSE, COL_TEXT);
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

        /*
         * Arrows for making moves.
         */
        for (i = 0; i < state->w; i++) {
            draw_arrow(fe, COORD(i), COORD(0), +1, 0);
            draw_arrow(fe, COORD(i+1), COORD(state->h), -1, 0);
        }
        for (i = 0; i < state->h; i++) {
            draw_arrow(fe, COORD(state->w), COORD(i), 0, +1);
            draw_arrow(fe, COORD(0), COORD(i+1), 0, -1);
        }

        ds->started = TRUE;
    }

    /*
     * Now draw each tile. We do this in two passes to make
     * animation easy.
     */

    clip(fe, COORD(0), COORD(0), TILE_SIZE*state->w, TILE_SIZE*state->h);

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
                int x, y, x2, y2;

                /*
                 * Figure out what to _actually_ draw, and where to
                 * draw it.
                 */
                if (t == -1) {
                    int x0, y0, x1, y1, dx, dy;
                    int j;

                    /*
                     * On the first pass, just blank the tile.
                     */
                    if (pass == 0) {
                        x = COORD(X(state, i));
                        y = COORD(Y(state, i));
                        x2 = y2 = -1;
                        t = 0;
                    } else {
                        float c;

                        t = state->tiles[i];

                        /*
                         * FIXME: must be prepared to draw a double
                         * tile in some situations.
                         */

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

                        dx = (x1 - x0);
                        if (dx != 0 &&
			    dx != TILE_SIZE * state->last_movement_sense) {
                            dx = (dx < 0 ? dx + TILE_SIZE * state->w :
                                  dx - TILE_SIZE * state->w);
                            assert(abs(dx) == TILE_SIZE);
                        }
                        dy = (y1 - y0);
                        if (dy != 0 &&
			    dy != TILE_SIZE * state->last_movement_sense) {
                            dy = (dy < 0 ? dy + TILE_SIZE * state->h :
                                  dy - TILE_SIZE * state->h);
                            assert(abs(dy) == TILE_SIZE);
                        }

                        c = (animtime / ANIM_TIME);
                        if (c < 0.0F) c = 0.0F;
                        if (c > 1.0F) c = 1.0F;

                        x = x0 + (int)(c * dx);
                        y = y0 + (int)(c * dy);
                        x2 = x1 - dx + (int)(c * dx);
                        y2 = y1 - dy + (int)(c * dy);
                    }

                } else {
                    if (pass == 0)
                        continue;
                    x = COORD(X(state, i));
                    y = COORD(Y(state, i));
                    x2 = y2 = -1;
                }

                draw_tile(fe, state, x, y, t, bgcolour);
                if (x2 != -1 || y2 != -1)
                    draw_tile(fe, state, x2, y2, t, bgcolour);
            }
            ds->tiles[i] = t0;
        }
    }

    unclip(fe);

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
