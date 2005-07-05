/*
 * guess.c: Mastermind clone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define FLASH_FRAME 0.5F

enum {
    COL_BACKGROUND,
    COL_FRAME, COL_CURSOR, COL_FLASH, COL_HOLD,
    COL_EMPTY, /* must be COL_1 - 1 */
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8, COL_9, COL_10,
    COL_CORRECTPLACE, COL_CORRECTCOLOUR,
    NCOLOURS
};

struct game_params {
    int ncolours, npegs, nguesses;
    int allow_blank, allow_multiple;
};

#define FEEDBACK_CORRECTPLACE  1
#define FEEDBACK_CORRECTCOLOUR 2

typedef struct pegrow {
    int npegs;
    int *pegs;          /* 0 is 'empty' */
    int *feedback;      /* may well be unused */
} *pegrow;

struct game_state {
    game_params params;
    pegrow *guesses;  /* length params->nguesses */
    pegrow solution;
    int next_go; /* from 0 to nguesses-1;
                    if next_go == nguesses then they've lost. */
    int solved;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    /* AFAIK this is the canonical Mastermind ruleset. */
    ret->ncolours = 6;
    ret->npegs = 4;
    ret->nguesses = 10;

    ret->allow_blank = 0;
    ret->allow_multiple = 1;

    return ret;
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

static const struct {
    char *name;
    game_params params;
} guess_presets[] = {
    {"Standard", {6, 4, 10, FALSE, TRUE}},
    {"Super", {8, 5, 12, FALSE, TRUE}},
};


static int game_fetch_preset(int i, char **name, game_params **params)
{
    if (i < 0 || i >= lenof(guess_presets))
        return FALSE;

    *name = dupstr(guess_presets[i].name);
    /*
     * get round annoying const issues
     */
    {
        game_params tmp = guess_presets[i].params;
        *params = dup_params(&tmp);
    }

    return TRUE;
}

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;
    game_params *defs = default_params();

    *params = *defs; free_params(defs);

    while (*p) {
	switch (*p++) {
	case 'c':
	    params->ncolours = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	    break;

	case 'p':
	    params->npegs = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	    break;

	case 'g':
	    params->nguesses = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	    break;

        case 'b':
            params->allow_blank = 1;
            break;

        case 'B':
            params->allow_blank = 0;
            break;

        case 'm':
            params->allow_multiple = 1;
            break;

        case 'M':
            params->allow_multiple = 0;
            break;

	default:
            ;
	}
    }
}

static char *encode_params(game_params *params, int full)
{
    char data[256];

    sprintf(data, "c%dp%dg%d%s%s",
            params->ncolours, params->npegs, params->nguesses,
            params->allow_blank ? "b" : "B", params->allow_multiple ? "m" : "M");

    return dupstr(data);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Colours";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->ncolours);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Pegs per guess";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->npegs);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "Guesses";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->nguesses);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = "Allow blanks";
    ret[3].type = C_BOOLEAN;
    ret[3].sval = NULL;
    ret[3].ival = params->allow_blank;

    ret[4].name = "Allow duplicates";
    ret[4].type = C_BOOLEAN;
    ret[4].sval = NULL;
    ret[4].ival = params->allow_multiple;

    ret[5].name = NULL;
    ret[5].type = C_END;
    ret[5].sval = NULL;
    ret[5].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->ncolours = atoi(cfg[0].sval);
    ret->npegs = atoi(cfg[1].sval);
    ret->nguesses = atoi(cfg[2].sval);

    ret->allow_blank = cfg[3].ival;
    ret->allow_multiple = cfg[4].ival;

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->ncolours < 2 || params->npegs < 2)
	return "Trivial solutions are uninteresting";
    /* NB as well as the no. of colours we define, max(ncolours) must
     * also fit in an unsigned char; see new_game_desc. */
    if (params->ncolours > 10)
	return "Too many colours";
    if (params->nguesses < 1)
	return "Must have at least one guess";
    if (!params->allow_multiple && params->ncolours < params->npegs)
        return "Disallowing multiple colours requires at least as many colours as pegs";
    return NULL;
}

static pegrow new_pegrow(int npegs)
{
    pegrow pegs = snew(struct pegrow);

    pegs->npegs = npegs;
    pegs->pegs = snewn(pegs->npegs, int);
    memset(pegs->pegs, 0, pegs->npegs * sizeof(int));
    pegs->feedback = snewn(pegs->npegs, int);
    memset(pegs->feedback, 0, pegs->npegs * sizeof(int));

    return pegs;
}

static pegrow dup_pegrow(pegrow pegs)
{
    pegrow newpegs = new_pegrow(pegs->npegs);

    memcpy(newpegs->pegs, pegs->pegs, newpegs->npegs * sizeof(int));
    memcpy(newpegs->feedback, pegs->feedback, newpegs->npegs * sizeof(int));

    return newpegs;
}

static void invalidate_pegrow(pegrow pegs)
{
    memset(pegs->pegs, -1, pegs->npegs * sizeof(int));
    memset(pegs->feedback, -1, pegs->npegs * sizeof(int));
}

static void free_pegrow(pegrow pegs)
{
    sfree(pegs->pegs);
    sfree(pegs->feedback);
    sfree(pegs);
}

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    unsigned char *bmp = snewn(params->npegs, unsigned char);
    char *ret;
    int i, c;
    pegrow colcount = new_pegrow(params->ncolours);

    for (i = 0; i < params->npegs; i++) {
newcol:
        c = random_upto(rs, params->ncolours);
        if (!params->allow_multiple && colcount->pegs[c]) goto newcol;
        colcount->pegs[c]++;
        bmp[i] = (unsigned char)(c+1);
    }
    obfuscate_bitmap(bmp, params->npegs*8, FALSE);

    ret = bin2hex(bmp, params->npegs);
    sfree(bmp);
    free_pegrow(colcount);
    return ret;
}

static char *validate_desc(game_params *params, char *desc)
{
    unsigned char *bmp;
    int i;

    /* desc is just an (obfuscated) bitmap of the solution; check that
     * it's the correct length and (when unobfuscated) contains only
     * sensible colours. */
    if (strlen(desc) != params->npegs * 2)
        return "Game description is wrong length";
    bmp = hex2bin(desc, params->npegs);
    obfuscate_bitmap(bmp, params->npegs*8, TRUE);
    for (i = 0; i < params->npegs; i++) {
        if (bmp[i] < 1 || bmp[i] > params->ncolours) {
            sfree(bmp);
            return "Game description is corrupted";
        }
    }
    sfree(bmp);

    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    game_state *state = snew(game_state);
    unsigned char *bmp;
    int i;

    state->params = *params;
    state->guesses = snewn(params->nguesses, pegrow);
    for (i = 0; i < params->nguesses; i++)
	state->guesses[i] = new_pegrow(params->npegs);
    state->solution = new_pegrow(params->npegs);

    bmp = hex2bin(desc, params->npegs);
    obfuscate_bitmap(bmp, params->npegs*8, TRUE);
    for (i = 0; i < params->npegs; i++)
	state->solution->pegs[i] = (int)bmp[i];
    sfree(bmp);

    state->next_go = state->solved = 0;

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);
    int i;

    *ret = *state;

    ret->guesses = snewn(state->params.nguesses, pegrow);
    for (i = 0; i < state->params.nguesses; i++)
	ret->guesses[i] = dup_pegrow(state->guesses[i]);
    ret->solution = dup_pegrow(state->solution);

    return ret;
}

static void free_game(game_state *state)
{
    int i;

    free_pegrow(state->solution);
    for (i = 0; i < state->params.nguesses; i++)
	free_pegrow(state->guesses[i]);
    sfree(state->guesses);

    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    return dupstr("S");
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

static int is_markable(game_params *params, pegrow pegs)
{
    int i, nset = 0, nrequired, ret = 0;
    pegrow colcount = new_pegrow(params->ncolours);

    nrequired = params->allow_blank ? 1 : params->npegs;

    for (i = 0; i < params->npegs; i++) {
        int c = pegs->pegs[i];
        if (c > 0) {
            colcount->pegs[c-1]++;
            nset++;
        }
    }
    if (nset < nrequired) goto done;

    if (!params->allow_multiple) {
        for (i = 0; i < params->ncolours; i++) {
            if (colcount->pegs[i] > 1) goto done;
        }
    }
    ret = 1;
done:
    free_pegrow(colcount);
    return ret;
}

struct game_ui {
    game_params params;
    pegrow curr_pegs; /* half-finished current move */
    int *holds;
    int colour_cur;   /* position of up-down colour picker cursor */
    int peg_cur;      /* position of left-right peg picker cursor */
    int display_cur, markable;

    int drag_col, drag_x, drag_y; /* x and y are *center* of peg! */
    int drag_opeg; /* peg index, if dragged from a peg (from current guess), otherwise -1 */
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(struct game_ui);
    memset(ui, 0, sizeof(struct game_ui));
    ui->params = state->params;        /* structure copy */
    ui->curr_pegs = new_pegrow(state->params.npegs);
    ui->holds = snewn(state->params.npegs, int);
    memset(ui->holds, 0, sizeof(int)*state->params.npegs);
    ui->drag_opeg = -1;
    return ui;
}

static void free_ui(game_ui *ui)
{
    free_pegrow(ui->curr_pegs);
    sfree(ui->holds);
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    char *ret, *p, *sep;
    int i;

    /*
     * For this game it's worth storing the contents of the current
     * guess.
     */
    ret = snewn(40 * ui->curr_pegs->npegs, char);
    p = ret;
    sep = "";
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
        p += sprintf(p, "%s%d", sep, ui->curr_pegs->pegs[i]);
        sep = ",";
    }
    assert(p - ret < 40 * ui->curr_pegs->npegs);
    return sresize(ret, p - ret + 1, char);
}

static void decode_ui(game_ui *ui, char *encoding)
{
    int i;
    char *p = encoding;
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
        ui->curr_pegs->pegs[i] = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
        if (*p == ',') p++;
    }
    ui->markable = is_markable(&ui->params, ui->curr_pegs);
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    int i;

    /* just clear the row-in-progress when we have an undo/redo. */
    for (i = 0; i < ui->curr_pegs->npegs; i++)
	ui->curr_pegs->pegs[i] = 0;
    ui->markable = FALSE;
}

#define PEGSZ   (ds->pegsz)
#define PEGOFF  (ds->pegsz + ds->gapsz)
#define HINTSZ  (ds->hintsz)
#define HINTOFF (ds->hintsz + ds->gapsz)

#define GAP     (ds->gapsz)
#define CGAP    (ds->gapsz / 2)

#define PEGRAD  (ds->pegrad)
#define HINTRAD (ds->hintrad)

#define COL_OX          (ds->colx)
#define COL_OY          (ds->coly)
#define COL_X(c)        (COL_OX)
#define COL_Y(c)        (COL_OY + (c)*PEGOFF)
#define COL_W           PEGOFF
#define COL_H           (ds->colours->npegs*PEGOFF)

#define GUESS_OX        (ds->guessx)
#define GUESS_OY        (ds->guessy)
#define GUESS_X(g,p)    (GUESS_OX + (p)*PEGOFF)
#define GUESS_Y(g,p)    (GUESS_OY + (g)*PEGOFF)
#define GUESS_W         (ds->solution->npegs*PEGOFF)
#define GUESS_H         (ds->nguesses*PEGOFF)

#define HINT_OX         (GUESS_OX + GUESS_W + ds->gapsz)
#define HINT_OY         (GUESS_OY + (PEGSZ - HINTOFF - HINTSZ) / 2)
#define HINT_X(g)       HINT_OX
#define HINT_Y(g)       (HINT_OY + (g)*PEGOFF)
#define HINT_W          ((ds->hintw*HINTOFF) - GAP)
#define HINT_H          GUESS_H

#define SOLN_OX         GUESS_OX
#define SOLN_OY         (GUESS_OY + GUESS_H + ds->gapsz + 2)
#define SOLN_W          GUESS_W
#define SOLN_H          PEGOFF

struct game_drawstate {
    int nguesses;
    pegrow *guesses;    /* same size as state->guesses */
    pegrow solution;    /* only displayed if state->solved */
    pegrow colours;     /* length ncolours, not npegs */

    int pegsz, hintsz, gapsz; /* peg size (diameter), etc. */
    int pegrad, hintrad;      /* radius of peg, hint */
    int border;
    int colx, coly;     /* origin of colours vertical bar */
    int guessx, guessy; /* origin of guesses */
    int solnx, solny;   /* origin of solution */
    int hintw;          /* no. of hint tiles we're wide per row */
    int w, h, started, solved;

    int next_go;

    blitter *blit_peg;
    int drag_col, blit_ox, blit_oy;
};

static void set_peg(game_params *params, game_ui *ui, int peg, int col)
{
    ui->curr_pegs->pegs[peg] = col;
    ui->markable = is_markable(params, ui->curr_pegs);
}

static int mark_pegs(pegrow guess, pegrow solution, int ncols)
{
    int nc_place = 0, nc_colour = 0, i, j;

    assert(guess && solution && (guess->npegs == solution->npegs));

    for (i = 0; i < guess->npegs; i++) {
        if (guess->pegs[i] == solution->pegs[i]) nc_place++;
    }

    /* slight bit of cleverness: we have the following formula, from
     * http://mathworld.wolfram.com/Mastermind.html that gives:
     *
     * nc_colour = sum(colours, min(#solution, #guess)) - nc_place
     *
     * I think this is due to Knuth.
     */
    for (i = 1; i <= ncols; i++) {
        int n_guess = 0, n_solution = 0;
        for (j = 0; j < guess->npegs; j++) {
            if (guess->pegs[j] == i) n_guess++;
            if (solution->pegs[j] == i) n_solution++;
        }
        nc_colour += min(n_guess, n_solution);
    }
    nc_colour -= nc_place;

    debug(("mark_pegs, %d pegs, %d right place, %d right colour",
           guess->npegs, nc_place, nc_colour));
    assert((nc_colour + nc_place) <= guess->npegs);

    memset(guess->feedback, 0, guess->npegs*sizeof(int));
    for (i = 0, j = 0; i < nc_place; i++)
        guess->feedback[j++] = FEEDBACK_CORRECTPLACE;
    for (i = 0; i < nc_colour; i++)
        guess->feedback[j++] = FEEDBACK_CORRECTCOLOUR;

    return nc_place;
}

static char *encode_move(game_state *from, game_ui *ui)
{
    char *buf, *p, *sep;
    int len, i, solved;
    pegrow tmppegs;

    len = ui->curr_pegs->npegs * 20 + 2;
    buf = snewn(len, char);
    p = buf;
    *p++ = 'G';
    sep = "";
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
	p += sprintf(p, "%s%d", sep, ui->curr_pegs->pegs[i]);
	sep = ",";
    }
    *p++ = '\0';
    assert(p - buf <= len);
    buf = sresize(buf, len, char);

    tmppegs = dup_pegrow(ui->curr_pegs);
    solved = mark_pegs(tmppegs, from->solution, from->params.ncolours);
    solved = (solved == from->params.ncolours);
    free_pegrow(tmppegs);

    for (i = 0; i < from->solution->npegs; i++) {
	if (!ui->holds[i] || solved) {
	    ui->curr_pegs->pegs[i] = 0;
	}
	if (solved) ui->holds[i] = 0;
    }
    ui->markable = is_markable(&from->params, ui->curr_pegs);
    if (!ui->markable && ui->peg_cur == from->solution->npegs)
	ui->peg_cur--;

    return buf;
}

static char *interpret_move(game_state *from, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int over_col = 0;           /* one-indexed */
    int over_guess = -1;        /* zero-indexed */
    int over_past_guess_y = -1; /* zero-indexed */
    int over_past_guess_x = -1; /* zero-indexed */
    int over_hint = 0;          /* zero or one */
    char *ret = NULL;

    int guess_ox = GUESS_X(from->next_go, 0);
    int guess_oy = GUESS_Y(from->next_go, 0);

    if (from->solved) return NULL;

    if (x >= COL_OX && x <= (COL_OX + COL_W) &&
        y >= COL_OY && y <= (COL_OY + COL_H)) {
        over_col = ((y - COL_OY) / PEGOFF) + 1;
    } else if (x >= guess_ox &&
               y >= guess_oy && y <= (guess_oy + GUESS_H)) {
        if (x <= (guess_ox + GUESS_W)) {
            over_guess = (x - guess_ox) / PEGOFF;
        } else {
            over_hint = 1;
        }
    } else if (x >= guess_ox && x <= (guess_ox + GUESS_W) &&
               y >= GUESS_OY && y < guess_oy) {
        over_past_guess_y = (y - GUESS_OY) / PEGOFF;
        over_past_guess_x = (x - guess_ox) / PEGOFF;
    }
    debug(("make_move: over_col %d, over_guess %d, over_hint %d,"
           " over_past_guess (%d,%d)", over_col, over_guess, over_hint,
           over_past_guess_x, over_past_guess_y));

    assert(ds->blit_peg);

    /* mouse input */
    if (button == LEFT_BUTTON) {
        if (over_col > 0) {
            ui->drag_col = over_col;
            ui->drag_opeg = -1;
            debug(("Start dragging from colours"));
        } else if (over_guess > -1) {
            int col = ui->curr_pegs->pegs[over_guess];
            if (col) {
                ui->drag_col = col;
                ui->drag_opeg = over_guess;
                debug(("Start dragging from a guess"));
            }
        } else if (over_past_guess_y > -1) {
            int col =
                from->guesses[over_past_guess_y]->pegs[over_past_guess_x];
            if (col) {
                ui->drag_col = col;
                ui->drag_opeg = -1;
                debug(("Start dragging from a past guess"));
            }
        }
        if (ui->drag_col) {
            ui->drag_x = x;
            ui->drag_y = y;
            debug(("Start dragging, col = %d, (%d,%d)",
                   ui->drag_col, ui->drag_x, ui->drag_y));
            ret = "";
        }
    } else if (button == LEFT_DRAG && ui->drag_col) {
        ui->drag_x = x;
        ui->drag_y = y;
        debug(("Keep dragging, (%d,%d)", ui->drag_x, ui->drag_y));
        ret = "";
    } else if (button == LEFT_RELEASE && ui->drag_col) {
        if (over_guess > -1) {
            debug(("Dropping colour %d onto guess peg %d",
                   ui->drag_col, over_guess));
            set_peg(&from->params, ui, over_guess, ui->drag_col);
        } else {
            if (ui->drag_opeg > -1) {
                debug(("Removing colour %d from peg %d",
                       ui->drag_col, ui->drag_opeg));
                set_peg(&from->params, ui, ui->drag_opeg, 0);
            }
        }
        ui->drag_col = 0;
        ui->drag_opeg = -1;
        ui->display_cur = 0;
        debug(("Stop dragging."));
        ret = "";
    } else if (button == RIGHT_BUTTON) {
        if (over_guess > -1) {
            /* we use ths feedback in the game_ui to signify
             * 'carry this peg to the next guess as well'. */
            ui->holds[over_guess] = 1 - ui->holds[over_guess];
            ret = "";
        }
    } else if (button == LEFT_RELEASE && over_hint && ui->markable) {
        /* NB this won't trigger if on the end of a drag; that's on
         * purpose, in case you drop by mistake... */
        ret = encode_move(from, ui);
    }

    /* keyboard input */
    if (button == CURSOR_UP || button == CURSOR_DOWN) {
        ui->display_cur = 1;
        if (button == CURSOR_DOWN && (ui->colour_cur+1) < from->params.ncolours)
            ui->colour_cur++;
        if (button == CURSOR_UP && ui->colour_cur > 0)
            ui->colour_cur--;
        ret = "";
    } else if (button == CURSOR_LEFT || button == CURSOR_RIGHT) {
        int maxcur = from->params.npegs;
        if (ui->markable) maxcur++;

        ui->display_cur = 1;
        if (button == CURSOR_RIGHT && (ui->peg_cur+1) < maxcur)
            ui->peg_cur++;
        if (button == CURSOR_LEFT && ui->peg_cur > 0)
            ui->peg_cur--;
        ret = "";
    } else if (button == CURSOR_SELECT || button == ' ' || button == '\r' ||
               button == '\n') {
        ui->display_cur = 1;
        if (ui->peg_cur == from->params.npegs) {
            ret = encode_move(from, ui);
        } else {
            set_peg(&from->params, ui, ui->peg_cur, ui->colour_cur+1);
            ret = "";
        }
    } else if (button == 'D' || button == 'd' || button == '\b') {
        ui->display_cur = 1;
        set_peg(&from->params, ui, ui->peg_cur, 0);
        ret = "";
    } else if (button == 'H' || button == 'h') {
        ui->display_cur = 1;
        ui->holds[ui->peg_cur] = 1 - ui->holds[ui->peg_cur];
        ret = "";
    }
    return ret;
}

static game_state *execute_move(game_state *from, char *move)
{
    int i, nc_place;
    game_state *ret;
    char *p;

    if (!strcmp(move, "S")) {
	ret = dup_game(from);
	ret->solved = 1;
	return ret;
    } else if (move[0] == 'G') {
	p = move+1;

	ret = dup_game(from);

	for (i = 0; i < from->solution->npegs; i++) {
	    int val = atoi(p);
	    if (val <= 0 || val > from->params.ncolours) {
		free_game(ret);
		return NULL;
	    }
	    ret->guesses[from->next_go]->pegs[i] = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
	    if (*p == ',') p++;
	}

	nc_place = mark_pegs(ret->guesses[from->next_go], ret->solution, ret->params.ncolours);

	if (nc_place == ret->solution->npegs) {
	    ret->solved = 1; /* win! */
	} else {
	    ret->next_go = from->next_go + 1;
	    if (ret->next_go >= ret->params.nguesses)
		ret->solved = 1; /* 'lose' so we show the pegs. */
	}

	return ret;
    } else
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define PEG_PREFER_SZ 32

/* next three are multipliers for pegsz. It will look much nicer if
 * (2*PEG_HINT) + PEG_GAP = 1.0 as the hints are formatted like that. */
#define PEG_GAP   0.10
#define PEG_HINT  0.35

#define BORDER    0.5

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    double hmul, vmul_c, vmul_g, vmul;
    int hintw = (params->npegs+1)/2;

    hmul = BORDER   * 2.0 +               /* border */
           1.0      * 2.0 +               /* vertical colour bar */
           1.0      * params->npegs +     /* guess pegs */
           PEG_GAP  * params->npegs +     /* guess gaps */
           PEG_HINT * hintw +             /* hint pegs */
           PEG_GAP  * (hintw - 1);        /* hint gaps */

    vmul_c = BORDER  * 2.0 +                    /* border */
             1.0     * params->ncolours +       /* colour pegs */
             PEG_GAP * (params->ncolours - 1);  /* colour gaps */

    vmul_g = BORDER  * 2.0 +                    /* border */
             1.0     * (params->nguesses + 1) + /* guesses plus solution */
             PEG_GAP * (params->nguesses + 1);  /* gaps plus gap above soln */

    vmul = max(vmul_c, vmul_g);

    *x = (int)ceil((double)tilesize * hmul);
    *y = (int)ceil((double)tilesize * vmul);
}

static void game_set_size(game_drawstate *ds, game_params *params,
			  int tilesize)
{
    int colh, guessh, x, y;

    ds->pegsz = tilesize;

    ds->hintsz = (int)((double)ds->pegsz * PEG_HINT);
    ds->gapsz  = (int)((double)ds->pegsz * PEG_GAP);
    ds->border = (int)((double)ds->pegsz * BORDER);

    ds->pegrad  = (ds->pegsz -1)/2; /* radius of peg to fit in pegsz (which is 2r+1) */
    ds->hintrad = (ds->hintsz-1)/2;

    colh = ((ds->pegsz + ds->gapsz) * params->ncolours) - ds->gapsz;
    guessh = ((ds->pegsz + ds->gapsz) * params->nguesses);      /* guesses */
    guessh += ds->gapsz + ds->pegsz;                            /* solution */

    game_compute_size(params, tilesize, &x, &y);
    ds->colx = ds->border;
    ds->coly = (y - colh) / 2;

    ds->guessx = ds->solnx = ds->border + ds->pegsz * 2;     /* border + colours */
    ds->guessy = (y - guessh) / 2;
    ds->solny = ds->guessy + ((ds->pegsz + ds->gapsz) * params->nguesses) + ds->gapsz;

    assert(ds->pegsz > 0);
    if (ds->blit_peg) blitter_free(ds->blit_peg);
    ds->blit_peg = blitter_new(ds->pegsz, ds->pegsz);
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float), max;
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /* red */
    ret[COL_1 * 3 + 0] = 1.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 0.0F;

    /* yellow (toned down a bit due to pale grey background) */
    ret[COL_2 * 3 + 0] = 0.7F;
    ret[COL_2 * 3 + 1] = 0.7F;
    ret[COL_2 * 3 + 2] = 0.0F;

    /* green (also toned down) */
    ret[COL_3 * 3 + 0] = 0.0F;
    ret[COL_3 * 3 + 1] = 0.5F;
    ret[COL_3 * 3 + 2] = 0.0F;

    /* blue */
    ret[COL_4 * 3 + 0] = 0.0F;
    ret[COL_4 * 3 + 1] = 0.0F;
    ret[COL_4 * 3 + 2] = 1.0F;

    /* orange */
    ret[COL_5 * 3 + 0] = 1.0F;
    ret[COL_5 * 3 + 1] = 0.5F;
    ret[COL_5 * 3 + 2] = 0.0F;

    /* purple */
    ret[COL_6 * 3 + 0] = 0.5F;
    ret[COL_6 * 3 + 1] = 0.0F;
    ret[COL_6 * 3 + 2] = 0.7F;

    /* brown */
    ret[COL_7 * 3 + 0] = 0.4F;
    ret[COL_7 * 3 + 1] = 0.2F;
    ret[COL_7 * 3 + 2] = 0.2F;

    /* light blue */
    ret[COL_8 * 3 + 0] = 0.4F;
    ret[COL_8 * 3 + 1] = 0.7F;
    ret[COL_8 * 3 + 2] = 1.0F;

    /* light green */
    ret[COL_9 * 3 + 0] = 0.5F;
    ret[COL_9 * 3 + 1] = 0.8F;
    ret[COL_9 * 3 + 2] = 0.5F;

    /* pink */
    ret[COL_10 * 3 + 0] = 1.0F;
    ret[COL_10 * 3 + 1] = 0.6F;
    ret[COL_10 * 3 + 2] = 1.0F;

    ret[COL_FRAME * 3 + 0] = 0.0F;
    ret[COL_FRAME * 3 + 1] = 0.0F;
    ret[COL_FRAME * 3 + 2] = 0.0F;

    ret[COL_CURSOR * 3 + 0] = 0.0F;
    ret[COL_CURSOR * 3 + 1] = 0.0F;
    ret[COL_CURSOR * 3 + 2] = 0.0F;

    ret[COL_FLASH * 3 + 0] = 0.5F;
    ret[COL_FLASH * 3 + 1] = 1.0F;
    ret[COL_FLASH * 3 + 2] = 1.0F;

    ret[COL_HOLD * 3 + 0] = 1.0F;
    ret[COL_HOLD * 3 + 1] = 0.5F;
    ret[COL_HOLD * 3 + 2] = 0.5F;

    ret[COL_CORRECTPLACE*3 + 0] = 0.0F;
    ret[COL_CORRECTPLACE*3 + 1] = 0.0F;
    ret[COL_CORRECTPLACE*3 + 2] = 0.0F;

    ret[COL_CORRECTCOLOUR*3 + 0] = 1.0F;
    ret[COL_CORRECTCOLOUR*3 + 1] = 1.0F;
    ret[COL_CORRECTCOLOUR*3 + 2] = 1.0F;

    /* We want to make sure we can distinguish COL_CORRECTCOLOUR
     * (which we hard-code as white) from COL_BACKGROUND (which
     * could default to white on some platforms).
     * Code borrowed from fifteen.c. */
    max = ret[COL_BACKGROUND*3];
    for (i = 1; i < 3; i++)
        if (ret[COL_BACKGROUND*3+i] > max)
            max = ret[COL_BACKGROUND*3+i];
    if (max * 1.2F > 1.0F) {
        for (i = 0; i < 3; i++)
            ret[COL_BACKGROUND*3+i] /= (max * 1.2F);
    }

    /* We also want to be able to tell the difference between BACKGROUND
     * and EMPTY, for similar distinguishing-hint reasons. */
    ret[COL_EMPTY * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0 / 3.0;
    ret[COL_EMPTY * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0 / 3.0;
    ret[COL_EMPTY * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0 / 3.0;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    memset(ds, 0, sizeof(struct game_drawstate));

    ds->guesses = snewn(state->params.nguesses, pegrow);
    ds->nguesses = state->params.nguesses;
    for (i = 0; i < state->params.nguesses; i++) {
        ds->guesses[i] = new_pegrow(state->params.npegs);
        invalidate_pegrow(ds->guesses[i]);
    }
    ds->solution = new_pegrow(state->params.npegs);
    invalidate_pegrow(ds->solution);
    ds->colours = new_pegrow(state->params.ncolours);
    invalidate_pegrow(ds->colours);

    ds->hintw = (state->params.npegs+1)/2; /* must round up */

    ds->blit_peg = NULL;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    int i;

    if (ds->blit_peg) blitter_free(ds->blit_peg);
    free_pegrow(ds->colours);
    free_pegrow(ds->solution);
    for (i = 0; i < ds->nguesses; i++)
        free_pegrow(ds->guesses[i]);
    sfree(ds->guesses);
    sfree(ds);
}

static void draw_peg(frontend *fe, game_drawstate *ds, int cx, int cy,
		     int moving, int col)
{
    /*
     * Some platforms antialias circles, which means we shouldn't
     * overwrite a circle of one colour with a circle of another
     * colour without erasing the background first. However, if the
     * peg is the one being dragged, we don't erase the background
     * because we _want_ it to alpha-blend nicely into whatever's
     * behind it.
     */
    if (!moving)
        draw_rect(fe, cx-CGAP, cy-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2,
                  COL_BACKGROUND);
    if (PEGRAD > 0) {
        draw_circle(fe, cx+PEGRAD, cy+PEGRAD, PEGRAD,
		    COL_EMPTY + col, COL_EMPTY + col);
    } else
        draw_rect(fe, cx, cy, PEGSZ, PEGSZ, COL_EMPTY + col);
    draw_update(fe, cx-CGAP, cy-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2);
}

static void draw_cursor(frontend *fe, game_drawstate *ds, int x, int y)
{
    draw_circle(fe, x+PEGRAD, y+PEGRAD, PEGRAD+CGAP, -1, COL_CURSOR);

    draw_update(fe, x-CGAP, y-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2);
}

static void guess_redraw(frontend *fe, game_drawstate *ds, int guess,
                         pegrow src, int *holds, int cur_col, int force)
{
    pegrow dest;
    int rowx, rowy, i, scol;

    if (guess == -1) {
        dest = ds->solution;
        rowx = SOLN_OX;
        rowy = SOLN_OY;
    } else {
        dest = ds->guesses[guess];
        rowx = GUESS_X(guess,0);
        rowy = GUESS_Y(guess,0);
    }
    if (src) assert(src->npegs == dest->npegs);

    for (i = 0; i < dest->npegs; i++) {
        scol = src ? src->pegs[i] : 0;
        if (i == cur_col)
            scol |= 0x1000;
        if (holds && holds[i])
            scol |= 0x2000;
        if ((dest->pegs[i] != scol) || force) {
	    draw_peg(fe, ds, rowx + PEGOFF * i, rowy, FALSE, scol &~ 0x3000);
            /*
             * Hold marker.
             */
            draw_rect(fe, rowx + PEGOFF * i, rowy + PEGSZ + ds->gapsz/2,
                      PEGSZ, 2, (scol & 0x2000 ? COL_HOLD : COL_BACKGROUND));
            draw_update(fe, rowx + PEGOFF * i, rowy + PEGSZ + ds->gapsz/2,
                        PEGSZ, 2);
            if (scol & 0x1000)
                draw_cursor(fe, ds, rowx + PEGOFF * i, rowy);
        }
        dest->pegs[i] = scol;
    }
}

static void hint_redraw(frontend *fe, game_drawstate *ds, int guess,
                        pegrow src, int force, int cursor, int markable)
{
    pegrow dest = ds->guesses[guess];
    int rowx, rowy, i, scol, col, hintlen;
    int need_redraw;
    int emptycol = (markable ? COL_FLASH : COL_EMPTY);

    if (src) assert(src->npegs == dest->npegs);

    hintlen = (dest->npegs + 1)/2;

    /*
     * Because of the possible presence of the cursor around this
     * entire section, we redraw all or none of it but never part.
     */
    need_redraw = FALSE;

    for (i = 0; i < dest->npegs; i++) {
        scol = src ? src->feedback[i] : 0;
        if (i == 0 && cursor)
            scol |= 0x1000;
        if (i == 0 && markable)
            scol |= 0x2000;
        if ((scol != dest->feedback[i]) || force) {
            need_redraw = TRUE;
        }
        dest->feedback[i] = scol;
    }

    if (need_redraw) {
        int hinth = HINTSZ + GAP + HINTSZ;
        int hx,hy,hw,hh;

        hx = HINT_X(guess)-GAP; hy = HINT_Y(guess)-GAP;
        hw = HINT_W+GAP*2; hh = hinth+GAP*2;

        /* erase a large background rectangle */
        draw_rect(fe, hx, hy, hw, hh, COL_BACKGROUND);

        for (i = 0; i < dest->npegs; i++) {
            scol = src ? src->feedback[i] : 0;
            col = ((scol == FEEDBACK_CORRECTPLACE) ? COL_CORRECTPLACE :
                   (scol == FEEDBACK_CORRECTCOLOUR) ? COL_CORRECTCOLOUR :
                   emptycol);

            rowx = HINT_X(guess);
            rowy = HINT_Y(guess);
            if (i < hintlen) {
                rowx += HINTOFF * i;
            } else {
                rowx += HINTOFF * (i - hintlen);
                rowy += HINTOFF;
            }
            if (HINTRAD > 0) {
                draw_circle(fe, rowx+HINTRAD, rowy+HINTRAD, HINTRAD, col, col);
            } else {
                draw_rect(fe, rowx, rowy, HINTSZ, HINTSZ, col);
            }
        }
        if (cursor) {
            int x1,y1,x2,y2;
            x1 = hx + CGAP; y1 = hy + CGAP;
            x2 = hx + hw - CGAP; y2 = hy + hh - CGAP;
            draw_line(fe, x1, y1, x2, y1, COL_CURSOR);
            draw_line(fe, x2, y1, x2, y2, COL_CURSOR);
            draw_line(fe, x2, y2, x1, y2, COL_CURSOR);
            draw_line(fe, x1, y2, x1, y1, COL_CURSOR);
        }

        draw_update(fe, hx, hy, hw, hh);
    }
}

static void currmove_redraw(frontend *fe, game_drawstate *ds, int guess, int col)
{
    int ox = GUESS_X(guess, 0), oy = GUESS_Y(guess, 0), off = PEGSZ/4;

    draw_rect(fe, ox-off-1, oy, 2, PEGSZ, col);
    draw_update(fe, ox-off-1, oy, 2, PEGSZ);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int i, new_move, last_go;

    new_move = (state->next_go != ds->next_go) || !ds->started;
    last_go = (state->next_go == state->params.nguesses-1);

    if (!ds->started) {
      draw_rect(fe, 0, 0, ds->w, ds->h, COL_BACKGROUND);
      draw_rect(fe, SOLN_OX, SOLN_OY - ds->gapsz - 1, SOLN_W, 2, COL_FRAME);
      draw_update(fe, 0, 0, ds->w, ds->h);
    }

    if (ds->drag_col != 0) {
        debug(("Loading from blitter."));
        blitter_load(fe, ds->blit_peg, ds->blit_ox, ds->blit_oy);
        draw_update(fe, ds->blit_ox, ds->blit_oy, PEGSZ, PEGSZ);
    }

    /* draw the colours */
    for (i = 0; i < state->params.ncolours; i++) {
        int val = i+1;
        if (ui->display_cur && ui->colour_cur == i)
            val |= 0x1000;
        if (ds->colours->pegs[i] != val) {
	    draw_peg(fe, ds, COL_X(i), COL_Y(i), FALSE, i+1);
            if (val & 0x1000)
                draw_cursor(fe, ds, COL_X(i), COL_Y(i));
            ds->colours->pegs[i] = val;
        }
    }

    /* draw the guesses (so far) and the hints */
    for (i = 0; i < state->params.nguesses; i++) {
        if (state->next_go > i || state->solved) {
            /* this info is stored in the game_state already */
            guess_redraw(fe, ds, i, state->guesses[i], NULL, -1, 0);
            hint_redraw(fe, ds, i, state->guesses[i],
                        i == (state->next_go-1) ? 1 : 0, FALSE, FALSE);
        } else if (state->next_go == i) {
            /* this is the one we're on; the (incomplete) guess is
             * stored in the game_ui. */
            guess_redraw(fe, ds, i, ui->curr_pegs,
                         ui->holds, ui->display_cur ? ui->peg_cur : -1, 0);
            hint_redraw(fe, ds, i, NULL, 1,
                        ui->display_cur && ui->peg_cur == state->params.npegs,
                        ui->markable);
        } else {
            /* we've not got here yet; it's blank. */
            guess_redraw(fe, ds, i, NULL, NULL, -1, 0);
            hint_redraw(fe, ds, i, NULL, 0, FALSE, FALSE);
        }
    }

    /* draw the 'current move' and 'able to mark' sign. */
    if (new_move)
        currmove_redraw(fe, ds, ds->next_go, COL_BACKGROUND);
    if (!state->solved)
        currmove_redraw(fe, ds, state->next_go, COL_HOLD);

    /* draw the solution (or the big rectangle) */
    if ((state->solved != ds->solved) || !ds->started) {
        draw_rect(fe, SOLN_OX, SOLN_OY, SOLN_W, SOLN_H,
                  state->solved ? COL_BACKGROUND : COL_EMPTY);
        draw_update(fe, SOLN_OX, SOLN_OY, SOLN_W, SOLN_H);
    }
    if (state->solved)
        guess_redraw(fe, ds, -1, state->solution, NULL, -1, !ds->solved);
    ds->solved = state->solved;

    ds->next_go = state->next_go;

    /* if ui->drag_col != 0, save the screen to the blitter,
     * draw the peg where we saved, and set ds->drag_* == ui->drag_*. */
    if (ui->drag_col != 0) {
        int ox = ui->drag_x - (PEGSZ/2);
        int oy = ui->drag_y - (PEGSZ/2);
        debug(("Saving to blitter at (%d,%d)", ox, oy));
        blitter_save(fe, ds->blit_peg, ox, oy);
        draw_peg(fe, ds, ox, oy, TRUE, ui->drag_col);

        ds->blit_ox = ox; ds->blit_oy = oy;
    }
    ds->drag_col = ui->drag_col;

    ds->started = 1;
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
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
#define thegame guess
#endif

const struct game thegame = {
    "Guess", "games.guess",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PEG_PREFER_SZ, game_compute_size, game_set_size,
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

/* vim: set shiftwidth=4 tabstop=8: */
