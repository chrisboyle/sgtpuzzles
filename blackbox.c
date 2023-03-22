/*
 * blackbox.c: implementation of 'Black Box'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define PREFERRED_TILE_SIZE 32
#define FLASH_FRAME 0.2F

/* Terminology, for ease of reading various macros scattered about the place.
 *
 * The 'arena' is the inner area where the balls are placed. This is
 *   indexed from (0,0) to (w-1,h-1) but its offset in the grid is (1,1).
 *
 * The 'range' (firing range) is the bit around the edge where
 *   the lasers are fired from. This is indexed from 0 --> (2*(w+h) - 1),
 *   starting at the top left ((1,0) on the grid) and moving clockwise.
 *
 * The 'grid' is just the big array containing arena and range;
 *   locations (0,0), (0,w+1), (h+1,w+1) and (h+1,0) are unused.
 */

enum {
    COL_BACKGROUND, COL_COVER, COL_LOCK,
    COL_TEXT, COL_FLASHTEXT,
    COL_HIGHLIGHT, COL_LOWLIGHT, COL_GRID,
    COL_BALL, COL_WRONG, COL_BUTTON,
    COL_CURSOR,
    NCOLOURS
};

struct game_params {
    int w, h;
    int minballs, maxballs;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 8;
    ret->minballs = ret->maxballs = 5;

    return ret;
}

static const game_params blackbox_presets[] = {
    { 5, 5, 3, 3 },
    { 8, 8, 5, 5 },
    { 8, 8, 3, 6 },
    { 10, 10, 5, 5 },
    { 10, 10, 4, 10 }
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    char str[80];
    game_params *ret;

    if (i < 0 || i >= lenof(blackbox_presets))
        return false;

    ret = snew(game_params);
    *ret = blackbox_presets[i];

    if (ret->minballs == ret->maxballs)
        sprintf(str, "%dx%d, %d balls",
                ret->w, ret->h, ret->minballs);
    else
        sprintf(str, "%dx%d, %d-%d balls",
                ret->w, ret->h, ret->minballs, ret->maxballs);

    *name = dupstr(str);
    *params = ret;
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

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;
    game_params *defs = default_params();

    *params = *defs; free_params(defs);

    while (*p) {
        switch (*p++) {
        case 'w':
            params->w = atoi(p);
            while (*p && isdigit((unsigned char)*p)) p++;
            break;

        case 'h':
            params->h = atoi(p);
            while (*p && isdigit((unsigned char)*p)) p++;
            break;

        case 'm':
            params->minballs = atoi(p);
            while (*p && isdigit((unsigned char)*p)) p++;
            break;

        case 'M':
            params->maxballs = atoi(p);
            while (*p && isdigit((unsigned char)*p)) p++;
            break;

        default:
            ;
        }
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char str[256];

    sprintf(str, "w%dh%dm%dM%d",
            params->w, params->h, params->minballs, params->maxballs);
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

    ret[2].name = "No. of balls";
    ret[2].type = C_STRING;
    if (params->minballs == params->maxballs)
        sprintf(buf, "%d", params->minballs);
    else
        sprintf(buf, "%d-%d", params->minballs, params->maxballs);
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

    /* Allow 'a-b' for a range, otherwise assume a single number. */
    if (sscanf(cfg[2].u.string.sval, "%d-%d",
               &ret->minballs, &ret->maxballs) < 2)
        ret->minballs = ret->maxballs = atoi(cfg[2].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 || params->h < 2)
        return "Width and height must both be at least two";
    /* next one is just for ease of coding stuff into 'char'
     * types, and could be worked around if required. */
    if (params->w > 255 || params->h > 255)
        return "Widths and heights greater than 255 are not supported";
    if (params->minballs < 0)
        return "Negative number of balls";
    if (params->minballs > params->maxballs)
        return "Minimum number of balls may not be greater than maximum";
    if (params->minballs >= params->w * params->h)
        return "Too many balls to fit in grid";
    return NULL;
}

/*
 * We store: width | height | ball1x | ball1y | [ ball2x | ball2y | [...] ]
 * all stored as unsigned chars; validate_params has already
 * checked this won't overflow an 8-bit char.
 * Then we obfuscate it.
 */

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int nballs = params->minballs, i;
    char *grid, *ret;
    unsigned char *bmp;

    if (params->maxballs > params->minballs)
        nballs += random_upto(rs, params->maxballs - params->minballs + 1);

    grid = snewn(params->w*params->h, char);
    memset(grid, 0, params->w * params->h * sizeof(char));

    bmp = snewn(nballs*2 + 2, unsigned char);
    memset(bmp, 0, (nballs*2 + 2) * sizeof(unsigned char));

    bmp[0] = params->w;
    bmp[1] = params->h;

    for (i = 0; i < nballs; i++) {
        int x, y;

        do {
            x = random_upto(rs, params->w);
            y = random_upto(rs, params->h);
        } while (grid[y*params->w + x]);

        grid[y*params->w + x] = 1;

        bmp[(i+1)*2 + 0] = x;
        bmp[(i+1)*2 + 1] = y;
    }
    sfree(grid);

    obfuscate_bitmap(bmp, (nballs*2 + 2) * 8, false);
    ret = bin2hex(bmp, nballs*2 + 2);
    sfree(bmp);

    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int nballs, dlen = strlen(desc), i;
    unsigned char *bmp;
    const char *ret;

    /* the bitmap is 2+(nballs*2) long; the hex version is double that. */
    nballs = ((dlen/2)-2)/2;

    if (dlen < 4 || dlen % 4 ||
        nballs < params->minballs || nballs > params->maxballs)
        return "Game description is wrong length";

    bmp = hex2bin(desc, nballs*2 + 2);
    obfuscate_bitmap(bmp, (nballs*2 + 2) * 8, true);
    ret = "Game description is corrupted";
    /* check general grid size */
    if (bmp[0] != params->w || bmp[1] != params->h)
        goto done;
    /* check each ball will fit on that grid */
    for (i = 0; i < nballs; i++) {
        int x = bmp[(i+1)*2 + 0], y = bmp[(i+1)*2 + 1];
        if (x < 0 || y < 0 || x >= params->w || y >= params->h)
            goto done;
    }
    ret = NULL;

done:
    sfree(bmp);
    return ret;
}

#define BALL_CORRECT    0x01
#define BALL_GUESS      0x02
#define BALL_LOCK       0x04

#define LASER_FLAGMASK  0x1f800
#define LASER_OMITTED    0x0800
#define LASER_REFLECT    0x1000
#define LASER_HIT        0x2000
#define LASER_WRONG      0x4000
#define LASER_FLASHED    0x8000
#define LASER_EMPTY      (~0)

#define FLAG_CURSOR     0x10000 /* needs to be disjoint from both sets */

struct game_state {
    int w, h, minballs, maxballs, nballs, nlasers;
    unsigned int *grid; /* (w+2)x(h+2), to allow for laser firing range */
    unsigned int *exits; /* one per laser */
    bool done;          /* user has finished placing his own balls. */
    int laserno;        /* number of next laser to be fired. */
    int nguesses, nright, nwrong, nmissed;
    bool reveal, justwrong;
};

#define GRID(s,x,y) ((s)->grid[(y)*((s)->w+2) + (x)])

#define RANGECHECK(s,x) ((x) >= 0 && (x) < (s)->nlasers)

/* specify numbers because they must match array indexes. */
enum { DIR_UP = 0, DIR_RIGHT = 1, DIR_DOWN = 2, DIR_LEFT = 3 };

struct offset { int x, y; };

static const struct offset offsets[] = {
    {  0, -1 }, /* up */
    {  1,  0 }, /* right */
    {  0,  1 }, /* down */
    { -1,  0 }  /* left */
};

#ifdef DEBUGGING
static const char *dirstrs[] = {
    "UP", "RIGHT", "DOWN", "LEFT"
};
#endif

static bool range2grid(const game_state *state, int rangeno, int *x, int *y,
                       int *direction)
{
    if (rangeno < 0)
        return false;

    if (rangeno < state->w) {
        /* top row; from (1,0) to (w,0) */
        *x = rangeno + 1;
        *y = 0;
        *direction = DIR_DOWN;
        return true;
    }
    rangeno -= state->w;
    if (rangeno < state->h) {
        /* RHS; from (w+1, 1) to (w+1, h) */
        *x = state->w+1;
        *y = rangeno + 1;
        *direction = DIR_LEFT;
        return true;
    }
    rangeno -= state->h;
    if (rangeno < state->w) {
        /* bottom row; from (1, h+1) to (w, h+1); counts backwards */
        *x = (state->w - rangeno);
        *y = state->h+1;
        *direction = DIR_UP;
        return true;
    }
    rangeno -= state->w;
    if (rangeno < state->h) {
        /* LHS; from (0, 1) to (0, h); counts backwards */
        *x = 0;
        *y = (state->h - rangeno);
        *direction = DIR_RIGHT;
        return true;
    }
    return false;
}

static bool grid2range(const game_state *state, int x, int y, int *rangeno)
{
    int ret, x1 = state->w+1, y1 = state->h+1;

    if (x > 0 && x < x1 && y > 0 && y < y1) return false; /* in arena */
    if (x < 0 || x > x1 || y < 0 || y > y1) return false; /* outside grid */

    if ((x == 0 || x == x1) && (y == 0 || y == y1))
        return false; /* one of 4 corners */

    if (y == 0) {               /* top line */
        ret = x - 1;
    } else if (x == x1) {       /* RHS */
        ret = y - 1 + state->w;
    } else if (y == y1) {       /* Bottom [and counts backwards] */
        ret = (state->w - x) + state->w + state->h;
    } else {                    /* LHS [and counts backwards ] */
        ret = (state->h-y) + state->w + state->w + state->h;
    }
    *rangeno = ret;
    debug(("grid2range: (%d,%d) rangeno = %d\n", x, y, ret));
    return true;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    int dlen = strlen(desc), i;
    unsigned char *bmp;

    state->minballs = params->minballs;
    state->maxballs = params->maxballs;
    state->nballs = ((dlen/2)-2)/2;

    bmp = hex2bin(desc, state->nballs*2 + 2);
    obfuscate_bitmap(bmp, (state->nballs*2 + 2) * 8, true);

    state->w = bmp[0]; state->h = bmp[1];
    state->nlasers = 2 * (state->w + state->h);

    state->grid = snewn((state->w+2)*(state->h+2), unsigned int);
    memset(state->grid, 0, (state->w+2)*(state->h+2) * sizeof(unsigned int));

    state->exits = snewn(state->nlasers, unsigned int);
    memset(state->exits, LASER_EMPTY, state->nlasers * sizeof(unsigned int));

    for (i = 0; i < state->nballs; i++) {
        GRID(state, bmp[(i+1)*2 + 0]+1, bmp[(i+1)*2 + 1]+1) = BALL_CORRECT;
    }
    sfree(bmp);

    state->done = false;
    state->justwrong = false;
    state->reveal = false;
    state->nguesses = state->nright = state->nwrong = state->nmissed = 0;
    state->laserno = 1;

    return state;
}

#define XFER(x) ret->x = state->x

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    XFER(w); XFER(h);
    XFER(minballs); XFER(maxballs);
    XFER(nballs); XFER(nlasers);

    ret->grid = snewn((ret->w+2)*(ret->h+2), unsigned int);
    memcpy(ret->grid, state->grid, (ret->w+2)*(ret->h+2) * sizeof(unsigned int));
    ret->exits = snewn(ret->nlasers, unsigned int);
    memcpy(ret->exits, state->exits, ret->nlasers * sizeof(unsigned int));

    XFER(done);
    XFER(laserno);
    XFER(nguesses);
    XFER(reveal);
    XFER(justwrong);
    XFER(nright); XFER(nwrong); XFER(nmissed);

    return ret;
}

#undef XFER

static void free_game(game_state *state)
{
    sfree(state->exits);
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return dupstr("S");
}

struct game_ui {
    int flash_laserno;
    int errors;
    bool newmove;
    int cur_x, cur_y;
    bool cur_visible;
    int flash_laser; /* 0 = never, 1 = always, 2 = if anim. */
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->flash_laserno = LASER_EMPTY;
    ui->errors = 0;
    ui->newmove = false;

    ui->cur_x = ui->cur_y = 1;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);

    ui->flash_laser = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    char buf[80];
    /*
     * The error counter needs preserving across a serialisation.
     */
    sprintf(buf, "E%d", ui->errors);
    return dupstr(buf);
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    sscanf(encoding, "E%d", &ui->errors);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    /*
     * If we've encountered a `justwrong' state as a result of
     * actually making a move, increment the ui error counter.
     */
    if (newstate->justwrong && ui->newmove)
	ui->errors++;
    ui->newmove = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (IS_CURSOR_SELECT(button) && ui->cur_visible && !state->reveal) {
        int gx = ui->cur_x, gy = ui->cur_y, rangeno = -1;
        if (gx == 0 && gy == 0 && button == CURSOR_SELECT) return "Check";
        if (gx >= 1 && gx <= state->w && gy >= 1 && gy <= state->h) {
            /* Cursor somewhere in the arena. */
            if (button == CURSOR_SELECT && !(GRID(state, gx,gy) & BALL_LOCK))
                return (GRID(state, gx, gy) & BALL_GUESS) ? "Clear" : "Ball";
            if (button == CURSOR_SELECT2)
                return (GRID(state, gx, gy) & BALL_LOCK) ? "Unlock" : "Lock";
        }
        if (grid2range(state, gx, gy, &rangeno)) {
            if (button == CURSOR_SELECT &&
                state->exits[rangeno] == LASER_EMPTY)
                return "Fire";
            if (button == CURSOR_SELECT2) {
                int n = 0;
                /* Row or column lock or unlock. */
                if (gy == 0 || gy > state->h) { /* Column lock */
                    for (gy = 1; gy <= state->h; gy++)
                        n += !!(GRID(state, gx, gy) & BALL_LOCK);
                    return n > state->h/2 ? "Unlock" : "Lock";
                } else { /* Row lock */
                    for (gx = 1; gx <= state->w; gx++)
                        n += !!(GRID(state, gx, gy) & BALL_LOCK);
                    return n > state->w/2 ? "Unlock" : "Lock";
                }
            }
        }
    }
    return "";
}

#define OFFSET(gx,gy,o) do {                                    \
    int off = (4 + (o) % 4) % 4;                                \
    (gx) += offsets[off].x;                                     \
    (gy) += offsets[off].y;                                     \
} while(0)

enum { LOOK_LEFT, LOOK_FORWARD, LOOK_RIGHT };

/* Given a position and a direction, check whether we can see a ball in front
 * of us, or to our front-left or front-right. */
static bool isball(game_state *state, int gx, int gy, int direction, int lookwhere)
{
    debug(("isball, (%d, %d), dir %s, lookwhere %s\n", gx, gy, dirstrs[direction],
           lookwhere == LOOK_LEFT ? "LEFT" :
           lookwhere == LOOK_FORWARD ? "FORWARD" : "RIGHT"));
    OFFSET(gx,gy,direction);
    if (lookwhere == LOOK_LEFT)
        OFFSET(gx,gy,direction-1);
    else if (lookwhere == LOOK_RIGHT)
        OFFSET(gx,gy,direction+1);
    else if (lookwhere != LOOK_FORWARD)
        assert(!"unknown lookwhere");

    debug(("isball, new (%d, %d)\n", gx, gy));

    /* if we're off the grid (into the firing range) there's never a ball. */
    if (gx < 1 || gy < 1 || gx > state->w || gy > state->h)
        return false;

    if (GRID(state, gx,gy) & BALL_CORRECT)
        return true;

    return false;
}

static int fire_laser_internal(game_state *state, int x, int y, int direction)
{
    int unused, lno;
    bool tmp;

    tmp = grid2range(state, x, y, &lno);
    assert(tmp);

    /* deal with strange initial reflection rules (that stop
     * you turning down the laser range) */

    /* I've just chosen to prioritise instant-hit over instant-reflection;
     * I can't find anywhere that gives me a definite algorithm for this. */
    if (isball(state, x, y, direction, LOOK_FORWARD)) {
        debug(("Instant hit at (%d, %d)\n", x, y));
	return LASER_HIT;	       /* hit */
    }

    if (isball(state, x, y, direction, LOOK_LEFT) ||
        isball(state, x, y, direction, LOOK_RIGHT)) {
        debug(("Instant reflection at (%d, %d)\n", x, y));
	return LASER_REFLECT;	       /* reflection */
    }
    /* move us onto the grid. */
    OFFSET(x, y, direction);

    while (1) {
        debug(("fire_laser: looping at (%d, %d) pointing %s\n",
               x, y, dirstrs[direction]));
        if (grid2range(state, x, y, &unused)) {
            int exitno;

	    tmp = grid2range(state, x, y, &exitno);
	    assert(tmp);

	    return (lno == exitno ? LASER_REFLECT : exitno);
        }
        /* paranoia. This obviously should never happen */
        assert(!(GRID(state, x, y) & BALL_CORRECT));

        if (isball(state, x, y, direction, LOOK_FORWARD)) {
            /* we're facing a ball; send back a reflection. */
            debug(("Ball ahead of (%d, %d)", x, y));
            return LASER_HIT;	       /* hit */
        }

        if (isball(state, x, y, direction, LOOK_LEFT)) {
            /* ball to our left; rotate clockwise and look again. */
            debug(("Ball to left; turning clockwise.\n"));
            direction += 1; direction %= 4;
            continue;
        }
        if (isball(state, x, y, direction, LOOK_RIGHT)) {
            /* ball to our right; rotate anti-clockwise and look again. */
            debug(("Ball to rightl turning anti-clockwise.\n"));
            direction += 3; direction %= 4;
            continue;
        }
        /* ... otherwise, we have no balls ahead of us so just move one step. */
        debug(("No balls; moving forwards.\n"));
        OFFSET(x, y, direction);
    }
}

static int laser_exit(game_state *state, int entryno)
{
    int x, y, direction;
    bool tmp;

    tmp = range2grid(state, entryno, &x, &y, &direction);
    assert(tmp);

    return fire_laser_internal(state, x, y, direction);
}

static void fire_laser(game_state *state, int entryno)
{
    int exitno, x, y, direction;
    bool tmp;

    tmp = range2grid(state, entryno, &x, &y, &direction);
    assert(tmp);

    exitno = fire_laser_internal(state, x, y, direction);

    if (exitno == LASER_HIT || exitno == LASER_REFLECT) {
	GRID(state, x, y) = state->exits[entryno] = exitno;
    } else {
	int newno = state->laserno++;
	int xend, yend, unused;
	tmp = range2grid(state, exitno, &xend, &yend, &unused);
	assert(tmp);
	GRID(state, x, y) = GRID(state, xend, yend) = newno;
	state->exits[entryno] = exitno;
	state->exits[exitno] = entryno;
    }
}

/* Checks that the guessed balls in the state match up with the real balls
 * for all possible lasers (i.e. not just the ones that the player might
 * have already guessed). This is required because any layout with >4 balls
 * might have multiple valid solutions. Returns non-zero for a 'correct'
 * (i.e. consistent) layout. */
static int check_guesses(game_state *state, bool cagey)
{
    game_state *solution, *guesses;
    int i, x, y, n, unused, tmp;
    bool tmpb;
    int ret = 0;

    if (cagey) {
	/*
	 * First, check that each laser the player has already
	 * fired is consistent with the layout. If not, show them
	 * one error they've made and reveal no further
	 * information.
	 *
	 * Failing that, check to see whether the player would have
	 * been able to fire any laser which distinguished the real
	 * solution from their guess. If so, show them one such
	 * laser and reveal no further information.
	 */
	guesses = dup_game(state);
	/* clear out BALL_CORRECT on guess, make BALL_GUESS BALL_CORRECT. */
	for (x = 1; x <= state->w; x++) {
	    for (y = 1; y <= state->h; y++) {
		GRID(guesses, x, y) &= ~BALL_CORRECT;
		if (GRID(guesses, x, y) & BALL_GUESS)
		    GRID(guesses, x, y) |= BALL_CORRECT;
	    }
	}
	n = 0;
	for (i = 0; i < guesses->nlasers; i++) {
	    if (guesses->exits[i] != LASER_EMPTY &&
		guesses->exits[i] != laser_exit(guesses, i))
		n++;
	}
	if (n) {
	    /*
	     * At least one of the player's existing lasers
	     * contradicts their ball placement. Pick a random one,
	     * highlight it, and return.
	     *
	     * A temporary random state is created from the current
	     * grid, so that repeating the same marking will give
	     * the same answer instead of a different one.
	     */
	    random_state *rs = random_new((char *)guesses->grid,
					  (state->w+2)*(state->h+2) *
					  sizeof(unsigned int));
	    n = random_upto(rs, n);
	    random_free(rs);
	    for (i = 0; i < guesses->nlasers; i++) {
		if (guesses->exits[i] != LASER_EMPTY &&
		    guesses->exits[i] != laser_exit(guesses, i) &&
		    n-- == 0) {
		    state->exits[i] |= LASER_WRONG;
		    tmp = laser_exit(state, i);
		    if (RANGECHECK(state, tmp))
			state->exits[tmp] |= LASER_WRONG;
		    state->justwrong = true;
		    free_game(guesses);
		    return 0;
		}
	    }
	}
	n = 0;
	for (i = 0; i < guesses->nlasers; i++) {
	    if (guesses->exits[i] == LASER_EMPTY &&
		laser_exit(state, i) != laser_exit(guesses, i))
		n++;
	}
	if (n) {
	    /*
	     * At least one of the player's unfired lasers would
	     * demonstrate their ball placement to be wrong. Pick a
	     * random one, highlight it, and return.
	     *
	     * A temporary random state is created from the current
	     * grid, so that repeating the same marking will give
	     * the same answer instead of a different one.
	     */
	    random_state *rs = random_new((char *)guesses->grid,
					  (state->w+2)*(state->h+2) *
					  sizeof(unsigned int));
	    n = random_upto(rs, n);
	    random_free(rs);
	    for (i = 0; i < guesses->nlasers; i++) {
		if (guesses->exits[i] == LASER_EMPTY &&
		    laser_exit(state, i) != laser_exit(guesses, i) &&
		    n-- == 0) {
		    fire_laser(state, i);
		    state->exits[i] |= LASER_OMITTED;
		    tmp = laser_exit(state, i);
		    if (RANGECHECK(state, tmp))
			state->exits[tmp] |= LASER_OMITTED;
		    state->justwrong = true;
		    free_game(guesses);
		    return 0;
		}
	    }
	}
	free_game(guesses);
    }

    /* duplicate the state (to solution) */
    solution = dup_game(state);

    /* clear out the lasers of solution */
    for (i = 0; i < solution->nlasers; i++) {
        tmpb = range2grid(solution, i, &x, &y, &unused);
        assert(tmpb);
        GRID(solution, x, y) = 0;
        solution->exits[i] = LASER_EMPTY;
    }

    /* duplicate solution to guess. */
    guesses = dup_game(solution);

    /* clear out BALL_CORRECT on guess, make BALL_GUESS BALL_CORRECT. */
    for (x = 1; x <= state->w; x++) {
        for (y = 1; y <= state->h; y++) {
            GRID(guesses, x, y) &= ~BALL_CORRECT;
            if (GRID(guesses, x, y) & BALL_GUESS)
                GRID(guesses, x, y) |= BALL_CORRECT;
        }
    }

    /* for each laser (on both game_states), fire it if it hasn't been fired.
     * If one has been fired (or received a hit) and another hasn't, we know
     * the ball layouts didn't match and can short-circuit return. */
    for (i = 0; i < solution->nlasers; i++) {
        if (solution->exits[i] == LASER_EMPTY)
            fire_laser(solution, i);
        if (guesses->exits[i] == LASER_EMPTY)
            fire_laser(guesses, i);
    }

    /* check each game_state's laser against the other; if any differ, return 0 */
    ret = 1;
    for (i = 0; i < solution->nlasers; i++) {
        tmpb = range2grid(solution, i, &x, &y, &unused);
        assert(tmpb);

        if (solution->exits[i] != guesses->exits[i]) {
            /* If the original state didn't have this shot fired,
             * and it would be wrong between the guess and the solution,
             * add it. */
            if (state->exits[i] == LASER_EMPTY) {
                state->exits[i] = solution->exits[i];
                if (state->exits[i] == LASER_REFLECT ||
                    state->exits[i] == LASER_HIT)
                    GRID(state, x, y) = state->exits[i];
                else {
                    /* add a new shot, incrementing state's laser count. */
                    int ex, ey, newno = state->laserno++;
                    tmpb = range2grid(state, state->exits[i], &ex, &ey, &unused);
                    assert(tmpb);
                    GRID(state, x, y) = newno;
                    GRID(state, ex, ey) = newno;
                }
		state->exits[i] |= LASER_OMITTED;
            } else {
		state->exits[i] |= LASER_WRONG;
	    }
            ret = 0;
        }
    }
    if (ret == 0 ||
	state->nguesses < state->minballs ||
	state->nguesses > state->maxballs) goto done;

    /* fix up original state so the 'correct' balls end up matching the guesses,
     * as we've just proved that they were equivalent. */
    for (x = 1; x <= state->w; x++) {
        for (y = 1; y <= state->h; y++) {
            if (GRID(state, x, y) & BALL_GUESS)
                GRID(state, x, y) |= BALL_CORRECT;
            else
                GRID(state, x, y) &= ~BALL_CORRECT;
        }
    }

done:
    /* fill in nright and nwrong. */
    state->nright = state->nwrong = state->nmissed = 0;
    for (x = 1; x <= state->w; x++) {
        for (y = 1; y <= state->h; y++) {
            int bs = GRID(state, x, y) & (BALL_GUESS | BALL_CORRECT);
            if (bs == (BALL_GUESS | BALL_CORRECT))
                state->nright++;
            else if (bs == BALL_GUESS)
                state->nwrong++;
            else if (bs == BALL_CORRECT)
                state->nmissed++;
        }
    }
    free_game(solution);
    free_game(guesses);
    state->reveal = true;
    return ret;
}

#define TILE_SIZE (ds->tilesize)

#define TODRAW(x) ((TILE_SIZE * (x)) + (TILE_SIZE / 2))
#define FROMDRAW(x) (((x) - (TILE_SIZE / 2)) / TILE_SIZE)

#define CAN_REVEAL(state) ((state)->nguesses >= (state)->minballs && \
			   (state)->nguesses <= (state)->maxballs && \
			   !(state)->reveal && !(state)->justwrong)

struct game_drawstate {
    int tilesize, crad, rrad, w, h; /* w and h to make macros work... */
    unsigned int *grid;          /* as the game_state grid */
    bool started, reveal, isflash;
    int flash_laserno;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gx = -1, gy = -1, rangeno = -1, wouldflash = 0;
    enum { NONE, TOGGLE_BALL, TOGGLE_LOCK, FIRE, REVEAL,
           TOGGLE_COLUMN_LOCK, TOGGLE_ROW_LOCK} action = NONE;
    char buf[80], *nullret = NULL;

    if (IS_CURSOR_MOVE(button)) {
        int cx = ui->cur_x, cy = ui->cur_y;

        move_cursor(button, &cx, &cy, state->w+2, state->h+2, false);
        if ((cx == 0 && cy == 0 && !CAN_REVEAL(state)) ||
            (cx == 0 && cy == state->h+1) ||
            (cx == state->w+1 && cy == 0) ||
            (cx == state->w+1 && cy == state->h+1))
            return NULL; /* disallow moving cursor to corners. */
        ui->cur_x = cx;
        ui->cur_y = cy;
        ui->cur_visible = true;
        return UI_UPDATE;
    }

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        gx = FROMDRAW(x);
        gy = FROMDRAW(y);
        ui->cur_visible = false;
        wouldflash = 1;
    } else if (button == LEFT_RELEASE) {
        ui->flash_laser = 0;
        return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (ui->cur_visible) {
            gx = ui->cur_x;
            gy = ui->cur_y;
            ui->flash_laser = 0;
            wouldflash = 2;
        } else {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        /* Fix up 'button' for the below logic. */
        if (button == CURSOR_SELECT2) button = RIGHT_BUTTON;
        else button = LEFT_BUTTON;
    }

    if (gx != -1 && gy != -1) {
        if (gx == 0 && gy == 0 && button == LEFT_BUTTON)
            action = REVEAL;
        if (gx >= 1 && gx <= state->w && gy >= 1 && gy <= state->h) {
            if (button == LEFT_BUTTON) {
                if (!(GRID(state, gx,gy) & BALL_LOCK))
                    action = TOGGLE_BALL;
            } else
                action = TOGGLE_LOCK;
        }
        if (grid2range(state, gx, gy, &rangeno)) {
            if (button == LEFT_BUTTON)
                action = FIRE;
            else if (gy == 0 || gy > state->h)
                action = TOGGLE_COLUMN_LOCK; /* and use gx */
            else
                action = TOGGLE_ROW_LOCK;    /* and use gy */
        }
    }

    switch (action) {
    case TOGGLE_BALL:
        sprintf(buf, "T%d,%d", gx, gy);
        break;

    case TOGGLE_LOCK:
        sprintf(buf, "LB%d,%d", gx, gy);
        break;

    case TOGGLE_COLUMN_LOCK:
        sprintf(buf, "LC%d", gx);
        break;

    case TOGGLE_ROW_LOCK:
        sprintf(buf, "LR%d", gy);
        break;

    case FIRE:
	if (state->reveal && state->exits[rangeno] == LASER_EMPTY)
	    return nullret;
        ui->flash_laserno = rangeno;
        ui->flash_laser = wouldflash;
        nullret = UI_UPDATE;
        if (state->exits[rangeno] != LASER_EMPTY)
            return UI_UPDATE;
        sprintf(buf, "F%d", rangeno);
        break;

    case REVEAL:
        if (!CAN_REVEAL(state)) return nullret;
        if (ui->cur_visible) ui->cur_x = ui->cur_y = 1;
        sprintf(buf, "R");
        break;

    default:
        return nullret;
    }
    if (state->reveal) return nullret;
    ui->newmove = true;
    return dupstr(buf);
}

static game_state *execute_move(const game_state *from, const char *move)
{
    game_state *ret = dup_game(from);
    int gx = -1, gy = -1, rangeno = -1;

    if (ret->justwrong) {
	int i;
	ret->justwrong = false;
	for (i = 0; i < ret->nlasers; i++)
	    if (ret->exits[i] != LASER_EMPTY)
		ret->exits[i] &= ~(LASER_OMITTED | LASER_WRONG);
    }

    if (!strcmp(move, "S")) {
        check_guesses(ret, false);
        return ret;
    }

    if (from->reveal) goto badmove;
    if (!*move) goto badmove;

    switch (move[0]) {
    case 'T':
        sscanf(move+1, "%d,%d", &gx, &gy);
        if (gx < 1 || gy < 1 || gx > ret->w || gy > ret->h)
            goto badmove;
        if (GRID(ret, gx, gy) & BALL_GUESS) {
            ret->nguesses--;
            GRID(ret, gx, gy) &= ~BALL_GUESS;
        } else {
            ret->nguesses++;
            GRID(ret, gx, gy) |= BALL_GUESS;
        }
        break;

    case 'F':
        sscanf(move+1, "%d", &rangeno);
        if (!RANGECHECK(ret, rangeno))
            goto badmove;
        if (ret->exits[rangeno] != LASER_EMPTY)
            goto badmove;
        fire_laser(ret, rangeno);
        break;

    case 'R':
        if (ret->nguesses < ret->minballs ||
            ret->nguesses > ret->maxballs)
            goto badmove;
        check_guesses(ret, true);
        break;

    case 'L':
        {
            int lcount = 0;
            if (strlen(move) < 2) goto badmove;
            switch (move[1]) {
            case 'B':
                sscanf(move+2, "%d,%d", &gx, &gy);
                if (gx < 1 || gy < 1 || gx > ret->w || gy > ret->h)
                    goto badmove;
                GRID(ret, gx, gy) ^= BALL_LOCK;
                break;

#define COUNTLOCK do { if (GRID(ret, gx, gy) & BALL_LOCK) lcount++; } while (0)
#define SETLOCKIF(c) do {                                       \
    if (lcount > (c)) GRID(ret, gx, gy) &= ~BALL_LOCK;          \
    else              GRID(ret, gx, gy) |= BALL_LOCK;           \
} while(0)

            case 'C':
                sscanf(move+2, "%d", &gx);
                if (gx < 1 || gx > ret->w) goto badmove;
                for (gy = 1; gy <= ret->h; gy++) { COUNTLOCK; }
                for (gy = 1; gy <= ret->h; gy++) { SETLOCKIF(ret->h/2); }
                break;

            case 'R':
                sscanf(move+2, "%d", &gy);
                if (gy < 1 || gy > ret->h) goto badmove;
                for (gx = 1; gx <= ret->w; gx++) { COUNTLOCK; }
                for (gx = 1; gx <= ret->w; gx++) { SETLOCKIF(ret->w/2); }
                break;

#undef COUNTLOCK
#undef SETLOCKIF

            default:
                goto badmove;
            }
        }
        break;

    default:
        goto badmove;
    }

    return ret;

badmove:
    free_game(ret);
    return NULL;
}


static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        *x = TODRAW(ui->cur_x);
        *y = TODRAW(ui->cur_y);
        *w = *h = TILE_SIZE;
    }
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Border is ts/2, to make things easier.
     * Thus we have (width) + 2 (firing range*2) + 1 (border*2) tiles
     * across, and similarly height + 2 + 1 tiles down. */
    *x = (params->w + 3) * tilesize;
    *y = (params->h + 3) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->crad = (tilesize-1)/2;
    ds->rrad = (3*tilesize)/8;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_BALL * 3 + 0] = 0.0F;
    ret[COL_BALL * 3 + 1] = 0.0F;
    ret[COL_BALL * 3 + 2] = 0.0F;

    ret[COL_WRONG * 3 + 0] = 1.0F;
    ret[COL_WRONG * 3 + 1] = 0.0F;
    ret[COL_WRONG * 3 + 2] = 0.0F;

    ret[COL_BUTTON * 3 + 0] = 0.0F;
    ret[COL_BUTTON * 3 + 1] = 1.0F;
    ret[COL_BUTTON * 3 + 2] = 0.0F;

    ret[COL_CURSOR * 3 + 0] = 1.0F;
    ret[COL_CURSOR * 3 + 1] = 0.0F;
    ret[COL_CURSOR * 3 + 2] = 0.0F;

    for (i = 0; i < 3; i++) {
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.9F;
        ret[COL_LOCK * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.7F;
        ret[COL_COVER * 3 + i] = ret[COL_BACKGROUND * 3 + i] * 0.5F;
        ret[COL_TEXT * 3 + i] = 0.0F;
    }

    ret[COL_FLASHTEXT * 3 + 0] = 0.0F;
    ret[COL_FLASHTEXT * 3 + 1] = 1.0F;
    ret[COL_FLASHTEXT * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->w = state->w; ds->h = state->h;
    ds->grid = snewn((state->w+2)*(state->h+2), unsigned int);
    memset(ds->grid, 0, (state->w+2)*(state->h+2)*sizeof(unsigned int));
    ds->started = false;
    ds->reveal = false;
    ds->flash_laserno = LASER_EMPTY;
    ds->isflash = false;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

static void draw_square_cursor(drawing *dr, game_drawstate *ds, int dx, int dy)
{
    int coff = TILE_SIZE/8;
    draw_rect_outline(dr, dx + coff, dy + coff,
                      TILE_SIZE - coff*2,
                      TILE_SIZE - coff*2,
                      COL_CURSOR);
}


static void draw_arena_tile(drawing *dr, const game_state *gs,
                            game_drawstate *ds, const game_ui *ui,
                            int ax, int ay, bool force, bool isflash)
{
    int gx = ax+1, gy = ay+1;
    int gs_tile = GRID(gs, gx, gy), ds_tile = GRID(ds, gx, gy);
    int dx = TODRAW(gx), dy = TODRAW(gy);

    if (ui->cur_visible && ui->cur_x == gx && ui->cur_y == gy)
        gs_tile |= FLAG_CURSOR;

    if (gs_tile != ds_tile || gs->reveal != ds->reveal || force) {
        int bcol, ocol, bg;

        bg = (gs->reveal ? COL_BACKGROUND :
              (gs_tile & BALL_LOCK) ? COL_LOCK : COL_COVER);

        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, bg);
        draw_rect_outline(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_GRID);

        if (gs->reveal) {
            /* Guessed balls are always black; if they're incorrect they'll
             * have a red cross added later.
             * Missing balls are red. */
            if (gs_tile & BALL_GUESS) {
                bcol = isflash ? bg : COL_BALL;
            } else if (gs_tile & BALL_CORRECT) {
                bcol = isflash ? bg : COL_WRONG;
            } else {
                bcol = bg;
            }
        } else {
            /* guesses are black/black, all else background. */
            if (gs_tile & BALL_GUESS) {
                bcol = COL_BALL;
            } else {
                bcol = bg;
            }
        }
        ocol = (gs_tile & FLAG_CURSOR && bcol != bg) ? COL_CURSOR : bcol;

        draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2, ds->crad-1,
                    ocol, ocol);
        draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2, ds->crad-3,
                    bcol, bcol);


        if (gs_tile & FLAG_CURSOR && bcol == bg)
            draw_square_cursor(dr, ds, dx, dy);

        if (gs->reveal &&
            (gs_tile & BALL_GUESS) &&
            !(gs_tile & BALL_CORRECT)) {
            int x1 = dx + 3, y1 = dy + 3;
            int x2 = dx + TILE_SIZE - 3, y2 = dy + TILE_SIZE-3;
	    int coords[8];

            /* Incorrect guess; draw a red cross over the ball. */
	    coords[0] = x1-1;
	    coords[1] = y1+1;
	    coords[2] = x1+1;
	    coords[3] = y1-1;
	    coords[4] = x2+1;
	    coords[5] = y2-1;
	    coords[6] = x2-1;
	    coords[7] = y2+1;
	    draw_polygon(dr, coords, 4, COL_WRONG, COL_WRONG);
	    coords[0] = x2+1;
	    coords[1] = y1+1;
	    coords[2] = x2-1;
	    coords[3] = y1-1;
	    coords[4] = x1-1;
	    coords[5] = y2-1;
	    coords[6] = x1+1;
	    coords[7] = y2+1;
	    draw_polygon(dr, coords, 4, COL_WRONG, COL_WRONG);
        }
        draw_update(dr, dx, dy, TILE_SIZE, TILE_SIZE);
    }
    GRID(ds,gx,gy) = gs_tile;
}

static void draw_laser_tile(drawing *dr, const game_state *gs,
                            game_drawstate *ds, const game_ui *ui,
                            int lno, bool force)
{
    int gx, gy, dx, dy, unused;
    int wrong, omitted, laserval;
    bool tmp, reflect, hit, flash = false;
    unsigned int gs_tile, ds_tile, exitno;

    tmp = range2grid(gs, lno, &gx, &gy, &unused);
    assert(tmp);
    gs_tile = GRID(gs, gx, gy);
    ds_tile = GRID(ds, gx, gy);
    dx = TODRAW(gx);
    dy = TODRAW(gy);

    wrong = gs->exits[lno] & LASER_WRONG;
    omitted = gs->exits[lno] & LASER_OMITTED;
    exitno = gs->exits[lno] & ~LASER_FLAGMASK;

    reflect = gs_tile & LASER_REFLECT;
    hit = gs_tile & LASER_HIT;
    laserval = gs_tile & ~LASER_FLAGMASK;

    if (lno == ds->flash_laserno)
        gs_tile |= LASER_FLASHED;
    else if (!(gs->exits[lno] & (LASER_HIT | LASER_REFLECT))) {
        if (exitno == ds->flash_laserno)
            gs_tile |= LASER_FLASHED;
    }
    if (gs_tile & LASER_FLASHED) flash = true;

    gs_tile |= wrong | omitted;

    if (ui->cur_visible && ui->cur_x == gx && ui->cur_y == gy)
        gs_tile |= FLAG_CURSOR;

    if (gs_tile != ds_tile || force) {
        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);
        draw_rect_outline(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_GRID);

        if (gs_tile &~ (LASER_WRONG | LASER_OMITTED | FLAG_CURSOR)) {
            char str[32];
            int tcol = flash ? COL_FLASHTEXT : omitted ? COL_WRONG : COL_TEXT;

            if (reflect || hit)
                sprintf(str, "%s", reflect ? "R" : "H");
            else
                sprintf(str, "%d", laserval);

            if (wrong) {
                draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
                            ds->rrad,
                            COL_WRONG, COL_WRONG);
                draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
                            ds->rrad - TILE_SIZE/16,
                            COL_BACKGROUND, COL_WRONG);
            }

            draw_text(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
                      tcol, str);
        }
        if (gs_tile & FLAG_CURSOR)
            draw_square_cursor(dr, ds, dx, dy);

        draw_update(dr, dx, dy, TILE_SIZE, TILE_SIZE);
    }
    GRID(ds, gx, gy) = gs_tile;
}

#define CUR_ANIM 0.2F

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i, x, y, ts = TILE_SIZE;
    bool isflash = false, force = false;

    if (flashtime > 0) {
        int frame = (int)(flashtime / FLASH_FRAME);
        isflash = (frame % 2) == 0;
        debug(("game_redraw: flashtime = %f", flashtime));
    }

    if (!ds->started) {
        int x0 = TODRAW(0)-1, y0 = TODRAW(0)-1;
        int x1 = TODRAW(state->w+2), y1 = TODRAW(state->h+2);

        /* clockwise around the outline starting at pt behind (1,1). */
        draw_line(dr, x0+ts, y0+ts, x0+ts, y0,    COL_HIGHLIGHT);
        draw_line(dr, x0+ts, y0,    x1-ts, y0,    COL_HIGHLIGHT);
        draw_line(dr, x1-ts, y0,    x1-ts, y0+ts, COL_LOWLIGHT);
        draw_line(dr, x1-ts, y0+ts, x1,    y0+ts, COL_HIGHLIGHT);
        draw_line(dr, x1,    y0+ts, x1,    y1-ts, COL_LOWLIGHT);
        draw_line(dr, x1,    y1-ts, x1-ts, y1-ts, COL_LOWLIGHT);
        draw_line(dr, x1-ts, y1-ts, x1-ts, y1,    COL_LOWLIGHT);
        draw_line(dr, x1-ts, y1,    x0+ts, y1,    COL_LOWLIGHT);
        draw_line(dr, x0+ts, y1,    x0+ts, y1-ts, COL_HIGHLIGHT);
        draw_line(dr, x0+ts, y1-ts, x0,    y1-ts, COL_LOWLIGHT);
        draw_line(dr, x0,    y1-ts, x0,    y0+ts, COL_HIGHLIGHT);
        draw_line(dr, x0,    y0+ts, x0+ts, y0+ts, COL_HIGHLIGHT);
        /* phew... */

        draw_update(dr, 0, 0,
                    TILE_SIZE * (state->w+3), TILE_SIZE * (state->h+3));
        force = true;
        ds->started = true;
    }

    if (isflash != ds->isflash) force = true;

    /* draw the arena */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            draw_arena_tile(dr, state, ds, ui, x, y, force, isflash);
        }
    }

    /* draw the lasers */
    ds->flash_laserno = LASER_EMPTY;
    if (ui->flash_laser == 1)
        ds->flash_laserno = ui->flash_laserno;
    else if (ui->flash_laser == 2 && animtime > 0)
        ds->flash_laserno = ui->flash_laserno;

    for (i = 0; i < 2*(state->w+state->h); i++) {
        draw_laser_tile(dr, state, ds, ui, i, force);
    }

    /* draw the 'finish' button */
    if (CAN_REVEAL(state)) {
        int outline = (ui->cur_visible && ui->cur_x == 0 && ui->cur_y == 0)
            ? COL_CURSOR : COL_BALL;
        clip(dr, TODRAW(0)-1, TODRAW(0)-1, TILE_SIZE+1, TILE_SIZE+1);
        draw_circle(dr, TODRAW(0) + ds->crad-1, TODRAW(0) + ds->crad-1, ds->crad-1,
                    outline, outline);
        draw_circle(dr, TODRAW(0) + ds->crad-1, TODRAW(0) + ds->crad-1, ds->crad-3,
                    COL_BUTTON, COL_BUTTON);
	unclip(dr);
    } else {
        draw_rect(dr, TODRAW(0)-1, TODRAW(0)-1,
		  TILE_SIZE, TILE_SIZE, COL_BACKGROUND);
    }
    draw_update(dr, TODRAW(0), TODRAW(0), TILE_SIZE, TILE_SIZE);
    ds->reveal = state->reveal;
    ds->isflash = isflash;

    {
        char buf[256];

        if (ds->reveal) {
            if (state->nwrong == 0 &&
                state->nmissed == 0 &&
                state->nright >= state->minballs)
                sprintf(buf, "CORRECT!");
            else
                sprintf(buf, "%d wrong and %d missed balls.",
                        state->nwrong, state->nmissed);
        } else if (state->justwrong) {
	    sprintf(buf, "Wrong! Guess again.");
	} else {
            if (state->nguesses > state->maxballs)
                sprintf(buf, "%d too many balls marked.",
                        state->nguesses - state->maxballs);
            else if (state->nguesses <= state->maxballs &&
                     state->nguesses >= state->minballs)
                sprintf(buf, "Click button to verify guesses.");
            else if (state->maxballs == state->minballs)
                sprintf(buf, "Balls marked: %d / %d",
                        state->nguesses, state->minballs);
            else
                sprintf(buf, "Balls marked: %d / %d-%d.",
                        state->nguesses, state->minballs, state->maxballs);
        }
	if (ui->errors) {
	    sprintf(buf + strlen(buf), " (%d error%s)",
		    ui->errors, ui->errors > 1 ? "s" : "");
	}
        status_bar(dr, buf);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return (ui->flash_laser == 2) ? CUR_ANIM : 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->reveal && newstate->reveal)
        return 4.0F * FLASH_FRAME;
    else
        return 0.0F;
}

static int game_status(const game_state *state)
{
    if (state->reveal) {
        /*
         * We return nonzero whenever the solution has been revealed,
         * even (on spoiler grounds) if it wasn't guessed correctly.
         */
        if (state->nwrong == 0 &&
            state->nmissed == 0 &&
            state->nright >= state->minballs)
            return +1;
        else
            return -1;
    }
    return 0;
}

#ifdef COMBINED
#define thegame blackbox
#endif

const struct game thegame = {
    "Black Box", "games.blackbox", "blackbox",
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
    false, NULL, NULL, /* can_format_as_text_now, text_format */
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
    REQUIRE_RBUTTON,		       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
