/*
 * twiddle.c: Puzzle involving rearranging a grid of squares by
 * rotating subsquares. Adapted and generalised from a
 * door-unlocking puzzle in Metroid Prime 2 (the one in the Main
 * Gyro Chamber).
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

#define ANIM_PER_BLKSIZE_UNIT 0.13F
#define FLASH_FRAME 0.13F

enum {
    COL_BACKGROUND,
    COL_TEXT,
    COL_HIGHLIGHT,
    COL_HIGHLIGHT_GENTLE,
    COL_LOWLIGHT,
    COL_LOWLIGHT_GENTLE,
    COL_HIGHCURSOR, COL_LOWCURSOR,
    NCOLOURS
};

struct game_params {
    int w, h, n;
    bool rowsonly;
    bool orientable;
    int movetarget;
};

struct game_state {
    int w, h, n;
    bool orientable;
    int *grid;
    int completed;
    bool used_solve;		       /* used to suppress completion flash */
    int movecount, movetarget;
    int lastx, lasty, lastr;	       /* coordinates of last rotation */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 3;
    ret->n = 2;
    ret->rowsonly = ret->orientable = false;
    ret->movetarget = 0;

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

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    static struct {
        const char *title;
        game_params params;
    } const presets[] = {
        { "3x3 rows only", { 3, 3, 2, true, false } },
        { "3x3 normal", { 3, 3, 2, false, false } },
        { "3x3 orientable", { 3, 3, 2, false, true } },
        { "4x4 normal", { 4, 4, 2, false } },
        { "4x4 orientable", { 4, 4, 2, false, true } },
        { "4x4, rotating 3x3 blocks", { 4, 4, 3, false } },
        { "5x5, rotating 3x3 blocks", { 5, 5, 3, false } },
        { "6x6, rotating 4x4 blocks", { 6, 6, 4, false } },
    };

    if (i < 0 || i >= lenof(presets))
        return false;

    *name = dupstr(presets[i].title);
    *params = dup_params(&presets[i].params);

    return true;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    ret->n = 2;
    ret->rowsonly = false;
    ret->orientable = false;
    ret->movetarget = 0;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'n') {
        string++;
        ret->n = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    while (*string) {
	if (*string == 'r') {
	    ret->rowsonly = true;
            string++;
	} else if (*string == 'o') {
	    ret->orientable = true;
            string++;
	} else if (*string == 'm') {
            string++;
	    ret->movetarget = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
	} else
            string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%dn%d%s%s", params->w, params->h, params->n,
	    params->rowsonly ? "r" : "",
	    params->orientable ? "o" : "");
    /* Shuffle limit is part of the limited parameters, because we have to
     * supply the target move count. */
    if (params->movetarget)
        sprintf(buf + strlen(buf), "m%d", params->movetarget);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(7, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Rotating block size";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->n);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "One number per row";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->rowsonly;

    ret[4].name = "Orientation matters";
    ret[4].type = C_BOOLEAN;
    ret[4].u.boolean.bval = params->orientable;

    ret[5].name = "Number of shuffling moves";
    ret[5].type = C_STRING;
    sprintf(buf, "%d", params->movetarget);
    ret[5].u.string.sval = dupstr(buf);

    ret[6].name = NULL;
    ret[6].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->n = atoi(cfg[2].u.string.sval);
    ret->rowsonly = cfg[3].u.boolean.bval;
    ret->orientable = cfg[4].u.boolean.bval;
    ret->movetarget = atoi(cfg[5].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->n < 2)
	return "Rotating block size must be at least two";
    if (params->w < params->n)
	return "Width must be at least the rotating block size";
    if (params->h < params->n)
	return "Height must be at least the rotating block size";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    return NULL;
}

/*
 * This function actually performs a rotation on a grid. The `x'
 * and `y' coordinates passed in are the coordinates of the _top
 * left corner_ of the rotated region. (Using the centre would have
 * involved half-integers and been annoyingly fiddly. Clicking in
 * the centre is good for a user interface, but too inconvenient to
 * use internally.)
 */
static void do_rotate(int *grid, int w, int h, int n, bool orientable,
		      int x, int y, int dir)
{
    int i, j;

    assert(x >= 0 && x+n <= w);
    assert(y >= 0 && y+n <= h);
    dir &= 3;
    if (dir == 0)
	return;			       /* nothing to do */

    grid += y*w+x;		       /* translate region to top corner */

    /*
     * If we were leaving the result of the rotation in a separate
     * grid, the simple thing to do would be to loop over each
     * square within the rotated region and assign it from its
     * source square. However, to do it in place without taking
     * O(n^2) memory, we need to be marginally more clever. What
     * I'm going to do is loop over about one _quarter_ of the
     * rotated region and permute each element within that quarter
     * with its rotational coset.
     * 
     * The size of the region I need to loop over is (n+1)/2 by
     * n/2, which is an obvious exact quarter for even n and is a
     * rectangle for odd n. (For odd n, this technique leaves out
     * one element of the square, which is of course the central
     * one that never moves anyway.)
     */
    for (i = 0; i < (n+1)/2; i++) {
	for (j = 0; j < n/2; j++) {
	    int k;
	    int g[4];
	    int p[4];
            
            p[0] = j*w+i;
            p[1] = i*w+(n-j-1);
            p[2] = (n-j-1)*w+(n-i-1);
            p[3] = (n-i-1)*w+j;

	    for (k = 0; k < 4; k++)
		g[k] = grid[p[k]];

	    for (k = 0; k < 4; k++) {
		int v = g[(k+dir) & 3];
		if (orientable)
		    v ^= ((v+dir) ^ v) & 3;  /* alter orientation */
		grid[p[k]] = v;
	    }
	}
    }

    /*
     * Don't forget the orientation on the centre square, if n is
     * odd.
     */
    if (orientable && (n & 1)) {
	int v = grid[n/2*(w+1)];
	v ^= ((v+dir) ^ v) & 3;  /* alter orientation */
	grid[n/2*(w+1)] = v;
    }
}

static bool grid_complete(int *grid, int wh, bool orientable)
{
    bool ok = true;
    int i;
    for (i = 1; i < wh; i++)
	if (grid[i] < grid[i-1])
	    ok = false;
    if (orientable) {
	for (i = 0; i < wh; i++)
	    if (grid[i] & 3)
		ok = false;
    }
    return ok;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int *grid;
    int w = params->w, h = params->h, n = params->n, wh = w*h;
    int i;
    char *ret;
    int retlen;
    int total_moves;

    /*
     * Set up a solved grid.
     */
    grid = snewn(wh, int);
    for (i = 0; i < wh; i++)
	grid[i] = ((params->rowsonly ? i/w : i) + 1) * 4;

    /*
     * Shuffle it. This game is complex enough that I don't feel up
     * to analysing its full symmetry properties (particularly at
     * n=4 and above!), so I'm going to do it the pedestrian way
     * and simply shuffle the grid by making a long sequence of
     * randomly chosen moves.
     */
    total_moves = params->movetarget;
    if (!total_moves)
        /* Add a random move to avoid parity issues. */
        total_moves = w*h*n*n*2 + random_upto(rs, 2);

    do {
        int *prevmoves;
        int rw, rh;                    /* w/h of rotation centre space */

        rw = w - n + 1;
        rh = h - n + 1;
        prevmoves = snewn(rw * rh, int);
        for (i = 0; i < rw * rh; i++)
            prevmoves[i] = 0;

        for (i = 0; i < total_moves; i++) {
            int x, y, r, oldtotal, newtotal, dx, dy;

            do {
                x = random_upto(rs, w - n + 1);
                y = random_upto(rs, h - n + 1);
                r = 2 * random_upto(rs, 2) - 1;

                /*
                 * See if any previous rotations has happened at
                 * this point which nothing has overlapped since.
                 * If so, ensure we haven't either undone a
                 * previous move or repeated one so many times that
                 * it turns into fewer moves in the inverse
                 * direction (i.e. three identical rotations).
                 */
                oldtotal = prevmoves[y*rw+x];
                newtotal = oldtotal + r;
                
                /*
                 * Special case here for w==h==n, in which case
                 * there is actually no way to _avoid_ all moves
                 * repeating or undoing previous ones.
                 */
            } while ((w != n || h != n) &&
                     (abs(newtotal) < abs(oldtotal) || abs(newtotal) > 2));

            do_rotate(grid, w, h, n, params->orientable, x, y, r);

            /*
             * Log the rotation we've just performed at this point,
             * for inversion detection in the next move.
             * 
             * Also zero a section of the prevmoves array, because
             * any rotation area which _overlaps_ this one is now
             * entirely safe to perform further moves in.
             * 
             * Two rotation areas overlap if their top left
             * coordinates differ by strictly less than n in both
             * directions
             */
            prevmoves[y*rw+x] += r;
            for (dy = -n+1; dy <= n-1; dy++) {
                if (y + dy < 0 || y + dy >= rh)
                    continue;
                for (dx = -n+1; dx <= n-1; dx++) {
                    if (x + dx < 0 || x + dx >= rw)
                        continue;
                    if (dx == 0 && dy == 0)
                        continue;
                    prevmoves[(y+dy)*rw+(x+dx)] = 0;
                }
            }
        }

        sfree(prevmoves);

    } while (grid_complete(grid, wh, params->orientable));

    /*
     * Now construct the game description, by describing the grid
     * as a simple sequence of integers. They're comma-separated,
     * unless the puzzle is orientable in which case they're
     * separated by orientation letters `u', `d', `l' and `r'.
     */
    ret = NULL;
    retlen = 0;
    for (i = 0; i < wh; i++) {
        char buf[80];
        int k;

        k = sprintf(buf, "%d%c", grid[i] / 4,
		    (char)(params->orientable ? "uldr"[grid[i] & 3] : ','));

        ret = sresize(ret, retlen + k + 1, char);
        strcpy(ret + retlen, buf);
        retlen += k;
    }
    if (!params->orientable)
	ret[retlen-1] = '\0';	       /* delete last comma */

    sfree(grid);
    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *p;
    int w = params->w, h = params->h, wh = w*h;
    int i;

    p = desc;

    for (i = 0; i < wh; i++) {
	if (*p < '0' || *p > '9')
	    return "Not enough numbers in string";
	while (*p >= '0' && *p <= '9')
	    p++;
	if (!params->orientable && i < wh-1) {
	    if (*p != ',')
		return "Expected comma after number";
	} else if (params->orientable && i < wh) {
	    if (*p != 'l' && *p != 'r' && *p != 'u' && *p != 'd')
		return "Expected orientation letter after number";
	} else if (i == wh-1 && *p) {
	    return "Excess junk at end of string";
	}

	if (*p) p++;		       /* eat comma */
    }

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int w = params->w, h = params->h, n = params->n, wh = w*h;
    int i;
    const char *p;

    state->w = w;
    state->h = h;
    state->n = n;
    state->orientable = params->orientable;
    state->completed = 0;
    state->used_solve = false;
    state->movecount = 0;
    state->movetarget = params->movetarget;
    state->lastx = state->lasty = state->lastr = -1;

    state->grid = snewn(wh, int);

    p = desc;

    for (i = 0; i < wh; i++) {
	state->grid[i] = 4 * atoi(p);
	while (*p >= '0' && *p <= '9')
	    p++;
	if (*p) {
	    if (params->orientable) {
		switch (*p) {
		  case 'l': state->grid[i] |= 1; break;
		  case 'd': state->grid[i] |= 2; break;
		  case 'r': state->grid[i] |= 3; break;
		}
	    }
	    p++;
	}
    }

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;
    ret->orientable = state->orientable;
    ret->completed = state->completed;
    ret->movecount = state->movecount;
    ret->movetarget = state->movetarget;
    ret->lastx = state->lastx;
    ret->lasty = state->lasty;
    ret->lastr = state->lastr;
    ret->used_solve = state->used_solve;

    ret->grid = snewn(ret->w * ret->h, int);
    memcpy(ret->grid, state->grid, ret->w * ret->h * sizeof(int));

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state);
}

static int compare_int(const void *av, const void *bv)
{
    const int *a = (const int *)av;
    const int *b = (const int *)bv;
    if (*a < *b)
	return -1;
    else if (*a > *b)
	return +1;
    else
	return 0;
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
    int i, x, y, col, maxlen;
    bool o = state->orientable;

    /* Pedantic check: ensure buf is large enough to format an int in
     * decimal, using the bound log10(2) < 1/3. (Obviously in practice
     * int is not going to be larger than even 32 bits any time soon,
     * but.) */
    assert(sizeof(buf) >= 1 + sizeof(int) * CHAR_BIT/3);

    /*
     * First work out how many characters we need to display each
     * number. We're pretty flexible on grid contents here, so we
     * have to scan the entire grid.
     */
    col = 0;
    for (i = 0; i < state->w * state->h; i++) {
	x = sprintf(buf, "%d", state->grid[i] / 4);
	if (col < x) col = x;
    }

    /* Reassure sprintf-checking compilers like gcc that the field
     * width we've just computed is not now excessive */
    if (col >= sizeof(buf))
        col = sizeof(buf)-1;

    /*
     * Now we know the exact total size of the grid we're going to
     * produce: it's got h rows, each containing w lots of col+o,
     * w-1 spaces and a trailing newline.
     */
    maxlen = state->h * state->w * (col+o+1);

    ret = snewn(maxlen+1, char);
    p = ret;

    for (y = 0; y < state->h; y++) {
	for (x = 0; x < state->w; x++) {
	    int v = state->grid[state->w*y+x];
	    sprintf(buf, "%*d", col, v/4);
	    memcpy(p, buf, col);
	    p += col;
	    if (o)
		*p++ = "^<v>"[v & 3];
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

struct game_ui {
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->cur_x = 0;
    ui->cur_y = 0;
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

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (!ui->cur_visible) return "";
    switch (button) {
      case CURSOR_SELECT: return "Turn left";
      case CURSOR_SELECT2: return "Turn right";
    }
    return "";
}

struct game_drawstate {
    bool started;
    int w, h, bgcolour;
    int *grid;
    int tilesize;
    int cur_x, cur_y;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->w, h = state->h, n = state->n /* , wh = w*h */;
    char buf[80];
    int dir;

    button = button & (~MOD_MASK | MOD_NUM_KEYPAD);

    if (IS_CURSOR_MOVE(button)) {
        if (button == CURSOR_LEFT && ui->cur_x > 0)
            ui->cur_x--;
        if (button == CURSOR_RIGHT && (ui->cur_x+n) < (w))
            ui->cur_x++;
        if (button == CURSOR_UP && ui->cur_y > 0)
            ui->cur_y--;
        if (button == CURSOR_DOWN && (ui->cur_y+n) < (h))
            ui->cur_y++;
        ui->cur_visible = true;
        return UI_UPDATE;
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
	/*
	 * Determine the coordinates of the click. We offset by n-1
	 * half-blocks so that the user must click at the centre of
	 * a rotation region rather than at the corner.
	 */
	x -= (n-1) * TILE_SIZE / 2;
	y -= (n-1) * TILE_SIZE / 2;
	x = FROMCOORD(x);
	y = FROMCOORD(y);
	dir = (button == LEFT_BUTTON ? 1 : -1);
	if (x < 0 || x > w-n || y < 0 || y > h-n)
	    return NULL;
        ui->cur_visible = false;
    } else if (IS_CURSOR_SELECT(button)) {
        if (ui->cur_visible) {
            x = ui->cur_x;
            y = ui->cur_y;
            dir = (button == CURSOR_SELECT2) ? -1 : +1;
        } else {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
    } else if (button == 'a' || button == 'A' || button==MOD_NUM_KEYPAD+'7') {
        x = y = 0;
        dir = (button == 'A' ? -1 : +1);
    } else if (button == 'b' || button == 'B' || button==MOD_NUM_KEYPAD+'9') {
        x = w-n;
        y = 0;
        dir = (button == 'B' ? -1 : +1);
    } else if (button == 'c' || button == 'C' || button==MOD_NUM_KEYPAD+'1') {
        x = 0;
        y = h-n;
        dir = (button == 'C' ? -1 : +1);
    } else if (button == 'd' || button == 'D' || button==MOD_NUM_KEYPAD+'3') {
        x = w-n;
        y = h-n;
        dir = (button == 'D' ? -1 : +1);
    } else if (button==MOD_NUM_KEYPAD+'8' && (w-n) % 2 == 0) {
        x = (w-n) / 2;
        y = 0;
        dir = +1;
    } else if (button==MOD_NUM_KEYPAD+'2' && (w-n) % 2 == 0) {
        x = (w-n) / 2;
        y = h-n;
        dir = +1;
    } else if (button==MOD_NUM_KEYPAD+'4' && (h-n) % 2 == 0) {
        x = 0;
        y = (h-n) / 2;
        dir = +1;
    } else if (button==MOD_NUM_KEYPAD+'6' && (h-n) % 2 == 0) {
        x = w-n;
        y = (h-n) / 2;
        dir = +1;
    } else if (button==MOD_NUM_KEYPAD+'5' && (w-n) % 2 == 0 && (h-n) % 2 == 0){
        x = (w-n) / 2;
        y = (h-n) / 2;
        dir = +1;
    } else {
        return NULL;                   /* no move to be made */
    }

    /*
     * If we reach here, we have a valid move.
     */
    sprintf(buf, "M%d,%d,%d", x, y, dir);
    return dupstr(buf);
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret;
    int w = from->w, h = from->h, n = from->n, wh = w*h;
    int x, y, dir;

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
	qsort(ret->grid, ret->w*ret->h, sizeof(int), compare_int);
	for (i = 0; i < ret->w*ret->h; i++)
	    ret->grid[i] &= ~3;
	ret->used_solve = true;
	ret->completed = ret->movecount = 1;

	return ret;
    }

    if (move[0] != 'M' ||
	sscanf(move+1, "%d,%d,%d", &x, &y, &dir) != 3 ||
	x < 0 || y < 0 || x > from->w - n || y > from->h - n)
	return NULL;		       /* can't parse this move string */

    ret = dup_game(from);
    ret->movecount++;
    do_rotate(ret->grid, w, h, n, ret->orientable, x, y, dir);
    ret->lastx = x;
    ret->lasty = y;
    ret->lastr = dir;

    /*
     * See if the game has been completed. To do this we simply
     * test that the grid contents are in increasing order.
     */
    if (!ret->completed && grid_complete(ret->grid, wh, ret->orientable))
        ret->completed = ret->movecount;
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

    /* cursor is light-background with a red tinge. */
    ret[COL_HIGHCURSOR * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 1.0F;
    ret[COL_HIGHCURSOR * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.5F;
    ret[COL_HIGHCURSOR * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 0.5F;

    for (i = 0; i < 3; i++) {
        ret[COL_HIGHLIGHT_GENTLE * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 1.1F;
        ret[COL_LOWLIGHT_GENTLE * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.9F;
        ret[COL_TEXT * 3 + i] = 0.0;
        ret[COL_LOWCURSOR * 3 + i] = ret[COL_HIGHCURSOR * 3 + i] * 0.6F;
    }

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
    ds->grid = snewn(ds->w*ds->h, int);
    ds->tilesize = 0;                  /* haven't decided yet */
    for (i = 0; i < ds->w*ds->h; i++)
        ds->grid[i] = -1;
    ds->cur_x = ds->cur_y = -state->n;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

struct rotation {
    int cx, cy, cw, ch;		       /* clip region */
    int ox, oy;			       /* rotation origin */
    float c, s;			       /* cos and sin of rotation angle */
    int lc, rc, tc, bc;		       /* colours of tile edges */
};

static void rotate(int *xy, struct rotation *rot)
{
    if (rot) {
	float xf = (float)xy[0] - rot->ox, yf = (float)xy[1] - rot->oy;
	float xf2, yf2;

	xf2 = rot->c * xf + rot->s * yf;
	yf2 = - rot->s * xf + rot->c * yf;

	xy[0] = (int)(xf2 + rot->ox + 0.5F);   /* round to nearest */
	xy[1] = (int)(yf2 + rot->oy + 0.5F);   /* round to nearest */
    }
}

#define CUR_TOP         1
#define CUR_RIGHT       2
#define CUR_BOTTOM      4
#define CUR_LEFT        8

static void draw_tile(drawing *dr, game_drawstate *ds, const game_state *state,
                      int x, int y, int tile, int flash_colour,
                      struct rotation *rot, unsigned cedges)
{
    int coords[8];
    char str[40];

    /*
     * If we've been passed a rotation region but we're drawing a
     * tile which is outside it, we must draw it normally. This can
     * occur if we're cleaning up after a completion flash while a
     * new move is also being made.
     */
    if (rot && (x < rot->cx || y < rot->cy ||
                x >= rot->cx+rot->cw || y >= rot->cy+rot->ch))
        rot = NULL;

    if (rot)
	clip(dr, rot->cx, rot->cy, rot->cw, rot->ch);

    /*
     * We must draw each side of the tile's highlight separately,
     * because in some cases (during rotation) they will all need
     * to be different colours.
     */

    /* The centre point is common to all sides. */
    coords[4] = x + TILE_SIZE / 2;
    coords[5] = y + TILE_SIZE / 2;
    rotate(coords+4, rot);

    /* Right side. */
    coords[0] = x + TILE_SIZE - 1;
    coords[1] = y + TILE_SIZE - 1;
    rotate(coords+0, rot);
    coords[2] = x + TILE_SIZE - 1;
    coords[3] = y;
    rotate(coords+2, rot);
    draw_polygon(dr, coords, 3, rot ? rot->rc : COL_LOWLIGHT,
		 rot ? rot->rc : (cedges & CUR_RIGHT) ? COL_LOWCURSOR : COL_LOWLIGHT);

    /* Bottom side. */
    coords[2] = x;
    coords[3] = y + TILE_SIZE - 1;
    rotate(coords+2, rot);
    draw_polygon(dr, coords, 3, rot ? rot->bc : COL_LOWLIGHT,
		 rot ? rot->bc : (cedges & CUR_BOTTOM) ? COL_LOWCURSOR : COL_LOWLIGHT);

    /* Left side. */
    coords[0] = x;
    coords[1] = y;
    rotate(coords+0, rot);
    draw_polygon(dr, coords, 3, rot ? rot->lc : COL_HIGHLIGHT,
		 rot ? rot->lc : (cedges & CUR_LEFT) ? COL_HIGHCURSOR : COL_HIGHLIGHT);

    /* Top side. */
    coords[2] = x + TILE_SIZE - 1;
    coords[3] = y;
    rotate(coords+2, rot);
    draw_polygon(dr, coords, 3, rot ? rot->tc : COL_HIGHLIGHT,
		 rot ? rot->tc : (cedges & CUR_TOP) ? COL_HIGHCURSOR : COL_HIGHLIGHT);

    /*
     * Now the main blank area in the centre of the tile.
     */
    if (rot) {
	coords[0] = x + HIGHLIGHT_WIDTH;
	coords[1] = y + HIGHLIGHT_WIDTH;
	rotate(coords+0, rot);
	coords[2] = x + HIGHLIGHT_WIDTH;
	coords[3] = y + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	rotate(coords+2, rot);
	coords[4] = x + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	coords[5] = y + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	rotate(coords+4, rot);
	coords[6] = x + TILE_SIZE - 1 - HIGHLIGHT_WIDTH;
	coords[7] = y + HIGHLIGHT_WIDTH;
	rotate(coords+6, rot);
	draw_polygon(dr, coords, 4, flash_colour, flash_colour);
    } else {
	draw_rect(dr, x + HIGHLIGHT_WIDTH, y + HIGHLIGHT_WIDTH,
		  TILE_SIZE - 2*HIGHLIGHT_WIDTH, TILE_SIZE - 2*HIGHLIGHT_WIDTH,
		  flash_colour);
    }

    /*
     * Next, the triangles for orientation.
     */
    if (state->orientable) {
	int xdx, xdy, ydx, ydy;
	int cx, cy, displ, displ2;
	switch (tile & 3) {
	  case 0:
	    xdx = 1, xdy = 0;
	    ydx = 0, ydy = 1;
	    break;
	  case 1:
	    xdx = 0, xdy = -1;
	    ydx = 1, ydy = 0;
	    break;
	  case 2:
	    xdx = -1, xdy = 0;
	    ydx = 0, ydy = -1;
	    break;
	  default /* case 3 */:
	    xdx = 0, xdy = 1;
	    ydx = -1, ydy = 0;
	    break;
	}

	cx = x + TILE_SIZE / 2;
	cy = y + TILE_SIZE / 2;
	displ = TILE_SIZE / 2 - HIGHLIGHT_WIDTH - 2;
	displ2 = TILE_SIZE / 3 - HIGHLIGHT_WIDTH;

	coords[0] = cx - displ * xdx + displ2 * ydx;
	coords[1] = cy - displ * xdy + displ2 * ydy;
	rotate(coords+0, rot);
	coords[2] = cx + displ * xdx + displ2 * ydx;
	coords[3] = cy + displ * xdy + displ2 * ydy;
	rotate(coords+2, rot);
	coords[4] = cx - displ * ydx;
	coords[5] = cy - displ * ydy;
	rotate(coords+4, rot);
	draw_polygon(dr, coords, 3, COL_LOWLIGHT_GENTLE, COL_LOWLIGHT_GENTLE);
    }

    coords[0] = x + TILE_SIZE/2;
    coords[1] = y + TILE_SIZE/2;
    rotate(coords+0, rot);
    sprintf(str, "%d", tile / 4);
    draw_text(dr, coords[0], coords[1],
	      FONT_VARIABLE, TILE_SIZE/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
	      COL_TEXT, str);

    if (rot)
	unclip(dr);

    draw_update(dr, x, y, TILE_SIZE, TILE_SIZE);
}

static int highlight_colour(float angle)
{
    int colours[32] = {
	COL_LOWLIGHT,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_HIGHLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT_GENTLE,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
	COL_LOWLIGHT,
    };

    return colours[(int)((angle + 2*(float)PI) / ((float)PI/16)) & 31];
}

static float game_anim_length_real(const game_state *oldstate,
                                   const game_state *newstate, int dir,
                                   const game_ui *ui)
{
    /*
     * Our game_anim_length doesn't need to modify its game_ui, so
     * this is the real function which declares ui as const. We must
     * wrap this for the backend structure with a version that has ui
     * non-const, but we still need this version to call from within
     * game_redraw which only has a const ui available.
     */
    return (float)(ANIM_PER_BLKSIZE_UNIT * sqrt(newstate->n-1));
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return game_anim_length_real(oldstate, newstate, dir, ui);

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
        *w = *h = state->n * TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, bgcolour;
    struct rotation srot, *rot;
    int lastx = -1, lasty = -1, lastr = -1;
    int cx, cy, n = state->n;
    bool cmoved = false;

    cx = ui->cur_visible ? ui->cur_x : -state->n;
    cy = ui->cur_visible ? ui->cur_y : -state->n;
    if (cx != ds->cur_x || cy != ds->cur_y)
        cmoved = true;

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
     * If we're drawing any rotated tiles, sort out the rotation
     * parameters, and also zap the rotation region to the
     * background colour before doing anything else.
     */
    if (oldstate) {
	float angle;
	float anim_max = game_anim_length_real(oldstate, state, dir, ui);

	if (dir > 0) {
	    lastx = state->lastx;
	    lasty = state->lasty;
	    lastr = state->lastr;
	} else {
	    lastx = oldstate->lastx;
	    lasty = oldstate->lasty;
	    lastr = -oldstate->lastr;
	}

	rot = &srot;
	rot->cx = COORD(lastx);
	rot->cy = COORD(lasty);
	rot->cw = rot->ch = TILE_SIZE * state->n;
	rot->ox = rot->cx + rot->cw/2;
	rot->oy = rot->cy + rot->ch/2;
	angle = ((-(float)PI/2 * lastr) * (1.0F - animtime / anim_max));
	rot->c = (float)cos(angle);
	rot->s = (float)sin(angle);

	/*
	 * Sort out the colours of the various sides of the tile.
	 */
	rot->lc = highlight_colour((float)PI + angle);
	rot->rc = highlight_colour(angle);
	rot->tc = highlight_colour((float)(PI/2.0) + angle);
	rot->bc = highlight_colour((float)(-PI/2.0) + angle);

	draw_rect(dr, rot->cx, rot->cy, rot->cw, rot->ch, bgcolour);
    } else
	rot = NULL;

    /*
     * Now draw each tile.
     */
    for (i = 0; i < state->w * state->h; i++) {
	int t;
        bool cc = false;
	int tx = i % state->w, ty = i / state->w;

	/*
	 * Figure out what should be displayed at this location.
	 * Usually it will be state->grid[i], unless we're in the
	 * middle of animating an actual rotation and this cell is
	 * within the rotation region, in which case we set -1
	 * (always display).
	 */
	if (oldstate && lastx >= 0 && lasty >= 0 &&
	    tx >= lastx && tx < lastx + state->n &&
	    ty >= lasty && ty < lasty + state->n)
	    t = -1;
	else
	    t = state->grid[i];

        if (cmoved) {
            /* cursor has moved (or changed visibility)... */
            if (tx == cx || tx == cx+n-1 || ty == cy || ty == cy+n-1)
                cc = true; /* ...we're on new cursor, redraw */
            if (tx == ds->cur_x || tx == ds->cur_x+n-1 ||
                ty == ds->cur_y || ty == ds->cur_y+n-1)
                cc = true; /* ...we were on old cursor, redraw */
        }

	if (ds->bgcolour != bgcolour ||   /* always redraw when flashing */
	    ds->grid[i] != t || ds->grid[i] == -1 || t == -1 || cc) {
	    int x = COORD(tx), y = COORD(ty);
            unsigned cedges = 0;

            if (tx == cx     && ty >= cy && ty <= cy+n-1) cedges |= CUR_LEFT;
            if (ty == cy     && tx >= cx && tx <= cx+n-1) cedges |= CUR_TOP;
            if (tx == cx+n-1 && ty >= cy && ty <= cy+n-1) cedges |= CUR_RIGHT;
            if (ty == cy+n-1 && tx >= cx && tx <= cx+n-1) cedges |= CUR_BOTTOM;

	    draw_tile(dr, ds, state, x, y, state->grid[i], bgcolour, rot, cedges);
            ds->grid[i] = t;
        }
    }
    ds->bgcolour = bgcolour;
    ds->cur_x = cx; ds->cur_y = cy;

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

#ifdef COMBINED
#define thegame twiddle
#endif

const struct game thegame = {
    "Twiddle", "games.twiddle", "twiddle",
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
