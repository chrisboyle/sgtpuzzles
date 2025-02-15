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
 *     + The way I envisage this working is simply to keep a flip dsf
 * 	 of all _faces_, which indicates whether they're on
 * 	 opposite sides of the loop from one another. We also
 * 	 include a special entry in the dsf for the infinite
 * 	 exterior "face".
 *     + So, the simple way to do this is to just go through the
 * 	 edges: every time we see an edge in a state other than
 * 	 LINE_UNKNOWN which separates two faces that aren't in the
 * 	 same dsf class, we can rectify that by merging the
 * 	 classes. Then, conversely, an edge in LINE_UNKNOWN state
 * 	 which separates two faces that _are_ in the same dsf
 * 	 class can immediately have its state determined.
 *     + But you can go one better, if you're prepared to loop
 * 	 over all _pairs_ of edges. Suppose we have edges A and B,
 * 	 which respectively separate faces A1,A2 and B1,B2.
 * 	 Suppose that A,B are in the same edge-dsf class and that
 * 	 A1,B1 (wlog) are in the same face-dsf class; then we can
 * 	 immediately place A2,B2 into the same face-dsf class (as
 * 	 each other, not as A1 and A2) one way round or the other.
 * 	 And conversely again, if A1,B1 are in the same face-dsf
 * 	 class and so are A2,B2, then we can put A,B into the same
 * 	 face-dsf class.
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
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif

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
    COL_FAINT,
    NCOLOURS
};

enum {
    PREF_DRAW_FAINT_LINES,
    PREF_AUTO_FOLLOW,
    N_PREF_ITEMS
};

struct game_state {
    grid *game_grid; /* ref-counted (internally) */

    /* Put -1 in a face that doesn't get a clue */
    signed char *clues;

    /* Array of line states, to store whether each line is
     * YES, NO or UNKNOWN */
    char *lines;

    bool *line_errors;
    bool exactly_one_loop;

    bool solved;
    bool cheated;

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
    bool *dot_solved, *face_solved;
    DSF *dotdsf;

    /* Information for Normal level deductions:
     * For each dline, store a bitmask for whether we know:
     * (bit 0) at least one is YES
     * (bit 1) at most one is YES */
    char *dlines;

    /* Hard level information */
    DSF *linedsf;
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


struct game_drawstate {
    bool started;
    int tilesize;
    bool flashing;
    int *textx, *texty;
    char *lines;
    bool *clue_error;
    bool *clue_satisfied;
};

static const char *validate_desc(const game_params *params, const char *desc);
static int dot_order(const game_state* state, int i, char line_type);
static int face_order(const game_state* state, int i, char line_type);
static solver_state *solve_game_rec(const solver_state *sstate);

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate);
#else
#define check_caches(s)
#endif

/*
 * Grid type config options available in Loopy.
 *
 * Annoyingly, we have to use an enum here which doesn't match up
 * exactly to the grid-type enum in grid.h. Values in params->types
 * are given by names such as LOOPY_GRID_SQUARE, which shouldn't be
 * confused with GRID_SQUARE which is the value you pass to grid_new()
 * and friends. So beware!
 *
 * (This is partly for historical reasons - Loopy's version of the
 * enum is encoded in game parameter strings, so we keep it for
 * backwards compatibility. But also, we need to store additional data
 * here alongside each enum value, such as names for the presets menu,
 * which isn't stored in grid.h; so we have to have our own list macro
 * here anyway, and C doesn't make it easy to enforce that that lines
 * up exactly with grid.h.)
 *
 * Do not add values to this list _except_ at the end, or old game ids
 * will stop working!
 */
#define GRIDLIST(A)                                             \
    A("Squares",SQUARE,3,3)                                     \
    A("Triangular",TRIANGULAR,3,3)                              \
    A("Honeycomb",HONEYCOMB,3,3)                                \
    A("Snub-Square",SNUBSQUARE,3,3)                             \
    A("Cairo",CAIRO,3,4)                                        \
    A("Great-Hexagonal",GREATHEXAGONAL,3,3)                     \
    A("Octagonal",OCTAGONAL,3,3)                                \
    A("Kites",KITE,3,3)                                         \
    A("Floret",FLORET,1,2)                                      \
    A("Dodecagonal",DODECAGONAL,2,2)                            \
    A("Great-Dodecagonal",GREATDODECAGONAL,2,2)                 \
    A("Penrose (kite/dart)",PENROSE_P2,3,3)                     \
    A("Penrose (rhombs)",PENROSE_P3,3,3)                        \
    A("Great-Great-Dodecagonal",GREATGREATDODECAGONAL,2,2)      \
    A("Kagome",KAGOME,3,3)                                      \
    A("Compass-Dodecagonal",COMPASSDODECAGONAL,2,2)             \
    A("Hats",HATS,6,6)                                          \
    A("Spectres",SPECTRES,6,6)                                  \
    /* end of list */

#define GRID_NAME(title,type,amin,omin) title,
#define GRID_CONFIG(title,type,amin,omin) ":" title
#define GRID_LOOPYTYPE(title,type,amin,omin) LOOPY_GRID_ ## type,
#define GRID_GRIDTYPE(title,type,amin,omin) GRID_ ## type,
#define GRID_SIZES(title,type,amin,omin) \
    {amin, omin, \
     "Width and height for this grid type must both be at least " #amin, \
     "At least one of width and height for this grid type must be at least " #omin,},
enum { GRIDLIST(GRID_LOOPYTYPE) LOOPY_GRID_DUMMY_TERMINATOR };
static char const *const gridnames[] = { GRIDLIST(GRID_NAME) };
#define GRID_CONFIGS GRIDLIST(GRID_CONFIG)
static grid_type grid_types[] = { GRIDLIST(GRID_GRIDTYPE) };
#define NUM_GRID_TYPES (sizeof(grid_types) / sizeof(grid_types[0]))
static const struct {
    int amin, omin;
    const char *aerr, *oerr;
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

#define SET_BIT(field, bit)  (BIT_SET(field, bit) ? false : \
                              ((field) |= (1<<(bit)), true))

#define CLEAR_BIT(field, bit) (BIT_SET(field, bit) ? \
                               ((field) &= ~(1<<(bit)), true) : false)

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

    ret->line_errors = snewn(state->game_grid->num_edges, bool);
    memcpy(ret->line_errors, state->line_errors,
           state->game_grid->num_edges * sizeof(bool));
    ret->exactly_one_loop = state->exactly_one_loop;

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

    ret->dotdsf = dsf_new(num_dots);
    ret->looplen = snewn(num_dots, int);

    for (i = 0; i < num_dots; i++) {
        ret->looplen[i] = 1;
    }

    ret->dot_solved = snewn(num_dots, bool);
    ret->face_solved = snewn(num_faces, bool);
    memset(ret->dot_solved, 0, num_dots * sizeof(bool));
    memset(ret->face_solved, 0, num_faces * sizeof(bool));

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
        ret->linedsf = dsf_new_flip(state->game_grid->num_edges);
    }

    return ret;
}

static void free_solver_state(solver_state *sstate) {
    if (sstate) {
        free_game(sstate->state);
        dsf_free(sstate->dotdsf);
        sfree(sstate->looplen);
        sfree(sstate->dot_solved);
        sfree(sstate->face_solved);
        sfree(sstate->dot_yes_count);
        sfree(sstate->dot_no_count);
        sfree(sstate->face_yes_count);
        sfree(sstate->face_no_count);

        /* OK, because sfree(NULL) is a no-op */
        sfree(sstate->dlines);
        dsf_free(sstate->linedsf);

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

    ret->dotdsf = dsf_new(num_dots);
    ret->looplen = snewn(num_dots, int);
    dsf_copy(ret->dotdsf, sstate->dotdsf);
    memcpy(ret->looplen, sstate->looplen,
           num_dots * sizeof(int));

    ret->dot_solved = snewn(num_dots, bool);
    ret->face_solved = snewn(num_faces, bool);
    memcpy(ret->dot_solved, sstate->dot_solved, num_dots * sizeof(bool));
    memcpy(ret->face_solved, sstate->face_solved, num_faces * sizeof(bool));

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
        ret->linedsf = dsf_new_flip(num_edges);
        dsf_copy(ret->linedsf, sstate->linedsf);
    } else {
        ret->linedsf = NULL;
    }

    return ret;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

#ifdef SLOW_SYSTEM
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

static const game_params loopy_presets_top[] = {
#ifdef SMALL_SCREEN
    {  7,  7, DIFF_EASY,   LOOPY_GRID_SQUARE },
    {  7,  7, DIFF_NORMAL, LOOPY_GRID_SQUARE },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_SQUARE },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_TRIANGULAR },
    {  5,  5, DIFF_HARD,   LOOPY_GRID_SNUBSQUARE },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_CAIRO },
    {  5,  5, DIFF_HARD,   LOOPY_GRID_KITE },
    {  6,  6, DIFF_HARD,   LOOPY_GRID_PENROSE_P2 },
    {  6,  6, DIFF_HARD,   LOOPY_GRID_PENROSE_P3 },
#else
    {  7,  7, DIFF_EASY,   LOOPY_GRID_SQUARE },
    { 10, 10, DIFF_EASY,   LOOPY_GRID_SQUARE },
    {  7,  7, DIFF_NORMAL, LOOPY_GRID_SQUARE },
    { 10, 10, DIFF_NORMAL, LOOPY_GRID_SQUARE },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_SQUARE },
    { 10, 10, DIFF_HARD,   LOOPY_GRID_SQUARE },
    { 12, 10, DIFF_HARD,   LOOPY_GRID_TRIANGULAR },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_SNUBSQUARE },
    {  9,  9, DIFF_HARD,   LOOPY_GRID_CAIRO },
    {  5,  5, DIFF_HARD,   LOOPY_GRID_KITE },
    { 10, 10, DIFF_HARD,   LOOPY_GRID_PENROSE_P2 },
    { 10, 10, DIFF_HARD,   LOOPY_GRID_PENROSE_P3 },
#endif
};

static const game_params loopy_presets_more[] = {
#ifdef SMALL_SCREEN
    {  7,  7, DIFF_HARD,   LOOPY_GRID_HONEYCOMB },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_GREATHEXAGONAL },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_KAGOME },
    {  5,  5, DIFF_HARD,   LOOPY_GRID_OCTAGONAL },
    {  3,  3, DIFF_HARD,   LOOPY_GRID_FLORET },
    {  3,  3, DIFF_HARD,   LOOPY_GRID_DODECAGONAL },
    {  3,  3, DIFF_HARD,   LOOPY_GRID_GREATDODECAGONAL },
    {  3,  2, DIFF_HARD,   LOOPY_GRID_GREATGREATDODECAGONAL },
    {  3,  3, DIFF_HARD,   LOOPY_GRID_COMPASSDODECAGONAL },
    {  6,  6, DIFF_HARD,   LOOPY_GRID_HATS },
    {  6,  6, DIFF_HARD,   LOOPY_GRID_SPECTRES },
#else
    { 10, 10, DIFF_HARD,   LOOPY_GRID_HONEYCOMB },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_GREATHEXAGONAL },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_KAGOME },
    {  7,  7, DIFF_HARD,   LOOPY_GRID_OCTAGONAL },
    {  5,  5, DIFF_HARD,   LOOPY_GRID_FLORET },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_DODECAGONAL },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_GREATDODECAGONAL },
    {  5,  3, DIFF_HARD,   LOOPY_GRID_GREATGREATDODECAGONAL },
    {  5,  4, DIFF_HARD,   LOOPY_GRID_COMPASSDODECAGONAL },
    { 10, 10, DIFF_HARD,   LOOPY_GRID_HATS },
    { 10, 10, DIFF_HARD,   LOOPY_GRID_SPECTRES },
#endif
};

static void preset_menu_add_preset_with_title(struct preset_menu *menu,
                                              const game_params *params)
{
    char buf[80];
    game_params *dup_params;

    sprintf(buf, "%dx%d %s - %s", params->h, params->w,
            gridnames[params->type], diffnames[params->diff]);

    dup_params = snew(game_params);
    *dup_params = *params;

    preset_menu_add_preset(menu, dupstr(buf), dup_params);
}

static struct preset_menu *game_preset_menu(void)
{
    struct preset_menu *top, *more;
    int i;

    top = preset_menu_new();
    for (i = 0; i < lenof(loopy_presets_top); i++)
        preset_menu_add_preset_with_title(top, &loopy_presets_top[i]);

    more = preset_menu_add_submenu(top, dupstr("More..."));
    for (i = 0; i < lenof(loopy_presets_more); i++)
        preset_menu_add_preset_with_title(more, &loopy_presets_more[i]);

    return top;
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

static char *encode_params(const game_params *params, bool full)
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

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Grid type";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = GRID_CONFIGS;
    ret[2].u.choices.selected = params->type;

    ret[3].name = "Difficulty";
    ret[3].type = C_CHOICES;
    ret[3].u.choices.choicenames = DIFFCONFIG;
    ret[3].u.choices.selected = params->diff;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->type = cfg[2].u.choices.selected;
    ret->diff = cfg[3].u.choices.selected;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    const char *err;
    if (params->type < 0 || params->type >= NUM_GRID_TYPES)
        return "Illegal grid type";
    if (params->w < grid_size_limits[params->type].amin ||
	params->h < grid_size_limits[params->type].amin)
        return grid_size_limits[params->type].aerr;
    if (params->w < grid_size_limits[params->type].omin &&
	params->h < grid_size_limits[params->type].omin)
        return grid_size_limits[params->type].oerr;
    err = grid_validate_params(grid_types[params->type], params->w, params->h);
    if (err != NULL) return err;

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
static const char *validate_desc(const game_params *params, const char *desc)
{
    int count = 0;
    grid *g;
    char *grid_desc;
    const char *ret;

    /* It's pretty inefficient to do this just for validation. All we need to
     * know is the precise number of faces. */
    grid_desc = extract_grid_desc(&desc);
    ret = grid_validate_desc(grid_types[params->type], params->w, params->h, grid_desc);
    if (ret) {
        sfree(grid_desc);
        return ret;
    }

    g = loopy_generate_grid(params, grid_desc);
    sfree(grid_desc);

    for (; *desc; ++desc) {
        if ((*desc >= '0' && *desc <= '9') || (*desc >= 'A' && *desc <= 'Z')) {
            count++;
            continue;
        }
        if (*desc >= 'a') {
            count += *desc - 'a' + 1;
            continue;
        }
        grid_free(g);
        return "Unknown character in description";
    }

    if (count < g->num_faces) {
        grid_free(g);
        return "Description too short for board size";
    }
    if (count > g->num_faces) {
        grid_free(g);
        return "Description too long for board size";
    }

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
    /*
     * User preference: should grid lines in LINE_NO state be drawn
     * very faintly so users can still see where they are, or should
     * they be completely invisible?
     */
    bool draw_faint_lines;

    /*
     * User preference: when clicking an edge that has only one
     * possible edge connecting to one (or both) of its ends, should
     * that edge also change to the same state as the edge we just
     * clicked?
     */
    enum {
        AF_OFF,     /* no, all grid edges are independent in the UI */
        AF_FIXED,   /* yes, but only based on the grid itself */
        AF_ADAPTIVE /* yes, and consider edges user has already set to NO */
    } autofollow;
};

static void legacy_prefs_override(struct game_ui *ui_out)
{
    static bool initialised = false;
    static int draw_faint_lines = -1;
    static int autofollow = -1;

    if (!initialised) {
        char *env;

        initialised = true;
        draw_faint_lines = getenv_bool("LOOPY_FAINT_LINES", -1);

        if ((env = getenv("LOOPY_AUTOFOLLOW")) != NULL) {
            if (!strcmp(env, "off"))
                autofollow = AF_OFF;
            else if (!strcmp(env, "fixed"))
                autofollow = AF_FIXED;
            else if (!strcmp(env, "adaptive"))
                autofollow = AF_ADAPTIVE;
        }
    }

    if (draw_faint_lines != -1)
        ui_out->draw_faint_lines = draw_faint_lines;
    if (autofollow != -1)
        ui_out->autofollow = autofollow;
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->draw_faint_lines = true;
    ui->autofollow = AF_OFF;
    legacy_prefs_override(ui);
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static config_item *get_prefs(game_ui *ui)
{
    config_item *ret;

    ret = snewn(N_PREF_ITEMS+1, config_item);

    ret[PREF_DRAW_FAINT_LINES].name = "Draw excluded grid lines faintly";
    ret[PREF_DRAW_FAINT_LINES].kw = "draw-faint-lines";
    ret[PREF_DRAW_FAINT_LINES].type = C_BOOLEAN;
    ret[PREF_DRAW_FAINT_LINES].u.boolean.bval = ui->draw_faint_lines;

    ret[PREF_AUTO_FOLLOW].name = "Auto-follow unique paths of edges";
    ret[PREF_AUTO_FOLLOW].kw = "auto-follow";
    ret[PREF_AUTO_FOLLOW].type = C_CHOICES;
    ret[PREF_AUTO_FOLLOW].u.choices.choicenames =
        ":No:Based on grid only:Based on grid and game state";
    ret[PREF_AUTO_FOLLOW].u.choices.choicekws = ":off:fixed:adaptive";
    ret[PREF_AUTO_FOLLOW].u.choices.selected = ui->autofollow;

    ret[N_PREF_ITEMS].name = NULL;
    ret[N_PREF_ITEMS].type = C_END;

    return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
    ui->draw_faint_lines = cfg[PREF_DRAW_FAINT_LINES].u.boolean.bval;
    ui->autofollow = cfg[PREF_AUTO_FOLLOW].u.choices.selected;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
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

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
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
    ds->started = false;
    ds->lines = snewn(num_edges, char);
    ds->clue_error = snewn(num_faces, bool);
    ds->clue_satisfied = snewn(num_faces, bool);
    ds->textx = snewn(num_faces, int);
    ds->texty = snewn(num_faces, int);
    ds->flashing = false;

    memset(ds->lines, LINE_UNKNOWN, num_edges);
    memset(ds->clue_error, 0, num_faces * sizeof(bool));
    memset(ds->clue_satisfied, 0, num_faces * sizeof(bool));
    for (i = 0; i < num_faces; i++)
        ds->textx[i] = ds->texty[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->textx);
    sfree(ds->texty);
    sfree(ds->clue_error);
    sfree(ds->clue_satisfied);
    sfree(ds->lines);
    sfree(ds);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    if (params->type != 0)
        return false;
    return true;
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
    f = g->faces[0]; /* first face */
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
        grid_edge *e = g->edges[i];
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

        f = g->faces[i];
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
 * Returns true if this actually changed the line's state. */
static bool solver_set_line(solver_state *sstate, int i,
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
        return false; /* nothing changed */
    }
    state->lines[i] = line_new;

#ifdef SHOW_WORKING
    fprintf(stderr, "solver: set line [%d] to %s (%s)\n",
            i, line_new == LINE_YES ? "YES" : "NO",
            reason);
#endif

    g = state->game_grid;
    e = g->edges[i];

    /* Update the cache for both dots and both faces affected by this. */
    if (line_new == LINE_YES) {
        sstate->dot_yes_count[e->dot1->index]++;
        sstate->dot_yes_count[e->dot2->index]++;
        if (e->face1) {
            sstate->face_yes_count[e->face1->index]++;
        }
        if (e->face2) {
            sstate->face_yes_count[e->face2->index]++;
        }
    } else {
        sstate->dot_no_count[e->dot1->index]++;
        sstate->dot_no_count[e->dot2->index]++;
        if (e->face1) {
            sstate->face_no_count[e->face1->index]++;
        }
        if (e->face2) {
            sstate->face_no_count[e->face2->index]++;
        }
    }

    check_caches(sstate);
    return true;
}

#ifdef SHOW_WORKING
#define solver_set_line(a, b, c) \
    solver_set_line(a, b, c, __FUNCTION__)
#endif

/*
 * Merge two dots due to the existence of an edge between them.
 * Updates the dsf tracking equivalence classes, and keeps track of
 * the length of path each dot is currently a part of.
 * Returns true if the dots were already linked, ie if they are part of a
 * closed loop, and false otherwise.
 */
static bool merge_dots(solver_state *sstate, int edge_index)
{
    int i, j, len;
    grid *g = sstate->state->game_grid;
    grid_edge *e = g->edges[edge_index];

    i = e->dot1->index;
    j = e->dot2->index;

    i = dsf_canonify(sstate->dotdsf, i);
    j = dsf_canonify(sstate->dotdsf, j);

    if (i == j) {
        return true;
    } else {
        len = sstate->looplen[i] + sstate->looplen[j];
        dsf_merge(sstate->dotdsf, i, j);
        i = dsf_canonify(sstate->dotdsf, i);
        sstate->looplen[i] = len;
        return false;
    }
}

/* Merge two lines because the solver has deduced that they must be either
 * identical or opposite.   Returns true if this is new information, otherwise
 * false. */
static bool merge_lines(solver_state *sstate, int i, int j, bool inverse
#ifdef SHOW_WORKING
                        , const char *reason
#endif
                        )
{
    bool inv_tmp;

    assert(i < sstate->state->game_grid->num_edges);
    assert(j < sstate->state->game_grid->num_edges);

    i = dsf_canonify_flip(sstate->linedsf, i, &inv_tmp);
    inverse ^= inv_tmp;
    j = dsf_canonify_flip(sstate->linedsf, j, &inv_tmp);
    inverse ^= inv_tmp;

    dsf_merge_flip(sstate->linedsf, i, j, inverse);

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
    grid_dot *d = g->dots[dot];
    int i;

    for (i = 0; i < d->order; i++) {
        grid_edge *e = d->edges[i];
        if (state->lines[e->index] == line_type)
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
    grid_face *f = g->faces[face];
    int i;

    for (i = 0; i < f->order; i++) {
        grid_edge *e = f->edges[i];
        if (state->lines[e->index] == line_type)
            ++n;
    }
    return n;
}

/* Set all lines bordering a dot of type old_type to type new_type
 * Return value tells caller whether this function actually did anything */
static bool dot_setall(solver_state *sstate, int dot,
                       char old_type, char new_type)
{
    bool retval = false, r;
    game_state *state = sstate->state;
    grid *g;
    grid_dot *d;
    int i;

    if (old_type == new_type)
        return false;

    g = state->game_grid;
    d = g->dots[dot];

    for (i = 0; i < d->order; i++) {
        int line_index = d->edges[i]->index;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r);
            retval = true;
        }
    }
    return retval;
}

/* Set all lines bordering a face of type old_type to type new_type */
static bool face_setall(solver_state *sstate, int face,
                        char old_type, char new_type)
{
    bool retval = false, r;
    game_state *state = sstate->state;
    grid *g;
    grid_face *f;
    int i;

    if (old_type == new_type)
        return false;

    g = state->game_grid;
    f = g->faces[face];

    for (i = 0; i < f->order; i++) {
        int line_index = f->edges[i]->index;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r);
            retval = true;
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
        grid_edge *e = g->edges[i];
        grid_face *f1 = e->face1;
        grid_face *f2 = e->face2;
        enum face_colour c1 = FACE_COLOUR(f1);
        enum face_colour c2 = FACE_COLOUR(f2);
        assert(c1 != FACE_GREY);
        assert(c2 != FACE_GREY);
        if (c1 != c2) {
            if (f1) clues[f1->index]++;
            if (f2) clues[f2->index]++;
        }
    }
    sfree(board);
}


static bool game_has_unique_soln(const game_state *state, int diff)
{
    bool ret;
    solver_state *sstate_new;
    solver_state *sstate = new_solver_state(state, diff);

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
                           char **aux, bool interactive)
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
    state->line_errors = snewn(g->num_edges, bool);
    state->exactly_one_loop = false;

    state->grid_type = params->type;

    newboard_please:

    memset(state->lines, LINE_UNKNOWN, g->num_edges);
    memset(state->line_errors, 0, g->num_edges * sizeof(bool));

    state->solved = false;
    state->cheated = false;

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
    state->line_errors = snewn(num_edges, bool);
    state->exactly_one_loop = false;

    state->solved = state->cheated = false;

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
    memset(state->line_errors, 0, num_edges * sizeof(bool));
    return state;
}

/* Calculates the line_errors data, and checks if the current state is a
 * solution */
static bool check_completion(game_state *state)
{
    grid *g = state->game_grid;
    int i;
    bool ret;
    DSF *dsf;
    int *component_state;
    int nsilly, nloop, npath, largest_comp, largest_size, total_pathsize;
    enum { COMP_NONE, COMP_LOOP, COMP_PATH, COMP_SILLY, COMP_EMPTY };

    memset(state->line_errors, 0, g->num_edges * sizeof(bool));

    /*
     * Find loops in the grid, and determine whether the puzzle is
     * solved.
     *
     * Loopy is a bit more complicated than most puzzles that care
     * about loop detection. In most of them, loops are simply
     * _forbidden_; so the obviously right way to do
     * error-highlighting during play is to light up a graph edge red
     * iff it is part of a loop, which is exactly what the centralised
     * findloop.c makes easy.
     *
     * But Loopy is unusual in that you're _supposed_ to be making a
     * loop - and yet _some_ loops are not the right loop. So we need
     * to be more discriminating, by identifying loops one by one and
     * then thinking about which ones to highlight, and so findloop.c
     * isn't quite the right tool for the job in this case.
     *
     * Worse still, consider situations in which the grid contains a
     * loop and also some non-loop edges: there are some cases like
     * this in which the user's intuitive expectation would be to
     * highlight the loop (if you're only about half way through the
     * puzzle and have accidentally made a little loop in some corner
     * of the grid), and others in which they'd be more likely to
     * expect you to highlight the non-loop edges (if you've just
     * closed off a whole loop that you thought was the entire
     * solution, but forgot some disconnected edges in a corner
     * somewhere). So while it's easy enough to check whether the
     * solution is _right_, highlighting the wrong parts is a tricky
     * problem for this puzzle!
     *
     * I'd quite like, in some situations, to identify the largest
     * loop among the player's YES edges, and then light up everything
     * other than that. But finding the longest cycle in a graph is an
     * NP-complete problem (because, in particular, it must return a
     * Hamilton cycle if one exists).
     *
     * However, I think we can make the problem tractable by
     * exercising the Puzzles principle that it isn't absolutely
     * necessary to highlight _all_ errors: the key point is that by
     * the time the user has filled in the whole grid, they should
     * either have seen a completion flash, or have _some_ error
     * highlight showing them why the solution isn't right. So in
     * principle it would be *just about* good enough to highlight
     * just one error in the whole grid, if there was really no better
     * way. But we'd like to highlight as many errors as possible.
     *
     * In this case, I think the simple approach is to make use of the
     * fact that no vertex may have degree > 2, and that's really
     * simple to detect. So the plan goes like this:
     *
     *  - Form the dsf of connected components of the graph vertices.
     *
     *  - Highlight an error at any vertex with degree > 2. (It so
     *    happens that we do this by lighting up all the edges
     *    incident to that vertex, but that's an output detail.)
     *
     *  - Any component that contains such a vertex is now excluded
     *    from further consideration, because it already has a
     *    highlight.
     *
     *  - The remaining components have no vertex with degree > 2, and
     *    hence they all consist of either a simple loop, or a simple
     *    path with two endpoints.
     *
     *  - For these purposes, group together all the paths and imagine
     *    them to be a single component (because in most normal
     *    situations the player will gradually build up the solution
     *    _not_ all in one connected segment, but as lots of separate
     *    little path pieces that gradually connect to each other).
     *
     *  - After doing that, if there is exactly one (sensible)
     *    component - be it a collection of paths or a loop - then
     *    highlight no further edge errors. (The former case is normal
     *    during play, and the latter is a potentially solved puzzle.)
     *
     *  - Otherwise, find the largest of the sensible components,
     *    leave that one unhighlighted, and light the rest up in red.
     */

    dsf = dsf_new(g->num_dots);

    /* Build the dsf. */
    for (i = 0; i < g->num_edges; i++) {
        if (state->lines[i] == LINE_YES) {
            grid_edge *e = g->edges[i];
            int d1 = e->dot1->index, d2 = e->dot2->index;
            dsf_merge(dsf, d1, d2);
        }
    }

    /* Initialise a state variable for each connected component. */
    component_state = snewn(g->num_dots, int);
    for (i = 0; i < g->num_dots; i++) {
        if (dsf_canonify(dsf, i) == i)
            component_state[i] = COMP_LOOP;
        else
            component_state[i] = COMP_NONE;
    }

    /* Check for dots with degree > 3. Here we also spot dots of
     * degree 1 in which the user has marked all the non-edges as
     * LINE_NO, because those are also clear vertex-level errors, so
     * we give them the same treatment of excluding their connected
     * component from the subsequent loop analysis. */
    for (i = 0; i < g->num_dots; i++) {
        int comp = dsf_canonify(dsf, i);
        int yes = dot_order(state, i, LINE_YES);
        int unknown = dot_order(state, i, LINE_UNKNOWN);
        if ((yes == 1 && unknown == 0) || (yes >= 3)) {
            /* violation, so mark all YES edges as errors */
            grid_dot *d = g->dots[i];
            int j;
            for (j = 0; j < d->order; j++) {
                int e = d->edges[j]->index;
                if (state->lines[e] == LINE_YES)
                    state->line_errors[e] = true;
            }
            /* And mark this component as not worthy of further
             * consideration. */
            component_state[comp] = COMP_SILLY;

        } else if (yes == 0) {
            /* A completely isolated dot must also be excluded it from
             * the subsequent loop highlighting pass, but we tag it
             * with a different enum value to avoid it counting
             * towards the components that inhibit returning a win
             * status. */
            component_state[comp] = COMP_EMPTY;
        } else if (yes == 1) {
            /* A dot with degree 1 that didn't fall into the 'clearly
             * erroneous' case above indicates that this connected
             * component will be a path rather than a loop - unless
             * something worse elsewhere in the component has
             * classified it as silly. */
            if (component_state[comp] != COMP_SILLY)
                component_state[comp] = COMP_PATH;
        }
    }

    /* Count up the components. Also, find the largest sensible
     * component. (Tie-breaking condition is derived from the order of
     * vertices in the grid data structure, which is fairly arbitrary
     * but at least stays stable throughout the game.) */
    nsilly = nloop = npath = 0;
    total_pathsize = 0;
    largest_comp = largest_size = -1;
    for (i = 0; i < g->num_dots; i++) {
        if (component_state[i] == COMP_SILLY) {
            nsilly++;
        } else if (component_state[i] == COMP_PATH) {
            total_pathsize += dsf_size(dsf, i);
            npath = 1;
        } else if (component_state[i] == COMP_LOOP) {
            int this_size;

            nloop++;

            if ((this_size = dsf_size(dsf, i)) > largest_size) {
                largest_comp = i;
                largest_size = this_size;
            }
        }
    }
    if (largest_size < total_pathsize) {
        largest_comp = -1;             /* means the paths */
        largest_size = total_pathsize;
    }

    if (nloop > 0 && nloop + npath > 1) {
        /*
         * If there are at least two sensible components including at
         * least one loop, highlight all edges in every sensible
         * component that is not the largest one.
         */
        for (i = 0; i < g->num_edges; i++) {
            if (state->lines[i] == LINE_YES) {
                grid_edge *e = g->edges[i];
                int d1 = e->dot1->index; /* either endpoint is good enough */
                int comp = dsf_canonify(dsf, d1);
                if ((component_state[comp] == COMP_PATH &&
                     -1 != largest_comp) ||
                    (component_state[comp] == COMP_LOOP &&
                     comp != largest_comp))
                    state->line_errors[i] = true;
            }
        }
    }

    if (nloop == 1 && npath == 0 && nsilly == 0) {
        /*
         * If there is exactly one component and it is a loop, then
         * the puzzle is potentially complete, so check the clues.
         */
        ret = true;

        for (i = 0; i < g->num_faces; i++) {
            int c = state->clues[i];
            if (c >= 0 && face_order(state, i, LINE_YES) != c) {
                ret = false;
                break;
            }
        }

        /*
         * Also, whether or not the puzzle is actually complete, set
         * the flag that says this game_state has exactly one loop and
         * nothing else, which will be used to vary the semantics of
         * clue highlighting at display time.
         */
        state->exactly_one_loop = true;
    } else {
        ret = false;
        state->exactly_one_loop = false;
    }

    sfree(component_state);
    dsf_free(dsf);

    return ret;
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
 *   Use flip dsf data structure to make equivalence classes of lines that are
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
 * true, use edge->dot1 else use edge->dot2.  So the total number of dlines is
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
    ret = 2 * (e->index) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_dot: d=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(d->index), i, (int)(e->index), (int)(e2 ->index), ret);
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
    ret = 2 * (e->index) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_face: f=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(f->index), i, (int)(e->index), (int)(e2->index), ret);
#endif
    return ret;
}
static bool is_atleastone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 0);
}
static bool set_atleastone(char *dline_array, int index)
{
    return SET_BIT(dline_array[index], 0);
}
static bool is_atmostone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 1);
}
static bool set_atmostone(char *dline_array, int index)
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
static bool dline_set_opp_atleastone(solver_state *sstate,
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
        if (state->lines[d->edges[opp]->index] != LINE_UNKNOWN)
            continue;
        if (state->lines[d->edges[opp2]->index] != LINE_UNKNOWN)
            continue;
        /* Found opposite UNKNOWNS and they're next to each other */
        opp_dline_index = dline_index_from_dot(g, d, opp);
        return set_atleastone(sstate->dlines, opp_dline_index);
    }
    return false;
}


/* Set pairs of lines around this face which are known to be identical, to
 * the given line_state */
static bool face_setall_identical(solver_state *sstate, int face_index,
                                  enum line_state line_new)
{
    /* can[dir] contains the canonical line associated with the line in
     * direction dir from the square in question.  Similarly inv[dir] is
     * whether or not the line in question is inverse to its canonical
     * element. */
    bool retval = false;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    grid_face *f = g->faces[face_index];
    int N = f->order;
    int i, j;
    int can1, can2;
    bool inv1, inv2;

    for (i = 0; i < N; i++) {
        int line1_index = f->edges[i]->index;
        if (state->lines[line1_index] != LINE_UNKNOWN)
            continue;
        for (j = i + 1; j < N; j++) {
            int line2_index = f->edges[j]->index;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;

            /* Found two UNKNOWNS */
            can1 = dsf_canonify_flip(sstate->linedsf, line1_index, &inv1);
            can2 = dsf_canonify_flip(sstate->linedsf, line2_index, &inv2);
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
    while (c < expected_count) {
        int line_index = (*edge_list)->index;
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
    DSF *linedsf = sstate->linedsf;

    if (unknown_count == 2) {
        /* Lines are known alike/opposite, depending on inv. */
        int e[2];
        find_unknowns(state, edge_list, 2, e);
        if (merge_lines(sstate, e[0], e[1], total_parity))
            diff = min(diff, DIFF_HARD);
    } else if (unknown_count == 3) {
        int e[3];
        int can[3]; /* canonical edges */
        bool inv[3]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 3, e);
        can[0] = dsf_canonify_flip(linedsf, e[0], inv);
        can[1] = dsf_canonify_flip(linedsf, e[1], inv+1);
        can[2] = dsf_canonify_flip(linedsf, e[2], inv+2);
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
        bool inv[4]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 4, e);
        can[0] = dsf_canonify_flip(linedsf, e[0], inv);
        can[1] = dsf_canonify_flip(linedsf, e[1], inv+1);
        can[2] = dsf_canonify_flip(linedsf, e[2], inv+2);
        can[3] = dsf_canonify_flip(linedsf, e[3], inv+3);
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
        grid_face *f = g->faces[i];

        if (sstate->face_solved[i])
            continue;

        current_yes = sstate->face_yes_count[i];
        current_no  = sstate->face_no_count[i];

        if (current_yes + current_no == f->order)  {
            sstate->face_solved[i] = true;
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
            sstate->face_solved[i] = true;
            continue;
        }

        if (f->order - state->clues[i] < current_no) {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (f->order - state->clues[i] == current_no) {
            if (face_setall(sstate, i, LINE_UNKNOWN, LINE_YES))
                diff = min(diff, DIFF_EASY);
            sstate->face_solved[i] = true;
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
                e1 = f->edges[j]->index;
                e2 = f->edges[j+1 < f->order ? j+1 : 0]->index;

                if (g->edges[e1]->dot1 == g->edges[e2]->dot1 ||
                    g->edges[e1]->dot1 == g->edges[e2]->dot2) {
                    d = g->edges[e1]->dot1->index;
                } else {
                    assert(g->edges[e1]->dot2 == g->edges[e2]->dot1 ||
                           g->edges[e1]->dot2 == g->edges[e2]->dot2);
                    d = g->edges[e1]->dot2->index;
                }

                if (state->lines[e1] == LINE_UNKNOWN &&
                    state->lines[e2] == LINE_UNKNOWN) {
                    for (k = 0; k < g->dots[d]->order; k++) {
                        int e = g->dots[d]->edges[k]->index;
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
                e = f->edges[j]->index;
                if (state->lines[e] == LINE_UNKNOWN && e != e1 && e != e2) {
                    bool r = solver_set_line(sstate, e, LINE_YES);
                    assert(r);
                    diff = min(diff, DIFF_EASY);
                }
            }
        }
    }

    check_caches(sstate);

    /* Per-dot deductions */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots[i];
        int yes, no, unknown;

        if (sstate->dot_solved[i])
            continue;

        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = d->order - yes - no;

        if (yes == 0) {
            if (unknown == 0) {
                sstate->dot_solved[i] = true;
            } else if (unknown == 1) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
                sstate->dot_solved[i] = true;
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
            sstate->dot_solved[i] = true;
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
#define MAX_FACE_SIZE 14

    for (i = 0; i < g->num_faces; i++) {
        int maxs[MAX_FACE_SIZE][MAX_FACE_SIZE];
        int mins[MAX_FACE_SIZE][MAX_FACE_SIZE];
        grid_face *f = g->faces[i];
        int N = f->order;
        int j,m;
        int clue = state->clues[i];
        assert(N <= MAX_FACE_SIZE);
        if (sstate->face_solved[i])
            continue;
        if (clue < 0) continue;

        /* Calculate the (j,j+1) entries */
        for (j = 0; j < N; j++) {
            int edge_index = f->edges[j]->index;
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
            edge_index = f->edges[k]->index;
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
            int line_index = e->index;
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
                if (state->lines[e->index] != LINE_UNKNOWN)
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
        grid_dot *d = g->dots[i];
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
            line1_index = d->edges[j]->index;
            line2_index = d->edges[k] ->index;
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
                                opp_index = d->edges[opp]->index;
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

        N = g->faces[i]->order;
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
        diff_tmp = parity_deductions(sstate, g->faces[i]->edges,
                                     (clue - yes) % 2, unknown);
        diff = min(diff, diff_tmp);
    }

    /* ------ Dot deductions ------ */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots[i];
        int N = d->order;
        int j;
        int yes, no, unknown;
        /* Go through dlines, and do any dline<->linedsf deductions wherever
         * we find two UNKNOWNS. */
        for (j = 0; j < N; j++) {
            int dline_index = dline_index_from_dot(g, d, j);
            int line1_index;
            int line2_index;
            int can1, can2;
            bool inv1, inv2;
            int j2;
            line1_index = d->edges[j]->index;
            if (state->lines[line1_index] != LINE_UNKNOWN)
                continue;
            j2 = j + 1;
            if (j2 == N) j2 = 0;
            line2_index = d->edges[j2]->index;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;
            /* Infer dline flags from linedsf */
            can1 = dsf_canonify_flip(sstate->linedsf, line1_index, &inv1);
            can2 = dsf_canonify_flip(sstate->linedsf, line2_index, &inv2);
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
                if (merge_lines(sstate, line1_index, line2_index, true))
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
        int can;
        bool inv;
        enum line_state s;
        can = dsf_canonify_flip(sstate->linedsf, i, &inv);
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
    int dots_connected;
    bool progress = false;
    int i;

    /*
     * Go through the grid and update for all the new edges.
     * Since merge_dots() is idempotent, the simplest way to
     * do this is just to update for _all_ the edges.
     * Also, while we're here, we count the edges.
     */
    for (i = 0; i < g->num_edges; i++) {
        if (state->lines[i] == LINE_YES) {
            merge_dots(sstate, i);
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
        progress = true;
        goto finished_loop_deductionsing;
    }

    /*
     * Now go through looking for LINE_UNKNOWN edges which
     * connect two dots that are already in the same
     * equivalence class. If we find one, test to see if the
     * loop it would create is a solution.
     */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges[i];
        int d1 = e->dot1->index;
        int d2 = e->dot2->index;
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
                int f = e->face1->index;
                int c = state->clues[f];
                if (c >= 0 && sstate->face_yes_count[f] == c - 1)
                    sm1_nearby++;
            }
            if (e->face2) {
                int f = e->face2->index;
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
        assert(progress);
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
                        const char *aux, const char **error)
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
    char *movebuf;
    int movelen, movesize;
    char button_char = ' ';
    enum line_state old_state;

    button = STRIP_BUTTON_MODIFIERS(button);

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

    i = e->index;

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

    movelen = 0;
    movesize = 80;
    movebuf = snewn(movesize, char);
    movelen = sprintf(movebuf, "%d%c", i, (int)button_char);

    if (ui->autofollow != AF_OFF) {
        int dotid;
        for (dotid = 0; dotid < 2; dotid++) {
            grid_dot *dot = (dotid == 0 ? e->dot1 : e->dot2);
            grid_edge *e_this = e;

            while (1) {
                int j, n_found;
                grid_edge *e_next = NULL;

                for (j = n_found = 0; j < dot->order; j++) {
                    grid_edge *e_candidate = dot->edges[j];
                    int i_candidate = e_candidate->index;
                    if (e_candidate != e_this &&
                        (ui->autofollow == AF_FIXED ||
                         state->lines[i] == LINE_NO ||
                         state->lines[i_candidate] != LINE_NO)) {
                        e_next = e_candidate;
                        n_found++;
                    }
                }

                if (n_found != 1 ||
                    state->lines[e_next->index] != state->lines[i])
                    break;

                if (e_next == e) {
                    /*
                     * Special case: we might have come all the way
                     * round a loop and found our way back to the same
                     * edge we started from. In that situation, we
                     * must terminate not only this while loop, but
                     * the 'for' outside it that was tracing in both
                     * directions from the starting edge, because if
                     * we let it trace in the second direction then
                     * we'll only find ourself traversing the same
                     * loop in the other order and generate an encoded
                     * move string that mentions the same set of edges
                     * twice.
                     */
                    goto autofollow_done;
                }

                dot = (e_next->dot1 != dot ? e_next->dot1 : e_next->dot2);
                if (movelen > movesize - 40) {
                    movesize = movesize * 5 / 4 + 128;
                    movebuf = sresize(movebuf, movesize, char);
                }
                e_this = e_next;
                movelen += sprintf(movebuf+movelen, "%d%c",
                                   (int)(e_this->index), button_char);
            }
          autofollow_done:;
        }
    }

    return sresize(movebuf, movelen+1, char);
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int i;
    game_state *newstate = dup_game(state);

    if (move[0] == 'S') {
        move++;
        newstate->cheated = true;
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
        newstate->solved = true;

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
    int faceindex = f->index;

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

    *x = xx - ds->tilesize * 5 / 4 - 1;
    *y = yy - ds->tilesize/4 - 3;
    *w = ds->tilesize * 5 / 2 + 2;
    *h = ds->tilesize/2 + 5;
}

static void game_redraw_clue(drawing *dr, game_drawstate *ds,
			     const game_state *state, int i)
{
    grid *g = state->game_grid;
    grid_face *f = g->faces[i];
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
    xmin = min(x1, x2) - (ds->tilesize + 15) / 16;
    xmax = max(x1, x2) + (ds->tilesize + 15) / 16;
    ymin = min(y1, y2) - (ds->tilesize + 15) / 16;
    ymax = max(y1, y2) + (ds->tilesize + 15) / 16;

    *x = xmin;
    *y = ymin;
    *w = xmax - xmin + 1;
    *h = ymax - ymin + 1;
}

static void dot_bbox(game_drawstate *ds, grid *g, grid_dot *d,
                     int *x, int *y, int *w, int *h)
{
    int x1, y1;
    int xmin, xmax, ymin, ymax;

    grid_to_screen(ds, g, d->x, d->y, &x1, &y1);

    xmin = x1 - (ds->tilesize * 5 + 63) / 64;
    xmax = x1 + (ds->tilesize * 5 + 63) / 64;
    ymin = y1 - (ds->tilesize * 5 + 63) / 64;
    ymax = y1 + (ds->tilesize * 5 + 63) / 64;

    *x = xmin;
    *y = ymin;
    *w = xmax - xmin + 1;
    *h = ymax - ymin + 1;
}

static const int loopy_line_redraw_phases[] = {
    COL_FAINT, COL_LINEUNKNOWN, COL_FOREGROUND, COL_HIGHLIGHT, COL_MISTAKE
};
#define NPHASES lenof(loopy_line_redraw_phases)

static void game_redraw_line(drawing *dr, game_drawstate *ds,const game_ui *ui,
			     const game_state *state, int i, int phase)
{
    grid *g = state->game_grid;
    grid_edge *e = g->edges[i];
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
	if (ui->draw_faint_lines)
            draw_thick_line(dr, ds->tilesize/24.0,
                            x1 + 0.5, y1 + 0.5,
                            x2 + 0.5, y2 + 0.5,
                            line_colour);
    } else {
	draw_thick_line(dr, ds->tilesize*3/32.0,
			x1 + 0.5, y1 + 0.5,
			x2 + 0.5, y2 + 0.5,
			line_colour);
    }
}

static void game_redraw_dot(drawing *dr, game_drawstate *ds,
			    const game_state *state, int i)
{
    grid *g = state->game_grid;
    grid_dot *d = g->dots[i];
    int x, y;

    grid_to_screen(ds, g, d->x, d->y, &x, &y);
    draw_circle(dr, x, y, ds->tilesize*2.5/32.0, COL_FOREGROUND, COL_FOREGROUND);
}

static bool boxes_intersect(int x0, int y0, int w0, int h0,
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
                                const game_ui *ui, const game_state *state,
                                int x, int y, int w, int h)
{
    grid *g = state->game_grid;
    int i, phase;
    int bx, by, bw, bh;

    clip(dr, x, y, w, h);
    draw_rect(dr, x, y, w, h, COL_BACKGROUND);

    for (i = 0; i < g->num_faces; i++) {
        if (state->clues[i] >= 0) {
            face_text_bbox(ds, g, g->faces[i], &bx, &by, &bw, &bh);
            if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
                game_redraw_clue(dr, ds, state, i);
        }
    }
    for (phase = 0; phase < NPHASES; phase++) {
        for (i = 0; i < g->num_edges; i++) {
            edge_bbox(ds, g, g->edges[i], &bx, &by, &bw, &bh);
            if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
                game_redraw_line(dr, ds, ui, state, i, phase);
        }
    }
    for (i = 0; i < g->num_dots; i++) {
        dot_bbox(ds, g, g->dots[i], &bx, &by, &bw, &bh);
        if (boxes_intersect(x, y, w, h, bx, by, bw, bh))
            game_redraw_dot(dr, ds, state, i);
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
    bool flash_changed;
    bool redraw_everything = false;

    int edges[REDRAW_OBJECTS_LIMIT], nedges = 0;
    int faces[REDRAW_OBJECTS_LIMIT], nfaces = 0;

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
	redraw_everything = true;
        /*
         * But we must still go through the upcoming loops, so that we
         * set up stuff in ds correctly for the initial redraw.
         */
    }

    /* First, trundle through the faces. */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces[i];
        int sides = f->order;
        int yes_order, no_order;
        bool clue_mistake;
        bool clue_satisfied;
        int n = state->clues[i];
        if (n < 0)
            continue;

        yes_order = face_order(state, i, LINE_YES);
        if (state->exactly_one_loop) {
            /*
             * Special case: if the set of LINE_YES edges in the grid
             * consists of exactly one loop and nothing else, then we
             * switch to treating LINE_UNKNOWN the same as LINE_NO for
             * purposes of clue checking.
             *
             * This is because some people like to play Loopy without
             * using the right-click, i.e. never setting anything to
             * LINE_NO. Without this special case, if a person playing
             * in that style fills in what they think is a correct
             * solution loop but in fact it has an underfilled clue,
             * then we will display no victory flash and also no error
             * highlight explaining why not. With this special case,
             * we light up underfilled clues at the instant the loop
             * is closed. (Of course, *overfilled* clues are fine
             * either way.)
             *
             * (It might still be considered unfortunate that we can't
             * warn this style of player any earlier, if they make a
             * mistake very near the beginning which doesn't show up
             * until they close the last edge of the loop. One other
             * thing we _could_ do here is to treat any LINE_UNKNOWN
             * as LINE_NO if either of its endpoints has yes-degree 2,
             * reflecting the fact that setting that line to YES would
             * be an obvious error. But I don't think even that could
             * catch _all_ clue errors in a timely manner; I think
             * there are some that won't be displayed until the loop
             * is filled in, even so, and there's no way to avoid that
             * with complete reliability except to switch to being a
             * player who sets things to LINE_NO.)
             */
            no_order = sides - yes_order;
        } else {
            no_order = face_order(state, i, LINE_NO);
        }

        clue_mistake = (yes_order > n || no_order > (sides-n));
        clue_satisfied = (yes_order == n && no_order == (sides-n));

        if (clue_mistake != ds->clue_error[i] ||
            clue_satisfied != ds->clue_satisfied[i]) {
            ds->clue_error[i] = clue_mistake;
            ds->clue_satisfied[i] = clue_satisfied;
            if (nfaces == REDRAW_OBJECTS_LIMIT)
                redraw_everything = true;
            else
                faces[nfaces++] = i;
        }
    }

    /* Work out what the flash state needs to be. */
    if (flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3)) {
        flash_changed = !ds->flashing;
        ds->flashing = true;
    } else {
        flash_changed = ds->flashing;
        ds->flashing = false;
    }

    /* Now, trundle through the edges. */
    for (i = 0; i < g->num_edges; i++) {
        char new_ds =
            state->line_errors[i] ? DS_LINE_ERROR : state->lines[i];
        if (new_ds != ds->lines[i] ||
            (flash_changed && state->lines[i] == LINE_YES)) {
            ds->lines[i] = new_ds;
            if (nedges == REDRAW_OBJECTS_LIMIT)
                redraw_everything = true;
            else
                edges[nedges++] = i;
        }
    }

    /* Pass one is now done.  Now we do the actual drawing. */
    if (redraw_everything) {
        int grid_width = g->highest_x - g->lowest_x;
        int grid_height = g->highest_y - g->lowest_y;
        int w = grid_width * ds->tilesize / g->tilesize;
        int h = grid_height * ds->tilesize / g->tilesize;

        game_redraw_in_rect(dr, ds, ui, state,
                            0, 0, w + 2*border + 1, h + 2*border + 1);
    } else {

	/* Right.  Now we roll up our sleeves. */

	for (i = 0; i < nfaces; i++) {
	    grid_face *f = g->faces[faces[i]];
	    int x, y, w, h;

            face_text_bbox(ds, g, f, &x, &y, &w, &h);
            game_redraw_in_rect(dr, ds, ui, state, x, y, w, h);
	}

	for (i = 0; i < nedges; i++) {
	    grid_edge *e = g->edges[edges[i]];
            int x, y, w, h;

            edge_bbox(ds, g, e, &x, &y, &w, &h);
            game_redraw_in_rect(dr, ds, ui, state, x, y, w, h);
	}
    }

    ds->started = true;
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

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
}

static int game_status(const game_state *state)
{
    return state->solved ? +1 : 0;
}

static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 7mm "squares" by default.
     */
    game_compute_size(params, 700, ui, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
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
        grid_to_screen(ds, g, g->dots[i]->x, g->dots[i]->y, &x, &y);
        draw_circle(dr, x, y, ds->tilesize / 15, ink, ink);
    }

    /*
     * Clues.
     */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces[i];
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
        grid_edge *e = g->edges[i];
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

#ifdef COMBINED
#define thegame loopy
#endif

const struct game thegame = {
    "Loopy", "games.loopy", "loopy",
    default_params,
    NULL, game_preset_menu,
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
    get_prefs, set_prefs,
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
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
    true, false, game_print_size, game_print,
    false /* wants_statusbar */,
    false, NULL,                       /* timing_state */
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
    char *id = NULL, *desc;
    const char *err;
    bool grade = false;
    int ret, diff;
#if 0 /* verbose solver not supported here (yet) */
    bool really_verbose = false;
#endif

    while (--argc > 0) {
        char *p = *++argv;
#if 0 /* verbose solver not supported here (yet) */
        if (!strcmp(p, "-v")) {
            really_verbose = true;
        } else
#endif
	if (!strcmp(p, "-g")) {
            grade = true;
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
