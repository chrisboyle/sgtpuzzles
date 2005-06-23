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
    COL_HIGHLIGHT, COL_LOWLIGHT, COL_FRAME, COL_FLASH, COL_HOLD,
    COL_EMPTY, /* must be COL_1 - 1 */
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8, COL_9, COL_10,
    COL_CORRECTPLACE, COL_CORRECTCOLOUR,
    NCOLOURS
};

struct game_params {
    int ncolours, npegs, nguesses;
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

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
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

	default:
            ;
	}
    }
}

static char *encode_params(game_params *params, int full)
{
    char data[256];

    sprintf(data, "c%dp%dg%d", params->ncolours, params->npegs, params->nguesses);

    return dupstr(data);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "No. of colours";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->ncolours);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "No. of pegs per row";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->npegs);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "No. of guesses";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->nguesses);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->ncolours = atoi(cfg[0].sval);
    ret->npegs = atoi(cfg[1].sval);
    ret->nguesses = atoi(cfg[2].sval);

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
    pegrow newpegs = snew(struct pegrow);

    newpegs->npegs = pegs->npegs;
    newpegs->pegs = snewn(newpegs->npegs, int);
    memcpy(newpegs->pegs, pegs->pegs, newpegs->npegs * sizeof(int));
    newpegs->feedback = snewn(newpegs->npegs, int);
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
			   game_aux_info **aux, int interactive)
{
    unsigned char *bmp = snewn(params->npegs, unsigned char);
    char *ret;
    int i;

    for (i = 0; i < params->npegs; i++)
        bmp[i] = (unsigned char)(random_upto(rs, params->ncolours)+1);
    obfuscate_bitmap(bmp, params->npegs*8, FALSE);

    ret = bin2hex(bmp, params->npegs);
    sfree(bmp);
    return ret;
}

static void game_free_aux_info(game_aux_info *aux)
{
    assert(!"Shouldn't happen");
}

static char *validate_desc(game_params *params, char *desc)
{
    /* desc is just an (obfuscated) bitmap of the solution; all we
     * care is that it's the correct length. */
    if (strlen(desc) != params->npegs * 2)
        return "Game description is wrong length";
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

static game_state *solve_game(game_state *state, game_state *currstate,
                              game_aux_info *aux, char **error)
{
    game_state *ret = dup_game(currstate);
    ret->solved = 1;
    return ret;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

struct game_ui {
    pegrow curr_pegs; /* half-finished current move */
    int *holds;
    int colour_cur;   /* position of up-down colour picker cursor */
    int peg_cur;      /* position of left-right peg picker cursor */
    int display_cur, markable;

    int drag_col, drag_x, drag_y; /* x and y are *center* of peg! */
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(struct game_ui);
    memset(ui, 0, sizeof(struct game_ui));
    ui->curr_pegs = new_pegrow(state->params.npegs);
    ui->holds = snewn(state->params.npegs, int);
    memset(ui->holds, 0, sizeof(int)*state->params.npegs);
    return ui;
}

static void free_ui(game_ui *ui)
{
    free_pegrow(ui->curr_pegs);
    sfree(ui->holds);
    sfree(ui);
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    int i;

    /* just clear the row-in-progress when we have an undo/redo. */
    for (i = 0; i < ui->curr_pegs->npegs; i++)
	ui->curr_pegs->pegs[i] = 0;
}

#define PEGSZ   (ds->pegsz)
#define PEGOFF  (ds->pegsz + ds->gapsz)
#define HINTSZ  (ds->hintsz)
#define HINTOFF (ds->hintsz + ds->gapsz)

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
#define HINT_W          (ds->hintw*HINTOFF)
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

    int *holds;

    int pegsz, hintsz, gapsz; /* peg size (diameter), etc. */
    int pegrad, hintrad;      /* radius of peg, hint */
    int border;
    int colx, coly;     /* origin of colours vertical bar */
    int guessx, guessy; /* origin of guesses */
    int solnx, solny;   /* origin of solution */
    int hintw;          /* no. of hint tiles we're wide per row */
    int w, h, started, solved;

    int colour_cur, peg_cur, display_cur; /* as in game_ui. */
    int next_go;

    blitter *blit_peg;
    int drag_col, blit_ox, blit_oy;
};

static void set_peg(game_ui *ui, int peg, int col)
{
    int i;

    ui->curr_pegs->pegs[peg] = col;

    /* set to 'markable' if all of our pegs are filled. */
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
        if (ui->curr_pegs->pegs[i] == 0) return;
    }
    debug(("UI is markable."));
    ui->markable = 1;
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

static game_state *mark_move(game_state *from, game_ui *ui)
{
    int i, ncleared = 0, nc_place;
    game_state *to = dup_game(from);

    for (i = 0; i < to->solution->npegs; i++) {
        to->guesses[from->next_go]->pegs[i] = ui->curr_pegs->pegs[i];
    }
    nc_place = mark_pegs(to->guesses[from->next_go], to->solution, to->params.ncolours);

    if (nc_place == to->solution->npegs) {
        to->solved = 1; /* win! */
    } else {
        to->next_go = from->next_go + 1;
        if (to->next_go >= to->params.nguesses)
            to->solved = 1; /* 'lose' so we show the pegs. */
    }

    for (i = 0; i < to->solution->npegs; i++) {
        if (!ui->holds[i] || to->solved) {
            ui->curr_pegs->pegs[i] = 0;
            ncleared++;
        }
        if (to->solved) ui->holds[i] = 0;
    }
    if (ncleared) {
        ui->markable = 0;
        if (ui->peg_cur == to->solution->npegs)
            ui->peg_cur--;
    }

    return to;
}

static game_state *make_move(game_state *from, game_ui *ui, game_drawstate *ds,
                             int x, int y, int button)
{
    int over_col = 0;           /* one-indexed */
    int over_guess = -1;        /* zero-indexed */
    int over_hint = 0;          /* zero or one */
    game_state *ret = NULL;

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
    }
    debug(("make_move: over_col %d, over_guess %d, over_hint %d",
           over_col, over_guess, over_hint));

    assert(ds->blit_peg);

    /* mouse input */
    if (button == LEFT_BUTTON) {
        if (over_col > 0) {
            ui->drag_col = over_col;
            debug(("Start dragging from colours"));
        } else if (over_guess > -1) {
            int col = ui->curr_pegs->pegs[over_guess];
            if (col) {
                ui->drag_col = col;
                debug(("Start dragging from a guess"));
            }
        }
        if (ui->drag_col) {
            ui->drag_x = x;
            ui->drag_y = y;
            debug(("Start dragging, col = %d, (%d,%d)",
                   ui->drag_col, ui->drag_x, ui->drag_y));
            ret = from;
        }
    } else if (button == LEFT_DRAG && ui->drag_col) {
        ui->drag_x = x;
        ui->drag_y = y;
        debug(("Keep dragging, (%d,%d)", ui->drag_x, ui->drag_y));
        ret = from;
    } else if (button == LEFT_RELEASE && ui->drag_col) {
        if (over_guess > -1) {
            debug(("Dropping colour %d onto guess peg %d",
                   ui->drag_col, over_guess));
            set_peg(ui, over_guess, ui->drag_col);
        }
        ui->drag_col = 0;
        debug(("Stop dragging."));
        ret = from;
    } else if (button == RIGHT_BUTTON) {
        if (over_guess > -1) {
            /* we use ths feedback in the game_ui to signify
             * 'carry this peg to the next guess as well'. */
            ui->holds[over_guess] = 1 - ui->holds[over_guess];
            ret = from;
        }
    } else if (button == LEFT_RELEASE && over_hint && ui->markable) {
        /* NB this won't trigger if on the end of a drag; that's on
         * purpose, in case you drop by mistake... */
        ret = mark_move(from, ui);
    }

    /* keyboard input */
    if (button == CURSOR_UP || button == CURSOR_DOWN) {
        ui->display_cur = 1;
        if (button == CURSOR_DOWN && (ui->colour_cur+1) < from->params.ncolours)
            ui->colour_cur++;
        if (button == CURSOR_UP && ui->colour_cur > 0)
            ui->colour_cur--;
        ret = from;
    } else if (button == CURSOR_LEFT || button == CURSOR_RIGHT) {
        int maxcur = from->params.npegs;
        if (ui->markable) maxcur++;

        ui->display_cur = 1;
        if (button == CURSOR_RIGHT && (ui->peg_cur+1) < maxcur)
            ui->peg_cur++;
        if (button == CURSOR_LEFT && ui->peg_cur > 0)
            ui->peg_cur--;
        ret = from;
    } else if (button == CURSOR_SELECT || button == ' ' || button == '\r' ||
               button == '\n') {
        if (ui->peg_cur == from->params.npegs) {
            ret = mark_move(from, ui);
        } else {
            set_peg(ui, ui->peg_cur, ui->colour_cur+1);
            ret = from;
        }
    }
    return ret;
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

static void game_size(game_params *params, game_drawstate *ds,
                      int *x, int *y, int expand)
{
    double hmul, vmul_c, vmul_g, vmul, szx, szy;
    int sz, colh, guessh;

    hmul = BORDER   * 2.0 +               /* border */
           1.0      * 2.0 +               /* vertical colour bar */
           1.0      * params->npegs +     /* guess pegs */
           PEG_GAP  * params->npegs +     /* guess gaps */
           PEG_HINT * ds->hintw +         /* hint pegs */
           PEG_GAP  * (ds->hintw - 1);    /* hint gaps */

    vmul_c = BORDER  * 2.0 +                    /* border */
             1.0     * params->ncolours +       /* colour pegs */
             PEG_GAP * (params->ncolours - 1);  /* colour gaps */

    vmul_g = BORDER  * 2.0 +                    /* border */
             1.0     * (params->nguesses + 1) + /* guesses plus solution */
             PEG_GAP * (params->nguesses + 1);  /* gaps plus gap above soln */

    vmul = max(vmul_c, vmul_g);

    szx = *x / hmul;
    szy = *y / vmul;
    sz = max(min((int)szx, (int)szy), 1);
    if (expand)
        ds->pegsz = sz;
    else
        ds->pegsz = min(sz, PEG_PREFER_SZ);

    ds->hintsz = (int)((double)ds->pegsz * PEG_HINT);
    ds->gapsz  = (int)((double)ds->pegsz * PEG_GAP);
    ds->border = (int)((double)ds->pegsz * BORDER);

    ds->pegrad  = (ds->pegsz -1)/2; /* radius of peg to fit in pegsz (which is 2r+1) */
    ds->hintrad = (ds->hintsz-1)/2;

    *x = (int)ceil((double)ds->pegsz * hmul);
    *y = (int)ceil((double)ds->pegsz * vmul);
    ds->w = *x; ds->h = *y;

    colh = ((ds->pegsz + ds->gapsz) * params->ncolours) - ds->gapsz;
    guessh = ((ds->pegsz + ds->gapsz) * params->nguesses);      /* guesses */
    guessh += ds->gapsz + ds->pegsz;                            /* solution */

    ds->colx = ds->border;
    ds->coly = (*y - colh) / 2;

    ds->guessx = ds->solnx = ds->border + ds->pegsz * 2;     /* border + colours */
    ds->guessy = (*y - guessh) / 2;
    ds->solny = ds->guessy + ((ds->pegsz + ds->gapsz) * params->nguesses) + ds->gapsz;

    if (ds->pegsz > 0) {
        if (ds->blit_peg) blitter_free(ds->blit_peg);
        ds->blit_peg = blitter_new(ds->pegsz, ds->pegsz);
    }
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_1 * 3 + 0] = 0.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 1.0F;

    ret[COL_2 * 3 + 0] = 0.0F;
    ret[COL_2 * 3 + 1] = 0.5F;
    ret[COL_2 * 3 + 2] = 0.0F;

    ret[COL_3 * 3 + 0] = 1.0F;
    ret[COL_3 * 3 + 1] = 0.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    ret[COL_4 * 3 + 0] = 1.0F;
    ret[COL_4 * 3 + 1] = 1.0F;
    ret[COL_4 * 3 + 2] = 0.0F;

    ret[COL_5 * 3 + 0] = 1.0F;
    ret[COL_5 * 3 + 1] = 0.0F;
    ret[COL_5 * 3 + 2] = 1.0F;

    ret[COL_6 * 3 + 0] = 0.0F;
    ret[COL_6 * 3 + 1] = 1.0F;
    ret[COL_6 * 3 + 2] = 1.0F;

    ret[COL_7 * 3 + 0] = 0.5F;
    ret[COL_7 * 3 + 1] = 0.5F;
    ret[COL_7 * 3 + 2] = 1.0F;

    ret[COL_8 * 3 + 0] = 0.5F;
    ret[COL_8 * 3 + 1] = 1.0F;
    ret[COL_8 * 3 + 2] = 0.5F;

    ret[COL_9 * 3 + 0] = 1.0F;
    ret[COL_9 * 3 + 1] = 0.5F;
    ret[COL_9 * 3 + 2] = 0.5F;

    ret[COL_10 * 3 + 0] = 1.0F;
    ret[COL_10 * 3 + 1] = 1.0F;
    ret[COL_10 * 3 + 2] = 1.0F;

    ret[COL_FRAME * 3 + 0] = 0.0F;
    ret[COL_FRAME * 3 + 1] = 0.0F;
    ret[COL_FRAME * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_LOWLIGHT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0 / 3.0;
    ret[COL_LOWLIGHT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0 / 3.0;
    ret[COL_LOWLIGHT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0 / 3.0;

    ret[COL_FLASH * 3 + 0] = 0.5F;
    ret[COL_FLASH * 3 + 1] = 1.0F;
    ret[COL_FLASH * 3 + 2] = 1.0F;

    ret[COL_HOLD * 3 + 0] = 1.0F;
    ret[COL_HOLD * 3 + 1] = 0.5F;
    ret[COL_HOLD * 3 + 2] = 0.5F;

    ret[COL_EMPTY * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0 / 3.0;
    ret[COL_EMPTY * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0 / 3.0;
    ret[COL_EMPTY * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0 / 3.0;

    ret[COL_CORRECTPLACE*3 + 0] = 1.0F;
    ret[COL_CORRECTPLACE*3 + 1] = 0.0F;
    ret[COL_CORRECTPLACE*3 + 2] = 0.0F;

    ret[COL_CORRECTCOLOUR*3 + 0] = 1.0F;
    ret[COL_CORRECTCOLOUR*3 + 1] = 1.0F;
    ret[COL_CORRECTCOLOUR*3 + 2] = 1.0F;

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

    ds->holds = snewn(state->params.npegs, int);
    memset(ds->holds, 0, state->params.npegs*sizeof(int));

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
    sfree(ds->holds);
    sfree(ds->guesses);
    sfree(ds);
}

static void draw_peg(frontend *fe, game_drawstate *ds, int cx, int cy, int col)
{
    if (PEGRAD > 0) {
        draw_circle(fe, cx+PEGRAD, cy+PEGRAD, PEGRAD, 1, COL_EMPTY + col);
        draw_circle(fe, cx+PEGRAD, cy+PEGRAD, PEGRAD, 0, COL_EMPTY + col);
    } else
        draw_rect(fe, cx, cy, PEGSZ, PEGSZ, COL_EMPTY + col);
    draw_update(fe, cx, cy, PEGSZ, PEGSZ);
}

static void guess_redraw(frontend *fe, game_drawstate *ds, int guess,
                         pegrow src, int force)
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
        if ((dest->pegs[i] != scol) || force)
	    draw_peg(fe, ds, rowx + PEGOFF * i, rowy, scol);
        dest->pegs[i] = scol;
    }
}

static void hint_redraw(frontend *fe, game_drawstate *ds, int guess,
                        pegrow src, int force, int emptycol)
{
    pegrow dest = ds->guesses[guess];
    int rowx, rowy, i, scol, col, hintlen;

    if (src) assert(src->npegs == dest->npegs);

    hintlen = (dest->npegs + 1)/2;

    for (i = 0; i < dest->npegs; i++) {
        scol = src ? src->feedback[i] : 0;
        col = (scol == FEEDBACK_CORRECTPLACE) ? COL_CORRECTPLACE :
              (scol == FEEDBACK_CORRECTCOLOUR) ? COL_CORRECTCOLOUR : emptycol;
        if ((scol != dest->feedback[i]) || force) {
            rowx = HINT_X(guess);
            rowy = HINT_Y(guess);
            if (i < hintlen) {
                rowx += HINTOFF * i;
            } else {
                rowx += HINTOFF * (i - hintlen);
                rowy += HINTOFF;
            }
            if (HINTRAD > 0) {
                draw_circle(fe, rowx+HINTRAD, rowy+HINTRAD, HINTRAD, 1, col);
                draw_circle(fe, rowx+HINTRAD, rowy+HINTRAD, HINTRAD, 0, col);
            } else {
                draw_rect(fe, rowx, rowy, HINTSZ, HINTSZ, col);
            }
            draw_update(fe, rowx, rowy, HINTSZ, HINTSZ);
        }
        dest->feedback[i] = scol;
    }
}

static void hold_redraw(frontend *fe, game_drawstate *ds, int guess, int *src, int force)
{
    int shold, col, ox, oy, i;

    if (guess >= ds->nguesses)
        return;

    for (i = 0; i < ds->solution->npegs; i++) {
        shold = src ? src[i] : 0;
        col = shold ? COL_HOLD : COL_BACKGROUND;
        if ((shold != ds->holds[i]) || force) {
            ox = GUESS_X(guess, i);
            oy = GUESS_Y(guess, i) + PEGSZ + ds->gapsz/2;

            draw_rect(fe, ox, oy, PEGSZ, 2, col);
            draw_update(fe, ox, oy, PEGSZ, 2);
        }
        if (src) ds->holds[i] = shold;
    }
}

static void currmove_redraw(frontend *fe, game_drawstate *ds, int guess, int col)
{
    int ox = GUESS_X(guess, 0), oy = GUESS_Y(guess, 0), off = PEGSZ/4;

    draw_rect(fe, ox-off-1, oy, 2, PEGSZ, col);
    draw_update(fe, ox-off-1, oy, 2, PEGSZ);
}

static void cur_redraw(frontend *fe, game_drawstate *ds,
                       int x, int y, int erase)
{
    int cgap = ds->gapsz / 2;
    int x1, y1, x2, y2, hi, lo;

    x1 = x-cgap; x2 = x+PEGSZ+cgap;
    y1 = y-cgap; y2 = y+PEGSZ+cgap;
    hi = erase ? COL_BACKGROUND : COL_HIGHLIGHT;
    lo = erase ? COL_BACKGROUND : COL_LOWLIGHT;

    draw_line(fe, x1, y1, x2, y1, hi);
    draw_line(fe, x2, y1, x2, y2, lo);
    draw_line(fe, x2, y2, x1, y2, lo);
    draw_line(fe, x1, y2, x1, y1, hi);

    draw_update(fe, x1, y1, x2, y2);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int i, cur_erase = 0, cur_draw = 0, new_move, last_go;

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
        if (ds->colours->pegs[i] != i+1) {
	    draw_peg(fe, ds, COL_X(i), COL_Y(i), i+1);
            ds->colours->pegs[i] = i+1;
        }
    }

    /* draw the guesses (so far) and the hints */
    for (i = 0; i < state->params.nguesses; i++) {
        if (state->next_go > i || state->solved) {
            /* this info is stored in the game_state already */
            guess_redraw(fe, ds, i, state->guesses[i], 0);
            hint_redraw(fe, ds, i, state->guesses[i],
                        i == (state->next_go-1) ? 1 : 0, COL_EMPTY);
        } else if (state->next_go == i) {
            /* this is the one we're on; the (incomplete) guess is
             * stored in the game_ui. */
            guess_redraw(fe, ds, i, ui->curr_pegs, 0);
            hint_redraw(fe, ds, i, NULL, 1, ui->markable ? COL_FLASH : COL_EMPTY);
        } else {
            /* we've not got here yet; it's blank. */
            guess_redraw(fe, ds, i, NULL, 0);
            hint_redraw(fe, ds, i, NULL, 0, COL_EMPTY);
        }
    }

    /* draw the 'hold' markers */
    if (state->solved) {
        hold_redraw(fe, ds, state->next_go, NULL, 1);
    } else if (new_move) {
        hold_redraw(fe, ds, ds->next_go, NULL, 1);
        hold_redraw(fe, ds, state->next_go, last_go ? NULL : ui->holds, 1);
    } else
        hold_redraw(fe, ds, state->next_go, last_go ? NULL : ui->holds, 0);

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
        guess_redraw(fe, ds, -1, state->solution, !ds->solved);
    ds->solved = state->solved;

    if (ui->display_cur && !ds->display_cur)
        cur_draw = 1;
    else if (!ui->display_cur && ds->display_cur)
        cur_erase = 1;
    else if (ui->display_cur) {
        if ((state->next_go != ds->next_go) ||
            (ui->peg_cur != ds->peg_cur) ||
            (ui->colour_cur != ds->colour_cur)) {
            cur_erase = 1;
            cur_draw = 1;
        }
    }
    if (cur_erase) {
        cur_redraw(fe, ds, COL_X(ds->colour_cur), COL_Y(ds->colour_cur), 1);
        cur_redraw(fe, ds,
                   GUESS_X(ds->next_go, ds->peg_cur), GUESS_Y(ds->next_go, ds->peg_cur), 1);
    }
    if (cur_draw) {
        cur_redraw(fe, ds, COL_X(ui->colour_cur), COL_Y(ui->colour_cur), 0);
        cur_redraw(fe, ds,
                   GUESS_X(state->next_go, ui->peg_cur), GUESS_Y(state->next_go, ui->peg_cur), 0);
    }
    ds->display_cur = ui->display_cur;
    ds->peg_cur = ui->peg_cur;
    ds->colour_cur = ui->colour_cur;
    ds->next_go = state->next_go;

    /* if ui->drag_col != 0, save the screen to the blitter,
     * draw the peg where we saved, and set ds->drag_* == ui->drag_*. */
    if (ui->drag_col != 0) {
        int ox = ui->drag_x - (PEGSZ/2);
        int oy = ui->drag_y - (PEGSZ/2);
        debug(("Saving to blitter at (%d,%d)", ox, oy));
        blitter_save(fe, ds->blit_peg, ox, oy);
        draw_peg(fe, ds, ox, oy, ui->drag_col);

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
    "Guess", NULL,
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
    make_move,
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

/* vim: set shiftwidth=4 tabstop=8: */
