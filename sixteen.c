/*
 * sixteen.c: `16-puzzle', a sliding-tiles jigsaw which differs
 * from the 15-puzzle in that you toroidally rotate a row or column
 * at a time.
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
#define BORDER TILE_SIZE
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + 2*TILE_SIZE) / TILE_SIZE - 2 )

#define ANIM_TIME 0.13F
#define FLASH_FRAME 0.13F

#define X(state, i) ( (i) % (state)->w )
#define Y(state, i) ( (i) / (state)->w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

#define TILE_CURSOR(i, state, x, y) ((i) == C((state), (x), (y)) &&     \
                                     0 <= (x) && (x) < (state)->w &&    \
                                     0 <= (y) && (y) < (state)->h)
enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,
    NCOLOURS
};

struct game_params {
    int w, h;
    int movetarget;
};

struct game_state {
    int w, h, n;
    int *tiles;
    int completed;
    bool used_solve;           /* used to suppress completion flash */
    int movecount, movetarget;
    int last_movement_sense;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 4;
    ret->movetarget = 0;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
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
      default: return false;
    }

    sprintf(buf, "%dx%d", w, h);
    *name = dupstr(buf);
    *params = ret = snew(game_params);
    ret->w = w;
    ret->h = h;
    ret->movetarget = 0;
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
    ret->movetarget = 0;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
	while (*string && isdigit((unsigned char)*string))
	    string++;
    }
    if (*string == 'm') {
        string++;
        ret->movetarget = atoi(string);
	while (*string && isdigit((unsigned char)*string))
	    string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);
    /* Shuffle limit is part of the limited parameters, because we have to
     * supply the target move count. */
    if (params->movetarget)
        sprintf(data + strlen(data), "m%d", params->movetarget);

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

    ret[2].name = "Number of shuffling moves";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->movetarget);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->movetarget = atoi(cfg[2].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 || params->h < 2)
	return "Width and height must both be at least two";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";

    return NULL;
}

static int perm_parity(int *perm, int n)
{
    int i, j, ret;

    ret = 0;

    for (i = 0; i < n-1; i++)
        for (j = i+1; j < n; j++)
            if (perm[i] > perm[j])
                ret = !ret;

    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int stop, n, i, x;
    int x1, x2, p1, p2;
    int *tiles;
    bool *used;
    char *ret;
    int retlen;

    n = params->w * params->h;

    tiles = snewn(n, int);

    if (params->movetarget) {
	int prevoffset = -1;
        int max = (params->w > params->h ? params->w : params->h);
        int *prevmoves = snewn(max, int);

	/*
	 * Shuffle the old-fashioned way, by making a series of
	 * single moves on the grid.
	 */

	for (i = 0; i < n; i++)
	    tiles[i] = i;

	for (i = 0; i < params->movetarget; i++) {
	    int start, offset, len, direction, index;
	    int j, tmp;

	    /*
	     * Choose a move to make. We can choose from any row
	     * or any column.
	     */
	    while (1) {
		j = random_upto(rs, params->w + params->h);

		if (j < params->w) {
		    /* Column. */
                    index = j;
		    start = j;
		    offset = params->w;
		    len = params->h;
		} else {
		    /* Row. */
                    index = j - params->w;
		    start = index * params->w;
		    offset = 1;
		    len = params->w;
		}

		direction = -1 + 2 * random_upto(rs, 2);

		/*
		 * To at least _try_ to avoid boring cases, check
		 * that this move doesn't directly undo a previous
		 * one, or repeat it so many times as to turn it
		 * into fewer moves in the opposite direction. (For
		 * example, in a row of length 4, we're allowed to
		 * move it the same way twice, but not three
		 * times.)
                 * 
                 * We track this for each individual row/column,
                 * and clear all the counters as soon as a
                 * perpendicular move is made. This isn't perfect
                 * (it _can't_ guaranteeably be perfect - there
                 * will always come a move count beyond which a
                 * shorter solution will be possible than the one
                 * which constructed the position) but it should
                 * sort out all the obvious cases.
		 */
                if (offset == prevoffset) {
                    tmp = prevmoves[index] + direction;
                    if (abs(2*tmp) > len || abs(tmp) < abs(prevmoves[index]))
                        continue;
                }

		/* If we didn't `continue', we've found an OK move to make. */
                if (offset != prevoffset) {
                    int i;
                    for (i = 0; i < max; i++)
                        prevmoves[i] = 0;
                    prevoffset = offset;
                }
                prevmoves[index] += direction;
		break;
	    }

	    /*
	     * Make the move.
	     */
	    if (direction < 0) {
		start += (len-1) * offset;
		offset = -offset;
	    }
	    tmp = tiles[start];
	    for (j = 0; j+1 < len; j++)
		tiles[start + j*offset] = tiles[start + (j+1)*offset];
	    tiles[start + (len-1) * offset] = tmp;
	}

        sfree(prevmoves);

    } else {

	used = snewn(n, bool);

	for (i = 0; i < n; i++) {
	    tiles[i] = -1;
	    used[i] = false;
	}

	/*
	 * If both dimensions are odd, there is a parity
	 * constraint.
	 */
	if (params->w & params->h & 1)
	    stop = 2;
	else
	    stop = 0;

	/*
	 * Place everything except (possibly) the last two tiles.
	 */
	for (x = 0, i = n; i > stop; i--) {
	    int k = i > 1 ? random_upto(rs, i) : 0;
	    int j;

	    for (j = 0; j < n; j++)
		if (!used[j] && (k-- == 0))
		    break;

	    assert(j < n && !used[j]);
	    used[j] = true;

	    while (tiles[x] >= 0)
		x++;
	    assert(x < n);
	    tiles[x] = j;
	}

	if (stop) {
	    /*
	     * Find the last two locations, and the last two
	     * pieces.
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
	     * Try the last two tiles one way round. If that fails,
	     * swap them.
	     */
	    tiles[x1] = p1;
	    tiles[x2] = p2;
	    if (perm_parity(tiles, n) != 0) {
		tiles[x1] = p2;
		tiles[x2] = p1;
		assert(perm_parity(tiles, n) == 0);
	    }
	}

	sfree(used);
    }

    /*
     * Now construct the game description, by describing the tile
     * array as a simple sequence of comma-separated integers.
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

    return ret;
}


static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *p, *err;
    int i, area;
    bool *used;

    area = params->w * params->h;
    p = desc;
    err = NULL;

    used = snewn(area, bool);
    for (i = 0; i < area; i++)
	used[i] = false;

    for (i = 0; i < area; i++) {
	const char *q = p;
	int n;

	if (*p < '0' || *p > '9') {
	    err = "Not enough numbers in string";
	    goto leave;
	}
	while (*p >= '0' && *p <= '9')
	    p++;
	if (i < area-1 && *p != ',') {
	    err = "Expected comma after number";
	    goto leave;
	}
	else if (i == area-1 && *p) {
	    err = "Excess junk at end of string";
	    goto leave;
	}
	n = atoi(q);
	if (n < 1 || n > area) {
	    err = "Number out of range";
	    goto leave;
	}
	if (used[n-1]) {
	    err = "Number used twice";
	    goto leave;
	}
	used[n-1] = true;

	if (*p) p++;		       /* eat comma */
    }

    leave:
    sfree(used);
    return err;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int i;
    const char *p;

    state->w = params->w;
    state->h = params->h;
    state->n = params->w * params->h;
    state->tiles = snewn(state->n, int);

    p = desc;
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
    state->movetarget = params->movetarget;
    state->used_solve = false;
    state->last_movement_sense = 0;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->tiles = snewn(state->w * state->h, int);
    memcpy(ret->tiles, state->tiles, state->w * state->h * sizeof(int));
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->movetarget = state->movetarget;
    ret->used_solve = state->used_solve;
    ret->last_movement_sense = state->last_movement_sense;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->tiles);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return dupstr("S");
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char *ret, *p, buf[80];
    int x, y, col, maxlen;

    /*
     * First work out how many characters we need to display each
     * number.
     */
    col = sprintf(buf, "%d", state->n);

    /*
     * Now we know the exact total size of the grid we're going to
     * produce: it's got h rows, each containing w lots of col, w-1
     * spaces and a trailing newline.
     */
    maxlen = state->h * state->w * (col+1);

    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->h; y++) {
	for (x = 0; x < state->w; x++) {
	    int v = state->tiles[state->w*y+x];
	    sprintf(buf, "%*d", col, v);
	    memcpy(p, buf, col);
	    p += col;
	    if (x+1 == state->w)
		*p++ = '\n';
	    else
		*p++ = ' ';
	}
    }

    assert(p - ret == maxlen);
    *p = '\0';
    return ret;
}

enum cursor_mode { unlocked, lock_tile, lock_position };

struct game_ui {
    int cur_x, cur_y;
    bool cur_visible;
    enum cursor_mode cur_mode;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = 0;
    ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->cur_mode = unlocked;

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

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cur_visible) {
        if (ui->cur_x == -1 || ui->cur_x == state->w ||
                ui->cur_y == -1 || ui->cur_y == state->h)
            return button == CURSOR_SELECT2 ? "Back" : "Slide";
        if (button == CURSOR_SELECT)
            return ui->cur_mode == lock_tile ? "Unlock" : "Lock tile";
        if (button == CURSOR_SELECT2)
            return ui->cur_mode == lock_position ? "Unlock" : "Lock pos";
    }
    return "";
}

struct game_drawstate {
    bool started;
    int w, h, bgcolour;
    int *tiles;
    int tilesize;
    int cur_x, cur_y;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int cx = -1, cy = -1, dx, dy;
    char buf[80];
    bool shift = button & MOD_SHFT, control = button & MOD_CTRL;
    int pad = button & MOD_NUM_KEYPAD;

    button &= ~MOD_MASK;

    if (IS_CURSOR_MOVE(button) || pad) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }

        if (control || shift || ui->cur_mode) {
            int x = ui->cur_x, y = ui->cur_y, xwrap = x, ywrap = y;
            if (x < 0 || x >= state->w || y < 0 || y >= state->h)
                return NULL;
            move_cursor(button | pad, &x, &y,
                        state->w, state->h, false);
            move_cursor(button | pad, &xwrap, &ywrap,
                        state->w, state->h, true);

            if (x != xwrap) {
                sprintf(buf, "R%d,%c1", y, x ? '+' : '-');
            } else if (y != ywrap) {
                sprintf(buf, "C%d,%c1", x, y ? '+' : '-');
            } else if (x == ui->cur_x)
                sprintf(buf, "C%d,%d", x, y - ui->cur_y);
            else
                sprintf(buf, "R%d,%d", y, x - ui->cur_x);

            if (control || (!shift && ui->cur_mode == lock_tile)) {
                ui->cur_x = xwrap;
                ui->cur_y = ywrap;
            }

            return dupstr(buf);
        } else {
            int x = ui->cur_x + 1, y = ui->cur_y + 1;

            move_cursor(button | pad, &x, &y,
                        state->w + 2, state->h + 2, false);

            if (x == 0 && y == 0) {
                int t = ui->cur_x;
                ui->cur_x = ui->cur_y;
                ui->cur_y = t;
            } else if (x == 0 && y == state->h + 1) {
                int t = ui->cur_x;
                ui->cur_x = (state->h - 1) - ui->cur_y;
                ui->cur_y = (state->h - 1) - t;
            } else if (x == state->w + 1 && y == 0) {
                int t = ui->cur_x;
                ui->cur_x = (state->w - 1) - ui->cur_y;
                ui->cur_y = (state->w - 1) - t;
            } else if (x == state->w + 1 && y == state->h + 1) {
                int t = ui->cur_x;
                ui->cur_x = state->w - state->h + ui->cur_y;
                ui->cur_y = state->h - state->w + t;
            } else {
                ui->cur_x = x - 1;
                ui->cur_y = y - 1;
            }

            ui->cur_visible = true;
            return UI_UPDATE;
        }
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        cx = FROMCOORD(x);
        cy = FROMCOORD(y);
        ui->cur_visible = false;
    } else if (IS_CURSOR_SELECT(button)) {
        if (ui->cur_visible) {
            if (ui->cur_x == -1 || ui->cur_x == state->w ||
                ui->cur_y == -1 || ui->cur_y == state->h) {
                cx = ui->cur_x;
                cy = ui->cur_y;
            } else {
                const enum cursor_mode m = (button == CURSOR_SELECT2 ?
                                            lock_position : lock_tile);
                ui->cur_mode = (ui->cur_mode == m ? unlocked : m);
                return UI_UPDATE;
            }
        } else {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
    } else {
	return NULL;
    }

    if (cx == -1 && cy >= 0 && cy < state->h)
        dx = -1, dy = 0;
    else if (cx == state->w && cy >= 0 && cy < state->h)
        dx = +1, dy = 0;
    else if (cy == -1 && cx >= 0 && cx < state->w)
        dy = -1, dx = 0;
    else if (cy == state->h && cx >= 0 && cx < state->w)
        dy = +1, dx = 0;
    else
        return UI_UPDATE;            /* invalid click location */

    /* reverse direction if right hand button is pressed */
    if (button == RIGHT_BUTTON || button == CURSOR_SELECT2) {
        dx = -dx;
        dy = -dy;
    }

    if (dx)
	sprintf(buf, "R%d,%d", cy, dx);
    else
	sprintf(buf, "C%d,%d", cx, dy);
    return dupstr(buf);
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int cx, cy, dx, dy;
    int tx, ty, n;
    game_state *ret;

    if (!strcmp(move, "S")) {
	int i;

	ret = dup_game(from);

	/*
	 * Simply replace the grid with a solved one. For this game,
	 * this isn't a useful operation for actually telling the user
	 * what they should have done, but it is useful for
	 * conveniently being able to get hold of a clean state from
	 * which to practise manoeuvres.
	 */
	for (i = 0; i < ret->n; i++)
	    ret->tiles[i] = i+1;
	ret->used_solve = true;
	ret->completed = ret->movecount = 1;

	return ret;
    }

    if (move[0] == 'R' && sscanf(move+1, "%d,%d", &cy, &dx) == 2 &&
	cy >= 0 && cy < from->h && -from->h <= dx && dx <= from->w ) {
	cx = dy = 0;
	n = from->w;
    } else if (move[0] == 'C' && sscanf(move+1, "%d,%d", &cx, &dy) == 2 &&
	       cx >= 0 && cx < from->w && -from->h <= dy && dy <= from->h) {
	cy = dx = 0;
	n = from->h;
    } else
	return NULL;

    ret = dup_game(from);

    do {
        tx = (cx - dx + from->w) % from->w;
        ty = (cy - dy + from->h) % from->h;
        ret->tiles[C(ret, cx, cy)] = from->tiles[C(from, tx, ty)];
        cx = tx;
        cy = ty;
    } while (--n > 0);

    ret->movecount++;

    ret->last_movement_sense = dx+dy;

    /*
     * See if the game has been completed.
     */
    if (!ret->completed) {
        ret->completed = ret->movecount;
        for (n = 0; n < ret->n; n++)
            if (ret->tiles[n] != n+1)
                ret->completed = 0;
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
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++)
        ret[COL_TEXT * 3 + i] = 0.0;

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
    ds->bgcolour = COL_BACKGROUND;
    ds->tiles = snewn(ds->w*ds->h, int);
    ds->tilesize = 0;                  /* haven't decided yet */
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = -1;
    ds->cur_x = ds->cur_y = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds,
                      const game_state *state, int x, int y,
                      int tile, int flash_colour)
{
    if (tile == 0) {
        draw_rect(dr, x, y, TILE_SIZE, TILE_SIZE,
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
        draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);

        coords[0] = x;
        coords[1] = y;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);

        draw_rect(dr, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
                  TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
                  flash_colour);

        sprintf(str, "%d", tile);
        draw_text(dr, x + TILE_SIZE/2, y + TILE_SIZE/2,
                  FONT_VARIABLE, TILE_SIZE/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
                  COL_TEXT, str);
    }
    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static void draw_arrow(drawing *dr, game_drawstate *ds,
                       int x, int y, int xdx, int xdy, bool cur)
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

    draw_polygon(dr, coords, 7, cur ? COL_HIGHLIGHT : COL_LOWLIGHT, COL_TEXT);
}

static void draw_arrow_for_cursor(drawing *dr, game_drawstate *ds,
                                  int cur_x, int cur_y, bool cur)
{
    if (cur_x == -1 && cur_y == -1)
        return; /* 'no cursur here */
    else if (cur_x == -1) /* LH column. */
        draw_arrow(dr, ds, COORD(0), COORD(cur_y+1), 0, -1, cur);
    else if (cur_x == ds->w) /* RH column */
        draw_arrow(dr, ds, COORD(ds->w), COORD(cur_y), 0, +1, cur);
    else if (cur_y == -1) /* Top row */
        draw_arrow(dr, ds, COORD(cur_x), COORD(0), +1, 0, cur);
    else if (cur_y == ds->h) /* Bottom row */
        draw_arrow(dr, ds, COORD(cur_x+1), COORD(ds->h), -1, 0, cur);
    else
        return;

    draw_update(dr, COORD(cur_x), COORD(cur_y),
                TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, bgcolour;
    int cur_x = -1, cur_y = -1;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        bgcolour = (frame % 2 ? COL_LOWLIGHT : COL_HIGHLIGHT);
    } else
        bgcolour = COL_BACKGROUND;

    if (!ds->started) {
        int coords[10];

        /*
         * Recessed area containing the whole puzzle.
         */
        coords[0] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[1] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[2] = COORD(state->w) + HIGHLIGHT_WIDTH - 1;
        coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[4] = coords[2] - TILE_SIZE;
        coords[5] = coords[3] + TILE_SIZE;
        coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[9] = COORD(state->h) + HIGHLIGHT_WIDTH - 1;
        coords[6] = coords[8] + TILE_SIZE;
        coords[7] = coords[9] - TILE_SIZE;
        draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

        coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
        coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
        draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);

        /*
         * Arrows for making moves.
         */
        for (i = 0; i < state->w; i++) {
            draw_arrow(dr, ds, COORD(i), COORD(0), +1, 0, false);
            draw_arrow(dr, ds, COORD(i+1), COORD(state->h), -1, 0, false);
        }
        for (i = 0; i < state->h; i++) {
            draw_arrow(dr, ds, COORD(state->w), COORD(i), 0, +1, false);
            draw_arrow(dr, ds, COORD(0), COORD(i+1), 0, -1, false);
        }

        ds->started = true;
    }
    /*
     * Cursor (highlighted arrow around edge)
     */
    if (ui->cur_visible) {
        cur_x = ui->cur_x; cur_y = ui->cur_y;
    }

    if (cur_x != ds->cur_x || cur_y != ds->cur_y) {
        /* Cursor has changed; redraw two (prev and curr) arrows. */
        draw_arrow_for_cursor(dr, ds, cur_x, cur_y, true);
        draw_arrow_for_cursor(dr, ds, ds->cur_x, ds->cur_y, false);
    }

    /*
     * Now draw each tile.
     */

    clip(dr, COORD(0), COORD(0), TILE_SIZE*state->w, TILE_SIZE*state->h);

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
            ds->tiles[i] != t || ds->tiles[i] == -1 || t == -1 ||
            ((ds->cur_x != cur_x || ds->cur_y != cur_y) && /* cursor moved */
             (TILE_CURSOR(i, state, ds->cur_x, ds->cur_y) ||
              TILE_CURSOR(i, state, cur_x, cur_y)))) {
            int x, y, x2, y2;

	    /*
	     * Figure out what to _actually_ draw, and where to
	     * draw it.
	     */
	    if (t == -1) {
		int x0, y0, x1, y1, dx, dy;
		int j;
		float c;
		int sense;

		if (dir < 0) {
		    assert(oldstate);
		    sense = -oldstate->last_movement_sense;
		} else {
		    sense = state->last_movement_sense;
		}

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
		    dx != TILE_SIZE * sense) {
		    dx = (dx < 0 ? dx + TILE_SIZE * state->w :
			  dx - TILE_SIZE * state->w);
		    assert(abs(dx) == TILE_SIZE);
		}
		dy = (y1 - y0);
		if (dy != 0 &&
		    dy != TILE_SIZE * sense) {
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
	    } else {
		x = COORD(X(state, i));
		y = COORD(Y(state, i));
		x2 = y2 = -1;
	    }

	    draw_tile(dr, ds, state, x, y, t,
		      (x2 == -1 && TILE_CURSOR(i, state, cur_x, cur_y)) ?
                      COL_LOWLIGHT : bgcolour);

	    if (x2 != -1 || y2 != -1)
		draw_tile(dr, ds, state, x2, y2, t, bgcolour);
	}
	ds->tiles[i] = t0;
    }

    ds->cur_x = cur_x;
    ds->cur_y = cur_y;

    unclip(dr);

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

	if (state->used_solve)
	    sprintf(statusbuf, "Moves since auto-solve: %d",
		    state->movecount - state->completed);
	else {
	    sprintf(statusbuf, "%sMoves: %d",
		    (state->completed ? "COMPLETED! " : ""),
		    (state->completed ? state->completed : state->movecount));
            if (state->movetarget)
                sprintf(statusbuf+strlen(statusbuf), " (target %d)",
                        state->movetarget);
	}

	status_bar(dr, statusbuf);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return ANIM_TIME;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->used_solve && !newstate->used_solve)
        return 2 * FLASH_FRAME;
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
        *x = COORD(ui->cur_x);
        *y = COORD(ui->cur_y);
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

#ifdef COMBINED
#define thegame sixteen
#endif

const struct game thegame = {
    "Sixteen", "games.sixteen", "sixteen",
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
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
