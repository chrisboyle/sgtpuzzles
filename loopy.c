/*
 * loopy.c: An implementation of the Nikoli game 'Loop the loop'.
 * (c) Mike Pinna, 2005, 2006
 *
 * vim: set shiftwidth=4 :set textwidth=80:
 */ 

/*
 * TODO:
 *
 *  - Setting very high recursion depth seems to cause memory munching: are we
 *    recursing before checking completion, by any chance?
 *
 *  - There's an interesting deductive technique which makes use of topology
 *    rather than just graph theory. Each _square_ in the grid is either inside
 *    or outside the loop; you can tell that two squares are on the same side
 *    of the loop if they're separated by an x (or, more generally, by a path
 *    crossing no LINE_UNKNOWNs and an even number of LINE_YESes), and on the
 *    opposite side of the loop if they're separated by a line (or an odd
 *    number of LINE_YESes and no LINE_UNKNOWNs). Oh, and any square separated
 *    from the outside of the grid by a LINE_YES or a LINE_NO is on the inside
 *    or outside respectively. So if you can track this for all squares, you
 *    figure out the state of the line between a pair once their relative
 *    insideness is known.
 *
 *  - (Just a speed optimisation.)  Consider some todo list queue where every
 *    time we modify something we mark it for consideration by other bits of
 *    the solver, to save iteration over things that have already been done.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

/* Debugging options */
/*#define DEBUG_CACHES*/
/*#define SHOW_WORKING*/

/* ----------------------------------------------------------------------
 * Struct, enum and function declarations
 */

enum {
    COL_BACKGROUND,
    COL_FOREGROUND,
    COL_HIGHLIGHT,
    COL_MISTAKE,
    NCOLOURS
};

struct game_state {
    int w, h;
    
    /* Put -1 in a square that doesn't get a clue */
    char *clues;
    
    /* Arrays of line states, stored left-to-right, top-to-bottom */
    char *hl, *vl;

    int solved;
    int cheated;

    int recursion_depth;
};

enum solver_status {
    SOLVER_SOLVED,    /* This is the only solution the solver could find */
    SOLVER_MISTAKE,   /* This is definitely not a solution */
    SOLVER_AMBIGUOUS, /* This _might_ be an ambiguous solution */
    SOLVER_INCOMPLETE /* This may be a partial solution */
};

typedef struct normal {
    char *dot_atleastone;
    char *dot_atmostone;
} normal_mode_state;

typedef struct hard {
    int *linedsf;
} hard_mode_state;

typedef struct solver_state {
    game_state *state;
    int recursion_remaining;
    enum solver_status solver_status;
    /* NB looplen is the number of dots that are joined together at a point, ie a
     * looplen of 1 means there are no lines to a particular dot */
    int *looplen;

    /* caches */
    char *dot_yescount;
    char *dot_nocount;
    char *square_yescount;
    char *square_nocount;
    char *dot_solved, *square_solved;
    int *dotdsf;

    normal_mode_state *normal;
    hard_mode_state *hard;
} solver_state;

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */

#define DIFFLIST(A) \
    A(EASY,Easy,e,easy_mode_deductions) \
    A(NORMAL,Normal,n,normal_mode_deductions) \
    A(HARD,Hard,h,hard_mode_deductions)
#define ENUM(upper,title,lower,fn) DIFF_ ## upper,
#define TITLE(upper,title,lower,fn) #title,
#define ENCODE(upper,title,lower,fn) #lower
#define CONFIG(upper,title,lower,fn) ":" #title
#define SOLVER_FN_DECL(upper,title,lower,fn) static int fn(solver_state *);
#define SOLVER_FN(upper,title,lower,fn) &fn,
enum { DIFFLIST(ENUM) DIFF_MAX };
static char const *const diffnames[] = { DIFFLIST(TITLE) };
static char const diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)
DIFFLIST(SOLVER_FN_DECL);
static int (*(solver_fns[]))(solver_state *) = { DIFFLIST(SOLVER_FN) };

struct game_params {
    int w, h;
    int diff;
    int rec;
};

enum line_state { LINE_YES, LINE_UNKNOWN, LINE_NO };

#define OPP(state) \
    (2 - state)

enum direction { UP, LEFT, RIGHT, DOWN };

#define OPP_DIR(dir) \
    (3 - dir) 

struct game_drawstate {
    int started;
    int tilesize, linewidth;
    int flashing;
    char *hl, *vl;
    char *clue_error;
};

static char *game_text_format(game_state *state);
static char *state_to_text(const game_state *state);
static char *validate_desc(game_params *params, char *desc);
static int get_line_status_from_point(const game_state *state,
                                      int x, int y, enum direction d);
static int dot_order(const game_state* state, int i, int j, char line_type);
static int square_order(const game_state* state, int i, int j, char line_type);
static solver_state *solve_game_rec(const solver_state *sstate,
                                    int diff);

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate);
#else
#define check_caches(s)
#endif

/* ----------------------------------------------------------------------
 * Preprocessor magic 
 */

/* General constants */
#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define LINEWIDTH (ds->linewidth)
#define BORDER (TILE_SIZE / 2)
#define FLASH_TIME 0.5F

/* Counts of various things that we're interested in */
#define HL_COUNT(state) ((state)->w * ((state)->h + 1))
#define VL_COUNT(state) (((state)->w + 1) * (state)->h)
#define LINE_COUNT(state) (HL_COUNT(state) + VL_COUNT(state))
#define DOT_COUNT(state) (((state)->w + 1) * ((state)->h + 1))
#define SQUARE_COUNT(state) ((state)->w * (state)->h)

/* For indexing into arrays */
#define DOT_INDEX(state, x, y) ((x) + ((state)->w + 1) * (y))
#define SQUARE_INDEX(state, x, y) ((x) + ((state)->w) * (y))
#define HL_INDEX(state, x, y) SQUARE_INDEX(state, x, y)
#define VL_INDEX(state, x, y) DOT_INDEX(state, x, y)

/* Useful utility functions */
#define LEGAL_DOT(state, i, j) ((i) >= 0 && (j) >= 0 && \
                                (i) <= (state)->w && (j) <= (state)->h)
#define LEGAL_SQUARE(state, i, j) ((i) >= 0 && (j) >= 0 && \
                                   (i) < (state)->w && (j) < (state)->h)

#define CLUE_AT(state, i, j) (LEGAL_SQUARE(state, i, j) ? \
                              LV_CLUE_AT(state, i, j) : -1)
                             
#define LV_CLUE_AT(state, i, j) ((state)->clues[SQUARE_INDEX(state, i, j)])

#define BIT_SET(field, bit) ((field) & (1<<(bit)))

#define SET_BIT(field, bit)  (BIT_SET(field, bit) ? FALSE : \
                              ((field) |= (1<<(bit)), TRUE))

#define CLEAR_BIT(field, bit) (BIT_SET(field, bit) ? \
                               ((field) &= ~(1<<(bit)), TRUE) : FALSE)

#define DIR2STR(d) \
    ((d == UP) ? "up" : \
     (d == DOWN) ? "down" : \
     (d == LEFT) ? "left" : \
     (d == RIGHT) ? "right" : "oops")

#define CLUE2CHAR(c) \
    ((c < 0) ? ' ' : c + '0')

/* Lines that have particular relationships with given dots or squares */
#define ABOVE_SQUARE(state, i, j) ((state)->hl[(i) + (state)->w * (j)])
#define BELOW_SQUARE(state, i, j) ABOVE_SQUARE(state, i, (j)+1)
#define LEFTOF_SQUARE(state, i, j)  ((state)->vl[(i) + ((state)->w + 1) * (j)])
#define RIGHTOF_SQUARE(state, i, j) LEFTOF_SQUARE(state, (i)+1, j)

/*
 * These macros return rvalues only, but can cope with being passed
 * out-of-range coordinates.
 */
/* XXX replace these with functions so we can create an array of function
 * pointers for nicer iteration over them.  This could probably be done with
 * loads of other things for eliminating many nasty hacks. */
#define ABOVE_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || j <= 0) ? \
                                LINE_NO : LV_ABOVE_DOT(state, i, j))
#define BELOW_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || j >= (state)->h) ? \
                                LINE_NO : LV_BELOW_DOT(state, i, j))

#define LEFTOF_DOT(state, i, j)  ((!LEGAL_DOT(state, i, j) || i <= 0) ? \
                                  LINE_NO : LV_LEFTOF_DOT(state, i, j))
#define RIGHTOF_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || i >= (state)->w)? \
                                  LINE_NO : LV_RIGHTOF_DOT(state, i, j))

/*
 * These macros expect to be passed valid coordinates, and return
 * lvalues.
 */
#define LV_BELOW_DOT(state, i, j) ((state)->vl[VL_INDEX(state, i, j)])
#define LV_ABOVE_DOT(state, i, j) LV_BELOW_DOT(state, i, (j)-1)

#define LV_RIGHTOF_DOT(state, i, j) ((state)->hl[HL_INDEX(state, i, j)])
#define LV_LEFTOF_DOT(state, i, j)  LV_RIGHTOF_DOT(state, (i)-1, j)

/* Counts of interesting things */
#define DOT_YES_COUNT(sstate, i, j) \
    ((sstate)->dot_yescount[DOT_INDEX((sstate)->state, i, j)])

#define DOT_NO_COUNT(sstate, i, j) \
    ((sstate)->dot_nocount[DOT_INDEX((sstate)->state, i, j)])

#define SQUARE_YES_COUNT(sstate, i, j) \
    ((sstate)->square_yescount[SQUARE_INDEX((sstate)->state, i, j)])

#define SQUARE_NO_COUNT(sstate, i, j) \
    ((sstate)->square_nocount[SQUARE_INDEX((sstate)->state, i, j)])

/* Iterators.  NB these iterate over height more slowly than over width so that
 * the elements come out in 'reading' order */
/* XXX considering adding a 'current' element to each of these which gets the
 * address of the current dot, say.  But expecting we'd need more than that
 * most of the time.  */
#define FORALL(i, j, w, h) \
    for ((j) = 0; (j) < (h); ++(j)) \
        for ((i) = 0; (i) < (w); ++(i))

#define FORALL_DOTS(state, i, j) \
    FORALL(i, j, (state)->w + 1, (state)->h + 1)

#define FORALL_SQUARES(state, i, j) \
    FORALL(i, j, (state)->w, (state)->h)

#define FORALL_HL(state, i, j) \
    FORALL(i, j, (state)->w, (state)->h+1)

#define FORALL_VL(state, i, j) \
    FORALL(i, j, (state)->w+1, (state)->h)

/* ----------------------------------------------------------------------
 * General struct manipulation and other straightforward code
 */

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->h = state->h;
    ret->w = state->w;
    ret->solved = state->solved;
    ret->cheated = state->cheated;

    ret->clues = snewn(SQUARE_COUNT(state), char);
    memcpy(ret->clues, state->clues, SQUARE_COUNT(state));

    ret->hl = snewn(HL_COUNT(state), char);
    memcpy(ret->hl, state->hl, HL_COUNT(state));

    ret->vl = snewn(VL_COUNT(state), char);
    memcpy(ret->vl, state->vl, VL_COUNT(state));

    ret->recursion_depth = state->recursion_depth;

    return ret;
}

static void free_game(game_state *state)
{
    if (state) {
        sfree(state->clues);
        sfree(state->hl);
        sfree(state->vl);
        sfree(state);
    }
}

static solver_state *new_solver_state(const game_state *state, int diff) {
    int i, j;
    solver_state *ret = snew(solver_state);

    ret->state = dup_game((game_state *)state);
    
    ret->recursion_remaining = state->recursion_depth;
    ret->solver_status = SOLVER_INCOMPLETE; 

    ret->dotdsf = snew_dsf(DOT_COUNT(state));
    ret->looplen = snewn(DOT_COUNT(state), int);

    for (i = 0; i < DOT_COUNT(state); i++) {
        ret->looplen[i] = 1;
    }

    ret->dot_solved = snewn(DOT_COUNT(state), char);
    ret->square_solved = snewn(SQUARE_COUNT(state), char);
    memset(ret->dot_solved, FALSE, DOT_COUNT(state));
    memset(ret->square_solved, FALSE, SQUARE_COUNT(state));

    ret->dot_yescount = snewn(DOT_COUNT(state), char);
    memset(ret->dot_yescount, 0, DOT_COUNT(state));
    ret->dot_nocount = snewn(DOT_COUNT(state), char);
    memset(ret->dot_nocount, 0, DOT_COUNT(state));
    ret->square_yescount = snewn(SQUARE_COUNT(state), char);
    memset(ret->square_yescount, 0, SQUARE_COUNT(state));
    ret->square_nocount = snewn(SQUARE_COUNT(state), char);
    memset(ret->square_nocount, 0, SQUARE_COUNT(state));

    /* dot_nocount needs special initialisation as we define lines coming off
     * dots on edges as fixed at NO */

    FORALL_DOTS(state, i, j) {
        if (i == 0 || i == state->w)
            ++ret->dot_nocount[DOT_INDEX(state, i, j)];
        if (j == 0 || j == state->h)
            ++ret->dot_nocount[DOT_INDEX(state, i, j)];
    }

    if (diff < DIFF_NORMAL) {
        ret->normal = NULL;
    } else {
        ret->normal = snew(normal_mode_state);

        ret->normal->dot_atmostone = snewn(DOT_COUNT(state), char);
        memset(ret->normal->dot_atmostone, 0, DOT_COUNT(state));
        ret->normal->dot_atleastone = snewn(DOT_COUNT(state), char);
        memset(ret->normal->dot_atleastone, 0, DOT_COUNT(state));
    }

    if (diff < DIFF_HARD) {
        ret->hard = NULL;
    } else {
        ret->hard = snew(hard_mode_state);
        ret->hard->linedsf = snew_dsf(LINE_COUNT(state));
    }

    return ret;
}

static void free_solver_state(solver_state *sstate) {
    if (sstate) {
        free_game(sstate->state);
        sfree(sstate->dotdsf);
        sfree(sstate->looplen);
        sfree(sstate->dot_solved);
        sfree(sstate->square_solved);
        sfree(sstate->dot_yescount);
        sfree(sstate->dot_nocount);
        sfree(sstate->square_yescount);
        sfree(sstate->square_nocount);

        if (sstate->normal) {
            sfree(sstate->normal->dot_atleastone);
            sfree(sstate->normal->dot_atmostone);
            sfree(sstate->normal);
        }

        if (sstate->hard) {
            sfree(sstate->hard->linedsf);
            sfree(sstate->hard);
        }

        sfree(sstate);
    }
}

static solver_state *dup_solver_state(const solver_state *sstate) {
    game_state *state;

    solver_state *ret = snew(solver_state);

    ret->state = state = dup_game(sstate->state);

    ret->recursion_remaining = sstate->recursion_remaining;
    ret->solver_status = sstate->solver_status;

    ret->dotdsf = snewn(DOT_COUNT(state), int);
    ret->looplen = snewn(DOT_COUNT(state), int);
    memcpy(ret->dotdsf, sstate->dotdsf, 
           DOT_COUNT(state) * sizeof(int));
    memcpy(ret->looplen, sstate->looplen, 
           DOT_COUNT(state) * sizeof(int));

    ret->dot_solved = snewn(DOT_COUNT(state), char);
    ret->square_solved = snewn(SQUARE_COUNT(state), char);
    memcpy(ret->dot_solved, sstate->dot_solved, 
           DOT_COUNT(state));
    memcpy(ret->square_solved, sstate->square_solved, 
           SQUARE_COUNT(state));

    ret->dot_yescount = snewn(DOT_COUNT(state), char);
    memcpy(ret->dot_yescount, sstate->dot_yescount,
           DOT_COUNT(state));
    ret->dot_nocount = snewn(DOT_COUNT(state), char);
    memcpy(ret->dot_nocount, sstate->dot_nocount,
           DOT_COUNT(state));

    ret->square_yescount = snewn(SQUARE_COUNT(state), char);
    memcpy(ret->square_yescount, sstate->square_yescount,
           SQUARE_COUNT(state));
    ret->square_nocount = snewn(SQUARE_COUNT(state), char);
    memcpy(ret->square_nocount, sstate->square_nocount,
           SQUARE_COUNT(state));

    if (sstate->normal) {
        ret->normal = snew(normal_mode_state);
        ret->normal->dot_atmostone = snewn(DOT_COUNT(state), char);
        memcpy(ret->normal->dot_atmostone, sstate->normal->dot_atmostone,
               DOT_COUNT(state));

        ret->normal->dot_atleastone = snewn(DOT_COUNT(state), char);
        memcpy(ret->normal->dot_atleastone, sstate->normal->dot_atleastone,
               DOT_COUNT(state));
    } else {
        ret->normal = NULL;
    }

    if (sstate->hard) {
        ret->hard = snew(hard_mode_state);
        ret->hard->linedsf = snewn(LINE_COUNT(state), int);
        memcpy(ret->hard->linedsf, sstate->hard->linedsf, 
               LINE_COUNT(state) * sizeof(int));
    } else {
        ret->hard = NULL;
    }

    return ret;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

#ifdef SLOW_SYSTEM
    ret->h = 4;
    ret->w = 4;
#else
    ret->h = 10;
    ret->w = 10;
#endif
    ret->diff = DIFF_EASY;
    ret->rec = 0;

    return ret;
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;                       /* structure copy */
    return ret;
}

static const game_params presets[] = {
    {  4,  4, DIFF_EASY, 0 },
    {  4,  4, DIFF_NORMAL, 0 },
    {  4,  4, DIFF_HARD, 0 },
    {  7,  7, DIFF_EASY, 0 },
    {  7,  7, DIFF_NORMAL, 0 },
    {  7,  7, DIFF_HARD, 0 },
    { 10, 10, DIFF_EASY, 0 },
    { 10, 10, DIFF_NORMAL, 0 },
    { 10, 10, DIFF_HARD, 0 },
#ifndef SLOW_SYSTEM
    { 15, 15, DIFF_EASY, 0 },
    { 15, 15, DIFF_NORMAL, 0 },
    { 15, 15, DIFF_HARD, 0 },
    { 30, 20, DIFF_EASY, 0 },
    { 30, 20, DIFF_NORMAL, 0 },
    { 30, 20, DIFF_HARD, 0 }
#endif
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *tmppar;
    char buf[80];

    if (i < 0 || i >= lenof(presets))
        return FALSE;

    tmppar = snew(game_params);
    *tmppar = presets[i];
    *params = tmppar;
    sprintf(buf, "%dx%d %s", tmppar->h, tmppar->w, diffnames[tmppar->diff]);
    *name = dupstr(buf);

    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static void decode_params(game_params *params, char const *string)
{
    params->h = params->w = atoi(string);
    params->rec = 0;
    params->diff = DIFF_EASY;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'r') {
        string++;
        params->rec = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
        int i;
        string++;
        for (i = 0; i < DIFF_MAX; i++)
            if (*string == diffchars[i])
                params->diff = i;
        if (*string) string++;
    }
}

static char *encode_params(game_params *params, int full)
{
    char str[80];
    sprintf(str, "%dx%d", params->w, params->h);
    if (full)
    sprintf(str + strlen(str), "r%dd%c", params->rec, diffchars[params->diff]);
    return dupstr(str);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

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

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].sval = DIFFCONFIG;
    ret[2].ival = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->rec = 0;
    ret->diff = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 4 || params->h < 4)
        return "Width and height must both be at least 4";
    if (params->rec < 0)
        return "Recursion depth can't be negative";

    /*
     * This shouldn't be able to happen at all, since decode_params
     * and custom_params will never generate anything that isn't
     * within range.
     */
    assert(params->diff < DIFF_MAX);

    return NULL;
}

/* Returns a newly allocated string describing the current puzzle */
static char *state_to_text(const game_state *state)
{
    char *retval;
    char *description = snewn(SQUARE_COUNT(state) + 1, char);
    char *dp = description;
    int empty_count = 0;
    int i, j;

    FORALL_SQUARES(state, i, j) {
        if (CLUE_AT(state, i, j) < 0) {
            if (empty_count > 25) {
                dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));
                empty_count = 0;
            }
            empty_count++;
        } else {
            if (empty_count) {
                dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));
                empty_count = 0;
            }
            dp += sprintf(dp, "%c", (int)CLUE2CHAR(CLUE_AT(state, i, j)));
        }
    }

    if (empty_count)
        dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));

    retval = dupstr(description);
    sfree(description);

    return retval;
}

/* We require that the params pass the test in validate_params and that the
 * description fills the entire game area */
static char *validate_desc(game_params *params, char *desc)
{
    int count = 0;

    for (; *desc; ++desc) {
        if (*desc >= '0' && *desc <= '9') {
            count++;
            continue;
        }
        if (*desc >= 'a') {
            count += *desc - 'a' + 1;
            continue;
        }
        return "Unknown character in description";
    }

    if (count < SQUARE_COUNT(params))
        return "Description too short for board size";
    if (count > SQUARE_COUNT(params))
        return "Description too long for board size";

    return NULL;
}

/* Sums the lengths of the numbers in range [0,n) */
/* See equivalent function in solo.c for justification of this. */
static int len_0_to_n(int n)
{
    int len = 1; /* Counting 0 as a bit of a special case */
    int i;

    for (i = 1; i < n; i *= 10) {
        len += max(n - i, 0);
    }

    return len;
}

static char *encode_solve_move(const game_state *state)
{
    int len, i, j;
    char *ret, *p;
    /* This is going to return a string representing the moves needed to set
     * every line in a grid to be the same as the ones in 'state'.  The exact
     * length of this string is predictable. */

    len = 1;  /* Count the 'S' prefix */
    /* Numbers in horizontal lines */
    /* Horizontal lines, x position */
    len += len_0_to_n(state->w) * (state->h + 1);
    /* Horizontal lines, y position */
    len += len_0_to_n(state->h + 1) * (state->w);
    /* Vertical lines, y position */
    len += len_0_to_n(state->h) * (state->w + 1);
    /* Vertical lines, x position */
    len += len_0_to_n(state->w + 1) * (state->h);
    /* For each line we also have two letters and a comma */
    len += 3 * (LINE_COUNT(state));

    ret = snewn(len + 1, char);
    p = ret;

    p += sprintf(p, "S");

    FORALL_HL(state, i, j) {
        switch (RIGHTOF_DOT(state, i, j)) {
            case LINE_YES:
                p += sprintf(p, "%d,%dhy", i, j);
                break;
            case LINE_NO:
                p += sprintf(p, "%d,%dhn", i, j);
                break;
        }
    }

    FORALL_VL(state, i, j) {
        switch (BELOW_DOT(state, i, j)) {
            case LINE_YES:
                p += sprintf(p, "%d,%dvy", i, j);
                break;
            case LINE_NO:
                p += sprintf(p, "%d,%dvn", i, j);
                break;
        }
    }

    /* No point in doing sums like that if they're going to be wrong */
    assert(strlen(ret) <= (size_t)len);
    return ret;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

#define SIZE(d) ((d) * TILE_SIZE + 2 * BORDER + 1)

static void game_compute_size(game_params *params, int tilesize,
                              int *x, int *y)
{
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = SIZE(params->w);
    *y = SIZE(params->h);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
              game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->linewidth = max(1,tilesize/16);
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(4 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_FOREGROUND * 3 + 0] = 0.0F;
    ret[COL_FOREGROUND * 3 + 1] = 0.0F;
    ret[COL_FOREGROUND * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_MISTAKE * 3 + 0] = 1.0F;
    ret[COL_MISTAKE * 3 + 1] = 0.0F;
    ret[COL_MISTAKE * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = ds->linewidth = 0;
    ds->started = 0;
    ds->hl = snewn(HL_COUNT(state), char);
    ds->vl = snewn(VL_COUNT(state), char);
    ds->clue_error = snewn(SQUARE_COUNT(state), char);
    ds->flashing = 0;

    memset(ds->hl, LINE_UNKNOWN, HL_COUNT(state));
    memset(ds->vl, LINE_UNKNOWN, VL_COUNT(state));
    memset(ds->clue_error, 0, SQUARE_COUNT(state));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->clue_error);
    sfree(ds->hl);
    sfree(ds->vl);
    sfree(ds);
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
}

static char *game_text_format(game_state *state)
{
    int i, j;
    int len;
    char *ret, *rp;

    len = (2 * state->w + 2) * (2 * state->h + 1);
    rp = ret = snewn(len + 1, char);
    
#define DRAW_HL \
    switch (ABOVE_SQUARE(state, i, j)) { \
        case LINE_YES: \
            rp += sprintf(rp, " -"); \
            break; \
        case LINE_NO: \
            rp += sprintf(rp, " x"); \
            break; \
        case LINE_UNKNOWN: \
            rp += sprintf(rp, "  "); \
            break; \
        default: \
            assert(!"Illegal line state for HL"); \
    }

#define DRAW_VL \
    switch (LEFTOF_SQUARE(state, i, j)) { \
        case LINE_YES: \
            rp += sprintf(rp, "|"); \
            break; \
        case LINE_NO: \
            rp += sprintf(rp, "x"); \
            break; \
        case LINE_UNKNOWN: \
            rp += sprintf(rp, " "); \
            break; \
        default: \
            assert(!"Illegal line state for VL"); \
    }
    
    for (j = 0; j < state->h; ++j) {
        for (i = 0; i < state->w; ++i) {
            DRAW_HL;
        }
        rp += sprintf(rp, " \n");
        for (i = 0; i < state->w; ++i) {
            DRAW_VL;
            rp += sprintf(rp, "%c", (int)CLUE2CHAR(CLUE_AT(state, i, j)));
        }
        DRAW_VL;
        rp += sprintf(rp, "\n");
    }
    for (i = 0; i < state->w; ++i) {
        DRAW_HL;
    }
    rp += sprintf(rp, " \n");
    
    assert(strlen(ret) == len);
    return ret;
}

/* ----------------------------------------------------------------------
 * Debug code
 */

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate)
{
    int i, j;
    const game_state *state = sstate->state;

    FORALL_DOTS(state, i, j) {
#if 0
        fprintf(stderr, "dot [%d,%d] y: %d %d n: %d %d\n", i, j,
               dot_order(state, i, j, LINE_YES),
               sstate->dot_yescount[i + (state->w + 1) * j],
               dot_order(state, i, j, LINE_NO),
               sstate->dot_nocount[i + (state->w + 1) * j]);
#endif
                    
        assert(dot_order(state, i, j, LINE_YES) ==
               DOT_YES_COUNT(sstate, i, j));
        assert(dot_order(state, i, j, LINE_NO) ==
               DOT_NO_COUNT(sstate, i, j));
    }

    FORALL_SQUARES(state, i, j) {
#if 0
        fprintf(stderr, "square [%d,%d] y: %d %d n: %d %d\n", i, j,
               square_order(state, i, j, LINE_YES),
               sstate->square_yescount[i + state->w * j],
               square_order(state, i, j, LINE_NO),
               sstate->square_nocount[i + state->w * j]);
#endif
                    
        assert(square_order(state, i, j, LINE_YES) ==
               SQUARE_YES_COUNT(sstate, i, j));
        assert(square_order(state, i, j, LINE_NO) ==
               SQUARE_NO_COUNT(sstate, i, j));
    }
}

#if 0
#define check_caches(s) \
    do { \
        fprintf(stderr, "check_caches at line %d\n", __LINE__); \
        check_caches(s); \
    } while (0)
#endif
#endif /* DEBUG_CACHES */

/* ----------------------------------------------------------------------
 * Solver utility functions
 */

static int set_line_bydot(solver_state *sstate, int x, int y, enum direction d,
                          enum line_state line_new
#ifdef SHOW_WORKING
                          , const char *reason
#endif
                          ) 
{
    game_state *state = sstate->state;

    /* This line borders at most two squares in our board.  We figure out the
     * x and y positions of those squares so we can record that their yes or no
     * counts have been changed */
    int sq1_x=-1, sq1_y=-1, sq2_x=-1, sq2_y=-1;
    int otherdot_x=-1, otherdot_y=-1;

    int progress = FALSE;

#if 0
    fprintf(stderr, "set_line_bydot [%d,%d], %s, %d\n",
            x, y, DIR2STR(d), line_new);
#endif

    assert(line_new != LINE_UNKNOWN);

    check_caches(sstate);

    switch (d) {
        case LEFT:
            assert(x > 0);

            if (LEFTOF_DOT(state, x, y) != line_new) {
                LV_LEFTOF_DOT(state, x, y) = line_new;

                otherdot_x = x-1;
                otherdot_y = y;

                sq1_x = x-1;
                sq1_y = y-1;
                sq2_x = x-1;
                sq2_y = y;

                progress = TRUE;
            }
            break;
        case RIGHT:
            assert(x < state->w);
            if (RIGHTOF_DOT(state, x, y) != line_new) {
                LV_RIGHTOF_DOT(state, x, y) = line_new;

                otherdot_x = x+1;
                otherdot_y = y;

                sq1_x = x;
                sq1_y = y-1;
                sq2_x = x;
                sq2_y = y;

                progress = TRUE;
            }
            break;
        case UP:
            assert(y > 0);
            if (ABOVE_DOT(state, x, y) != line_new) {
                LV_ABOVE_DOT(state, x, y) = line_new;

                otherdot_x = x;
                otherdot_y = y-1;

                sq1_x = x-1;
                sq1_y = y-1;
                sq2_x = x;
                sq2_y = y-1;

                progress = TRUE;
            }
            break;
        case DOWN:
            assert(y < state->h);
            if (BELOW_DOT(state, x, y) != line_new) {
                LV_BELOW_DOT(state, x, y) = line_new;

                otherdot_x = x;
                otherdot_y = y+1;

                sq1_x = x-1;
                sq1_y = y;
                sq2_x = x;
                sq2_y = y;

                progress = TRUE;
            }
            break;
    }

    if (!progress)
        return progress;

#ifdef SHOW_WORKING
    fprintf(stderr, "set line [%d,%d] -> [%d,%d] to %s (%s)\n",
            x, y, otherdot_x, otherdot_y, line_new == LINE_YES ? "YES" : "NO",
            reason);
#endif

    /* Above we updated the cache for the dot that the line in question reaches
     * from the dot we've been told about.  Here we update that for the dot
     * named in our arguments. */
    if (line_new == LINE_YES) {
        if (sq1_x >= 0 && sq1_y >= 0)
            ++SQUARE_YES_COUNT(sstate, sq1_x, sq1_y);
        if (sq2_x < state->w && sq2_y < state->h)
            ++SQUARE_YES_COUNT(sstate, sq2_x, sq2_y);
        ++DOT_YES_COUNT(sstate, x, y);
        ++DOT_YES_COUNT(sstate, otherdot_x, otherdot_y);
    } else {
        if (sq1_x >= 0 && sq1_y >= 0)
            ++SQUARE_NO_COUNT(sstate, sq1_x, sq1_y);
        if (sq2_x < state->w && sq2_y < state->h)
            ++SQUARE_NO_COUNT(sstate, sq2_x, sq2_y);
        ++DOT_NO_COUNT(sstate, x, y);
        ++DOT_NO_COUNT(sstate, otherdot_x, otherdot_y);
    }
    
    check_caches(sstate);
    return progress;
}

#ifdef SHOW_WORKING
#define set_line_bydot(a, b, c, d, e) \
    set_line_bydot(a, b, c, d, e, __FUNCTION__)
#endif

/*
 * Merge two dots due to the existence of an edge between them.
 * Updates the dsf tracking equivalence classes, and keeps track of
 * the length of path each dot is currently a part of.
 * Returns TRUE if the dots were already linked, ie if they are part of a
 * closed loop, and false otherwise.
 */
static int merge_dots(solver_state *sstate, int x1, int y1, int x2, int y2)
{
    int i, j, len;

    i = y1 * (sstate->state->w + 1) + x1;
    j = y2 * (sstate->state->w + 1) + x2;

    i = dsf_canonify(sstate->dotdsf, i);
    j = dsf_canonify(sstate->dotdsf, j);

    if (i == j) {
        return TRUE;
    } else {
        len = sstate->looplen[i] + sstate->looplen[j];
        dsf_merge(sstate->dotdsf, i, j);
        i = dsf_canonify(sstate->dotdsf, i);
        sstate->looplen[i] = len;
        return FALSE;
    }
}

/* Seriously, these should be functions */

#define LINEDSF_INDEX(state, x, y, d) \
   ((d == UP)    ? ((y-1) * (state->w + 1) + x) : \
    (d == DOWN)  ? ((y)   * (state->w + 1) + x) : \
    (d == LEFT)  ? ((y) * (state->w) + x-1 + VL_COUNT(state)) : \
    (d == RIGHT) ? ((y) * (state->w) + x   + VL_COUNT(state)) : \
    (assert(!"bad direction value"), 0))

static void linedsf_deindex(const game_state *state, int i, 
                            int *px, int *py, enum direction *pd)
{
    int i_mod;
    if (i < VL_COUNT(state)) {
        *(pd) = DOWN;
        *(px) = (i) % (state->w+1);
        *(py) = (i) / (state->w+1);
    } else {
        i_mod = i - VL_COUNT(state);
        *(pd) = RIGHT;
        *(px) = (i_mod) % (state->w);
        *(py) = (i_mod) / (state->w);
    }
}

/* Merge two lines because the solver has deduced that they must be either
 * identical or opposite.   Returns TRUE if this is new information, otherwise
 * FALSE. */
static int merge_lines(solver_state *sstate, 
                       int x1, int y1, enum direction d1,
                       int x2, int y2, enum direction d2,
                       int inverse
#ifdef SHOW_WORKING
                       , const char *reason
#endif
                      )
{
    int i, j, inv_tmp;

    i = LINEDSF_INDEX(sstate->state, x1, y1, d1);
    j = LINEDSF_INDEX(sstate->state, x2, y2, d2);

    assert(i < LINE_COUNT(sstate->state));
    assert(j < LINE_COUNT(sstate->state));
    
    i = edsf_canonify(sstate->hard->linedsf, i, &inv_tmp);
    inverse ^= inv_tmp;
    j = edsf_canonify(sstate->hard->linedsf, j, &inv_tmp);
    inverse ^= inv_tmp;

    edsf_merge(sstate->hard->linedsf, i, j, inverse);

#ifdef SHOW_WORKING
    if (i != j) {
        fprintf(stderr, "%s [%d,%d,%s] [%d,%d,%s] %s(%s)\n",
                __FUNCTION__, 
                x1, y1, DIR2STR(d1),
                x2, y2, DIR2STR(d2),
                inverse ? "inverse " : "", reason);
    }
#endif
    return (i != j);
}

#ifdef SHOW_WORKING
#define merge_lines(a, b, c, d, e, f, g, h) \
    merge_lines(a, b, c, d, e, f, g, h, __FUNCTION__)
#endif

/* Return 0 if the given lines are not in the same equivalence class, 1 if they
 * are known identical, or 2 if they are known opposite */
#if 0
static int lines_related(solver_state *sstate,
                         int x1, int y1, enum direction d1, 
                         int x2, int y2, enum direction d2)
{
    int i, j, inv1, inv2;

    i = LINEDSF_INDEX(sstate->state, x1, y1, d1);
    j = LINEDSF_INDEX(sstate->state, x2, y2, d2);
  
    i = edsf_canonify(sstate->hard->linedsf, i, &inv1);
    j = edsf_canonify(sstate->hard->linedsf, j, &inv2);

    if (i == j)
        return (inv1 == inv2) ? 1 : 2;
    else
        return 0;
}
#endif

/* Count the number of lines of a particular type currently going into the
 * given dot.  Lines going off the edge of the board are assumed fixed no. */
static int dot_order(const game_state* state, int i, int j, char line_type)
{
    int n = 0;

    if (i > 0) {
        if (line_type == LV_LEFTOF_DOT(state, i, j))
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (i < state->w) {
        if (line_type == LV_RIGHTOF_DOT(state, i, j))
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (j > 0) {
        if (line_type == LV_ABOVE_DOT(state, i, j))
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (j < state->h) {
        if (line_type == LV_BELOW_DOT(state, i, j))
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }

    return n;
}

/* Count the number of lines of a particular type currently surrounding the
 * given square */
static int square_order(const game_state* state, int i, int j, char line_type)
{
    int n = 0;

    if (ABOVE_SQUARE(state, i, j) == line_type)
        ++n;
    if (BELOW_SQUARE(state, i, j) == line_type)
        ++n;
    if (LEFTOF_SQUARE(state, i, j) == line_type)
        ++n;
    if (RIGHTOF_SQUARE(state, i, j) == line_type)
        ++n;

    return n;
}

/* Set all lines bordering a dot of type old_type to type new_type 
 * Return value tells caller whether this function actually did anything */
static int dot_setall(solver_state *sstate, int i, int j,
                       char old_type, char new_type)
{
    int retval = FALSE, r;
    game_state *state = sstate->state;
    
    if (old_type == new_type)
        return FALSE;

    if (i > 0        && LEFTOF_DOT(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, LEFT, new_type);
        assert(r == TRUE);
        retval = TRUE;
    }

    if (i < state->w && RIGHTOF_DOT(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, RIGHT, new_type);
        assert(r == TRUE);
        retval = TRUE;
    }

    if (j > 0        && ABOVE_DOT(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, UP, new_type);
        assert(r == TRUE);
        retval = TRUE;
    }

    if (j < state->h && BELOW_DOT(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, DOWN, new_type);
        assert(r == TRUE);
        retval = TRUE;
    }

    return retval;
}

/* Set all lines bordering a square of type old_type to type new_type */
static int square_setall(solver_state *sstate, int i, int j,
                         char old_type, char new_type)
{
    int r = FALSE;
    game_state *state = sstate->state;

#if 0
    fprintf(stderr, "square_setall [%d,%d] from %d to %d\n", i, j,
                    old_type, new_type);
#endif
    if (ABOVE_SQUARE(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, RIGHT, new_type);
        assert(r == TRUE);
    }
    if (BELOW_SQUARE(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j+1, RIGHT, new_type);
        assert(r == TRUE);
    }
    if (LEFTOF_SQUARE(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i, j, DOWN, new_type);
        assert(r == TRUE);
    }
    if (RIGHTOF_SQUARE(state, i, j) == old_type) {
        r = set_line_bydot(sstate, i+1, j, DOWN, new_type);
        assert(r == TRUE);
    }

    return r;
}

/* ----------------------------------------------------------------------
 * Loop generation and clue removal
 */

/* We're going to store a list of current candidate squares for lighting.
 * Each square gets a 'score', which tells us how adding that square right
 * now would affect the length of the solution loop.  We're trying to
 * maximise that quantity so will bias our random selection of squares to
 * light towards those with high scores */
struct square { 
    int score;
    unsigned long random;
    int x, y;
};

static int get_square_cmpfn(void *v1, void *v2) 
{
    struct square *s1 = v1;
    struct square *s2 = v2;
    int r;
    
    r = s1->x - s2->x;
    if (r)
        return r;

    r = s1->y - s2->y;
    if (r)
        return r;

    return 0;
}

static int square_sort_cmpfn(void *v1, void *v2)
{
    struct square *s1 = v1;
    struct square *s2 = v2;
    int r;

    r = s2->score - s1->score;
    if (r) {
        return r;
    }

    if (s1->random < s2->random)
        return -1;
    else if (s1->random > s2->random)
        return 1;

    /*
     * It's _just_ possible that two squares might have been given
     * the same random value. In that situation, fall back to
     * comparing based on the coordinates. This introduces a tiny
     * directional bias, but not a significant one.
     */
    return get_square_cmpfn(v1, v2);
}

enum { SQUARE_LIT, SQUARE_UNLIT };

#define SQUARE_STATE(i, j) \
    ( LEGAL_SQUARE(state, i, j) ? \
        LV_SQUARE_STATE(i,j) : \
        SQUARE_UNLIT )

#define LV_SQUARE_STATE(i, j) board[SQUARE_INDEX(state, i, j)]

/* Generate a new complete set of clues for the given game_state (respecting
 * the dimensions provided by said game_state) */
static void add_full_clues(game_state *state, random_state *rs)
{
    char *clues;
    char *board;
    int i, j, a, b, c;
    int board_area = SQUARE_COUNT(state);
    int t;

    struct square *square, *tmpsquare, *sq;
    struct square square_pos;

    /* These will contain exactly the same information, sorted into different
     * orders */
    tree234 *lightable_squares_sorted, *lightable_squares_gettable;

#define SQUARE_REACHABLE(i,j) \
     (t = (SQUARE_STATE(i-1, j) == SQUARE_LIT || \
           SQUARE_STATE(i+1, j) == SQUARE_LIT || \
           SQUARE_STATE(i, j-1) == SQUARE_LIT || \
           SQUARE_STATE(i, j+1) == SQUARE_LIT), \
      t)

    /* One situation in which we may not light a square is if that'll leave one
     * square above/below and one left/right of us unlit, separated by a lit
     * square diagnonal from us */
#define SQUARE_DIAGONAL_VIOLATION(i, j, h, v) \
    (t = (SQUARE_STATE((i)+(h), (j))     == SQUARE_UNLIT && \
          SQUARE_STATE((i),     (j)+(v)) == SQUARE_UNLIT && \
          SQUARE_STATE((i)+(h), (j)+(v)) == SQUARE_LIT), \
     t)

    /* We also may not light a square if it will form a loop of lit squares
     * around some unlit squares, as then the game soln won't have a single
     * loop */
#define SQUARE_LOOP_VIOLATION(i, j, lit1, lit2) \
    (SQUARE_STATE((i)+1, (j)) == lit1    && \
     SQUARE_STATE((i)-1, (j)) == lit1    && \
     SQUARE_STATE((i), (j)+1) == lit2    && \
     SQUARE_STATE((i), (j)-1) == lit2)

#define CAN_LIGHT_SQUARE(i, j) \
    (SQUARE_REACHABLE(i, j)                                 && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, -1, -1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, +1, -1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, -1, +1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, +1, +1)               && \
     !SQUARE_LOOP_VIOLATION(i, j, SQUARE_LIT, SQUARE_UNLIT) && \
     !SQUARE_LOOP_VIOLATION(i, j, SQUARE_UNLIT, SQUARE_LIT))

#define IS_LIGHTING_CANDIDATE(i, j) \
    (SQUARE_STATE(i, j) == SQUARE_UNLIT && \
     CAN_LIGHT_SQUARE(i,j))

    /* The 'score' of a square reflects its current desirability for selection
     * as the next square to light.  We want to encourage moving into uncharted
     * areas so we give scores according to how many of the square's neighbours
     * are currently unlit. */

   /* UNLIT    SCORE
    *   3        2
    *   2        0
    *   1       -2
    */
#define SQUARE_SCORE(i,j) \
    (2*((SQUARE_STATE(i-1, j) == SQUARE_UNLIT)  + \
        (SQUARE_STATE(i+1, j) == SQUARE_UNLIT)  + \
        (SQUARE_STATE(i, j-1) == SQUARE_UNLIT)  + \
        (SQUARE_STATE(i, j+1) == SQUARE_UNLIT)) - 4)

    /* When a square gets lit, this defines how far away from that square we
     * need to go recomputing scores */
#define SCORE_DISTANCE 1

    board = snewn(board_area, char);
    clues = state->clues;

    /* Make a board */
    memset(board, SQUARE_UNLIT, board_area);
    
    /* Seed the board with a single lit square near the middle */
    i = state->w / 2;
    j = state->h / 2;
    if (state->w & 1 && random_bits(rs, 1))
        ++i;
    if (state->h & 1 && random_bits(rs, 1))
        ++j;

    LV_SQUARE_STATE(i, j) = SQUARE_LIT;

    /* We need a way of favouring squares that will increase our loopiness.
     * We do this by maintaining a list of all candidate squares sorted by
     * their score and choose randomly from that with appropriate skew. 
     * In order to avoid consistently biasing towards particular squares, we
     * need the sort order _within_ each group of scores to be completely
     * random.  But it would be abusing the hospitality of the tree234 data
     * structure if our comparison function were nondeterministic :-).  So with
     * each square we associate a random number that does not change during a
     * particular run of the generator, and use that as a secondary sort key.
     * Yes, this means we will be biased towards particular random squares in
     * any one run but that doesn't actually matter. */
    
    lightable_squares_sorted   = newtree234(square_sort_cmpfn);
    lightable_squares_gettable = newtree234(get_square_cmpfn);
#define ADD_SQUARE(s) \
    do { \
        sq = add234(lightable_squares_sorted, s); \
        assert(sq == s); \
        sq = add234(lightable_squares_gettable, s); \
        assert(sq == s); \
    } while (0)

#define REMOVE_SQUARE(s) \
    do { \
        sq = del234(lightable_squares_sorted, s); \
        assert(sq); \
        sq = del234(lightable_squares_gettable, s); \
        assert(sq); \
    } while (0)
        
#define HANDLE_DIR(a, b) \
    square = snew(struct square); \
    square->x = (i)+(a); \
    square->y = (j)+(b); \
    square->score = 2; \
    square->random = random_bits(rs, 31); \
    ADD_SQUARE(square);
    HANDLE_DIR(-1, 0);
    HANDLE_DIR( 1, 0);
    HANDLE_DIR( 0,-1);
    HANDLE_DIR( 0, 1);
#undef HANDLE_DIR
    
    /* Light squares one at a time until the board is interesting enough */
    while (TRUE)
    {
        /* We have count234(lightable_squares) possibilities, and in
         * lightable_squares_sorted they are sorted with the most desirable
         * first.  */
        c = count234(lightable_squares_sorted);
        if (c == 0)
            break;
        assert(c == count234(lightable_squares_gettable));

        /* Check that the best square available is any good */
        square = (struct square *)index234(lightable_squares_sorted, 0);
        assert(square);

        /*
         * We never want to _decrease_ the loop's perimeter. Making
         * moves that leave the perimeter the same is occasionally
         * useful: if it were _never_ done then the user would be
         * able to deduce illicitly that any degree-zero vertex was
         * on the outside of the loop. So we do it sometimes but
         * not always.
         */
        if (square->score < 0 || (square->score == 0 &&
                                  random_upto(rs, 2) == 0)) {
            break;
        }

        assert(square->score == SQUARE_SCORE(square->x, square->y));
        assert(SQUARE_STATE(square->x, square->y) == SQUARE_UNLIT);
        assert(square->x >= 0 && square->x < state->w);
        assert(square->y >= 0 && square->y < state->h);

        /* Update data structures */
        LV_SQUARE_STATE(square->x, square->y) = SQUARE_LIT;
        REMOVE_SQUARE(square);

        /* We might have changed the score of any squares up to 2 units away in
         * any direction */
        for (b = -SCORE_DISTANCE; b <= SCORE_DISTANCE; b++) {
            for (a = -SCORE_DISTANCE; a <= SCORE_DISTANCE; a++) {
                if (!a && !b) 
                    continue;
                square_pos.x = square->x + a;
                square_pos.y = square->y + b;
                if (square_pos.x < 0 || square_pos.x >= state->w ||
                    square_pos.y < 0 || square_pos.y >= state->h) {
                   continue; 
                }
                tmpsquare = find234(lightable_squares_gettable, &square_pos,
                                    NULL);
                if (tmpsquare) {
                    assert(tmpsquare->x == square_pos.x);
                    assert(tmpsquare->y == square_pos.y);
                    assert(SQUARE_STATE(tmpsquare->x, tmpsquare->y) == 
                           SQUARE_UNLIT);
                    REMOVE_SQUARE(tmpsquare);
                } else {
                    tmpsquare = snew(struct square);
                    tmpsquare->x = square_pos.x;
                    tmpsquare->y = square_pos.y;
                    tmpsquare->random = random_bits(rs, 31);
                }
                tmpsquare->score = SQUARE_SCORE(tmpsquare->x, tmpsquare->y);

                if (IS_LIGHTING_CANDIDATE(tmpsquare->x, tmpsquare->y)) {
                    ADD_SQUARE(tmpsquare);
                } else {
                    sfree(tmpsquare);
                }
            }
        }
        sfree(square);
    }

    /* Clean up */
    while ((square = delpos234(lightable_squares_gettable, 0)) != NULL)
        sfree(square);
    freetree234(lightable_squares_gettable);
    freetree234(lightable_squares_sorted);

    /* Copy out all the clues */
    FORALL_SQUARES(state, i, j) {
        c = SQUARE_STATE(i, j);
        LV_CLUE_AT(state, i, j) = 0;
        if (SQUARE_STATE(i-1, j) != c) ++LV_CLUE_AT(state, i, j);
        if (SQUARE_STATE(i+1, j) != c) ++LV_CLUE_AT(state, i, j);
        if (SQUARE_STATE(i, j-1) != c) ++LV_CLUE_AT(state, i, j);
        if (SQUARE_STATE(i, j+1) != c) ++LV_CLUE_AT(state, i, j);
    }

    sfree(board);
}

static int game_has_unique_soln(const game_state *state, int diff)
{
    int ret;
    solver_state *sstate_new;
    solver_state *sstate = new_solver_state((game_state *)state, diff);
    
    sstate_new = solve_game_rec(sstate, diff);

    assert(sstate_new->solver_status != SOLVER_MISTAKE);
    ret = (sstate_new->solver_status == SOLVER_SOLVED);

    free_solver_state(sstate_new);
    free_solver_state(sstate);

    return ret;
}

/* Remove clues one at a time at random. */
static game_state *remove_clues(game_state *state, random_state *rs, 
                                int diff)
{
    int *square_list, squares;
    game_state *ret = dup_game(state), *saved_ret;
    int n;
#ifdef SHOW_WORKING
    char *desc;
#endif

    /* We need to remove some clues.  We'll do this by forming a list of all
     * available clues, shuffling it, then going along one at a
     * time clearing each clue in turn for which doing so doesn't render the
     * board unsolvable. */
    squares = state->w * state->h;
    square_list = snewn(squares, int);
    for (n = 0; n < squares; ++n) {
        square_list[n] = n;
    }

    shuffle(square_list, squares, sizeof(int), rs);
    
    for (n = 0; n < squares; ++n) {
        saved_ret = dup_game(ret);
        LV_CLUE_AT(ret, square_list[n] % state->w,
                   square_list[n] / state->w) = -1;

#ifdef SHOW_WORKING
        desc = state_to_text(ret);
        fprintf(stderr, "%dx%d:%s\n", state->w, state->h, desc);
        sfree(desc);
#endif

        if (game_has_unique_soln(ret, diff)) {
            free_game(saved_ret);
        } else {
            free_game(ret);
            ret = saved_ret;
        }
    }
    sfree(square_list);

    return ret;
}

static char *new_game_desc(game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    /* solution and description both use run-length encoding in obvious ways */
    char *retval;
    game_state *state = snew(game_state), *state_new;

    state->h = params->h;
    state->w = params->w;

    state->clues = snewn(SQUARE_COUNT(params), char);
    state->hl = snewn(HL_COUNT(params), char);
    state->vl = snewn(VL_COUNT(params), char);

newboard_please:
    memset(state->hl, LINE_UNKNOWN, HL_COUNT(params));
    memset(state->vl, LINE_UNKNOWN, VL_COUNT(params));

    state->solved = state->cheated = FALSE;
    state->recursion_depth = params->rec;

    /* Get a new random solvable board with all its clues filled in.  Yes, this
     * can loop for ever if the params are suitably unfavourable, but
     * preventing games smaller than 4x4 seems to stop this happening */

    do {
        add_full_clues(state, rs);
    } while (!game_has_unique_soln(state, params->diff));

    state_new = remove_clues(state, rs, params->diff);
    free_game(state);
    state = state_new;

    if (params->diff > 0 && game_has_unique_soln(state, params->diff-1)) {
#ifdef SHOW_WORKING
        fprintf(stderr, "Rejecting board, it is too easy\n");
#endif
        goto newboard_please;
    }

    retval = state_to_text(state);

    free_game(state);
    
    assert(!validate_desc(params, retval));

    return retval;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int i,j;
    game_state *state = snew(game_state);
    int empties_to_make = 0;
    int n;
    const char *dp = desc;

    state->recursion_depth = 0; /* XXX pending removal, probably */
    
    state->h = params->h;
    state->w = params->w;

    state->clues = snewn(SQUARE_COUNT(params), char);
    state->hl = snewn(HL_COUNT(params), char);
    state->vl = snewn(VL_COUNT(params), char);

    state->solved = state->cheated = FALSE;

    FORALL_SQUARES(params, i, j) {
        if (empties_to_make) {
            empties_to_make--;
            LV_CLUE_AT(state, i, j) = -1;
            continue;
        }

        assert(*dp);
        n = *dp - '0';
        if (n >= 0 && n < 10) {
            LV_CLUE_AT(state, i, j) = n;
        } else {
            n = *dp - 'a' + 1;
            assert(n > 0);
            LV_CLUE_AT(state, i, j) = -1;
            empties_to_make = n - 1;
        }
        ++dp;
    }

    memset(state->hl, LINE_UNKNOWN, HL_COUNT(params));
    memset(state->vl, LINE_UNKNOWN, VL_COUNT(params));

    return state;
}

enum { LOOP_NONE=0, LOOP_SOLN, LOOP_NOT_SOLN };

/* ----------------------------------------------------------------------
 * Solver logic
 *
 * Our solver modes operate as follows.  Each mode also uses the modes above it.
 *
 *   Easy Mode
 *   Just implement the rules of the game.
 *
 *   Normal Mode
 *   For each pair of lines through each dot we store a bit for whether
 *   at least one of them is on and whether at most one is on.  (If we know
 *   both or neither is on that's already stored more directly.)  That's six
 *   bits per dot.  Bit number n represents the lines shown in dline_desc.
 *
 *   Advanced Mode
 *   Use edsf data structure to make equivalence classes of lines that are
 *   known identical to or opposite to one another.
 */

/* The order the following are defined in is very important, see below.
 * The last two fields may seem non-obvious: they specify that when talking
 * about a square the dx and dy offsets should be added to the square coords to
 * get to the right dot.  Where dx and dy are -1 this means that the dline
 * doesn't make sense for a square. */
/* XXX can this be done with a struct instead? */
#define DLINES \
    DLINE(DLINE_UD, UP,   DOWN,  -1, -1) \
    DLINE(DLINE_LR, LEFT, RIGHT, -1, -1) \
    DLINE(DLINE_UR, UP,   RIGHT,  0,  1) \
    DLINE(DLINE_DL, DOWN, LEFT,   1,  0) \
    DLINE(DLINE_UL, UP,   LEFT,   1,  1) \
    DLINE(DLINE_DR, DOWN, RIGHT,  0,  0)

#define OPP_DLINE(dline_desc) ((dline_desc) ^ 1)

enum dline_desc {
#define DLINE(desc, dir1, dir2, dx, dy) \
    desc,
    DLINES
#undef DLINE
};

struct dline {
    enum dline_desc desc;
    enum direction dir1, dir2;
    int dx, dy;
};

const static struct dline dlines[] =  {
#define DLINE(desc, dir1, dir2, dx, dy) \
    { desc, dir1, dir2, dx, dy },
    DLINES
#undef DLINE
};

#define FORALL_DOT_DLINES(dl_iter) \
    for (dl_iter = 0; dl_iter < lenof(dlines); ++dl_iter)

#define FORALL_SQUARE_DLINES(dl_iter) \
    for (dl_iter = 2; dl_iter < lenof(dlines); ++dl_iter)

#define DL2STR(d) \
    ((d==DLINE_UD) ? "DLINE_UD": \
     (d==DLINE_LR) ? "DLINE_LR": \
     (d==DLINE_UR) ? "DLINE_UR": \
     (d==DLINE_DL) ? "DLINE_DL": \
     (d==DLINE_UL) ? "DLINE_UL": \
     (d==DLINE_DR) ? "DLINE_DR": \
     "oops")

static const struct dline *get_dline(enum dline_desc desc)
{
    return &dlines[desc];
}

/* This will fail an assertion if the directions handed to it are the same, as
 * no dline corresponds to that */
static enum dline_desc dline_desc_from_dirs(enum direction dir1, 
                                            enum direction dir2)
{
    int i;

    assert (dir1 != dir2);

    for (i = 0; i < lenof(dlines); ++i) {
        if ((dir1 == dlines[i].dir1 && dir2 == dlines[i].dir2) ||
            (dir1 == dlines[i].dir2 && dir2 == dlines[i].dir1)) {
            return dlines[i].desc;
        }
    }

    assert(!"dline not found");
    return DLINE_UD; /* placate compiler */
}

/* The following functions allow you to get or set info about the selected
 * dline corresponding to the dot or square at [i,j].  You'll get an assertion
 * failure if you talk about a dline that doesn't exist, ie if you ask about
 * non-touching lines around a square. */
static int get_dot_dline(const game_state *state, const char *dline_array,
                         int i, int j, enum dline_desc desc)
{
/*    fprintf(stderr, "get_dot_dline %p [%d,%d] %s\n", dline_array, i, j, DL2STR(desc)); */
    return BIT_SET(dline_array[i + (state->w + 1) * j], desc);
}

static int set_dot_dline(game_state *state, char *dline_array,
                         int i, int j, enum dline_desc desc
#ifdef SHOW_WORKING
                         , const char *reason
#endif
                         )
{
    int ret;
    ret = SET_BIT(dline_array[i + (state->w + 1) * j], desc);

#ifdef SHOW_WORKING
    if (ret)
        fprintf(stderr, "set_dot_dline %p [%d,%d] %s (%s)\n", dline_array, i, j, DL2STR(desc), reason);
#endif
    return ret;
}

static int get_square_dline(game_state *state, char *dline_array,
                            int i, int j, enum dline_desc desc)
{
    const struct dline *dl = get_dline(desc);
    assert(dl->dx != -1 && dl->dy != -1);
/*    fprintf(stderr, "get_square_dline %p [%d,%d] %s\n", dline_array, i, j, DL2STR(desc)); */
    return BIT_SET(dline_array[(i+dl->dx) + (state->w + 1) * (j+dl->dy)], 
                   desc);
}

static int set_square_dline(game_state *state, char *dline_array,
                            int i, int j, enum dline_desc desc
#ifdef SHOW_WORKING
                            , const char *reason
#endif
                            )
{
    const struct dline *dl = get_dline(desc);
    int ret;
    assert(dl->dx != -1 && dl->dy != -1);
    ret = SET_BIT(dline_array[(i+dl->dx) + (state->w + 1) * (j+dl->dy)], desc);
#ifdef SHOW_WORKING
    if (ret)
        fprintf(stderr, "set_square_dline %p [%d,%d] %s (%s)\n", dline_array, i, j, DL2STR(desc), reason);
#endif
    return ret;
}

#ifdef SHOW_WORKING
#define set_dot_dline(a, b, c, d, e) \
        set_dot_dline(a, b, c, d, e, __FUNCTION__)
#define set_square_dline(a, b, c, d, e) \
        set_square_dline(a, b, c, d, e, __FUNCTION__)
#endif

static int set_dot_opp_dline(game_state *state, char *dline_array,
                             int i, int j, enum dline_desc desc)
{
    return set_dot_dline(state, dline_array, i, j, OPP_DLINE(desc));
}

static int set_square_opp_dline(game_state *state, char *dline_array,
                                int i, int j, enum dline_desc desc)
{
    return set_square_dline(state, dline_array, i, j, OPP_DLINE(desc));
}

/* Find out if both the lines in the given dline are UNKNOWN */
static int dline_both_unknown(const game_state *state, int i, int j,
                              enum dline_desc desc)
{
    const struct dline *dl = get_dline(desc);
    return 
        (get_line_status_from_point(state, i, j, dl->dir1) == LINE_UNKNOWN) &&
        (get_line_status_from_point(state, i, j, dl->dir2) == LINE_UNKNOWN);
}

#define SQUARE_DLINES \
                   HANDLE_DLINE(DLINE_UL, RIGHTOF_SQUARE, BELOW_SQUARE, 1, 1); \
                   HANDLE_DLINE(DLINE_UR, LEFTOF_SQUARE,  BELOW_SQUARE, 0, 1); \
                   HANDLE_DLINE(DLINE_DL, RIGHTOF_SQUARE, ABOVE_SQUARE, 1, 0); \
                   HANDLE_DLINE(DLINE_DR, LEFTOF_SQUARE,  ABOVE_SQUARE, 0, 0); 

#define DOT_DLINES \
                   HANDLE_DLINE(DLINE_UD,    ABOVE_DOT,  BELOW_DOT); \
                   HANDLE_DLINE(DLINE_LR,    LEFTOF_DOT, RIGHTOF_DOT); \
                   HANDLE_DLINE(DLINE_UL,    ABOVE_DOT,  LEFTOF_DOT); \
                   HANDLE_DLINE(DLINE_UR,    ABOVE_DOT,  RIGHTOF_DOT); \
                   HANDLE_DLINE(DLINE_DL,    BELOW_DOT,  LEFTOF_DOT); \
                   HANDLE_DLINE(DLINE_DR,    BELOW_DOT,  RIGHTOF_DOT); 

static void array_setall(char *array, char from, char to, int len)
{
    char *p = array, *p_old = p;
    int len_remaining = len;

    while ((p = memchr(p, from, len_remaining))) {
        *p = to;
        len_remaining -= p - p_old;
        p_old = p;
    }
}



static int get_line_status_from_point(const game_state *state,
                                      int x, int y, enum direction d)
{
    switch (d) {
        case LEFT:
            return LEFTOF_DOT(state, x, y);
        case RIGHT:
            return RIGHTOF_DOT(state, x, y);
        case UP:
            return ABOVE_DOT(state, x, y);
        case DOWN:
            return BELOW_DOT(state, x, y);
    }

    return 0;
}

/* First and second args are coord offset from top left of square to one end
 * of line in question, third and fourth args are the direction from the first
 * end of the line to the second.  Fifth arg is the direction of the line from
 * the coord offset position.
 * How confusing.  
 */
#define SQUARE_LINES \
    SQUARE_LINE( 0,  0, RIGHT, RIGHTOF_DOT, UP); \
    SQUARE_LINE( 0, +1, RIGHT, RIGHTOF_DOT, DOWN); \
    SQUARE_LINE( 0,  0, DOWN,  BELOW_DOT,   LEFT); \
    SQUARE_LINE(+1,  0, DOWN,  BELOW_DOT,   RIGHT); 

/* Set pairs of lines around this square which are known to be identical to
 * the given line_state */
static int square_setall_identical(solver_state *sstate, int x, int y,
                                   enum line_state line_new)
{
    /* can[dir] contains the canonical line associated with the line in
     * direction dir from the square in question.  Similarly inv[dir] is
     * whether or not the line in question is inverse to its canonical
     * element. */
    int can[4], inv[4], i, j;
    int retval = FALSE;

    i = 0;

#if 0
    fprintf(stderr, "Setting all identical unknown lines around square "
                    "[%d,%d] to %d:\n", x, y, line_new);                 
#endif

#define SQUARE_LINE(dx, dy, linedir, dir_dot, sqdir) \
    can[sqdir] = \
        edsf_canonify(sstate->hard->linedsf, \
                      LINEDSF_INDEX(sstate->state, x+(dx), y+(dy), linedir), \
                      &inv[sqdir]);
    
    SQUARE_LINES;

#undef SQUARE_LINE

    for (j = 0; j < 4; ++j) {
        for (i = 0; i < 4; ++i) {
            if (i == j)
                continue;

            if (can[i] == can[j] && inv[i] == inv[j]) {

                /* Lines in directions i and j are identical.
                 * Only do j now, we'll do i when the loop causes us to
                 * consider {i,j} in the opposite order. */
#define SQUARE_LINE(dx, dy, dir, c, sqdir) \
                if (j == sqdir) { \
                    retval = set_line_bydot(sstate, x+(dx), y+(dy), dir, line_new); \
                    if (retval) { \
                        break; \
                    } \
                }
                
                SQUARE_LINES;

#undef SQUARE_LINE
            }
        }
    }

    return retval;
}

#if 0
/* Set all identical lines passing through the current dot to the chosen line
 * state.  (implicitly this only looks at UNKNOWN lines) */
static int dot_setall_identical(solver_state *sstate, int x, int y,
                                enum line_state line_new)
{
    /* The implementation of this is a little naughty but I can't see how to do
     * it elegantly any other way */
    int can[4], inv[4], i, j;
    enum direction d;
    int retval = FALSE;

    for (d = 0; d < 4; ++d) {
        can[d] = edsf_canonify(sstate->hard->linedsf, 
                               LINEDSF_INDEX(sstate->state, x, y, d),
                               inv+d);
    }
    
    for (j = 0; j < 4; ++j) {
next_j:
        for (i = 0; i < j; ++i) {
            if (can[i] == can[j] && inv[i] == inv[j]) {
                /* Lines in directions i and j are identical */
                if (get_line_status_from_point(sstate->state, x, y, j) ==
                        LINE_UNKNOWN) {
                    set_line_bydot(sstate->state, x, y, j, 
                                               line_new);
                    retval = TRUE;
                    goto next_j;
                }
            }

        }
    }

    return retval;
}
#endif

static int square_setboth_in_dline(solver_state *sstate, enum dline_desc dd,
                                   int i, int j, enum line_state line_new)
{
    int retval = FALSE;
    const struct dline *dl = get_dline(dd);
    
#if 0
    fprintf(stderr, "square_setboth_in_dline %s [%d,%d] to %d\n",
                    DL2STR(dd), i, j, line_new);
#endif

    assert(dl->dx != -1 && dl->dy != -1);
    
    retval |=
        set_line_bydot(sstate, i+dl->dx, j+dl->dy, dl->dir1, line_new);
    retval |=
        set_line_bydot(sstate, i+dl->dx, j+dl->dy, dl->dir2, line_new);

    return retval;
}

/* Call this function to register that the two unknown lines going into the dot
 * [x,y] are identical or opposite (depending on the value of 'inverse').  This
 * function will cause an assertion failure if anything other than exactly two
 * lines into the dot are unknown. 
 * As usual returns TRUE if any progress was made, otherwise FALSE. */
static int dot_relate_2_unknowns(solver_state *sstate, int x, int y, int inverse)
{
    enum direction d1=DOWN, d2=DOWN; /* Just to keep compiler quiet */
    int dirs_set = 0;

#define TRY_DIR(d) \
              if (get_line_status_from_point(sstate->state, x, y, d) == \
                      LINE_UNKNOWN) { \
                  if (dirs_set == 0) \
                      d1 = d; \
                  else { \
                      assert(dirs_set == 1); \
                      d2 = d; \
                  } \
                  dirs_set++; \
              } while (0)
    
    TRY_DIR(UP);
    TRY_DIR(DOWN);
    TRY_DIR(LEFT);
    TRY_DIR(RIGHT);
#undef TRY_DIR

    assert(dirs_set == 2);
    assert(d1 != d2);

#if 0
    fprintf(stderr, "Lines in direction %s and %s from dot [%d,%d] are %s\n",
            DIR2STR(d1), DIR2STR(d2), x, y, inverse?"opposite":"the same");
#endif

    return merge_lines(sstate, x, y, d1, x, y, d2, inverse);
}

/* Very similar to dot_relate_2_unknowns. */
static int square_relate_2_unknowns(solver_state *sstate, int x, int y, int inverse)
{
    enum direction d1=DOWN, d2=DOWN;
    int x1=-1, y1=-1, x2=-1, y2=-1;
    int dirs_set = 0;

#if 0
    fprintf(stderr, "2 unknowns around square [%d,%d] are %s\n",
                     x, y, inverse?"opposite":"the same");
#endif

#define TRY_DIR(i, j, d, dir_sq) \
          do { \
              if (dir_sq(sstate->state, x, y) == LINE_UNKNOWN) { \
                  if (dirs_set == 0) { \
                      d1 = d; x1 = i; y1 = j; \
                  } else { \
                      assert(dirs_set == 1); \
                      d2 = d; x2 = i; y2 = j; \
                  } \
                  dirs_set++; \
              } \
          } while (0)
    
    TRY_DIR(x,   y,   RIGHT, ABOVE_SQUARE);
    TRY_DIR(x,   y,   DOWN, LEFTOF_SQUARE);
    TRY_DIR(x+1, y,   DOWN, RIGHTOF_SQUARE);
    TRY_DIR(x,   y+1, RIGHT, BELOW_SQUARE);
#undef TRY_DIR

    assert(dirs_set == 2);

#if 0
    fprintf(stderr, "Line in direction %s from dot [%d,%d] and line in direction %s from dot [%2d,%2d] are %s\n",
            DIR2STR(d1), x1, y1, DIR2STR(d2), x2, y2, inverse?"opposite":"the same");
#endif

    return merge_lines(sstate, x1, y1, d1, x2, y2, d2, inverse);
}

/* Figure out if any dlines can be 'collapsed' (and do so if they can).  This
 * can happen if one of the lines is known and due to the dline status this
 * tells us state of the other, or if there's an interaction with the linedsf
 * (ie if atmostone is set for a dline and the lines are known identical they
 * must both be LINE_NO, etc).  XXX at the moment only the former is
 * implemented, and indeed the latter should be implemented in the hard mode
 * solver only.
 */
static int dot_collapse_dlines(solver_state *sstate, int i, int j)
{
    int progress = FALSE;
    enum direction dir1, dir2;
    int dir1st;
    int dlset;
    game_state *state = sstate->state;
    enum dline_desc dd;

    for (dir1 = 0; dir1 < 4; dir1++) {
        dir1st = get_line_status_from_point(state, i, j, dir1);
        if (dir1st == LINE_UNKNOWN)
            continue;
        /* dir2 iterates over the whole range rather than starting at dir1+1
         * because test below is asymmetric */
        for (dir2 = 0; dir2 < 4; dir2++) {
            if (dir1 == dir2)
                continue;

            if ((i == 0        && (dir1 == LEFT  || dir2 == LEFT))  ||
                (j == 0        && (dir1 == UP    || dir2 == UP))    ||
                (i == state->w && (dir1 == RIGHT || dir2 == RIGHT)) ||
                (j == state->h && (dir1 == DOWN  || dir2 == DOWN))) {
                continue;
            }

#if 0
        fprintf(stderr, "dot_collapse_dlines [%d,%d], %s %s\n", i, j,
                    DIR2STR(dir1), DIR2STR(dir2));
#endif

            if (get_line_status_from_point(state, i, j, dir2) == 
                LINE_UNKNOWN) {
                dd = dline_desc_from_dirs(dir1, dir2);

                dlset = get_dot_dline(state, sstate->normal->dot_atmostone, i, j, dd);
                if (dlset && dir1st == LINE_YES) {
/*                    fprintf(stderr, "setting %s to NO\n", DIR2STR(dir2)); */
                    progress |= 
                        set_line_bydot(sstate, i, j, dir2, LINE_NO);
                }

                dlset = get_dot_dline(state, sstate->normal->dot_atleastone, i, j, dd);
                if (dlset && dir1st == LINE_NO) {
/*                    fprintf(stderr, "setting %s to YES\n", DIR2STR(dir2)); */
                    progress |=
                        set_line_bydot(sstate, i, j, dir2, LINE_YES);
                }
            }
        }
    }

    return progress;
}

/*
 * These are the main solver functions.  
 *
 * Their return values are diff values corresponding to the lowest mode solver
 * that would notice the work that they have done.  For example if the normal
 * mode solver adds actual lines or crosses, it will return DIFF_EASY as the
 * easy mode solver might be able to make progress using that.  It doesn't make
 * sense for one of them to return a diff value higher than that of the
 * function itself.  
 *
 * Each function returns the lowest value it can, as early as possible, in
 * order to try and pass as much work as possible back to the lower level
 * solvers which progress more quickly.
 */

/* PROPOSED NEW DESIGN:
 * We have a work queue consisting of 'events' notifying us that something has
 * happened that a particular solver mode might be interested in.  For example
 * the hard mode solver might do something that helps the normal mode solver at
 * dot [x,y] in which case it will enqueue an event recording this fact.  Then
 * we pull events off the work queue, and hand each in turn to the solver that
 * is interested in them.  If a solver reports that it failed we pass the same
 * event on to progressively more advanced solvers and the loop detector.  Once
 * we've exhausted an event, or it has helped us progress, we drop it and
 * continue to the next one.  The events are sorted first in order of solver
 * complexity (easy first) then order of insertion (oldest first).
 * Once we run out of events we loop over each permitted solver in turn
 * (easiest first) until either a deduction is made (and an event therefore
 * emerges) or no further deductions can be made (in which case we've failed).
 *
 * QUESTIONS: 
 *    * How do we 'loop over' a solver when both dots and squares are concerned.
 *      Answer: first all squares then all dots.
 */

static int easy_mode_deductions(solver_state *sstate)
{
    int i, j, h, w, current_yes, current_no;
    game_state *state;
    int diff = DIFF_MAX;

    state = sstate->state;
    h = state->h;
    w = state->w;
    
    /* Per-square deductions */
    FORALL_SQUARES(state, i, j) {
        if (sstate->square_solved[SQUARE_INDEX(state, i, j)])
            continue;

        current_yes = SQUARE_YES_COUNT(sstate, i, j);
        current_no  = SQUARE_NO_COUNT(sstate, i, j);

        if (current_yes + current_no == 4)  {
            sstate->square_solved[SQUARE_INDEX(state, i, j)] = TRUE;
/*            diff = min(diff, DIFF_EASY); */
            continue;
        }

        if (CLUE_AT(state, i, j) < 0)
            continue;

        if (CLUE_AT(state, i, j) < current_yes) {
#if 0
            fprintf(stderr, "detected error [%d,%d] in %s at line %d\n", i, j, __FUNCTION__, __LINE__);
#endif
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (CLUE_AT(state, i, j) == current_yes) {
            if (square_setall(sstate, i, j, LINE_UNKNOWN, LINE_NO))
                diff = min(diff, DIFF_EASY);
            sstate->square_solved[SQUARE_INDEX(state, i, j)] = TRUE;
            continue;
        }

        if (4 - CLUE_AT(state, i, j) < current_no) {
#if 0
            fprintf(stderr, "detected error [%d,%d] in %s at line %d\n", i, j, __FUNCTION__, __LINE__);
#endif
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (4 - CLUE_AT(state, i, j) == current_no) {
            if (square_setall(sstate, i, j, LINE_UNKNOWN, LINE_YES))
                diff = min(diff, DIFF_EASY);
            sstate->square_solved[SQUARE_INDEX(state, i, j)] = TRUE;
            continue;
        }
    }

    check_caches(sstate);

    /* Per-dot deductions */
    FORALL_DOTS(state, i, j) {
        if (sstate->dot_solved[DOT_INDEX(state, i, j)])
            continue;

        switch (DOT_YES_COUNT(sstate, i, j)) {
            case 0:
                switch (DOT_NO_COUNT(sstate, i, j)) {
                    case 3:
#if 0
                        fprintf(stderr, "dot [%d,%d]: 0 yes, 3 no\n", i, j);
#endif
                        dot_setall(sstate, i, j, LINE_UNKNOWN, LINE_NO);
                        diff = min(diff, DIFF_EASY);
                        /* fall through */
                    case 4:
                        sstate->dot_solved[DOT_INDEX(state, i, j)] = TRUE;
                        break;
                }
                break;
            case 1:
                switch (DOT_NO_COUNT(sstate, i, j)) {
                    case 2: /* 1 yes, 2 no */
#if 0
                        fprintf(stderr, "dot [%d,%d]: 1 yes, 2 no\n", i, j);
#endif
                        dot_setall(sstate, i, j, LINE_UNKNOWN, LINE_YES);
                        diff = min(diff, DIFF_EASY);
                        sstate->dot_solved[DOT_INDEX(state, i, j)] = TRUE;
                        break;
                    case 3: /* 1 yes, 3 no */
#if 0
                        fprintf(stderr, "detected error [%d,%d] in %s at line %d\n", i, j, __FUNCTION__, __LINE__);
#endif
                        sstate->solver_status = SOLVER_MISTAKE;
                        return DIFF_EASY;
                }
                break;
            case 2:
#if 0
                fprintf(stderr, "dot [%d,%d]: 2 yes\n", i, j);
#endif
                dot_setall(sstate, i, j, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
                sstate->dot_solved[DOT_INDEX(state, i, j)] = TRUE;
                break;
            case 3:
            case 4:
#if 0
                fprintf(stderr, "detected error [%d,%d] in %s at line %d\n", i, j, __FUNCTION__, __LINE__);
#endif
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
        }
    }

    check_caches(sstate);

    return diff;
}

static int normal_mode_deductions(solver_state *sstate)
{
    int i, j;
    game_state *state = sstate->state;
    enum dline_desc dd;
    int diff = DIFF_MAX;

    FORALL_SQUARES(state, i, j) {
        if (sstate->square_solved[SQUARE_INDEX(state, i, j)])
            continue;

        if (CLUE_AT(state, i, j) < 0)
            continue;

        switch (CLUE_AT(state, i, j)) {
            case 1:
#if 0
                fprintf(stderr, "clue [%d,%d] is 1, doing dline ops\n",
                        i, j);
#endif
                FORALL_SQUARE_DLINES(dd) {
                    /* At most one of any DLINE can be set */
                    if (set_square_dline(state, 
                                         sstate->normal->dot_atmostone, 
                                         i, j, dd)) {
                        diff = min(diff, DIFF_NORMAL);
                    }

                    if (get_square_dline(state,
                                         sstate->normal->dot_atleastone, 
                                         i, j, dd)) {
                        /* This DLINE provides enough YESes to solve the clue */
                        if (square_setboth_in_dline(sstate, OPP_DLINE(dd),
                                                     i, j, LINE_NO)) {
                            diff = min(diff, DIFF_EASY);
                        }
                    }
                }

                break;
            case 2:
                /* If at least one of one DLINE is set, at most one
                 * of the opposing one is and vice versa */
#if 0
                fprintf(stderr, "clue [%d,%d] is 2, doing dline ops\n",
                               i, j);
#endif
                FORALL_SQUARE_DLINES(dd) {
                    if (get_square_dline(state,
                                         sstate->normal->dot_atmostone,
                                         i, j, dd)) {
                        if (set_square_opp_dline(state,
                                                 sstate->normal->dot_atleastone,
                                                 i, j, dd)) {
                            diff = min(diff, DIFF_NORMAL);
                        }
                    }
                    if (get_square_dline(state,
                                         sstate->normal->dot_atleastone,
                                         i, j, dd)) {
                        if (set_square_opp_dline(state,
                                                 sstate->normal->dot_atmostone,
                                                 i, j, dd)) {
                            diff = min(diff, DIFF_NORMAL);
                        }
                    }
                }
                break;
            case 3:
#if 0
                fprintf(stderr, "clue [%d,%d] is 3, doing dline ops\n",
                                i, j);
#endif
                FORALL_SQUARE_DLINES(dd) {
                    /* At least one of any DLINE must be set */
                    if (set_square_dline(state, 
                                         sstate->normal->dot_atleastone, 
                                         i, j, dd)) {
                        diff = min(diff, DIFF_NORMAL);
                    }

                    if (get_square_dline(state,
                                         sstate->normal->dot_atmostone, 
                                         i, j, dd)) {
                        /* This DLINE provides enough NOs to solve the clue */
                        if (square_setboth_in_dline(sstate, OPP_DLINE(dd),
                                                    i, j, LINE_YES)) {
                            diff = min(diff, DIFF_EASY);
                        }
                    }
                }
                break;
        }
    }

    check_caches(sstate);

    if (diff < DIFF_NORMAL)
        return diff;

    FORALL_DOTS(state, i, j) {
        if (sstate->dot_solved[DOT_INDEX(state, i, j)])
            continue;

#if 0
        text = game_text_format(state);
        fprintf(stderr, "-----------------\n%s", text);
        sfree(text);
#endif

        switch (DOT_YES_COUNT(sstate, i, j)) {
        case 0:
            switch (DOT_NO_COUNT(sstate, i, j)) {
                case 1:
                    /* Make note that at most one of each unknown DLINE
                     * is YES */
                    break;
            }
            break;

        case 1:
            switch (DOT_NO_COUNT(sstate, i, j)) {
                case 1: 
                    /* 1 yes, 1 no, so exactly one of unknowns is
                     * yes */
#if 0
                    fprintf(stderr, "dot [%d,%d]: 1 yes, 1 no\n", i, j);
#endif
                    FORALL_DOT_DLINES(dd) {
                        if (dline_both_unknown(state, 
                                               i, j, dd)) {
                            if (set_dot_dline(state,
                                              sstate->normal->dot_atleastone,
                                              i, j, dd)) {
                                diff = min(diff, DIFF_NORMAL); 
                            }
                        }
                    }

                    /* fall through */
                case 0: 
#if 0
                    fprintf(stderr, "dot [%d,%d]: 1 yes, 0 or 1 no\n", i, j);
#endif
                    /* 1 yes, fewer than 2 no, so at most one of
                     * unknowns is yes */
                    FORALL_DOT_DLINES(dd) {
                        if (dline_both_unknown(state, 
                                               i, j, dd)) {
                            if (set_dot_dline(state,
                                              sstate->normal->dot_atmostone,
                                              i, j, dd)) {
                                diff = min(diff, DIFF_NORMAL); 
                            }
                        }
                    }
                    break;
            }
            break;
        }

        /* DLINE deductions that don't depend on the exact number of
         * LINE_YESs or LINE_NOs */

        /* If at least one of a dline in a dot is YES, at most one
         * of the opposite dline to that dot must be YES. */
        FORALL_DOT_DLINES(dd) {
            if (get_dot_dline(state, 
                              sstate->normal->dot_atleastone,
                              i, j, dd)) {
                if (set_dot_opp_dline(state,
                                      sstate->normal->dot_atmostone,
                                      i, j, dd)) {
                    diff = min(diff, DIFF_NORMAL); 
                }
            }
        }

        if (dot_collapse_dlines(sstate, i, j))
            diff = min(diff, DIFF_EASY);
    }
    check_caches(sstate);

    return diff;
}

static int hard_mode_deductions(solver_state *sstate)
{
    int i, j, a, b, s;
    game_state *state = sstate->state;
    const int h=state->h, w=state->w;
    enum direction dir1, dir2;
    int can1, can2, inv1, inv2;
    int diff = DIFF_MAX;
    const struct dline *dl;
    enum dline_desc dd;

    FORALL_SQUARES(state, i, j) {
        if (sstate->square_solved[SQUARE_INDEX(state, i, j)])
            continue;

        switch (CLUE_AT(state, i, j)) {
            case -1:
                continue;

            case 1:
                if (square_setall_identical(sstate, i, j, LINE_NO)) 
                    diff = min(diff, DIFF_EASY);
                break;
            case 3:
                if (square_setall_identical(sstate, i, j, LINE_YES))
                    diff = min(diff, DIFF_EASY);
                break;
        }

        if (SQUARE_YES_COUNT(sstate, i, j) + 
            SQUARE_NO_COUNT(sstate, i, j) == 2) {
            /* There are exactly two unknown lines bordering this
             * square. */
            if (SQUARE_YES_COUNT(sstate, i, j) + 1 == 
                CLUE_AT(state, i, j)) {
                /* They must be different */
                if (square_relate_2_unknowns(sstate, i, j, TRUE))
                    diff = min(diff, DIFF_HARD);
            }
        }
    }

    check_caches(sstate);

    FORALL_DOTS(state, i, j) {
        if (DOT_YES_COUNT(sstate, i, j) == 1 &&
            DOT_NO_COUNT(sstate, i, j) == 1) {
            if (dot_relate_2_unknowns(sstate, i, j, TRUE))
                diff = min(diff, DIFF_HARD);
            continue;
        }

        if (DOT_YES_COUNT(sstate, i, j) == 0 &&
            DOT_NO_COUNT(sstate, i, j) == 2) {
            if (dot_relate_2_unknowns(sstate, i, j, FALSE))
                diff = min(diff, DIFF_HARD);
            continue;
        }
    }

    /* If two lines into a dot are related, the other two lines into that dot
     * are related in the same way. */

    /* iter over points that aren't on edges */
    for (i = 1; i < w; ++i) {
        for (j = 1; j < h; ++j) {
            if (sstate->dot_solved[DOT_INDEX(state, i, j)])
                continue;

            /* iter over directions */
            for (dir1 = 0; dir1 < 4; ++dir1) {
                for (dir2 = dir1+1; dir2 < 4; ++dir2) {
                    /* canonify both lines */
                    can1 = edsf_canonify
                        (sstate->hard->linedsf,
                         LINEDSF_INDEX(state, i, j, dir1),
                         &inv1);
                    can2 = edsf_canonify
                        (sstate->hard->linedsf,
                         LINEDSF_INDEX(state, i, j, dir2),
                         &inv2);
                    /* merge opposite lines */
                    if (can1 == can2) {
                        if (merge_lines(sstate, 
                                        i, j, OPP_DIR(dir1),
                                        i, j, OPP_DIR(dir2),
                                        inv1 ^ inv2)) {
                            diff = min(diff, DIFF_HARD);
                        }
                    }
                }
            }
        }
    }

    /* If the state of a line is known, deduce the state of its canonical line
     * too. */
    FORALL_DOTS(state, i, j) {
        /* Do this even if the dot we're on is solved */
        if (i < w) {
            can1 = edsf_canonify(sstate->hard->linedsf, 
                                 LINEDSF_INDEX(state, i, j, RIGHT),
                                 &inv1);
            linedsf_deindex(state, can1, &a, &b, &dir1);
            s = RIGHTOF_DOT(state, i, j);
            if (s != LINE_UNKNOWN)
            {
                if (set_line_bydot(sstate, a, b, dir1, inv1 ? OPP(s) : s))
                    diff = min(diff, DIFF_EASY);
            }
        }
        if (j < h) {
            can1 = edsf_canonify(sstate->hard->linedsf, 
                                 LINEDSF_INDEX(state, i, j, DOWN),
                                 &inv1);
            linedsf_deindex(state, can1, &a, &b, &dir1);
            s = BELOW_DOT(state, i, j);
            if (s != LINE_UNKNOWN)
            {
                if (set_line_bydot(sstate, a, b, dir1, inv1 ? OPP(s) : s))
                    diff = min(diff, DIFF_EASY);
            }
        }
    }

    /* Interactions between dline and linedsf */
    FORALL_DOTS(state, i, j) {
        if (sstate->dot_solved[DOT_INDEX(state, i, j)])
            continue;

        FORALL_DOT_DLINES(dd) {
            dl = get_dline(dd);
            if (i == 0 && (dl->dir1 == LEFT || dl->dir2 == LEFT))
                continue;
            if (i == w && (dl->dir1 == RIGHT || dl->dir2 == RIGHT))
                continue;
            if (j == 0 && (dl->dir1 == UP || dl->dir2 == UP))
                continue;
            if (j == h && (dl->dir1 == DOWN || dl->dir2 == DOWN))
                continue;

            if (get_dot_dline(state, sstate->normal->dot_atleastone,
                              i, j, dd) &&
                get_dot_dline(state, sstate->normal->dot_atmostone,
                              i, j, dd)) {
                /* atleastone && atmostone => inverse */
                if (merge_lines(sstate, i, j, dl->dir1, i, j, dl->dir2, 1)) {
                    diff = min(diff, DIFF_HARD);
                }
            } else {
                /* don't have atleastone and atmostone for this dline */
                can1 = edsf_canonify(sstate->hard->linedsf,
                                     LINEDSF_INDEX(state, i, j, dl->dir1),
                                     &inv1);
                can2 = edsf_canonify(sstate->hard->linedsf,
                                     LINEDSF_INDEX(state, i, j, dl->dir2),
                                     &inv2);
                if (can1 == can2) {
                    if (inv1 == inv2) {
                        /* identical => collapse dline */
                        if (get_dot_dline(state, 
                                          sstate->normal->dot_atleastone,
                                          i, j, dd)) {
                            if (set_line_bydot(sstate, i, j, 
                                               dl->dir1, LINE_YES)) {
                                diff = min(diff, DIFF_EASY);
                            }
                            if (set_line_bydot(sstate, i, j, 
                                               dl->dir2, LINE_YES)) {
                                diff = min(diff, DIFF_EASY);
                            }
                        } else if (get_dot_dline(state, 
                                                 sstate->normal->dot_atmostone,
                                                 i, j, dd)) {
                            if (set_line_bydot(sstate, i, j, 
                                               dl->dir1, LINE_NO)) {
                                diff = min(diff, DIFF_EASY);
                            }
                            if (set_line_bydot(sstate, i, j, 
                                               dl->dir2, LINE_NO)) {
                                diff = min(diff, DIFF_EASY);
                            }
                        }
                    } else {
                        /* inverse => atleastone && atmostone */
                        if (set_dot_dline(state, 
                                          sstate->normal->dot_atleastone,
                                          i, j, dd)) {
                            diff = min(diff, DIFF_NORMAL);
                        }
                        if (set_dot_dline(state, 
                                          sstate->normal->dot_atmostone,
                                          i, j, dd)) {
                            diff = min(diff, DIFF_NORMAL);
                        }
                    }
                }
            }
        }
    }
    
    /* If the state of the canonical line for line 'l' is known, deduce the
     * state of 'l' */
    FORALL_DOTS(state, i, j) {
        if (sstate->dot_solved[DOT_INDEX(state, i, j)])
            continue;

        if (i < w) {
            can1 = edsf_canonify(sstate->hard->linedsf, 
                                 LINEDSF_INDEX(state, i, j, RIGHT),
                                 &inv1);
            linedsf_deindex(state, can1, &a, &b, &dir1);
            s = get_line_status_from_point(state, a, b, dir1);
            if (s != LINE_UNKNOWN)
            {
                if (set_line_bydot(sstate, i, j, RIGHT, inv1 ? OPP(s) : s))
                    diff = min(diff, DIFF_EASY);
            }
        }
        if (j < h) {
            can1 = edsf_canonify(sstate->hard->linedsf, 
                                 LINEDSF_INDEX(state, i, j, DOWN),
                                 &inv1);
            linedsf_deindex(state, can1, &a, &b, &dir1);
            s = get_line_status_from_point(state, a, b, dir1);
            if (s != LINE_UNKNOWN)
            {
                if (set_line_bydot(sstate, i, j, DOWN, inv1 ? OPP(s) : s))
                    diff = min(diff, DIFF_EASY);
            }
        }
    }

    return diff;
}

static int loop_deductions(solver_state *sstate)
{
    int edgecount = 0, clues = 0, satclues = 0, sm1clues = 0;
    game_state *state = sstate->state;
    int shortest_chainlen = DOT_COUNT(state);
    int loop_found = FALSE;
    int d;
    int dots_connected;
    int progress = FALSE;
    int i, j;

    /*
     * Go through the grid and update for all the new edges.
     * Since merge_dots() is idempotent, the simplest way to
     * do this is just to update for _all_ the edges.
     * 
     * Also, while we're here, we count the edges, count the
     * clues, count the satisfied clues, and count the
     * satisfied-minus-one clues.
     */
    FORALL_DOTS(state, i, j) {
        if (RIGHTOF_DOT(state, i, j) == LINE_YES) {
            loop_found |= merge_dots(sstate, i, j, i+1, j);
            edgecount++;
        }
        if (BELOW_DOT(state, i, j) == LINE_YES) {
            loop_found |= merge_dots(sstate, i, j, i, j+1);
            edgecount++;
        }

        if (CLUE_AT(state, i, j) >= 0) {
            int c = CLUE_AT(state, i, j);
            int o = SQUARE_YES_COUNT(sstate, i, j);
            if (o == c)
                satclues++;
            else if (o == c-1)
                sm1clues++;
            clues++;
        }
    }

    for (i = 0; i < DOT_COUNT(state); ++i) {
        dots_connected = 
            sstate->looplen[dsf_canonify(sstate->dotdsf, i)];
        if (dots_connected > 1)
            shortest_chainlen = min(shortest_chainlen, dots_connected);
    }

    assert(sstate->solver_status == SOLVER_INCOMPLETE);

    if (satclues == clues && shortest_chainlen == edgecount) {
        sstate->solver_status = SOLVER_SOLVED;
        /* This discovery clearly counts as progress, even if we haven't
         * just added any lines or anything */
        progress = TRUE; 
        goto finished_loop_deductionsing;
    }

    /*
     * Now go through looking for LINE_UNKNOWN edges which
     * connect two dots that are already in the same
     * equivalence class. If we find one, test to see if the
     * loop it would create is a solution.
     */
    FORALL_DOTS(state, i, j) {
        for (d = 0; d < 2; d++) {
            int i2, j2, eqclass, val;

            if (d == 0) {
                if (RIGHTOF_DOT(state, i, j) !=
                        LINE_UNKNOWN)
                    continue;
                i2 = i+1;
                j2 = j;
            } else {
                if (BELOW_DOT(state, i, j) !=
                    LINE_UNKNOWN) {
                    continue;
                }
                i2 = i;
                j2 = j+1;
            }

            eqclass = dsf_canonify(sstate->dotdsf, j * (state->w+1) + i);
            if (eqclass != dsf_canonify(sstate->dotdsf,
                                        j2 * (state->w+1) + i2)) {
                continue;
            }

            val = LINE_NO;  /* loop is bad until proven otherwise */

            /*
             * This edge would form a loop. Next
             * question: how long would the loop be?
             * Would it equal the total number of edges
             * (plus the one we'd be adding if we added
             * it)?
             */
            if (sstate->looplen[eqclass] == edgecount + 1) {
                int sm1_nearby;
                int cx, cy;

                /*
                 * This edge would form a loop which
                 * took in all the edges in the entire
                 * grid. So now we need to work out
                 * whether it would be a valid solution
                 * to the puzzle, which means we have to
                 * check if it satisfies all the clues.
                 * This means that every clue must be
                 * either satisfied or satisfied-minus-
                 * 1, and also that the number of
                 * satisfied-minus-1 clues must be at
                 * most two and they must lie on either
                 * side of this edge.
                 */
                sm1_nearby = 0;
                cx = i - (j2-j);
                cy = j - (i2-i);
                if (CLUE_AT(state, cx,cy) >= 0 &&
                        square_order(state, cx,cy, LINE_YES) ==
                        CLUE_AT(state, cx,cy) - 1) {
                    sm1_nearby++;
                }
                if (CLUE_AT(state, i, j) >= 0 &&
                        SQUARE_YES_COUNT(sstate, i, j) ==
                        CLUE_AT(state, i, j) - 1) {
                    sm1_nearby++;
                }
                if (sm1clues == sm1_nearby &&
                    sm1clues + satclues == clues) {
                    val = LINE_YES;  /* loop is good! */
                }
            }

            /*
             * Right. Now we know that adding this edge
             * would form a loop, and we know whether
             * that loop would be a viable solution or
             * not.
             * 
             * If adding this edge produces a solution,
             * then we know we've found _a_ solution but
             * we don't know that it's _the_ solution -
             * if it were provably the solution then
             * we'd have deduced this edge some time ago
             * without the need to do loop detection. So
             * in this state we return SOLVER_AMBIGUOUS,
             * which has the effect that hitting Solve
             * on a user-provided puzzle will fill in a
             * solution but using the solver to
             * construct new puzzles won't consider this
             * a reasonable deduction for the user to
             * make.
             */
            if (d == 0) {
                progress = set_line_bydot(sstate, i, j, RIGHT, val);
                assert(progress == TRUE);
            } else {
                progress = set_line_bydot(sstate, i, j, DOWN, val);
                assert(progress == TRUE);
            }
            if (val == LINE_YES) {
                sstate->solver_status = SOLVER_AMBIGUOUS;
                goto finished_loop_deductionsing;
            }
        }
    }

finished_loop_deductionsing:
    return progress ? DIFF_EASY : DIFF_MAX;
}

/* This will return a dynamically allocated solver_state containing the (more)
 * solved grid */
static solver_state *solve_game_rec(const solver_state *sstate_start, 
                                    int diff)
{
    int i, j;
    int w, h;
    solver_state *sstate, *sstate_saved, *sstate_tmp;
    solver_state *sstate_rec_solved;
    int recursive_soln_count;
    int solver_progress;
    game_state *state;

    /* Indicates which solver we should call next.  This is a sensible starting
     * point */
    int current_solver = DIFF_EASY, next_solver;
#ifdef SHOW_WORKING
    char *text;
#endif

#if 0
    printf("solve_game_rec: recursion_remaining = %d\n", 
           sstate_start->recursion_remaining);
#endif

    sstate = dup_solver_state(sstate_start);
 
    /* Cache the values of some variables for readability */
    state = sstate->state;
    h = state->h;
    w = state->w;

    sstate_saved = NULL;

nonrecursive_solver:
    solver_progress = FALSE;

    check_caches(sstate);

    do {
#ifdef SHOW_WORKING
        text = game_text_format(state);
        fprintf(stderr, "-----------------\n%s", text);
        sfree(text);
#endif

        if (sstate->solver_status == SOLVER_MISTAKE)
            return sstate;

/*        fprintf(stderr, "Invoking solver %d\n", current_solver); */
        next_solver = solver_fns[current_solver](sstate);

        if (next_solver == DIFF_MAX) {
/*            fprintf(stderr, "Current solver failed\n"); */
            if (current_solver < diff && current_solver + 1 < DIFF_MAX) {
                /* Try next beefier solver */
                next_solver = current_solver + 1;
            } else {
/*                fprintf(stderr, "Doing loop deductions\n"); */
                next_solver = loop_deductions(sstate);
            }
        }

        if (sstate->solver_status == SOLVER_SOLVED || 
            sstate->solver_status == SOLVER_AMBIGUOUS) {
/*            fprintf(stderr, "Solver completed\n"); */
            break;
        }

        /* Once we've looped over all permitted solvers then the loop
         * deductions without making any progress, we'll exit this while loop */
        current_solver = next_solver;
    } while (current_solver < DIFF_MAX);

    if (sstate->solver_status == SOLVER_SOLVED ||
        sstate->solver_status == SOLVER_AMBIGUOUS) {
        /* s/LINE_UNKNOWN/LINE_NO/g */
        array_setall(sstate->state->hl, LINE_UNKNOWN, LINE_NO, 
                     HL_COUNT(sstate->state));
        array_setall(sstate->state->vl, LINE_UNKNOWN, LINE_NO, 
                     VL_COUNT(sstate->state));
        return sstate;
    }

    /* Perform recursive calls */
    if (sstate->recursion_remaining) {
        sstate_saved = dup_solver_state(sstate);

        sstate->recursion_remaining--;

        recursive_soln_count = 0;
        sstate_rec_solved = NULL;

        /* Memory management: 
         * sstate_saved won't be modified but needs to be freed when we have
         * finished with it.
         * sstate is expected to contain our 'best' solution by the time we
         * finish this section of code.  It's the thing we'll try adding lines
         * to, seeing if they make it more solvable.
         * If sstate_rec_solved is non-NULL, it will supersede sstate
         * eventually.  sstate_tmp should not hold a value persistently.
         */

        /* NB SOLVER_AMBIGUOUS is like SOLVER_SOLVED except the solver is aware
         * of the possibility of additional solutions.  So as soon as we have a
         * SOLVER_AMBIGUOUS we can safely propagate it back to our caller, but
         * if we get a SOLVER_SOLVED we want to keep trying in case we find
         * further solutions and have to mark it ambiguous.
         */

#define DO_RECURSIVE_CALL(dir_dot) \
    if (dir_dot(sstate->state, i, j) == LINE_UNKNOWN) { \
        debug(("Trying " #dir_dot " at [%d,%d]\n", i, j)); \
        LV_##dir_dot(sstate->state, i, j) = LINE_YES; \
        sstate_tmp = solve_game_rec(sstate, diff); \
        switch (sstate_tmp->solver_status) { \
            case SOLVER_AMBIGUOUS: \
                debug(("Solver ambiguous, returning\n")); \
                sstate_rec_solved = sstate_tmp; \
                goto finished_recursion; \
            case SOLVER_SOLVED: \
                switch (++recursive_soln_count) { \
                    case 1: \
                        debug(("One solution found\n")); \
                        sstate_rec_solved = sstate_tmp; \
                        break; \
                    case 2: \
                        debug(("Ambiguous solutions found\n")); \
                        free_solver_state(sstate_tmp); \
                        sstate_rec_solved->solver_status = SOLVER_AMBIGUOUS; \
                        goto finished_recursion; \
                    default: \
                        assert(!"recursive_soln_count out of range"); \
                        break; \
                } \
                break; \
            case SOLVER_MISTAKE: \
                debug(("Non-solution found\n")); \
                free_solver_state(sstate_tmp); \
                free_solver_state(sstate_saved); \
                LV_##dir_dot(sstate->state, i, j) = LINE_NO; \
                goto nonrecursive_solver; \
            case SOLVER_INCOMPLETE: \
                debug(("Recursive step inconclusive\n")); \
                free_solver_state(sstate_tmp); \
                break; \
        } \
        free_solver_state(sstate); \
        sstate = dup_solver_state(sstate_saved); \
    }
       
       FORALL_DOTS(state, i, j) {
           /* Only perform recursive calls on 'loose ends' */
           if (DOT_YES_COUNT(sstate, i, j) == 1) {
               DO_RECURSIVE_CALL(LEFTOF_DOT);
               DO_RECURSIVE_CALL(RIGHTOF_DOT);
               DO_RECURSIVE_CALL(ABOVE_DOT);
               DO_RECURSIVE_CALL(BELOW_DOT);
           }
       }

finished_recursion:

       if (sstate_rec_solved) {
           free_solver_state(sstate);
           sstate = sstate_rec_solved;
       } 
    }

    return sstate;
}

#if 0
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b) \
               if (sstate->normal->dot_atmostone[i+a + (sstate->state->w + 1) * (j+b)] & \
                   1<<dline) { \
                   if (square_order(sstate->state, i, j,  LINE_UNKNOWN) - 1 == \
                       CLUE_AT(sstate->state, i, j) - '0') { \
                       square_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_YES); \
                           /* XXX the following may overwrite known data! */ \
                       dir1_sq(sstate->state, i, j) = LINE_UNKNOWN; \
                       dir2_sq(sstate->state, i, j) = LINE_UNKNOWN; \
                   } \
               }
               SQUARE_DLINES;
#undef HANDLE_DLINE
#endif

static char *solve_game(game_state *state, game_state *currstate,
                        char *aux, char **error)
{
    char *soln = NULL;
    solver_state *sstate, *new_sstate;

    sstate = new_solver_state(state, DIFF_MAX);
    new_sstate = solve_game_rec(sstate, DIFF_MAX);

    if (new_sstate->solver_status == SOLVER_SOLVED) {
        soln = encode_solve_move(new_sstate->state);
    } else if (new_sstate->solver_status == SOLVER_AMBIGUOUS) {
        soln = encode_solve_move(new_sstate->state);
        /**error = "Solver found ambiguous solutions"; */
    } else {
        soln = encode_solve_move(new_sstate->state);
        /**error = "Solver failed"; */
    }

    free_solver_state(new_sstate);
    free_solver_state(sstate);

    return soln;
}

/* ----------------------------------------------------------------------
 * Drawing and mouse-handling
 */

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
                            int x, int y, int button)
{
    int hl_selected;
    int i, j, p, q; 
    char *ret, buf[80];
    char button_char = ' ';
    enum line_state old_state;

    button &= ~MOD_MASK;

    /* Around each line is a diamond-shaped region where points within that
     * region are closer to this line than any other.  We assume any click
     * within a line's diamond was meant for that line.  It would all be a lot
     * simpler if the / and % operators respected modulo arithmetic properly
     * for negative numbers. */
    
    x -= BORDER;
    y -= BORDER;

    /* Get the coordinates of the square the click was in */
    i = (x + TILE_SIZE) / TILE_SIZE - 1; 
    j = (y + TILE_SIZE) / TILE_SIZE - 1;

    /* Get the precise position inside square [i,j] */
    p = (x + TILE_SIZE) % TILE_SIZE;
    q = (y + TILE_SIZE) % TILE_SIZE;

    /* After this bit of magic [i,j] will correspond to the point either above
     * or to the left of the line selected */
    if (p > q) { 
        if (TILE_SIZE - p > q) {
            hl_selected = TRUE;
        } else {
            hl_selected = FALSE;
            ++i;
        }
    } else {
        if (TILE_SIZE - q > p) {
            hl_selected = FALSE;
        } else {
            hl_selected = TRUE;
            ++j;
        }
    }

    if (i < 0 || j < 0)
        return NULL;

    if (hl_selected) {
        if (i >= state->w || j >= state->h + 1)
            return NULL;
    } else { 
        if (i >= state->w + 1 || j >= state->h)
            return NULL;
    }

    /* I think it's only possible to play this game with mouse clicks, sorry */
    /* Maybe will add mouse drag support some time */
    if (hl_selected)
        old_state = RIGHTOF_DOT(state, i, j);
    else
        old_state = BELOW_DOT(state, i, j);

    switch (button) {
        case LEFT_BUTTON:
            switch (old_state) {
                case LINE_UNKNOWN:
                    button_char = 'y';
                    break;
                case LINE_YES:
                case LINE_NO:
                    button_char = 'u';
                    break;
            }
            break;
        case MIDDLE_BUTTON:
            button_char = 'u';
            break;
        case RIGHT_BUTTON:
            switch (old_state) {
                case LINE_UNKNOWN:
                    button_char = 'n';
                    break;
                case LINE_NO:
                case LINE_YES:
                    button_char = 'u';
                    break;
            }
            break;
        default:
            return NULL;
    }


    sprintf(buf, "%d,%d%c%c", i, j, (int)(hl_selected ? 'h' : 'v'), (int)button_char);
    ret = dupstr(buf);

    return ret;
}

static game_state *execute_move(game_state *state, char *move)
{
    int i, j;
    game_state *newstate = dup_game(state);

    if (move[0] == 'S') {
        move++;
        newstate->cheated = TRUE;
    }

    while (*move) {
        i = atoi(move);
        move = strchr(move, ',');
        if (!move)
            goto fail;
        j = atoi(++move);
        move += strspn(move, "1234567890");
        switch (*(move++)) {
            case 'h':
                if (i >= newstate->w || j > newstate->h)
                    goto fail;
                switch (*(move++)) {
                    case 'y':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_YES;
                        break;
                    case 'n':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_NO;
                        break;
                    case 'u':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_UNKNOWN;
                        break;
                    default:
                        goto fail;
                }
                break;
            case 'v':
                if (i > newstate->w || j >= newstate->h)
                    goto fail;
                switch (*(move++)) {
                    case 'y':
                        LV_BELOW_DOT(newstate, i, j) = LINE_YES;
                        break;
                    case 'n':
                        LV_BELOW_DOT(newstate, i, j) = LINE_NO;
                        break;
                    case 'u':
                        LV_BELOW_DOT(newstate, i, j) = LINE_UNKNOWN;
                        break;
                    default:
                        goto fail;
                }
                break;
            default:
                goto fail;
        }
    }

    /*
     * Check for completion.
     */
    i = 0;                   /* placate optimiser */
    for (j = 0; j <= newstate->h; j++) {
        for (i = 0; i < newstate->w; i++)
            if (LV_RIGHTOF_DOT(newstate, i, j) == LINE_YES)
                break;
        if (i < newstate->w)
            break;
    }
    if (j <= newstate->h) {
        int prevdir = 'R';
        int x = i, y = j;
        int looplen, count;

        /*
         * We've found a horizontal edge at (i,j). Follow it round
         * to see if it's part of a loop.
         */
        looplen = 0;
        while (1) {
            int order = dot_order(newstate, x, y, LINE_YES);
            if (order != 2)
                goto completion_check_done;

            if (LEFTOF_DOT(newstate, x, y) == LINE_YES && prevdir != 'L') {
                x--;
                prevdir = 'R';
            } else if (RIGHTOF_DOT(newstate, x, y) == LINE_YES &&
                       prevdir != 'R') {
                x++;
                prevdir = 'L';
            } else if (ABOVE_DOT(newstate, x, y) == LINE_YES &&
                       prevdir != 'U') {
                y--;
                prevdir = 'D';
            } else if (BELOW_DOT(newstate, x, y) == LINE_YES && 
                       prevdir != 'D') {
                y++;
                prevdir = 'U';
            } else {
                assert(!"Can't happen");   /* dot_order guarantees success */
            }

            looplen++;

            if (x == i && y == j)
                break;
        }

        if (x != i || y != j || looplen == 0)
            goto completion_check_done;

        /*
         * We've traced our way round a loop, and we know how many
         * line segments were involved. Count _all_ the line
         * segments in the grid, to see if the loop includes them
         * all.
         */
        count = 0;
        FORALL_DOTS(newstate, i, j) {
            count += ((RIGHTOF_DOT(newstate, i, j) == LINE_YES) +
                      (BELOW_DOT(newstate, i, j) == LINE_YES));
        }
        assert(count >= looplen);
        if (count != looplen)
            goto completion_check_done;

        /*
         * The grid contains one closed loop and nothing else.
         * Check that all the clues are satisfied.
         */
        FORALL_SQUARES(newstate, i, j) {
            if (CLUE_AT(newstate, i, j) >= 0) {
                if (square_order(newstate, i, j, LINE_YES) != 
                    CLUE_AT(newstate, i, j)) {
                    goto completion_check_done;
                }
            }
        }

        /*
         * Completed!
         */
        newstate->solved = TRUE;
    }

completion_check_done:
    return newstate;

fail:
    free_game(newstate);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */
static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
                        game_state *state, int dir, game_ui *ui,
                        float animtime, float flashtime)
{
    int i, j, n;
    char c[2];
    int line_colour, flash_changed;
    int clue_mistake;

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed and
         * can vary with front ends. To be on the safe side, all games
         * should start by drawing a big background-colour rectangle
         * covering the whole window.
         */
        draw_rect(dr, 0, 0, SIZE(state->w), SIZE(state->h), COL_BACKGROUND);

        /* Draw dots */
        FORALL_DOTS(state, i, j) {
            draw_rect(dr, 
                      BORDER + i * TILE_SIZE - LINEWIDTH/2,
                      BORDER + j * TILE_SIZE - LINEWIDTH/2,
                      LINEWIDTH, LINEWIDTH, COL_FOREGROUND);
        }

        /* Draw clues */
        FORALL_SQUARES(state, i, j) {
            c[0] = CLUE2CHAR(CLUE_AT(state, i, j));
            c[1] = '\0';
            draw_text(dr, 
                      BORDER + i * TILE_SIZE + TILE_SIZE/2,
                      BORDER + j * TILE_SIZE + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE/2, 
                      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_FOREGROUND, c);
        }
        draw_update(dr, 0, 0,
                    state->w * TILE_SIZE + 2*BORDER + 1,
                    state->h * TILE_SIZE + 2*BORDER + 1);
        ds->started = TRUE;
    }

    if (flashtime > 0 && 
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3)) {
        flash_changed = !ds->flashing;
        ds->flashing = TRUE;
        line_colour = COL_HIGHLIGHT;
    } else {
        flash_changed = ds->flashing;
        ds->flashing = FALSE;
        line_colour = COL_FOREGROUND;
    }

#define CROSS_SIZE (3 * LINEWIDTH / 2)
    
    /* Redraw clue colours if necessary */
    FORALL_SQUARES(state, i, j) {
        n = CLUE_AT(state, i, j);
        if (n < 0)
            continue;

        assert(n >= 0 && n <= 4);

        c[0] = CLUE2CHAR(CLUE_AT(state, i, j));
        c[1] = '\0';

        clue_mistake = (square_order(state, i, j, LINE_YES) > n ||
                        square_order(state, i, j, LINE_NO ) > (4-n));

        if (clue_mistake != ds->clue_error[SQUARE_INDEX(state, i, j)]) {
            draw_rect(dr, 
                      BORDER + i * TILE_SIZE + CROSS_SIZE,
                      BORDER + j * TILE_SIZE + CROSS_SIZE,
                      TILE_SIZE - CROSS_SIZE * 2, TILE_SIZE - CROSS_SIZE * 2,
                      COL_BACKGROUND);
            draw_text(dr, 
                      BORDER + i * TILE_SIZE + TILE_SIZE/2,
                      BORDER + j * TILE_SIZE + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE/2, 
                      ALIGN_VCENTRE | ALIGN_HCENTRE, 
                      clue_mistake ? COL_MISTAKE : COL_FOREGROUND, c);
            draw_update(dr, i * TILE_SIZE + BORDER, j * TILE_SIZE + BORDER,
                        TILE_SIZE, TILE_SIZE);

            ds->clue_error[SQUARE_INDEX(state, i, j)] = clue_mistake;
        }
    }

    /* I've also had a request to colour lines red if they make a non-solution
     * loop, or if more than two lines go into any point.  I think that would
     * be good some time. */

#define CLEAR_VL(i, j) \
    do { \
       draw_rect(dr, \
                 BORDER + i * TILE_SIZE - CROSS_SIZE, \
                 BORDER + j * TILE_SIZE + LINEWIDTH - LINEWIDTH/2, \
                 CROSS_SIZE * 2, \
                 TILE_SIZE - LINEWIDTH, \
                 COL_BACKGROUND); \
        draw_update(dr, \
                    BORDER + i * TILE_SIZE - CROSS_SIZE, \
                    BORDER + j * TILE_SIZE - CROSS_SIZE, \
                    CROSS_SIZE*2, \
                    TILE_SIZE + CROSS_SIZE*2); \
    } while (0)

#define CLEAR_HL(i, j) \
    do { \
       draw_rect(dr, \
                 BORDER + i * TILE_SIZE + LINEWIDTH - LINEWIDTH/2, \
                 BORDER + j * TILE_SIZE - CROSS_SIZE, \
                 TILE_SIZE - LINEWIDTH, \
                 CROSS_SIZE * 2, \
                 COL_BACKGROUND); \
       draw_update(dr, \
                   BORDER + i * TILE_SIZE - CROSS_SIZE, \
                   BORDER + j * TILE_SIZE - CROSS_SIZE, \
                   TILE_SIZE + CROSS_SIZE*2, \
                   CROSS_SIZE*2); \
    } while (0)

    /* Vertical lines */
    FORALL_VL(state, i, j) {
        switch (BELOW_DOT(state, i, j)) {
            case LINE_UNKNOWN:
                if (ds->vl[VL_INDEX(state, i, j)] != BELOW_DOT(state, i, j)) {
                    CLEAR_VL(i, j);
                }
                break;
            case LINE_YES:
                if (ds->vl[VL_INDEX(state, i, j)] != BELOW_DOT(state, i, j) ||
                    flash_changed) {
                    CLEAR_VL(i, j);
                    draw_rect(dr,
                              BORDER + i * TILE_SIZE - LINEWIDTH/2,
                              BORDER + j * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,
                              LINEWIDTH, TILE_SIZE - LINEWIDTH, 
                              line_colour);
                }
                break;
            case LINE_NO:
                if (ds->vl[VL_INDEX(state, i, j)] != BELOW_DOT(state, i, j)) {
                    CLEAR_VL(i, j);
                    draw_line(dr,
                              BORDER + i * TILE_SIZE - CROSS_SIZE,
                              BORDER + j * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                              BORDER + i * TILE_SIZE + CROSS_SIZE - 1,
                              BORDER + j * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                              COL_FOREGROUND);
                    draw_line(dr,
                              BORDER + i * TILE_SIZE + CROSS_SIZE - 1,
                              BORDER + j * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                              BORDER + i * TILE_SIZE - CROSS_SIZE,
                              BORDER + j * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                              COL_FOREGROUND);
                }
                break;
        }
        ds->vl[VL_INDEX(state, i, j)] = BELOW_DOT(state, i, j);
    }

    /* Horizontal lines */
    FORALL_HL(state, i, j) {
        switch (RIGHTOF_DOT(state, i, j)) {
            case LINE_UNKNOWN:
                if (ds->hl[HL_INDEX(state, i, j)] != RIGHTOF_DOT(state, i, j)) {
                    CLEAR_HL(i, j);
                }
                break;
            case LINE_YES:
                if (ds->hl[HL_INDEX(state, i, j)] != RIGHTOF_DOT(state, i, j) ||
                    flash_changed) {
                    CLEAR_HL(i, j);
                    draw_rect(dr,
                              BORDER + i * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,
                              BORDER + j * TILE_SIZE - LINEWIDTH/2,
                              TILE_SIZE - LINEWIDTH, LINEWIDTH, 
                              line_colour);
                }
                break; 
            case LINE_NO:
                if (ds->hl[HL_INDEX(state, i, j)] != RIGHTOF_DOT(state, i, j)) {
                    CLEAR_HL(i, j);
                    draw_line(dr,
                              BORDER + i * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                              BORDER + j * TILE_SIZE + CROSS_SIZE - 1,
                              BORDER + i * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                              BORDER + j * TILE_SIZE - CROSS_SIZE,
                              COL_FOREGROUND);
                    draw_line(dr,
                              BORDER + i * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                              BORDER + j * TILE_SIZE - CROSS_SIZE,
                              BORDER + i * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                              BORDER + j * TILE_SIZE + CROSS_SIZE - 1,
                              COL_FOREGROUND);
                    break;
                }
        }
        ds->hl[HL_INDEX(state, i, j)] = RIGHTOF_DOT(state, i, j);
    }
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
                               int dir, game_ui *ui)
{
    if (!oldstate->solved  &&  newstate->solved &&
        !oldstate->cheated && !newstate->cheated) {
        return FLASH_TIME;
    }

    return 0.0F;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 7mm squares by default.
     */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int x, y;
    game_drawstate ads, *ds = &ads;

    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Dots. I'll deliberately make the dots a bit wider than the
     * lines, so you can still see them. (And also because it's
     * annoyingly tricky to make them _exactly_ the same size...)
     */
    FORALL_DOTS(state, x, y) {
        draw_circle(dr, BORDER + x * TILE_SIZE, BORDER + y * TILE_SIZE,
                    LINEWIDTH, ink, ink);
    }

    /*
     * Clues.
     */
    FORALL_SQUARES(state, x, y) {
        if (CLUE_AT(state, x, y) >= 0) {
            char c[2];

            c[0] = CLUE2CHAR(CLUE_AT(state, x, y));
            c[1] = '\0';
            draw_text(dr, 
                      BORDER + x * TILE_SIZE + TILE_SIZE/2,
                      BORDER + y * TILE_SIZE + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE/2, 
                      ALIGN_VCENTRE | ALIGN_HCENTRE, ink, c);
        }
    }

    /*
     * Lines. (At the moment, I'm not bothering with crosses.)
     */
    FORALL_HL(state, x, y) {
        if (RIGHTOF_DOT(state, x, y) == LINE_YES)
        draw_rect(dr, BORDER + x * TILE_SIZE,
                  BORDER + y * TILE_SIZE - LINEWIDTH/2,
                  TILE_SIZE, (LINEWIDTH/2) * 2 + 1, ink);
    }

    FORALL_VL(state, x, y) {
        if (BELOW_DOT(state, x, y) == LINE_YES)
        draw_rect(dr, BORDER + x * TILE_SIZE - LINEWIDTH/2,
                  BORDER + y * TILE_SIZE,
                  (LINEWIDTH/2) * 2 + 1, TILE_SIZE, ink);
    }
}

#ifdef COMBINED
#define thegame loopy
#endif

const struct game thegame = {
    "Loopy", "games.loopy",
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
    1, solve_game,
    TRUE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    TRUE, FALSE, game_print_size, game_print,
    FALSE /* wants_statusbar */,
    FALSE, game_timing_state,
    0,                                       /* mouse_priorities */
};
