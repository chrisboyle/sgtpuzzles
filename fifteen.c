/*
 * fifteen.c: standard 15-puzzle.
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
#define BORDER    (TILE_SIZE / 2)
#define HIGHLIGHT_WIDTH (TILE_SIZE / 20)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define ANIM_TIME 0.13F
#define FLASH_FRAME 0.13F

#define X(state, i) ( (i) % (state)->w )
#define Y(state, i) ( (i) / (state)->w )
#define C(state, x, y) ( (y) * (state)->w + (x) )

#define PARITY_P(params, gap) (((X((params), (gap)) - ((params)->w - 1)) ^ \
                                (Y((params), (gap)) - ((params)->h - 1)) ^ \
                                (((params)->w * (params)->h) + 1)) & 1)
#define PARITY_S(state) PARITY_P((state), ((state)->gap_pos))

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
    int completed;             /* move count at time of completion */
    bool used_solve;           /* used to suppress completion flash */
    int movecount;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 4;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    if (i == 0) {
	*params = default_params();
	*name = dupstr("4x4");
	return true;
    }
    return false;
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
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "%dx%d", params->w, params->h);

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = NULL;
    ret[2].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);

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

static int is_completed(int *tiles, int n) {
    int p;
    for (p = 0; p < n; p++)
        if (tiles[p] != (p < n-1 ? p+1 : 0))
            return 0;
    return 1;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int gap, n, i, x;
    int x1, x2, p1, p2, parity;
    int *tiles;
    bool *used;
    char *ret;
    int retlen;

    n = params->w * params->h;

    tiles = snewn(n, int);
    used = snewn(n, bool);

    do {
        for (i = 0; i < n; i++) {
            tiles[i] = -1;
            used[i] = false;
        }

        gap = random_upto(rs, n);
        tiles[gap] = 0;
        used[0] = true;

        /*
         * Place everything else except the last two tiles.
         */
        for (x = 0, i = n - 1; i > 2; i--) {
            int k = random_upto(rs, i);
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
        for (i = p1 + 1; i < n; i++)
            if (!used[i])
                break;
        p2 = i;

        /*
         * Determine the required parity of the overall permutation.
         * This is the XOR of:
         *
         * 	- The chessboard parity ((x^y)&1) of the gap square. The
         * 	  bottom right counts as even.
         *
         *  - The parity of n. (The target permutation is 1,...,n-1,0
         *    rather than 0,...,n-1; this is a cyclic permutation of
         *    the starting point and hence is odd iff n is even.)
         */
        parity = PARITY_P(params, gap);

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
    } while (is_completed(tiles, n));

    /*
     * Now construct the game description, by describing the tile
     * array as a simple sequence of comma-separated integers.
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

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *p;
    const char *err;
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
	if (n < 0 || n >= area) {
	    err = "Number out of range";
	    goto leave;
	}
	if (used[n]) {
	    err = "Number used twice";
	    goto leave;
	}
	used[n] = true;

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

    state->gap_pos = 0;
    p = desc;
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
    state->used_solve = false;

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
    ret->gap_pos = state->gap_pos;
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->used_solve = state->used_solve;

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
    col = sprintf(buf, "%d", state->n-1);

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
	    if (v == 0)
		sprintf(buf, "%*s", col, "");
	    else
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

static game_ui *new_ui(const game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
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

struct game_drawstate {
    bool started;
    int w, h, bgcolour;
    int *tiles;
    int tilesize;
};

static int flip_cursor(int button)
{
    switch (button) {
    case CURSOR_UP: return CURSOR_DOWN;
    case CURSOR_DOWN: return CURSOR_UP;
    case CURSOR_LEFT: return CURSOR_RIGHT;
    case CURSOR_RIGHT: return CURSOR_LEFT;
    }
    return 0;
}

static void next_move_3x2(int ax, int ay, int bx, int by,
                          int gx, int gy, int *dx, int *dy)
{
    /* When w = 3 and h = 2 and the tile going in the top left corner
     * is at (ax, ay) and the tile going in the bottom left corner is
     * at (bx, by) and the blank tile is at (gx, gy), how do you move? */

    /* Hard-coded shortest solutions.  Sorry. */
    static const unsigned char move[120] = {
        1,2,0,1,2,2,
        2,0,0,2,0,0,
        0,0,2,0,2,0,
        0,0,0,2,0,2,
        2,0,0,0,2,0,

        0,3,0,1,1,1,
        3,0,3,2,1,2,
        2,1,1,0,1,0,
        2,1,2,1,0,1,
        1,2,0,2,1,2,

        0,1,3,1,3,0,
        1,3,1,3,0,3,
        0,0,3,3,0,0,
        0,0,0,1,2,1,
        3,0,0,1,1,1,

        3,1,1,1,3,0,
        1,1,1,1,1,1,
        1,3,1,1,3,0,
        1,1,3,3,1,3,
        1,3,0,0,0,0
    };
    static const struct { int dx, dy; } d[4] = {{+1,0},{-1,0},{0,+1},{0,-1}};

    int ea = 3*ay + ax, eb = 3*by + bx, eg = 3*gy + gx, v;
    if (eb > ea) --eb;
    if (eg > ea) --eg;
    if (eg > eb) --eg;
    v = move[ea + eb*6 + eg*5*6];
    *dx = d[v].dx;
    *dy = d[v].dy;
}

static void next_move(int nx, int ny, int ox, int oy, int gx, int gy,
                      int tx, int ty, int w, int *dx, int *dy)
{
    const int to_tile_x = (gx < nx ? +1 : -1);
    const int to_goal_x = (gx < tx ? +1 : -1);
    const bool gap_x_on_goal_side = ((nx-tx) * (nx-gx) > 0);

    assert (nx != tx || ny != ty); /* not already in place */
    assert (nx != gx || ny != gy); /* not placing the gap */
    assert (ty <= ny); /* because we're greedy (and flipping) */
    assert (ty <= gy); /* because we're greedy (and flipping) */

    /* TODO: define a termination function.  Idea: 0 if solved, or
     * the number of moves to solve the next piece plus the number of
     * further unsolved pieces times an upper bound on the number of
     * moves required to solve any piece.  If such a function can be
     * found, we have (termination && (termination => correctness)).
     * The catch is our temporary disturbance of 2x3 corners. */

    /* handles end-of-row, when 3 and 4 are in the top right 2x3 box */
    if (tx == w - 2 &&
        ny <= ty + 2 && (nx == tx || nx == tx + 1) &&
        oy <= ty + 2 && (ox == tx || ox == tx + 1) &&
        gy <= ty + 2 && (gx == tx || gx == tx + 1))
    {
        next_move_3x2(oy - ty, tx + 1 - ox,
                      ny - ty, tx + 1 - nx,
                      gy - ty, tx + 1 - gx, dy, dx);
        *dx *= -1;
        return;
    }

    if (tx == w - 1) {
        if (ny <= ty + 2 && (nx == tx || nx == tx - 1) &&
            gy <= ty + 2 && (gx == tx || gx == tx - 1)) {
            next_move_3x2(ny - ty, tx - nx, 0, 1, gy - ty, tx - gx, dy, dx);
            *dx *= -1;
        } else if (gy == ty)
            *dy = +1;
        else if (nx != tx || ny != ty + 1) {
            next_move((w - 1) - nx, ny, -1, -1, (w - 1) - gx, gy,
                      0, ty + 1, -1, dx, dy);
            *dx *= -1;
        } else if (gx == nx)
            *dy = -1;
        else
            *dx = +1;
        return;
    }

    /* note that *dy = -1 is unsafe when gy = ty + 1 and gx < tx */
    if (gy < ny)
        if (nx == gx || (gy == ty && gx == tx))
            *dy = +1;
        else if (!gap_x_on_goal_side)
            *dx = to_tile_x;
        else if (ny - ty > abs(nx - tx))
            *dx = to_tile_x;
        else *dy = +1;

    else if (gy == ny)
        if (nx == tx) /* then we know ny > ty */
            if (gx > nx || ny > ty + 1)
                *dy = -1; /* ... so this is safe */
            else
                *dy = +1;
        else if (gap_x_on_goal_side)
            *dx = to_tile_x;
        else if (gy == ty || (gy == ty + 1 && gx < tx))
            *dy = +1;
        else
            *dy = -1;

    else if (nx == tx) /* gy > ny */
        if (gx > nx)
            *dy = -1;
        else
            *dx = +1;
    else if (gx == nx)
        *dx = to_goal_x;
    else if (gap_x_on_goal_side)
        if (gy == ty + 1 && gx < tx)
            *dx = to_tile_x;
        else
            *dy = -1;

    else if (ny - ty > abs(nx - tx))
        *dy = -1;
    else
        *dx = to_tile_x;
}

static bool compute_hint(const game_state *state, int *out_x, int *out_y)
{
    /* The overall solving process is this:
     * 1. Find the next piece to be put in its place
     * 2. Move it diagonally towards its place
     * 3. Move it horizontally or vertically towards its place
     * (Modulo the last two tiles at the end of each row/column)
     */

    int gx = X(state, state->gap_pos);
    int gy = Y(state, state->gap_pos);

    int tx, ty, nx, ny, ox, oy, /* {target,next,next2}_{x,y} */ i;
    int dx = 0, dy = 0;

    /* 1. Find the next piece
     * if (there are no more unfinished columns than rows) {
     *     fill the top-most row, left to right
     * } else { fill the left-most column, top to bottom }
     */
    const int w = state->w, h = state->h, n = w*h;
    int next_piece = 0, next_piece_2 = 0, solr = 0, solc = 0;
    int unsolved_rows = h, unsolved_cols = w;

    assert(out_x);
    assert(out_y);

    while (solr < h && solc < w) {
        int start, step, stop;
        if (unsolved_cols <= unsolved_rows)
            start = solr*w + solc, step = 1, stop = unsolved_cols;
        else
            start = solr*w + solc, step = w, stop = unsolved_rows;
        for (i = 0; i < stop; ++i) {
            const int j = start + i*step;
            if (state->tiles[j] != j + 1) {
                next_piece = j + 1;
                next_piece_2 = next_piece + step;
                break;
            }
        }
        if (i < stop) break;

        (unsolved_cols <= unsolved_rows)
            ? (++solr, --unsolved_rows)
            : (++solc, --unsolved_cols);
    }

    if (next_piece == n)
        return false;

    /* 2, 3. Move the next piece towards its place */

    /* gx, gy already set */
    tx = X(state, next_piece - 1); /* where we're going */
    ty = Y(state, next_piece - 1);
    for (i = 0; i < n && state->tiles[i] != next_piece; ++i);
    nx = X(state, i); /* where we're at */
    ny = Y(state, i);
    for (i = 0; i < n && state->tiles[i] != next_piece_2; ++i);
    ox = X(state, i);
    oy = Y(state, i);

    if (unsolved_cols <= unsolved_rows)
        next_move(nx, ny, ox, oy, gx, gy, tx, ty, w, &dx, &dy);
    else
        next_move(ny, nx, oy, ox, gy, gx, ty, tx, h, &dy, &dx);

    assert (dx || dy);

    *out_x = gx + dx;
    *out_y = gy + dy;
    return true;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int cx = X(state, state->gap_pos), nx = cx;
    int cy = Y(state, state->gap_pos), ny = cy;
    char buf[80];

    button &= ~MOD_MASK;

    if (button == LEFT_BUTTON) {
        nx = FROMCOORD(x);
        ny = FROMCOORD(y);
        if (nx < 0 || nx >= state->w || ny < 0 || ny >= state->h)
            return NULL;               /* out of bounds */
    } else if (IS_CURSOR_MOVE(button)) {
        static int invert_cursor = -1;
        if (invert_cursor == -1)
            invert_cursor = getenv_bool("FIFTEEN_INVERT_CURSOR", false);
        button = flip_cursor(button); /* the default */
        if (invert_cursor)
            button = flip_cursor(button); /* undoes the first flip */
	move_cursor(button, &nx, &ny, state->w, state->h, false);
    } else if ((button == 'h' || button == 'H') && !state->completed) {
        if (!compute_hint(state, &nx, &ny))
            return NULL; /* shouldn't happen, since ^^we^^checked^^ */
    } else
        return NULL;                   /* no move */

    /*
     * Any click location should be equal to the gap location
     * in _precisely_ one coordinate.
     */
    if ((cx == nx) ^ (cy == ny)) {
	sprintf(buf, "M%d,%d", nx, ny);
	return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int gx, gy, dx, dy, ux, uy, up, p;
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
	    ret->tiles[i] = (i+1) % ret->n;
	ret->gap_pos = ret->n-1;
	ret->used_solve = true;
	ret->completed = ret->movecount = 1;

	return ret;
    }

    gx = X(from, from->gap_pos);
    gy = Y(from, from->gap_pos);

    if (move[0] != 'M' ||
	sscanf(move+1, "%d,%d", &dx, &dy) != 2 ||
	(dx == gx && dy == gy) || (dx != gx && dy != gy) ||
	dx < 0 || dx >= from->w || dy < 0 || dy >= from->h)
	return NULL;

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
    if (!ret->completed && is_completed(ret->tiles, ret->n)) {
        ret->completed = ret->movecount;
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

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, int tile, int flash_colour)
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

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, pass, bgcolour;

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

        ds->started = true;
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

                draw_tile(dr, ds, state, x, y, t, bgcolour);
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

	if (state->used_solve)
	    sprintf(statusbuf, "Moves since auto-solve: %d",
		    state->movecount - state->completed);
	else
	    sprintf(statusbuf, "%sMoves: %d",
		    (state->completed ? "COMPLETED! " : ""),
		    (state->completed ? state->completed : state->movecount));

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
    *x = COORD(X(state, state->gap_pos));
    *y = COORD(Y(state, state->gap_pos));
    *w = *h = TILE_SIZE;
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

#ifdef COMBINED
#define thegame fifteen
#endif

const struct game thegame = {
    "Fifteen", "games.fifteen", "fifteen",
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

#ifdef STANDALONE_SOLVER

int main(int argc, char **argv)
{
    game_params *params;
    game_state *state;
    char *id = NULL, *desc;
    const char *err;
    bool grade = false;
    char *progname = argv[0];

    char buf[80];
    int limit, x, y;
    bool solvable;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            /* solver_show_working = true; */
        } else if (!strcmp(p, "-g")) {
            grade = true;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", progname, p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    params = default_params();
    decode_params(params, id);
    err = validate_desc(params, desc);
    if (err) {
        free_params(params);
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }

    state = new_game(NULL, params, desc);
    free_params(params);

    solvable = (PARITY_S(state) == perm_parity(state->tiles, state->n));
    if (grade || !solvable) {
        free_game(state);
        fputs(solvable ? "Game is solvable" : "Game is unsolvable",
              grade ? stdout : stderr);
        return !grade;
    }

    for (limit = 5 * state->n * state->n * state->n; limit; --limit) {
        game_state *next_state;
        if (!compute_hint(state, &x, &y)) {
            fprintf(stderr, "couldn't compute next move while solving %s:%s",
                    id, desc);
            return 1;
        }
        printf("Move the space to (%d, %d), moving %d into the space\n",
               x + 1, y + 1, state->tiles[C(state, x, y)]);
        sprintf(buf, "M%d,%d", x, y);
        next_state = execute_move(state, buf);

        free_game(state);
        if (!next_state) {
            fprintf(stderr, "invalid move when solving %s:%s\n", id, desc);
            return 1;
        }
        state = next_state;
        if (next_state->completed) {
            free_game(state);
            return 0;
        }
    }

    free_game(state);
    fprintf(stderr, "ran out of moves for %s:%s\n", id, desc);
    return 1;
}

#endif
