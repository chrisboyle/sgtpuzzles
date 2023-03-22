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
    bool allow_blank, allow_multiple;
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
    bool *holds;
    pegrow solution;
    int next_go; /* from 0 to nguesses-1;
                    if next_go == nguesses then they've lost. */
    int solved;   /* +1 = win, -1 = lose, 0 = still playing */
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    /* AFAIK this is the canonical Mastermind ruleset. */
    ret->ncolours = 6;
    ret->npegs = 4;
    ret->nguesses = 10;

    ret->allow_blank = false;
    ret->allow_multiple = true;

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

static const struct {
    const char *name;
    game_params params;
} guess_presets[] = {
    {"Standard", {6, 4, 10, false, true}},
    {"Super", {8, 5, 12, false, true}},
};


static bool game_fetch_preset(int i, char **name, game_params **params)
{
    if (i < 0 || i >= lenof(guess_presets))
        return false;

    *name = dupstr(guess_presets[i].name);
    *params = dup_params(&guess_presets[i].params);

    return true;
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
            params->allow_blank = true;
            break;

        case 'B':
            params->allow_blank = false;
            break;

        case 'm':
            params->allow_multiple = true;
            break;

        case 'M':
            params->allow_multiple = false;
            break;

	default:
            ;
	}
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char data[256];

    sprintf(data, "c%dp%dg%d%s%s",
            params->ncolours, params->npegs, params->nguesses,
            params->allow_blank ? "b" : "B", params->allow_multiple ? "m" : "M");

    return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Colours";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->ncolours);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Pegs per guess";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->npegs);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Guesses";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->nguesses);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Allow blanks";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->allow_blank;

    ret[4].name = "Allow duplicates";
    ret[4].type = C_BOOLEAN;
    ret[4].u.boolean.bval = params->allow_multiple;

    ret[5].name = NULL;
    ret[5].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->ncolours = atoi(cfg[0].u.string.sval);
    ret->npegs = atoi(cfg[1].u.string.sval);
    ret->nguesses = atoi(cfg[2].u.string.sval);

    ret->allow_blank = cfg[3].u.boolean.bval;
    ret->allow_multiple = cfg[4].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
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

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
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
    obfuscate_bitmap(bmp, params->npegs*8, false);

    ret = bin2hex(bmp, params->npegs);
    sfree(bmp);
    free_pegrow(colcount);
    return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    unsigned char *bmp;
    int i;

    /* desc is just an (obfuscated) bitmap of the solution; check that
     * it's the correct length and (when unobfuscated) contains only
     * sensible colours. */
    if (strlen(desc) != params->npegs * 2)
        return "Game description is wrong length";
    bmp = hex2bin(desc, params->npegs);
    obfuscate_bitmap(bmp, params->npegs*8, true);
    for (i = 0; i < params->npegs; i++) {
        if (bmp[i] < 1 || bmp[i] > params->ncolours) {
            sfree(bmp);
            return "Game description is corrupted";
        }
    }
    sfree(bmp);

    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    unsigned char *bmp;
    int i;

    state->params = *params;
    state->guesses = snewn(params->nguesses, pegrow);
    for (i = 0; i < params->nguesses; i++)
	state->guesses[i] = new_pegrow(params->npegs);
    state->holds = snewn(params->npegs, bool);
    state->solution = new_pegrow(params->npegs);

    bmp = hex2bin(desc, params->npegs);
    obfuscate_bitmap(bmp, params->npegs*8, true);
    for (i = 0; i < params->npegs; i++)
	state->solution->pegs[i] = (int)bmp[i];
    sfree(bmp);

    memset(state->holds, 0, sizeof(bool) * params->npegs);
    state->next_go = state->solved = 0;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);
    int i;

    *ret = *state;

    ret->guesses = snewn(state->params.nguesses, pegrow);
    for (i = 0; i < state->params.nguesses; i++)
	ret->guesses[i] = dup_pegrow(state->guesses[i]);
    ret->holds = snewn(state->params.npegs, bool);
    memcpy(ret->holds, state->holds, sizeof(bool) * state->params.npegs);
    ret->solution = dup_pegrow(state->solution);

    return ret;
}

static void free_game(game_state *state)
{
    int i;

    free_pegrow(state->solution);
    for (i = 0; i < state->params.nguesses; i++)
	free_pegrow(state->guesses[i]);
    sfree(state->holds);
    sfree(state->guesses);

    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    return dupstr("S");
}

static bool is_markable(const game_params *params, pegrow pegs)
{
    int i, nset = 0, nrequired;
    bool ret = false;
    pegrow colcount = new_pegrow(params->ncolours);

    nrequired = params->allow_blank ? 1 : params->npegs;

    for (i = 0; i < params->npegs; i++) {
        int c = pegs->pegs[i];
        if (c > 0) {
            assert(c <= params->ncolours);
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
    ret = true;
done:
    free_pegrow(colcount);
    return ret;
}

struct game_ui {
    game_params params;
    pegrow curr_pegs; /* half-finished current move */
    bool *holds;
    int colour_cur;   /* position of up-down colour picker cursor */
    int peg_cur;      /* position of left-right peg picker cursor */
    bool display_cur, markable;

    int drag_col, drag_x, drag_y; /* x and y are *center* of peg! */
    int drag_opeg; /* peg index, if dragged from a peg (from current guess), otherwise -1 */

    bool show_labels;                   /* label the colours with numbers */
    pegrow hint;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    memset(ui, 0, sizeof(game_ui));
    ui->params = state->params;        /* structure copy */
    ui->curr_pegs = new_pegrow(state->params.npegs);
    ui->holds = snewn(state->params.npegs, bool);
    ui->display_cur = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    memset(ui->holds, 0, sizeof(bool)*state->params.npegs);
    ui->drag_opeg = -1;
    return ui;
}

static void free_ui(game_ui *ui)
{
    if (ui->hint)
        free_pegrow(ui->hint);
    free_pegrow(ui->curr_pegs);
    sfree(ui->holds);
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    char *ret, *p;
    const char *sep;
    int i;

    /*
     * For this game it's worth storing the contents of the current
     * guess, and the current set of holds.
     */
    ret = snewn(40 * ui->curr_pegs->npegs, char);
    p = ret;
    sep = "";
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
        p += sprintf(p, "%s%d%s", sep, ui->curr_pegs->pegs[i],
                     ui->holds[i] ? "_" : "");
        sep = ",";
    }
    *p++ = '\0';
    assert(p - ret < 40 * ui->curr_pegs->npegs);
    return sresize(ret, p - ret, char);
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    int i;
    const char *p = encoding;
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
        ui->curr_pegs->pegs[i] = atoi(p);
        if (ui->curr_pegs->pegs[i] < 0 ||
            ui->curr_pegs->pegs[i] > ui->params.ncolours)
            ui->curr_pegs->pegs[i] = 0; /* Remove invalid pegs. */
        while (*p && isdigit((unsigned char)*p)) p++;
        if (*p == '_') {
            /* NB: old versions didn't store holds */
            ui->holds[i] = true;
            p++;
        } else
            ui->holds[i] = false;
        if (*p == ',') p++;
    }
    ui->markable = is_markable(&ui->params, ui->curr_pegs);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    int i;

    if (newstate->next_go < oldstate->next_go) {
        sfree(ui->hint);
        ui->hint = NULL;
    }

    /* Implement holds, clear other pegs.
     * This does something that is arguably the Right Thing even
     * for undo. */
    for (i = 0; i < newstate->solution->npegs; i++) {
        if (newstate->solved)
            ui->holds[i] = false;
        else
            ui->holds[i] = newstate->holds[i];
	if (newstate->solved || (newstate->next_go == 0) || !ui->holds[i]) {
	    ui->curr_pegs->pegs[i] = 0;
	} else
            ui->curr_pegs->pegs[i] =
                newstate->guesses[newstate->next_go-1]->pegs[i];
    }
    ui->markable = is_markable(&newstate->params, ui->curr_pegs);
    /* Clean up cursor position */
    if (!ui->markable && ui->peg_cur == newstate->solution->npegs)
	ui->peg_cur = 0;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (state->solved) return "";
    if (button == CURSOR_SELECT) {
        if (ui->peg_cur == state->params.npegs) return "Submit";
        return "Place";
    }
    if (button == CURSOR_SELECT2 && ui->peg_cur != state->params.npegs)
        return "Hold";
    return "";
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
    int w, h;
    bool started;
    int solved;

    int next_go;

    blitter *blit_peg;
    int drag_col, blit_ox, blit_oy;
};

static void set_peg(const game_params *params, game_ui *ui, int peg, int col)
{
    ui->curr_pegs->pegs[peg] = col;
    ui->markable = is_markable(params, ui->curr_pegs);
}

static int mark_pegs(pegrow guess, const pegrow solution, int ncols)
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

static char *encode_move(const game_state *from, game_ui *ui)
{
    char *buf, *p;
    const char *sep;
    int len, i;

    len = ui->curr_pegs->npegs * 20 + 2;
    buf = snewn(len, char);
    p = buf;
    *p++ = 'G';
    sep = "";
    for (i = 0; i < ui->curr_pegs->npegs; i++) {
	p += sprintf(p, "%s%d%s", sep, ui->curr_pegs->pegs[i],
                     ui->holds[i] ? "_" : "");
	sep = ",";
    }
    *p++ = '\0';
    assert(p - buf <= len);
    buf = sresize(buf, len, char);

    return buf;
}

static void compute_hint(const game_state *state, game_ui *ui)
{
    /* Suggest the lexicographically first row consistent with all
     * previous feedback.  This is not only a useful hint, but also
     * a reasonable strategy if applied consistently.  If the user
     * uses hints in every turn, they may be able to intuit this
     * strategy, or one similar to it.  I (Jonas Kölker) came up
     * with something close to it without seeing it in action. */

    /* Some performance characteristics: I want to ask for each n,
     * how many solutions are guessed in exactly n guesses if you
     * use the hint in each turn.
     *
     * With 4 pegs and 6 colours you get the following histogram:
     *
     *  1 guesses:     1 solution
     *  2 guesses:     4 solutions
     *  3 guesses:    25 solutions
     *  4 guesses:   108 solutions
     *  5 guesses:   305 solutions
     *  6 guesses:   602 solutions
     *  7 guesses:   196 solutions
     *  8 guesses:    49 solutions
     *  9 guesses:     6 solutions
     * (note: the tenth guess is never necessary.)
     *
     * With 5 pegs and 8 colours you get the following histogram:
     *
     *  1 guesses:     1 solution
     *  2 guesses:     5 solutions
     *  3 guesses:    43 solutions
     *  4 guesses:   278 solutions
     *  5 guesses:  1240 solutions
     *  6 guesses:  3515 solutions
     *  7 guesses:  7564 solutions
     *  8 guesses: 14086 solutions
     *  9 guesses:  4614 solutions
     * 10 guesses:  1239 solutions
     * 11 guesses:   175 solutions
     * 12 guesses:     7 solutions
     * 13 guesses:     1 solution
     *
     * The solution which takes too many guesses is {8, 8, 5, 6, 7}.
     * The game ID is c8p5g12Bm:4991e5e41a. */

    int mincolour = 1, maxcolour = 0, i, j;

    /* For large values of npegs and ncolours, the lexicographically
     * next guess make take a while to find.  Finding upper and
     * lower limits on which colours we have to consider will speed
     * this up, as will caching our progress from one invocation to
     * the next.  The latter strategy works, since if we have ruled
     * out a candidate we will never reverse this judgment in the
     * light of new information.  Removing information, i.e. undo,
     * will require us to backtrack somehow.  We backtrack by fully
     * forgetting our progress (and recomputing it if required). */

    for (i = 0; i < state->next_go; ++i)
        for (j = 0; j < state->params.npegs; ++j)
            if (state->guesses[i]->pegs[j] > maxcolour)
                maxcolour = state->guesses[i]->pegs[j];
    if (state->params.allow_multiple)
        maxcolour = min(maxcolour + 1, state->params.ncolours);
    else
        maxcolour = min(maxcolour + state->params.npegs,
                        state->params.ncolours);

increase_mincolour:
    for (i = 0; i < state->next_go; ++i) {
        if (state->guesses[i]->feedback[0])
            goto next_iteration;
        for (j = 0; j < state->params.npegs; ++j)
            if (state->guesses[i]->pegs[j] != mincolour)
                goto next_iteration;
        ++mincolour;
        goto increase_mincolour;
    next_iteration:
        ;
    }

    if (!ui->hint) {
        ui->hint = new_pegrow(state->params.npegs);
        for (i = 0; i < state->params.npegs; ++i)
            ui->hint->pegs[i] = 1;
    }

    while (ui->hint->pegs[0] <= state->params.ncolours) {
        if (!is_markable(&state->params, ui->hint)) goto increment_pegrow;
        for (i = 0; i < state->next_go; ++i) {
            mark_pegs(ui->hint, state->guesses[i], maxcolour);
            for (j = 0; j < state->params.npegs; ++j)
                if (ui->hint->feedback[j] != state->guesses[i]->feedback[j])
                    goto increment_pegrow;
        }
        /* a valid guess was found; install it and return */
        for (i = 0; i < state->params.npegs; ++i)
            ui->curr_pegs->pegs[i] = ui->hint->pegs[i];

        ui->markable = true;
        ui->peg_cur = state->params.npegs;
        ui->display_cur = true;
        return;

    increment_pegrow:
        for (i = ui->hint->npegs;
             ++ui->hint->pegs[--i], i && ui->hint->pegs[i] > maxcolour;
             ui->hint->pegs[i] = mincolour);
    }
    /* No solution is compatible with the given hints.  Impossible! */
    /* (hack new_game_desc to create invalid solutions to get here) */

    /* For some values of npegs and ncolours, the hinting function takes a
     * long time to complete.  To visually indicate completion with failure,
     * should it ever happen, update the ui in some trivial way.  This gives
     * the user a sense of broken(ish)ness and futility. */
    if (!ui->display_cur) {
        ui->display_cur = true;
    } else if (state->params.npegs == 1) {
        ui->display_cur = false;
    } else {
        ui->peg_cur = (ui->peg_cur + 1) % state->params.npegs;
    }
}

static char *interpret_move(const game_state *from, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int over_col = 0;           /* one-indexed */
    int over_guess = -1;        /* zero-indexed */
    int over_past_guess_y = -1; /* zero-indexed */
    int over_past_guess_x = -1; /* zero-indexed */
    bool over_hint = false;
    char *ret = NULL;

    int guess_ox = GUESS_X(from->next_go, 0);
    int guess_oy = GUESS_Y(from->next_go, 0);

    /*
     * Enable or disable labels on colours.
     */
    if (button == 'l' || button == 'L') {
        ui->show_labels = !ui->show_labels;
        return UI_UPDATE;
    }

    if (from->solved) return NULL;

    if (x >= COL_OX && x < (COL_OX + COL_W) &&
        y >= COL_OY && y < (COL_OY + COL_H)) {
        over_col = ((y - COL_OY) / PEGOFF) + 1;
        assert(over_col >= 1 && over_col <= ds->colours->npegs);
    } else if (x >= guess_ox &&
               y >= guess_oy && y < (guess_oy + GUESS_H)) {
        if (x < (guess_ox + GUESS_W)) {
            over_guess = (x - guess_ox) / PEGOFF;
            assert(over_guess >= 0 && over_guess < ds->solution->npegs);
        } else {
            over_hint = true;
        }
    } else if (x >= guess_ox && x < (guess_ox + GUESS_W) &&
               y >= GUESS_OY && y < guess_oy) {
        over_past_guess_y = (y - GUESS_OY) / PEGOFF;
        over_past_guess_x = (x - guess_ox) / PEGOFF;
        assert(over_past_guess_y >= 0 && over_past_guess_y < from->next_go);
        assert(over_past_guess_x >= 0 && over_past_guess_x < ds->solution->npegs);
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
            ret = UI_UPDATE;
        }
    } else if (button == LEFT_DRAG && ui->drag_col) {
        ui->drag_x = x;
        ui->drag_y = y;
        debug(("Keep dragging, (%d,%d)", ui->drag_x, ui->drag_y));
        ret = UI_UPDATE;
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
        ui->display_cur = false;
        debug(("Stop dragging."));
        ret = UI_UPDATE;
    } else if (button == RIGHT_BUTTON) {
        if (over_guess > -1) {
            /* we use ths feedback in the game_ui to signify
             * 'carry this peg to the next guess as well'. */
            ui->holds[over_guess] ^= 1;
            ret = UI_UPDATE;
        }
    } else if (button == LEFT_RELEASE && over_hint && ui->markable) {
        /* NB this won't trigger if on the end of a drag; that's on
         * purpose, in case you drop by mistake... */
        ret = encode_move(from, ui);
    }

    /* keyboard input */
    if (button == CURSOR_UP || button == CURSOR_DOWN) {
        ui->display_cur = true;
        if (button == CURSOR_DOWN && (ui->colour_cur+1) < from->params.ncolours)
            ui->colour_cur++;
        if (button == CURSOR_UP && ui->colour_cur > 0)
            ui->colour_cur--;
        ret = UI_UPDATE;
    } else if (button == 'h' || button == 'H' || button == '?') {
        compute_hint(from, ui);
        ret = UI_UPDATE;
    } else if (button == CURSOR_LEFT || button == CURSOR_RIGHT) {
        int maxcur = from->params.npegs;
        if (ui->markable) maxcur++;

        ui->display_cur = true;
        if (button == CURSOR_RIGHT && (ui->peg_cur+1) < maxcur)
            ui->peg_cur++;
        if (button == CURSOR_LEFT && ui->peg_cur > 0)
            ui->peg_cur--;
        ret = UI_UPDATE;
    } else if (button == CURSOR_SELECT) {
        ui->display_cur = true;
        if (ui->peg_cur == from->params.npegs) {
            ret = encode_move(from, ui);
        } else {
            set_peg(&from->params, ui, ui->peg_cur, ui->colour_cur+1);
            ret = UI_UPDATE;
        }
    } else if (((button >= '1' && button <= '0' + from->params.ncolours) ||
                (button == '0' && from->params.ncolours == 10)) &&
               ui->peg_cur < from->params.npegs) {
        ui->display_cur = true;
        /* Number keys insert a peg and advance the cursor. */
        set_peg(&from->params, ui, ui->peg_cur,
                button == '0' ? 10 : button - '0');
        if (ui->peg_cur + 1 < from->params.npegs + ui->markable)
            ui->peg_cur++;
        ret = UI_UPDATE;
    } else if (button == 'D' || button == 'd' || button == '\b') {
        ui->display_cur = true;
        set_peg(&from->params, ui, ui->peg_cur, 0);
        ret = UI_UPDATE;
    } else if (button == CURSOR_SELECT2) {
        if (ui->peg_cur == from->params.npegs)
            return NULL;
        ui->display_cur = true;
        ui->holds[ui->peg_cur] ^= 1;
        ret = UI_UPDATE;
    }
    return ret;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int i, nc_place;
    game_state *ret;
    const char *p;

    if (!strcmp(move, "S")) {
	ret = dup_game(from);
	ret->solved = -1;
	return ret;
    } else if (move[0] == 'G') {
        /* No guesses are allowed once the game is solved. */
        if (from->solved) return NULL;
	p = move+1;

	ret = dup_game(from);

	for (i = 0; i < from->solution->npegs; i++) {
	    int val = atoi(p);
	    int min_colour = from->params.allow_blank? 0 : 1;
	    if (val < min_colour || val > from->params.ncolours) {
		free_game(ret);
		return NULL;
	    }
	    ret->guesses[from->next_go]->pegs[i] = atoi(p);
	    while (*p && isdigit((unsigned char)*p)) p++;
            if (*p == '_') {
                ret->holds[i] = true;
                p++;
            } else
                ret->holds[i] = false;
	    if (*p == ',') p++;
	}

	nc_place = mark_pegs(ret->guesses[from->next_go], ret->solution, ret->params.ncolours);

	if (nc_place == ret->solution->npegs) {
	    ret->solved = +1; /* win! */
	} else {
	    ret->next_go = from->next_go + 1;
	    if (ret->next_go >= ret->params.nguesses)
		ret->solved = -1; /* lose, meaning we show the pegs. */
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

static void game_compute_size(const game_params *params, int tilesize,
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

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    int colh, guessh;

    ds->pegsz = tilesize;

    ds->hintsz = (int)((double)ds->pegsz * PEG_HINT);
    ds->gapsz  = (int)((double)ds->pegsz * PEG_GAP);
    ds->border = (int)((double)ds->pegsz * BORDER);

    ds->pegrad  = (ds->pegsz -1)/2; /* radius of peg to fit in pegsz (which is 2r+1) */
    ds->hintrad = (ds->hintsz-1)/2;

    colh = ((ds->pegsz + ds->gapsz) * params->ncolours) - ds->gapsz;
    guessh = ((ds->pegsz + ds->gapsz) * params->nguesses);      /* guesses */
    guessh += ds->gapsz + ds->pegsz;                            /* solution */

    game_compute_size(params, tilesize, &ds->w, &ds->h);
    ds->colx = ds->border;
    ds->coly = (ds->h - colh) / 2;

    ds->guessx = ds->solnx = ds->border + ds->pegsz * 2;     /* border + colours */
    ds->guessy = (ds->h - guessh) / 2;
    ds->solny = ds->guessy + ((ds->pegsz + ds->gapsz) * params->nguesses) + ds->gapsz;

    assert(ds->pegsz > 0);
    assert(!ds->blit_peg);             /* set_size is never called twice */
    ds->blit_peg = blitter_new(dr, ds->pegsz+2, ds->pegsz+2);
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float), max;
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    /* red */
    ret[COL_1 * 3 + 0] = 1.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 0.0F;

    /* yellow */
    ret[COL_2 * 3 + 0] = 1.0F;
    ret[COL_2 * 3 + 1] = 1.0F;
    ret[COL_2 * 3 + 2] = 0.0F;

    /* green */
    ret[COL_3 * 3 + 0] = 0.0F;
    ret[COL_3 * 3 + 1] = 1.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    /* blue */
    ret[COL_4 * 3 + 0] = 0.2F;
    ret[COL_4 * 3 + 1] = 0.3F;
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
    ret[COL_7 * 3 + 0] = 0.5F;
    ret[COL_7 * 3 + 1] = 0.3F;
    ret[COL_7 * 3 + 2] = 0.3F;

    /* light blue */
    ret[COL_8 * 3 + 0] = 0.4F;
    ret[COL_8 * 3 + 1] = 0.8F;
    ret[COL_8 * 3 + 2] = 1.0F;

    /* light green */
    ret[COL_9 * 3 + 0] = 0.7F;
    ret[COL_9 * 3 + 1] = 1.0F;
    ret[COL_9 * 3 + 2] = 0.7F;

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
    ret[COL_EMPTY * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 2.0F / 3.0F;
    ret[COL_EMPTY * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 2.0F / 3.0F;
    ret[COL_EMPTY * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 2.0F / 3.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
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

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    int i;

    if (ds->blit_peg) blitter_free(dr, ds->blit_peg);
    free_pegrow(ds->colours);
    free_pegrow(ds->solution);
    for (i = 0; i < ds->nguesses; i++)
        free_pegrow(ds->guesses[i]);
    sfree(ds->guesses);
    sfree(ds);
}

static void draw_peg(drawing *dr, game_drawstate *ds, int cx, int cy,
		     bool moving, bool labelled, int col)
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
        draw_rect(dr, cx-CGAP, cy-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2,
                  COL_BACKGROUND);
    if (PEGRAD > 0) {
        draw_circle(dr, cx+PEGRAD, cy+PEGRAD, PEGRAD,
		    COL_EMPTY + col, (col ? COL_FRAME : COL_EMPTY));
    } else
        draw_rect(dr, cx, cy, PEGSZ, PEGSZ, COL_EMPTY + col);

    if (labelled && col) {
        char buf[2];
        buf[0] = '0' + (col % 10);
        buf[1] = '\0';
        draw_text(dr, cx+PEGRAD, cy+PEGRAD, FONT_VARIABLE, PEGRAD,
                  ALIGN_HCENTRE|ALIGN_VCENTRE, COL_FRAME, buf);
    }

    draw_update(dr, cx-CGAP, cy-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2);
}

static void draw_cursor(drawing *dr, game_drawstate *ds, int x, int y)
{
    draw_circle(dr, x+PEGRAD, y+PEGRAD, PEGRAD+CGAP, -1, COL_CURSOR);

    draw_update(dr, x-CGAP, y-CGAP, PEGSZ+CGAP*2, PEGSZ+CGAP*2);
}

static void guess_redraw(drawing *dr, game_drawstate *ds, int guess,
                         pegrow src, bool *holds, int cur_col, bool force,
                         bool labelled)
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
        if (labelled)
            scol |= 0x4000;
        if ((dest->pegs[i] != scol) || force) {
	    draw_peg(dr, ds, rowx + PEGOFF * i, rowy, false, labelled,
                     scol &~ 0x7000);
            /*
             * Hold marker.
             */
            draw_rect(dr, rowx + PEGOFF * i, rowy + PEGSZ + ds->gapsz/2,
                      PEGSZ, 2, (scol & 0x2000 ? COL_HOLD : COL_BACKGROUND));
            draw_update(dr, rowx + PEGOFF * i, rowy + PEGSZ + ds->gapsz/2,
                        PEGSZ, 2);
            if (scol & 0x1000)
                draw_cursor(dr, ds, rowx + PEGOFF * i, rowy);
        }
        dest->pegs[i] = scol;
    }
}

static void hint_redraw(drawing *dr, game_drawstate *ds, int guess,
                        pegrow src, bool force, bool cursor, bool markable)
{
    pegrow dest = ds->guesses[guess];
    int rowx, rowy, i, scol, col, hintlen;
    bool need_redraw;
    int emptycol = (markable ? COL_FLASH : COL_EMPTY);

    if (src) assert(src->npegs == dest->npegs);

    hintlen = (dest->npegs + 1)/2;

    /*
     * Because of the possible presence of the cursor around this
     * entire section, we redraw all or none of it but never part.
     */
    need_redraw = false;

    for (i = 0; i < dest->npegs; i++) {
        scol = src ? src->feedback[i] : 0;
        if (i == 0 && cursor)
            scol |= 0x1000;
        if (i == 0 && markable)
            scol |= 0x2000;
        if ((scol != dest->feedback[i]) || force) {
            need_redraw = true;
        }
        dest->feedback[i] = scol;
    }

    if (need_redraw) {
        int hinth = HINTSZ + GAP + HINTSZ;
        int hx,hy,hw,hh;

        hx = HINT_X(guess)-GAP; hy = HINT_Y(guess)-GAP;
        hw = HINT_W+GAP*2; hh = hinth+GAP*2;

        /* erase a large background rectangle */
        draw_rect(dr, hx, hy, hw, hh, COL_BACKGROUND);

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
                draw_circle(dr, rowx+HINTRAD, rowy+HINTRAD, HINTRAD, col,
                            (col == emptycol ? emptycol : COL_FRAME));
            } else {
                draw_rect(dr, rowx, rowy, HINTSZ, HINTSZ, col);
            }
        }
        if (cursor) {
            int x1,y1,x2,y2;
            x1 = hx + CGAP; y1 = hy + CGAP;
            x2 = hx + hw - CGAP; y2 = hy + hh - CGAP;
            draw_line(dr, x1, y1, x2, y1, COL_CURSOR);
            draw_line(dr, x2, y1, x2, y2, COL_CURSOR);
            draw_line(dr, x2, y2, x1, y2, COL_CURSOR);
            draw_line(dr, x1, y2, x1, y1, COL_CURSOR);
        }

        draw_update(dr, hx, hy, hw, hh);
    }
}

static void currmove_redraw(drawing *dr, game_drawstate *ds, int guess, int col)
{
    int ox = GUESS_X(guess, 0), oy = GUESS_Y(guess, 0), off = PEGSZ/4;

    draw_rect(dr, ox-off-1, oy, 2, PEGSZ, col);
    draw_update(dr, ox-off-1, oy, 2, PEGSZ);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i;
    bool new_move;

    new_move = (state->next_go != ds->next_go) || !ds->started;

    if (!ds->started) {
      draw_rect(dr, SOLN_OX, SOLN_OY - ds->gapsz - 1, SOLN_W, 2, COL_FRAME);
      draw_update(dr, 0, 0, ds->w, ds->h);
    }

    if (ds->drag_col != 0) {
        debug(("Loading from blitter."));
        blitter_load(dr, ds->blit_peg, ds->blit_ox, ds->blit_oy);
        draw_update(dr, ds->blit_ox, ds->blit_oy, PEGSZ, PEGSZ);
    }

    /* draw the colours */
    for (i = 0; i < state->params.ncolours; i++) {
        int val = i+1;
        if (ui->display_cur && ui->colour_cur == i)
            val |= 0x1000;
        if (ui->show_labels)
            val |= 0x2000;
        if (ds->colours->pegs[i] != val) {
	    draw_peg(dr, ds, COL_X(i), COL_Y(i), false, ui->show_labels, i+1);
            if (val & 0x1000)
                draw_cursor(dr, ds, COL_X(i), COL_Y(i));
            ds->colours->pegs[i] = val;
        }
    }

    /* draw the guesses (so far) and the hints
     * (in reverse order to avoid trampling holds, and postponing the
     * next_go'th to not overrender the top of the circular cursor) */
    for (i = state->params.nguesses - 1; i >= 0; i--) {
        if (i < state->next_go || state->solved) {
            /* this info is stored in the game_state already */
            guess_redraw(dr, ds, i, state->guesses[i], NULL, -1, false,
                         ui->show_labels);
            hint_redraw(dr, ds, i, state->guesses[i],
                        i == (state->next_go-1), false, false);
        } else if (i > state->next_go) {
            /* we've not got here yet; it's blank. */
            guess_redraw(dr, ds, i, NULL, NULL, -1, false, ui->show_labels);
            hint_redraw(dr, ds, i, NULL, false, false, false);
        }
    }
    if (!state->solved) {
	/* this is the one we're on; the (incomplete) guess is stored in
	 * the game_ui. */
	guess_redraw(dr, ds, state->next_go, ui->curr_pegs,
		     ui->holds, ui->display_cur ? ui->peg_cur : -1, false,
		     ui->show_labels);
	hint_redraw(dr, ds, state->next_go, NULL, true,
		    ui->display_cur && ui->peg_cur == state->params.npegs,
		    ui->markable);
    }

    /* draw the 'current move' and 'able to mark' sign. */
    if (new_move)
        currmove_redraw(dr, ds, ds->next_go, COL_BACKGROUND);
    if (!state->solved)
        currmove_redraw(dr, ds, state->next_go, COL_HOLD);

    /* draw the solution (or the big rectangle) */
    if ((!state->solved ^ !ds->solved) || !ds->started) {
        draw_rect(dr, SOLN_OX, SOLN_OY, SOLN_W, SOLN_H,
                  state->solved ? COL_BACKGROUND : COL_EMPTY);
        draw_update(dr, SOLN_OX, SOLN_OY, SOLN_W, SOLN_H);
    }
    if (state->solved)
        guess_redraw(dr, ds, -1, state->solution, NULL, -1, !ds->solved,
                     ui->show_labels);
    ds->solved = state->solved;

    ds->next_go = state->next_go;

    /* if ui->drag_col != 0, save the screen to the blitter,
     * draw the peg where we saved, and set ds->drag_* == ui->drag_*. */
    if (ui->drag_col != 0) {
        int ox = ui->drag_x - (PEGSZ/2);
        int oy = ui->drag_y - (PEGSZ/2);
        ds->blit_ox = ox - 1; ds->blit_oy = oy - 1;
        debug(("Saving to blitter at (%d,%d)", ds->blit_ox, ds->blit_oy));
        blitter_save(dr, ds->blit_peg, ds->blit_ox, ds->blit_oy);
        draw_peg(dr, ds, ox, oy, true, ui->show_labels, ui->drag_col);
    }
    ds->drag_col = ui->drag_col;

    ds->started = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->display_cur && !state->solved) {
        *x = GUESS_X(state->next_go, ui->peg_cur) - CGAP;
        *y = GUESS_Y(state->next_go, ui->peg_cur) - CGAP;

        *w = *h = 2 * (PEGRAD + CGAP) + 1;
    }
}

static int game_status(const game_state *state)
{
    /*
     * We return nonzero whenever the solution has been revealed, even
     * (on spoiler grounds) if it wasn't guessed correctly. The
     * correct return value from this function is already in
     * state->solved.
     */
    return state->solved;
}

#ifdef COMBINED
#define thegame guess
#endif

const struct game thegame = {
    "Guess", "games.guess", "guess",
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
    PEG_PREFER_SZ, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, NULL, NULL,          /* print_size, print */
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};

/* vim: set shiftwidth=4 tabstop=8: */
