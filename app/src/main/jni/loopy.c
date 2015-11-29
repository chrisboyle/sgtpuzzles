/*
 * loopy.c:
 *
 * An implementation of the Nikoli game 'Loop the loop'.
 * (c) Mike Pinna, 2005, 2006
 * Substantially rewritten to allowing for more general types of grid.
 * (c) Lambros Lambrou 2008
 *
 * vim: set shiftwidth=4 :set textwidth=80:
 */

/*
 * Possible future solver enhancements:
 * 
 *  - There's an interesting deductive technique which makes use
 *    of topology rather than just graph theory. Each _face_ in
 *    the grid is either inside or outside the loop; you can tell
 *    that two faces are on the same side of the loop if they're
 *    separated by a LINE_NO (or, more generally, by a path
 *    crossing no LINE_UNKNOWNs and an even number of LINE_YESes),
 *    and on the opposite side of the loop if they're separated by
 *    a LINE_YES (or an odd number of LINE_YESes and no
 *    LINE_UNKNOWNs). Oh, and any face separated from the outside
 *    of the grid by a LINE_YES or a LINE_NO is on the inside or
 *    outside respectively. So if you can track this for all
 *    faces, you figure out the state of the line between a pair
 *    once their relative insideness is known.
 *     + The way I envisage this working is simply to keep an edsf
 * 	 of all _faces_, which indicates whether they're on
 * 	 opposite sides of the loop from one another. We also
 * 	 include a special entry in the edsf for the infinite
 * 	 exterior "face".
 *     + So, the simple way to do this is to just go through the
 * 	 edges: every time we see an edge in a state other than
 * 	 LINE_UNKNOWN which separates two faces that aren't in the
 * 	 same edsf class, we can rectify that by merging the
 * 	 classes. Then, conversely, an edge in LINE_UNKNOWN state
 * 	 which separates two faces that _are_ in the same edsf
 * 	 class can immediately have its state determined.
 *     + But you can go one better, if you're prepared to loop
 * 	 over all _pairs_ of edges. Suppose we have edges A and B,
 * 	 which respectively separate faces A1,A2 and B1,B2.
 * 	 Suppose that A,B are in the same edge-edsf class and that
 * 	 A1,B1 (wlog) are in the same face-edsf class; then we can
 * 	 immediately place A2,B2 into the same face-edsf class (as
 * 	 each other, not as A1 and A2) one way round or the other.
 * 	 And conversely again, if A1,B1 are in the same face-edsf
 * 	 class and so are A2,B2, then we can put A,B into the same
 * 	 face-edsf class.
 * 	  * Of course, this deduction requires a quadratic-time
 * 	    loop over all pairs of edges in the grid, so it should
 * 	    be reserved until there's nothing easier left to be
 * 	    done.
 * 
 *  - The generalised grid support has made me (SGT) notice a
 *    possible extension to the loop-avoidance code. When you have
 *    a path of connected edges such that no other edges at all
 *    are incident on any vertex in the middle of the path - or,
 *    alternatively, such that any such edges are already known to
 *    be LINE_NO - then you know those edges are either all
 *    LINE_YES or all LINE_NO. Hence you can mentally merge the
 *    entire path into a single long curly edge for the purposes
 *    of loop avoidance, and look directly at whether or not the
 *    extreme endpoints of the path are connected by some other
 *    route. I find this coming up fairly often when I play on the
 *    octagonal grid setting, so it might be worth implementing in
 *    the solver.
 *
 *  - (Just a speed optimisation.)  Consider some todo list queue where every
 *    time we modify something we mark it for consideration by other bits of
 *    the solver, to save iteration over things that have already been done.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"
#include "grid.h"
#include "loopgen.h"

/* Debugging options */

/*
#define DEBUG_CACHES
#define SHOW_WORKING
#define DEBUG_DLINES
*/

/* ----------------------------------------------------------------------
 * Struct, enum and function declarations
 */

enum {
    COL_BACKGROUND,
    COL_FOREGROUND,
    COL_LINEUNKNOWN,
    COL_HIGHLIGHT,
    COL_MISTAKE,
    COL_SATISFIED,
    COL_CURSOR,
    COL_FAINT,
    NCOLOURS
};

struct game_state {
    grid *game_grid; /* ref-counted (internally) */

    /* Put -1 in a face that doesn't get a clue */
    signed char *clues;

    /* Array of line states, to store whether each line is
     * YES, NO or UNKNOWN */
    char *lines;

    unsigned char *line_errors;

    int solved;
    int cheated;

    /* Used in game_text_format(), so that it knows what type of
     * grid it's trying to render as ASCII text. */
    int grid_type;
};

enum solver_status {
    SOLVER_SOLVED,    /* This is the only solution the solver could find */
    SOLVER_MISTAKE,   /* This is definitely not a solution */
    SOLVER_AMBIGUOUS, /* This _might_ be an ambiguous solution */
    SOLVER_INCOMPLETE /* This may be a partial solution */
};

/* ------ Solver state ------ */
typedef struct solver_state {
    game_state *state;
    enum solver_status solver_status;
    /* NB looplen is the number of dots that are joined together at a point, ie a
     * looplen of 1 means there are no lines to a particular dot */
    int *looplen;

    /* Difficulty level of solver.  Used by solver functions that want to
     * vary their behaviour depending on the requested difficulty level. */
    int diff;

    /* caches */
    char *dot_yes_count;
    char *dot_no_count;
    char *face_yes_count;
    char *face_no_count;
    char *dot_solved, *face_solved;
    int *dotdsf;

    /* Information for Normal level deductions:
     * For each dline, store a bitmask for whether we know:
     * (bit 0) at least one is YES
     * (bit 1) at most one is YES */
    char *dlines;

    /* Hard level information */
    int *linedsf;
} solver_state;

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */

#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(NORMAL,Normal,n) \
    A(TRICKY,Tricky,t) \
    A(HARD,Hard,h)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFF_MAX };
static char const *const diffnames[] = { DIFFLIST(TITLE) };
static char const diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

/*
 * Solver routines, sorted roughly in order of computational cost.
 * The solver will run the faster deductions first, and slower deductions are
 * only invoked when the faster deductions are unable to make progress.
 * Each function is associated with a difficulty level, so that the generated
 * puzzles are solvable by applying only the functions with the chosen
 * difficulty level or lower.
 */
#define SOLVERLIST(A) \
    A(trivial_deductions, DIFF_EASY) \
    A(dline_deductions, DIFF_NORMAL) \
    A(linedsf_deductions, DIFF_HARD) \
    A(loop_deductions, DIFF_EASY)
#define SOLVER_FN_DECL(fn,diff) static int fn(solver_state *);
#define SOLVER_FN(fn,diff) &fn,
#define SOLVER_DIFF(fn,diff) diff,
SOLVERLIST(SOLVER_FN_DECL)
static int (*(solver_fns[]))(solver_state *) = { SOLVERLIST(SOLVER_FN) };
static int const solver_diffs[] = { SOLVERLIST(SOLVER_DIFF) };
static const int NUM_SOLVERS = sizeof(solver_diffs)/sizeof(*solver_diffs);

struct game_params {
    int w, h;
    int diff;
    int type;
};

/* line_drawstate is the same as line_state, but with the extra ERROR
 * possibility.  The drawing code copies line_state to line_drawstate,
 * except in the case that the line is an error. */
enum line_state { LINE_YES, LINE_UNKNOWN, LINE_NO };
enum line_drawstate { DS_LINE_YES, DS_LINE_UNKNOWN,
                      DS_LINE_NO, DS_LINE_ERROR };

#define OPP(line_state) \
    (2 - line_state)

/* Define this to display the crosshair cursor. The highlighted-edge
 * cursor is always displayed (this is the thing you're actually
 * interested in). */
#define CURSOR_IS_VISIBLE 1

struct game_drawstate {
    int started;
    int tilesize;
    int flashing;
    int *textx, *texty;
    char *lines;
    char *clue_error;
    char *clue_satisfied;

    int cur_visible;
#ifdef CURSOR_IS_VISIBLE
    int cur_bl_x, cur_bl_y;
    blitter *cur_bl;
#endif
    grid_edge *cur_edge;
};

static char *validate_desc(const game_params *params, const char *desc);
static int dot_order(const game_state* state, int i, char line_type);
static int face_order(const game_state* state, int i, char line_type);
static solver_state *solve_game_rec(const solver_state *sstate);

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate);
#else
#define check_caches(s)
#endif

/* ------- List of grid generators ------- */
#define GRIDLIST(A) \
    A(Squares,GRID_SQUARE,3,3) \
    A(Triangular,GRID_TRIANGULAR,3,3) \
    A(Honeycomb,GRID_HONEYCOMB,3,3) \
    A(Snub-Square,GRID_SNUBSQUARE,3,3) \
    A(Cairo,GRID_CAIRO,3,4) \
    A(Great-Hexagonal,GRID_GREATHEXAGONAL,3,3) \
    A(Octagonal,GRID_OCTAGONAL,3,3) \
    A(Kites,GRID_KITE,3,3) \
    A(Floret,GRID_FLORET,1,2) \
    A(Dodecagonal,GRID_DODECAGONAL,2,2) \
    A(Great-Dodecagonal,GRID_GREATDODECAGONAL,2,2) \
    A(Penrose (kite/dart),GRID_PENROSE_P2,3,3) \
    A(Penrose (rhombs),GRID_PENROSE_P3,3,3)
/* _("Squares"), _("Triangular"), _("Honeycomb"), _("Snub-Square"), _("Cairo"), _("Great-Hexagonal"), _("Octagonal"), _("Kites"), _("Floret"), _("Dodecagonal"), _("Great-Dodecagonal"), _("Penrose (kite/dart)"), _("Penrose (rhombs)") */

#define GRID_NAME(title,type,amin,omin) #title,
#define GRID_CONFIG(title,type,amin,omin) ":" #title
#define GRID_TYPE(title,type,amin,omin) type,
#define GRID_SIZES(title,type,amin,omin) \
    {amin, omin},
static char const *const gridnames[] = { GRIDLIST(GRID_NAME) };
#define GRID_CONFIGS GRIDLIST(GRID_CONFIG)
static grid_type grid_types[] = { GRIDLIST(GRID_TYPE) };
#define NUM_GRID_TYPES (sizeof(grid_types) / sizeof(grid_types[0]))
static const struct {
    int amin, omin;
} grid_size_limits[] = { GRIDLIST(GRID_SIZES) };

/* Generates a (dynamically allocated) new grid, according to the
 * type and size requested in params.  Does nothing if the grid is already
 * generated. */
static grid *loopy_generate_grid(const game_params *params,
                                 const char *grid_desc)
{
    return grid_new(grid_types[params->type], params->w, params->h, grid_desc);
}

/* ----------------------------------------------------------------------
 * Preprocessor magic
 */

/* General constants */
#define PREFERRED_TILE_SIZE 32
#define BORDER(tilesize) ((tilesize) / 2)
#define FLASH_TIME 0.5F

#define BIT_SET(field, bit) ((field) & (1<<(bit)))

#define SET_BIT(field, bit)  (BIT_SET(field, bit) ? FALSE : \
                              ((field) |= (1<<(bit)), TRUE))

#define CLEAR_BIT(field, bit) (BIT_SET(field, bit) ? \
                               ((field) &= ~(1<<(bit)), TRUE) : FALSE)

#define CLUE2CHAR(c) \
    ((c < 0) ? ' ' : c < 10 ? c + '0' : c - 10 + 'A')

/* ----------------------------------------------------------------------
 * General struct manipulation and other straightforward code
 */

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->game_grid = state->game_grid;
    ret->game_grid->refcount++;

    ret->solved = state->solved;
    ret->cheated = state->cheated;

    ret->clues = snewn(state->game_grid->num_faces, signed char);
    memcpy(ret->clues, state->clues, state->game_grid->num_faces);

    ret->lines = snewn(state->game_grid->num_edges, char);
    memcpy(ret->lines, state->lines, state->game_grid->num_edges);

    ret->line_errors = snewn(state->game_grid->num_edges, unsigned char);
    memcpy(ret->line_errors, state->line_errors, state->game_grid->num_edges);

    ret->grid_type = state->grid_type;
    return ret;
}

static void free_game(game_state *state)
{
    if (state) {
        grid_free(state->game_grid);
        sfree(state->clues);
        sfree(state->lines);
        sfree(state->line_errors);
        sfree(state);
    }
}

static solver_state *new_solver_state(const game_state *state, int diff) {
    int i;
    int num_dots = state->game_grid->num_dots;
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;
    solver_state *ret = snew(solver_state);

    ret->state = dup_game(state);

    ret->solver_status = SOLVER_INCOMPLETE;
    ret->diff = diff;

    ret->dotdsf = snew_dsf(num_dots);
    ret->looplen = snewn(num_dots, int);

    for (i = 0; i < num_dots; i++) {
        ret->looplen[i] = 1;
    }

    ret->dot_solved = snewn(num_dots, char);
    ret->face_solved = snewn(num_faces, char);
    memset(ret->dot_solved, FALSE, num_dots);
    memset(ret->face_solved, FALSE, num_faces);

    ret->dot_yes_count = snewn(num_dots, char);
    memset(ret->dot_yes_count, 0, num_dots);
    ret->dot_no_count = snewn(num_dots, char);
    memset(ret->dot_no_count, 0, num_dots);
    ret->face_yes_count = snewn(num_faces, char);
    memset(ret->face_yes_count, 0, num_faces);
    ret->face_no_count = snewn(num_faces, char);
    memset(ret->face_no_count, 0, num_faces);

    if (diff < DIFF_NORMAL) {
        ret->dlines = NULL;
    } else {
        ret->dlines = snewn(2*num_edges, char);
        memset(ret->dlines, 0, 2*num_edges);
    }

    if (diff < DIFF_HARD) {
        ret->linedsf = NULL;
    } else {
        ret->linedsf = snew_dsf(state->game_grid->num_edges);
    }

    return ret;
}

static void free_solver_state(solver_state *sstate) {
    if (sstate) {
        free_game(sstate->state);
        sfree(sstate->dotdsf);
        sfree(sstate->looplen);
        sfree(sstate->dot_solved);
        sfree(sstate->face_solved);
        sfree(sstate->dot_yes_count);
        sfree(sstate->dot_no_count);
        sfree(sstate->face_yes_count);
        sfree(sstate->face_no_count);

        /* OK, because sfree(NULL) is a no-op */
        sfree(sstate->dlines);
        sfree(sstate->linedsf);

        sfree(sstate);
    }
}

static solver_state *dup_solver_state(const solver_state *sstate) {
    game_state *state = sstate->state;
    int num_dots = state->game_grid->num_dots;
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;
    solver_state *ret = snew(solver_state);

    ret->state = state = dup_game(sstate->state);

    ret->solver_status = sstate->solver_status;
    ret->diff = sstate->diff;

    ret->dotdsf = snewn(num_dots, int);
    ret->looplen = snewn(num_dots, int);
    memcpy(ret->dotdsf, sstate->dotdsf,
           num_dots * sizeof(int));
    memcpy(ret->looplen, sstate->looplen,
           num_dots * sizeof(int));

    ret->dot_solved = snewn(num_dots, char);
    ret->face_solved = snewn(num_faces, char);
    memcpy(ret->dot_solved, sstate->dot_solved, num_dots);
    memcpy(ret->face_solved, sstate->face_solved, num_faces);

    ret->dot_yes_count = snewn(num_dots, char);
    memcpy(ret->dot_yes_count, sstate->dot_yes_count, num_dots);
    ret->dot_no_count = snewn(num_dots, char);
    memcpy(ret->dot_no_count, sstate->dot_no_count, num_dots);

    ret->face_yes_count = snewn(num_faces, char);
    memcpy(ret->face_yes_count, sstate->face_yes_count, num_faces);
    ret->face_no_count = snewn(num_faces, char);
    memcpy(ret->face_no_count, sstate->face_no_count, num_faces);

    if (sstate->dlines) {
        ret->dlines = snewn(2*num_edges, char);
        memcpy(ret->dlines, sstate->dlines,
               2*num_edges);
    } else {
        ret->dlines = NULL;
    }

    if (sstate->linedsf) {
        ret->linedsf = snewn(num_edges, int);
        memcpy(ret->linedsf, sstate->linedsf,
               num_edges * sizeof(int));
    } else {
        ret->linedsf = NULL;
    }

    return ret;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

#if defined SLOW_SYSTEM || defined SMALL_SCREEN
    ret->h = 7;
    ret->w = 7;
#else
    ret->h = 10;
    ret->w = 10;
#endif
    ret->diff = DIFF_EASY;
    ret->type = 0;

    return ret;
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);

    *ret = *params;                       /* structure copy */
    return ret;
}

static const game_params presets[] = {
#ifdef SMALL_SCREEN
    {  7,  7, DIFF_EASY, 0 },
    {  7,  7, DIFF_NORMAL, 0 },
    {  7,  7, DIFF_HARD, 0 },
    {  7,  7, DIFF_HARD, 1 },
    {  7,  7, DIFF_HARD, 2 },
    {  5,  5, DIFF_HARD, 3 },
    {  7,  7, DIFF_HARD, 4 },
    {  5,  4, DIFF_HARD, 5 },
    {  5,  5, DIFF_HARD, 6 },
    {  5,  5, DIFF_HARD, 7 },
    {  3,  3, DIFF_HARD, 8 },
    {  3,  3, DIFF_HARD, 9 },
    {  3,  3, DIFF_HARD, 10 },
    {  6,  6, DIFF_HARD, 11 },
    {  6,  6, DIFF_HARD, 12 },
#else
    {  7,  7, DIFF_EASY, 0 },
    {  10,  10, DIFF_EASY, 0 },
    {  7,  7, DIFF_NORMAL, 0 },
    {  10,  10, DIFF_NORMAL, 0 },
    {  7,  7, DIFF_HARD, 0 },
    {  10,  10, DIFF_HARD, 0 },
    {  10,  10, DIFF_HARD, 1 },
    {  12,  10, DIFF_HARD, 2 },
    {  7,  7, DIFF_HARD, 3 },
    {  9,  9, DIFF_HARD, 4 },
    {  5,  4, DIFF_HARD, 5 },
    {  7,  7, DIFF_HARD, 6 },
    {  5,  5, DIFF_HARD, 7 },
    {  5,  5, DIFF_HARD, 8 },
    {  5,  4, DIFF_HARD, 9 },
    {  5,  4, DIFF_HARD, 10 },
    {  10, 10, DIFF_HARD, 11 },
    {  10, 10, DIFF_HARD, 12 }
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
    sprintf(buf, "%dx%d %s - %s", tmppar->h, tmppar->w,
            _(gridnames[tmppar->type]), diffnames[tmppar->diff]);
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
    params->diff = DIFF_EASY;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 't') {
        string++;
        params->type = atoi(string);
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

static char *encode_params(const game_params *params, int full)
{
    char str[80];
    sprintf(str, "%dx%dt%d", params->w, params->h, params->type);
    if (full)
        sprintf(str + strlen(str), "d%c", diffchars[params->diff]);
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = _("Width");
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = _("Height");
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = _("Grid type");
    ret[2].type = C_CHOICES;
    ret[2].sval = GRID_CONFIGS;
    ret[2].ival = params->type;

    ret[3].name = _("Difficulty");
    ret[3].type = C_CHOICES;
    ret[3].sval = DIFFCONFIG;
    ret[3].ival = params->diff;

    ret[4].name = NULL;
    ret[4].type = C_END;
    ret[4].sval = NULL;
    ret[4].ival = 0;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->type = cfg[2].ival;
    ret->diff = cfg[3].ival;

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    static char err[128];
    int l = grid_size_limits[params->type].amin;
    if (params->type < 0 || params->type >= NUM_GRID_TYPES)
        return _("Illegal grid type");
    if (params->w < l || params->h < l) {
        sprintf(err, _("Width and height for this grid type must both be at least %d"), l);
        return err;
    }
    l = grid_size_limits[params->type].omin;
    if (params->w < l && params->h < l) {
        sprintf(err, _("At least one of width and height for this grid type must be at least %d"), l);
        return err;
    }

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
    grid *g = state->game_grid;
    char *retval;
    int num_faces = g->num_faces;
    char *description = snewn(num_faces + 1, char);
    char *dp = description;
    int empty_count = 0;
    int i;

    for (i = 0; i < num_faces; i++) {
        if (state->clues[i] < 0) {
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
            dp += sprintf(dp, "%c", (int)CLUE2CHAR(state->clues[i]));
        }
    }

    if (empty_count)
        dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));

    retval = dupstr(description);
    sfree(description);

    return retval;
}

#define GRID_DESC_SEP '_'

/* Splits up a (optional) grid_desc from the game desc. Returns the
 * grid_desc (which needs freeing) and updates the desc pointer to
 * start of real desc, or returns NULL if no desc. */
static char *extract_grid_desc(const char **desc)
{
    char *sep = strchr(*desc, GRID_DESC_SEP), *gd;
    int gd_len;

    if (!sep) return NULL;

    gd_len = sep - (*desc);
    gd = snewn(gd_len+1, char);
    memcpy(gd, *desc, gd_len);
    gd[gd_len] = '\0';

    *desc = sep+1;

    return gd;
}

/* We require that the params pass the test in validate_params and that the
 * description fills the entire game area */
static char *validate_desc(const game_params *params, const char *desc)
{
    int count = 0;
    grid *g;
    char *grid_desc, *ret;

    /* It's pretty inefficient to do this just for validation. All we need to
     * know is the precise number of faces. */
    grid_desc = extract_grid_desc(&desc);
    ret = grid_validate_desc(grid_types[params->type], params->w, params->h, grid_desc);
    if (ret) return ret;

    g = loopy_generate_grid(params, grid_desc);
    if (grid_desc) sfree(grid_desc);

    for (; *desc; ++desc) {
        if ((*desc >= '0' && *desc <= '9') || (*desc >= 'A' && *desc <= 'Z')) {
            count++;
            continue;
        }
        if (*desc >= 'a') {
            count += *desc - 'a' + 1;
            continue;
        }
        return _("Unknown character in description");
    }

    if (count < g->num_faces)
        return _("Description too short for board size");
    if (count > g->num_faces)
        return _("Description too long for board size");

    grid_free(g);

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
    int len;
    char *ret, *p;
    int i;
    int num_edges = state->game_grid->num_edges;

    /* This is going to return a string representing the moves needed to set
     * every line in a grid to be the same as the ones in 'state'.  The exact
     * length of this string is predictable. */

    len = 1;  /* Count the 'S' prefix */
    /* Numbers in all lines */
    len += len_0_to_n(num_edges);
    /* For each line we also have a letter */
    len += num_edges;

    ret = snewn(len + 1, char);
    p = ret;

    p += sprintf(p, "S");

    for (i = 0; i < num_edges; i++) {
        switch (state->lines[i]) {
	  case LINE_YES:
	    p += sprintf(p, "%dy", i);
	    break;
	  case LINE_NO:
	    p += sprintf(p, "%dn", i);
	    break;
        }
    }

    /* No point in doing sums like that if they're going to be wrong */
    assert(strlen(ret) <= (size_t)len);
    return ret;
}

struct game_ui {
    int cur_x, cur_y; /* grid coordinates. */
    int cur_visible;
};


static game_ui *new_ui(const game_state *state)
{
    grid *g = state->game_grid;
    game_ui *ui = snew(game_ui);
    ui->cur_x = (g->lowest_x + g->highest_x)/2;
    ui->cur_y = (g->lowest_y + g->highest_y)/2;
    ui->cur_visible = 0;
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

static void android_cursor_visibility(game_ui *ui, int visible)
{
    ui->cur_visible = visible;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
#ifdef ANDROID
    if (newstate->solved && ! newstate->cheated && oldstate && ! oldstate->solved) android_completed();
#endif
}

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    int grid_width, grid_height, rendered_width, rendered_height;
    int g_tilesize;

    grid_compute_size(grid_types[params->type], params->w, params->h,
                      &g_tilesize, &grid_width, &grid_height);

    /* multiply first to minimise rounding error on integer division */
    rendered_width = grid_width * tilesize / g_tilesize;
    rendered_height = grid_height * tilesize / g_tilesize;
    *x = rendered_width + 2 * BORDER(tilesize) + 1;
    *y = rendered_height + 2 * BORDER(tilesize) + 1;
}

#ifdef CURSOR_IS_VISIBLE
#define BLITTER_HSZ ((ds->tilesize)/8+1)
#define BLITTER_SZ (2*(BLITTER_HSZ)+1)

#define CUR_HSZ 1
#define CUR_SZ 3
#endif

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;

#ifdef CURSOR_IS_VISIBLE
    assert(!ds->cur_bl);
    ds->cur_bl = blitter_new(dr, BLITTER_SZ, BLITTER_SZ);
#endif
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_FOREGROUND * 3 + 0] = 0.0F;
    ret[COL_FOREGROUND * 3 + 1] = 0.0F;
    ret[COL_FOREGROUND * 3 + 2] = 0.0F;

    /*
     * We want COL_LINEUNKNOWN to be a yellow which is a bit darker
     * than the background. (I previously set it to 0.8,0.8,0, but
     * found that this went badly with the 0.8,0.8,0.8 favoured as a
     * background by the Java frontend.)
     */
    ret[COL_LINEUNKNOWN * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.9F;
    ret[COL_LINEUNKNOWN * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.9F;
    ret[COL_LINEUNKNOWN * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_MISTAKE * 3 + 0] = 1.0F;
    ret[COL_MISTAKE * 3 + 1] = 0.0F;
    ret[COL_MISTAKE * 3 + 2] = 0.0F;

    ret[COL_CURSOR * 3 + 0] = 0.5F;
    ret[COL_CURSOR * 3 + 1] = 0.5F;
    ret[COL_CURSOR * 3 + 2] = 1.0F;

    ret[COL_SATISFIED * 3 + 0] = 0.0F;
    ret[COL_SATISFIED * 3 + 1] = 0.0F;
    ret[COL_SATISFIED * 3 + 2] = 0.0F;

    /* We want the faint lines to be a bit darker than the background.
     * Except if the background is pretty dark already; then it ought to be a
     * bit lighter.  Oy vey.
     */
    ret[COL_FAINT * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.9F;
    ret[COL_FAINT * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] * 0.9F;
    ret[COL_FAINT * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] * 0.9F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;
    int i;

    ds->tilesize = 0;
    ds->started = 0;
    ds->lines = snewn(num_edges, char);
    ds->clue_error = snewn(num_faces, char);
    ds->clue_satisfied = snewn(num_faces, char);
    ds->textx = snewn(num_faces, int);
    ds->texty = snewn(num_faces, int);
    ds->flashing = 0;

    memset(ds->lines, LINE_UNKNOWN, num_edges);
    memset(ds->clue_error, 0, num_faces);
    memset(ds->clue_satisfied, 0, num_faces);
    for (i = 0; i < num_faces; i++)
        ds->textx[i] = ds->texty[i] = -1;

    ds->cur_visible = 0;
#ifdef CURSOR_IS_VISIBLE
    ds->cur_bl_x = ds->cur_bl_y = 0;
    ds->cur_bl = NULL;
#endif
    ds->cur_edge = NULL;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
#ifdef CURSOR_IS_VISIBLE
    if (ds->cur_bl) blitter_free(dr, ds->cur_bl);
#endif

    sfree(ds->textx);
    sfree(ds->texty);
    sfree(ds->clue_error);
    sfree(ds->clue_satisfied);
    sfree(ds->lines);
    sfree(ds);
}

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static int game_can_format_as_text_now(const game_params *params)
{
    if (params->type != 0)
        return FALSE;
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    int w, h, W, H;
    int x, y, i;
    int cell_size;
    char *ret;
    grid *g = state->game_grid;
    grid_face *f;

    assert(state->grid_type == 0);

    /* Work out the basic size unit */
    f = g->faces; /* first face */
    assert(f->order == 4);
    /* The dots are ordered clockwise, so the two opposite
     * corners are guaranteed to span the square */
    cell_size = abs(f->dots[0]->x - f->dots[2]->x);

    w = (g->highest_x - g->lowest_x) / cell_size;
    h = (g->highest_y - g->lowest_y) / cell_size;

    /* Create a blank "canvas" to "draw" on */
    W = 2 * w + 2;
    H = 2 * h + 1;
    ret = snewn(W * H + 1, char);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W-1; x++) {
            ret[y*W + x] = ' ';
        }
        ret[y*W + W-1] = '\n';
    }
    ret[H*W] = '\0';

    /* Fill in edge info */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        /* Cell coordinates, from (0,0) to (w-1,h-1) */
        int x1 = (e->dot1->x - g->lowest_x) / cell_size;
        int x2 = (e->dot2->x - g->lowest_x) / cell_size;
        int y1 = (e->dot1->y - g->lowest_y) / cell_size;
        int y2 = (e->dot2->y - g->lowest_y) / cell_size;
        /* Midpoint, in canvas coordinates (canvas coordinates are just twice
         * cell coordinates) */
        x = x1 + x2;
        y = y1 + y2;
        switch (state->lines[i]) {
	  case LINE_YES:
	    ret[y*W + x] = (y1 == y2) ? '-' : '|';
	    break;
	  case LINE_NO:
	    ret[y*W + x] = 'x';
	    break;
	  case LINE_UNKNOWN:
	    break; /* already a space */
	  default:
	    assert(!"Illegal line state");
        }
    }

    /* Fill in clues */
    for (i = 0; i < g->num_faces; i++) {
	int x1, x2, y1, y2;

        f = g->faces + i;
        assert(f->order == 4);
        /* Cell coordinates, from (0,0) to (w-1,h-1) */
	x1 = (f->dots[0]->x - g->lowest_x) / cell_size;
	x2 = (f->dots[2]->x - g->lowest_x) / cell_size;
	y1 = (f->dots[0]->y - g->lowest_y) / cell_size;
	y2 = (f->dots[2]->y - g->lowest_y) / cell_size;
        /* Midpoint, in canvas coordinates */
        x = x1 + x2;
        y = y1 + y2;
        ret[y*W + x] = CLUE2CHAR(state->clues[i]);
    }
    return ret;
}

/* ----------------------------------------------------------------------
 * Debug code
 */

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate)
{
    int i;
    const game_state *state = sstate->state;
    const grid *g = state->game_grid;

    for (i = 0; i < g->num_dots; i++) {
        assert(dot_order(state, i, LINE_YES) == sstate->dot_yes_count[i]);
        assert(dot_order(state, i, LINE_NO) == sstate->dot_no_count[i]);
    }

    for (i = 0; i < g->num_faces; i++) {
        assert(face_order(state, i, LINE_YES) == sstate->face_yes_count[i]);
        assert(face_order(state, i, LINE_NO) == sstate->face_no_count[i]);
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

/* Sets the line (with index i) to the new state 'line_new', and updates
 * the cached counts of any affected faces and dots.
 * Returns TRUE if this actually changed the line's state. */
static int solver_set_line(solver_state *sstate, int i,
                           enum line_state line_new
#ifdef SHOW_WORKING
			   , const char *reason
#endif
			   )
{
    game_state *state = sstate->state;
    grid *g;
    grid_edge *e;

    assert(line_new != LINE_UNKNOWN);

    check_caches(sstate);

    if (state->lines[i] == line_new) {
        return FALSE; /* nothing changed */
    }
    state->lines[i] = line_new;

#ifdef SHOW_WORKING
    fprintf(stderr, "solver: set line [%d] to %s (%s)\n",
            i, line_new == LINE_YES ? "YES" : "NO",
            reason);
#endif

    g = state->game_grid;
    e = g->edges + i;

    /* Update the cache for both dots and both faces affected by this. */
    if (line_new == LINE_YES) {
        sstate->dot_yes_count[e->dot1 - g->dots]++;
        sstate->dot_yes_count[e->dot2 - g->dots]++;
        if (e->face1) {
            sstate->face_yes_count[e->face1 - g->faces]++;
        }
        if (e->face2) {
            sstate->face_yes_count[e->face2 - g->faces]++;
        }
    } else {
        sstate->dot_no_count[e->dot1 - g->dots]++;
        sstate->dot_no_count[e->dot2 - g->dots]++;
        if (e->face1) {
            sstate->face_no_count[e->face1 - g->faces]++;
        }
        if (e->face2) {
            sstate->face_no_count[e->face2 - g->faces]++;
        }
    }

    check_caches(sstate);
    return TRUE;
}

#ifdef SHOW_WORKING
#define solver_set_line(a, b, c) \
    solver_set_line(a, b, c, __FUNCTION__)
#endif

/*
 * Merge two dots due to the existence of an edge between them.
 * Updates the dsf tracking equivalence classes, and keeps track of
 * the length of path each dot is currently a part of.
 * Returns TRUE if the dots were already linked, ie if they are part of a
 * closed loop, and false otherwise.
 */
static int merge_dots(solver_state *sstate, int edge_index)
{
    int i, j, len;
    grid *g = sstate->state->game_grid;
    grid_edge *e = g->edges + edge_index;

    i = e->dot1 - g->dots;
    j = e->dot2 - g->dots;

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

/* Merge two lines because the solver has deduced that they must be either
 * identical or opposite.   Returns TRUE if this is new information, otherwise
 * FALSE. */
static int merge_lines(solver_state *sstate, int i, int j, int inverse
#ifdef SHOW_WORKING
                       , const char *reason
#endif
		       )
{
    int inv_tmp;

    assert(i < sstate->state->game_grid->num_edges);
    assert(j < sstate->state->game_grid->num_edges);

    i = edsf_canonify(sstate->linedsf, i, &inv_tmp);
    inverse ^= inv_tmp;
    j = edsf_canonify(sstate->linedsf, j, &inv_tmp);
    inverse ^= inv_tmp;

    edsf_merge(sstate->linedsf, i, j, inverse);

#ifdef SHOW_WORKING
    if (i != j) {
        fprintf(stderr, "%s [%d] [%d] %s(%s)\n",
                __FUNCTION__, i, j,
                inverse ? "inverse " : "", reason);
    }
#endif
    return (i != j);
}

#ifdef SHOW_WORKING
#define merge_lines(a, b, c, d) \
    merge_lines(a, b, c, d, __FUNCTION__)
#endif

/* Count the number of lines of a particular type currently going into the
 * given dot. */
static int dot_order(const game_state* state, int dot, char line_type)
{
    int n = 0;
    grid *g = state->game_grid;
    grid_dot *d = g->dots + dot;
    int i;

    for (i = 0; i < d->order; i++) {
        grid_edge *e = d->edges[i];
        if (state->lines[e - g->edges] == line_type)
            ++n;
    }
    return n;
}

/* Count the number of lines of a particular type currently surrounding the
 * given face */
static int face_order(const game_state* state, int face, char line_type)
{
    int n = 0;
    grid *g = state->game_grid;
    grid_face *f = g->faces + face;
    int i;

    for (i = 0; i < f->order; i++) {
        grid_edge *e = f->edges[i];
        if (state->lines[e - g->edges] == line_type)
            ++n;
    }
    return n;
}

/* Set all lines bordering a dot of type old_type to type new_type
 * Return value tells caller whether this function actually did anything */
static int dot_setall(solver_state *sstate, int dot,
		      char old_type, char new_type)
{
    int retval = FALSE, r;
    game_state *state = sstate->state;
    grid *g;
    grid_dot *d;
    int i;

    if (old_type == new_type)
        return FALSE;

    g = state->game_grid;
    d = g->dots + dot;

    for (i = 0; i < d->order; i++) {
        int line_index = d->edges[i] - g->edges;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r == TRUE);
            retval = TRUE;
        }
    }
    return retval;
}

/* Set all lines bordering a face of type old_type to type new_type */
static int face_setall(solver_state *sstate, int face,
                       char old_type, char new_type)
{
    int retval = FALSE, r;
    game_state *state = sstate->state;
    grid *g;
    grid_face *f;
    int i;

    if (old_type == new_type)
        return FALSE;

    g = state->game_grid;
    f = g->faces + face;

    for (i = 0; i < f->order; i++) {
        int line_index = f->edges[i] - g->edges;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r == TRUE);
            retval = TRUE;
        }
    }
    return retval;
}

/* ----------------------------------------------------------------------
 * Loop generation and clue removal
 */

static void add_full_clues(game_state *state, random_state *rs)
{
    signed char *clues = state->clues;
    grid *g = state->game_grid;
    char *board = snewn(g->num_faces, char);
    int i;

    generate_loop(g, board, rs, NULL, NULL);

    /* Fill out all the clues by initialising to 0, then iterating over
     * all edges and incrementing each clue as we find edges that border
     * between BLACK/WHITE faces.  While we're at it, we verify that the
     * algorithm does work, and there aren't any GREY faces still there. */
    memset(clues, 0, g->num_faces);
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        grid_face *f1 = e->face1;
        grid_face *f2 = e->face2;
        enum face_colour c1 = FACE_COLOUR(f1);
        enum face_colour c2 = FACE_COLOUR(f2);
        assert(c1 != FACE_GREY);
        assert(c2 != FACE_GREY);
        if (c1 != c2) {
            if (f1) clues[f1 - g->faces]++;
            if (f2) clues[f2 - g->faces]++;
        }
    }
    sfree(board);
}


static int game_has_unique_soln(const game_state *state, int diff)
{
    int ret;
    solver_state *sstate_new;
    solver_state *sstate = new_solver_state((game_state *)state, diff);

    sstate_new = solve_game_rec(sstate);

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
    int *face_list;
    int num_faces = state->game_grid->num_faces;
    game_state *ret = dup_game(state), *saved_ret;
    int n;

    /* We need to remove some clues.  We'll do this by forming a list of all
     * available clues, shuffling it, then going along one at a
     * time clearing each clue in turn for which doing so doesn't render the
     * board unsolvable. */
    face_list = snewn(num_faces, int);
    for (n = 0; n < num_faces; ++n) {
        face_list[n] = n;
    }

    shuffle(face_list, num_faces, sizeof(int), rs);

    for (n = 0; n < num_faces; ++n) {
        saved_ret = dup_game(ret);
        ret->clues[face_list[n]] = -1;

        if (game_has_unique_soln(ret, diff)) {
            free_game(saved_ret);
        } else {
            free_game(ret);
            ret = saved_ret;
        }
    }
    sfree(face_list);

    return ret;
}


static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    /* solution and description both use run-length encoding in obvious ways */
    char *retval, *game_desc, *grid_desc;
    grid *g;
    game_state *state = snew(game_state);
    game_state *state_new;

    grid_desc = grid_new_desc(grid_types[params->type], params->w, params->h, rs);
    state->game_grid = g = loopy_generate_grid(params, grid_desc);

    state->clues = snewn(g->num_faces, signed char);
    state->lines = snewn(g->num_edges, char);
    state->line_errors = snewn(g->num_edges, unsigned char);

    state->grid_type = params->type;

    newboard_please:

    memset(state->lines, LINE_UNKNOWN, g->num_edges);
    memset(state->line_errors, 0, g->num_edges);

    state->solved = state->cheated = FALSE;

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

    game_desc = state_to_text(state);

    free_game(state);

    if (grid_desc) {
        retval = snewn(strlen(grid_desc) + 1 + strlen(game_desc) + 1, char);
        sprintf(retval, "%s%c%s", grid_desc, (int)GRID_DESC_SEP, game_desc);
        sfree(grid_desc);
        sfree(game_desc);
    } else {
        retval = game_desc;
    }

    assert(!validate_desc(params, retval));

    return retval;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int i;
    game_state *state = snew(game_state);
    int empties_to_make = 0;
    int n,n2;
    const char *dp;
    char *grid_desc;
    grid *g;
    int num_faces, num_edges;

    grid_desc = extract_grid_desc(&desc);
    state->game_grid = g = loopy_generate_grid(params, grid_desc);
    if (grid_desc) sfree(grid_desc);

    dp = desc;

    num_faces = g->num_faces;
    num_edges = g->num_edges;

    state->clues = snewn(num_faces, signed char);
    state->lines = snewn(num_edges, char);
    state->line_errors = snewn(num_edges, unsigned char);

    state->solved = state->cheated = FALSE;

    state->grid_type = params->type;

    for (i = 0; i < num_faces; i++) {
        if (empties_to_make) {
            empties_to_make--;
            state->clues[i] = -1;
            continue;
        }

        assert(*dp);
        n = *dp - '0';
        n2 = *dp - 'A' + 10;
        if (n >= 0 && n < 10) {
            state->clues[i] = n;
	} else if (n2 >= 10 && n2 < 36) {
            state->clues[i] = n2;
        } else {
            n = *dp - 'a' + 1;
            assert(n > 0);
            state->clues[i] = -1;
            empties_to_make = n - 1;
        }
        ++dp;
    }

    memset(state->lines, LINE_UNKNOWN, num_edges);
    memset(state->line_errors, 0, num_edges);
    return state;
}

/* Calculates the line_errors data, and checks if the current state is a
 * solution */
static int check_completion(game_state *state)
{
    grid *g = state->game_grid;
    int *dsf;
    int num_faces = g->num_faces;
    int i;
    int infinite_area, finite_area;
    int loops_found = 0;
    int found_edge_not_in_loop = FALSE;

    memset(state->line_errors, 0, g->num_edges);

    /* LL implementation of SGT's idea:
     * A loop will partition the grid into an inside and an outside.
     * If there is more than one loop, the grid will be partitioned into
     * even more distinct regions.  We can therefore track equivalence of
     * faces, by saying that two faces are equivalent when there is a non-YES
     * edge between them.
     * We could keep track of the number of connected components, by counting
     * the number of dsf-merges that aren't no-ops.
     * But we're only interested in 3 separate cases:
     * no loops, one loop, more than one loop.
     *
     * No loops: all faces are equivalent to the infinite face.
     * One loop: only two equivalence classes - finite and infinite.
     * >= 2 loops: there are 2 distinct finite regions.
     *
     * So we simply make two passes through all the edges.
     * In the first pass, we dsf-merge the two faces bordering each non-YES
     * edge.
     * In the second pass, we look for YES-edges bordering:
     * a) two non-equivalent faces.
     * b) two non-equivalent faces, and one of them is part of a different
     *    finite area from the first finite area we've seen.
     *
     * An occurrence of a) means there is at least one loop.
     * An occurrence of b) means there is more than one loop.
     * Edges satisfying a) are marked as errors.
     *
     * While we're at it, we set a flag if we find a YES edge that is not
     * part of a loop.
     * This information will help decide, if there's a single loop, whether it
     * is a candidate for being a solution (that is, all YES edges are part of
     * this loop).
     *
     * If there is a candidate loop, we then go through all clues and check
     * they are all satisfied.  If so, we have found a solution and we can
     * unmark all line_errors.
     */
    
    /* Infinite face is at the end - its index is num_faces.
     * This macro is just to make this obvious! */
    #define INF_FACE num_faces
    dsf = snewn(num_faces + 1, int);
    dsf_init(dsf, num_faces + 1);
    
    /* First pass */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int f1 = e->face1 ? e->face1 - g->faces : INF_FACE;
        int f2 = e->face2 ? e->face2 - g->faces : INF_FACE;
        if (state->lines[i] != LINE_YES)
            dsf_merge(dsf, f1, f2);
    }
    
    /* Second pass */
    infinite_area = dsf_canonify(dsf, INF_FACE);
    finite_area = -1;
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int f1 = e->face1 ? e->face1 - g->faces : INF_FACE;
        int can1 = dsf_canonify(dsf, f1);
        int f2 = e->face2 ? e->face2 - g->faces : INF_FACE;
        int can2 = dsf_canonify(dsf, f2);
        if (state->lines[i] != LINE_YES) continue;

        if (can1 == can2) {
            /* Faces are equivalent, so this edge not part of a loop */
            found_edge_not_in_loop = TRUE;
            continue;
        }
        state->line_errors[i] = TRUE;
        if (loops_found == 0) loops_found = 1;

        /* Don't bother with further checks if we've already found 2 loops */
        if (loops_found == 2) continue;

        if (finite_area == -1) {
            /* Found our first finite area */
            if (can1 != infinite_area)
                finite_area = can1;
            else
                finite_area = can2;
        }

        /* Have we found a second area? */
        if (finite_area != -1) {
            if (can1 != infinite_area && can1 != finite_area) {
                loops_found = 2;
                continue;
            }
            if (can2 != infinite_area && can2 != finite_area) {
                loops_found = 2;
            }
        }
    }

/*
    printf("loops_found = %d\n", loops_found);
    printf("found_edge_not_in_loop = %s\n",
        found_edge_not_in_loop ? "TRUE" : "FALSE");
*/

    sfree(dsf); /* No longer need the dsf */
    
    /* Have we found a candidate loop? */
    if (loops_found == 1 && !found_edge_not_in_loop) {
        /* Yes, so check all clues are satisfied */
        int found_clue_violation = FALSE;
        for (i = 0; i < num_faces; i++) {
            int c = state->clues[i];
            if (c >= 0) {
                if (face_order(state, i, LINE_YES) != c) {
                    found_clue_violation = TRUE;
                    break;
                }
            }
        }
        
        if (!found_clue_violation) {
            /* The loop is good */
            memset(state->line_errors, 0, g->num_edges);
            return TRUE; /* No need to bother checking for dot violations */
        }
    }

    /* Check for dot violations */
    for (i = 0; i < g->num_dots; i++) {
        int yes = dot_order(state, i, LINE_YES);
        int unknown = dot_order(state, i, LINE_UNKNOWN);
        if ((yes == 1 && unknown == 0) || (yes >= 3)) {
            /* violation, so mark all YES edges as errors */
            grid_dot *d = g->dots + i;
            int j;
            for (j = 0; j < d->order; j++) {
                int e = d->edges[j] - g->edges;
                if (state->lines[e] == LINE_YES)
                    state->line_errors[e] = TRUE;
            }
        }
    }
    return FALSE;
}

/* ----------------------------------------------------------------------
 * Solver logic
 *
 * Our solver modes operate as follows.  Each mode also uses the modes above it.
 *
 *   Easy Mode
 *   Just implement the rules of the game.
 *
 *   Normal and Tricky Modes
 *   For each (adjacent) pair of lines through each dot we store a bit for
 *   whether at least one of them is on and whether at most one is on.  (If we
 *   know both or neither is on that's already stored more directly.)
 *
 *   Advanced Mode
 *   Use edsf data structure to make equivalence classes of lines that are
 *   known identical to or opposite to one another.
 */


/* DLines:
 * For general grids, we consider "dlines" to be pairs of lines joined
 * at a dot.  The lines must be adjacent around the dot, so we can think of
 * a dline as being a dot+face combination.  Or, a dot+edge combination where
 * the second edge is taken to be the next clockwise edge from the dot.
 * Original loopy code didn't have this extra restriction of the lines being
 * adjacent.  From my tests with square grids, this extra restriction seems to
 * take little, if anything, away from the quality of the puzzles.
 * A dline can be uniquely identified by an edge/dot combination, given that
 * a dline-pair always goes clockwise around its common dot.  The edge/dot
 * combination can be represented by an edge/bool combination - if bool is
 * TRUE, use edge->dot1 else use edge->dot2.  So the total number of dlines is
 * exactly twice the number of edges in the grid - although the dlines
 * spanning the infinite face are not all that useful to the solver.
 * Note that, by convention, a dline goes clockwise around its common dot,
 * which means the dline goes anti-clockwise around its common face.
 */

/* Helper functions for obtaining an index into an array of dlines, given
 * various information.  We assume the grid layout conventions about how
 * the various lists are interleaved - see grid_make_consistent() for
 * details. */

/* i points to the first edge of the dline pair, reading clockwise around
 * the dot. */
static int dline_index_from_dot(grid *g, grid_dot *d, int i)
{
    grid_edge *e = d->edges[i];
    int ret;
#ifdef DEBUG_DLINES
    grid_edge *e2;
    int i2 = i+1;
    if (i2 == d->order) i2 = 0;
    e2 = d->edges[i2];
#endif
    ret = 2 * (e - g->edges) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_dot: d=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(d - g->dots), i, (int)(e - g->edges),
           (int)(e2 - g->edges), ret);
#endif
    return ret;
}
/* i points to the second edge of the dline pair, reading clockwise around
 * the face.  That is, the edges of the dline, starting at edge{i}, read
 * anti-clockwise around the face.  By layout conventions, the common dot
 * of the dline will be f->dots[i] */
static int dline_index_from_face(grid *g, grid_face *f, int i)
{
    grid_edge *e = f->edges[i];
    grid_dot *d = f->dots[i];
    int ret;
#ifdef DEBUG_DLINES
    grid_edge *e2;
    int i2 = i - 1;
    if (i2 < 0) i2 += f->order;
    e2 = f->edges[i2];
#endif
    ret = 2 * (e - g->edges) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_face: f=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(f - g->faces), i, (int)(e - g->edges),
           (int)(e2 - g->edges), ret);
#endif
    return ret;
}
static int is_atleastone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 0);
}
static int set_atleastone(char *dline_array, int index)
{
    return SET_BIT(dline_array[index], 0);
}
static int is_atmostone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 1);
}
static int set_atmostone(char *dline_array, int index)
{
    return SET_BIT(dline_array[index], 1);
}

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

/* Helper, called when doing dline dot deductions, in the case where we
 * have 4 UNKNOWNs, and two of them (adjacent) have *exactly* one YES between
 * them (because of dline atmostone/atleastone).
 * On entry, edge points to the first of these two UNKNOWNs.  This function
 * will find the opposite UNKNOWNS (if they are adjacent to one another)
 * and set their corresponding dline to atleastone.  (Setting atmostone
 * already happens in earlier dline deductions) */
static int dline_set_opp_atleastone(solver_state *sstate,
                                    grid_dot *d, int edge)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    int N = d->order;
    int opp, opp2;
    for (opp = 0; opp < N; opp++) {
        int opp_dline_index;
        if (opp == edge || opp == edge+1 || opp == edge-1)
            continue;
        if (opp == 0 && edge == N-1)
            continue;
        if (opp == N-1 && edge == 0)
            continue;
        opp2 = opp + 1;
        if (opp2 == N) opp2 = 0;
        /* Check if opp, opp2 point to LINE_UNKNOWNs */
        if (state->lines[d->edges[opp] - g->edges] != LINE_UNKNOWN)
            continue;
        if (state->lines[d->edges[opp2] - g->edges] != LINE_UNKNOWN)
            continue;
        /* Found opposite UNKNOWNS and they're next to each other */
        opp_dline_index = dline_index_from_dot(g, d, opp);
        return set_atleastone(sstate->dlines, opp_dline_index);
    }
    return FALSE;
}


/* Set pairs of lines around this face which are known to be identical, to
 * the given line_state */
static int face_setall_identical(solver_state *sstate, int face_index,
                                 enum line_state line_new)
{
    /* can[dir] contains the canonical line associated with the line in
     * direction dir from the square in question.  Similarly inv[dir] is
     * whether or not the line in question is inverse to its canonical
     * element. */
    int retval = FALSE;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    grid_face *f = g->faces + face_index;
    int N = f->order;
    int i, j;
    int can1, can2, inv1, inv2;

    for (i = 0; i < N; i++) {
        int line1_index = f->edges[i] - g->edges;
        if (state->lines[line1_index] != LINE_UNKNOWN)
            continue;
        for (j = i + 1; j < N; j++) {
            int line2_index = f->edges[j] - g->edges;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;

            /* Found two UNKNOWNS */
            can1 = edsf_canonify(sstate->linedsf, line1_index, &inv1);
            can2 = edsf_canonify(sstate->linedsf, line2_index, &inv2);
            if (can1 == can2 && inv1 == inv2) {
                solver_set_line(sstate, line1_index, line_new);
                solver_set_line(sstate, line2_index, line_new);
            }
        }
    }
    return retval;
}

/* Given a dot or face, and a count of LINE_UNKNOWNs, find them and
 * return the edge indices into e. */
static void find_unknowns(game_state *state,
    grid_edge **edge_list, /* Edge list to search (from a face or a dot) */
    int expected_count, /* Number of UNKNOWNs (comes from solver's cache) */
    int *e /* Returned edge indices */)
{
    int c = 0;
    grid *g = state->game_grid;
    while (c < expected_count) {
        int line_index = *edge_list - g->edges;
        if (state->lines[line_index] == LINE_UNKNOWN) {
            e[c] = line_index;
            c++;
        }
        ++edge_list;
    }
}

/* If we have a list of edges, and we know whether the number of YESs should
 * be odd or even, and there are only a few UNKNOWNs, we can do some simple
 * linedsf deductions.  This can be used for both face and dot deductions.
 * Returns the difficulty level of the next solver that should be used,
 * or DIFF_MAX if no progress was made. */
static int parity_deductions(solver_state *sstate,
    grid_edge **edge_list, /* Edge list (from a face or a dot) */
    int total_parity, /* Expected number of YESs modulo 2 (either 0 or 1) */
    int unknown_count)
{
    game_state *state = sstate->state;
    int diff = DIFF_MAX;
    int *linedsf = sstate->linedsf;

    if (unknown_count == 2) {
        /* Lines are known alike/opposite, depending on inv. */
        int e[2];
        find_unknowns(state, edge_list, 2, e);
        if (merge_lines(sstate, e[0], e[1], total_parity))
            diff = min(diff, DIFF_HARD);
    } else if (unknown_count == 3) {
        int e[3];
        int can[3]; /* canonical edges */
        int inv[3]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 3, e);
        can[0] = edsf_canonify(linedsf, e[0], inv);
        can[1] = edsf_canonify(linedsf, e[1], inv+1);
        can[2] = edsf_canonify(linedsf, e[2], inv+2);
        if (can[0] == can[1]) {
            if (solver_set_line(sstate, e[2], (total_parity^inv[0]^inv[1]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        if (can[0] == can[2]) {
            if (solver_set_line(sstate, e[1], (total_parity^inv[0]^inv[2]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        if (can[1] == can[2]) {
            if (solver_set_line(sstate, e[0], (total_parity^inv[1]^inv[2]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
    } else if (unknown_count == 4) {
        int e[4];
        int can[4]; /* canonical edges */
        int inv[4]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 4, e);
        can[0] = edsf_canonify(linedsf, e[0], inv);
        can[1] = edsf_canonify(linedsf, e[1], inv+1);
        can[2] = edsf_canonify(linedsf, e[2], inv+2);
        can[3] = edsf_canonify(linedsf, e[3], inv+3);
        if (can[0] == can[1]) {
            if (merge_lines(sstate, e[2], e[3], total_parity^inv[0]^inv[1]))
                diff = min(diff, DIFF_HARD);
        } else if (can[0] == can[2]) {
            if (merge_lines(sstate, e[1], e[3], total_parity^inv[0]^inv[2]))
                diff = min(diff, DIFF_HARD);
        } else if (can[0] == can[3]) {
            if (merge_lines(sstate, e[1], e[2], total_parity^inv[0]^inv[3]))
                diff = min(diff, DIFF_HARD);
        } else if (can[1] == can[2]) {
            if (merge_lines(sstate, e[0], e[3], total_parity^inv[1]^inv[2]))
                diff = min(diff, DIFF_HARD);
        } else if (can[1] == can[3]) {
            if (merge_lines(sstate, e[0], e[2], total_parity^inv[1]^inv[3]))
                diff = min(diff, DIFF_HARD);
        } else if (can[2] == can[3]) {
            if (merge_lines(sstate, e[0], e[1], total_parity^inv[2]^inv[3]))
                diff = min(diff, DIFF_HARD);
        }
    }
    return diff;
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

static int trivial_deductions(solver_state *sstate)
{
    int i, current_yes, current_no;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    int diff = DIFF_MAX;

    /* Per-face deductions */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;

        if (sstate->face_solved[i])
            continue;

        current_yes = sstate->face_yes_count[i];
        current_no  = sstate->face_no_count[i];

        if (current_yes + current_no == f->order)  {
            sstate->face_solved[i] = TRUE;
            continue;
        }

        if (state->clues[i] < 0)
            continue;

        /*
         * This code checks whether the numeric clue on a face is so
         * large as to permit all its remaining LINE_UNKNOWNs to be
         * filled in as LINE_YES, or alternatively so small as to
         * permit them all to be filled in as LINE_NO.
         */

        if (state->clues[i] < current_yes) {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (state->clues[i] == current_yes) {
            if (face_setall(sstate, i, LINE_UNKNOWN, LINE_NO))
                diff = min(diff, DIFF_EASY);
            sstate->face_solved[i] = TRUE;
            continue;
        }

        if (f->order - state->clues[i] < current_no) {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (f->order - state->clues[i] == current_no) {
            if (face_setall(sstate, i, LINE_UNKNOWN, LINE_YES))
                diff = min(diff, DIFF_EASY);
            sstate->face_solved[i] = TRUE;
            continue;
        }

        if (f->order - state->clues[i] == current_no + 1 &&
            f->order - current_yes - current_no > 2) {
            /*
             * One small refinement to the above: we also look for any
             * adjacent pair of LINE_UNKNOWNs around the face with
             * some LINE_YES incident on it from elsewhere. If we find
             * one, then we know that pair of LINE_UNKNOWNs can't
             * _both_ be LINE_YES, and hence that pushes us one line
             * closer to being able to determine all the rest.
             */
            int j, k, e1, e2, e, d;

            for (j = 0; j < f->order; j++) {
                e1 = f->edges[j] - g->edges;
                e2 = f->edges[j+1 < f->order ? j+1 : 0] - g->edges;

                if (g->edges[e1].dot1 == g->edges[e2].dot1 ||
                    g->edges[e1].dot1 == g->edges[e2].dot2) {
                    d = g->edges[e1].dot1 - g->dots;
                } else {
                    assert(g->edges[e1].dot2 == g->edges[e2].dot1 ||
                           g->edges[e1].dot2 == g->edges[e2].dot2);
                    d = g->edges[e1].dot2 - g->dots;
                }

                if (state->lines[e1] == LINE_UNKNOWN &&
                    state->lines[e2] == LINE_UNKNOWN) {
                    for (k = 0; k < g->dots[d].order; k++) {
                        int e = g->dots[d].edges[k] - g->edges;
                        if (state->lines[e] == LINE_YES)
                            goto found;    /* multi-level break */
                    }
                }
            }
            continue;

          found:
            /*
             * If we get here, we've found such a pair of edges, and
             * they're e1 and e2.
             */
            for (j = 0; j < f->order; j++) {
                e = f->edges[j] - g->edges;
                if (state->lines[e] == LINE_UNKNOWN && e != e1 && e != e2) {
#ifndef NDEBUG
                    int r =
#endif
                    solver_set_line(sstate, e, LINE_YES);
#ifndef NDEBUG
                    assert(r);
#endif
                    diff = min(diff, DIFF_EASY);
                }
            }
        }
    }

    check_caches(sstate);

    /* Per-dot deductions */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int yes, no, unknown;

        if (sstate->dot_solved[i])
            continue;

        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = d->order - yes - no;

        if (yes == 0) {
            if (unknown == 0) {
                sstate->dot_solved[i] = TRUE;
            } else if (unknown == 1) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
                sstate->dot_solved[i] = TRUE;
            }
        } else if (yes == 1) {
            if (unknown == 0) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            } else if (unknown == 1) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_YES);
                diff = min(diff, DIFF_EASY);
            }
        } else if (yes == 2) {
            if (unknown > 0) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
            }
            sstate->dot_solved[i] = TRUE;
        } else {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
    }

    check_caches(sstate);

    return diff;
}

static int dline_deductions(solver_state *sstate)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    char *dlines = sstate->dlines;
    int i;
    int diff = DIFF_MAX;

    /* ------ Face deductions ------ */

    /* Given a set of dline atmostone/atleastone constraints, need to figure
     * out if we can deduce any further info.  For more general faces than
     * squares, this turns out to be a tricky problem.
     * The approach taken here is to define (per face) NxN matrices:
     * "maxs" and "mins".
     * The entries maxs(j,k) and mins(j,k) define the upper and lower limits
     * for the possible number of edges that are YES between positions j and k
     * going clockwise around the face.  Can think of j and k as marking dots
     * around the face (recall the labelling scheme: edge0 joins dot0 to dot1,
     * edge1 joins dot1 to dot2 etc).
     * Trivially, mins(j,j) = maxs(j,j) = 0, and we don't even bother storing
     * these.  mins(j,j+1) and maxs(j,j+1) are determined by whether edge{j}
     * is YES, NO or UNKNOWN.  mins(j,j+2) and maxs(j,j+2) are related to
     * the dline atmostone/atleastone status for edges j and j+1.
     *
     * Then we calculate the remaining entries recursively.  We definitely
     * know that
     * mins(j,k) >= { mins(j,u) + mins(u,k) } for any u between j and k.
     * This is because any valid placement of YESs between j and k must give
     * a valid placement between j and u, and also between u and k.
     * I believe it's sufficient to use just the two values of u:
     * j+1 and j+2.  Seems to work well in practice - the bounds we compute
     * are rigorous, even if they might not be best-possible.
     *
     * Once we have maxs and mins calculated, we can make inferences about
     * each dline{j,j+1} by looking at the possible complementary edge-counts
     * mins(j+2,j) and maxs(j+2,j) and comparing these with the face clue.
     * As well as dlines, we can make similar inferences about single edges.
     * For example, consider a pentagon with clue 3, and we know at most one
     * of (edge0, edge1) is YES, and at most one of (edge2, edge3) is YES.
     * We could then deduce edge4 is YES, because maxs(0,4) would be 2, so
     * that final edge would have to be YES to make the count up to 3.
     */

    /* Much quicker to allocate arrays on the stack than the heap, so
     * define the largest possible face size, and base our array allocations
     * on that.  We check this with an assertion, in case someone decides to
     * make a grid which has larger faces than this.  Note, this algorithm
     * could get quite expensive if there are many large faces. */
#define MAX_FACE_SIZE 12

    for (i = 0; i < g->num_faces; i++) {
        int maxs[MAX_FACE_SIZE][MAX_FACE_SIZE];
        int mins[MAX_FACE_SIZE][MAX_FACE_SIZE];
        grid_face *f = g->faces + i;
        int N = f->order;
        int j,m;
        int clue = state->clues[i];
        assert(N <= MAX_FACE_SIZE);
        if (sstate->face_solved[i])
            continue;
        if (clue < 0) continue;

        /* Calculate the (j,j+1) entries */
        for (j = 0; j < N; j++) {
            int edge_index = f->edges[j] - g->edges;
            int dline_index;
            enum line_state line1 = state->lines[edge_index];
            enum line_state line2;
            int tmp;
            int k = j + 1;
            if (k >= N) k = 0;
            maxs[j][k] = (line1 == LINE_NO) ? 0 : 1;
            mins[j][k] = (line1 == LINE_YES) ? 1 : 0;
            /* Calculate the (j,j+2) entries */
            dline_index = dline_index_from_face(g, f, k);
            edge_index = f->edges[k] - g->edges;
            line2 = state->lines[edge_index];
            k++;
            if (k >= N) k = 0;

            /* max */
            tmp = 2;
            if (line1 == LINE_NO) tmp--;
            if (line2 == LINE_NO) tmp--;
            if (tmp == 2 && is_atmostone(dlines, dline_index))
                tmp = 1;
            maxs[j][k] = tmp;

            /* min */
            tmp = 0;
            if (line1 == LINE_YES) tmp++;
            if (line2 == LINE_YES) tmp++;
            if (tmp == 0 && is_atleastone(dlines, dline_index))
                tmp = 1;
            mins[j][k] = tmp;
        }

        /* Calculate the (j,j+m) entries for m between 3 and N-1 */
        for (m = 3; m < N; m++) {
            for (j = 0; j < N; j++) {
                int k = j + m;
                int u = j + 1;
                int v = j + 2;
                int tmp;
                if (k >= N) k -= N;
                if (u >= N) u -= N;
                if (v >= N) v -= N;
                maxs[j][k] = maxs[j][u] + maxs[u][k];
                mins[j][k] = mins[j][u] + mins[u][k];
                tmp = maxs[j][v] + maxs[v][k];
                maxs[j][k] = min(maxs[j][k], tmp);
                tmp = mins[j][v] + mins[v][k];
                mins[j][k] = max(mins[j][k], tmp);
            }
        }

        /* See if we can make any deductions */
        for (j = 0; j < N; j++) {
            int k;
            grid_edge *e = f->edges[j];
            int line_index = e - g->edges;
            int dline_index;

            if (state->lines[line_index] != LINE_UNKNOWN)
                continue;
            k = j + 1;
            if (k >= N) k = 0;

            /* minimum YESs in the complement of this edge */
            if (mins[k][j] > clue) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            }
            if (mins[k][j] == clue) {
                /* setting this edge to YES would make at least
                 * (clue+1) edges - contradiction */
                solver_set_line(sstate, line_index, LINE_NO);
                diff = min(diff, DIFF_EASY);
            }
            if (maxs[k][j] < clue - 1) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            }
            if (maxs[k][j] == clue - 1) {
                /* Only way to satisfy the clue is to set edge{j} as YES */
                solver_set_line(sstate, line_index, LINE_YES);
                diff = min(diff, DIFF_EASY);
            }

            /* More advanced deduction that allows propagation along diagonal
             * chains of faces connected by dots, for example, 3-2-...-2-3
             * in square grids. */
            if (sstate->diff >= DIFF_TRICKY) {
                /* Now see if we can make dline deduction for edges{j,j+1} */
                e = f->edges[k];
                if (state->lines[e - g->edges] != LINE_UNKNOWN)
                    /* Only worth doing this for an UNKNOWN,UNKNOWN pair.
                     * Dlines where one of the edges is known, are handled in the
                     * dot-deductions */
                    continue;
    
                dline_index = dline_index_from_face(g, f, k);
                k++;
                if (k >= N) k = 0;
    
                /* minimum YESs in the complement of this dline */
                if (mins[k][j] > clue - 2) {
                    /* Adding 2 YESs would break the clue */
                    if (set_atmostone(dlines, dline_index))
                        diff = min(diff, DIFF_NORMAL);
                }
                /* maximum YESs in the complement of this dline */
                if (maxs[k][j] < clue) {
                    /* Adding 2 NOs would mean not enough YESs */
                    if (set_atleastone(dlines, dline_index))
                        diff = min(diff, DIFF_NORMAL);
                }
            }
        }
    }

    if (diff < DIFF_NORMAL)
        return diff;

    /* ------ Dot deductions ------ */

    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int N = d->order;
        int yes, no, unknown;
        int j;
        if (sstate->dot_solved[i])
            continue;
        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = N - yes - no;

        for (j = 0; j < N; j++) {
            int k;
            int dline_index;
            int line1_index, line2_index;
            enum line_state line1, line2;
            k = j + 1;
            if (k >= N) k = 0;
            dline_index = dline_index_from_dot(g, d, j);
            line1_index = d->edges[j] - g->edges;
            line2_index = d->edges[k] - g->edges;
            line1 = state->lines[line1_index];
            line2 = state->lines[line2_index];

            /* Infer dline state from line state */
            if (line1 == LINE_NO || line2 == LINE_NO) {
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
            if (line1 == LINE_YES || line2 == LINE_YES) {
                if (set_atleastone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
            /* Infer line state from dline state */
            if (is_atmostone(dlines, dline_index)) {
                if (line1 == LINE_YES && line2 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line2_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
                if (line2 == LINE_YES && line1 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line1_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
            }
            if (is_atleastone(dlines, dline_index)) {
                if (line1 == LINE_NO && line2 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line2_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
                if (line2 == LINE_NO && line1 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line1_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
            }
            /* Deductions that depend on the numbers of lines.
             * Only bother if both lines are UNKNOWN, otherwise the
             * easy-mode solver (or deductions above) would have taken
             * care of it. */
            if (line1 != LINE_UNKNOWN || line2 != LINE_UNKNOWN)
                continue;

            if (yes == 0 && unknown == 2) {
                /* Both these unknowns must be identical.  If we know
                 * atmostone or atleastone, we can make progress. */
                if (is_atmostone(dlines, dline_index)) {
                    solver_set_line(sstate, line1_index, LINE_NO);
                    solver_set_line(sstate, line2_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
                if (is_atleastone(dlines, dline_index)) {
                    solver_set_line(sstate, line1_index, LINE_YES);
                    solver_set_line(sstate, line2_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
            }
            if (yes == 1) {
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                if (unknown == 2) {
                    if (set_atleastone(dlines, dline_index))
                        diff = min(diff, DIFF_NORMAL);
                }
            }

            /* More advanced deduction that allows propagation along diagonal
             * chains of faces connected by dots, for example: 3-2-...-2-3
             * in square grids. */
            if (sstate->diff >= DIFF_TRICKY) {
                /* If we have atleastone set for this dline, infer
                 * atmostone for each "opposite" dline (that is, each
                 * dline without edges in common with this one).
                 * Again, this test is only worth doing if both these
                 * lines are UNKNOWN.  For if one of these lines were YES,
                 * the (yes == 1) test above would kick in instead. */
                if (is_atleastone(dlines, dline_index)) {
                    int opp;
                    for (opp = 0; opp < N; opp++) {
                        int opp_dline_index;
                        if (opp == j || opp == j+1 || opp == j-1)
                            continue;
                        if (j == 0 && opp == N-1)
                            continue;
                        if (j == N-1 && opp == 0)
                            continue;
                        opp_dline_index = dline_index_from_dot(g, d, opp);
                        if (set_atmostone(dlines, opp_dline_index))
                            diff = min(diff, DIFF_NORMAL);
                    }
                    if (yes == 0 && is_atmostone(dlines, dline_index)) {
                        /* This dline has *exactly* one YES and there are no
                         * other YESs.  This allows more deductions. */
                        if (unknown == 3) {
                            /* Third unknown must be YES */
                            for (opp = 0; opp < N; opp++) {
                                int opp_index;
                                if (opp == j || opp == k)
                                    continue;
                                opp_index = d->edges[opp] - g->edges;
                                if (state->lines[opp_index] == LINE_UNKNOWN) {
                                    solver_set_line(sstate, opp_index,
                                                    LINE_YES);
                                    diff = min(diff, DIFF_EASY);
                                }
                            }
                        } else if (unknown == 4) {
                            /* Exactly one of opposite UNKNOWNS is YES.  We've
                             * already set atmostone, so set atleastone as
                             * well.
                             */
                            if (dline_set_opp_atleastone(sstate, d, j))
                                diff = min(diff, DIFF_NORMAL);
                        }
                    }
                }
            }
        }
    }
    return diff;
}

static int linedsf_deductions(solver_state *sstate)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    char *dlines = sstate->dlines;
    int i;
    int diff = DIFF_MAX;
    int diff_tmp;

    /* ------ Face deductions ------ */

    /* A fully-general linedsf deduction seems overly complicated
     * (I suspect the problem is NP-complete, though in practice it might just
     * be doable because faces are limited in size).
     * For simplicity, we only consider *pairs* of LINE_UNKNOWNS that are
     * known to be identical.  If setting them both to YES (or NO) would break
     * the clue, set them to NO (or YES). */

    for (i = 0; i < g->num_faces; i++) {
        int N, yes, no, unknown;
        int clue;

        if (sstate->face_solved[i])
            continue;
        clue = state->clues[i];
        if (clue < 0)
            continue;

        N = g->faces[i].order;
        yes = sstate->face_yes_count[i];
        if (yes + 1 == clue) {
            if (face_setall_identical(sstate, i, LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        no = sstate->face_no_count[i];
        if (no + 1 == N - clue) {
            if (face_setall_identical(sstate, i, LINE_YES))
                diff = min(diff, DIFF_EASY);
        }

        /* Reload YES count, it might have changed */
        yes = sstate->face_yes_count[i];
        unknown = N - no - yes;

        /* Deductions with small number of LINE_UNKNOWNs, based on overall
         * parity of lines. */
        diff_tmp = parity_deductions(sstate, g->faces[i].edges,
                                     (clue - yes) % 2, unknown);
        diff = min(diff, diff_tmp);
    }

    /* ------ Dot deductions ------ */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int N = d->order;
        int j;
        int yes, no, unknown;
        /* Go through dlines, and do any dline<->linedsf deductions wherever
         * we find two UNKNOWNS. */
        for (j = 0; j < N; j++) {
            int dline_index = dline_index_from_dot(g, d, j);
            int line1_index;
            int line2_index;
            int can1, can2, inv1, inv2;
            int j2;
            line1_index = d->edges[j] - g->edges;
            if (state->lines[line1_index] != LINE_UNKNOWN)
                continue;
            j2 = j + 1;
            if (j2 == N) j2 = 0;
            line2_index = d->edges[j2] - g->edges;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;
            /* Infer dline flags from linedsf */
            can1 = edsf_canonify(sstate->linedsf, line1_index, &inv1);
            can2 = edsf_canonify(sstate->linedsf, line2_index, &inv2);
            if (can1 == can2 && inv1 != inv2) {
                /* These are opposites, so set dline atmostone/atleastone */
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                if (set_atleastone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                continue;
            }
            /* Infer linedsf from dline flags */
            if (is_atmostone(dlines, dline_index)
		&& is_atleastone(dlines, dline_index)) {
                if (merge_lines(sstate, line1_index, line2_index, 1))
                    diff = min(diff, DIFF_HARD);
            }
        }

        /* Deductions with small number of LINE_UNKNOWNs, based on overall
         * parity of lines. */
        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = N - yes - no;
        diff_tmp = parity_deductions(sstate, d->edges,
                                     yes % 2, unknown);
        diff = min(diff, diff_tmp);
    }

    /* ------ Edge dsf deductions ------ */

    /* If the state of a line is known, deduce the state of its canonical line
     * too, and vice versa. */
    for (i = 0; i < g->num_edges; i++) {
        int can, inv;
        enum line_state s;
        can = edsf_canonify(sstate->linedsf, i, &inv);
        if (can == i)
            continue;
        s = sstate->state->lines[can];
        if (s != LINE_UNKNOWN) {
            if (solver_set_line(sstate, i, inv ? OPP(s) : s))
                diff = min(diff, DIFF_EASY);
        } else {
            s = sstate->state->lines[i];
            if (s != LINE_UNKNOWN) {
                if (solver_set_line(sstate, can, inv ? OPP(s) : s))
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
    grid *g = state->game_grid;
    int shortest_chainlen = g->num_dots;
    int loop_found = FALSE;
    int dots_connected;
    int progress = FALSE;
    int i;

    /*
     * Go through the grid and update for all the new edges.
     * Since merge_dots() is idempotent, the simplest way to
     * do this is just to update for _all_ the edges.
     * Also, while we're here, we count the edges.
     */
    for (i = 0; i < g->num_edges; i++) {
        if (state->lines[i] == LINE_YES) {
            loop_found |= merge_dots(sstate, i);
            edgecount++;
        }
    }

    /*
     * Count the clues, count the satisfied clues, and count the
     * satisfied-minus-one clues.
     */
    for (i = 0; i < g->num_faces; i++) {
        int c = state->clues[i];
        if (c >= 0) {
            int o = sstate->face_yes_count[i];
            if (o == c)
                satclues++;
            else if (o == c-1)
                sm1clues++;
            clues++;
        }
    }

    for (i = 0; i < g->num_dots; ++i) {
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
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int d1 = e->dot1 - g->dots;
        int d2 = e->dot2 - g->dots;
        int eqclass, val;
        if (state->lines[i] != LINE_UNKNOWN)
            continue;

        eqclass = dsf_canonify(sstate->dotdsf, d1);
        if (eqclass != dsf_canonify(sstate->dotdsf, d2))
            continue;

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
            if (e->face1) {
                int f = e->face1 - g->faces;
                int c = state->clues[f];
                if (c >= 0 && sstate->face_yes_count[f] == c - 1)
                    sm1_nearby++;
            }
            if (e->face2) {
                int f = e->face2 - g->faces;
                int c = state->clues[f];
                if (c >= 0 && sstate->face_yes_count[f] == c - 1)
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
        progress = solver_set_line(sstate, i, val);
        assert(progress == TRUE);
        if (val == LINE_YES) {
            sstate->solver_status = SOLVER_AMBIGUOUS;
            goto finished_loop_deductionsing;
        }
    }

    finished_loop_deductionsing:
    return progress ? DIFF_EASY : DIFF_MAX;
}

/* This will return a dynamically allocated solver_state containing the (more)
 * solved grid */
static solver_state *solve_game_rec(const solver_state *sstate_start)
{
    solver_state *sstate;

    /* Index of the solver we should call next. */
    int i = 0;
    
    /* As a speed-optimisation, we avoid re-running solvers that we know
     * won't make any progress.  This happens when a high-difficulty
     * solver makes a deduction that can only help other high-difficulty
     * solvers.
     * For example: if a new 'dline' flag is set by dline_deductions, the
     * trivial_deductions solver cannot do anything with this information.
     * If we've already run the trivial_deductions solver (because it's
     * earlier in the list), there's no point running it again.
     *
     * Therefore: if a solver is earlier in the list than "threshold_index",
     * we don't bother running it if it's difficulty level is less than
     * "threshold_diff".
     */
    int threshold_diff = 0;
    int threshold_index = 0;
    
    sstate = dup_solver_state(sstate_start);

    check_caches(sstate);

    while (i < NUM_SOLVERS) {
        if (sstate->solver_status == SOLVER_MISTAKE)
            return sstate;
        if (sstate->solver_status == SOLVER_SOLVED ||
            sstate->solver_status == SOLVER_AMBIGUOUS) {
            /* solver finished */
            break;
        }

        if ((solver_diffs[i] >= threshold_diff || i >= threshold_index)
            && solver_diffs[i] <= sstate->diff) {
            /* current_solver is eligible, so use it */
            int next_diff = solver_fns[i](sstate);
            if (next_diff != DIFF_MAX) {
                /* solver made progress, so use new thresholds and
                * start again at top of list. */
                threshold_diff = next_diff;
                threshold_index = i;
                i = 0;
                continue;
            }
        }
        /* current_solver is ineligible, or failed to make progress, so
         * go to the next solver in the list */
        i++;
    }

    if (sstate->solver_status == SOLVER_SOLVED ||
        sstate->solver_status == SOLVER_AMBIGUOUS) {
        /* s/LINE_UNKNOWN/LINE_NO/g */
        array_setall(sstate->state->lines, LINE_UNKNOWN, LINE_NO,
                     sstate->state->game_grid->num_edges);
        return sstate;
    }

    return sstate;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
    char *soln = NULL;
    solver_state *sstate, *new_sstate;

    sstate = new_solver_state(state, DIFF_MAX);
    new_sstate = solve_game_rec(sstate);

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

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    grid *g = state->game_grid;
    grid_edge *e;
    int i;
    char *ret, buf[80];
    char button_char = ' ';
    enum line_state old_state;

    if (IS_CURSOR_MOVE(button)) {
        int dx = 0, dy = 0, x = ui->cur_x, y = ui->cur_y;
        grid_edge *newe;

        switch (button) {
        case CURSOR_UP:         dy = -1; break;
        case CURSOR_DOWN:       dy = 1; break;
        case CURSOR_RIGHT:      dx = 1; break;
        case CURSOR_LEFT:       dx = -1; break;
        default: assert(!"unknown cursor");
        }

        /* We keep moving in the offset direction until we reach the sides
         * of the grid (in which case we don't move the cursor) or we are
         * nearer to a new edge (in which case we update the cursor position
         * and return that). */
        e = newe = grid_nearest_edge(g, ui->cur_x, ui->cur_y);
        x = ui->cur_x; y = ui->cur_y;
        while (newe == e || newe == NULL) {
            x += dx; y += dy;
            if (x < g->lowest_x || x > g->highest_x) goto hitedge;
            if (y < g->lowest_y || y > g->highest_y) goto hitedge;
            newe = grid_nearest_edge(g, x, y);
        }
        ui->cur_x = x; ui->cur_y = y;
hitedge:
        ui->cur_visible = 1;
        return "";
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = 1;
            return "";
        }
        e = grid_nearest_edge(g, ui->cur_x, ui->cur_y);
        if (e == NULL)
            return NULL;
        i = e - g->edges;

        old_state = state->lines[i];

        if (button == CURSOR_SELECT2)
            button_char = (old_state == LINE_UNKNOWN) ? 'n' : 'u';
        else
            button_char = (old_state == LINE_UNKNOWN) ? 'y' : 'u';

        goto makemove;
    }

    button &= ~MOD_MASK;

    /* Convert mouse-click (x,y) to grid coordinates */
    x -= BORDER(ds->tilesize);
    y -= BORDER(ds->tilesize);
    x = x * g->tilesize / ds->tilesize;
    y = y * g->tilesize / ds->tilesize;
    x += g->lowest_x;
    y += g->lowest_y;

    e = grid_nearest_edge(g, x, y);
    if (e == NULL)
        return NULL;

    i = e - g->edges;

    /* I think it's only possible to play this game with mouse clicks, sorry */
    /* Maybe will add mouse drag support some time */
    old_state = state->lines[i];

    switch (button) {
      case LEFT_BUTTON:
	switch (old_state) {
	  case LINE_UNKNOWN:
	    button_char = 'y';
	    break;
	  case LINE_YES:
#ifdef STYLUS_BASED
	    button_char = 'n';
	    break;
#endif
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
#ifdef STYLUS_BASED
	    button_char = 'y';
	    break;
#endif
	  case LINE_YES:
	    button_char = 'u';
	    break;
	}
	break;
      default:
	return NULL;
    }

    ui->cur_visible = 0;

makemove:
    sprintf(buf, "%d%c", i, (int)button_char);
    ret = dupstr(buf);

    return ret;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int i;
    game_state *newstate = dup_game(state);

    if (move[0] == 'S') {
        move++;
        newstate->cheated = TRUE;
    }

    while (*move) {
        i = atoi(move);
        if (i < 0 || i >= newstate->game_grid->num_edges)
            goto fail;
        move += strspn(move, "1234567890");
        switch (*(move++)) {
	  case 'y':
	    newstate->lines[i] = LINE_YES;
	    break;
	  case 'n':
	    newstate->lines[i] = LINE_NO;
	    break;
	  case 'u':
	    newstate->lines[i] = LINE_UNKNOWN;
	    break;
	  default:
	    goto fail;
        }
    }

    /*
     * Check for completion.
     */
    if (check_completion(newstate))
        newstate->solved = TRUE;

    return newstate;

    fail:
    free_game(newstate);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

/* Convert from grid coordinates to screen coordinates */
static void grid_to_screen(const game_drawstate *ds, const grid *g,
                           int grid_x, int grid_y, int *x, int *y)
{
    *x = grid_x - g->lowest_x;
    *y = grid_y - g->lowest_y;
    *x = *x * ds->tilesize / g->tilesize;
    *y = *y * ds->tilesize / g->tilesize;
    *x += BORDER(ds->tilesize);
    *y += BORDER(ds->tilesize);
}

/* Returns (into x,y) position of centre of face for rendering the text clue.
 */
static void face_text_pos(const game_drawstate *ds, const grid *g,
                          grid_face *f, int *xret, int *yret)
{
    int faceindex = f - g->faces;

    /*
     * Return the cached position for this face, if we've already
     * worked it out.
     */
    if (ds->textx[faceindex] >= 0) {
        *xret = ds->textx[faceindex];
        *yret = ds->texty[faceindex];
        return;
    }

    /*
     * Otherwise, use the incentre computed by grid.c and convert it
     * to screen coordinates.
     */
    grid_find_incentre(f);
    grid_to_screen(ds, g, f->ix, f->iy,
                   &ds->textx[faceindex], &ds->texty[faceindex]);

    *xret = ds->textx[faceindex];
    *yret = ds->texty[faceindex];
}

static void face_text_bbox(game_drawstate *ds, grid *g, grid_face *f,
                           int *x, int *y, int *w, int *h)
{
    int xx, yy;
    face_text_pos(ds, g, f, &xx, &yy);

    /* There seems to be a certain amount of trial-and-error involved
     * in working out the correct bounding-box for the text. */

    *x = xx - ds->tilesize/4 - 1;
    *y = yy - ds->tilesize/4 - 3;
    *w = ds->tilesize/2 + 2;
    *h = ds->tilesize/2 + 5;
}

static void game_redraw_clue(drawing *dr, game_drawstate *ds,
			     const game_state *state, int i)
{
    grid *g = state->game_grid;
    grid_face *f = g->faces + i;
    int x, y;
    char c[20];

    sprintf(c, "%d", state->clues[i]);

    face_text_pos(ds, g, f, &x, &y);
    draw_text(dr, x, y,
	      FONT_VARIABLE, ds->tilesize/2,
	      ALIGN_VCENTRE | ALIGN_HCENTRE,
	      ds->clue_error[i] ? COL_MISTAKE :
	      ds->clue_satisfied[i] ? COL_SATISFIED : COL_FOREGROUND, c);
}

static void edge_bbox(game_drawstate *ds, grid *g, grid_edge *e,
                      int *x, int *y, int *w, int *h)
{
    int x1 = e->dot1->x;
    int y1 = e->dot1->y;
    int x2 = e->dot2->x;
    int y2 = e->dot2->y;
    int xmin, xmax, ymin, ymax;

    grid_to_screen(ds, g, x1, y1, &x1, &y1);
    grid_to_screen(ds, g, x2, y2, &x2, &y2);
    /* Allow extra margin for dots, and thickness of lines */
    xmin = min(x1, x2) - 2;
    xmax = max(x1, x2) + 2;
    ymin = min(y1, y2) - 2;
    ymax = max(y1, y2) + 2;

    *x = xmin;
    *y = ymin;
    *w = xmax - xmin + 1;
    *h = ymax - ymin + 1;
}

static void dot_bbox(game_drawstate *ds, grid *g, grid_dot *d,
                     int *x, int *y, int *w, int *h)
{
    int x1, y1;

    grid_to_screen(ds, g, d->x, d->y, &x1, &y1);

    *x = x1 - 2;
    *y = y1 - 2;
    *w = 5;
    *h = 5;
}

static const int loopy_line_redraw_phases[] = {
    COL_FAINT, COL_LINEUNKNOWN, COL_FOREGROUND, COL_HIGHLIGHT, COL_MISTAKE
};
#define NPHASES lenof(loopy_line_redraw_phases)

static void game_redraw_line(drawing *dr, game_drawstate *ds,
			     const game_state *state, int i, int phase)
{
    grid *g = state->game_grid;
    grid_edge *e = g->edges + i;
    int x1, x2, y1, y2;
    int line_colour;

    if (state->line_errors[i])
	line_colour = COL_MISTAKE;
    else if (state->lines[i] == LINE_UNKNOWN)
	line_colour = COL_LINEUNKNOWN;
    else if (state->lines[i] == LINE_NO)
	line_colour = COL_FAINT;
    else if (ds->flashing)
	line_colour = COL_HIGHLIGHT;
    else
	line_colour = COL_FOREGROUND;
    if (line_colour != loopy_line_redraw_phases[phase])
        return;

    /* Convert from grid to screen coordinates */
    grid_to_screen(ds, g, e->dot1->x, e->dot1->y, &x1, &y1);
    grid_to_screen(ds, g, e->dot2->x, e->dot2->y, &x2, &y2);

    if (line_colour == COL_FAINT) {
	static int draw_faint_lines = -1;
	if (draw_faint_lines < 0) {
	    char *env = getenv("LOOPY_FAINT_LINES");
	    draw_faint_lines = (!env || (env[0] == 'y' ||
					 env[0] == 'Y'));
	}
	if (draw_faint_lines)
	    draw_line(dr, x1, y1, x2, y2, line_colour);
    } else {
	draw_thick_line(dr, 3.0,
			x1 + 0.5, y1 + 0.5,
			x2 + 0.5, y2 + 0.5,
			line_colour);
    }
}

static void game_redraw_dot(drawing *dr, game_drawstate *ds,
			    const game_state *state, int i, int current)
{
    grid *g = state->game_grid;
    grid_dot *d = g->dots + i;
    int x, y;
    int dot_colour = current ? COL_CURSOR : COL_FOREGROUND;

    grid_to_screen(ds, g, d->x, d->y, &x, &y);
    draw_circle(dr, x, y, 2, dot_colour, dot_colour);
}

static int boxes_intersect(int x0, int y0, int w0, int h0,
                           int x1, int y1, int w1, int h1)
{
    /*
     * Two intervals intersect iff neither is wholly on one side of
     * the other. Two boxes intersect iff their horizontal and
     * vertical intervals both intersect.
     */
    return (x0 < x1+w1 && x1 < x0+w0 && y0 < y1+h1 && y1 < y0+h0);
}

static void game_redraw_in_rect(drawing *dr, game_drawstate *ds,
                                const game_state *state,
                                int x, int y, int w, int h)
{
    grid *g = state->game_grid;
    int i, phase;
    int bx, by, bw, bh;
    int cur1, cur2;
    if (ds->cur_edge) {
	cur1 = ds->cur_edge->dot1 - g->dots;
	cur2 = ds->cur_edge->dot2 - g->dots;
    } else {
	cur1 = cur2 = -1;
    }

    clip(dr, x, y, w, h);
    draw_rect(dr, x, y, w, h, COL_BACKGROUND);

    for (i = 0; i < g->num_faces; i++) {
        if (state->clues[i] >= 0) {
            face_text_bbox(ds, g, &g->faces[i], &bx, &by, &bw, &bh);
            if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
                game_redraw_clue(dr, ds, state, i);
        }
    }
    for (phase = 0; phase < NPHASES; phase++) {
        for (i = 0; i < g->num_edges; i++) {
            edge_bbox(ds, g, &g->edges[i], &bx, &by, &bw, &bh);
            if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
                game_redraw_line(dr, ds, state, i, phase);
        }
    }
    for (i = 0; i < g->num_dots; i++) {
        dot_bbox(ds, g, &g->dots[i], &bx, &by, &bw, &bh);
        if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
            game_redraw_dot(dr, ds, state, i, i == cur1 || i == cur2);
    }

    unclip(dr);
    draw_update(dr, x, y, w, h);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
#define REDRAW_OBJECTS_LIMIT 16		/* Somewhat arbitrary tradeoff */

    grid *g = state->game_grid;
    int border = BORDER(ds->tilesize);
    int i;
    int flash_changed;
    int redraw_everything = FALSE;
    grid_edge *cur_edge;
    /*int cur1, cur2;*/

    int edges[REDRAW_OBJECTS_LIMIT], nedges = 0;
    int faces[REDRAW_OBJECTS_LIMIT], nfaces = 0;

#ifdef CURSOR_IS_VISIBLE
    if (ds->cur_visible) {
        assert(ds->cur_bl);
        blitter_load(dr, ds->cur_bl, ds->cur_bl_x, ds->cur_bl_y);
        draw_update(dr, ds->cur_bl_x, ds->cur_bl_y, BLITTER_SZ, BLITTER_SZ);
    }
#endif

    if (ui->cur_visible) {
        cur_edge = grid_nearest_edge(g, ui->cur_x, ui->cur_y);
    } else {
        cur_edge = NULL;
    }

    /* Redrawing is somewhat involved.
     *
     * An update can theoretically affect an arbitrary number of edges
     * (consider, for example, completing or breaking a cycle which doesn't
     * satisfy all the clues -- we'll switch many edges between error and
     * normal states).  On the other hand, redrawing the whole grid takes a
     * while, making the game feel sluggish, and many updates are actually
     * quite well localized.
     *
     * This redraw algorithm attempts to cope with both situations gracefully
     * and correctly.  For localized changes, we set a clip rectangle, fill
     * it with background, and then redraw (a plausible but conservative
     * guess at) the objects which intersect the rectangle; if several
     * objects need redrawing, we'll do them individually.  However, if lots
     * of objects are affected, we'll just redraw everything.
     *
     * The reason for all of this is that it's just not safe to do the redraw
     * piecemeal.  If you try to draw an antialiased diagonal line over
     * itself, you get a slightly thicker antialiased diagonal line, which
     * looks rather ugly after a while.
     *
     * So, we take two passes over the grid.  The first attempts to work out
     * what needs doing, and the second actually does it.
     */

    if (!ds->started) {
	redraw_everything = TRUE;
        /*
         * But we must still go through the upcoming loops, so that we
         * set up stuff in ds correctly for the initial redraw.
         */
    }

    /* First, trundle through the faces. */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int sides = f->order;
        int clue_mistake;
        int clue_satisfied;
        int n = state->clues[i];
        if (n < 0)
            continue;

        clue_mistake = (face_order(state, i, LINE_YES) > n ||
                        face_order(state, i, LINE_NO ) > (sides-n));
        clue_satisfied = (face_order(state, i, LINE_YES) == n &&
                          face_order(state, i, LINE_NO ) == (sides-n));

        if (clue_mistake != ds->clue_error[i] ||
            clue_satisfied != ds->clue_satisfied[i]) {
            ds->clue_error[i] = clue_mistake;
            ds->clue_satisfied[i] = clue_satisfied;
            if (nfaces == REDRAW_OBJECTS_LIMIT)
                redraw_everything = TRUE;
            else
                faces[nfaces++] = i;
        }
    }

    /* Work out what the flash state needs to be. */
    if (flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3)) {
        flash_changed = !ds->flashing;
        ds->flashing = TRUE;
    } else {
        flash_changed = ds->flashing;
        ds->flashing = FALSE;
    }

    /* Now, trundle through the edges. */
    for (i = 0; i < g->num_edges; i++) {
        char new_ds =
            state->line_errors[i] ? DS_LINE_ERROR : state->lines[i];
        if (new_ds != ds->lines[i] ||
            (flash_changed && state->lines[i] == LINE_YES)) {
            ds->lines[i] = new_ds;
            if (nedges == REDRAW_OBJECTS_LIMIT)
                redraw_everything = TRUE;
            else
                edges[nedges++] = i;
        }
    }

    /* Pass one is now done.  Now we do the actual drawing. */
    /*if (cur_edge) {
	cur1 = cur_edge->dot1 - g->dots;
	cur2 = cur_edge->dot2 - g->dots;
    } else {
	cur1 = cur2 = -1;
    }*/

    if (redraw_everything) {
        int grid_width = g->highest_x - g->lowest_x;
        int grid_height = g->highest_y - g->lowest_y;
        int w = grid_width * ds->tilesize / g->tilesize;
        int h = grid_height * ds->tilesize / g->tilesize;

        game_redraw_in_rect(dr, ds, state,
                            0, 0, w + 2*border + 1, h + 2*border + 1);
    } else {

	/* Right.  Now we roll up our sleeves. */

	for (i = 0; i < nfaces; i++) {
	    grid_face *f = g->faces + faces[i];
	    int x, y, w, h;

            face_text_bbox(ds, g, f, &x, &y, &w, &h);
            game_redraw_in_rect(dr, ds, state, x, y, w, h);
	}

	for (i = 0; i < nedges; i++) {
	    grid_edge *e = g->edges + edges[i];
            int x, y, w, h;

            edge_bbox(ds, g, e, &x, &y, &w, &h);
            game_redraw_in_rect(dr, ds, state, x, y, w, h);
	}
    }

    ds->started = TRUE;

#ifdef CURSOR_IS_VISIBLE
    /* Draw current cursor, if present (after all other drawing) */
    if (ui->cur_visible) {
        int cx, cy;
        grid_to_screen(ds, g, ui->cur_x, ui->cur_y, &cx, &cy);

        ds->cur_bl_x = cx - BLITTER_HSZ;
        ds->cur_bl_y = cy - BLITTER_HSZ;
        blitter_save(dr, ds->cur_bl, ds->cur_bl_x, ds->cur_bl_y);

        draw_rect(dr, ds->cur_bl_x + 1, cy-CUR_HSZ, BLITTER_SZ - 2, CUR_SZ, COL_CURSOR);
        draw_rect(dr, cx-CUR_HSZ, ds->cur_bl_y + 1, CUR_SZ, BLITTER_SZ - 2, COL_CURSOR);

        draw_update(dr, ds->cur_bl_x, ds->cur_bl_y, BLITTER_SZ, BLITTER_SZ);
    }
#endif

    ds->cur_edge = cur_edge;
    ds->cur_visible = ui->cur_visible;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->solved  &&  newstate->solved &&
        !oldstate->cheated && !newstate->cheated) {
        return FLASH_TIME;
    }

    return 0.0F;
}

static int game_status(const game_state *state)
{
    return state->solved ? +1 : 0;
}

#ifndef NO_PRINTING
static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 7mm "squares" by default.
     */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int i;
    game_drawstate ads, *ds = &ads;
    grid *g = state->game_grid;

    ds->tilesize = tilesize;
    ds->textx = snewn(g->num_faces, int);
    ds->texty = snewn(g->num_faces, int);
    for (i = 0; i < g->num_faces; i++)
        ds->textx[i] = ds->texty[i] = -1;

    for (i = 0; i < g->num_dots; i++) {
        int x, y;
        grid_to_screen(ds, g, g->dots[i].x, g->dots[i].y, &x, &y);
        draw_circle(dr, x, y, ds->tilesize / 15, ink, ink);
    }

    /*
     * Clues.
     */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int clue = state->clues[i];
        if (clue >= 0) {
            char c[20];
            int x, y;
            sprintf(c, "%d", state->clues[i]);
            face_text_pos(ds, g, f, &x, &y);
            draw_text(dr, x, y,
                      FONT_VARIABLE, ds->tilesize / 2,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, ink, c);
        }
    }

    /*
     * Lines.
     */
    for (i = 0; i < g->num_edges; i++) {
        int thickness = (state->lines[i] == LINE_YES) ? 30 : 150;
        grid_edge *e = g->edges + i;
        int x1, y1, x2, y2;
        grid_to_screen(ds, g, e->dot1->x, e->dot1->y, &x1, &y1);
        grid_to_screen(ds, g, e->dot2->x, e->dot2->y, &x2, &y2);
        if (state->lines[i] == LINE_YES)
        {
            /* (dx, dy) points from (x1, y1) to (x2, y2).
             * The line is then "fattened" in a perpendicular
             * direction to create a thin rectangle. */
            double d = sqrt(SQ((double)x1 - x2) + SQ((double)y1 - y2));
            double dx = (x2 - x1) / d;
            double dy = (y2 - y1) / d;
	    int points[8];

            dx = (dx * ds->tilesize) / thickness;
            dy = (dy * ds->tilesize) / thickness;
	    points[0] = x1 + (int)dy;
	    points[1] = y1 - (int)dx;
	    points[2] = x1 - (int)dy;
	    points[3] = y1 + (int)dx;
	    points[4] = x2 - (int)dy;
	    points[5] = y2 + (int)dx;
	    points[6] = x2 + (int)dy;
	    points[7] = y2 - (int)dx;
            draw_polygon(dr, points, 4, ink, ink);
        }
        else
        {
            /* Draw a dotted line */
            int divisions = 6;
            int j;
            for (j = 1; j < divisions; j++) {
                /* Weighted average */
                int x = (x1 * (divisions -j) + x2 * j) / divisions;
                int y = (y1 * (divisions -j) + y2 * j) / divisions;
                draw_circle(dr, x, y, ds->tilesize / thickness, ink, ink);
            }
        }
    }

    sfree(ds->textx);
    sfree(ds->texty);
}
#endif

#ifdef COMBINED
#define thegame loopy
#endif

const struct game thegame = {
    "Loopy", "games.loopy", "loopy",
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
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL,  /* android_request_keys */
    android_cursor_visibility,
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
    game_status,
#ifndef NO_PRINTING
    TRUE, FALSE, game_print_size, game_print,
#endif
    FALSE /* wants_statusbar */,
    FALSE, game_timing_state,
    0,                                       /* mouse_priorities */
};

#ifdef STANDALONE_SOLVER

/*
 * Half-hearted standalone solver. It can't output the solution to
 * anything but a square puzzle, and it can't log the deductions
 * it makes either. But it can solve square puzzles, and more
 * importantly it can use its solver to grade the difficulty of
 * any puzzle you give it.
 */

#include <stdarg.h>

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *err;
    int grade = FALSE;
    int ret, diff;
#if 0 /* verbose solver not supported here (yet) */
    int really_verbose = FALSE;
#endif

    while (--argc > 0) {
        char *p = *++argv;
#if 0 /* verbose solver not supported here (yet) */
        if (!strcmp(p, "-v")) {
            really_verbose = TRUE;
        } else
#endif
	if (!strcmp(p, "-g")) {
            grade = TRUE;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
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

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    /*
     * When solving an Easy puzzle, we don't want to bother the
     * user with Hard-level deductions. For this reason, we grade
     * the puzzle internally before doing anything else.
     */
    ret = -1;			       /* placate optimiser */
    for (diff = 0; diff < DIFF_MAX; diff++) {
	solver_state *sstate_new;
	solver_state *sstate = new_solver_state((game_state *)s, diff);

	sstate_new = solve_game_rec(sstate);

	if (sstate_new->solver_status == SOLVER_MISTAKE)
	    ret = 0;
	else if (sstate_new->solver_status == SOLVER_SOLVED)
	    ret = 1;
	else
	    ret = 2;

	free_solver_state(sstate_new);
	free_solver_state(sstate);

	if (ret < 2)
	    break;
    }

    if (diff == DIFF_MAX) {
	if (grade)
	    printf("Difficulty rating: harder than Hard, or ambiguous\n");
	else
	    printf("Unable to find a unique solution\n");
    } else {
	if (grade) {
	    if (ret == 0)
		printf("Difficulty rating: impossible (no solution exists)\n");
	    else if (ret == 1)
		printf("Difficulty rating: %s\n", diffnames[diff]);
	} else {
	    solver_state *sstate_new;
	    solver_state *sstate = new_solver_state((game_state *)s, diff);

	    /* If we supported a verbose solver, we'd set verbosity here */

	    sstate_new = solve_game_rec(sstate);

	    if (sstate_new->solver_status == SOLVER_MISTAKE)
		printf("Puzzle is inconsistent\n");
	    else {
		assert(sstate_new->solver_status == SOLVER_SOLVED);
		if (s->grid_type == 0) {
		    fputs(game_text_format(sstate_new->state), stdout);
		} else {
		    printf("Unable to output non-square grids\n");
		}
	    }

	    free_solver_state(sstate_new);
	    free_solver_state(sstate);
	}
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
