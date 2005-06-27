/*
 * pattern.c: the pattern-reconstruction game known as `nonograms'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    COL_EMPTY,
    COL_FULL,
    COL_UNKNOWN,
    COL_GRID,
    NCOLOURS
};

#define PREFERRED_TILE_SIZE 24
#define TILE_SIZE (ds->tilesize)
#define BORDER (3 * TILE_SIZE / 4)
#define TLBORDER(d) ( (d) / 5 + 2 )
#define GUTTER (TILE_SIZE / 2)

#define FROMCOORD(d, x) \
        ( ((x) - (BORDER + GUTTER + TILE_SIZE * TLBORDER(d))) / TILE_SIZE )

#define SIZE(d) (2*BORDER + GUTTER + TILE_SIZE * (TLBORDER(d) + (d)))
#define GETTILESIZE(d, w) (w / (2 + TLBORDER(d) + (d)))

#define TOCOORD(d, x) (BORDER + GUTTER + TILE_SIZE * (TLBORDER(d) + (x)))

struct game_params {
    int w, h;
};

#define GRID_UNKNOWN 2
#define GRID_FULL 1
#define GRID_EMPTY 0

struct game_state {
    int w, h;
    unsigned char *grid;
    int rowsize;
    int *rowdata, *rowlen;
    int completed, cheated;
};

#define FLASH_TIME 0.13F

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 15;

    return ret;
}

static const struct game_params pattern_presets[] = {
    {10, 10},
    {15, 15},
    {20, 20},
#ifndef SLOW_SYSTEM
    {25, 25},
    {30, 30},
#endif
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(pattern_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = pattern_presets[i];

    sprintf(str, "%dx%d", ret->w, ret->h);

    *name = dupstr(str);
    *params = ret;
    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    char const *p = string;

    ret->w = atoi(p);
    while (*p && isdigit(*p)) p++;
    if (*p == 'x') {
        p++;
        ret->h = atoi(p);
        while (*p && isdigit(*p)) p++;
    } else {
        ret->h = ret->w;
    }
}

static char *encode_params(game_params *params, int full)
{
    char ret[400];
    int len;

    len = sprintf(ret, "%dx%d", params->w, params->h);
    assert(len < lenof(ret));
    ret[len] = '\0';

    return dupstr(ret);
}

static config_item *game_configure(game_params *params)
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

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->w <= 0 || params->h <= 0)
	return "Width and height must both be greater than zero";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Puzzle generation code.
 * 
 * For this particular puzzle, it seemed important to me to ensure
 * a unique solution. I do this the brute-force way, by having a
 * solver algorithm alongside the generator, and repeatedly
 * generating a random grid until I find one whose solution is
 * unique. It turns out that this isn't too onerous on a modern PC
 * provided you keep grid size below around 30. Any offers of
 * better algorithms, however, will be very gratefully received.
 * 
 * Another annoyance of this approach is that it limits the
 * available puzzles to those solvable by the algorithm I've used.
 * My algorithm only ever considers a single row or column at any
 * one time, which means it's incapable of solving the following
 * difficult example (found by Bella Image around 1995/6, when she
 * and I were both doing maths degrees):
 * 
 *        2  1  2  1 
 *
 *      +--+--+--+--+
 * 1 1  |  |  |  |  |
 *      +--+--+--+--+
 *   2  |  |  |  |  |
 *      +--+--+--+--+
 *   1  |  |  |  |  |
 *      +--+--+--+--+
 *   1  |  |  |  |  |
 *      +--+--+--+--+
 * 
 * Obviously this cannot be solved by a one-row-or-column-at-a-time
 * algorithm (it would require at least one row or column reading
 * `2 1', `1 2', `3' or `4' to get started). However, it can be
 * proved to have a unique solution: if the top left square were
 * empty, then the only option for the top row would be to fill the
 * two squares in the 1 columns, which would imply the squares
 * below those were empty, leaving no place for the 2 in the second
 * row. Contradiction. Hence the top left square is full, and the
 * unique solution follows easily from that starting point.
 * 
 * (The game ID for this puzzle is 4x4:2/1/2/1/1.1/2/1/1 , in case
 * it's useful to anyone.)
 */

static int float_compare(const void *av, const void *bv)
{
    const float *a = (const float *)av;
    const float *b = (const float *)bv;
    if (*a < *b)
        return -1;
    else if (*a > *b)
        return +1;
    else
        return 0;
}

static void generate(random_state *rs, int w, int h, unsigned char *retgrid)
{
    float *fgrid;
    float *fgrid2;
    int step, i, j;
    float threshold;

    fgrid = snewn(w*h, float);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            fgrid[i*w+j] = random_upto(rs, 100000000UL) / 100000000.F;
        }
    }

    /*
     * The above gives a completely random splattering of black and
     * white cells. We want to gently bias this in favour of _some_
     * reasonably thick areas of white and black, while retaining
     * some randomness and fine detail.
     * 
     * So we evolve the starting grid using a cellular automaton.
     * Currently, I'm doing something very simple indeed, which is
     * to set each square to the average of the surrounding nine
     * cells (or the average of fewer, if we're on a corner).
     */
    for (step = 0; step < 1; step++) {
        fgrid2 = snewn(w*h, float);

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                float sx, xbar;
                int n, p, q;

                /*
                 * Compute the average of the surrounding cells.
                 */
                n = 0;
                sx = 0.F;
                for (p = -1; p <= +1; p++) {
                    for (q = -1; q <= +1; q++) {
                        if (i+p < 0 || i+p >= h || j+q < 0 || j+q >= w)
                            continue;
			/*
			 * An additional special case not mentioned
			 * above: if a grid dimension is 2xn then
			 * we do not average across that dimension
			 * at all. Otherwise a 2x2 grid would
			 * contain four identical squares.
			 */
			if ((h==2 && p!=0) || (w==2 && q!=0))
			    continue;
                        n++;
                        sx += fgrid[(i+p)*w+(j+q)];
                    }
                }
                xbar = sx / n;

                fgrid2[i*w+j] = xbar;
            }
        }

        sfree(fgrid);
        fgrid = fgrid2;
    }

    fgrid2 = snewn(w*h, float);
    memcpy(fgrid2, fgrid, w*h*sizeof(float));
    qsort(fgrid2, w*h, sizeof(float), float_compare);
    threshold = fgrid2[w*h/2];
    sfree(fgrid2);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            retgrid[i*w+j] = (fgrid[i*w+j] >= threshold ? GRID_FULL :
                              GRID_EMPTY);
        }
    }

    sfree(fgrid);
}

static int compute_rowdata(int *ret, unsigned char *start, int len, int step)
{
    int i, n;

    n = 0;

    for (i = 0; i < len; i++) {
        if (start[i*step] == GRID_FULL) {
            int runlen = 1;
            while (i+runlen < len && start[(i+runlen)*step] == GRID_FULL)
                runlen++;
            ret[n++] = runlen;
            i += runlen;
        }

        if (i < len && start[i*step] == GRID_UNKNOWN)
            return -1;
    }

    return n;
}

#define UNKNOWN 0
#define BLOCK 1
#define DOT 2
#define STILL_UNKNOWN 3

static void do_recurse(unsigned char *known, unsigned char *deduced,
                       unsigned char *row, int *data, int len,
                       int freespace, int ndone, int lowest)
{
    int i, j, k;

    if (data[ndone]) {
	for (i=0; i<=freespace; i++) {
	    j = lowest;
	    for (k=0; k<i; k++) row[j++] = DOT;
	    for (k=0; k<data[ndone]; k++) row[j++] = BLOCK;
	    if (j < len) row[j++] = DOT;
	    do_recurse(known, deduced, row, data, len,
                       freespace-i, ndone+1, j);
	}
    } else {
	for (i=lowest; i<len; i++)
	    row[i] = DOT;
	for (i=0; i<len; i++)
	    if (known[i] && known[i] != row[i])
		return;
	for (i=0; i<len; i++)
	    deduced[i] |= row[i];
    }
}

static int do_row(unsigned char *known, unsigned char *deduced,
                  unsigned char *row,
                  unsigned char *start, int len, int step, int *data)
{
    int rowlen, i, freespace, done_any;

    freespace = len+1;
    for (rowlen = 0; data[rowlen]; rowlen++)
	freespace -= data[rowlen]+1;

    for (i = 0; i < len; i++) {
	known[i] = start[i*step];
	deduced[i] = 0;
    }

    do_recurse(known, deduced, row, data, len, freespace, 0, 0);
    done_any = FALSE;
    for (i=0; i<len; i++)
	if (deduced[i] && deduced[i] != STILL_UNKNOWN && !known[i]) {
	    start[i*step] = deduced[i];
	    done_any = TRUE;
	}
    return done_any;
}

static unsigned char *generate_soluble(random_state *rs, int w, int h)
{
    int i, j, done_any, ok, ntries, max;
    unsigned char *grid, *matrix, *workspace;
    int *rowdata;

    grid = snewn(w*h, unsigned char);
    matrix = snewn(w*h, unsigned char);
    max = max(w, h);
    workspace = snewn(max*3, unsigned char);
    rowdata = snewn(max+1, int);

    ntries = 0;

    do {
        ntries++;

        generate(rs, w, h, grid);

        /*
         * The game is a bit too easy if any row or column is
         * completely black or completely white. An exception is
         * made for rows/columns that are under 3 squares,
         * otherwise nothing will ever be successfully generated.
         */
        ok = TRUE;
        if (w > 2) {
            for (i = 0; i < h; i++) {
                int colours = 0;
                for (j = 0; j < w; j++)
                    colours |= (grid[i*w+j] == GRID_FULL ? 2 : 1);
                if (colours != 3)
                    ok = FALSE;
            }
        }
        if (h > 2) {
            for (j = 0; j < w; j++) {
                int colours = 0;
                for (i = 0; i < h; i++)
                    colours |= (grid[i*w+j] == GRID_FULL ? 2 : 1);
                if (colours != 3)
                    ok = FALSE;
            }
        }
        if (!ok)
            continue;

        memset(matrix, 0, w*h);

        do {
            done_any = 0;
            for (i=0; i<h; i++) {
                rowdata[compute_rowdata(rowdata, grid+i*w, w, 1)] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i*w, w, 1, rowdata);
            }
            for (i=0; i<w; i++) {
                rowdata[compute_rowdata(rowdata, grid+i, h, w)] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i, h, w, rowdata);
            }
        } while (done_any);

        ok = TRUE;
        for (i=0; i<h; i++) {
            for (j=0; j<w; j++) {
                if (matrix[i*w+j] == UNKNOWN)
                    ok = FALSE;
            }
        }
    } while (!ok);

    sfree(matrix);
    sfree(workspace);
    sfree(rowdata);
    return grid;
}

struct game_aux_info {
    int w, h;
    unsigned char *grid;
};

static char *new_game_desc(game_params *params, random_state *rs,
			   game_aux_info **aux, int interactive)
{
    unsigned char *grid;
    int i, j, max, rowlen, *rowdata;
    char intbuf[80], *desc;
    int desclen, descpos;

    grid = generate_soluble(rs, params->w, params->h);
    max = max(params->w, params->h);
    rowdata = snewn(max, int);

    /*
     * Save the solved game in an aux_info.
     */
    {
	game_aux_info *ai = snew(game_aux_info);

	ai->w = params->w;
	ai->h = params->h;
        ai->grid = grid;

	*aux = ai;
    }

    /*
     * Seed is a slash-separated list of row contents; each row
     * contents section is a dot-separated list of integers. Row
     * contents are listed in the order (columns left to right,
     * then rows top to bottom).
     * 
     * Simplest way to handle memory allocation is to make two
     * passes, first computing the seed size and then writing it
     * out.
     */
    desclen = 0;
    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowlen = compute_rowdata(rowdata, grid+i, params->h, params->w);
        else
            rowlen = compute_rowdata(rowdata, grid+(i-params->w)*params->w,
                                     params->w, 1);
        if (rowlen > 0) {
            for (j = 0; j < rowlen; j++) {
                desclen += 1 + sprintf(intbuf, "%d", rowdata[j]);
            }
        } else {
            desclen++;
        }
    }
    desc = snewn(desclen, char);
    descpos = 0;
    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowlen = compute_rowdata(rowdata, grid+i, params->h, params->w);
        else
            rowlen = compute_rowdata(rowdata, grid+(i-params->w)*params->w,
                                     params->w, 1);
        if (rowlen > 0) {
            for (j = 0; j < rowlen; j++) {
                int len = sprintf(desc+descpos, "%d", rowdata[j]);
                if (j+1 < rowlen)
                    desc[descpos + len] = '.';
                else
                    desc[descpos + len] = '/';
                descpos += len+1;
            }
        } else {
            desc[descpos++] = '/';
        }
    }
    assert(descpos == desclen);
    assert(desc[desclen-1] == '/');
    desc[desclen-1] = '\0';
    sfree(rowdata);
    return desc;
}

static void game_free_aux_info(game_aux_info *aux)
{
    sfree(aux->grid);
    sfree(aux);
}

static char *validate_desc(game_params *params, char *desc)
{
    int i, n, rowspace;
    char *p;

    for (i = 0; i < params->w + params->h; i++) {
        if (i < params->w)
            rowspace = params->h + 1;
        else
            rowspace = params->w + 1;

        if (*desc && isdigit((unsigned char)*desc)) {
            do {
                p = desc;
                while (desc && isdigit((unsigned char)*desc)) desc++;
                n = atoi(p);
                rowspace -= n+1;

                if (rowspace < 0) {
                    if (i < params->w)
                        return "at least one column contains more numbers than will fit";
                    else
                        return "at least one row contains more numbers than will fit";
                }
            } while (*desc++ == '.');
        } else {
            desc++;                    /* expect a slash immediately */
        }

        if (desc[-1] == '/') {
            if (i+1 == params->w + params->h)
                return "too many row/column specifications";
        } else if (desc[-1] == '\0') {
            if (i+1 < params->w + params->h)
                return "too few row/column specifications";
        } else
            return "unrecognised character in game specification";
    }

    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    int i;
    char *p;
    game_state *state = snew(game_state);

    state->w = params->w;
    state->h = params->h;

    state->grid = snewn(state->w * state->h, unsigned char);
    memset(state->grid, GRID_UNKNOWN, state->w * state->h);

    state->rowsize = max(state->w, state->h);
    state->rowdata = snewn(state->rowsize * (state->w + state->h), int);
    state->rowlen = snewn(state->w + state->h, int);

    state->completed = state->cheated = FALSE;

    for (i = 0; i < params->w + params->h; i++) {
        state->rowlen[i] = 0;
        if (*desc && isdigit((unsigned char)*desc)) {
            do {
                p = desc;
                while (desc && isdigit((unsigned char)*desc)) desc++;
                state->rowdata[state->rowsize * i + state->rowlen[i]++] =
                    atoi(p);
            } while (*desc++ == '.');
        } else {
            desc++;                    /* expect a slash immediately */
        }
    }

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;

    ret->grid = snewn(ret->w * ret->h, unsigned char);
    memcpy(ret->grid, state->grid, ret->w * ret->h);

    ret->rowsize = state->rowsize;
    ret->rowdata = snewn(ret->rowsize * (ret->w + ret->h), int);
    ret->rowlen = snewn(ret->w + ret->h, int);
    memcpy(ret->rowdata, state->rowdata,
           ret->rowsize * (ret->w + ret->h) * sizeof(int));
    memcpy(ret->rowlen, state->rowlen,
           (ret->w + ret->h) * sizeof(int));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->rowdata);
    sfree(state->rowlen);
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			game_aux_info *ai, char **error)
{
    unsigned char *matrix;
    int matrix_needs_freeing;
    int full, empty;
    int w = state->w, h = state->h;
    int i;
    char *ret;

    /*
     * If we already have the solved state in an aux_info, copy it
     * out.
     */
    if (ai) {
	assert(ai->w == w && ai->h == h);

	matrix = ai->grid;
	matrix_needs_freeing = FALSE;
	full = GRID_FULL;
	empty = GRID_EMPTY;
    } else {
	int done_any, max;
	unsigned char *workspace;
	int *rowdata;

	matrix = snewn(w*h, unsigned char);
	max = max(w, h);
	workspace = snewn(max*3, unsigned char);
	rowdata = snewn(max+1, int);

        memset(matrix, 0, w*h);

        do {
            done_any = 0;
            for (i=0; i<h; i++) {
		memcpy(rowdata, state->rowdata + state->rowsize*(w+i),
		       max*sizeof(int));
		rowdata[state->rowlen[w+i]] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i*w, w, 1, rowdata);
            }
            for (i=0; i<w; i++) {
		memcpy(rowdata, state->rowdata + state->rowsize*i, max*sizeof(int));
		rowdata[state->rowlen[i]] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i, h, w, rowdata);
            }
        } while (done_any);

	sfree(workspace);
	sfree(rowdata);

	for (i = 0; i < w*h; i++) {
	    if (matrix[i] != BLOCK && matrix[i] != DOT) {
		sfree(matrix);
		*error = "Solving algorithm cannot complete this puzzle";
		return NULL;
	    }
	}

	matrix_needs_freeing = TRUE;
	full = BLOCK;
	empty = DOT;
    }

    ret = snewn(w*h+2, char);
    ret[0] = 'S';
    for (i = 0; i < w*h; i++) {
	assert(matrix[i] == full || matrix[i] == empty);
	ret[i+1] = (matrix[i] == full ? '1' : '0');
    }
    ret[w*h+1] = '\0';

    if (matrix_needs_freeing)
	sfree(matrix);

    return ret;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

struct game_ui {
    int dragging;
    int drag_start_x;
    int drag_start_y;
    int drag_end_x;
    int drag_end_y;
    int drag, release, state;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ret;

    ret = snew(game_ui);
    ret->dragging = FALSE;

    return ret;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

struct game_drawstate {
    int started;
    int w, h;
    int tilesize;
    unsigned char *visible;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    button &= ~MOD_MASK;

    x = FROMCOORD(state->w, x);
    y = FROMCOORD(state->h, y);

    if (x >= 0 && x < state->w && y >= 0 && y < state->h &&
        (button == LEFT_BUTTON || button == RIGHT_BUTTON ||
         button == MIDDLE_BUTTON)) {

        ui->dragging = TRUE;

        if (button == LEFT_BUTTON) {
            ui->drag = LEFT_DRAG;
            ui->release = LEFT_RELEASE;
            ui->state = GRID_FULL;
        } else if (button == RIGHT_BUTTON) {
            ui->drag = RIGHT_DRAG;
            ui->release = RIGHT_RELEASE;
            ui->state = GRID_EMPTY;
        } else /* if (button == MIDDLE_BUTTON) */ {
            ui->drag = MIDDLE_DRAG;
            ui->release = MIDDLE_RELEASE;
            ui->state = GRID_UNKNOWN;
        }

        ui->drag_start_x = ui->drag_end_x = x;
        ui->drag_start_y = ui->drag_end_y = y;

        return "";		       /* UI activity occurred */
    }

    if (ui->dragging && button == ui->drag) {
        /*
         * There doesn't seem much point in allowing a rectangle
         * drag; people will generally only want to drag a single
         * horizontal or vertical line, so we make that easy by
         * snapping to it.
         * 
         * Exception: if we're _middle_-button dragging to tag
         * things as UNKNOWN, we may well want to trash an entire
         * area and start over!
         */
        if (ui->state != GRID_UNKNOWN) {
            if (abs(x - ui->drag_start_x) > abs(y - ui->drag_start_y))
                y = ui->drag_start_y;
            else
                x = ui->drag_start_x;
        }

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= state->w) x = state->w - 1;
        if (y >= state->h) y = state->h - 1;

        ui->drag_end_x = x;
        ui->drag_end_y = y;

        return "";		       /* UI activity occurred */
    }

    if (ui->dragging && button == ui->release) {
        int x1, x2, y1, y2, xx, yy;
        int move_needed = FALSE;

        x1 = min(ui->drag_start_x, ui->drag_end_x);
        x2 = max(ui->drag_start_x, ui->drag_end_x);
        y1 = min(ui->drag_start_y, ui->drag_end_y);
        y2 = max(ui->drag_start_y, ui->drag_end_y);

        for (yy = y1; yy <= y2; yy++)
            for (xx = x1; xx <= x2; xx++)
                if (state->grid[yy * state->w + xx] != ui->state)
                    move_needed = TRUE;

        ui->dragging = FALSE;

        if (move_needed) {
	    char buf[80];
	    sprintf(buf, "%c%d,%d,%d,%d",
		    (ui->state == GRID_FULL ? 'F' :
		     ui->state == GRID_EMPTY ? 'E' : 'U'),
		    x1, y1, x2-x1+1, y2-y1+1);
	    return dupstr(buf);
        } else
            return "";		       /* UI activity occurred */
    }

    return NULL;
}

static game_state *execute_move(game_state *from, char *move)
{
    game_state *ret;
    int x1, x2, y1, y2, xx, yy;
    int val;

    if (move[0] == 'S' && strlen(move) == from->w * from->h + 1) {
	int i;

	ret = dup_game(from);

	for (i = 0; i < ret->w * ret->h; i++)
	    ret->grid[i] = (move[i+1] == '1' ? GRID_FULL : GRID_EMPTY);

	ret->completed = ret->cheated = TRUE;

	return ret;
    } else if ((move[0] == 'F' || move[0] == 'E' || move[0] == 'U') &&
	sscanf(move+1, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4 &&
	x1 >= 0 && x2 >= 0 && x1+x2 <= from->w &&
	y1 >= 0 && y2 >= 0 && y1+y2 <= from->h) {

	x2 += x1;
	y2 += y1;
	val = (move[0] == 'F' ? GRID_FULL :
		 move[0] == 'E' ? GRID_EMPTY : GRID_UNKNOWN);

	ret = dup_game(from);
	for (yy = y1; yy < y2; yy++)
	    for (xx = x1; xx < x2; xx++)
		ret->grid[yy * ret->w + xx] = val;

	/*
	 * An actual change, so check to see if we've completed the
	 * game.
	 */
	if (!ret->completed) {
	    int *rowdata = snewn(ret->rowsize, int);
	    int i, len;

	    ret->completed = TRUE;

	    for (i=0; i<ret->w; i++) {
		len = compute_rowdata(rowdata,
				      ret->grid+i, ret->h, ret->w);
		if (len != ret->rowlen[i] ||
		    memcmp(ret->rowdata+i*ret->rowsize, rowdata,
			   len * sizeof(int))) {
		    ret->completed = FALSE;
		    break;
		}
	    }
	    for (i=0; i<ret->h; i++) {
		len = compute_rowdata(rowdata,
				      ret->grid+i*ret->w, ret->w, 1);
		if (len != ret->rowlen[i+ret->w] ||
		    memcmp(ret->rowdata+(i+ret->w)*ret->rowsize, rowdata,
			   len * sizeof(int))) {
		    ret->completed = FALSE;
		    break;
		}
	    }

	    sfree(rowdata);
	}

	return ret;
    } else
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_size(game_params *params, game_drawstate *ds,
                      int *x, int *y, int expand)
{
    int ts;

    ts = min(GETTILESIZE(params->w, *x), GETTILESIZE(params->h, *y));
    if (expand)
        ds->tilesize = ts;
    else
        ds->tilesize = min(ts, PREFERRED_TILE_SIZE);

    *x = SIZE(params->w);
    *y = SIZE(params->h);
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.3F;
    ret[COL_GRID * 3 + 1] = 0.3F;
    ret[COL_GRID * 3 + 2] = 0.3F;

    ret[COL_UNKNOWN * 3 + 0] = 0.5F;
    ret[COL_UNKNOWN * 3 + 1] = 0.5F;
    ret[COL_UNKNOWN * 3 + 2] = 0.5F;

    ret[COL_FULL * 3 + 0] = 0.0F;
    ret[COL_FULL * 3 + 1] = 0.0F;
    ret[COL_FULL * 3 + 2] = 0.0F;

    ret[COL_EMPTY * 3 + 0] = 1.0F;
    ret[COL_EMPTY * 3 + 1] = 1.0F;
    ret[COL_EMPTY * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->visible = snewn(ds->w * ds->h, unsigned char);
    ds->tilesize = 0;                  /* not decided yet */
    memset(ds->visible, 255, ds->w * ds->h);

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->visible);
    sfree(ds);
}

static void grid_square(frontend *fe, game_drawstate *ds,
                        int y, int x, int state)
{
    int xl, xr, yt, yb;

    draw_rect(fe, TOCOORD(ds->w, x), TOCOORD(ds->h, y),
              TILE_SIZE, TILE_SIZE, COL_GRID);

    xl = (x % 5 == 0 ? 1 : 0);
    yt = (y % 5 == 0 ? 1 : 0);
    xr = (x % 5 == 4 || x == ds->w-1 ? 1 : 0);
    yb = (y % 5 == 4 || y == ds->h-1 ? 1 : 0);

    draw_rect(fe, TOCOORD(ds->w, x) + 1 + xl, TOCOORD(ds->h, y) + 1 + yt,
              TILE_SIZE - xl - xr - 1, TILE_SIZE - yt - yb - 1,
              (state == GRID_FULL ? COL_FULL :
               state == GRID_EMPTY ? COL_EMPTY : COL_UNKNOWN));

    draw_update(fe, TOCOORD(ds->w, x), TOCOORD(ds->h, y),
                TILE_SIZE, TILE_SIZE);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                        game_state *state, int dir, game_ui *ui,
                        float animtime, float flashtime)
{
    int i, j;
    int x1, x2, y1, y2;

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed
         * and can vary with front ends. To be on the safe side,
         * all games should start by drawing a big background-
         * colour rectangle covering the whole window.
         */
        draw_rect(fe, 0, 0, SIZE(ds->w), SIZE(ds->h), COL_BACKGROUND);

        /*
         * Draw the numbers.
         */
        for (i = 0; i < ds->w + ds->h; i++) {
            int rowlen = state->rowlen[i];
            int *rowdata = state->rowdata + state->rowsize * i;
	    int nfit;

	    /*
	     * Normally I space the numbers out by the same
	     * distance as the tile size. However, if there are
	     * more numbers than available spaces, I have to squash
	     * them up a bit.
	     */
	    nfit = max(rowlen, TLBORDER(ds->h))-1;
	    assert(nfit > 0);

            for (j = 0; j < rowlen; j++) {
                int x, y;
                char str[80];

                if (i < ds->w) {
                    x = TOCOORD(ds->w, i);
                    y = BORDER + TILE_SIZE * (TLBORDER(ds->h)-1);
		    y -= ((rowlen-j-1)*TILE_SIZE) * (TLBORDER(ds->h)-1) / nfit;
                } else {
                    y = TOCOORD(ds->h, i - ds->w);
                    x = BORDER + TILE_SIZE * (TLBORDER(ds->w)-1);
		    x -= ((rowlen-j-1)*TILE_SIZE) * (TLBORDER(ds->h)-1) / nfit;
                }

                sprintf(str, "%d", rowdata[j]);
                draw_text(fe, x+TILE_SIZE/2, y+TILE_SIZE/2, FONT_VARIABLE,
                          TILE_SIZE/2, ALIGN_HCENTRE | ALIGN_VCENTRE,
                          COL_FULL, str);   /* FIXME: COL_TEXT */
            }
        }

        /*
         * Draw the grid outline.
         */
        draw_rect(fe, TOCOORD(ds->w, 0) - 1, TOCOORD(ds->h, 0) - 1,
                  ds->w * TILE_SIZE + 3, ds->h * TILE_SIZE + 3,
                  COL_GRID);

        ds->started = TRUE;

	draw_update(fe, 0, 0, SIZE(ds->w), SIZE(ds->h));
    }

    if (ui->dragging) {
        x1 = min(ui->drag_start_x, ui->drag_end_x);
        x2 = max(ui->drag_start_x, ui->drag_end_x);
        y1 = min(ui->drag_start_y, ui->drag_end_y);
        y2 = max(ui->drag_start_y, ui->drag_end_y);
    } else {
        x1 = x2 = y1 = y2 = -1;        /* placate gcc warnings */
    }

    /*
     * Now draw any grid squares which have changed since last
     * redraw.
     */
    for (i = 0; i < ds->h; i++) {
        for (j = 0; j < ds->w; j++) {
            int val;

            /*
             * Work out what state this square should be drawn in,
             * taking any current drag operation into account.
             */
            if (ui->dragging && x1 <= j && j <= x2 && y1 <= i && i <= y2)
                val = ui->state;
            else
                val = state->grid[i * state->w + j];

            /*
             * Briefly invert everything twice during a completion
             * flash.
             */
            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 || flashtime >= FLASH_TIME*2/3) &&
                val != GRID_UNKNOWN)
                val = (GRID_FULL ^ GRID_EMPTY) ^ val;

            if (ds->visible[i * ds->w + j] != val) {
                grid_square(fe, ds, i, j, val);
                ds->visible[i * ds->w + j] = val;
            }
        }
    }
}

static float game_anim_length(game_state *oldstate,
			      game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate,
			       game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame pattern
#endif

const struct game thegame = {
    "Pattern", "games.pattern",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    game_free_aux_info,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};

#ifdef STANDALONE_SOLVER

/*
 * gcc -DSTANDALONE_SOLVER -o patternsolver pattern.c malloc.c
 */

#include <stdarg.h>

void frontend_default_colour(frontend *fe, float *output) {}
void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text) {}
void draw_rect(frontend *fe, int x, int y, int w, int h, int colour) {}
void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour) {}
void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour) {}
void clip(frontend *fe, int x, int y, int w, int h) {}
void unclip(frontend *fe) {}
void start_draw(frontend *fe) {}
void draw_update(frontend *fe, int x, int y, int w, int h) {}
void end_draw(frontend *fe) {}
unsigned long random_upto(random_state *state, unsigned long limit)
{ assert(!"Shouldn't get randomness"); return 0; }

void fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    int recurse = TRUE;
    char *id = NULL, *desc, *err;
    int y, x;
    int grade = FALSE;

    while (--argc > 0) {
        char *p = *++argv;
	if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0]);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s <game_id>\n", argv[0]);
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

    {
	int w = p->w, h = p->h, i, j, done_any, max;
	unsigned char *matrix, *workspace;
	int *rowdata;

	matrix = snewn(w*h, unsigned char);
	max = max(w, h);
	workspace = snewn(max*3, unsigned char);
	rowdata = snewn(max+1, int);

        memset(matrix, 0, w*h);

        do {
            done_any = 0;
            for (i=0; i<h; i++) {
		memcpy(rowdata, s->rowdata + s->rowsize*(w+i),
		       max*sizeof(int));
		rowdata[s->rowlen[w+i]] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i*w, w, 1, rowdata);
            }
            for (i=0; i<w; i++) {
		memcpy(rowdata, s->rowdata + s->rowsize*i, max*sizeof(int));
		rowdata[s->rowlen[i]] = 0;
                done_any |= do_row(workspace, workspace+max, workspace+2*max,
                                   matrix+i, h, w, rowdata);
            }
        } while (done_any);

	for (i = 0; i < h; i++) {
	    for (j = 0; j < w; j++) {
		int c = (matrix[i*w+j] == UNKNOWN ? '?' :
			 matrix[i*w+j] == BLOCK ? '#' :
			 matrix[i*w+j] == DOT ? '.' :
			 '!');
		putchar(c);
	    }
	    printf("\n");
	}
    }

    return 0;
}

#endif
